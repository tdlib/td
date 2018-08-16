//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/files/FileManager.h"

#include "td/telegram/telegram_api.h"

#include "td/telegram/ConfigShared.h"
#include "td/telegram/files/FileLoaderUtils.h"
#include "td/telegram/files/FileLocation.h"
#include "td/telegram/Global.h"
#include "td/telegram/misc.h"
#include "td/telegram/Td.h"

#include "td/utils/base64.h"
#include "td/utils/format.h"
#include "td/utils/HttpUrl.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/PathView.h"
#include "td/utils/port/FileFd.h"
#include "td/utils/port/path.h"
#include "td/utils/port/Stat.h"
#include "td/utils/ScopeGuard.h"
#include "td/utils/tl_helpers.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <tuple>
#include <utility>

namespace td {

static int VERBOSITY_NAME(update_file) = VERBOSITY_NAME(DEBUG);

FileNode *FileNodePtr::operator->() const {
  return get();
}

FileNode &FileNodePtr::operator*() const {
  return *get();
}

FileNode *FileNodePtr::get() const {
  auto res = get_unsafe();
  CHECK(res);
  return res;
}

FullRemoteFileLocation *FileNodePtr::get_remote() const {
  return file_manager_->get_remote(file_id_.get_remote());
}

FileNode *FileNodePtr::get_unsafe() const {
  CHECK(file_manager_ != nullptr);
  return file_manager_->get_file_node_raw(file_id_);
}

FileNodePtr::operator bool() const {
  return file_manager_ != nullptr && get_unsafe() != nullptr;
}

void FileNode::set_local_location(const LocalFileLocation &local, int64 ready_size) {
  if (local_ready_size_ != ready_size) {
    local_ready_size_ = ready_size;
    VLOG(update_file) << "File " << main_file_id_ << " has changed local ready size";
    on_info_changed();
  }
  if (local_ != local) {
    VLOG(update_file) << "File " << main_file_id_ << " has changed local location";
    local_ = local;
    on_changed();
  }
}

void FileNode::set_remote_location(const RemoteFileLocation &remote, FileLocationSource source, int64 ready_size) {
  if (remote_ready_size_ != ready_size) {
    remote_ready_size_ = ready_size;
    VLOG(update_file) << "File " << main_file_id_ << " has changed remote ready size";
    on_info_changed();
  }
  if (remote_ == remote) {
    if (remote_.type() == RemoteFileLocation::Type::Full) {
      if (remote_.full().get_access_hash() == remote.full().get_access_hash()) {
        return;
      }
    } else {
      return;
    }
  }

  VLOG(update_file) << "File " << main_file_id_ << " has changed remote location";
  remote_ = remote;
  remote_source_ = source;
  on_changed();
}

void FileNode::set_generate_location(unique_ptr<FullGenerateFileLocation> &&generate) {
  bool is_changed = generate_ == nullptr ? generate != nullptr : generate == nullptr || *generate_ != *generate;
  if (is_changed) {
    generate_ = std::move(generate);
    on_pmc_changed();
  }
}

void FileNode::set_size(int64 size) {
  if (size_ != size) {
    VLOG(update_file) << "File " << main_file_id_ << " has changed size to " << size;
    size_ = size;
    on_changed();
  }
}

void FileNode::set_expected_size(int64 expected_size) {
  if (expected_size_ != expected_size) {
    VLOG(update_file) << "File " << main_file_id_ << " has changed expected size to " << expected_size;
    expected_size_ = expected_size;
    on_changed();
  }
}

void FileNode::set_remote_name(string remote_name) {
  if (remote_name_ != remote_name) {
    remote_name_ = std::move(remote_name);
    on_pmc_changed();
  }
}

void FileNode::set_url(string url) {
  if (url_ != url) {
    VLOG(update_file) << "File " << main_file_id_ << " has changed URL to " << url;
    url_ = std::move(url);
    on_changed();
  }
}

void FileNode::set_owner_dialog_id(DialogId owner_id) {
  if (owner_dialog_id_ != owner_id) {
    owner_dialog_id_ = owner_id;
    on_pmc_changed();
  }
}

void FileNode::set_encryption_key(FileEncryptionKey key) {
  if (encryption_key_ != key) {
    encryption_key_ = std::move(key);
    on_pmc_changed();
  }
}

void FileNode::set_download_priority(int8 priority) {
  if ((download_priority_ == 0) != (priority == 0)) {
    VLOG(update_file) << "File " << main_file_id_ << " has changed download priority to " << priority;
    on_info_changed();
  }
  download_priority_ = priority;
}

void FileNode::set_upload_priority(int8 priority) {
  if ((upload_priority_ == 0) != (priority == 0)) {
    VLOG(update_file) << "File " << main_file_id_ << " has changed upload priority to " << priority;
    on_info_changed();
  }
  upload_priority_ = priority;
}

void FileNode::set_generate_priority(int8 download_priority, int8 upload_priority) {
  if ((generate_download_priority_ == 0) != (download_priority == 0) ||
      (generate_upload_priority_ == 0) != (upload_priority == 0)) {
    VLOG(update_file) << "File " << main_file_id_ << " has changed generate priority to " << download_priority << "/"
                      << upload_priority;
    on_info_changed();
  }
  generate_priority_ = max(download_priority, upload_priority);
  generate_download_priority_ = download_priority;
  generate_upload_priority_ = upload_priority;
}

void FileNode::on_changed() {
  on_pmc_changed();
  on_info_changed();
}
void FileNode::on_info_changed() {
  info_changed_flag_ = true;
}
void FileNode::on_pmc_changed() {
  pmc_changed_flag_ = true;
}

bool FileNode::need_info_flush() const {
  return info_changed_flag_;
}

bool FileNode::need_pmc_flush() const {
  if (!pmc_changed_flag_) {
    return false;
  }

  // already in pmc
  if (pmc_id_ != 0) {
    return true;
  }

  // We must save encryption key
  if (!encryption_key_.empty()) {
    // && remote_.type() != RemoteFileLocation::Type::Empty
    return true;
  }

  bool has_generate_location = generate_ != nullptr;
  // Do not save "#file_id#" conversion.
  if (has_generate_location && begins_with(generate_->conversion_, "#file_id#")) {
    has_generate_location = false;
  }

  if (remote_.type() == RemoteFileLocation::Type::Full &&
      (has_generate_location || local_.type() != LocalFileLocation::Type::Empty)) {
    return true;
  }
  if (local_.type() == LocalFileLocation::Type::Full &&
      (has_generate_location || remote_.type() != RemoteFileLocation::Type::Empty)) {
    return true;
  }

  // TODO: Generate location with constant conversion

  return false;
}

void FileNode::on_pmc_flushed() {
  pmc_changed_flag_ = false;
}
void FileNode::on_info_flushed() {
  info_changed_flag_ = false;
}

string FileNode::suggested_name() const {
  if (!remote_name_.empty()) {
    return remote_name_;
  }
  if (!url_.empty()) {
    auto file_name = get_url_file_name(url_);
    if (!file_name.empty()) {
      return file_name;
    }
  }
  if (generate_ != nullptr) {
    if (!generate_->original_path_.empty()) {
      return generate_->original_path_;
    }
  }
  return local_.file_name().str();
}

/*** FileView ***/
bool FileView::has_local_location() const {
  return node_->local_.type() == LocalFileLocation::Type::Full;
}
const FullLocalFileLocation &FileView::local_location() const {
  CHECK(has_local_location());
  return node_->local_.full();
}
bool FileView::has_remote_location() const {
  return node_->remote_.type() == RemoteFileLocation::Type::Full;
}
const FullRemoteFileLocation &FileView::remote_location() const {
  CHECK(has_remote_location());
  auto *remote = node_.get_remote();
  if (remote) {
    return *remote;
  }
  return node_->remote_.full();
}
bool FileView::has_generate_location() const {
  return node_->generate_ != nullptr;
}
const FullGenerateFileLocation &FileView::generate_location() const {
  CHECK(has_generate_location());
  return *node_->generate_;
}

int64 FileView::size() const {
  return node_->size_;
}

int64 FileView::expected_size() const {
  if (node_->size_ != 0) {
    return node_->size_;
  }
  return node_->expected_size_;
}

bool FileView::is_downloading() const {
  return node_->download_priority_ != 0 || node_->generate_download_priority_ != 0;
}

int64 FileView::local_size() const {
  switch (node_->local_.type()) {
    case LocalFileLocation::Type::Full:
      return node_->size_;
    case LocalFileLocation::Type::Partial: {
      if (is_encrypted_secure()) {
        // File is not decrypted yet
        return 0;
      }
      return node_->local_.partial().part_size_ * node_->local_.partial().ready_part_count_;
    }
    default:
      return 0;
  }
}
int64 FileView::local_total_size() const {
  switch (node_->local_.type()) {
    case LocalFileLocation::Type::Empty:
      return 0;
    case LocalFileLocation::Type::Full:
      return node_->size_;
    case LocalFileLocation::Type::Partial:
      return max(static_cast<int64>(node_->local_.partial().part_size_) * node_->local_.partial().ready_part_count_,
                 node_->local_ready_size_);
    default:
      UNREACHABLE();
      return 0;
  }
}

bool FileView::is_uploading() const {
  return node_->upload_priority_ != 0 || node_->generate_upload_priority_ != 0;
}

int64 FileView::remote_size() const {
  switch (node_->remote_.type()) {
    case RemoteFileLocation::Type::Full:
      return node_->size_;
    case RemoteFileLocation::Type::Partial: {
      auto res =
          max(static_cast<int64>(node_->remote_.partial().part_size_) * node_->remote_.partial().ready_part_count_,
              node_->remote_ready_size_);
      if (size() != 0 && size() < res) {
        res = size();
      }
      return res;
    }
    default:
      return 0;
  }
}

string FileView::path() const {
  switch (node_->local_.type()) {
    case LocalFileLocation::Type::Full:
      return node_->local_.full().path_;
    case LocalFileLocation::Type::Partial:
      return node_->local_.partial().path_;
    default:
      return "";
  }
}

bool FileView::has_url() const {
  return !node_->url_.empty();
}

const string &FileView::url() const {
  return node_->url_;
}

const string &FileView::remote_name() const {
  return node_->remote_name_;
}

string FileView::suggested_name() const {
  return node_->suggested_name();
}

DialogId FileView::owner_dialog_id() const {
  return node_->owner_dialog_id_;
}

bool FileView::get_by_hash() const {
  return node_->get_by_hash_;
}

FileView::FileView(ConstFileNodePtr node) : node_(node) {
}

bool FileView::empty() const {
  return !node_;
}

bool FileView::can_download_from_server() const {
  if (!has_remote_location()) {
    return false;
  }
  if (remote_location().file_type_ == FileType::Encrypted && encryption_key().empty()) {
    return false;
  }
  if (remote_location().is_web()) {
    return true;
  }
  if (remote_location().get_dc_id().is_empty()) {
    return false;
  }
  return true;
}

bool FileView::can_generate() const {
  return has_generate_location();
}

bool FileView::can_delete() const {
  if (has_local_location()) {
    return begins_with(local_location().path_, get_files_dir(get_type()));
  }
  return node_->local_.type() == LocalFileLocation::Type::Partial;
}

/*** FileManager ***/
namespace {
void prepare_path_for_pmc(FileType file_type, string &path) {
  path = PathView::relative(path, get_files_base_dir(file_type)).str();
}
}  // namespace

FileManager::FileManager(std::unique_ptr<Context> context) : context_(std::move(context)) {
  if (G()->parameters().use_file_db) {
    file_db_ = G()->td_db()->get_file_db_shared();
  }

  parent_ = context_->create_reference();
  next_file_id();
  next_file_node_id();

  std::vector<string> dirs;
  auto create_dir = [&](CSlice path) {
    dirs.push_back(path.str());
    auto status = mkdir(path, 0750);
    if (status.is_error()) {
      auto r_stat = stat(path);
      if (r_stat.is_ok() && r_stat.ok().is_dir_) {
        LOG(ERROR) << "mkdir " << tag("path", path) << " failed " << status << ", but directory exists";
      } else {
        LOG(ERROR) << "mkdir " << tag("path", path) << " failed " << status;
      }
    }
#if TD_ANDROID
    FileFd::open(dirs.back() + ".nomedia", FileFd::Create | FileFd::Read).ignore();
#endif
  };
  for (int i = 0; i < file_type_size; i++) {
    auto path = get_files_dir(FileType(i));
    create_dir(path);
  }

  // Create both temp dirs.
  create_dir(get_files_temp_dir(FileType::Encrypted));
  create_dir(get_files_temp_dir(FileType::Video));

  G()->td_db()->with_db_path([this](CSlice path) { this->bad_paths_.insert(path.str()); });
}

void FileManager::init_actor() {
  file_load_manager_ = create_actor_on_scheduler<FileLoadManager>("FileLoadManager", G()->get_slow_net_scheduler_id(),
                                                                  actor_shared(this), context_->create_reference());
  file_generate_manager_ = create_actor_on_scheduler<FileGenerateManager>(
      "FileGenerateManager", G()->get_slow_net_scheduler_id(), context_->create_reference());
}
FileManager::~FileManager() {
}

string FileManager::fix_file_extension(Slice file_name, Slice file_type, Slice file_extension) {
  return (file_name.empty() ? file_type : file_name).str() + "." + file_extension.str();
}

string FileManager::get_file_name(FileType file_type, Slice path) {
  PathView path_view(path);
  auto file_name = path_view.file_name();
  auto extension = path_view.extension();
  switch (file_type) {
    case FileType::Thumbnail:
      if (extension != "jpg" && extension != "jpeg" && extension != "webp") {
        return fix_file_extension(file_name, "thumbnail", "jpg");
      }
      break;
    case FileType::ProfilePhoto:
    case FileType::Photo:
      if (extension != "jpg" && extension != "jpeg" && extension != "gif" && extension != "png" && extension != "tif" &&
          extension != "bmp") {
        return fix_file_extension(file_name, "photo", "jpg");
      }
      break;
    case FileType::VoiceNote:
      if (extension != "ogg" && extension != "oga" && extension != "mp3" && extension != "mpeg3" &&
          extension != "m4a") {
        return fix_file_extension(file_name, "voice", "oga");
      }
      break;
    case FileType::Video:
    case FileType::VideoNote:
      if (extension != "mov" && extension != "3gp" && extension != "mpeg4" && extension != "mp4") {
        return fix_file_extension(file_name, "video", "mp4");
      }
      break;
    case FileType::Audio:
      if (extension != "ogg" && extension != "oga" && extension != "mp3" && extension != "mpeg3" &&
          extension != "m4a") {
        return fix_file_extension(file_name, "audio", "mp3");
      }
      break;
    case FileType::Document:
    case FileType::Sticker:
    case FileType::Animation:
    case FileType::Encrypted:
    case FileType::Temp:
    case FileType::EncryptedThumbnail:
    case FileType::Wallpaper:
    case FileType::Secure:
    case FileType::SecureRaw:
      break;
    default:
      UNREACHABLE();
  }
  return file_name.str();
}

Status FileManager::check_local_location(FullLocalFileLocation &location, int64 &size) {
  constexpr int64 MAX_THUMBNAIL_SIZE = 200 * (1 << 10) /* 200KB */;
  constexpr int64 MAX_FILE_SIZE = 1500 * (1 << 20) /* 1500MB */;

  if (location.path_.empty()) {
    return Status::Error("File must have non-empty path");
  }
  TRY_RESULT(path, realpath(location.path_, true));
  if (bad_paths_.count(path) != 0) {
    return Status::Error("Sending of internal database files is forbidden");
  }
  location.path_ = std::move(path);
  TRY_RESULT(stat, stat(location.path_));
  if (!stat.is_reg_) {
    return Status::Error("File must be a regular file");
  }
  if (stat.size_ < 0) {
    // TODO is it possible?
    return Status::Error("File is too big");
  }
  if (stat.size_ == 0) {
    return Status::Error("File must be non-empty");
  }

  if (size == 0) {
    size = stat.size_;
  }
  if (location.mtime_nsec_ == 0) {
    LOG(INFO) << "Set file \"" << location.path_ << "\" modification time to " << stat.mtime_nsec_;
    location.mtime_nsec_ = stat.mtime_nsec_;
  } else if (location.mtime_nsec_ != stat.mtime_nsec_) {
    LOG(INFO) << "File \"" << location.path_ << "\" was nodified: old mtime = " << location.mtime_nsec_
              << ", new mtime = " << stat.mtime_nsec_;
    return Status::Error(PSLICE() << "File \"" << location.path_ << "\" was modified");
  }
  if ((location.file_type_ == FileType::Thumbnail || location.file_type_ == FileType::EncryptedThumbnail) &&
      size >= MAX_THUMBNAIL_SIZE && !begins_with(PathView(location.path_).file_name(), "map")) {
    return Status::Error(PSLICE() << "File \"" << location.path_ << "\" is too big for thumbnail "
                                  << tag("size", format::as_size(size)));
  }
  if (size >= MAX_FILE_SIZE) {
    return Status::Error(PSLICE() << "File \"" << location.path_ << "\" is too big "
                                  << tag("size", format::as_size(size)));
  }
  return Status::OK();
}

static Status check_partial_local_location(const PartialLocalFileLocation &location) {
  TRY_RESULT(stat, stat(location.path_));
  if (!stat.is_reg_) {
    if (stat.is_dir_) {
      return Status::Error(PSLICE() << "Can't use directory \"" << location.path_ << "\" as a file path");
    }
    return Status::Error("File must be a regular file");
  }
  // can't check mtime. Hope nobody will mess with this file in our temporary dir.
  return Status::OK();
}

Status FileManager::check_local_location(FileNodePtr node) {
  Status status;
  if (node->local_.type() == LocalFileLocation::Type::Full) {
    status = check_local_location(node->local_.full(), node->size_);
  } else if (node->local_.type() == LocalFileLocation::Type::Partial) {
    status = check_partial_local_location(node->local_.partial());
  }
  if (status.is_error()) {
    node->set_local_location(LocalFileLocation(), 0);
    try_flush_node(node);
  }
  return status;
}

FileManager::FileIdInfo *FileManager::get_file_id_info(FileId file_id) {
  CHECK(0 <= file_id.get() && file_id.get() < static_cast<int32>(file_id_info_.size()))
      << file_id << " " << file_id_info_.size();
  return &file_id_info_[file_id.get()];
}

FileId FileManager::dup_file_id(FileId file_id) {
  int32 file_node_id;
  auto *file_node = get_file_node_raw(file_id, &file_node_id);
  if (!file_node) {
    return FileId();
  }
  auto result = create_file_id(file_node_id, file_node);
  LOG(INFO) << "Dup file " << file_id << " to " << result;
  return result;
}

FileId FileManager::create_file_id(int32 file_node_id, FileNode *file_node) {
  auto file_id = next_file_id();
  get_file_id_info(file_id)->node_id_ = file_node_id;
  file_node->file_ids_.push_back(file_id);
  return file_id;
}
void FileManager::try_forget_file_id(FileId file_id) {
  auto *info = get_file_id_info(file_id);
  if (info->send_updates_flag_ || info->pin_flag_) {
    return;
  }
  auto file_node = get_file_node(file_id);
  if (file_node->main_file_id_ == file_id) {
    return;
  }

  LOG(DEBUG) << "Forget file " << file_id;
  auto it = std::find(file_node->file_ids_.begin(), file_node->file_ids_.end(), file_id);
  CHECK(it != file_node->file_ids_.end());
  file_node->file_ids_.erase(it);
  *info = FileIdInfo();
  empty_file_ids_.push_back(file_id.get());
}

FileId FileManager::register_empty(FileType type) {
  return register_local(FullLocalFileLocation(type, "", 0), DialogId(), 0, false, true).ok();
}

void FileManager::on_file_unlink(const FullLocalFileLocation &location) {
  auto it = local_location_to_file_id_.find(location);
  if (it == local_location_to_file_id_.end()) {
    return;
  }
  auto file_id = it->second;
  auto file_node = get_sync_file_node(file_id);
  CHECK(file_node);
  file_node->set_local_location(LocalFileLocation(), 0);
  try_flush_node_info(file_node);
}

Result<FileId> FileManager::register_local(FullLocalFileLocation location, DialogId owner_dialog_id, int64 size,
                                           bool get_by_hash, bool force) {
  // TODO: use get_by_hash
  FileData data;
  data.local_ = LocalFileLocation(std::move(location));
  data.owner_dialog_id_ = owner_dialog_id;
  data.size_ = size;
  return register_file(std::move(data), FileLocationSource::None /*won't be used*/, "register_local", force);
}

FileId FileManager::register_remote(const FullRemoteFileLocation &location, FileLocationSource file_location_source,
                                    DialogId owner_dialog_id, int64 size, int64 expected_size, string name) {
  FileData data;
  data.remote_ = RemoteFileLocation(location);
  data.owner_dialog_id_ = owner_dialog_id;
  data.size_ = size;
  data.expected_size_ = expected_size;
  data.remote_name_ = std::move(name);

  auto file_id = register_file(std::move(data), file_location_source, "register_remote", false).move_as_ok();
  auto url = location.get_url();
  if (!url.empty()) {
    auto file_node = get_file_node(file_id);
    CHECK(file_node);
    file_node->set_url(url);
  }
  return file_id;
}

FileId FileManager::register_url(string url, FileType file_type, FileLocationSource file_location_source,
                                 DialogId owner_dialog_id) {
  auto file_id = register_generate(file_type, file_location_source, url, "#url#", owner_dialog_id, 0).ok();
  auto file_node = get_file_node(file_id);
  CHECK(file_node);
  file_node->set_url(url);
  return file_id;
}

Result<FileId> FileManager::register_generate(FileType file_type, FileLocationSource file_location_source,
                                              string original_path, string conversion, DialogId owner_dialog_id,
                                              int64 expected_size) {
  FileData data;
  data.generate_ = make_unique<FullGenerateFileLocation>(file_type, std::move(original_path), std::move(conversion));
  data.owner_dialog_id_ = owner_dialog_id;
  data.expected_size_ = expected_size;
  return register_file(std::move(data), file_location_source, "register_generate", false);
}

Result<FileId> FileManager::register_file(FileData data, FileLocationSource file_location_source, const char *source,
                                          bool force) {
  bool has_remote = data.remote_.type() == RemoteFileLocation::Type::Full;
  bool has_generate = data.generate_ != nullptr;
  if (data.local_.type() == LocalFileLocation::Type::Full && !force) {
    if (file_location_source == FileLocationSource::FromDb) {
      PathView path_view(data.local_.full().path_);
      if (path_view.is_relative()) {
        data.local_.full().path_ = get_files_base_dir(data.local_.full().file_type_) + data.local_.full().path_;
      }
    }

    auto status = check_local_location(data.local_.full(), data.size_);
    if (status.is_error()) {
      LOG(WARNING) << "Invalid local location: " << status << " from " << source;
      data.local_ = LocalFileLocation();
      if (data.remote_.type() == RemoteFileLocation::Type::Partial) {
        data.remote_ = {};
      }

      if (!has_remote && !has_generate) {
        return std::move(status);
      }
    }
  }
  bool has_local = data.local_.type() == LocalFileLocation::Type::Full;
  bool has_location = has_local || has_remote || has_generate;
  if (!has_location) {
    return Status::Error("No location");
  }

  FileId file_id = next_file_id();

  LOG(INFO) << "Register file data " << data << " as " << file_id << " from " << source;
  // create FileNode
  auto file_node_id = next_file_node_id();
  auto &node = file_nodes_[file_node_id];
  node = std::make_unique<FileNode>(std::move(data.local_), std::move(data.remote_), std::move(data.generate_),
                                    data.size_, data.expected_size_, std::move(data.remote_name_), std::move(data.url_),
                                    data.owner_dialog_id_, std::move(data.encryption_key_), file_id,
                                    static_cast<int8>(has_remote));
  node->remote_source_ = file_location_source;
  node->pmc_id_ = data.pmc_id_;
  get_file_id_info(file_id)->node_id_ = file_node_id;
  node->file_ids_.push_back(file_id);

  FileView file_view(get_file_node(file_id));

  std::vector<FileId> to_merge;
  auto register_location = [&](const auto &location, auto &mp) {
    auto &other_id = mp[location];
    if (other_id.empty()) {
      other_id = file_id;
      get_file_id_info(file_id)->pin_flag_ = true;
      return true;
    } else {
      to_merge.push_back(other_id);
      return false;
    }
  };
  bool new_remote = false;
  int32 remote_key = 0;
  if (file_view.has_remote_location()) {
    RemoteInfo info{file_view.remote_location(), file_id};
    remote_key = remote_location_info_.add(info);
    auto &stored_info = remote_location_info_.get(remote_key);
    if (stored_info.file_id_ == file_id) {
      get_file_id_info(file_id)->pin_flag_ = true;
      new_remote = true;
    } else {
      to_merge.push_back(stored_info.file_id_);
      if (stored_info.remote_ == file_view.remote_location() &&
          stored_info.remote_.get_access_hash() != file_view.remote_location().get_access_hash() &&
          file_location_source == FileLocationSource::FromServer) {
        stored_info.remote_ = file_view.remote_location();
      }
    }
  }
  bool new_local = false;
  if (file_view.has_local_location()) {
    new_local = register_location(file_view.local_location(), local_location_to_file_id_);
  }
  bool new_generate = false;
  if (file_view.has_generate_location()) {
    new_generate = register_location(file_view.generate_location(), generate_location_to_file_id_);
  }
  std::sort(to_merge.begin(), to_merge.end());
  to_merge.erase(std::unique(to_merge.begin(), to_merge.end()), to_merge.end());

  int new_cnt = new_remote + new_local + new_generate;
  if (data.pmc_id_ == 0 && file_db_ && new_cnt > 0) {
    node->need_load_from_pmc_ = true;
  }
  bool no_sync_merge = to_merge.size() == 1 && new_cnt == 0;
  for (auto id : to_merge) {
    // may invalidate node
    merge(file_id, id, no_sync_merge).ignore();
  }

  try_flush_node(get_file_node(file_id));
  auto main_file_id = get_file_node(file_id)->main_file_id_;
  try_forget_file_id(file_id);
  return FileId(main_file_id.get(), remote_key);
}

// 0 -- choose x
// 1 -- choose y
// 2 -- choose any
static int merge_choose(const LocalFileLocation &x, const LocalFileLocation &y) {
  int32 x_type = static_cast<int32>(x.type());
  int32 y_type = static_cast<int32>(y.type());
  if (x_type != y_type) {
    return x_type < y_type;
  }
  return 2;
}

static int merge_choose(const RemoteFileLocation &x, int8 x_source, const RemoteFileLocation &y, int8 y_source) {
  int32 x_type = static_cast<int32>(x.type());
  int32 y_type = static_cast<int32>(y.type());
  if (x_type != y_type) {
    return x_type < y_type;
  }
  // If access_hash changed use a newer one
  if (x.type() == RemoteFileLocation::Type::Full) {
    if (x.full().get_access_hash() != y.full().get_access_hash()) {
      return x_source < y_source;
    }
  }
  return 2;
}
static int merge_choose(const unique_ptr<FullGenerateFileLocation> &x, const unique_ptr<FullGenerateFileLocation> &y) {
  int x_type = static_cast<int>(x != nullptr);
  int y_type = static_cast<int>(y != nullptr);
  if (x_type != y_type) {
    return x_type < y_type;
  }
  return 2;
}

// -1 -- error
static int merge_choose_size(int64 x, int64 y) {
  if (x == 0) {
    return 1;
  }
  if (y == 0) {
    return 0;
  }
  if (x != y) {
    return -1;
  }
  return 2;
}
static int merge_choose_expected_size(int64 x, int64 y) {
  if (x == 0) {
    return 1;
  }
  if (y == 0) {
    return 0;
  }
  return 2;
}

static int merge_choose_name(Slice x, Slice y) {
  if (x.empty() != y.empty()) {
    return x.empty() > y.empty();
  }
  return 2;
}

static int merge_choose_owner(DialogId x, DialogId y) {
  if (x.is_valid() != y.is_valid()) {
    return x.is_valid() < y.is_valid();
  }
  return 2;
}

static int merge_choose_main_file_id(FileId a, int8 a_priority, FileId b, int8 b_priority) {
  if (a_priority != b_priority) {
    return a_priority < b_priority;
  }
  return a.get() > b.get();
}

static int merge_choose_encryption_key(const FileEncryptionKey &a, const FileEncryptionKey &b) {
  if (a.empty() != b.empty()) {
    return a.empty() > b.empty();
  }
  if (a.key_iv_ != b.key_iv_) {
    return -1;
  }
  return 2;
}

void FileManager::cancel_download(FileNodePtr node) {
  if (node->download_id_ == 0) {
    return;
  }
  send_closure(file_load_manager_, &FileLoadManager::cancel, node->download_id_);
  node->download_id_ = 0;
  node->is_download_started_ = false;
  node->set_download_priority(0);
}

void FileManager::cancel_upload(FileNodePtr node) {
  if (node->upload_id_ == 0) {
    return;
  }
  send_closure(file_load_manager_, &FileLoadManager::cancel, node->upload_id_);
  node->upload_id_ = 0;
  node->set_upload_priority(0);
}

void FileManager::cancel_generate(FileNodePtr node) {
  if (node->generate_id_ == 0) {
    return;
  }
  send_closure(file_generate_manager_, &FileGenerateManager::cancel, node->generate_id_);
  node->generate_id_ = 0;
  node->generate_was_update_ = false;
  node->set_generate_priority(0, 0);
}

Result<FileId> FileManager::merge(FileId x_file_id, FileId y_file_id, bool no_sync) {
  LOG(DEBUG) << x_file_id << " VS " << y_file_id;

  if (!x_file_id.is_valid()) {
    return Status::Error("First file_id is invalid");
  }
  FileNodePtr x_node = no_sync ? get_file_node(x_file_id) : get_sync_file_node(x_file_id);
  if (!x_node) {
    return Status::Error(PSLICE() << "Can't merge files. First id is invalid: " << x_file_id << " and " << y_file_id);
  }

  if (!y_file_id.is_valid()) {
    return x_node->main_file_id_;
  }
  FileNodePtr y_node = get_file_node(y_file_id);
  if (!y_node) {
    return Status::Error(PSLICE() << "Can't merge files. Second id is invalid: " << x_file_id << " and " << y_file_id);
  }

  if (x_file_id == x_node->upload_pause_) {
    x_node->upload_pause_ = FileId();
  }
  if (x_node.get() == y_node.get()) {
    return x_node->main_file_id_;
  }
  if (y_file_id == y_node->upload_pause_) {
    y_node->upload_pause_ = FileId();
  }

  if (x_node->remote_.type() == RemoteFileLocation::Type::Full &&
      y_node->remote_.type() == RemoteFileLocation::Type::Full && !x_node->remote_.full().is_web() &&
      !y_node->remote_.full().is_web() && x_node->remote_.full().get_dc_id() != y_node->remote_.full().get_dc_id()) {
    LOG(ERROR) << "File remote location was changed from " << y_node->remote_.full() << " to "
               << x_node->remote_.full();
  }

  FileNodePtr nodes[] = {x_node, y_node, x_node};
  FileNodeId node_ids[] = {get_file_id_info(x_file_id)->node_id_, get_file_id_info(y_file_id)->node_id_};
  int trusted_by_source =
      static_cast<int>(static_cast<int8>(x_node->remote_source_) < static_cast<int8>(y_node->remote_source_));

  int local_i = merge_choose(x_node->local_, y_node->local_);
  int remote_i = merge_choose(x_node->remote_, static_cast<int8>(x_node->remote_source_), y_node->remote_,
                              static_cast<int8>(y_node->remote_source_));
  int generate_i = merge_choose(x_node->generate_, y_node->generate_);
  int size_i = merge_choose_size(x_node->size_, y_node->size_);
  int expected_size_i = merge_choose_expected_size(x_node->expected_size_, y_node->expected_size_);
  int remote_name_i = merge_choose_name(x_node->remote_name_, y_node->remote_name_);
  int url_i = merge_choose_name(x_node->url_, y_node->url_);
  int owner_i = merge_choose_owner(x_node->owner_dialog_id_, y_node->owner_dialog_id_);
  int encryption_key_i = merge_choose_encryption_key(x_node->encryption_key_, y_node->encryption_key_);
  int main_file_id_i = merge_choose_main_file_id(x_node->main_file_id_, x_node->main_file_id_priority_,
                                                 y_node->main_file_id_, y_node->main_file_id_priority_);

  if (size_i == -1) {
    return Status::Error(PSLICE() << "Can't merge files. Different size: " << x_node->size_ << " and "
                                  << y_node->size_);
  }
  if (encryption_key_i == -1) {
    if (nodes[remote_i]->remote_.type() == RemoteFileLocation::Type::Full &&
        nodes[local_i]->local_.type() != LocalFileLocation::Type::Partial) {
      //???
      LOG(ERROR) << "Different encryption key in files, but go Choose same key as remote location";
      encryption_key_i = remote_i;
    } else {
      return Status::Error("Can't merge files. Different encryption keys");
    }
  }
  if (trusted_by_source == 0) {  // if new is more trusted
    if (remote_name_i == 2) {
      remote_name_i = 0;
    }
    if (url_i == 2) {
      url_i = 0;
    }
    if (expected_size_i == 2) {
      expected_size_i = 0;
    }
  }

  int node_i = std::make_tuple(y_node->pmc_id_ != 0, x_node->pmc_id_, y_node->file_ids_.size(), main_file_id_i == 1) >
               std::make_tuple(x_node->pmc_id_ != 0, y_node->pmc_id_, x_node->file_ids_.size(), main_file_id_i == 0);

  auto other_node_i = 1 - node_i;
  FileNodePtr node = nodes[node_i];
  FileNodePtr other_node = nodes[other_node_i];
  auto file_view = FileView(node);

  LOG(DEBUG) << "x_node->pmc_id_ = " << x_node->pmc_id_ << ", y_node->pmc_id_ = " << y_node->pmc_id_
             << ", x_node_size = " << x_node->file_ids_.size() << ", y_node_size = " << y_node->file_ids_.size()
             << ", node_i = " << node_i << ", local_i = " << local_i << ", remote_i = " << remote_i
             << ", generate_i = " << generate_i << ", size_i = " << size_i << ", remote_name_i = " << remote_name_i
             << ", url_i = " << url_i << ", owner_i = " << owner_i << ", encryption_key_i = " << encryption_key_i
             << ", main_file_id_i = " << main_file_id_i;
  if (local_i == other_node_i) {
    cancel_download(node);
    node->set_local_location(other_node->local_, other_node->local_ready_size_);
    node->download_id_ = other_node->download_id_;
    node->is_download_started_ |= other_node->is_download_started_;
    node->set_download_priority(other_node->download_priority_);
    other_node->download_id_ = 0;
    other_node->is_download_started_ = false;
    other_node->download_priority_ = 0;

    //cancel_generate(node);
    //node->set_generate_location(std::move(other_node->generate_));
    //node->generate_id_ = other_node->generate_id_;
    //node->set_generate_priority(other_node->generate_download_priority_, other_node->generate_upload_priority_);
    //other_node->generate_id_ = 0;
    //other_node->generate_was_update_ = false;
    //other_node->generate_priority_ = 0;
    //other_node->generate_download_priority_ = 0;
    //other_node->generate_upload_priority_ = 0;
  } else {
    cancel_download(other_node);
    //cancel_generate(other_node);
  }

  if (remote_i == other_node_i) {
    cancel_upload(node);
    node->set_remote_location(other_node->remote_, other_node->remote_source_, other_node->remote_ready_size_);
    node->upload_id_ = other_node->upload_id_;
    node->set_upload_priority(other_node->upload_priority_);
    node->upload_pause_ = other_node->upload_pause_;
    other_node->upload_id_ = 0;
    other_node->upload_priority_ = 0;
    other_node->upload_pause_ = FileId();
  } else {
    cancel_upload(other_node);
  }

  if (generate_i == other_node_i) {
    cancel_generate(node);
    node->set_generate_location(std::move(other_node->generate_));
    node->generate_id_ = other_node->generate_id_;
    node->set_generate_priority(other_node->generate_download_priority_, other_node->generate_upload_priority_);
    other_node->generate_id_ = 0;
    other_node->generate_priority_ = 0;
    other_node->generate_download_priority_ = 0;
    other_node->generate_upload_priority_ = 0;
  } else {
    cancel_generate(other_node);
  }

  if (size_i == other_node_i) {
    node->set_size(other_node->size_);
  }

  if (expected_size_i == other_node_i) {
    node->set_expected_size(other_node->expected_size_);
  }

  if (remote_name_i == other_node_i) {
    node->set_remote_name(other_node->remote_name_);
  }

  if (url_i == other_node_i) {
    node->set_url(other_node->url_);
  }

  if (owner_i == other_node_i) {
    node->set_owner_dialog_id(other_node->owner_dialog_id_);
  }

  if (encryption_key_i == other_node_i) {
    node->set_encryption_key(other_node->encryption_key_);
    nodes[node_i]->set_encryption_key(nodes[encryption_key_i]->encryption_key_);
  }
  node->need_load_from_pmc_ |= other_node->need_load_from_pmc_;
  node->can_search_locally_ &= other_node->can_search_locally_;

  if (main_file_id_i == other_node_i) {
    node->main_file_id_ = other_node->main_file_id_;
    node->main_file_id_priority_ = other_node->main_file_id_priority_;
  }

  bool send_updates_flag = false;
  auto other_pmc_id = other_node->pmc_id_;
  node->file_ids_.insert(node->file_ids_.end(), other_node->file_ids_.begin(), other_node->file_ids_.end());

  for (auto file_id : other_node->file_ids_) {
    auto file_id_info = get_file_id_info(file_id);
    CHECK(file_id_info->node_id_ == node_ids[other_node_i])
        << node_ids[node_i] << " " << node_ids[other_node_i] << " " << file_id << " " << file_id_info->node_id_;
    file_id_info->node_id_ = node_ids[node_i];
    send_updates_flag |= file_id_info->send_updates_flag_;
  }
  other_node = {};

  if (send_updates_flag) {
    // node might not changed, but other_node might changed, so we need to send update anyway
    VLOG(update_file) << "File " << node->main_file_id_ << " has been merged";
    node->on_info_changed();
  }

  // Check is some download/upload queries are ready
  for (auto file_id : vector<FileId>(node->file_ids_)) {
    auto *info = get_file_id_info(file_id);
    if (info->download_priority_ != 0 && file_view.has_local_location()) {
      info->download_priority_ = 0;
      if (info->download_callback_) {
        info->download_callback_->on_download_ok(file_id);
        info->download_callback_.reset();
      }
    }
    if (info->upload_priority_ != 0 && file_view.has_remote_location()) {
      info->upload_priority_ = 0;
      if (info->upload_callback_) {
        info->upload_callback_->on_upload_ok(file_id, nullptr);
        info->upload_callback_.reset();
      }
    }
  }

  file_nodes_[node_ids[other_node_i]] = nullptr;

  run_generate(node);
  run_download(node);
  run_upload(node, {});

  if (other_pmc_id != 0) {
    // node might not changed, but we need to merge nodes in pmc anyway
    node->on_pmc_changed();
  }
  try_flush_node(node, node_i != remote_i, node_i != local_i, node_i != generate_i, other_pmc_id);

  return node->main_file_id_;
}

void FileManager::try_flush_node(FileNodePtr node, bool new_remote, bool new_local, bool new_generate,
                                 FileDbId other_pmc_id) {
  if (node->need_pmc_flush()) {
    if (file_db_) {
      load_from_pmc(node, true, true, true);
      flush_to_pmc(node, new_remote, new_local, new_generate);
      if (other_pmc_id != 0 && node->pmc_id_ != other_pmc_id) {
        file_db_->set_file_data_ref(other_pmc_id, node->pmc_id_);
      }
    }
    node->on_pmc_flushed();
  }

  try_flush_node_info(node);
}

void FileManager::try_flush_node_info(FileNodePtr node) {
  if (node->need_info_flush()) {
    for (auto file_id : vector<FileId>(node->file_ids_)) {
      auto *info = get_file_id_info(file_id);
      if (info->send_updates_flag_) {
        VLOG(update_file) << "Send UpdateFile about file " << file_id;
        context_->on_file_updated(file_id);
      }
    }
    node->on_info_flushed();
  }
}

void FileManager::clear_from_pmc(FileNodePtr node) {
  if (!file_db_) {
    return;
  }
  if (node->pmc_id_ == 0) {
    return;
  }

  LOG(INFO) << "Delete files " << format::as_array(node->file_ids_) << " from pmc";
  FileData data;
  auto file_view = FileView(node);
  if (file_view.has_local_location()) {
    data.local_ = node->local_;
  }
  if (file_view.has_remote_location()) {
    data.remote_ = node->remote_;
  }
  if (file_view.has_generate_location()) {
    data.generate_ = std::make_unique<FullGenerateFileLocation>(*node->generate_);
  }
  file_db_->clear_file_data(node->pmc_id_, data);
  node->pmc_id_ = 0;
}

void FileManager::flush_to_pmc(FileNodePtr node, bool new_remote, bool new_local, bool new_generate) {
  if (!file_db_) {
    return;
  }
  FileView view(node);
  bool create_flag = false;
  if (node->pmc_id_ == 0) {
    create_flag = true;
    node->pmc_id_ = file_db_->create_pmc_id();
  }

  FileData data;
  data.pmc_id_ = node->pmc_id_;
  data.local_ = node->local_;
  if (data.local_.type() == LocalFileLocation::Type::Full) {
    prepare_path_for_pmc(data.local_.full().file_type_, data.local_.full().path_);
  }
  data.remote_ = node->remote_;
  if (node->generate_ != nullptr && !begins_with(node->generate_->conversion_, "#file_id#")) {
    data.generate_ = std::make_unique<FullGenerateFileLocation>(*node->generate_);
  }

  // TODO: not needed when GenerateLocation has constant convertion
  if (data.remote_.type() != RemoteFileLocation::Type::Full && data.local_.type() != LocalFileLocation::Type::Full) {
    data.local_ = LocalFileLocation();
    data.remote_ = RemoteFileLocation();
  }
  if (data.remote_.type() != RemoteFileLocation::Type::Full && node->encryption_key_.is_secure()) {
    data.remote_ = RemoteFileLocation();
  }

  data.size_ = node->size_;
  data.expected_size_ = node->expected_size_;
  data.remote_name_ = node->remote_name_;
  data.encryption_key_ = node->encryption_key_;
  data.url_ = node->url_;
  data.owner_dialog_id_ = node->owner_dialog_id_;

  file_db_->set_file_data(node->pmc_id_, data, (create_flag || new_remote), (create_flag || new_local),
                          (create_flag || new_generate));
}

FileNode *FileManager::get_file_node_raw(FileId file_id, FileNodeId *file_node_id) {
  if (file_id.get() <= 0 || file_id.get() >= static_cast<int32>(file_id_info_.size())) {
    return nullptr;
  }
  FileNodeId node_id = file_id_info_[file_id.get()].node_id_;
  if (node_id == 0) {
    return nullptr;
  }
  if (file_node_id != nullptr) {
    *file_node_id = node_id;
  }
  return file_nodes_[node_id].get();
}

FileNodePtr FileManager::get_sync_file_node(FileId file_id) {
  auto file_node = get_file_node(file_id);
  if (!file_node) {
    return {};
  }
  load_from_pmc(file_node, true, true, true);
  return file_node;
}

void FileManager::load_from_pmc(FileNodePtr node, bool new_remote, bool new_local, bool new_generate) {
  if (!node->need_load_from_pmc_) {
    return;
  }
  auto file_id = node->main_file_id_;
  node->need_load_from_pmc_ = false;
  if (!file_db_) {
    return;
  }
  auto file_view = get_file_view(file_id);

  FullRemoteFileLocation remote;
  FullLocalFileLocation local;
  FullGenerateFileLocation generate;
  new_remote &= file_view.has_remote_location();
  if (new_remote) {
    remote = file_view.remote_location();
  }
  new_local &= file_view.has_local_location();
  if (new_local) {
    local = get_file_view(file_id).local_location();
    prepare_path_for_pmc(local.file_type_, local.path_);
  }
  new_generate &= file_view.has_generate_location();
  if (new_generate) {
    generate = file_view.generate_location();
  }

  LOG(DEBUG) << "Load from pmc " << file_id << "/" << file_view.file_id() << ", new_remote = " << new_remote
             << ", new_local = " << new_local << ", new_generate = " << new_generate;
  auto load = [&](auto location) {
    TRY_RESULT(file_data, file_db_->get_file_data_sync(location));
    TRY_RESULT(new_file_id, register_file(std::move(file_data), FileLocationSource::FromDb, "load_from_pmc", false));
    TRY_RESULT(main_file_id, merge(file_id, new_file_id));
    file_id = main_file_id;
    return Status::OK();
  };
  if (new_remote) {
    load(remote);
  }
  if (new_local) {
    load(local);
  }
  if (new_generate) {
    load(generate);
  }
  return;
}

bool FileManager::set_encryption_key(FileId file_id, FileEncryptionKey key) {
  auto node = get_sync_file_node(file_id);
  if (!node) {
    return false;
  }
  auto view = FileView(node);
  if (view.has_local_location() && view.has_remote_location()) {
    return false;
  }
  if (!node->encryption_key_.empty()) {
    return false;
  }
  node->set_encryption_key(std::move(key));
  try_flush_node(node);
  return true;
}

bool FileManager::set_content(FileId file_id, BufferSlice bytes) {
  if (G()->shared_config().get_option_boolean("ignore_inline_thumbnails")) {
    return false;
  }

  auto node = get_sync_file_node(file_id);
  if (!node) {
    return false;
  }

  if (node->local_.type() == LocalFileLocation::Type::Full) {
    // There was no download so we don't need an update
    return true;
  }

  if (node->download_priority_ == FROM_BYTES_PRIORITY) {
    return true;
  }

  cancel_download(node);

  auto *file_info = get_file_id_info(file_id);
  file_info->download_priority_ = FROM_BYTES_PRIORITY;

  node->set_download_priority(FROM_BYTES_PRIORITY);

  QueryId id = queries_container_.create(Query{file_id, Query::SetContent});
  node->download_id_ = id;
  node->is_download_started_ = true;
  send_closure(file_load_manager_, &FileLoadManager::from_bytes, id, node->remote_.full().file_type_, std::move(bytes),
               node->suggested_name());
  return true;
}

void FileManager::get_content(FileId file_id, Promise<BufferSlice> promise) {
  auto node = get_sync_file_node(file_id);
  if (!node) {
    return promise.set_error(Status::Error("Unknown file_id"));
  }
  auto status = check_local_location(node);
  status.ignore();

  auto file_view = FileView(node);
  if (!file_view.has_local_location()) {
    return promise.set_error(Status::Error("No local location"));
  }

  send_closure(file_load_manager_, &FileLoadManager::get_content, node->local_.full(), std::move(promise));
}

void FileManager::delete_file(FileId file_id, Promise<Unit> promise, const char *source) {
  LOG(INFO) << "Trying to delete file " << file_id << " from " << source;
  auto node = get_sync_file_node(file_id);
  if (!node) {
    return promise.set_value(Unit());
  }

  auto file_view = FileView(node);

  // TODO: review delete condition
  if (file_view.has_local_location()) {
    if (begins_with(file_view.local_location().path_, get_files_dir(file_view.get_type()))) {
      LOG(INFO) << "Unlink file " << file_id << " at " << file_view.local_location().path_;
      clear_from_pmc(node);

      unlink(file_view.local_location().path_).ignore();
      context_->on_new_file(-file_view.size());
      node->set_local_location(LocalFileLocation(), 0);
      try_flush_node(node);
    }
  } else {
    if (file_view.get_type() == FileType::Encrypted) {
      clear_from_pmc(node);
    }
    if (node->local_.type() == LocalFileLocation::Type::Partial) {
      LOG(INFO) << "Unlink partial file " << file_id << " at " << node->local_.partial().path_;
      unlink(node->local_.partial().path_).ignore();
      node->set_local_location(LocalFileLocation(), 0);
      try_flush_node(node);
    }
  }

  promise.set_value(Unit());
}

void FileManager::download(FileId file_id, std::shared_ptr<DownloadCallback> callback, int32 new_priority) {
  LOG(INFO) << "Download file " << file_id << " with priority " << new_priority;
  auto node = get_sync_file_node(file_id);
  if (!node) {
    LOG(INFO) << "File " << file_id << " not found";
    if (callback) {
      callback->on_download_error(file_id, Status::Error("File not found"));
    }
    return;
  }

  if (node->local_.type() == LocalFileLocation::Type::Full) {
    auto status = check_local_location(node);
    if (status.is_error()) {
      LOG(WARNING) << "Need to redownload file " << file_id << ": " << status.error();
    } else {
      LOG(INFO) << "File " << file_id << " is already downloaded";
      if (callback) {
        callback->on_download_ok(file_id);
      }
      return;
    }
  } else if (node->local_.type() == LocalFileLocation::Type::Partial) {
    auto status = check_local_location(node);
    if (status.is_error()) {
      LOG(WARNING) << "Need to download file " << file_id << " from beginning: " << status.error();
    }
  }

  FileView file_view(node);
  if (!file_view.can_download_from_server() && !file_view.can_generate()) {
    LOG(INFO) << "File " << file_id << " can't be downloaded";
    if (callback) {
      callback->on_download_error(file_id, Status::Error("Can't download or generate file"));
    }
    return;
  }

  if (new_priority == -1) {
    if (node->is_download_started_) {
      LOG(INFO) << "File " << file_id << " is being downloaded";
      return;
    }
    new_priority = 0;
  }

  LOG(INFO) << "Change download priority of file " << file_id << " to " << new_priority;

  auto *file_info = get_file_id_info(file_id);
  CHECK(new_priority == 0 || callback);
  file_info->download_priority_ = narrow_cast<int8>(new_priority);
  file_info->download_callback_ = std::move(callback);
  // TODO: send current progress?

  run_generate(node);
  run_download(node);

  try_flush_node(node);
}

void FileManager::run_download(FileNodePtr node) {
  if (node->need_load_from_pmc_) {
    return;
  }
  if (node->generate_id_) {
    return;
  }
  auto file_view = FileView(node);
  if (!file_view.can_download_from_server()) {
    return;
  }
  int8 priority = 0;
  for (auto id : node->file_ids_) {
    auto *info = get_file_id_info(id);
    if (info->download_priority_ > priority) {
      priority = info->download_priority_;
    }
  }

  auto old_priority = node->download_priority_;
  node->set_download_priority(priority);

  if (priority == 0) {
    if (old_priority != 0) {
      cancel_download(node);
    }
    return;
  }

  if (old_priority != 0) {
    CHECK(node->download_id_ != 0);
    send_closure(file_load_manager_, &FileLoadManager::update_priority, node->download_id_, priority);
    return;
  }

  CHECK(node->download_id_ == 0);
  CHECK(!node->file_ids_.empty());
  auto file_id = node->file_ids_.back();
  QueryId id = queries_container_.create(Query{file_id, Query::Download});
  node->download_id_ = id;
  node->is_download_started_ = false;
  LOG(DEBUG) << "Run download of file " << file_id << " of size " << node->size_ << " from " << node->remote_.full()
             << " with suggested name " << node->suggested_name() << " and encyption key " << node->encryption_key_;
  send_closure(file_load_manager_, &FileLoadManager::download, id, node->remote_.full(), node->local_, node->size_,
               node->suggested_name(), node->encryption_key_, node->can_search_locally_, priority);
}

void FileManager::resume_upload(FileId file_id, std::vector<int> bad_parts, std::shared_ptr<UploadCallback> callback,
                                int32 new_priority, uint64 upload_order) {
  LOG(INFO) << "Resume upload of file " << file_id << " with priority " << new_priority;

  auto node = get_sync_file_node(file_id);
  if (!node) {
    LOG(INFO) << "File " << file_id << " not found";
    if (callback) {
      callback->on_upload_error(file_id, Status::Error("File not found"));
    }
    return;
  }
  if (node->upload_pause_ == file_id) {
    node->upload_pause_ = FileId();
  }
  FileView file_view(node);
  if (file_view.has_remote_location() && file_view.get_type() != FileType::Thumbnail &&
      file_view.get_type() != FileType::EncryptedThumbnail) {
    LOG(INFO) << "File " << file_id << " is already uploaded";
    if (callback) {
      callback->on_upload_ok(file_id, nullptr);
    }
    return;
  }

  if (file_view.has_local_location()) {
    auto status = check_local_location(node);
    if (status.is_error()) {
      LOG(INFO) << "Full local location of file " << file_id << " for upload is invalid: " << status;
    }
  }

  if (!file_view.has_local_location() && !file_view.has_generate_location()) {
    LOG(INFO) << "File " << file_id << " can't be uploaded";
    if (callback) {
      callback->on_upload_error(file_id, Status::Error("Need full local (or generate) location for upload"));
    }
    return;
  }

  LOG(INFO) << "Change upload priority of file " << file_id << " to " << new_priority;
  auto *file_info = get_file_id_info(file_id);
  CHECK(new_priority == 0 || callback);
  file_info->upload_order_ = upload_order;
  file_info->upload_priority_ = narrow_cast<int8>(new_priority);
  file_info->upload_callback_ = std::move(callback);
  // TODO: send current progress?

  run_generate(node);
  run_upload(node, std::move(bad_parts));
  try_flush_node(node);
}

bool FileManager::delete_partial_remote_location(FileId file_id) {
  auto node = get_sync_file_node(file_id);
  if (!node) {
    LOG(INFO) << "Wrong file id " << file_id;
    return false;
  }
  if (node->upload_pause_ == file_id) {
    node->upload_pause_ = FileId();
  }
  if (node->remote_.type() == RemoteFileLocation::Type::Full) {
    LOG(INFO) << "File " << file_id << " is already uploaded";
    return true;
  }

  node->set_remote_location(RemoteFileLocation(), FileLocationSource::None, 0);
  auto *file_info = get_file_id_info(file_id);
  file_info->upload_priority_ = 0;

  if (node->local_.type() != LocalFileLocation::Type::Full) {
    LOG(INFO) << "Need full local location to upload file " << file_id;
    return false;
  }

  auto status = check_local_location(node);
  if (status.is_error()) {
    LOG(INFO) << "Need full local location to upload file " << file_id << ": " << status;
    return false;
  }

  run_upload(node, std::vector<int>());
  try_flush_node(node);
  return true;
}

void FileManager::external_file_generate_progress(int64 id, int32 expected_size, int32 local_prefix_size,
                                                  Promise<> promise) {
  send_closure(file_generate_manager_, &FileGenerateManager::external_file_generate_progress, id, expected_size,
               local_prefix_size, std::move(promise));
}
void FileManager::external_file_generate_finish(int64 id, Status status, Promise<> promise) {
  send_closure(file_generate_manager_, &FileGenerateManager::external_file_generate_finish, id, std::move(status),
               std::move(promise));
}

void FileManager::run_generate(FileNodePtr node) {
  if (node->need_load_from_pmc_) {
    return;
  }
  FileView file_view(node);
  if (file_view.has_local_location() || file_view.can_download_from_server() || !file_view.can_generate()) {
    return;
  }

  int8 download_priority = 0;
  int8 upload_priority = 0;
  FileId file_id = node->main_file_id_;
  for (auto id : node->file_ids_) {
    auto *info = get_file_id_info(id);
    if (info->download_priority_ > download_priority) {
      download_priority = info->download_priority_;
      if (download_priority > upload_priority) {
        file_id = id;
      }
    }
    if (info->upload_priority_ > upload_priority) {
      upload_priority = info->upload_priority_;
      if (upload_priority > download_priority) {
        file_id = id;
      }
    }
  }

  auto old_priority = node->generate_priority_;
  node->set_generate_priority(download_priority, upload_priority);

  if (node->generate_priority_ == 0) {
    if (old_priority != 0) {
      LOG(INFO) << "Cancel file " << file_id << " generation";
      cancel_generate(node);
    }
    return;
  }

  if (old_priority != 0) {
    LOG(INFO) << "TODO: change file " << file_id << " generation priority";
    return;
  }

  QueryId id = queries_container_.create(Query{file_id, Query::Generate});
  node->generate_id_ = id;
  send_closure(file_generate_manager_, &FileGenerateManager::generate_file, id, *node->generate_, node->local_,
               node->suggested_name(), [file_manager = this, id] {
                 class Callback : public FileGenerateCallback {
                   ActorId<FileManager> actor_;
                   uint64 query_id_;

                  public:
                   Callback(ActorId<FileManager> actor, QueryId id) : actor_(std::move(actor)), query_id_(id) {
                   }
                   void on_partial_generate(const PartialLocalFileLocation &partial_local,
                                            int32 expected_size) override {
                     send_closure(actor_, &FileManager::on_partial_generate, query_id_, partial_local, expected_size);
                   }
                   void on_ok(const FullLocalFileLocation &local) override {
                     send_closure(actor_, &FileManager::on_generate_ok, query_id_, local);
                   }
                   void on_error(Status error) override {
                     send_closure(actor_, &FileManager::on_error, query_id_, std::move(error));
                   }
                 };
                 return std::make_unique<Callback>(file_manager->actor_id(file_manager), id);
               }());

  LOG(INFO) << "File " << file_id << " generate request has sent to FileGenerateManager";
}

void FileManager::run_upload(FileNodePtr node, std::vector<int> bad_parts) {
  if (node->need_load_from_pmc_) {
    return;
  }
  if (node->upload_pause_.is_valid()) {
    return;
  }
  FileView file_view(node);
  if (!file_view.has_local_location()) {
    if (node->get_by_hash_ || node->generate_id_ == 0 || !node->generate_was_update_) {
      return;
    }
    if (file_view.has_generate_location() && file_view.generate_location().file_type_ == FileType::Secure) {
      // Can't upload secure file before its size is known.
      return;
    }
  }
  int8 priority = 0;
  FileId file_id = node->main_file_id_;
  for (auto id : node->file_ids_) {
    auto *info = get_file_id_info(id);
    if (info->upload_priority_ > priority) {
      priority = info->upload_priority_;
      file_id = id;
    }
  }

  auto old_priority = node->upload_priority_;
  node->set_upload_priority(priority);

  if (priority == 0) {
    if (old_priority != 0) {
      LOG(INFO) << "Cancel file " << file_id << " uploading";
      cancel_upload(node);
    }
    return;
  }

  // create encryption key if necessary
  if (((file_view.has_generate_location() && file_view.generate_location().file_type_ == FileType::Encrypted) ||
       (file_view.has_local_location() && file_view.local_location().file_type_ == FileType::Encrypted)) &&
      file_view.encryption_key().empty()) {
    CHECK(!node->file_ids_.empty());
    bool success = set_encryption_key(node->file_ids_[0], FileEncryptionKey::create());
    LOG_IF(FATAL, !success) << "Failed to set encryption key for file " << file_id;
  }

  // create encryption key if necessary
  if (file_view.has_local_location() && file_view.local_location().file_type_ == FileType::Secure &&
      file_view.encryption_key().empty()) {
    CHECK(!node->file_ids_.empty());
    bool success = set_encryption_key(node->file_ids_[0], FileEncryptionKey::create_secure_key());
    LOG_IF(FATAL, !success) << "Failed to set encryption key for file " << file_id;
  }

  if (old_priority != 0) {
    LOG(INFO) << "File " << file_id << " is already uploading";
    CHECK(node->upload_id_ != 0);
    send_closure(file_load_manager_, &FileLoadManager::update_priority, node->upload_id_, narrow_cast<int8>(-priority));
    return;
  }

  CHECK(node->upload_id_ == 0);
  if (node->remote_.type() != RemoteFileLocation::Type::Partial && node->get_by_hash_) {
    QueryId id = queries_container_.create(Query{file_id, Query::UploadByHash});
    node->upload_id_ = id;

    send_closure(file_load_manager_, &FileLoadManager::upload_by_hash, id, node->local_.full(), node->size_,
                 narrow_cast<int8>(-priority));
    return;
  }

  QueryId id = queries_container_.create(Query{file_id, Query::Upload});
  node->upload_id_ = id;
  send_closure(file_load_manager_, &FileLoadManager::upload, id, node->local_, node->remote_, node->size_,
               node->encryption_key_, narrow_cast<int8>(bad_parts.empty() ? -priority : priority),
               std::move(bad_parts));

  LOG(INFO) << "File " << file_id << " upload request has sent to FileLoadManager";
}

void FileManager::upload(FileId file_id, std::shared_ptr<UploadCallback> callback, int32 new_priority,
                         uint64 upload_order) {
  return resume_upload(file_id, std::vector<int>(), std::move(callback), new_priority, upload_order);
}

// is't quite stupid, yep
// 0x00 <count of zeroes>
static string zero_decode(Slice s) {
  string res;
  for (size_t n = s.size(), i = 0; i < n; i++) {
    if (i + 1 < n && s[i] == 0) {
      res.append(static_cast<unsigned char>(s[i + 1]), 0);
      i++;
      continue;
    }
    res.push_back(s[i]);
  }
  return res;
}

static string zero_encode(Slice s) {
  string res;
  for (size_t n = s.size(), i = 0; i < n; i++) {
    res.push_back(s[i]);
    if (s[i] == 0) {
      unsigned char cnt = 1;
      while (cnt < 250 && i + cnt < n && s[i + cnt] == 0) {
        cnt++;
      }
      res.push_back(cnt);
      i += cnt - 1;
    }
  }
  return res;
}

static bool is_document_type(FileType type) {
  return type == FileType::Document || type == FileType::Sticker || type == FileType::Audio ||
         type == FileType::Animation;
}

string FileManager::get_persistent_id(const FullRemoteFileLocation &location) {
  auto binary = serialize(location);

  binary = zero_encode(binary);
  binary.push_back(PERSISTENT_ID_VERSION);
  return base64url_encode(binary);
}

Result<string> FileManager::to_persistent_id(FileId file_id) {
  auto view = get_file_view(file_id);
  if (view.empty()) {
    return Status::Error(10, "Unknown file id");
  }
  if (!view.has_remote_location()) {
    return Status::Error(10, "File has no persistent id");
  }
  return get_persistent_id(view.remote_location());
}

Result<FileId> FileManager::from_persistent_id(CSlice persistent_id, FileType file_type) {
  if (persistent_id.find('.') != string::npos) {
    string input_url = persistent_id.str();  // TODO do not copy persistent_id
    TRY_RESULT(http_url, parse_url(input_url));
    auto url = http_url.get_url();
    if (!clean_input_string(url)) {
      return Status::Error(400, "URL must be in UTF-8");
    }
    return register_url(std::move(url), file_type, FileLocationSource::FromUser, DialogId());
  }

  auto r_binary = base64url_decode(persistent_id);
  if (r_binary.is_error()) {
    return Status::Error(10, PSLICE() << "Wrong remote file id specified: " << r_binary.error().message());
  }
  auto binary = r_binary.move_as_ok();
  if (binary.empty()) {
    return Status::Error(10, "Remote file id can't be empty");
  }
  if (binary.back() != PERSISTENT_ID_VERSION) {
    return Status::Error(10, "Wrong remote file id specified: can't unserialize it. Wrong last symbol");
  }
  binary.pop_back();
  binary = zero_decode(binary);
  FullRemoteFileLocation remote_location;
  auto status = unserialize(remote_location, binary);
  if (status.is_error()) {
    return Status::Error(10, "Wrong remote file id specified: can't unserialize it");
  }
  auto &real_file_type = remote_location.file_type_;
  if (is_document_type(real_file_type) && is_document_type(file_type)) {
    real_file_type = file_type;
  } else if (real_file_type != file_type && file_type != FileType::Temp) {
    return Status::Error(10, "Type of file mismatch");
  }
  FileData data;
  data.remote_ = RemoteFileLocation(std::move(remote_location));
  return register_file(std::move(data), FileLocationSource::FromUser, "from_persistent_id", false).move_as_ok();
}

FileView FileManager::get_file_view(FileId file_id) const {
  auto file_node = get_file_node(file_id);
  if (!file_node) {
    return FileView();
  }
  return FileView(file_node);
}
FileView FileManager::get_sync_file_view(FileId file_id) {
  auto file_node = get_sync_file_node(file_id);
  if (!file_node) {
    return FileView();
  }
  return FileView(file_node);
}

tl_object_ptr<td_api::file> FileManager::get_file_object(FileId file_id, bool with_main_file_id) {
  auto file_view = get_sync_file_view(file_id);

  if (file_view.empty()) {
    return td_api::make_object<td_api::file>(0, 0, 0, td_api::make_object<td_api::localFile>(),
                                             td_api::make_object<td_api::remoteFile>());
  }

  string persistent_file_id;
  if (file_view.has_remote_location()) {
    persistent_file_id = get_persistent_id(file_view.remote_location());
  } else if (file_view.has_url()) {
    persistent_file_id = file_view.url();
  }

  int32 size = narrow_cast<int32>(file_view.size());
  int32 expected_size = narrow_cast<int32>(file_view.expected_size());
  int32 local_size = narrow_cast<int32>(file_view.local_size());
  int32 local_total_size = narrow_cast<int32>(file_view.local_total_size());
  int32 remote_size = narrow_cast<int32>(file_view.remote_size());
  string path = file_view.path();
  bool can_be_downloaded = file_view.can_download_from_server() || file_view.can_generate();
  bool can_be_deleted = file_view.can_delete();

  auto result_file_id = file_id;
  auto *file_info = get_file_id_info(result_file_id);
  if (with_main_file_id) {
    if (!file_info->send_updates_flag_) {
      result_file_id = file_view.file_id();
    }
    file_info = get_file_id_info(file_view.file_id());
  }
  file_info->send_updates_flag_ = true;
  VLOG(update_file) << "Send file " << file_id << " as " << result_file_id << " and update send_updates_flag_ for file "
                    << (with_main_file_id ? file_view.file_id() : result_file_id);

  return td_api::make_object<td_api::file>(
      result_file_id.get(), size, expected_size,
      td_api::make_object<td_api::localFile>(std::move(path), can_be_downloaded, can_be_deleted,
                                             file_view.is_downloading(), file_view.has_local_location(), local_size,
                                             local_total_size),
      td_api::make_object<td_api::remoteFile>(std::move(persistent_file_id), file_view.is_uploading(),
                                              file_view.has_remote_location(), remote_size));
}

vector<tl_object_ptr<td_api::file>> FileManager::get_files_object(const vector<FileId> &file_ids,
                                                                  bool with_main_file_id) {
  return transform(file_ids,
                   [this, with_main_file_id](FileId file_id) { return get_file_object(file_id, with_main_file_id); });
}

Result<FileId> FileManager::check_input_file_id(FileType type, Result<FileId> result, bool is_encrypted,
                                                bool allow_zero, bool is_secure) {
  TRY_RESULT(file_id, std::move(result));
  if (allow_zero && !file_id.is_valid()) {
    return FileId();
  }

  auto file_node = get_file_node(file_id);
  if (!file_node) {
    return Status::Error(6, "File not found");
  }
  auto file_view = FileView(file_node);
  FileType real_type = file_view.get_type();
  if (!is_encrypted && !is_secure) {
    if (real_type != type && !(real_type == FileType::Temp && file_view.has_url()) &&
        !(is_document_type(real_type) && is_document_type(type))) {
      // TODO: send encrypted file to unencrypted chat
      return Status::Error(6, "Type of file mismatch");
    }
  }

  if (!file_view.has_remote_location()) {
    // TODO why not return file_id here? We will dup it anyway
    // But it will not be duped if has_input_media(), so for now we can't return main_file_id
    return dup_file_id(file_id);
  }
  return file_node->main_file_id_;
}

Result<FileId> FileManager::get_input_thumbnail_file_id(const tl_object_ptr<td_api::InputFile> &thumbnail_input_file,
                                                        DialogId owner_dialog_id, bool is_encrypted) {
  if (thumbnail_input_file == nullptr) {
    return Status::Error(6, "inputThumbnail not specified");
  }

  switch (thumbnail_input_file->get_id()) {
    case td_api::inputFileLocal::ID: {
      const string &path = static_cast<const td_api::inputFileLocal *>(thumbnail_input_file.get())->path_;
      return register_local(
          FullLocalFileLocation(is_encrypted ? FileType::EncryptedThumbnail : FileType::Thumbnail, path, 0),
          owner_dialog_id, 0, false);
    }
    case td_api::inputFileId::ID:
      return Status::Error(6, "InputFileId is not supported for thumbnails");
    case td_api::inputFileRemote::ID:
      return Status::Error(6, "InputFileRemote is not supported for thumbnails");
    case td_api::inputFileGenerated::ID: {
      auto *generated_thumbnail = static_cast<const td_api::inputFileGenerated *>(thumbnail_input_file.get());
      return register_generate(is_encrypted ? FileType::EncryptedThumbnail : FileType::Thumbnail,
                               FileLocationSource::FromUser, generated_thumbnail->original_path_,
                               generated_thumbnail->conversion_, owner_dialog_id, generated_thumbnail->expected_size_);
    }
    default:
      UNREACHABLE();
      return Status::Error(500, "Unreachable");
  }
}

Result<FileId> FileManager::get_input_file_id(FileType type, const tl_object_ptr<td_api::InputFile> &file,
                                              DialogId owner_dialog_id, bool allow_zero, bool is_encrypted,
                                              bool get_by_hash, bool is_secure) {
  if (!file) {
    if (allow_zero) {
      return FileId();
    }
    return Status::Error(6, "InputFile not specified");
  }

  if (is_encrypted || is_secure) {
    get_by_hash = false;
  }

  auto new_type = is_encrypted ? FileType::Encrypted : (is_secure ? FileType::Secure : type);

  auto r_file_id = [&]() -> Result<FileId> {
    switch (file->get_id()) {
      case td_api::inputFileLocal::ID: {
        const string &path = static_cast<const td_api::inputFileLocal *>(file.get())->path_;
        if (allow_zero && path.empty()) {
          return FileId();
        }
        return register_local(FullLocalFileLocation(new_type, path, 0), owner_dialog_id, 0, get_by_hash);
      }
      case td_api::inputFileId::ID: {
        FileId file_id(static_cast<const td_api::inputFileId *>(file.get())->id_, 0);
        if (!file_id.is_valid()) {
          return FileId();
        }
        return file_id;
      }
      case td_api::inputFileRemote::ID: {
        const string &file_persistent_id = static_cast<const td_api::inputFileRemote *>(file.get())->id_;
        if (allow_zero && file_persistent_id.empty()) {
          return FileId();
        }
        return from_persistent_id(file_persistent_id, type);
      }
      case td_api::inputFileGenerated::ID: {
        auto *generated_file = static_cast<const td_api::inputFileGenerated *>(file.get());
        return register_generate(new_type, FileLocationSource::FromUser, generated_file->original_path_,
                                 generated_file->conversion_, owner_dialog_id, generated_file->expected_size_);
      }
      default:
        UNREACHABLE();
        return Status::Error(500, "Unreachable");
    }
  }();

  return check_input_file_id(type, std::move(r_file_id), is_encrypted, allow_zero, is_secure);
}

Result<FileId> FileManager::get_map_thumbnail_file_id(Location location, int32 zoom, int32 width, int32 height,
                                                      int32 scale, DialogId owner_dialog_id) {
  if (!location.is_valid_map_point()) {
    return Status::Error(6, "Invalid location specified");
  }
  if (zoom < 13 || zoom > 20) {
    return Status::Error(6, "Wrong zoom");
  }
  if (width < 16 || width > 1024) {
    return Status::Error(6, "Wrong width");
  }
  if (height < 16 || height > 1024) {
    return Status::Error(6, "Wrong height");
  }
  if (scale < 1 || scale > 3) {
    return Status::Error(6, "Wrong scale");
  }

  const double PI = 3.14159265358979323846;
  double sin_latitude = std::sin(location.get_latitude() * PI / 180);
  int32 size = 256 * (1 << zoom);
  int32 x = static_cast<int32>((location.get_longitude() + 180) / 360 * size);
  int32 y = static_cast<int32>((0.5 - std::log((1 + sin_latitude) / (1 - sin_latitude)) / (4 * PI)) * size);
  x = clamp(x, 0, size - 1);  // just in case
  y = clamp(y, 0, size - 1);  // just in case

  string conversion = PSTRING() << "#map#" << zoom << "#" << x << "#" << y << "#" << width << "#" << height << "#"
                                << scale << "#";
  return register_generate(FileType::Thumbnail, FileLocationSource::FromUser, string(), std::move(conversion),
                           owner_dialog_id, 0);
}

vector<tl_object_ptr<telegram_api::InputDocument>> FileManager::get_input_documents(const vector<FileId> &file_ids) {
  vector<tl_object_ptr<telegram_api::InputDocument>> result;
  result.reserve(file_ids.size());
  for (auto file_id : file_ids) {
    auto file_view = get_file_view(file_id);
    CHECK(!file_view.empty());
    CHECK(file_view.has_remote_location());
    CHECK(!file_view.remote_location().is_web());
    result.push_back(file_view.remote_location().as_input_document());
  }
  return result;
}

FileId FileManager::next_file_id() {
  if (!empty_file_ids_.empty()) {
    auto res = empty_file_ids_.back();
    empty_file_ids_.pop_back();
    return FileId{res, 0};
  }
  FileId res(static_cast<int32>(file_id_info_.size()), 0);
  // LOG(ERROR) << "NEXT file_id " << res;
  file_id_info_.push_back({});
  return res;
}

FileManager::FileNodeId FileManager::next_file_node_id() {
  FileNodeId res = static_cast<FileNodeId>(file_nodes_.size());
  file_nodes_.emplace_back(nullptr);
  return res;
}

void FileManager::on_start_download(QueryId query_id) {
  if (is_closed_) {
    return;
  }

  auto query = queries_container_.get(query_id);
  CHECK(query != nullptr);

  auto file_id = query->file_id_;
  auto file_node = get_file_node(file_id);
  LOG(DEBUG) << "Receive on_start_download for file " << file_id;
  if (!file_node) {
    return;
  }
  if (file_node->download_id_ != query_id) {
    return;
  }

  LOG(DEBUG) << "Start to download part of file " << file_id;
  file_node->is_download_started_ = true;
}

void FileManager::on_partial_download(QueryId query_id, const PartialLocalFileLocation &partial_local,
                                      int64 ready_size) {
  if (is_closed_) {
    return;
  }

  auto query = queries_container_.get(query_id);
  CHECK(query != nullptr);

  auto file_id = query->file_id_;
  auto file_node = get_file_node(file_id);
  LOG(DEBUG) << "Receive on_parial_download for file " << file_id;
  if (!file_node) {
    return;
  }
  if (file_node->download_id_ != query_id) {
    return;
  }

  file_node->set_local_location(LocalFileLocation(partial_local), ready_size);
  try_flush_node(file_node);
}

void FileManager::on_hash(QueryId query_id, string hash) {
  if (is_closed_) {
    return;
  }

  auto query = queries_container_.get(query_id);
  CHECK(query != nullptr);

  auto file_id = query->file_id_;

  auto file_node = get_file_node(file_id);
  LOG(DEBUG) << "Receive on_hash for file " << file_id;
  if (!file_node) {
    return;
  }
  if (file_node->upload_id_ != query_id) {
    return;
  }

  file_node->encryption_key_.set_value_hash(secure_storage::ValueHash::create(hash).move_as_ok());
}

void FileManager::on_partial_upload(QueryId query_id, const PartialRemoteFileLocation &partial_remote,
                                    int64 ready_size) {
  if (is_closed_) {
    return;
  }

  auto query = queries_container_.get(query_id);
  CHECK(query != nullptr);

  auto file_id = query->file_id_;
  auto file_node = get_file_node(file_id);
  LOG(DEBUG) << "Receive on_partial_upload for file " << file_id;
  if (!file_node) {
    return;
  }
  if (file_node->upload_id_ != query_id) {
    return;
  }

  file_node->set_remote_location(RemoteFileLocation(partial_remote), FileLocationSource::None, ready_size);
  try_flush_node(file_node);
}

void FileManager::on_download_ok(QueryId query_id, const FullLocalFileLocation &local, int64 size) {
  if (is_closed_) {
    return;
  }

  auto file_id = finish_query(query_id).first.file_id_;
  LOG(INFO) << "ON DOWNLOAD OK file " << file_id << " of size " << size;
  auto r_new_file_id = register_local(local, DialogId(), size);
  if (r_new_file_id.is_error()) {
    LOG(ERROR) << "Can't register local file after download: " << r_new_file_id.error();
  } else {
    context_->on_new_file(get_file_view(r_new_file_id.ok()).size());
    LOG_STATUS(merge(r_new_file_id.ok(), file_id));
  }
}

void FileManager::on_upload_ok(QueryId query_id, FileType file_type, const PartialRemoteFileLocation &partial_remote,
                               int64 size) {
  if (is_closed_) {
    return;
  }

  CHECK(partial_remote.ready_part_count_ == partial_remote.part_count_);
  auto some_file_id = finish_query(query_id).first.file_id_;
  LOG(INFO) << "ON UPLOAD OK file " << some_file_id << " of size " << size;

  auto file_node = get_file_node(some_file_id);
  if (!file_node) {
    return;
  }

  FileId file_id;
  uint64 file_id_upload_order{std::numeric_limits<uint64>::max()};
  for (auto id : file_node->file_ids_) {
    auto *info = get_file_id_info(id);
    if (info->upload_priority_ != 0 && info->upload_order_ < file_id_upload_order) {
      file_id = id;
      file_id_upload_order = info->upload_order_;
    }
  }
  if (!file_id.is_valid()) {
    return;
  }

  auto *file_info = get_file_id_info(file_id);
  file_info->upload_priority_ = 0;
  file_info->download_priority_ = 0;

  FileView file_view(file_node);
  string file_name = get_file_name(file_type, file_view.suggested_name());

  if (file_view.is_encrypted_secret()) {
    tl_object_ptr<telegram_api::InputEncryptedFile> input_file;
    if (partial_remote.is_big_) {
      input_file = make_tl_object<telegram_api::inputEncryptedFileBigUploaded>(
          partial_remote.file_id_, partial_remote.part_count_, file_view.encryption_key().calc_fingerprint());
    } else {
      input_file = make_tl_object<telegram_api::inputEncryptedFileUploaded>(
          partial_remote.file_id_, partial_remote.part_count_, "", file_view.encryption_key().calc_fingerprint());
    }
    if (file_info->upload_callback_) {
      file_info->upload_callback_->on_upload_encrypted_ok(file_id, std::move(input_file));
      file_node->upload_pause_ = file_id;
      file_info->upload_callback_.reset();
    }
  } else if (file_view.is_secure()) {
    tl_object_ptr<telegram_api::InputSecureFile> input_file;
    input_file = make_tl_object<telegram_api::inputSecureFileUploaded>(
        partial_remote.file_id_, partial_remote.part_count_, "" /*md5*/, BufferSlice() /*file_hash*/,
        BufferSlice() /*encrypted_secret*/);
    if (file_info->upload_callback_) {
      file_info->upload_callback_->on_upload_secure_ok(file_id, std::move(input_file));
      file_node->upload_pause_ = file_id;
      file_info->upload_callback_.reset();
    }
  } else {
    tl_object_ptr<telegram_api::InputFile> input_file;
    if (partial_remote.is_big_) {
      input_file = make_tl_object<telegram_api::inputFileBig>(partial_remote.file_id_, partial_remote.part_count_,
                                                              std::move(file_name));
    } else {
      input_file = make_tl_object<telegram_api::inputFile>(partial_remote.file_id_, partial_remote.part_count_,
                                                           std::move(file_name), "");
    }
    if (file_info->upload_callback_) {
      file_info->upload_callback_->on_upload_ok(file_id, std::move(input_file));
      file_node->upload_pause_ = file_id;
      file_info->upload_callback_.reset();
    }
  }
}

void FileManager::on_upload_full_ok(QueryId query_id, const FullRemoteFileLocation &remote) {
  if (is_closed_) {
    return;
  }

  auto file_id = finish_query(query_id).first.file_id_;
  LOG(INFO) << "ON UPLOAD FULL OK for file " << file_id;
  auto new_file_id = register_remote(remote, FileLocationSource::FromServer, DialogId(), 0, 0, "");
  LOG_STATUS(merge(new_file_id, file_id));
}

void FileManager::on_partial_generate(QueryId query_id, const PartialLocalFileLocation &partial_local,
                                      int32 expected_size) {
  if (is_closed_) {
    return;
  }

  auto query = queries_container_.get(query_id);
  CHECK(query != nullptr);

  auto file_id = query->file_id_;
  auto file_node = get_file_node(file_id);
  LOG(DEBUG) << "Receive on_parital_generate for file " << file_id << ": " << partial_local.path_ << " "
             << partial_local.ready_part_count_;
  if (!file_node) {
    return;
  }
  if (file_node->generate_id_ != query_id) {
    return;
  }
  file_node->set_local_location(LocalFileLocation(partial_local), 0);
  // TODO check for size and local_size, abort generation if needed
  if (expected_size > 0) {
    file_node->set_expected_size(expected_size);
  }
  if (!file_node->generate_was_update_) {
    file_node->generate_was_update_ = true;
    run_upload(file_node, {});
  }
  if (file_node->upload_id_ != 0) {
    send_closure(file_load_manager_, &FileLoadManager::update_local_file_location, file_node->upload_id_,
                 LocalFileLocation(partial_local));
  }

  try_flush_node(file_node);
}

void FileManager::on_generate_ok(QueryId query_id, const FullLocalFileLocation &local) {
  if (is_closed_) {
    return;
  }

  Query query;
  bool was_active;
  std::tie(query, was_active) = finish_query(query_id);
  auto generate_file_id = query.file_id_;

  LOG(INFO) << "Receive on_generate_ok for file " << generate_file_id << ": " << local;
  auto file_node = get_file_node(generate_file_id);
  if (!file_node) {
    return;
  }

  auto old_upload_id = file_node->upload_id_;

  auto r_new_file_id = register_local(local, DialogId(), 0);
  Status status;
  if (r_new_file_id.is_error()) {
    status = Status::Error(PSLICE() << "Can't register local file after generate: " << r_new_file_id.error());
  } else {
    auto result = merge(r_new_file_id.ok(), generate_file_id);
    if (result.is_error()) {
      status = result.move_as_error();
    }
  }
  file_node = get_file_node(generate_file_id);
  if (status.is_error()) {
    return on_error_impl(file_node, query.type_, was_active, std::move(status));
  }

  CHECK(file_node);
  context_->on_new_file(FileView(file_node).size());

  run_upload(file_node, {});

  if (was_active) {
    if (old_upload_id != 0 && old_upload_id == file_node->upload_id_) {
      send_closure(file_load_manager_, &FileLoadManager::update_local_file_location, file_node->upload_id_,
                   LocalFileLocation(local));
    }
  }
}

void FileManager::on_error(QueryId query_id, Status status) {
  if (is_closed_) {
    return;
  }

  Query query;
  bool was_active;
  std::tie(query, was_active) = finish_query(query_id);
  auto node = get_file_node(query.file_id_);
  if (!node) {
    LOG(ERROR) << "Can't find file node for " << query.file_id_ << " " << status;
    return;
  }

  if (query.type_ == Query::UploadByHash && !G()->close_flag()) {
    LOG(INFO) << "Upload By Hash failed: " << status << ", restart upload";
    node->get_by_hash_ = false;
    run_upload(node, {});
    return;
  }
  on_error_impl(node, query.type_, was_active, std::move(status));
}

void FileManager::on_error_impl(FileNodePtr node, FileManager::Query::Type type, bool was_active, Status status) {
  SCOPE_EXIT {
    try_flush_node(node);
  };
  if (status.code() != 1 && !G()->close_flag()) {
    LOG(WARNING) << "Failed to upload/download/generate file: " << status << ". Query type = " << type
                 << ". File type is " << file_type_name[static_cast<int32>(FileView(node).get_type())];
    if (status.code() == 0) {
      // Remove partial locations
      if (node->local_.type() == LocalFileLocation::Type::Partial && status.message() != "FILE_UPLOAD_RESTART") {
        LOG(INFO) << "Unlink file " << node->local_.partial().path_;
        unlink(node->local_.partial().path_).ignore();
        node->set_local_location(LocalFileLocation(), 0);
      }
      if (node->remote_.type() == RemoteFileLocation::Type::Partial) {
        node->set_remote_location(RemoteFileLocation(), FileLocationSource::None, 0);
      }
      status = Status::Error(400, status.message());
    }
  }

  if (status.message() == "FILE_UPLOAD_RESTART") {
    run_upload(node, {});
    return;
  }
  if (status.message() == "FILE_DOWNLOAD_RESTART") {
    node->can_search_locally_ = false;
    run_download(node);
    return;
  }

  if (!was_active) {
    return;
  }

  // Stop everything on error
  cancel_generate(node);
  cancel_download(node);
  cancel_upload(node);

  for (auto file_id : vector<FileId>(node->file_ids_)) {
    auto *info = get_file_id_info(file_id);
    if (info->download_priority_ != 0) {
      info->download_priority_ = 0;
      if (info->download_callback_) {
        info->download_callback_->on_download_error(file_id, status.clone());
        info->download_callback_.reset();
      }
    }
    if (info->upload_priority_ != 0) {
      info->upload_priority_ = 0;
      if (info->upload_callback_) {
        info->upload_callback_->on_upload_error(file_id, status.clone());
        info->upload_callback_.reset();
      }
    }
  }
}

std::pair<FileManager::Query, bool> FileManager::finish_query(QueryId query_id) {
  SCOPE_EXIT {
    queries_container_.erase(query_id);
  };
  auto query = queries_container_.get(query_id);
  CHECK(query != nullptr);

  auto res = *query;
  auto node = get_file_node(res.file_id_);
  if (!node) {
    return std::make_pair(res, false);
  }
  bool was_active = false;
  if (node->generate_id_ == query_id) {
    node->generate_id_ = 0;
    node->generate_was_update_ = false;
    node->set_generate_priority(0, 0);
    was_active = true;
  }
  if (node->download_id_ == query_id) {
    node->download_id_ = 0;
    node->is_download_started_ = false;
    node->set_download_priority(0);
    was_active = true;
  }
  if (node->upload_id_ == query_id) {
    node->upload_id_ = 0;
    node->set_upload_priority(0);
    was_active = true;
  }
  return std::make_pair(res, was_active);
}

FullRemoteFileLocation *FileManager::get_remote(int32 key) {
  if (key == 0) {
    return nullptr;
  }
  return &remote_location_info_.get(key).remote_;
}

void FileManager::hangup() {
  file_db_.reset();
  file_generate_manager_.reset();
  file_load_manager_.reset();
  while (!queries_container_.empty()) {
    auto ids = queries_container_.ids();
    for (auto id : ids) {
      on_error(id, Status::Error(500, "Request aborted"));
    }
  }
  is_closed_ = true;
  stop();
}

void FileManager::tear_down() {
  parent_.reset();
}

}  // namespace td
