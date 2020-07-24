//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/net/HttpReader.h"

#include "td/utils/filesystem.h"
#include "td/utils/find_boundary.h"
#include "td/utils/format.h"
#include "td/utils/Gzip.h"
#include "td/utils/HttpUrl.h"
#include "td/utils/JsonBuilder.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/Parser.h"
#include "td/utils/PathView.h"
#include "td/utils/port/path.h"

#include <cstring>

namespace td {

constexpr const char HttpReader::TEMP_DIRECTORY_PREFIX[];

static size_t urldecode(Slice from, MutableSlice to, bool decode_plus_sign_as_space) {
  size_t to_i = 0;
  CHECK(to.size() >= from.size());
  for (size_t from_i = 0, n = from.size(); from_i < n; from_i++) {
    if (from[from_i] == '%' && from_i + 2 < n) {
      int high = hex_to_int(from[from_i + 1]);
      int low = hex_to_int(from[from_i + 2]);
      if (high < 16 && low < 16) {
        to[to_i++] = static_cast<char>(high * 16 + low);
        from_i += 2;
        continue;
      }
    }
    to[to_i++] = decode_plus_sign_as_space && from[from_i] == '+' ? ' ' : from[from_i];
  }
  return to_i;
}

static MutableSlice urldecode_inplace(MutableSlice str, bool decode_plus_sign_as_space) {
  size_t result_size = urldecode(str, str, decode_plus_sign_as_space);
  str.truncate(result_size);
  return str;
}

void HttpReader::init(ChainBufferReader *input, size_t max_post_size, size_t max_files) {
  input_ = input;
  state_ = State::ReadHeaders;
  headers_read_length_ = 0;
  content_length_ = 0;
  query_ = nullptr;
  max_post_size_ = max_post_size;
  max_files_ = max_files;
  total_parameters_length_ = 0;
  total_headers_length_ = 0;
}

Result<size_t> HttpReader::read_next(HttpQuery *query, bool can_be_slow) {
  if (query_ != query) {
    CHECK(query_ == nullptr);
    query_ = query;
  }
  size_t need_size = input_->size() + 1;
  while (true) {
    if (state_ != State::ReadHeaders) {
      gzip_flow_.wakeup();
      flow_source_.wakeup();
      if (flow_sink_.is_ready() && flow_sink_.status().is_error()) {
        if (!temp_file_.empty()) {
          clean_temporary_file();
        }
        return Status::Error(400, PSLICE() << "Bad Request: " << flow_sink_.status().message());
      }
      need_size = flow_source_.get_need_size();
      if (need_size == 0) {
        need_size = input_->size() + 1;
      }
    }
    switch (state_) {
      case State::ReadHeaders: {
        auto result = split_header();
        if (result.is_error() || result.ok() != 0) {
          return result;
        }
        if (transfer_encoding_.empty() && content_length_ == 0) {
          break;
        }

        flow_source_ = ByteFlowSource(input_);
        ByteFlowInterface *source = &flow_source_;
        if (transfer_encoding_.empty()) {
          content_length_flow_ = HttpContentLengthByteFlow(content_length_);
          *source >> content_length_flow_;
          source = &content_length_flow_;
        } else if (transfer_encoding_ == "chunked") {
          chunked_flow_ = HttpChunkedByteFlow();
          *source >> chunked_flow_;
          source = &chunked_flow_;
        } else {
          LOG(ERROR) << "Unsupported " << tag("transfer-encoding", transfer_encoding_);
          return Status::Error(501, "Unimplemented: unsupported transfer-encoding");
        }

        if (content_encoding_.empty()) {
        } else if (content_encoding_ == "gzip" || content_encoding_ == "deflate") {
          gzip_flow_ = GzipByteFlow(Gzip::Mode::Decode);
          GzipByteFlow::Options options;
          options.write_watermark.low = 0;
          options.write_watermark.high = max(max_post_size_, static_cast<size_t>(1 << 16));
          gzip_flow_.set_options(options);
          gzip_flow_.set_max_output_size(MAX_CONTENT_SIZE);
          *source >> gzip_flow_;
          source = &gzip_flow_;
        } else {
          LOG(WARNING) << "Unsupported " << tag("content-encoding", content_encoding_);
          return Status::Error(415, "Unsupported Media Type: unsupported content-encoding");
        }

        flow_sink_ = ByteFlowSink();
        *source >> flow_sink_;
        content_ = flow_sink_.get_output();

        if (content_length_ > MAX_CONTENT_SIZE) {
          return Status::Error(413, PSLICE() << "Request Entity Too Large: content length is " << content_length_);
        }

        if (content_type_lowercased_.find("multipart/form-data") != string::npos) {
          state_ = State::ReadMultipartFormData;

          const char *p = std::strstr(content_type_lowercased_.c_str(), "boundary");
          if (p == nullptr) {
            return Status::Error(400, "Bad Request: boundary not found");
          }
          p += 8;
          ptrdiff_t offset = p - content_type_lowercased_.c_str();
          p = static_cast<const char *>(
              std::memchr(content_type_.begin() + offset, '=', content_type_.size() - offset));
          if (p == nullptr) {
            return Status::Error(400, "Bad Request: boundary value not found");
          }
          p++;
          const char *end_p = static_cast<const char *>(std::memchr(p, ';', content_type_.end() - p));
          if (end_p == nullptr) {
            end_p = content_type_.end();
          }
          if (*p == '"' && p + 1 < end_p && end_p[-1] == '"') {
            p++;
            end_p--;
          }

          CHECK(p != nullptr);
          Slice boundary(p, static_cast<size_t>(end_p - p));
          if (boundary.empty() || boundary.size() > MAX_BOUNDARY_LENGTH) {
            return Status::Error(400, "Bad Request: boundary too big or empty");
          }

          boundary_ = "\r\n--" + boundary.str();
          form_data_parse_state_ = FormDataParseState::SkipPrologue;
          form_data_read_length_ = 0;
          form_data_skipped_length_ = 0;
        } else if (content_type_lowercased_.find("application/x-www-form-urlencoded") != string::npos ||
                   content_type_lowercased_.find("application/json") != string::npos) {
          state_ = State::ReadArgs;
        } else {
          form_data_skipped_length_ = 0;
          state_ = State::ReadContent;
        }
        continue;
      }
      case State::ReadContent: {
        if (content_->size() > max_post_size_) {
          state_ = State::ReadContentToFile;
          GzipByteFlow::Options options;
          options.write_watermark.low = 4 << 20;
          options.write_watermark.high = 8 << 20;
          gzip_flow_.set_options(options);
          continue;
        }
        if (flow_sink_.is_ready()) {
          CHECK(query_->container_.size() == 1u);
          query_->container_.emplace_back(content_->cut_head(content_->size()).move_as_buffer_slice());
          query_->content_ = query_->container_.back().as_slice();
          break;
        }

        return need_size;
      }
      case State::ReadContentToFile: {
        if (!can_be_slow) {
          return Status::Error("SLOW");
        }
        // save content to a file
        if (temp_file_.empty()) {
          auto file = open_temp_file("file");
          if (file.is_error()) {
            return Status::Error(500, "Internal Server Error: can't create temporary file");
          }
        }

        auto size = content_->size();
        bool restart = false;
        if (size > (1 << 20) || flow_sink_.is_ready()) {
          TRY_STATUS(save_file_part(content_->cut_head(size).move_as_buffer_slice()));
          restart = true;
        }
        if (flow_sink_.is_ready()) {
          query_->files_.emplace_back("file", "", content_type_.str(), file_size_, temp_file_name_);
          close_temp_file();
          break;
        }
        if (restart) {
          continue;
        }

        return need_size;
      }
      case State::ReadArgs: {
        auto size = content_->size();
        if (size > MAX_TOTAL_PARAMETERS_LENGTH - total_parameters_length_) {
          return Status::Error(413, "Request Entity Too Large: too much parameters");
        }

        if (flow_sink_.is_ready()) {
          query_->container_.emplace_back(content_->cut_head(size).move_as_buffer_slice());
          Status result;
          if (content_type_lowercased_.find("application/x-www-form-urlencoded") != string::npos) {
            result = parse_parameters(query_->container_.back().as_slice());
          } else {
            result = parse_json_parameters(query_->container_.back().as_slice());
          }
          if (result.is_error()) {
            if (result.code() == 413) {
              return std::move(result);
            }
            LOG(INFO) << result.message();
          }
          query_->content_ = MutableSlice();
          break;
        }

        return need_size;
      }
      case State::ReadMultipartFormData: {
        if (!content_->empty()) {
          TRY_RESULT(result, parse_multipart_form_data(can_be_slow));
          if (result) {
            break;
          }
        }
        return need_size;
      }
      default:
        UNREACHABLE();
    }
    break;
  }

  init(input_, max_post_size_, max_files_);
  return 0;
}

// returns Status on wrong request
// returns true if parsing has finished
// returns false if need more data
Result<bool> HttpReader::parse_multipart_form_data(bool can_be_slow) {
  while (true) {
    LOG(DEBUG) << "Parsing multipart form data in state " << static_cast<int32>(form_data_parse_state_)
               << " with already read length " << form_data_read_length_;
    switch (form_data_parse_state_) {
      case FormDataParseState::SkipPrologue:
        if (find_boundary(content_->clone(), {boundary_.c_str() + 2, boundary_.size() - 2}, form_data_read_length_)) {
          size_t to_skip = form_data_read_length_ + (boundary_.size() - 2);
          content_->advance(to_skip);
          form_data_skipped_length_ += to_skip;
          form_data_read_length_ = 0;

          form_data_parse_state_ = FormDataParseState::ReadPartHeaders;
          continue;
        }

        content_->advance(form_data_read_length_);
        form_data_skipped_length_ += form_data_read_length_;
        form_data_read_length_ = 0;
        return false;
      case FormDataParseState::ReadPartHeaders:
        if (find_boundary(content_->clone(), "\r\n\r\n", form_data_read_length_)) {
          total_headers_length_ += form_data_read_length_;
          if (total_headers_length_ > MAX_TOTAL_HEADERS_LENGTH) {
            return Status::Error(431, "Request Header Fields Too Large: total headers size exceeded");
          }
          if (form_data_read_length_ == 0) {
            // there is no headers at all
            return Status::Error(400, "Bad Request: headers in multipart/form-data are empty");
          }

          content_->advance(2);  // "\r\n" after boundary
          auto headers = content_->cut_head(form_data_read_length_).move_as_buffer_slice();
          CHECK(headers.as_slice().size() == form_data_read_length_);
          LOG(DEBUG) << "Parse headers in multipart form data: \"" << headers.as_slice() << "\"";
          content_->advance(2);

          form_data_skipped_length_ += form_data_read_length_ + 4;
          form_data_read_length_ = 0;

          field_name_ = MutableSlice();
          file_field_name_.clear();
          field_content_type_ = "application/octet-stream";
          file_name_.clear();
          has_file_name_ = false;
          CHECK(temp_file_.empty());
          temp_file_name_.clear();

          Parser headers_parser(headers.as_slice());
          while (headers_parser.status().is_ok() && !headers_parser.data().empty()) {
            MutableSlice header_name = headers_parser.read_till(':');
            headers_parser.skip(':');
            char *header_value_begin = headers_parser.ptr();
            char *header_value_end;
            do {
              headers_parser.read_till('\r');
              header_value_end = headers_parser.ptr();
              headers_parser.skip('\r');
              headers_parser.skip('\n');
            } while (headers_parser.status().is_ok() &&
                     (headers_parser.peek_char() == ' ' || headers_parser.peek_char() == '\t'));

            MutableSlice header_value(header_value_begin, header_value_end);

            header_name = trim(header_name);
            header_value = trim(header_value);
            to_lower_inplace(header_name);

            if (header_name == "content-disposition") {
              if (header_value.substr(0, 10) != "form-data;") {
                return Status::Error(400, "Bad Request: expected form-data content disposition");
              }
              header_value.remove_prefix(10);
              while (true) {
                header_value = trim(header_value);
                const char *key_end =
                    static_cast<const char *>(std::memchr(header_value.data(), '=', header_value.size()));
                if (key_end == nullptr) {
                  break;
                }
                size_t key_size = key_end - header_value.data();
                auto key = header_value.substr(0, key_size);
                key = trim(key);

                header_value.remove_prefix(key_size + 1);
                const char *value_end =
                    static_cast<const char *>(std::memchr(header_value.data(), ';', header_value.size()));
                size_t value_size;
                if (value_end == nullptr) {
                  value_size = header_value.size();
                } else {
                  value_size = value_end - header_value.data();
                }
                auto value = header_value.substr(0, value_size);
                value = trim(value);
                if (value.size() > 1u && value[0] == '"' && value.back() == '"') {
                  value = {value.data() + 1, value.size() - 2};
                }
                header_value.remove_prefix(value_size + (header_value.size() > value_size));

                if (key == "name") {
                  field_name_ = value;
                } else if (key == "filename") {
                  file_name_ = value.str();
                  has_file_name_ = true;
                } else {
                  // ignore unknown parts of header
                }
              }
            } else if (header_name == "content-type") {
              field_content_type_ = header_value.str();
            } else {
              // ignore unknown header
            }
          }

          if (headers_parser.status().is_error()) {
            return Status::Error(400, "Bad Request: can't parse form data headers");
          }

          if (field_name_.empty()) {
            return Status::Error(400, "Bad Request: field name in multipart/form-data not found");
          }

          if (has_file_name_) {
            // file
            if (query_->files_.size() == max_files_) {
              return Status::Error(413, "Request Entity Too Large: too much files attached");
            }
            auto file = open_temp_file(file_name_);
            if (file.is_error()) {
              return Status::Error(500, "Internal Server Error: can't create temporary file");
            }

            // don't need to save headers for files
            file_field_name_ = field_name_.str();
            form_data_parse_state_ = FormDataParseState::ReadFile;
          } else {
            // save headers for query parameters. They contain header names
            query_->container_.push_back(std::move(headers));
            form_data_parse_state_ = FormDataParseState::ReadPartValue;
          }

          continue;
        }

        if (total_headers_length_ + form_data_read_length_ > MAX_TOTAL_HEADERS_LENGTH) {
          return Status::Error(431, "Request Header Fields Too Large: total headers size exceeded");
        }
        return false;
      case FormDataParseState::ReadPartValue:
        if (find_boundary(content_->clone(), boundary_, form_data_read_length_)) {
          if (total_parameters_length_ + form_data_read_length_ > MAX_TOTAL_PARAMETERS_LENGTH) {
            return Status::Error(413, "Request Entity Too Large: too much parameters in form data");
          }

          query_->container_.emplace_back(content_->cut_head(form_data_read_length_).move_as_buffer_slice());
          MutableSlice value = query_->container_.back().as_slice();
          content_->advance(boundary_.size());
          form_data_skipped_length_ += form_data_read_length_ + boundary_.size();
          form_data_read_length_ = 0;

          if (begins_with(field_content_type_, "application/x-www-form-urlencoded")) {
            // treat value as ordinary parameters
            auto result = parse_parameters(value);
            if (result.is_error()) {
              return std::move(result);
            }
          } else {
            total_parameters_length_ += form_data_read_length_;
            LOG(DEBUG) << "Get ordinary parameter in multipart form data: \"" << field_name_ << "\": \"" << value
                       << "\"";
            query_->args_.emplace_back(field_name_, value);
          }

          form_data_parse_state_ = FormDataParseState::CheckForLastBoundary;
          continue;
        }
        CHECK(content_->size() < form_data_read_length_ + boundary_.size());

        if (total_parameters_length_ + form_data_read_length_ > MAX_TOTAL_PARAMETERS_LENGTH) {
          return Status::Error(413, "Request Entity Too Large: too much parameters in form data");
        }
        return false;
      case FormDataParseState::ReadFile: {
        if (!can_be_slow) {
          return Status::Error("SLOW");
        }
        if (find_boundary(content_->clone(), boundary_, form_data_read_length_)) {
          auto file_part = content_->cut_head(form_data_read_length_).move_as_buffer_slice();
          content_->advance(boundary_.size());
          form_data_skipped_length_ += form_data_read_length_ + boundary_.size();
          form_data_read_length_ = 0;

          TRY_STATUS(save_file_part(std::move(file_part)));

          query_->files_.emplace_back(file_field_name_, file_name_, field_content_type_, file_size_, temp_file_name_);
          close_temp_file();

          form_data_parse_state_ = FormDataParseState::CheckForLastBoundary;
          continue;
        }

        // TODO optimize?
        auto file_part = content_->cut_head(form_data_read_length_).move_as_buffer_slice();
        form_data_skipped_length_ += form_data_read_length_;
        form_data_read_length_ = 0;
        CHECK(content_->size() < boundary_.size());

        TRY_STATUS(save_file_part(std::move(file_part)));
        return false;
      }
      case FormDataParseState::CheckForLastBoundary: {
        if (content_->size() < 2) {
          // need more data
          return false;
        }

        auto range = content_->clone();
        char x[2];
        range.advance(2, {x, 2});
        if (x[0] == '-' && x[1] == '-') {
          content_->advance(2);
          form_data_skipped_length_ += 2;
          form_data_parse_state_ = FormDataParseState::SkipEpilogue;
        } else {
          form_data_parse_state_ = FormDataParseState::ReadPartHeaders;
        }
        continue;
      }
      case FormDataParseState::SkipEpilogue: {
        size_t size = content_->size();
        LOG(DEBUG) << "Skipping epilogue. Have " << size << " bytes";
        content_->advance(size);
        form_data_skipped_length_ += size;
        // TODO(now): check if form_data_skipped_length is too big
        return flow_sink_.is_ready();
      }
      default:
        UNREACHABLE();
    }
    break;
  }

  return true;
}

Result<size_t> HttpReader::split_header() {
  if (find_boundary(input_->clone(), "\r\n\r\n", headers_read_length_)) {
    query_->container_.clear();
    auto a = input_->cut_head(headers_read_length_ + 2);
    auto b = a.move_as_buffer_slice();
    query_->container_.emplace_back(std::move(b));
    // query_->container_.emplace_back(input_->cut_head(headers_read_length_ + 2).move_as_buffer_slice());
    CHECK(query_->container_.back().size() == headers_read_length_ + 2);
    input_->advance(2);
    total_headers_length_ = headers_read_length_;
    auto status = parse_head(query_->container_.back().as_slice());
    if (status.is_error()) {
      return std::move(status);
    }
    return 0;
  }

  if (input_->size() > MAX_TOTAL_HEADERS_LENGTH) {
    return Status::Error(431, "Request Header Fields Too Large: total headers size exceeded");
  }
  return input_->size() + 1;
}

void HttpReader::process_header(MutableSlice header_name, MutableSlice header_value) {
  header_name = trim(header_name);
  header_value = trim(header_value);  // TODO need to remove "\r\n" from value
  to_lower_inplace(header_name);
  LOG(DEBUG) << "Process header [" << header_name << "=>" << header_value << "]";
  query_->headers_.emplace_back(header_name, header_value);
  // TODO: check if protocol is HTTP/1.1
  query_->keep_alive_ = true;
  if (header_name == "content-length") {
    content_length_ = to_integer<size_t>(header_value);
  } else if (header_name == "connection") {
    to_lower_inplace(header_value);
    if (header_value == "close") {
      query_->keep_alive_ = false;
    }
  } else if (header_name == "content-type") {
    content_type_ = header_value;
    content_type_lowercased_ = header_value.str();
    to_lower_inplace(content_type_lowercased_);
  } else if (header_name == "content-encoding") {
    to_lower_inplace(header_value);
    content_encoding_ = header_value;
  } else if (header_name == "transfer-encoding") {
    to_lower_inplace(header_value);
    transfer_encoding_ = header_value;
  }
}

Status HttpReader::parse_url(MutableSlice url) {
  size_t url_path_size = 0;
  while (url_path_size < url.size() && url[url_path_size] != '?' && url[url_path_size] != '#') {
    url_path_size++;
  }

  query_->url_path_ = urldecode_inplace({url.data(), url_path_size}, false);

  if (url_path_size == url.size() || url[url_path_size] != '?') {
    return Status::OK();
  }
  return parse_parameters(url.substr(url_path_size + 1));
}

Status HttpReader::parse_parameters(MutableSlice parameters) {
  total_parameters_length_ += parameters.size();
  if (total_parameters_length_ > MAX_TOTAL_PARAMETERS_LENGTH) {
    return Status::Error(413, "Request Entity Too Large: too much parameters");
  }
  LOG(DEBUG) << "Parse parameters: \"" << parameters << "\"";

  Parser parser(parameters);
  while (!parser.data().empty()) {
    auto key_value = parser.read_till_nofail('&');
    parser.skip_nofail('&');
    Parser kv_parser(key_value);
    auto key = urldecode_inplace(kv_parser.read_till_nofail('='), true);
    kv_parser.skip_nofail('=');
    auto value = urldecode_inplace(kv_parser.data(), true);
    query_->args_.emplace_back(key, value);
  }

  CHECK(parser.status().is_ok());
  return Status::OK();
}

Status HttpReader::parse_json_parameters(MutableSlice parameters) {
  if (parameters.empty()) {
    return Status::OK();
  }

  total_parameters_length_ += parameters.size();
  if (total_parameters_length_ > MAX_TOTAL_PARAMETERS_LENGTH) {
    return Status::Error(413, "Request Entity Too Large: too much parameters");
  }
  LOG(DEBUG) << "Parse JSON parameters: \"" << parameters << "\"";

  Parser parser(parameters);
  parser.skip_whitespaces();
  if (parser.peek_char() == '"') {
    auto r_value = json_string_decode(parser);
    if (r_value.is_error()) {
      return Status::Error(400, PSLICE() << "Bad Request: can't parse string content: " << r_value.error().message());
    }
    if (!parser.empty()) {
      return Status::Error(400, "Bad Request: extra data after string");
    }
    query_->container_.emplace_back(BufferSlice("content"));
    query_->args_.emplace_back(query_->container_.back().as_slice(), r_value.move_as_ok());
    return Status::OK();
  }
  parser.skip('{');
  if (parser.status().is_error()) {
    return Status::Error(400, "Bad Request: JSON object expected");
  }
  while (true) {
    parser.skip_whitespaces();
    if (parser.try_skip('}')) {
      parser.skip_whitespaces();
      if (parser.empty()) {
        return Status::OK();
      }
      return Status::Error(400, "Bad Request: unexpected data after object end");
    }
    if (parser.empty()) {
      return Status::Error(400, "Bad Request: expected parameter name");
    }
    auto r_key = json_string_decode(parser);
    if (r_key.is_error()) {
      return Status::Error(400, PSLICE() << "Bad Request: can't parse parameter name: " << r_key.error().message());
    }
    parser.skip_whitespaces();
    if (!parser.try_skip(':')) {
      return Status::Error(400, "Bad Request: can't parse object, ':' expected");
    }
    parser.skip_whitespaces();
    auto r_value = [&]() -> Result<MutableSlice> {
      if (parser.peek_char() == '"') {
        return json_string_decode(parser);
      } else {
        const int32 DEFAULT_MAX_DEPTH = 100;
        auto begin = parser.ptr();
        auto result = do_json_skip(parser, DEFAULT_MAX_DEPTH);
        if (result.is_ok()) {
          return MutableSlice(begin, parser.ptr());
        } else {
          return result.move_as_error();
        }
      }
    }();
    if (r_value.is_error()) {
      return Status::Error(400, PSLICE() << "Bad Request: can't parse parameter value: " << r_value.error().message());
    }
    query_->args_.emplace_back(r_key.move_as_ok(), r_value.move_as_ok());

    parser.skip_whitespaces();
    if (parser.peek_char() != '}' && !parser.try_skip(',')) {
      return Status::Error(400, "Bad Request: expected next field or object end");
    }
  }
  UNREACHABLE();
  return Status::OK();
}

Status HttpReader::parse_head(MutableSlice head) {
  Parser parser(head);

  Slice type = parser.read_till(' ');
  parser.skip(' ');
  // GET POST HTTP/1.1
  if (type == "GET") {
    query_->type_ = HttpQuery::Type::Get;
  } else if (type == "POST") {
    query_->type_ = HttpQuery::Type::Post;
  } else if (type.size() >= 4 && type.substr(0, 4) == "HTTP") {
    if (type == "HTTP/1.1" || type == "HTTP/1.0") {
      query_->type_ = HttpQuery::Type::Response;
    } else {
      LOG(INFO) << "Unsupported HTTP version: " << type;
      return Status::Error(505, "HTTP Version Not Supported");
    }
  } else {
    LOG(INFO) << "Not Implemented " << tag("type", type) << tag("head", head);
    return Status::Error(501, "Not Implemented");
  }

  query_->args_.clear();

  if (query_->type_ == HttpQuery::Type::Response) {
    query_->code_ = to_integer<int32>(parser.read_till(' '));
    parser.skip(' ');
    query_->reason_ = parser.read_till('\r');
    LOG(DEBUG) << "Receive HTTP response " << query_->code_ << " " << query_->reason_;
  } else {
    auto url_version = parser.read_till('\r');
    auto space_pos = url_version.rfind(' ');
    if (space_pos == static_cast<size_t>(-1)) {
      return Status::Error(400, "Bad Request: wrong request line");
    }

    TRY_STATUS(parse_url(url_version.substr(0, space_pos)));

    auto http_version = url_version.substr(space_pos + 1);
    if (http_version != "HTTP/1.1" && http_version != "HTTP/1.0") {
      LOG(WARNING) << "Unsupported HTTP version: " << http_version;
      return Status::Error(505, "HTTP Version Not Supported");
    }
  }
  parser.skip('\r');
  parser.skip('\n');

  content_length_ = 0;
  content_type_ = Slice("application/octet-stream");
  content_type_lowercased_ = content_type_.str();
  transfer_encoding_ = Slice();
  content_encoding_ = Slice();

  query_->keep_alive_ = false;
  query_->headers_.clear();
  query_->files_.clear();
  query_->content_ = MutableSlice();
  while (parser.status().is_ok() && !parser.data().empty()) {
    MutableSlice header_name = parser.read_till(':');
    parser.skip(':');
    char *header_value_begin = parser.ptr();
    char *header_value_end;
    do {
      parser.read_till('\r');
      header_value_end = parser.ptr();
      parser.skip('\r');
      parser.skip('\n');
    } while (parser.status().is_ok() && (parser.peek_char() == ' ' || parser.peek_char() == '\t'));

    process_header(header_name, {header_value_begin, header_value_end});
  }
  return parser.status().is_ok() ? Status::OK() : Status::Error(400, "Bad Request");
}

Status HttpReader::open_temp_file(CSlice desired_file_name) {
  CHECK(temp_file_.empty());

  auto tmp_dir = get_temporary_dir();
  if (tmp_dir.empty()) {
    return Status::Error("Can't find temporary directory");
  }

  TRY_RESULT(dir, realpath(tmp_dir, true));
  CHECK(!dir.empty());

  auto first_try = try_open_temp_file(dir, desired_file_name);
  if (first_try.is_ok()) {
    return Status::OK();
  }

  // Creation of new file with desired name has failed. Trying to create unique directory for it
  TRY_RESULT(directory, mkdtemp(dir, TEMP_DIRECTORY_PREFIX));
  auto second_try = try_open_temp_file(directory, desired_file_name);
  if (second_try.is_ok()) {
    return Status::OK();
  }
  auto third_try = try_open_temp_file(directory, "file");
  if (third_try.is_ok()) {
    return Status::OK();
  }

  rmdir(directory).ignore();
  LOG(WARNING) << "Failed to create temporary file " << desired_file_name << ": " << second_try.error();
  return second_try.move_as_error();
}

Status HttpReader::try_open_temp_file(Slice directory_name, CSlice desired_file_name) {
  CHECK(temp_file_.empty());
  CHECK(!directory_name.empty());

  string file_name = clean_filename(desired_file_name);
  if (file_name.empty()) {
    file_name = "file";
  }

  temp_file_name_.clear();
  temp_file_name_.reserve(directory_name.size() + 1 + file_name.size());
  temp_file_name_.append(directory_name.data(), directory_name.size());
  if (temp_file_name_.back() != TD_DIR_SLASH) {
    temp_file_name_ += TD_DIR_SLASH;
  }
  temp_file_name_.append(file_name.data(), file_name.size());

  TRY_RESULT(opened_file, FileFd::open(temp_file_name_, FileFd::Write | FileFd::CreateNew, 0640));

  file_size_ = 0;
  temp_file_ = std::move(opened_file);
  LOG(DEBUG) << "Created temporary file " << temp_file_name_;
  return Status::OK();
}

Status HttpReader::save_file_part(BufferSlice &&file_part) {
  file_size_ += narrow_cast<int64>(file_part.size());
  if (file_size_ > MAX_FILE_SIZE) {
    clean_temporary_file();
    return Status::Error(
        413, PSLICE() << "Request Entity Too Large: file of size " << file_size_ << " is too big to be uploaded");
  }

  LOG(DEBUG) << "Save file part of size " << file_part.size() << " to file " << temp_file_name_;
  auto result_written = temp_file_.write(file_part.as_slice());
  if (result_written.is_error() || result_written.ok() != file_part.size()) {
    clean_temporary_file();
    return Status::Error(500, "Internal Server Error: can't upload the file");
  }
  return Status::OK();
}

void HttpReader::clean_temporary_file() {
  string file_name = temp_file_name_;
  close_temp_file();
  delete_temp_file(file_name);
}

void HttpReader::close_temp_file() {
  LOG(DEBUG) << "Close temporary file " << temp_file_name_;
  CHECK(!temp_file_.empty());
  temp_file_.close();
  CHECK(temp_file_.empty());
  temp_file_name_.clear();
}

void HttpReader::delete_temp_file(CSlice file_name) {
  CHECK(!file_name.empty());
  LOG(DEBUG) << "Unlink temporary file " << file_name;
  unlink(file_name).ignore();
  PathView path_view(file_name);
  Slice parent = path_view.parent_dir();
  const size_t prefix_length = std::strlen(TEMP_DIRECTORY_PREFIX);
  if (parent.size() >= prefix_length + 7 &&
      parent.substr(parent.size() - prefix_length - 7, prefix_length) == TEMP_DIRECTORY_PREFIX) {
    LOG(DEBUG) << "Unlink temporary directory " << parent;
    rmdir(PSLICE() << Slice(parent.data(), parent.size() - 1)).ignore();
  }
}

}  // namespace td
