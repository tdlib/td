//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/files/FileDownloader.h"

#include "td/telegram/telegram_api.h"

#include "td/telegram/FileReferenceManager.h"
#include "td/telegram/files/FileLoaderUtils.h"
#include "td/telegram/files/FileType.h"
#include "td/telegram/Global.h"
#include "td/telegram/net/DcId.h"
#include "td/telegram/SecureStorage.h"
#include "td/telegram/UniqueId.h"

#include "td/utils/as.h"
#include "td/utils/base64.h"
#include "td/utils/buffer.h"
#include "td/utils/common.h"
#include "td/utils/crypto.h"
#include "td/utils/format.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/port/path.h"
#include "td/utils/port/Stat.h"
#include "td/utils/ScopeGuard.h"
#include "td/utils/UInt.h"

#include <tuple>

namespace td {

FileDownloader::FileDownloader(const FullRemoteFileLocation &remote, const LocalFileLocation &local, int64 size,
                               string name, const FileEncryptionKey &encryption_key, bool is_small, bool search_file,
                               int64 offset, int64 limit, unique_ptr<Callback> callback)
    : remote_(remote)
    , local_(local)
    , size_(size)
    , name_(std::move(name))
    , encryption_key_(encryption_key)
    , callback_(std::move(callback))
    , is_small_(is_small)
    , search_file_(search_file)
    , offset_(offset)
    , limit_(limit) {
  if (encryption_key.is_secret()) {
    set_ordered_flag(true);
  }
  if (!encryption_key.empty()) {
    CHECK(offset_ == 0);
  }
}

Result<FileLoader::FileInfo> FileDownloader::init() {
  SCOPE_EXIT {
    try_release_fd();
  };
  if (local_.type() == LocalFileLocation::Type::Full) {
    return Status::Error("File is already downloaded");
  }
  if (encryption_key_.is_secure() && !encryption_key_.has_value_hash()) {
    LOG(ERROR) << "Can't download Secure file with unknown value_hash";
  }
  if (remote_.file_type_ == FileType::Secure) {
    size_ = 0;
  }
  int32 part_size = 0;
  Bitmask bitmask{Bitmask::Ones{}, 0};
  if (local_.type() == LocalFileLocation::Type::Partial) {
    const auto &partial = local_.partial();
    path_ = partial.path_;
    auto result_fd = FileFd::open(path_, FileFd::Write | FileFd::Read);
    // TODO: check timestamps..
    if (result_fd.is_ok()) {
      bitmask = Bitmask(Bitmask::Decode{}, partial.ready_bitmask_);
      if (encryption_key_.is_secret()) {
        LOG_CHECK(partial.iv_.size() == 32) << partial.iv_.size();
        encryption_key_.mutable_iv() = as<UInt256>(partial.iv_.data());
        next_part_ = narrow_cast<int32>(bitmask.get_ready_parts(0));
      }
      fd_ = result_fd.move_as_ok();
      part_size = partial.part_size_;
    }
  }
  if (search_file_ && fd_.empty() && size_ > 0 && size_ < 1000 * (1 << 20) && encryption_key_.empty() &&
      !remote_.is_web()) {
    [&] {
      TRY_RESULT(path, search_file(get_files_dir(remote_.file_type_), name_, size_));
      TRY_RESULT(fd, FileFd::open(path, FileFd::Read));
      LOG(INFO) << "Check hash of local file " << path;
      path_ = std::move(path);
      fd_ = std::move(fd);
      need_check_ = true;
      only_check_ = true;
      part_size = 32 * (1 << 10);
      bitmask = Bitmask{Bitmask::Ones{}, (size_ + part_size - 1) / part_size};
      return Status::OK();
    }();
  }

  FileInfo res;
  res.size = size_;
  res.is_size_final = true;
  res.part_size = part_size;
  res.ready_parts = bitmask.as_vector();
  res.use_part_count_limit = false;
  res.only_check = only_check_;
  auto file_type = remote_.file_type_;
  res.need_delay =
      !is_small_ &&
      (file_type == FileType::VideoNote || file_type == FileType::Document || file_type == FileType::DocumentAsFile ||
       file_type == FileType::VoiceNote || file_type == FileType::Audio || file_type == FileType::Video ||
       file_type == FileType::Animation || (file_type == FileType::Encrypted && size_ > (1 << 20)));
  res.offset = offset_;
  res.limit = limit_;
  return res;
}

Status FileDownloader::on_ok(int64 size) {
  auto dir = get_files_dir(remote_.file_type_);

  std::string path;
  fd_.close();
  if (encryption_key_.is_secure()) {
    TRY_RESULT(file_path, open_temp_file(remote_.file_type_));
    string tmp_path;
    std::tie(std::ignore, tmp_path) = std::move(file_path);
    TRY_STATUS(secure_storage::decrypt_file(encryption_key_.secret(), encryption_key_.value_hash(), path_, tmp_path));
    unlink(path_).ignore();
    path_ = std::move(tmp_path);
    TRY_RESULT(path_stat, stat(path_));
    size = path_stat.size_;
  }
  if (only_check_) {
    path = path_;
  } else {
    TRY_RESULT_ASSIGN(path, create_from_temp(path_, dir, name_));
  }
  callback_->on_ok(FullLocalFileLocation(remote_.file_type_, std::move(path), 0), size, !only_check_);
  return Status::OK();
}

void FileDownloader::on_error(Status status) {
  fd_.close();
  callback_->on_error(std::move(status));
}

Result<bool> FileDownloader::should_restart_part(Part part, NetQueryPtr &net_query) {
  // Check if we should use CDN or reupload file to CDN

  if (net_query->is_error()) {
    if (net_query->error().message() == "FILE_TOKEN_INVALID") {
      use_cdn_ = false;
      return true;
    }
    if (net_query->error().message() == "REQUEST_TOKEN_INVALID") {
      return true;
    }
    return false;
  }

  switch (narrow_cast<QueryType>(UniqueId::extract_key(net_query->id()))) {
    case QueryType::Default: {
      if (net_query->ok_tl_constructor() == telegram_api::upload_fileCdnRedirect::ID) {
        TRY_RESULT(file_base, fetch_result<telegram_api::upload_getFile>(net_query->ok()));
        CHECK(file_base->get_id() == telegram_api::upload_fileCdnRedirect::ID);
        auto file = move_tl_object_as<telegram_api::upload_fileCdnRedirect>(file_base);
        LOG(DEBUG) << part.id << " got REDIRECT " << to_string(file);

        auto new_cdn_file_token = file->file_token_.as_slice();
        if (cdn_file_token_ == new_cdn_file_token) {
          return true;
        }

        use_cdn_ = true;
        need_check_ = true;
        cdn_file_token_generation_++;
        cdn_file_token_ = new_cdn_file_token.str();
        cdn_dc_id_ = DcId::external(file->dc_id_);
        cdn_encryption_key_ = file->encryption_key_.as_slice().str();
        cdn_encryption_iv_ = file->encryption_iv_.as_slice().str();
        add_hash_info(file->file_hashes_);
        if (cdn_encryption_iv_.size() != 16 || cdn_encryption_key_.size() != 32) {
          return Status::Error("Wrong ctr key or iv size");
        }

        return true;
      }
      return false;
    }
    case QueryType::ReuploadCDN: {
      TRY_RESULT(file_hashes, fetch_result<telegram_api::upload_reuploadCdnFile>(net_query->ok()));
      add_hash_info(file_hashes);
      LOG(DEBUG) << part.id << " got REUPLOAD_OK";
      return true;
    }
    case QueryType::CDN: {
      if (net_query->ok_tl_constructor() == telegram_api::upload_cdnFileReuploadNeeded::ID) {
        TRY_RESULT(file_base, fetch_result<telegram_api::upload_getCdnFile>(net_query->ok()));
        CHECK(file_base->get_id() == telegram_api::upload_cdnFileReuploadNeeded::ID);
        auto file = move_tl_object_as<telegram_api::upload_cdnFileReuploadNeeded>(file_base);
        LOG(DEBUG) << part.id << " got REUPLOAD " << to_string(file);
        cdn_part_reupload_token_[part.id] = file->request_token_.as_slice().str();
        return true;
      }
      auto it = cdn_part_file_token_generation_.find(part.id);
      CHECK(it != cdn_part_file_token_generation_.end());
      if (it->second != cdn_file_token_generation_) {
        LOG(DEBUG) << part.id << " got part with old file_token";
        return true;
      }
      return false;
    }
    default:
      UNREACHABLE();
  }

  return false;
}

Result<std::pair<NetQueryPtr, bool>> FileDownloader::start_part(Part part, int32 part_count, int64 streaming_offset) {
  if (encryption_key_.is_secret()) {
    part.size = (part.size + 15) & ~15;  // fix for last part
  }
  // auto size = part.size;
  //// sometimes we can ask more than server has, just to check size
  // if (size < get_part_size()) {
  // size = min(size + 16, get_part_size());
  // LOG(INFO) << "Ask " << size << " instead of " << part.size;
  //}
  auto size = get_part_size();
  CHECK(part.size <= size);

  callback_->on_start_download();

  auto net_query_type = is_small_ ? NetQuery::Type::DownloadSmall : NetQuery::Type::Download;
  NetQueryPtr net_query;
  if (!use_cdn_) {
    int32 flags = 0;
#if !TD_EMSCRIPTEN
    // CDN is supported, unless we use domains instead of IPs from a browser
    if (streaming_offset == 0) {
      flags |= telegram_api::upload_getFile::CDN_SUPPORTED_MASK;
    }
#endif
    DcId dc_id = remote_.is_web() ? G()->get_webfile_dc_id() : remote_.get_dc_id();
    auto id = UniqueId::next(UniqueId::Type::Default, static_cast<uint8>(QueryType::Default));
    net_query = remote_.is_web()
                    ? G()->net_query_creator().create(
                          id,
                          telegram_api::upload_getWebFile(remote_.as_input_web_file_location(),
                                                          static_cast<int32>(part.offset), static_cast<int32>(size)),
                          dc_id, net_query_type, NetQuery::AuthFlag::On)
                    : G()->net_query_creator().create(
                          id,
                          telegram_api::upload_getFile(flags, false /*ignored*/, false /*ignored*/,
                                                       remote_.as_input_file_location(),
                                                       static_cast<int32>(part.offset), static_cast<int32>(size)),
                          dc_id, net_query_type, NetQuery::AuthFlag::On);
  } else {
    if (remote_.is_web()) {
      return Status::Error("Can't download web file from CDN");
    }
    auto it = cdn_part_reupload_token_.find(part.id);
    if (it == cdn_part_reupload_token_.end()) {
      auto query = telegram_api::upload_getCdnFile(BufferSlice(cdn_file_token_), static_cast<int32>(part.offset),
                                                   static_cast<int32>(size));
      cdn_part_file_token_generation_[part.id] = cdn_file_token_generation_;
      LOG(DEBUG) << part.id << " " << to_string(query);
      net_query =
          G()->net_query_creator().create(UniqueId::next(UniqueId::Type::Default, static_cast<uint8>(QueryType::CDN)),
                                          query, cdn_dc_id_, net_query_type, NetQuery::AuthFlag::Off);
    } else {
      auto query = telegram_api::upload_reuploadCdnFile(BufferSlice(cdn_file_token_), BufferSlice(it->second));
      LOG(DEBUG) << part.id << " " << to_string(query);
      net_query = G()->net_query_creator().create(
          UniqueId::next(UniqueId::Type::Default, static_cast<uint8>(QueryType::ReuploadCDN)), query,
          remote_.get_dc_id(), net_query_type, NetQuery::AuthFlag::On);
      cdn_part_reupload_token_.erase(it);
    }
  }
  net_query->file_type_ = narrow_cast<int32>(remote_.file_type_);
  return std::make_pair(std::move(net_query), false);
}

Status FileDownloader::check_net_query(NetQueryPtr &net_query) {
  if (net_query->is_error()) {
    auto error = net_query->move_as_error();
    if (FileReferenceManager::is_file_reference_error(error)) {
      VLOG(file_references) << "Receive " << error << " for being downloaded file";
      error = Status::Error(error.code(),
                            PSLICE() << error.message() << "#BASE64" << base64_encode(remote_.get_file_reference()));
    }
    return error;
  }
  return Status::OK();
}

Result<size_t> FileDownloader::process_part(Part part, NetQueryPtr net_query) {
  TRY_STATUS(check_net_query(net_query));

  BufferSlice bytes;
  bool need_cdn_decrypt = false;
  auto query_type = narrow_cast<QueryType>(UniqueId::extract_key(net_query->id()));
  switch (query_type) {
    case QueryType::Default: {
      if (remote_.is_web()) {
        TRY_RESULT(file, fetch_result<telegram_api::upload_getWebFile>(net_query->ok()));
        bytes = std::move(file->bytes_);
      } else {
        TRY_RESULT(file_base, fetch_result<telegram_api::upload_getFile>(net_query->ok()));
        CHECK(file_base->get_id() == telegram_api::upload_file::ID);
        auto file = move_tl_object_as<telegram_api::upload_file>(file_base);
        LOG(DEBUG) << part.id << " upload_getFile result " << to_string(file);
        bytes = std::move(file->bytes_);
      }
      break;
    }
    case QueryType::CDN: {
      TRY_RESULT(file_base, fetch_result<telegram_api::upload_getCdnFile>(net_query->ok()));
      CHECK(file_base->get_id() == telegram_api::upload_cdnFile::ID);
      auto file = move_tl_object_as<telegram_api::upload_cdnFile>(file_base);
      LOG(DEBUG) << part.id << " upload_getCdnFile result " << to_string(file);
      bytes = std::move(file->bytes_);
      need_cdn_decrypt = true;
      break;
    }
    default:
      UNREACHABLE();
  }

  auto padded_size = part.size;
  if (encryption_key_.is_secret()) {
    padded_size = (part.size + 15) & ~15;
  }
  if (bytes.size() > padded_size) {
    return Status::Error("Part size is more than requested");
  }
  if (bytes.empty()) {
    return 0;
  }

  // Encryption
  if (need_cdn_decrypt) {
    CHECK(part.offset % 16 == 0);
    auto offset = narrow_cast<uint32>(part.offset / 16);
    offset =
        ((offset & 0xff) << 24) | ((offset & 0xff00) << 8) | ((offset & 0xff0000) >> 8) | ((offset & 0xff000000) >> 24);

    AesCtrState ctr_state;
    string iv = cdn_encryption_iv_;
    as<uint32>(&iv[12]) = offset;
    ctr_state.init(cdn_encryption_key_, iv);
    ctr_state.decrypt(bytes.as_slice(), bytes.as_slice());
  }
  if (encryption_key_.is_secret()) {
    LOG_CHECK(next_part_ == part.id) << tag("expected part.id", next_part_) << "!=" << tag("part.id", part.id);
    CHECK(!next_part_stop_);
    next_part_++;
    if (part.size % 16 != 0) {
      next_part_stop_ = true;
    }
    aes_ige_decrypt(as_slice(encryption_key_.key()), as_slice(encryption_key_.mutable_iv()), bytes.as_slice(),
                    bytes.as_slice());
  }

  auto slice = bytes.as_slice().truncate(part.size);
  TRY_STATUS(acquire_fd());
  LOG(INFO) << "Got " << slice.size() << " bytes at offset " << part.offset << " for \"" << path_ << '"';
  TRY_RESULT(written, fd_.pwrite(slice, part.offset));
  LOG(INFO) << "Written " << written << " bytes";
  // may write less than part.size, when size of downloadable file is unknown
  if (written != slice.size()) {
    return Status::Error("Failed to save file part to the file");
  }
  return written;
}

void FileDownloader::on_progress(Progress progress) {
  if (progress.is_ready) {
    // do not send partial location. will lead to wrong local_size
    return;
  }
  if (progress.ready_size == 0 || path_.empty()) {
    return;
  }
  if (encryption_key_.empty() || encryption_key_.is_secure()) {
    callback_->on_partial_download(
        PartialLocalFileLocation{remote_.file_type_, progress.part_size, path_, "", std::move(progress.ready_bitmask)},
        progress.ready_size, progress.size);
  } else if (encryption_key_.is_secret()) {
    UInt256 iv;
    if (progress.ready_part_count == next_part_) {
      iv = encryption_key_.mutable_iv();
    } else {
      LOG(FATAL) << tag("ready_part_count", progress.ready_part_count) << tag("next_part", next_part_);
    }
    callback_->on_partial_download(PartialLocalFileLocation{remote_.file_type_, progress.part_size, path_,
                                                            as_slice(iv).str(), std::move(progress.ready_bitmask)},
                                   progress.ready_size, progress.size);
  } else {
    UNREACHABLE();
  }
}

FileLoader::Callback *FileDownloader::get_callback() {
  return static_cast<FileLoader::Callback *>(callback_.get());
}

Status FileDownloader::process_check_query(NetQueryPtr net_query) {
  has_hash_query_ = false;
  TRY_STATUS(check_net_query(net_query));
  TRY_RESULT(file_hashes, fetch_result<telegram_api::upload_getCdnFileHashes>(std::move(net_query)));
  add_hash_info(file_hashes);
  return Status::OK();
}

Result<FileLoader::CheckInfo> FileDownloader::check_loop(int64 checked_prefix_size, int64 ready_prefix_size,
                                                         bool is_ready) {
  if (!need_check_) {
    return CheckInfo{};
  }
  SCOPE_EXIT {
    try_release_fd();
  };
  CheckInfo info;
  while (checked_prefix_size < ready_prefix_size) {
    //LOG(ERROR) << "NEED TO CHECK: " << checked_prefix_size << "->" << ready_prefix_size - checked_prefix_size;
    HashInfo search_info;
    search_info.offset = checked_prefix_size;
    auto it = hash_info_.upper_bound(search_info);
    if (it != hash_info_.begin()) {
      --it;
    }
    if (it != hash_info_.end() && it->offset <= checked_prefix_size &&
        it->offset + narrow_cast<int64>(it->size) > checked_prefix_size) {
      int64 begin_offset = it->offset;
      int64 end_offset = it->offset + narrow_cast<int64>(it->size);
      if (ready_prefix_size < end_offset) {
        if (!is_ready) {
          break;
        }
        end_offset = ready_prefix_size;
      }
      size_t size = narrow_cast<size_t>(end_offset - begin_offset);
      auto slice = BufferSlice(size);
      TRY_STATUS(acquire_fd());
      TRY_RESULT(read_size, fd_.pread(slice.as_slice(), begin_offset));
      if (size != read_size) {
        return Status::Error("Failed to read file to check hash");
      }
      string hash(32, ' ');
      sha256(slice.as_slice(), hash);

      if (hash != it->hash) {
        if (only_check_) {
          return Status::Error("FILE_DOWNLOAD_RESTART");
        }
        return Status::Error("Hash mismatch");
      }

      checked_prefix_size = end_offset;
      info.changed = true;
      continue;
    }
    if (!has_hash_query_) {
      has_hash_query_ = true;
      auto query =
          telegram_api::upload_getFileHashes(remote_.as_input_file_location(), narrow_cast<int32>(checked_prefix_size));
      auto net_query_type = is_small_ ? NetQuery::Type::DownloadSmall : NetQuery::Type::Download;
      auto net_query = G()->net_query_creator().create(query, remote_.get_dc_id(), net_query_type);
      info.queries.push_back(std::move(net_query));
      break;
    }
    // Should fail?
    break;
  }
  info.need_check = need_check_;
  info.checked_prefix_size = checked_prefix_size;
  return std::move(info);
}

void FileDownloader::add_hash_info(const std::vector<telegram_api::object_ptr<telegram_api::fileHash>> &hashes) {
  for (auto &hash : hashes) {
    //LOG(ERROR) << "ADD HASH " << hash->offset_ << "->" << hash->limit_;
    HashInfo hash_info;
    hash_info.size = hash->limit_;
    hash_info.offset = hash->offset_;
    hash_info.hash = hash->hash_.as_slice().str();
    hash_info_.insert(std::move(hash_info));
  }
}

void FileDownloader::keep_fd_flag(bool keep_fd) {
  keep_fd_ = keep_fd;
  try_release_fd();
}

void FileDownloader::try_release_fd() {
  if (!keep_fd_ && !fd_.empty()) {
    fd_.close();
  }
}

Status FileDownloader::acquire_fd() {
  if (fd_.empty()) {
    if (path_.empty()) {
      TRY_RESULT_ASSIGN(std::tie(fd_, path_), open_temp_file(remote_.file_type_));
    } else {
      TRY_RESULT_ASSIGN(fd_, FileFd::open(path_, (only_check_ ? 0 : FileFd::Write) | FileFd::Read));
    }
  }
  return Status::OK();
}

}  // namespace td
