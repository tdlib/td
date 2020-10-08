//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "data.h"

#include "td/net/HttpChunkedByteFlow.h"
#include "td/net/HttpHeaderCreator.h"
#include "td/net/HttpQuery.h"
#include "td/net/HttpReader.h"

#include "td/utils/AesCtrByteFlow.h"
#include "td/utils/base64.h"
#include "td/utils/buffer.h"
#include "td/utils/BufferedFd.h"
#include "td/utils/ByteFlow.h"
#include "td/utils/common.h"
#include "td/utils/crypto.h"
#include "td/utils/format.h"
#include "td/utils/Gzip.h"
#include "td/utils/GzipByteFlow.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/port/detail/PollableFd.h"
#include "td/utils/port/FileFd.h"
#include "td/utils/port/path.h"
#include "td/utils/port/PollFlags.h"
#include "td/utils/port/thread_local.h"
#include "td/utils/Random.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"
#include "td/utils/tests.h"
#include "td/utils/UInt.h"

#include <algorithm>
#include <limits>

REGISTER_TESTS(http)

using namespace td;

static string make_chunked(string str) {
  auto v = rand_split(str);
  string res;
  for (auto &s : v) {
    res += PSTRING() << format::as_hex_dump(static_cast<int32>(s.size()));
    res += "\r\n";
    res += s;
    res += "\r\n";
  }
  res += "0\r\n\r\n";
  return res;
}

static string gen_http_content() {
  int t = Random::fast(0, 2);
  int len;
  if (t == 0) {
    len = Random::fast(1, 10);
  } else if (t == 1) {
    len = Random::fast(100, 200);
  } else {
    len = Random::fast(1000, 20000);
  }
  return rand_string(std::numeric_limits<char>::min(), std::numeric_limits<char>::max(), len);
}

static string make_http_query(string content, bool is_chunked, bool is_gzip, double gzip_k = 5,
                              string zip_override = "") {
  HttpHeaderCreator hc;
  hc.init_post("/");
  hc.add_header("jfkdlsahhjk", rand_string('a', 'z', Random::fast(1, 2000)));
  if (is_gzip) {
    BufferSlice zip;
    if (zip_override.empty()) {
      zip = gzencode(content, gzip_k);
    } else {
      zip = BufferSlice(zip_override);
    }
    if (!zip.empty()) {
      hc.add_header("content-encoding", "gzip");
      content = zip.as_slice().str();
    }
  }
  if (is_chunked) {
    hc.add_header("transfer-encoding", "chunked");
    content = make_chunked(content);
  } else {
    hc.set_content_size(content.size());
  }
  string res;
  auto r_header = hc.finish();
  CHECK(r_header.is_ok());
  res += r_header.ok().str();
  res += content;
  return res;
}

static string rand_http_query(string content) {
  bool is_chunked = Random::fast_bool();
  bool is_gzip = Random::fast_bool();
  return make_http_query(std::move(content), is_chunked, is_gzip);
}

static string join(const std::vector<string> &v) {
  string res;
  for (auto &s : v) {
    res += s;
  }
  return res;
}

TEST(Http, stack_overflow) {
  ChainBufferWriter writer;
  BufferSlice slice(string(256, 'A'));
  for (int i = 0; i < 1000000; i++) {
    ChainBufferWriter tmp_writer;
    writer.append(slice.clone());
  }
  {
    auto reader = writer.extract_reader();
    reader.sync_with_writer();
  }
}

TEST(Http, reader) {
#if TD_ANDROID || TD_TIZEN
  return;
#endif
  clear_thread_locals();
  SET_VERBOSITY_LEVEL(VERBOSITY_NAME(ERROR));
  auto start_mem = BufferAllocator::get_buffer_mem();
  auto start_size = BufferAllocator::get_buffer_slice_size();
  {
    BufferSlice a("test test");
    BufferSlice b = std::move(a);
#if TD_CLANG
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunknown-pragmas"
#pragma clang diagnostic ignored "-Wunknown-warning-option"
#pragma clang diagnostic ignored "-Wself-move"
#endif
    a = std::move(a);
    b = std::move(b);
#if TD_CLANG
#pragma clang diagnostic pop
#endif
    a = std::move(b);
    BufferSlice c = a.from_slice(a);
    CHECK(c.size() == a.size());
  }
  clear_thread_locals();
  ASSERT_EQ(start_mem, BufferAllocator::get_buffer_mem());
  ASSERT_EQ(start_size, BufferAllocator::get_buffer_slice_size());
  for (int i = 0; i < 20; i++) {
    td::ChainBufferWriter input_writer;
    auto input = input_writer.extract_reader();
    HttpReader reader;
    int max_post_size = 10000;
    reader.init(&input, max_post_size, 0);

    std::vector<string> contents(100);
    std::generate(contents.begin(), contents.end(), gen_http_content);
    auto v = td::transform(contents, rand_http_query);
    auto vec_str = rand_split(join(v));

    HttpQuery q;
    std::vector<string> res;
    for (auto &str : vec_str) {
      input_writer.append(str);
      input.sync_with_writer();
      while (true) {
        auto r_state = reader.read_next(&q);
        LOG_IF(ERROR, r_state.is_error()) << r_state.error() << tag("ok", res.size());
        ASSERT_TRUE(r_state.is_ok());
        auto state = r_state.ok();
        if (state == 0) {
          if (q.files_.empty()) {
            ASSERT_TRUE(td::narrow_cast<int>(q.content_.size()) <= max_post_size);
            auto expected = contents[res.size()];
            ASSERT_EQ(expected, q.content_.str());
            res.push_back(q.content_.str());
          } else {
            auto r_fd = FileFd::open(q.files_[0].temp_file_name, FileFd::Read);
            ASSERT_TRUE(r_fd.is_ok());
            auto fd = r_fd.move_as_ok();
            string content(td::narrow_cast<size_t>(q.files_[0].size), '\0');
            auto r_size = fd.read(MutableSlice(content));
            ASSERT_TRUE(r_size.is_ok());
            ASSERT_TRUE(r_size.ok() == content.size());
            ASSERT_TRUE(td::narrow_cast<int>(content.size()) > max_post_size);
            ASSERT_EQ(contents[res.size()], content);
            res.push_back(content);
            fd.close();
          }
        } else {
          break;
        }
      }
    }
    ASSERT_EQ(contents.size(), res.size());
    ASSERT_EQ(contents, res);
  }
  clear_thread_locals();
  ASSERT_EQ(start_mem, BufferAllocator::get_buffer_mem());
  ASSERT_EQ(start_size, BufferAllocator::get_buffer_slice_size());
}

TEST(Http, gzip_bomb) {
#if TD_ANDROID || TD_TIZEN || TD_EMSCRIPTEN  // the test should be disabled on low-memory systems
  return;
#endif
  auto gzip_bomb_str =
      gzdecode(gzdecode(base64url_decode(Slice(gzip_bomb, gzip_bomb_size)).ok()).as_slice()).as_slice().str();

  auto query = make_http_query("", false, true, 0.01, gzip_bomb_str);
  auto parts = rand_split(query);
  td::ChainBufferWriter input_writer;
  auto input = input_writer.extract_reader();
  HttpReader reader;
  HttpQuery q;
  reader.init(&input, 100000000, 0);
  for (auto &part : parts) {
    input_writer.append(part);
    input.sync_with_writer();
    auto r_state = reader.read_next(&q);
    if (r_state.is_error()) {
      LOG(INFO) << r_state.error();
      return;
    }
    ASSERT_TRUE(r_state.ok() != 0);
  }
}

TEST(Http, aes_ctr_encode_decode_flow) {
  auto str = rand_string('a', 'z', 1000000);
  auto parts = rand_split(str);
  td::ChainBufferWriter input_writer;
  auto input = input_writer.extract_reader();
  ByteFlowSource source(&input);
  UInt256 key;
  UInt128 iv;
  Random::secure_bytes(key.raw, sizeof(key));
  Random::secure_bytes(iv.raw, sizeof(iv));
  AesCtrByteFlow aes_encode;
  aes_encode.init(key, iv);
  AesCtrByteFlow aes_decode;
  aes_decode.init(key, iv);
  ByteFlowSink sink;
  source >> aes_encode >> aes_decode >> sink;

  ASSERT_TRUE(!sink.is_ready());
  for (auto &part : parts) {
    input_writer.append(part);
    source.wakeup();
  }
  ASSERT_TRUE(!sink.is_ready());
  source.close_input(Status::OK());
  ASSERT_TRUE(sink.is_ready());
  LOG_IF(ERROR, sink.status().is_error()) << sink.status();
  ASSERT_TRUE(sink.status().is_ok());
  ASSERT_EQ(str, sink.result()->move_as_buffer_slice().as_slice().str());
}

TEST(Http, aes_file_encryption) {
  auto str = rand_string('a', 'z', 1000000);
  CSlice name = "test_encryption";
  unlink(name).ignore();
  UInt256 key;
  UInt128 iv;
  Random::secure_bytes(key.raw, sizeof(key));
  Random::secure_bytes(iv.raw, sizeof(iv));

  {
    BufferedFdBase<FileFd> fd(FileFd::open(name, FileFd::Write | FileFd::Create).move_as_ok());

    auto parts = rand_split(str);

    ChainBufferWriter output_writer;
    auto output_reader = output_writer.extract_reader();
    ByteFlowSource source(&output_reader);
    AesCtrByteFlow aes_encode;
    aes_encode.init(key, iv);
    ByteFlowSink sink;

    source >> aes_encode >> sink;
    fd.set_output_reader(sink.get_output());

    for (auto &part : parts) {
      output_writer.append(part);
      source.wakeup();
      fd.flush_write().ensure();
    }
    fd.close();
  }

  {
    BufferedFdBase<FileFd> fd(FileFd::open(name, FileFd::Read).move_as_ok());

    ChainBufferWriter input_writer;
    auto input_reader = input_writer.extract_reader();
    ByteFlowSource source(&input_reader);
    AesCtrByteFlow aes_encode;
    aes_encode.init(key, iv);
    ByteFlowSink sink;
    source >> aes_encode >> sink;
    fd.set_input_writer(&input_writer);

    fd.get_poll_info().add_flags(PollFlags::Read());
    while (can_read_local(fd)) {
      fd.flush_read(4096).ensure();
      source.wakeup();
    }

    fd.close();

    source.close_input(Status::OK());
    ASSERT_TRUE(sink.is_ready());
    LOG_IF(ERROR, sink.status().is_error()) << sink.status();
    ASSERT_TRUE(sink.status().is_ok());
    auto result = sink.result()->move_as_buffer_slice().as_slice().str();
    ASSERT_EQ(str, result);
  }
}

TEST(Http, chunked_flow) {
  auto str = rand_string('a', 'z', 100);
  auto parts = rand_split(make_chunked(str));
  td::ChainBufferWriter input_writer;
  auto input = input_writer.extract_reader();
  ByteFlowSource source(&input);
  HttpChunkedByteFlow chunked_flow;
  ByteFlowSink sink;
  source >> chunked_flow >> sink;

  for (auto &part : parts) {
    input_writer.append(part);
    source.wakeup();
  }
  source.close_input(Status::OK());
  ASSERT_TRUE(sink.is_ready());
  LOG_IF(ERROR, sink.status().is_error()) << sink.status();
  ASSERT_TRUE(sink.status().is_ok());
  auto res = sink.result()->move_as_buffer_slice().as_slice().str();
  ASSERT_EQ(str.size(), res.size());
  ASSERT_EQ(str, res);
}

TEST(Http, chunked_flow_error) {
  auto str = rand_string('a', 'z', 100000);
  for (int d = 1; d < 100; d += 10) {
    auto new_str = make_chunked(str);
    new_str.resize(str.size() - d);
    auto parts = rand_split(new_str);
    td::ChainBufferWriter input_writer;
    auto input = input_writer.extract_reader();
    ByteFlowSource source(&input);
    HttpChunkedByteFlow chunked_flow;
    ByteFlowSink sink;
    source >> chunked_flow >> sink;

    for (auto &part : parts) {
      input_writer.append(part);
      source.wakeup();
    }
    ASSERT_TRUE(!sink.is_ready());
    source.close_input(Status::OK());
    ASSERT_TRUE(sink.is_ready());
    ASSERT_TRUE(!sink.status().is_ok());
  }
}

TEST(Http, gzip_chunked_flow) {
  auto str = rand_string('a', 'z', 1000000);
  auto parts = rand_split(make_chunked(gzencode(str, 2.0).as_slice().str()));

  ChainBufferWriter input_writer;
  auto input = input_writer.extract_reader();
  ByteFlowSource source(&input);
  HttpChunkedByteFlow chunked_flow;
  GzipByteFlow gzip_flow(Gzip::Mode::Decode);
  ByteFlowSink sink;
  source >> chunked_flow >> gzip_flow >> sink;

  for (auto &part : parts) {
    input_writer.append(part);
    source.wakeup();
  }
  source.close_input(Status::OK());
  ASSERT_TRUE(sink.is_ready());
  LOG_IF(ERROR, sink.status().is_error()) << sink.status();
  ASSERT_TRUE(sink.status().is_ok());
  ASSERT_EQ(str, sink.result()->move_as_buffer_slice().as_slice().str());
}

TEST(Http, gzip_bomb_with_limit) {
  std::string gzip_bomb_str;
  {
    ChainBufferWriter input_writer;
    auto input = input_writer.extract_reader();
    GzipByteFlow gzip_flow(Gzip::Mode::Encode);
    ByteFlowSource source(&input);
    ByteFlowSink sink;
    source >> gzip_flow >> sink;

    std::string s(1 << 16, 'a');
    for (int i = 0; i < 1000; i++) {
      input_writer.append(s);
      source.wakeup();
    }
    source.close_input(Status::OK());
    ASSERT_TRUE(sink.is_ready());
    LOG_IF(ERROR, sink.status().is_error()) << sink.status();
    ASSERT_TRUE(sink.status().is_ok());
    gzip_bomb_str = sink.result()->move_as_buffer_slice().as_slice().str();
  }

  auto query = make_http_query("", false, true, 0.01, gzip_bomb_str);
  auto parts = rand_split(query);
  td::ChainBufferWriter input_writer;
  auto input = input_writer.extract_reader();
  HttpReader reader;
  HttpQuery q;
  reader.init(&input, 1000000);
  bool ok = false;
  for (auto &part : parts) {
    input_writer.append(part);
    input.sync_with_writer();
    auto r_state = reader.read_next(&q);
    if (r_state.is_error()) {
      LOG(FATAL) << r_state.error();
      return;
    } else if (r_state.ok() == 0) {
      ok = true;
    }
  }
  ASSERT_TRUE(ok);
}
