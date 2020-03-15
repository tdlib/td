//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/files/FileUploader.h"

#include "td/telegram/telegram_api.h"

#include "td/telegram/files/FileLoaderUtils.h"

#include "td/telegram/Global.h"
#include "td/telegram/net/DcId.h"
#include "td/telegram/net/NetQueryDispatcher.h"
#include "td/telegram/SecureStorage.h"

#include "td/utils/buffer.h"
#include "td/utils/common.h"
#include "td/utils/crypto.h"
#include "td/utils/format.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/port/path.h"
#include "td/utils/Random.h"
#include "td/utils/ScopeGuard.h"

namespace td {

FileUploader::FileUploader(const LocalFileLocation &local, const RemoteFileLocation &remote, int64 expected_size,
                           const FileEncryptionKey &encryption_key, std::vector<int> bad_parts,
                           unique_ptr<Callback> callback)
    : local_(local)
    , remote_(remote)
    , expected_size_(expected_size)
    , encryption_key_(encryption_key)
    , bad_parts_(std::move(bad_parts))
    , callback_(std::move(callback)) {
  if (encryption_key_.is_secret()) {
    iv_ = encryption_key_.mutable_iv();
    generate_iv_ = encryption_key_.iv_slice().str();
  }
  if (remote_.type() == RemoteFileLocation::Type::Partial && encryption_key_.is_secure()) {
    remote_ = RemoteFileLocation{};
  }
}

Result<FileLoader::FileInfo> FileUploader::init() {
  if (remote_.type() == RemoteFileLocation::Type::Full) {
    return Status::Error("File is already uploaded");
  }

  // file_size is needed only for partial local locations, but for uploaded partial files
  // size is yet unknown or local location is full, so we can always pass 0 here
  TRY_RESULT(prefix_info, on_update_local_location(local_, 0));
  (void)prefix_info;

  int offset = 0;
  int part_size = 0;
  if (remote_.type() == RemoteFileLocation::Type::Partial) {
    const auto &partial = remote_.partial();
    file_id_ = partial.file_id_;
    part_size = partial.part_size_;
    big_flag_ = partial.is_big_ != 0;
    offset = partial.ready_part_count_;
  } else {
    file_id_ = Random::secure_int64();
    big_flag_ = is_file_big(file_type_, expected_size_);
  }

  std::vector<bool> ok(offset, true);
  for (auto bad_id : bad_parts_) {
    if (bad_id >= 0 && bad_id < offset) {
      ok[bad_id] = false;
    }
  }
  std::vector<int> parts;
  for (int i = 0; i < offset; i++) {
    if (ok[i]) {
      parts.push_back(i);
    }
  }
  if (!ok.empty() && !ok[0]) {
    parts.clear();
  }
  FileInfo res;
  res.size = local_size_;
  res.is_size_final = local_is_ready_;
  res.part_size = part_size;
  res.ready_parts = std::move(parts);
  res.is_upload = true;
  return res;
}

Result<FileLoader::PrefixInfo> FileUploader::on_update_local_location(const LocalFileLocation &location,
                                                                      int64 file_size) {
  SCOPE_EXIT {
    try_release_fd();
  };

  if (encryption_key_.is_secure() && !fd_path_.empty()) {
    return Status::Error("Can't change local location for Secure file");
  }

  string path;
  int64 local_size = -1;
  bool local_is_ready{false};
  FileType file_type{FileType::Temp};
  if (location.type() == LocalFileLocation::Type::Empty ||
      (location.type() == LocalFileLocation::Type::Partial && encryption_key_.is_secure())) {
    path = "";
    local_size = 0;
    local_is_ready = false;
    file_type = FileType::Temp;
  } else if (location.type() == LocalFileLocation::Type::Partial) {
    path = location.partial().path_;
    local_size = Bitmask(Bitmask::Decode{}, location.partial().ready_bitmask_)
                     .get_ready_prefix_size(0, location.partial().part_size_, file_size);
    local_is_ready = false;
    file_type = location.partial().file_type_;
  } else {
    path = location.full().path_;
    if (path.empty()) {
      return Status::Error("FullLocalFileLocation with empty path");
    }
    local_is_ready = true;
    file_type = location.full().file_type_;
  }

  LOG(INFO) << "In FileUploader::on_update_local_location with " << location << ". Have path = \"" << path
            << "\", local_size = " << local_size << ", local_is_ready = " << local_is_ready
            << " and file type = " << file_type;

  file_type_ = file_type;

  bool is_temp = false;
  if (encryption_key_.is_secure() && local_is_ready) {
    TRY_RESULT(file_fd_path, open_temp_file(FileType::Temp));
    file_fd_path.first.close();
    auto new_path = std::move(file_fd_path.second);
    TRY_RESULT(hash, secure_storage::encrypt_file(encryption_key_.secret(), path, new_path));
    LOG(INFO) << "ENCRYPT " << path << " " << new_path;
    callback_->on_hash(hash.as_slice().str());
    path = new_path;
    is_temp = true;
  }

  if (!path.empty() && (path != fd_path_ || fd_.empty())) {
    auto res_fd = FileFd::open(path, FileFd::Read);

    // Race: partial location could be already deleted. Just ignore such locations
    if (res_fd.is_error()) {
      if (location.type() == LocalFileLocation::Type::Partial) {
        LOG(INFO) << "Ignore partial local location: " << res_fd.error();
        PrefixInfo info;
        info.size = local_size_;
        info.is_ready = local_is_ready_;
        return info;
      }
      return res_fd.move_as_error();
    }

    fd_.close();
    fd_ = res_fd.move_as_ok();
    fd_path_ = path;
    is_temp_ = is_temp;
  }
  if (local_is_ready) {
    CHECK(!fd_.empty());
    TRY_RESULT_ASSIGN(local_size, fd_.get_size());
    LOG(INFO) << "Set file local_size to " << local_size;
    if (local_size == 0) {
      return Status::Error("Can't upload empty file");
    }
  } else if (!fd_.empty()) {
    TRY_RESULT(real_local_size, fd_.get_size());
    if (real_local_size < local_size) {
      LOG(ERROR) << tag("real_local_size", real_local_size) << " < " << tag("local_size", local_size);
      PrefixInfo info;
      info.size = local_size_;
      info.is_ready = local_is_ready_;
      return info;
    }
  }

  local_size_ = local_size;
  if (expected_size_ < local_size_) {
    expected_size_ = local_size_;
  }
  local_is_ready_ = local_is_ready;

  PrefixInfo info;
  info.size = local_size_;
  info.is_ready = local_is_ready_;
  return info;
}

Status FileUploader::on_ok(int64 size) {
  fd_.close();
  if (is_temp_) {
    LOG(INFO) << "UNLINK " << fd_path_;
    unlink(fd_path_).ignore();
  }
  return Status::OK();
}

void FileUploader::on_error(Status status) {
  fd_.close();
  if (is_temp_) {
    LOG(INFO) << "UNLINK " << fd_path_;
    unlink(fd_path_).ignore();
  }
  callback_->on_error(std::move(status));
}

Status FileUploader::generate_iv_map() {
  LOG(INFO) << "Generate iv_map " << generate_offset_ << " " << local_size_;
  auto part_size = get_part_size();
  auto encryption_key = FileEncryptionKey(encryption_key_.key_slice(), generate_iv_);
  BufferSlice bytes(part_size);
  if (iv_map_.empty()) {
    iv_map_.push_back(encryption_key.mutable_iv());
  }
  CHECK(!fd_.empty());
  for (; generate_offset_ + static_cast<int64>(part_size) < local_size_;
       generate_offset_ += static_cast<int64>(part_size)) {
    TRY_RESULT(read_size, fd_.pread(bytes.as_slice(), generate_offset_));
    if (read_size != part_size) {
      return Status::Error("Failed to read file part (for iv_map)");
    }
    aes_ige_encrypt(as_slice(encryption_key.key()), as_slice(encryption_key.mutable_iv()), bytes.as_slice(),
                    bytes.as_slice());
    iv_map_.push_back(encryption_key.mutable_iv());
  }
  generate_iv_ = encryption_key.iv_slice().str();
  return Status::OK();
}

Status FileUploader::before_start_parts() {
  auto status = acquire_fd();
  if (status.is_error() && !local_is_ready_) {
    return Status::Error(1, "Can't open temporary file");
  }
  return status;
}
void FileUploader::after_start_parts() {
  try_release_fd();
}

Result<std::pair<NetQueryPtr, bool>> FileUploader::start_part(Part part, int32 part_count, int64 streaming_offset) {
  auto padded_size = part.size;
  if (encryption_key_.is_secret()) {
    padded_size = (padded_size + 15) & ~15;
  }
  BufferSlice bytes(padded_size);
  TRY_RESULT(size, fd_.pread(bytes.as_slice().truncate(part.size), part.offset));
  if (encryption_key_.is_secret()) {
    Random::secure_bytes(bytes.as_slice().substr(part.size));
    if (next_offset_ == part.offset) {
      aes_ige_encrypt(as_slice(encryption_key_.key()), as_slice(iv_), bytes.as_slice(), bytes.as_slice());
      next_offset_ += static_cast<int64>(bytes.size());
    } else {
      if (part.id >= static_cast<int32>(iv_map_.size())) {
        TRY_STATUS(generate_iv_map());
      }
      CHECK(part.id < static_cast<int32>(iv_map_.size()) && part.id >= 0);
      auto iv = iv_map_[part.id];
      aes_ige_encrypt(as_slice(encryption_key_.key()), as_slice(iv), bytes.as_slice(), bytes.as_slice());
    }
  }

  if (size != part.size) {
    return Status::Error("Failed to read file part");
  }

  NetQueryPtr net_query;
  if (big_flag_) {
    auto query =
        telegram_api::upload_saveBigFilePart(file_id_, part.id, local_is_ready_ ? part_count : -1, std::move(bytes));
    net_query = G()->net_query_creator().create(query, DcId::main(), NetQuery::Type::Upload);
  } else {
    auto query = telegram_api::upload_saveFilePart(file_id_, part.id, std::move(bytes));
    net_query = G()->net_query_creator().create(query, DcId::main(), NetQuery::Type::Upload);
  }
  net_query->file_type_ = narrow_cast<int32>(file_type_);
  return std::make_pair(std::move(net_query), false);
}

Result<size_t> FileUploader::process_part(Part part, NetQueryPtr net_query) {
  if (net_query->is_error()) {
    return std::move(net_query->error());
  }
  Result<bool> result = [&] {
    if (big_flag_) {
      return fetch_result<telegram_api::upload_saveBigFilePart>(net_query->ok());
    } else {
      return fetch_result<telegram_api::upload_saveFilePart>(net_query->ok());
    }
  }();
  if (result.is_error()) {
    return result.move_as_error();
  }
  if (!result.ok()) {
    // TODO: it is possible
    return Status::Error(500, "Internal Server Error");
  }
  return part.size;
}

void FileUploader::on_progress(Progress progress) {
  callback_->on_partial_upload(PartialRemoteFileLocation{file_id_, progress.part_count, progress.part_size,
                                                         progress.ready_part_count, big_flag_},
                               progress.ready_size);
  if (progress.is_ready) {
    callback_->on_ok(file_type_,
                     PartialRemoteFileLocation{file_id_, progress.part_count, progress.part_size,
                                               progress.ready_part_count, big_flag_},
                     local_size_);
  }
}
FileLoader::Callback *FileUploader::get_callback() {
  return static_cast<FileLoader::Callback *>(callback_.get());
}

void FileUploader::keep_fd_flag(bool keep_fd) {
  keep_fd_ = keep_fd;
  try_release_fd();
}

void FileUploader::try_release_fd() {
  if (!keep_fd_ && !fd_.empty()) {
    fd_.close();
  }
}

Status FileUploader::acquire_fd() {
  if (fd_.empty()) {
    TRY_RESULT_ASSIGN(fd_, FileFd::open(fd_path_, FileFd::Read));
  }
  return Status::OK();
}

}  // namespace td
