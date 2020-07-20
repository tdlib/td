//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/files/FileManager.h"

#include "td/telegram/telegram_api.h"

#include "td/telegram/ConfigShared.h"
#include "td/telegram/FileReferenceManager.h"
#include "td/telegram/files/FileData.h"
#include "td/telegram/files/FileDb.h"
#include "td/telegram/files/FileLoaderUtils.h"
#include "td/telegram/files/FileLocation.h"
#include "td/telegram/files/FileLocation.hpp"
#include "td/telegram/Global.h"
#include "td/telegram/logevent/LogEvent.h"
#include "td/telegram/misc.h"
#include "td/telegram/SecureStorage.h"
#include "td/telegram/TdDb.h"
#include "td/telegram/Version.h"

#include "td/actor/SleepActor.h"

#include "td/utils/base64.h"
#include "td/utils/crypto.h"
#include "td/utils/filesystem.h"
#include "td/utils/format.h"
#include "td/utils/HttpUrl.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/PathView.h"
#include "td/utils/port/FileFd.h"
#include "td/utils/port/path.h"
#include "td/utils/port/Stat.h"
#include "td/utils/ScopeGuard.h"
#include "td/utils/StringBuilder.h"
#include "td/utils/Time.h"
#include "td/utils/tl_helpers.h"
#include "td/utils/tl_parsers.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <tuple>
#include <unordered_set>
#include <utility>

namespace td {
namespace {
constexpr int64 MAX_FILE_SIZE = 2000 * (1 << 20) /* 2000MB */;
}  // namespace

int VERBOSITY_NAME(update_file) = VERBOSITY_NAME(INFO);

StringBuilder &operator<<(StringBuilder &string_builder, FileLocationSource source) {
  switch (source) {
    case FileLocationSource::None:
      return string_builder << "None";
    case FileLocationSource::FromUser:
      return string_builder << "User";
    case FileLocationSource::FromBinlog:
      return string_builder << "Binlog";
    case FileLocationSource::FromDatabase:
      return string_builder << "Database";
    case FileLocationSource::FromServer:
      return string_builder << "Server";
    default:
      UNREACHABLE();
      return string_builder << "Unknown";
  }
}

StringBuilder &operator<<(StringBuilder &string_builder, FileManager::Query::Type type) {
  switch (type) {
    case FileManager::Query::Type::UploadByHash:
      return string_builder << "UploadByHash";
    case FileManager::Query::Type::UploadWaitFileReference:
      return string_builder << "UploadWaitFileReference";
    case FileManager::Query::Type::Upload:
      return string_builder << "Upload";
    case FileManager::Query::Type::DownloadWaitFileReference:
      return string_builder << "DownloadWaitFileReference";
    case FileManager::Query::Type::DownloadReloadDialog:
      return string_builder << "DownloadReloadDialog";
    case FileManager::Query::Type::Download:
      return string_builder << "Download";
    case FileManager::Query::Type::SetContent:
      return string_builder << "SetContent";
    case FileManager::Query::Type::Generate:
      return string_builder << "Generate";
    default:
      UNREACHABLE();
      return string_builder << "Unknown";
  }
}

NewRemoteFileLocation::NewRemoteFileLocation(RemoteFileLocation remote, FileLocationSource source) {
  switch (remote.type()) {
    case RemoteFileLocation::Type::Empty:
      break;
    case RemoteFileLocation::Type::Partial:
      partial = make_unique<PartialRemoteFileLocation>(remote.partial());
      break;
    case RemoteFileLocation::Type::Full:
      full = remote.full();
      full_source = source;
      is_full_alive = true;
      break;
    default:
      UNREACHABLE();
  }
}

RemoteFileLocation NewRemoteFileLocation::partial_or_empty() const {
  if (partial) {
    return RemoteFileLocation(*partial);
  }
  return {};
}

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

void FileNode::recalc_ready_prefix_size(int64 prefix_offset, int64 ready_prefix_size) {
  if (local_.type() != LocalFileLocation::Type::Partial) {
    return;
  }
  int64 new_local_ready_prefix_size;
  if (download_offset_ == prefix_offset) {
    new_local_ready_prefix_size = ready_prefix_size;
  } else {
    new_local_ready_prefix_size = Bitmask(Bitmask::Decode{}, local_.partial().ready_bitmask_)
                                      .get_ready_prefix_size(download_offset_, local_.partial().part_size_, size_);
  }
  if (new_local_ready_prefix_size != local_ready_prefix_size_) {
    VLOG(update_file) << "File " << main_file_id_ << " has changed local_ready_prefix_size from "
                      << local_ready_prefix_size_ << " to " << new_local_ready_prefix_size;
    local_ready_prefix_size_ = new_local_ready_prefix_size;
    on_info_changed();
  }
}

void FileNode::init_ready_size() {
  if (local_.type() != LocalFileLocation::Type::Partial) {
    return;
  }
  auto bitmask = Bitmask(Bitmask::Decode{}, local_.partial().ready_bitmask_);
  local_ready_prefix_size_ = bitmask.get_ready_prefix_size(0, local_.partial().part_size_, size_);
  local_ready_size_ = bitmask.get_total_size(local_.partial().part_size_, size_);
}

void FileNode::set_download_offset(int64 download_offset) {
  if (download_offset < 0 || download_offset > MAX_FILE_SIZE) {
    return;
  }
  if (download_offset == download_offset_) {
    return;
  }

  VLOG(update_file) << "File " << main_file_id_ << " has changed download_offset from " << download_offset_ << " to "
                    << download_offset;
  download_offset_ = download_offset;
  is_download_offset_dirty_ = true;
  recalc_ready_prefix_size(-1, -1);
  on_info_changed();
}
void FileNode::set_download_limit(int64 download_limit) {
  if (download_limit < 0) {
    return;
  }
  if (download_limit == download_limit_) {
    return;
  }

  VLOG(update_file) << "File " << main_file_id_ << " has changed download_limit from " << download_limit_ << " to "
                    << download_limit;
  download_limit_ = download_limit;
  is_download_limit_dirty_ = true;
}

void FileNode::drop_local_location() {
  set_local_location(LocalFileLocation(), 0, -1, -1);
}

void FileNode::set_local_location(const LocalFileLocation &local, int64 ready_size, int64 prefix_offset,
                                  int64 ready_prefix_size) {
  if (local_ready_size_ != ready_size) {
    VLOG(update_file) << "File " << main_file_id_ << " has changed local ready size from " << local_ready_size_
                      << " to " << ready_size;
    local_ready_size_ = ready_size;
    on_info_changed();
  }
  if (local_ != local) {
    VLOG(update_file) << "File " << main_file_id_ << " has changed local location";
    local_ = local;

    recalc_ready_prefix_size(prefix_offset, ready_prefix_size);

    on_changed();
  }
}

void FileNode::set_new_remote_location(NewRemoteFileLocation new_remote) {
  if (new_remote.full) {
    if (remote_.full && remote_.full.value() == new_remote.full.value()) {
      if (remote_.full.value().get_access_hash() != new_remote.full.value().get_access_hash() ||
          remote_.full.value().get_file_reference() != new_remote.full.value().get_file_reference() ||
          remote_.full.value().get_source() != new_remote.full.value().get_source()) {
        on_pmc_changed();
      }
    } else {
      VLOG(update_file) << "File " << main_file_id_ << " has changed remote location";
      on_changed();
    }
    remote_.full = new_remote.full;
    remote_.full_source = new_remote.full_source;
    remote_.is_full_alive = new_remote.is_full_alive;
  } else {
    if (remote_.full) {
      VLOG(update_file) << "File " << main_file_id_ << " has lost remote location";
      remote_.full = {};
      remote_.is_full_alive = false;
      remote_.full_source = FileLocationSource::None;
      on_changed();
    }
  }

  if (new_remote.partial) {
    set_partial_remote_location(*new_remote.partial, new_remote.ready_size);
  } else {
    delete_partial_remote_location();
  }
}
void FileNode::delete_partial_remote_location() {
  if (remote_.partial) {
    VLOG(update_file) << "File " << main_file_id_ << " has lost partial remote location";
    remote_.partial.reset();
    on_changed();
  }
}

void FileNode::set_partial_remote_location(const PartialRemoteFileLocation &remote, int64 ready_size) {
  if (remote_.is_full_alive) {
    VLOG(update_file) << "File " << main_file_id_ << " remote is still alive, so there is NO reason to update partial";
    return;
  }
  if (remote_.ready_size != ready_size) {
    VLOG(update_file) << "File " << main_file_id_ << " has changed remote ready size from " << remote_.ready_size
                      << " to " << ready_size;
    remote_.ready_size = ready_size;
    on_info_changed();
  }
  if (remote_.partial && *remote_.partial == remote) {
    VLOG(update_file) << "Partial location of " << main_file_id_ << " is NOT changed";
    return;
  }
  if (!remote_.partial && remote.ready_part_count_ == 0) {
    // empty partial remote is equal to empty remote
    VLOG(update_file) << "Partial location of " << main_file_id_
                      << " is still empty, so there is NO reason to update it";
    return;
  }

  VLOG(update_file) << "File " << main_file_id_ << " partial location has changed to " << remote;
  remote_.partial = make_unique<PartialRemoteFileLocation>(remote);
  on_changed();
}

bool FileNode::delete_file_reference(Slice file_reference) {
  if (!remote_.full) {
    VLOG(file_references) << "Can't delete file reference, because there is no remote location";
    return false;
  }

  if (!remote_.full.value().delete_file_reference(file_reference)) {
    VLOG(file_references) << "Can't delete unmatching file reference " << format::escaped(file_reference) << ", have "
                          << format::escaped(remote_.full.value().get_file_reference());
    return false;
  }

  VLOG(file_references) << "Do delete file reference of main file " << main_file_id_;
  upload_was_update_file_reference_ = false;
  download_was_update_file_reference_ = false;
  on_pmc_changed();
  return true;
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

void FileNode::set_upload_pause(FileId upload_pause) {
  if (upload_pause_ != upload_pause) {
    LOG(INFO) << "Change file " << main_file_id_ << " upload_pause from " << upload_pause_ << " to " << upload_pause;
    upload_pause_ = upload_pause;
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
  if (!remote_.is_full_alive && (upload_priority_ == 0) != (priority == 0)) {
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
  if (pmc_id_.is_valid()) {
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

  if (remote_.full/* &&
      (has_generate_location || local_.type() != LocalFileLocation::Type::Empty)*/) {
    // we need to always save file sources
    return true;
  }
  if (local_.type() == LocalFileLocation::Type::Full && (has_generate_location || remote_.full || remote_.partial)) {
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
  return static_cast<bool>(node_->remote_.full);
}

bool FileView::has_alive_remote_location() const {
  return node_->remote_.is_full_alive;
}

bool FileView::has_active_upload_remote_location() const {
  if (!has_remote_location()) {
    return false;
  }
  if (!has_alive_remote_location()) {
    return false;
  }
  if (main_remote_location().is_encrypted_any()) {
    return true;
  }
  return main_remote_location().has_file_reference();
}

bool FileView::has_active_download_remote_location() const {
  if (!has_remote_location()) {
    return false;
  }
  if (remote_location().is_encrypted_any()) {
    return true;
  }
  return remote_location().has_file_reference();
}

const FullRemoteFileLocation &FileView::remote_location() const {
  CHECK(has_remote_location());
  auto *remote = node_.get_remote();
  if (remote) {
    return *remote;
  }
  return node_->remote_.full.value();
}

const FullRemoteFileLocation &FileView::main_remote_location() const {
  CHECK(has_remote_location());
  return node_->remote_.full.value();
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

int64 FileView::get_allocated_local_size() const {
  auto file_path = path();
  if (file_path.empty()) {
    return 0;
  }
  auto r_stat = stat(file_path);
  if (r_stat.is_error()) {
    return 0;
  }
  return r_stat.ok().real_size_;
}

int64 FileView::expected_size(bool may_guess) const {
  if (node_->size_ != 0) {
    return node_->size_;
  }
  int64 current_size = local_total_size();  // TODO: this is not the best approximation
  if (node_->expected_size_ != 0) {
    return max(current_size, node_->expected_size_);
  }
  if (may_guess && node_->local_.type() == LocalFileLocation::Type::Partial) {
    current_size *= 3;
  }
  return current_size;
}

bool FileView::is_downloading() const {
  return node_->download_priority_ != 0 || node_->generate_download_priority_ != 0;
}

int64 FileView::download_offset() const {
  return node_->download_offset_;
}

int64 FileView::downloaded_prefix(int64 offset) const {
  switch (node_->local_.type()) {
    case LocalFileLocation::Type::Empty:
      return 0;
    case LocalFileLocation::Type::Full:
      if (offset < node_->size_) {
        return node_->size_ - offset;
      }
      return 0;
    case LocalFileLocation::Type::Partial:
      if (is_encrypted_secure()) {
        // File is not decrypted and verified yet
        return 0;
      }
      return Bitmask(Bitmask::Decode{}, node_->local_.partial().ready_bitmask_)
          .get_ready_prefix_size(offset, node_->local_.partial().part_size_, node_->size_);
    default:
      UNREACHABLE();
      return 0;
  }
}

int64 FileView::local_prefix_size() const {
  switch (node_->local_.type()) {
    case LocalFileLocation::Type::Full:
      return node_->download_offset_ <= node_->size_ ? node_->size_ - node_->download_offset_ : 0;
    case LocalFileLocation::Type::Partial: {
      if (is_encrypted_secure()) {
        // File is not decrypted and verified yet
        return 0;
      }
      return node_->local_ready_prefix_size_;
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
      VLOG(update_file) << "Have local_ready_prefix_size = " << node_->local_ready_prefix_size_
                        << " and local_ready_size = " << node_->local_ready_size_;
      return max(node_->local_ready_prefix_size_, node_->local_ready_size_);
    default:
      UNREACHABLE();
      return 0;
  }
}

bool FileView::is_uploading() const {
  return node_->upload_priority_ != 0 || node_->generate_upload_priority_ != 0;
}

int64 FileView::remote_size() const {
  if (node_->remote_.is_full_alive) {
    return node_->size_;
  }
  if (node_->remote_.partial) {
    auto part_size = static_cast<int64>(node_->remote_.partial->part_size_);
    auto ready_part_count = node_->remote_.partial->ready_part_count_;
    auto remote_ready_size = node_->remote_.ready_size;
    VLOG(update_file) << "Have part_size = " << part_size << ", remote_ready_part_count = " << ready_part_count
                      << ", remote_ready_size = " << remote_ready_size << ", size = " << size();
    auto res = max(part_size * ready_part_count, remote_ready_size);
    if (size() != 0 && size() < res) {
      res = size();
    }
    return res;
  }
  return node_->remote_.ready_size;  //???
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
  if (!remote_location().is_encrypted_any() && !remote_location().has_file_reference() &&
      ((node_->download_id_ == 0 && node_->download_was_update_file_reference_) || !node_->remote_.is_full_alive)) {
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

string FileView::get_unique_id(const FullGenerateFileLocation &location) {
  return base64url_encode(zero_encode('\xff' + serialize(location)));
}

string FileView::get_unique_id(const FullRemoteFileLocation &location) {
  return base64url_encode(zero_encode(serialize(location.as_unique())));
}

string FileView::get_persistent_id(const FullGenerateFileLocation &location) {
  auto binary = serialize(location);

  binary = zero_encode(binary);
  binary.push_back(FileNode::PERSISTENT_ID_VERSION_MAP);
  return base64url_encode(binary);
}

string FileView::get_persistent_id(const FullRemoteFileLocation &location) {
  auto binary = serialize(location);

  binary = zero_encode(binary);
  binary.push_back(static_cast<char>(narrow_cast<uint8>(Version::Next) - 1));
  binary.push_back(FileNode::PERSISTENT_ID_VERSION);
  return base64url_encode(binary);
}

string FileView::get_persistent_file_id() const {
  if (!empty()) {
    if (has_alive_remote_location()) {
      return get_persistent_id(remote_location());
    } else if (has_url()) {
      return url();
    } else if (has_generate_location() && begins_with(generate_location().conversion_, "#map#")) {
      return get_persistent_id(generate_location());
    }
  }
  return string();
}

string FileView::get_unique_file_id() const {
  if (!empty()) {
    if (has_alive_remote_location()) {
      if (!remote_location().is_web()) {
        return get_unique_id(remote_location());
      }
    } else if (has_generate_location() && begins_with(generate_location().conversion_, "#map#")) {
      return get_unique_id(generate_location());
    }
  }
  return string();
}

/*** FileManager ***/
static int merge_choose_remote_location(const FullRemoteFileLocation &x, FileLocationSource x_source,
                                        const FullRemoteFileLocation &y, FileLocationSource y_source);

namespace {
void prepare_path_for_pmc(FileType file_type, string &path) {
  path = PathView::relative(path, get_files_base_dir(file_type)).str();
}
}  // namespace

FileManager::FileManager(unique_ptr<Context> context) : context_(std::move(context)) {
  if (G()->parameters().use_file_db) {
    file_db_ = G()->td_db()->get_file_db_shared();
  }

  parent_ = context_->create_reference();
  next_file_id();
  next_file_node_id();

  std::unordered_set<string> dir_paths;
  for (int32 i = 0; i < MAX_FILE_TYPE; i++) {
    dir_paths.insert(get_files_dir(static_cast<FileType>(i)));
  }
  // add both temp dirs
  dir_paths.insert(get_files_temp_dir(FileType::Encrypted));
  dir_paths.insert(get_files_temp_dir(FileType::Video));

  for (const auto &path : dir_paths) {
    auto status = mkdir(path, 0750);
    if (status.is_error()) {
      auto r_stat = stat(path);
      if (r_stat.is_ok() && r_stat.ok().is_dir_) {
        LOG(ERROR) << "Creation of directory \"" << path << "\" failed with " << status << ", but directory exists";
      } else {
        LOG(ERROR) << "Creation of directory \"" << path << "\" failed with " << status;
      }
    }
#if TD_ANDROID
    FileFd::open(path + ".nomedia", FileFd::Create | FileFd::Read).ignore();
#endif
  };

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
    case FileType::Wallpaper:
    case FileType::Background:
      if (extension != "jpg" && extension != "jpeg" && extension != "png") {
        return fix_file_extension(file_name, "wallpaper", "jpg");
      }
      break;
    case FileType::Sticker:
      if (extension != "webp" && extension != "tgs") {
        return fix_file_extension(file_name, "sticker", "webp");
      }
      break;
    case FileType::Document:
    case FileType::Animation:
    case FileType::Encrypted:
    case FileType::Temp:
    case FileType::EncryptedThumbnail:
    case FileType::Secure:
    case FileType::SecureRaw:
    case FileType::DocumentAsFile:
      break;
    default:
      UNREACHABLE();
  }
  return file_name.str();
}

bool FileManager::are_modification_times_equal(int64 old_mtime, int64 new_mtime) {
  if (old_mtime == new_mtime) {
    return true;
  }
  if (old_mtime < new_mtime) {
    return false;
  }
  if (old_mtime - new_mtime == 1000000000 && old_mtime % 1000000000 == 0 && new_mtime % 2000000000 == 0) {
    // FAT32 has 2 seconds mtime resolution, but file system sometimes reports odd modification time
    return true;
  }
  return false;
}

Status FileManager::check_local_location(FullLocalFileLocation &location, int64 &size, bool skip_file_size_checks) {
  constexpr int64 MAX_THUMBNAIL_SIZE = 200 * (1 << 10) - 1 /* 200 KB - 1 B */;
  constexpr int64 MAX_PHOTO_SIZE = 10 * (1 << 20) /* 10 MB */;

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
    VLOG(files) << "Set file \"" << location.path_ << "\" modification time to " << stat.mtime_nsec_;
    location.mtime_nsec_ = stat.mtime_nsec_;
  } else if (!are_modification_times_equal(location.mtime_nsec_, stat.mtime_nsec_)) {
    VLOG(files) << "File \"" << location.path_ << "\" was modified: old mtime = " << location.mtime_nsec_
                << ", new mtime = " << stat.mtime_nsec_;
    return Status::Error(PSLICE() << "File \"" << location.path_ << "\" was modified");
  }
  if (skip_file_size_checks) {
    return Status::OK();
  }
  if ((location.file_type_ == FileType::Thumbnail || location.file_type_ == FileType::EncryptedThumbnail) &&
      size > MAX_THUMBNAIL_SIZE && !begins_with(PathView(location.path_).file_name(), "map")) {
    return Status::Error(PSLICE() << "File \"" << location.path_ << "\" is too big for a thumbnail "
                                  << tag("size", format::as_size(size)));
  }
  if (location.file_type_ == FileType::Photo && size > MAX_PHOTO_SIZE) {
    return Status::Error(PSLICE() << "File \"" << location.path_ << "\" is too big for a photo "
                                  << tag("size", format::as_size(size)));
  }
  if (size > MAX_FILE_SIZE) {
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
    status = check_local_location(node->local_.full(), node->size_, false);
  } else if (node->local_.type() == LocalFileLocation::Type::Partial) {
    status = check_partial_local_location(node->local_.partial());
  }
  if (status.is_error()) {
    node->drop_local_location();
    try_flush_node(node, "check_local_location");
  }
  return status;
}

bool FileManager::try_fix_partial_local_location(FileNodePtr node) {
  LOG(INFO) << "Trying to fix partial local location";
  if (node->local_.type() != LocalFileLocation::Type::Partial) {
    LOG(INFO) << "   failed - not a partial location";
    return false;
  }
  auto partial = node->local_.partial();
  if (!partial.iv_.empty()) {
    // can't recalc iv_
    LOG(INFO) << "   failed - partial location has nonempty iv";
    return false;
  }
  if (partial.part_size_ >= 512 * (1 << 10)) {
    LOG(INFO) << "   failed - too big part_size already: " << partial.part_size_;
    return false;
  }
  auto old_part_size = partial.part_size_;
  int new_part_size = 512 * (1 << 10);
  auto k = new_part_size / old_part_size;
  Bitmask mask(Bitmask::Decode(), partial.ready_bitmask_);
  auto new_mask = mask.compress(k);

  partial.part_size_ = new_part_size;
  partial.ready_bitmask_ = new_mask.encode();

  auto ready_size = new_mask.get_total_size(partial.part_size_, node->size_);
  node->set_local_location(LocalFileLocation(partial), ready_size, -1, -1);
  LOG(INFO) << "   ok: increase part_size " << old_part_size << "->" << new_part_size;
  return true;
}

FileManager::FileIdInfo *FileManager::get_file_id_info(FileId file_id) {
  LOG_CHECK(0 <= file_id.get() && file_id.get() < static_cast<int32>(file_id_info_.size()))
      << file_id << " " << file_id_info_.size();
  return &file_id_info_[file_id.get()];
}

FileId FileManager::dup_file_id(FileId file_id) {
  int32 file_node_id;
  auto *file_node = get_file_node_raw(file_id, &file_node_id);
  if (!file_node) {
    return FileId();
  }
  auto result = FileId(create_file_id(file_node_id, file_node).get(), file_id.get_remote());
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
  if (info->send_updates_flag_ || info->pin_flag_ || info->sent_file_id_flag_) {
    return;
  }
  auto file_node = get_file_node(file_id);
  if (file_node->main_file_id_ == file_id) {
    return;
  }

  LOG(DEBUG) << "Forget file " << file_id;
  bool is_removed = td::remove(file_node->file_ids_, file_id);
  CHECK(is_removed);
  *info = FileIdInfo();
  empty_file_ids_.push_back(file_id.get());
}

FileId FileManager::register_empty(FileType type) {
  return register_local(FullLocalFileLocation(type, "", 0), DialogId(), 0, false, true).ok();
}

void FileManager::on_file_unlink(const FullLocalFileLocation &location) {
  // TODO: remove file from the database too
  auto it = local_location_to_file_id_.find(location);
  if (it == local_location_to_file_id_.end()) {
    return;
  }
  auto file_id = it->second;
  auto file_node = get_sync_file_node(file_id);
  CHECK(file_node);
  file_node->drop_local_location();
  try_flush_node_info(file_node, "on_file_unlink");
}

Result<FileId> FileManager::register_local(FullLocalFileLocation location, DialogId owner_dialog_id, int64 size,
                                           bool get_by_hash, bool force, bool skip_file_size_checks) {
  // TODO: use get_by_hash
  FileData data;
  data.local_ = LocalFileLocation(std::move(location));
  data.owner_dialog_id_ = owner_dialog_id;
  data.size_ = size;
  return register_file(std::move(data), FileLocationSource::None /*won't be used*/, "register_local", force,
                       skip_file_size_checks);
}

FileId FileManager::register_remote(const FullRemoteFileLocation &location, FileLocationSource file_location_source,
                                    DialogId owner_dialog_id, int64 size, int64 expected_size, string remote_name) {
  FileData data;
  data.remote_ = RemoteFileLocation(location);
  data.owner_dialog_id_ = owner_dialog_id;
  data.size_ = size;
  data.expected_size_ = expected_size;
  data.remote_name_ = std::move(remote_name);

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
  // add #mtime# into conversion
  if (!original_path.empty() && conversion[0] != '#' && PathView(original_path).is_absolute()) {
    auto file_paths = log_interface->get_file_paths();
    if (!td::contains(file_paths, original_path)) {
      auto r_stat = stat(original_path);
      uint64 mtime = r_stat.is_ok() ? r_stat.ok().mtime_nsec_ : 0;
      conversion = PSTRING() << "#mtime#" << lpad0(to_string(mtime), 20) << '#' << conversion;
    }
  }

  FileData data;
  data.generate_ =
      td::make_unique<FullGenerateFileLocation>(file_type, std::move(original_path), std::move(conversion));
  data.owner_dialog_id_ = owner_dialog_id;
  data.expected_size_ = expected_size;
  return register_file(std::move(data), file_location_source, "register_generate", false);
}

Result<FileId> FileManager::register_file(FileData &&data, FileLocationSource file_location_source, const char *source,
                                          bool force, bool skip_file_size_checks) {
  bool has_remote = data.remote_.type() == RemoteFileLocation::Type::Full;
  bool has_generate = data.generate_ != nullptr;
  if (data.local_.type() == LocalFileLocation::Type::Full && !force) {
    if (file_location_source == FileLocationSource::FromBinlog ||
        file_location_source == FileLocationSource::FromDatabase) {
      PathView path_view(data.local_.full().path_);
      if (path_view.is_relative()) {
        data.local_.full().path_ = PSTRING()
                                   << get_files_base_dir(data.local_.full().file_type_) << data.local_.full().path_;
      }
    }

    auto status = check_local_location(data.local_.full(), data.size_, skip_file_size_checks);
    if (status.is_error()) {
      LOG(WARNING) << "Invalid " << data.local_.full() << ": " << status << " from " << source;
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
  node = td::make_unique<FileNode>(std::move(data.local_), NewRemoteFileLocation(data.remote_, file_location_source),
                                   std::move(data.generate_), data.size_, data.expected_size_,
                                   std::move(data.remote_name_), std::move(data.url_), data.owner_dialog_id_,
                                   std::move(data.encryption_key_), file_id, static_cast<int8>(has_remote));
  node->pmc_id_ = FileDbId(data.pmc_id_);
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
    RemoteInfo info{file_view.remote_location(), file_location_source, file_id};
    remote_key = remote_location_info_.add(info);
    auto &stored_info = remote_location_info_.get(remote_key);
    if (stored_info.file_id_ == file_id) {
      get_file_id_info(file_id)->pin_flag_ = true;
      new_remote = true;
    } else {
      to_merge.push_back(stored_info.file_id_);
      if (merge_choose_remote_location(file_view.remote_location(), file_location_source, stored_info.remote_,
                                       stored_info.file_location_source_) == 0) {
        stored_info.remote_ = file_view.remote_location();
        stored_info.file_location_source_ = file_location_source;
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

  try_flush_node(get_file_node(file_id), "register_file");
  auto main_file_id = get_file_node(file_id)->main_file_id_;
  try_forget_file_id(file_id);
  for (auto file_source_id : data.file_source_ids_) {
    VLOG(file_references) << "Loaded " << data.file_source_ids_ << " for file " << main_file_id << " from " << source;
    if (file_source_id.is_valid()) {
      context_->add_file_source(main_file_id, file_source_id);
    }
  }
  return FileId(main_file_id.get(), remote_key);
}

// 0 -- choose x
// 1 -- choose y
// 2 -- choose any
static int merge_choose_local_location(const LocalFileLocation &x, const LocalFileLocation &y) {
  int32 x_type = static_cast<int32>(x.type());
  int32 y_type = static_cast<int32>(y.type());
  if (x_type != y_type) {
    return x_type < y_type;
  }
  return 2;
}

static int merge_choose_file_source_location(FileLocationSource x, FileLocationSource y) {
  return static_cast<int>(x) < static_cast<int>(y);
}

static int merge_choose_remote_location(const FullRemoteFileLocation &x, FileLocationSource x_source,
                                        const FullRemoteFileLocation &y, FileLocationSource y_source) {
  LOG(INFO) << "Choose between " << x << " from " << x_source << " and " << y << " from " << y_source;
  if (x.is_web() != y.is_web()) {
    return x.is_web();  // prefer non-web
  }
  auto x_ref = x.has_file_reference();
  auto y_ref = y.has_file_reference();
  if (x_ref || y_ref) {
    if (x_ref != y_ref) {
      return !x_ref;
    }
    if (x.get_file_reference() != y.get_file_reference()) {
      return merge_choose_file_source_location(x_source, y_source);
    }
  }
  if ((x.get_access_hash() != y.get_access_hash() || x.get_source() != y.get_source()) &&
      (x_source != y_source || x.is_web() || x.get_id() == y.get_id())) {
    return merge_choose_file_source_location(x_source, y_source);
  }
  return 2;
}

static int merge_choose_remote_location(const NewRemoteFileLocation &x, const NewRemoteFileLocation &y) {
  if (x.is_full_alive != y.is_full_alive) {
    return !x.is_full_alive;
  }
  if (x.is_full_alive) {
    return merge_choose_remote_location(x.full.value(), x.full_source, y.full.value(), y.full_source);
  }
  if (!x.partial != !y.partial) {
    return !x.partial;
  }
  return 2;
}

static int merge_choose_generate_location(const unique_ptr<FullGenerateFileLocation> &x,
                                          const unique_ptr<FullGenerateFileLocation> &y) {
  int x_empty = (x == nullptr);
  int y_empty = (y == nullptr);
  if (x_empty != y_empty) {
    return x_empty ? 1 : 0;
  }
  if (!x_empty && *x != *y) {
    bool x_has_mtime = begins_with(x->conversion_, "#mtime#");
    bool y_has_mtime = begins_with(y->conversion_, "#mtime#");
    if (x_has_mtime != y_has_mtime) {
      return x_has_mtime ? 0 : 1;
    }
    return x->conversion_ >= y->conversion_
               ? 0
               : 1;  // the bigger conversion, the bigger mtime or at least more stable choise
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
  if (a != b) {
    return -1;
  }
  return 2;
}

void FileManager::do_cancel_download(FileNodePtr node) {
  if (node->download_id_ == 0) {
    return;
  }
  send_closure(file_load_manager_, &FileLoadManager::cancel, node->download_id_);
  node->download_id_ = 0;
  node->is_download_started_ = false;
  node->download_was_update_file_reference_ = false;
  node->set_download_priority(0);
}

void FileManager::do_cancel_upload(FileNodePtr node) {
  if (node->upload_id_ == 0) {
    return;
  }
  send_closure(file_load_manager_, &FileLoadManager::cancel, node->upload_id_);
  node->upload_id_ = 0;
  node->upload_was_update_file_reference_ = false;
  node->set_upload_priority(0);
}

void FileManager::do_cancel_generate(FileNodePtr node) {
  if (node->generate_id_ == 0) {
    return;
  }
  send_closure(file_generate_manager_, &FileGenerateManager::cancel, node->generate_id_);
  node->generate_id_ = 0;
  node->generate_was_update_ = false;
  node->set_generate_priority(0, 0);
}

Result<FileId> FileManager::merge(FileId x_file_id, FileId y_file_id, bool no_sync) {
  LOG(DEBUG) << "Merge new file " << x_file_id << " and old file " << y_file_id;

  if (!x_file_id.is_valid()) {
    return Status::Error("First file_id is invalid");
  }
  FileNodePtr x_node = no_sync ? get_file_node(x_file_id) : get_sync_file_node(x_file_id);
  if (!x_node) {
    return Status::Error(PSLICE() << "Can't merge files. First id is invalid: " << x_file_id << " and " << y_file_id);
  }

  if (!y_file_id.is_valid()) {
    LOG(DEBUG) << "Old file is invalid";
    return x_node->main_file_id_;
  }
  FileNodePtr y_node = get_file_node(y_file_id);
  if (!y_node) {
    return Status::Error(PSLICE() << "Can't merge files. Second id is invalid: " << x_file_id << " and " << y_file_id);
  }

  if (x_file_id == x_node->upload_pause_) {
    x_node->set_upload_pause(FileId());
  }
  if (x_node.get() == y_node.get()) {
    LOG(DEBUG) << "Files are already merged";
    return x_node->main_file_id_;
  }
  if (y_file_id == y_node->upload_pause_) {
    y_node->set_upload_pause(FileId());
  }

  if (x_node->remote_.full && y_node->remote_.full && !x_node->remote_.full.value().is_web() &&
      !y_node->remote_.full.value().is_web() && y_node->remote_.is_full_alive &&
      x_node->remote_.full_source == FileLocationSource::FromServer &&
      y_node->remote_.full_source == FileLocationSource::FromServer &&
      x_node->remote_.full.value().get_dc_id() != y_node->remote_.full.value().get_dc_id()) {
    LOG(ERROR) << "File remote location was changed from " << y_node->remote_.full.value() << " to "
               << x_node->remote_.full.value();
  }

  bool drop_last_successful_force_reupload_time = x_node->last_successful_force_reupload_time_ <= 0 &&
                                                  x_node->remote_.full &&
                                                  x_node->remote_.full_source == FileLocationSource::FromServer;

  auto count_local = [](auto &node) {
    return std::accumulate(node->file_ids_.begin(), node->file_ids_.end(), 0,
                           [](const auto &x, const auto &y) { return x + (y.get_remote() != 0); });
  };
  if (count_local(x_node) + count_local(y_node) > 100) {
  }

  FileNodePtr nodes[] = {x_node, y_node, x_node};
  FileNodeId node_ids[] = {get_file_id_info(x_file_id)->node_id_, get_file_id_info(y_file_id)->node_id_};
  int trusted_by_source = merge_choose_file_source_location(x_node->remote_.full_source, y_node->remote_.full_source);

  int local_i = merge_choose_local_location(x_node->local_, y_node->local_);
  int remote_i = merge_choose_remote_location(x_node->remote_, y_node->remote_);
  int generate_i = merge_choose_generate_location(x_node->generate_, y_node->generate_);
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
    if (nodes[remote_i]->remote_.full && nodes[local_i]->local_.type() != LocalFileLocation::Type::Partial) {
      LOG(ERROR) << "Different encryption key in files, but lets choose same key as remote location";
      encryption_key_i = remote_i;
    } else {
      return Status::Error("Can't merge files. Different encryption keys");
    }
  }

  // prefer more trusted source
  if (remote_name_i == 2) {
    remote_name_i = trusted_by_source;
  }
  if (url_i == 2) {
    url_i = trusted_by_source;
  }
  if (expected_size_i == 2) {
    expected_size_i = trusted_by_source;
  }

  int node_i =
      std::make_tuple(y_node->pmc_id_.is_valid(), x_node->pmc_id_, y_node->file_ids_.size(), main_file_id_i == 1) >
      std::make_tuple(x_node->pmc_id_.is_valid(), y_node->pmc_id_, x_node->file_ids_.size(), main_file_id_i == 0);

  auto other_node_i = 1 - node_i;
  FileNodePtr node = nodes[node_i];
  FileNodePtr other_node = nodes[other_node_i];
  auto file_view = FileView(node);

  LOG(DEBUG) << "Have x_node->pmc_id_ = " << x_node->pmc_id_.get() << ", y_node->pmc_id_ = " << y_node->pmc_id_.get()
             << ", x_node_size = " << x_node->file_ids_.size() << ", y_node_size = " << y_node->file_ids_.size()
             << ", node_i = " << node_i << ", local_i = " << local_i << ", remote_i = " << remote_i
             << ", generate_i = " << generate_i << ", size_i = " << size_i << ", remote_name_i = " << remote_name_i
             << ", url_i = " << url_i << ", owner_i = " << owner_i << ", encryption_key_i = " << encryption_key_i
             << ", main_file_id_i = " << main_file_id_i << ", trusted_by_source = " << trusted_by_source
             << ", x_source = " << x_node->remote_.full_source << ", y_source = " << y_node->remote_.full_source;
  if (local_i == other_node_i) {
    do_cancel_download(node);
    node->set_download_offset(other_node->download_offset_);
    node->set_local_location(other_node->local_, other_node->local_ready_size_, other_node->download_offset_,
                             other_node->local_ready_prefix_size_);
    node->download_id_ = other_node->download_id_;
    node->download_was_update_file_reference_ = other_node->download_was_update_file_reference_;
    node->is_download_started_ |= other_node->is_download_started_;
    node->set_download_priority(other_node->download_priority_);
    other_node->download_id_ = 0;
    other_node->download_was_update_file_reference_ = false;
    other_node->is_download_started_ = false;
    other_node->download_priority_ = 0;
    other_node->download_offset_ = 0;
    other_node->local_ready_prefix_size_ = 0;

    //do_cancel_generate(node);
    //node->set_generate_location(std::move(other_node->generate_));
    //node->generate_id_ = other_node->generate_id_;
    //node->set_generate_priority(other_node->generate_download_priority_, other_node->generate_upload_priority_);
    //other_node->generate_id_ = 0;
    //other_node->generate_was_update_ = false;
    //other_node->generate_priority_ = 0;
    //other_node->generate_download_priority_ = 0;
    //other_node->generate_upload_priority_ = 0;
  } else {
    do_cancel_download(other_node);
    //do_cancel_generate(other_node);
  }

  if (remote_i == other_node_i) {
    do_cancel_upload(node);
    node->set_new_remote_location(std::move(other_node->remote_));
    node->upload_id_ = other_node->upload_id_;
    node->upload_was_update_file_reference_ = other_node->upload_was_update_file_reference_;
    node->set_upload_priority(other_node->upload_priority_);
    node->set_upload_pause(other_node->upload_pause_);
    other_node->upload_id_ = 0;
    other_node->upload_was_update_file_reference_ = false;
    other_node->upload_priority_ = 0;
    other_node->set_upload_pause(FileId());
  } else {
    do_cancel_upload(other_node);
  }

  if (generate_i == other_node_i) {
    do_cancel_generate(node);
    node->set_generate_location(std::move(other_node->generate_));
    node->generate_id_ = other_node->generate_id_;
    node->set_generate_priority(other_node->generate_download_priority_, other_node->generate_upload_priority_);
    other_node->generate_id_ = 0;
    other_node->generate_priority_ = 0;
    other_node->generate_download_priority_ = 0;
    other_node->generate_upload_priority_ = 0;
  } else {
    do_cancel_generate(other_node);
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

  if (drop_last_successful_force_reupload_time) {
    node->last_successful_force_reupload_time_ = -1e10;
  } else if (other_node->last_successful_force_reupload_time_ > node->last_successful_force_reupload_time_) {
    node->last_successful_force_reupload_time_ = other_node->last_successful_force_reupload_time_;
  }

  if (main_file_id_i == other_node_i) {
    context_->on_merge_files(other_node->main_file_id_, node->main_file_id_);
    node->main_file_id_ = other_node->main_file_id_;
    node->main_file_id_priority_ = other_node->main_file_id_priority_;
  } else {
    context_->on_merge_files(node->main_file_id_, other_node->main_file_id_);
  }

  bool send_updates_flag = false;
  auto other_pmc_id = other_node->pmc_id_;
  node->file_ids_.insert(node->file_ids_.end(), other_node->file_ids_.begin(), other_node->file_ids_.end());

  for (auto file_id : other_node->file_ids_) {
    auto file_id_info = get_file_id_info(file_id);
    LOG_CHECK(file_id_info->node_id_ == node_ids[other_node_i])
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
    if (info->upload_priority_ != 0 && file_view.has_active_upload_remote_location()) {
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

  if (other_pmc_id.is_valid()) {
    // node might not changed, but we need to merge nodes in pmc anyway
    node->on_pmc_changed();
  }
  try_flush_node_full(node, node_i != remote_i, node_i != local_i, node_i != generate_i, other_pmc_id);

  return node->main_file_id_;
}

void FileManager::add_file_source(FileId file_id, FileSourceId file_source_id) {
  auto node = get_file_node(file_id);
  if (!node) {
    return;
  }

  CHECK(file_source_id.is_valid());
  if (context_->add_file_source(node->main_file_id_, file_source_id)) {
    node->on_pmc_changed();
    try_flush_node_pmc(node, "add_file_source");
  }
}

void FileManager::remove_file_source(FileId file_id, FileSourceId file_source_id) {
  auto node = get_file_node(file_id);
  if (!node) {
    return;
  }

  CHECK(file_source_id.is_valid());
  if (context_->remove_file_source(node->main_file_id_, file_source_id)) {
    node->on_pmc_changed();
    try_flush_node_pmc(node, "remove_file_source");
  }
}

void FileManager::change_files_source(FileSourceId file_source_id, const vector<FileId> &old_file_ids,
                                      const vector<FileId> &new_file_ids) {
  if (old_file_ids == new_file_ids) {
    return;
  }
  CHECK(file_source_id.is_valid());

  auto old_main_file_ids = get_main_file_ids(old_file_ids);
  auto new_main_file_ids = get_main_file_ids(new_file_ids);
  for (auto file_id : old_main_file_ids) {
    auto it = new_main_file_ids.find(file_id);
    if (it == new_main_file_ids.end()) {
      remove_file_source(file_id, file_source_id);
    } else {
      new_main_file_ids.erase(it);
    }
  }
  for (auto file_id : new_main_file_ids) {
    add_file_source(file_id, file_source_id);
  }
}

void FileManager::on_file_reference_repaired(FileId file_id, FileSourceId file_source_id, Result<Unit> &&result,
                                             Promise<Unit> &&promise) {
  auto file_view = get_file_view(file_id);
  CHECK(!file_view.empty());
  if (result.is_ok() &&
      (!file_view.has_active_upload_remote_location() || !file_view.has_active_download_remote_location())) {
    result = Status::Error("No active remote location");
  }
  if (result.is_error() && result.error().code() != 429 && result.error().code() < 500) {
    VLOG(file_references) << "Invalid " << file_source_id << " " << result.error();
    remove_file_source(file_id, file_source_id);
  }
  promise.set_result(std::move(result));
}

std::unordered_set<FileId, FileIdHash> FileManager::get_main_file_ids(const vector<FileId> &file_ids) {
  std::unordered_set<FileId, FileIdHash> result;
  for (auto file_id : file_ids) {
    auto node = get_file_node(file_id);
    if (node) {
      result.insert(node->main_file_id_);
    }
  }
  return result;
}

void FileManager::try_flush_node_full(FileNodePtr node, bool new_remote, bool new_local, bool new_generate,
                                      FileDbId other_pmc_id) {
  if (node->need_pmc_flush()) {
    if (file_db_) {
      load_from_pmc(node, true, true, true);
      flush_to_pmc(node, new_remote, new_local, new_generate, "try_flush_node_full");
      if (other_pmc_id.is_valid() && node->pmc_id_ != other_pmc_id) {
        file_db_->set_file_data_ref(other_pmc_id, node->pmc_id_);
      }
    }
    node->on_pmc_flushed();
  }

  try_flush_node_info(node, "try_flush_node_full");
}

void FileManager::try_flush_node(FileNodePtr node, const char *source) {
  try_flush_node_pmc(node, source);
  try_flush_node_info(node, source);
}

void FileManager::try_flush_node_pmc(FileNodePtr node, const char *source) {
  if (node->need_pmc_flush()) {
    if (file_db_) {
      load_from_pmc(node, true, true, true);
      flush_to_pmc(node, false, false, false, source);
    }
    node->on_pmc_flushed();
  }
}

void FileManager::try_flush_node_info(FileNodePtr node, const char *source) {
  if (node->need_info_flush()) {
    for (auto file_id : vector<FileId>(node->file_ids_)) {
      auto *info = get_file_id_info(file_id);
      if (info->send_updates_flag_) {
        VLOG(update_file) << "Send UpdateFile about file " << file_id << " from " << source;
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
  if (node->pmc_id_.empty()) {
    return;
  }

  LOG(INFO) << "Delete files " << format::as_array(node->file_ids_) << " from pmc";
  FileData data;
  auto file_view = FileView(node);
  if (file_view.has_local_location()) {
    data.local_ = node->local_;
  }
  if (file_view.has_remote_location()) {
    data.remote_ = RemoteFileLocation(*node->remote_.full);
  }
  if (file_view.has_generate_location()) {
    data.generate_ = make_unique<FullGenerateFileLocation>(*node->generate_);
  }
  file_db_->clear_file_data(node->pmc_id_, data);
  node->pmc_id_ = FileDbId();
}

void FileManager::flush_to_pmc(FileNodePtr node, bool new_remote, bool new_local, bool new_generate,
                               const char *source) {
  if (!file_db_) {
    return;
  }
  FileView view(node);
  bool create_flag = false;
  if (node->pmc_id_.empty()) {
    create_flag = true;
    node->pmc_id_ = file_db_->create_pmc_id();
  }

  FileData data;
  data.pmc_id_ = node->pmc_id_.get();
  data.local_ = node->local_;
  if (data.local_.type() == LocalFileLocation::Type::Full) {
    prepare_path_for_pmc(data.local_.full().file_type_, data.local_.full().path_);
  }
  if (node->remote_.full) {
    data.remote_ = RemoteFileLocation(node->remote_.full.value());
  } else if (node->remote_.partial) {
    data.remote_ = RemoteFileLocation(*node->remote_.partial);
  }
  if (node->generate_ != nullptr && !begins_with(node->generate_->conversion_, "#file_id#")) {
    data.generate_ = make_unique<FullGenerateFileLocation>(*node->generate_);
  }

  // TODO: not needed when GenerateLocation has constant conversion
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
  data.file_source_ids_ = context_->get_some_file_sources(view.file_id());
  VLOG(file_references) << "Save file " << view.file_id() << " to database with " << data.file_source_ids_ << " from "
                        << source;

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
    TRY_RESULT(new_file_id,
               register_file(std::move(file_data), FileLocationSource::FromDatabase, "load_from_pmc", false));
    TRY_RESULT(main_file_id, merge(file_id, new_file_id));
    file_id = main_file_id;
    return Status::OK();
  };
  if (new_remote) {
    load(remote).ignore();
  }
  if (new_local) {
    load(local).ignore();
  }
  if (new_generate) {
    load(generate).ignore();
  }
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
  try_flush_node_pmc(node, "set_encryption_key");
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

  do_cancel_download(node);

  auto *file_info = get_file_id_info(file_id);
  file_info->download_priority_ = FROM_BYTES_PRIORITY;

  node->set_download_priority(FROM_BYTES_PRIORITY);

  QueryId id = queries_container_.create(Query{file_id, Query::Type::SetContent});
  node->download_id_ = id;
  node->is_download_started_ = true;
  send_closure(file_load_manager_, &FileLoadManager::from_bytes, id, node->remote_.full.value().file_type_,
               std::move(bytes), node->suggested_name());
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

void FileManager::read_file_part(FileId file_id, int32 offset, int32 count, int left_tries,
                                 Promise<td_api::object_ptr<td_api::filePart>> promise) {
  if (G()->close_flag()) {
    return promise.set_error(Status::Error(500, "Request aborted"));
  }

  if (!file_id.is_valid()) {
    return promise.set_error(Status::Error(400, "File identifier is invalid"));
  }
  auto node = get_sync_file_node(file_id);
  if (!node) {
    return promise.set_error(Status::Error(400, "File not found"));
  }
  if (offset < 0) {
    return promise.set_error(Status::Error(400, "Parameter offset must be non-negative"));
  }
  if (count < 0) {
    return promise.set_error(Status::Error(400, "Parameter count must be non-negative"));
  }

  auto file_view = FileView(node);

  if (count == 0) {
    count = narrow_cast<int32>(file_view.downloaded_prefix(offset));
    if (count == 0) {
      return promise.set_value(td_api::make_object<td_api::filePart>());
    }
  } else if (file_view.downloaded_prefix(offset) < static_cast<int64>(count)) {
    // TODO this check is safer to do in another thread
    return promise.set_error(Status::Error(400, "There is not enough downloaded bytes in the file to read"));
  }

  const string *path = nullptr;
  bool is_partial = false;
  if (file_view.has_local_location()) {
    path = &file_view.local_location().path_;
    if (!begins_with(*path, get_files_dir(file_view.get_type()))) {
      return promise.set_error(Status::Error(400, "File is not inside the cache"));
    }
  } else {
    CHECK(node->local_.type() == LocalFileLocation::Type::Partial);
    path = &node->local_.partial().path_;
    is_partial = true;
  }

  // TODO move file reading to another thread
  auto r_bytes = [&]() -> Result<string> {
    TRY_RESULT(fd, FileFd::open(*path, FileFd::Read));
    string data;
    data.resize(count);
    TRY_RESULT(read_bytes, fd.pread(data, offset));
    if (read_bytes != static_cast<size_t>(count)) {
      return Status::Error("Read less bytes than expected");
    }
    return std::move(data);
  }();
  if (r_bytes.is_error()) {
    LOG(INFO) << "Failed to read file bytes: " << r_bytes.error();
    if (--left_tries == 0 || !is_partial) {
      return promise.set_error(Status::Error(400, "Failed to read the file"));
    }

    // the temporary file could be moved from temp to persistent folder
    // we need to wait for the corresponding update and repeat the reading
    create_actor<SleepActor>("RepeatReadFilePartActor", 0.01,
                             PromiseCreator::lambda([actor_id = actor_id(this), file_id, offset, count, left_tries,
                                                     promise = std::move(promise)](Result<Unit> result) mutable {
                               send_closure(actor_id, &FileManager::read_file_part, file_id, offset, count, left_tries,
                                            std::move(promise));
                             }))
        .release();
    return;
  }

  auto result = td_api::make_object<td_api::filePart>();
  result->data_ = r_bytes.move_as_ok();
  promise.set_value(std::move(result));
}

void FileManager::delete_file(FileId file_id, Promise<Unit> promise, const char *source) {
  LOG(INFO) << "Trying to delete file " << file_id << " from " << source;
  auto node = get_sync_file_node(file_id);
  if (!node) {
    return promise.set_value(Unit());
  }

  auto file_view = FileView(node);

  // TODO review delete condition
  if (file_view.has_local_location()) {
    if (begins_with(file_view.local_location().path_, get_files_dir(file_view.get_type()))) {
      LOG(INFO) << "Unlink file " << file_id << " at " << file_view.local_location().path_;
      clear_from_pmc(node);

      context_->on_new_file(-file_view.size(), -file_view.get_allocated_local_size(), -1);
      unlink(file_view.local_location().path_).ignore();
      node->drop_local_location();
      try_flush_node(node, "delete_file 1");
    }
  } else {
    if (file_view.get_type() == FileType::Encrypted) {
      clear_from_pmc(node);
    }
    if (node->local_.type() == LocalFileLocation::Type::Partial) {
      LOG(INFO) << "Unlink partial file " << file_id << " at " << node->local_.partial().path_;
      unlink(node->local_.partial().path_).ignore();
      node->drop_local_location();
      try_flush_node(node, "delete_file 2");
    }
  }

  promise.set_value(Unit());
}

void FileManager::download(FileId file_id, std::shared_ptr<DownloadCallback> callback, int32 new_priority, int64 offset,
                           int64 limit) {
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
  node->set_download_offset(offset);
  node->set_download_limit(limit);
  auto *file_info = get_file_id_info(file_id);
  CHECK(new_priority == 0 || callback);
  if (file_info->download_callback_ != nullptr && file_info->download_callback_.get() != callback.get()) {
    // the callback will be destroyed soon and lost forever
    // this would be an error and should never happen, unless we cancel previous download query
    // in that case we send an error to the callback
    CHECK(new_priority == 0);
    file_info->download_callback_->on_download_error(file_id, Status::Error(200, "Cancelled"));
  }
  file_info->download_priority_ = narrow_cast<int8>(new_priority);
  file_info->download_callback_ = std::move(callback);
  // TODO: send current progress?

  run_generate(node);
  run_download(node);

  try_flush_node(node, "download");
}

void FileManager::run_download(FileNodePtr node) {
  int8 priority = 0;
  for (auto id : node->file_ids_) {
    auto *info = get_file_id_info(id);
    if (info->download_priority_ > priority) {
      priority = info->download_priority_;
    }
  }

  auto old_priority = node->download_priority_;

  if (priority == 0) {
    node->set_download_priority(priority);
    LOG(INFO) << "Cancel downloading of file " << node->main_file_id_;
    if (old_priority != 0) {
      do_cancel_download(node);
    }
    return;
  }

  if (node->need_load_from_pmc_) {
    LOG(INFO) << "Skip run_download, because file " << node->main_file_id_ << " needs to be loaded from PMC";
    return;
  }
  if (node->generate_id_) {
    LOG(INFO) << "Skip run_download, because file " << node->main_file_id_ << " is being generated";
    return;
  }
  auto file_view = FileView(node);
  if (!file_view.can_download_from_server()) {
    LOG(INFO) << "Skip run_download, because file " << node->main_file_id_ << " can't be downloaded from server";
    return;
  }
  node->set_download_priority(priority);
  bool need_update_offset = node->is_download_offset_dirty_;
  node->is_download_offset_dirty_ = false;

  bool need_update_limit = node->is_download_limit_dirty_;
  node->is_download_limit_dirty_ = false;

  if (old_priority != 0) {
    LOG(INFO) << "Update download offset and limits of file " << node->main_file_id_;
    CHECK(node->download_id_ != 0);
    send_closure(file_load_manager_, &FileLoadManager::update_priority, node->download_id_, priority);
    if (need_update_limit) {
      auto download_limit = node->download_limit_;
      send_closure(file_load_manager_, &FileLoadManager::update_download_limit, node->download_id_, download_limit);
    }
    if (need_update_offset) {
      auto download_offset = file_view.is_encrypted_any() ? 0 : node->download_offset_;
      send_closure(file_load_manager_, &FileLoadManager::update_download_offset, node->download_id_, download_offset);
    }
    return;
  }

  CHECK(node->download_id_ == 0);
  CHECK(!node->file_ids_.empty());
  auto file_id = node->main_file_id_;

  if (node->need_reload_photo_ && file_view.may_reload_photo()) {
    LOG(INFO) << "Reload photo from file " << node->main_file_id_;
    QueryId id = queries_container_.create(Query{file_id, Query::Type::DownloadReloadDialog});
    node->download_id_ = id;
    context_->reload_photo(file_view.remote_location().get_source(),
                           PromiseCreator::lambda([id, actor_id = actor_id(this), file_id](Result<Unit> res) {
                             Status error;
                             if (res.is_ok()) {
                               error = Status::Error("FILE_DOWNLOAD_ID_INVALID");
                             } else {
                               error = res.move_as_error();
                             }
                             VLOG(file_references)
                                 << "Got result from reload photo for file " << file_id << ": " << error;
                             send_closure(actor_id, &FileManager::on_error, id, std::move(error));
                           }));
    node->need_reload_photo_ = false;
    return;
  }

  // If file reference is needed
  if (!file_view.has_active_download_remote_location()) {
    VLOG(file_references) << "Do not have valid file_reference for file " << file_id;
    QueryId id = queries_container_.create(Query{file_id, Query::Type::DownloadWaitFileReference});
    node->download_id_ = id;
    if (node->download_was_update_file_reference_) {
      on_error(id, Status::Error("Can't download file: have no valid file reference"));
      return;
    }
    node->download_was_update_file_reference_ = true;

    context_->repair_file_reference(
        file_id, PromiseCreator::lambda([id, actor_id = actor_id(this), file_id](Result<Unit> res) {
          Status error;
          if (res.is_ok()) {
            error = Status::Error("FILE_DOWNLOAD_RESTART_WITH_FILE_REFERENCE");
          } else {
            error = res.move_as_error();
          }
          VLOG(file_references) << "Got result from FileSourceManager for file " << file_id << ": " << error;
          send_closure(actor_id, &FileManager::on_error, id, std::move(error));
        }));
    return;
  }

  QueryId id = queries_container_.create(Query{file_id, Query::Type::Download});
  node->download_id_ = id;
  node->is_download_started_ = false;
  LOG(INFO) << "Run download of file " << file_id << " of size " << node->size_ << " from "
            << node->remote_.full.value() << " with suggested name " << node->suggested_name() << " and encyption key "
            << node->encryption_key_;
  auto download_offset = file_view.is_encrypted_any() ? 0 : node->download_offset_;
  auto download_limit = node->download_limit_;
  send_closure(file_load_manager_, &FileLoadManager::download, id, node->remote_.full.value(), node->local_,
               node->size_, node->suggested_name(), node->encryption_key_, node->can_search_locally_, download_offset,
               download_limit, priority);
}

class FileManager::ForceUploadActor : public Actor {
 public:
  ForceUploadActor(FileManager *file_manager, FileId file_id, std::shared_ptr<FileManager::UploadCallback> callback,
                   int32 new_priority, uint64 upload_order, ActorShared<> parent)
      : file_manager_(file_manager)
      , file_id_(file_id)
      , callback_(std::move(callback))
      , new_priority_(new_priority)
      , upload_order_(upload_order)
      , parent_(std::move(parent)) {
  }

 private:
  FileManager *file_manager_;
  FileId file_id_;
  std::shared_ptr<FileManager::UploadCallback> callback_;
  int32 new_priority_;
  uint64 upload_order_;
  ActorShared<> parent_;
  bool is_active_{false};
  int attempt_{0};
  class UploadCallback : public FileManager::UploadCallback {
   public:
    explicit UploadCallback(ActorId<ForceUploadActor> callback) : callback_(std::move(callback)) {
    }
    void on_upload_ok(FileId file_id, tl_object_ptr<telegram_api::InputFile> input_file) override {
      send_closure(callback_, &ForceUploadActor::on_upload_ok, std::move(input_file));
    }

    void on_upload_encrypted_ok(FileId file_id, tl_object_ptr<telegram_api::InputEncryptedFile> input_file) override {
      send_closure(callback_, &ForceUploadActor::on_upload_encrypted_ok, std::move(input_file));
    }

    void on_upload_secure_ok(FileId file_id, tl_object_ptr<telegram_api::InputSecureFile> input_file) override {
      send_closure(callback_, &ForceUploadActor::on_upload_secure_ok, std::move(input_file));
    }

    void on_upload_error(FileId file_id, Status error) override {
      send_closure(callback_, &ForceUploadActor::on_upload_error, std::move(error));
    }

   private:
    ActorId<ForceUploadActor> callback_;
  };

  void on_upload_ok(tl_object_ptr<telegram_api::InputFile> input_file) {
    is_active_ = false;
    if (input_file || is_ready()) {
      callback_->on_upload_ok(file_id_, std::move(input_file));
      on_ok();
    } else {
      loop();
    }
  }

  void on_upload_encrypted_ok(tl_object_ptr<telegram_api::InputEncryptedFile> input_file) {
    is_active_ = false;
    if (input_file || is_ready()) {
      callback_->on_upload_encrypted_ok(file_id_, std::move(input_file));
      on_ok();
    } else {
      loop();
    }
  }

  void on_upload_secure_ok(tl_object_ptr<telegram_api::InputSecureFile> input_file) {
    is_active_ = false;
    if (input_file || is_ready()) {
      callback_->on_upload_secure_ok(file_id_, std::move(input_file));
      on_ok();
    } else {
      loop();
    }
  }

  bool is_ready() const {
    return !G()->close_flag() && file_manager_->get_file_view(file_id_).has_active_upload_remote_location();
  }

  void on_ok() {
    callback_.reset();
    send_closure(G()->file_manager(), &FileManager::on_force_reupload_success, file_id_);
    stop();
  }

  void on_upload_error(Status error) {
    if (attempt_ == 2) {
      callback_->on_upload_error(file_id_, std::move(error));
      callback_.reset();
      stop();
    } else {
      is_active_ = false;
      loop();
    }
  }

  auto create_callback() {
    return std::make_shared<UploadCallback>(actor_id(this));
  }

  void loop() override {
    if (is_active_) {
      return;
    }

    is_active_ = true;
    attempt_++;
    send_closure(G()->file_manager(), &FileManager::resume_upload, file_id_, std::vector<int>(), create_callback(),
                 new_priority_, upload_order_, attempt_ == 2);
  }

  void tear_down() override {
    if (callback_) {
      callback_->on_upload_error(file_id_, Status::Error("Cancelled"));
    }
  }
};

void FileManager::on_force_reupload_success(FileId file_id) {
  auto node = get_sync_file_node(file_id);
  CHECK(node);
  if (!node->remote_.is_full_alive) {  // do not update for multiple simultaneous uploads
    node->last_successful_force_reupload_time_ = Time::now();
  }
}

void FileManager::resume_upload(FileId file_id, std::vector<int> bad_parts, std::shared_ptr<UploadCallback> callback,
                                int32 new_priority, uint64 upload_order, bool force) {
  auto node = get_sync_file_node(file_id);
  if (!node) {
    LOG(INFO) << "File " << file_id << " not found";
    if (callback) {
      callback->on_upload_error(file_id, Status::Error("File not found"));
    }
    return;
  }

  if (bad_parts.size() == 1 && bad_parts[0] == -1) {
    if (node->last_successful_force_reupload_time_ >= Time::now() - 60) {
      LOG(INFO) << "Recently reuploaded file " << file_id << ", do not try again";
      if (callback) {
        callback->on_upload_error(file_id, Status::Error("Failed to reupload file"));
      }
      return;
    }

    create_actor<ForceUploadActor>("ForceUploadActor", this, file_id, std::move(callback), new_priority, upload_order,
                                   context_->create_reference())
        .release();
    return;
  }
  LOG(INFO) << "Resume upload of file " << file_id << " with priority " << new_priority << " and force = " << force;

  if (force) {
    node->remote_.is_full_alive = false;
  }
  if (node->upload_pause_ == file_id) {
    node->set_upload_pause(FileId());
  }
  FileView file_view(node);
  if (file_view.has_active_upload_remote_location() && file_view.get_type() != FileType::Thumbnail &&
      file_view.get_type() != FileType::EncryptedThumbnail && file_view.get_type() != FileType::Background) {
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

  if (!file_view.has_local_location() && !file_view.has_generate_location() && !file_view.has_alive_remote_location()) {
    LOG(INFO) << "File " << file_id << " can't be uploaded";
    if (callback) {
      callback->on_upload_error(file_id,
                                Status::Error("Need full local (or generate, or inactive remote) location for upload"));
    }
    return;
  }
  if (file_view.get_type() == FileType::Thumbnail &&
      (!file_view.has_local_location() && file_view.can_download_from_server())) {
    // TODO
    if (callback) {
      callback->on_upload_error(file_id, Status::Error("Failed to upload thumbnail without local location"));
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
  try_flush_node(node, "resume_upload");
}

bool FileManager::delete_partial_remote_location(FileId file_id) {
  auto node = get_sync_file_node(file_id);
  if (!node) {
    LOG(INFO) << "Wrong file identifier " << file_id;
    return false;
  }
  if (node->upload_pause_ == file_id) {
    node->set_upload_pause(FileId());
  }
  if (node->remote_.is_full_alive) {
    LOG(INFO) << "File " << file_id << " is already uploaded";
    return true;
  }

  node->delete_partial_remote_location();
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
  try_flush_node(node, "delete_partial_remote_location");
  return true;
}

void FileManager::delete_file_reference(FileId file_id, string file_reference) {
  VLOG(file_references) << "Delete file reference of file " << file_id << " "
                        << tag("reference_base64", base64_encode(file_reference));
  auto node = get_sync_file_node(file_id);
  if (!node) {
    LOG(ERROR) << "Wrong file identifier " << file_id;
    return;
  }
  node->delete_file_reference(file_reference);
  auto remote = get_remote(file_id.get_remote());
  if (remote != nullptr) {
    VLOG(file_references) << "Do delete file reference of remote file " << file_id;
    if (remote->delete_file_reference(file_reference)) {
      VLOG(file_references) << "Successfully deleted file reference of remote file " << file_id;
      node->upload_was_update_file_reference_ = false;
      node->download_was_update_file_reference_ = false;
      node->on_pmc_changed();
    }
  }
  try_flush_node_pmc(node, "delete_file_reference");
}

void FileManager::external_file_generate_write_part(int64 id, int32 offset, string data, Promise<> promise) {
  send_closure(file_generate_manager_, &FileGenerateManager::external_file_generate_write_part, id, offset,
               std::move(data), std::move(promise));
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
    LOG(INFO) << "Skip run_generate, because file " << node->main_file_id_ << " needs to be loaded from PMC";
    return;
  }
  FileView file_view(node);
  if (!file_view.can_generate()) {
    LOG(INFO) << "Skip run_generate, because file " << node->main_file_id_ << " can't be generated";
    return;
  }
  if (file_view.has_local_location()) {
    LOG(INFO) << "Skip run_generate, because file " << node->main_file_id_ << " has local location";
    return;
  }
  if (file_view.can_download_from_server()) {
    LOG(INFO) << "Skip run_generate, because file " << node->main_file_id_ << " can be downloaded from server";
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
      do_cancel_generate(node);
    }
    return;
  }

  if (old_priority != 0) {
    LOG(INFO) << "TODO: change file " << file_id << " generation priority";
    return;
  }

  QueryId id = queries_container_.create(Query{file_id, Query::Type::Generate});
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
                 return make_unique<Callback>(file_manager->actor_id(file_manager), id);
               }());

  LOG(INFO) << "File " << file_id << " generate request has sent to FileGenerateManager";
}

void FileManager::run_upload(FileNodePtr node, std::vector<int> bad_parts) {
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

  if (priority == 0) {
    node->set_upload_priority(priority);
    if (old_priority != 0) {
      LOG(INFO) << "Cancel file " << file_id << " uploading";
      do_cancel_upload(node);
    } else {
      LOG(INFO) << "File " << file_id << " upload priority is still 0";
    }
    return;
  }

  if (node->need_load_from_pmc_) {
    LOG(INFO) << "File " << node->main_file_id_ << " needs to be loaded from database before upload";
    return;
  }
  if (node->upload_pause_.is_valid()) {
    LOG(INFO) << "File " << node->main_file_id_ << " upload is paused: " << node->upload_pause_;
    return;
  }

  FileView file_view(node);
  if (!file_view.has_local_location() && !file_view.has_remote_location()) {
    if (node->get_by_hash_ || node->generate_id_ == 0 || !node->generate_was_update_) {
      LOG(INFO) << "Have no local location for file: get_by_hash = " << node->get_by_hash_
                << ", generate_id = " << node->generate_id_ << ", generate_was_update = " << node->generate_was_update_;
      return;
    }
    if (file_view.has_generate_location() && file_view.generate_location().file_type_ == FileType::Secure) {
      // Can't upload secure file before its size is known
      LOG(INFO) << "Can't upload secure file " << node->main_file_id_ << " before it's size is known";
      return;
    }
  }

  node->set_upload_priority(priority);

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
  if (file_view.has_alive_remote_location() && !file_view.has_active_upload_remote_location() &&
      file_view.get_type() != FileType::Thumbnail && file_view.get_type() != FileType::EncryptedThumbnail &&
      file_view.get_type() != FileType::Background) {
    QueryId id = queries_container_.create(Query{file_id, Query::Type::UploadWaitFileReference});
    node->upload_id_ = id;
    if (node->upload_was_update_file_reference_) {
      on_error(id, Status::Error("Can't upload file: have no valid file reference"));
      return;
    }
    node->upload_was_update_file_reference_ = true;

    context_->repair_file_reference(
        node->main_file_id_, PromiseCreator::lambda([id, actor_id = actor_id(this)](Result<Unit> res) {
          send_closure(actor_id, &FileManager::on_error, id, Status::Error("FILE_UPLOAD_RESTART_WITH_FILE_REFERENCE"));
        }));
    return;
  }

  if (!node->remote_.partial && node->get_by_hash_) {
    LOG(INFO) << "Get file " << node->main_file_id_ << " by hash";
    QueryId id = queries_container_.create(Query{file_id, Query::Type::UploadByHash});
    node->upload_id_ = id;

    send_closure(file_load_manager_, &FileLoadManager::upload_by_hash, id, node->local_.full(), node->size_,
                 narrow_cast<int8>(-priority));
    return;
  }

  auto new_priority = narrow_cast<int8>(bad_parts.empty() ? -priority : priority);
  td::remove_if(bad_parts, [](auto part_id) { return part_id < 0; });

  QueryId id = queries_container_.create(Query{file_id, Query::Type::Upload});
  node->upload_id_ = id;
  send_closure(file_load_manager_, &FileLoadManager::upload, id, node->local_, node->remote_.partial_or_empty(),
               file_view.expected_size(true), node->encryption_key_, new_priority, std::move(bad_parts));

  LOG(INFO) << "File " << file_id << " upload request has sent to FileLoadManager";
}

void FileManager::upload(FileId file_id, std::shared_ptr<UploadCallback> callback, int32 new_priority,
                         uint64 upload_order) {
  return resume_upload(file_id, std::vector<int>(), std::move(callback), new_priority, upload_order);
}

void FileManager::cancel_upload(FileId file_id) {
  return resume_upload(file_id, std::vector<int>(), nullptr, 0, 0);
}

static bool is_document_type(FileType type) {
  return type == FileType::Document || type == FileType::Sticker || type == FileType::Audio ||
         type == FileType::Animation || type == FileType::Background || type == FileType::DocumentAsFile;
}

static bool is_background_type(FileType type) {
  return type == FileType::Wallpaper || type == FileType::Background;
}

Result<FileId> FileManager::from_persistent_id(CSlice persistent_id, FileType file_type) {
  if (persistent_id.find('.') != string::npos) {
    TRY_RESULT(http_url, parse_url(persistent_id));
    auto url = http_url.get_url();
    if (!clean_input_string(url)) {
      return Status::Error(400, "URL must be in UTF-8");
    }
    return register_url(std::move(url), file_type, FileLocationSource::FromUser, DialogId());
  }

  auto r_binary = base64url_decode(persistent_id);
  if (r_binary.is_error()) {
    return Status::Error(10, PSLICE() << "Wrong remote file identifier specified: " << r_binary.error().message());
  }
  auto binary = r_binary.move_as_ok();
  if (binary.empty()) {
    return Status::Error(10, "Remote file identifier can't be empty");
  }
  if (binary.back() == FileNode::PERSISTENT_ID_VERSION_OLD) {
    return from_persistent_id_v2(binary, file_type);
  }
  if (binary.back() == FileNode::PERSISTENT_ID_VERSION) {
    return from_persistent_id_v3(binary, file_type);
  }
  if (binary.back() == FileNode::PERSISTENT_ID_VERSION_MAP) {
    return from_persistent_id_map(binary, file_type);
  }
  return Status::Error(10, "Wrong remote file identifier specified: can't unserialize it. Wrong last symbol");
}

Result<FileId> FileManager::from_persistent_id_map(Slice binary, FileType file_type) {
  binary.remove_suffix(1);
  auto decoded_binary = zero_decode(binary);
  FullGenerateFileLocation generate_location;
  auto status = unserialize(generate_location, decoded_binary);
  if (status.is_error()) {
    return Status::Error(10, "Wrong remote file identifier specified: can't unserialize it");
  }
  auto real_file_type = generate_location.file_type_;
  if ((real_file_type != file_type && file_type != FileType::Temp) ||
      (real_file_type != FileType::Thumbnail && real_file_type != FileType::EncryptedThumbnail)) {
    return Status::Error(10, "Type of file mismatch");
  }
  if (!begins_with(generate_location.conversion_, "#map#")) {
    return Status::Error(10, "Unexpected conversion type");
  }
  FileData data;
  data.generate_ = make_unique<FullGenerateFileLocation>(std::move(generate_location));
  return register_file(std::move(data), FileLocationSource::FromUser, "from_persistent_id_map", false).move_as_ok();
}

Result<FileId> FileManager::from_persistent_id_v23(Slice binary, FileType file_type, int32 version) {
  if (version < 0 || version >= static_cast<int32>(Version::Next)) {
    return Status::Error("Invalid remote file identifier");
  }
  auto decoded_binary = zero_decode(binary);
  FullRemoteFileLocation remote_location;
  logevent::WithVersion<TlParser> parser(decoded_binary);
  parser.set_version(version);
  parse(remote_location, parser);
  parser.fetch_end();
  auto status = parser.get_status();
  if (status.is_error()) {
    return Status::Error(10, "Wrong remote file identifier specified: can't unserialize it");
  }
  auto &real_file_type = remote_location.file_type_;
  if (is_document_type(real_file_type) && is_document_type(file_type)) {
    real_file_type = file_type;
  } else if (is_background_type(real_file_type) && is_background_type(file_type)) {
    // type of file matches, but real type is in the stored remote location
  } else if (real_file_type != file_type && file_type != FileType::Temp) {
    return Status::Error(10, "Type of file mismatch");
  }
  FileData data;
  data.remote_ = RemoteFileLocation(std::move(remote_location));
  auto file_id =
      register_file(std::move(data), FileLocationSource::FromUser, "from_persistent_id_v23", false).move_as_ok();
  return file_id;
}

Result<FileId> FileManager::from_persistent_id_v2(Slice binary, FileType file_type) {
  binary.remove_suffix(1);
  return from_persistent_id_v23(binary, file_type, 0);
}

Result<FileId> FileManager::from_persistent_id_v3(Slice binary, FileType file_type) {
  binary.remove_suffix(1);
  if (binary.empty()) {
    return Status::Error("Invalid remote file identifier");
  }
  int32 version = static_cast<uint8>(binary.back());
  binary.remove_suffix(1);
  return from_persistent_id_v23(binary, file_type, version);
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

td_api::object_ptr<td_api::file> FileManager::get_file_object(FileId file_id, bool with_main_file_id) {
  auto file_view = get_sync_file_view(file_id);

  if (file_view.empty()) {
    return td_api::make_object<td_api::file>(0, 0, 0, td_api::make_object<td_api::localFile>(),
                                             td_api::make_object<td_api::remoteFile>());
  }

  string persistent_file_id = file_view.get_persistent_file_id();
  string unique_file_id = file_view.get_unique_file_id();
  bool is_uploading_completed = !persistent_file_id.empty();
  int32 size = narrow_cast<int32>(file_view.size());
  int32 expected_size = narrow_cast<int32>(file_view.expected_size());
  int32 download_offset = narrow_cast<int32>(file_view.download_offset());
  int32 local_prefix_size = narrow_cast<int32>(file_view.local_prefix_size());
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
                                             file_view.is_downloading(), file_view.has_local_location(),
                                             download_offset, local_prefix_size, local_total_size),
      td_api::make_object<td_api::remoteFile>(std::move(persistent_file_id), std::move(unique_file_id),
                                              file_view.is_uploading(), is_uploading_completed, remote_size));
}

vector<int32> FileManager::get_file_ids_object(const vector<FileId> &file_ids, bool with_main_file_id) {
  return transform(file_ids, [this, with_main_file_id](FileId file_id) {
    auto file_view = get_sync_file_view(file_id);
    auto result_file_id = file_id;
    auto *file_info = get_file_id_info(result_file_id);
    if (with_main_file_id) {
      if (!file_info->sent_file_id_flag_ && !file_info->send_updates_flag_) {
        result_file_id = file_view.file_id();
      }
      file_info = get_file_id_info(file_view.file_id());
    }
    file_info->sent_file_id_flag_ = true;

    return result_file_id.get();
  });
}

Result<FileId> FileManager::check_input_file_id(FileType type, Result<FileId> result, bool is_encrypted,
                                                bool allow_zero, bool is_secure) {
  TRY_RESULT(file_id, std::move(result));
  if (allow_zero && !file_id.is_valid()) {
    return FileId();
  }

  auto file_node = get_sync_file_node(file_id);  // we need full data about sent files
  if (!file_node) {
    return Status::Error(6, "File not found");
  }
  auto file_view = FileView(file_node);
  FileType real_type = file_view.get_type();
  LOG(INFO) << "Checking file " << file_id << " of type " << type << "/" << real_type;
  if (!is_encrypted && !is_secure) {
    if (real_type != type && !(real_type == FileType::Temp && file_view.has_url()) &&
        !(is_document_type(real_type) && is_document_type(type)) &&
        !(is_background_type(real_type) && is_background_type(type))) {
      // TODO: send encrypted file to unencrypted chat
      return Status::Error(6, "Type of file mismatch");
    }
  }

  if (!file_view.has_remote_location()) {
    // TODO why not return file_id here? We will dup it anyway
    // But it will not be duped if has_input_media(), so for now we can't return main_file_id
    return dup_file_id(file_id);
  }

  int32 remote_id = file_id.get_remote();
  if (remote_id == 0) {
    RemoteInfo info{file_view.remote_location(), FileLocationSource::FromUser, file_id};
    remote_id = remote_location_info_.add(info);
    if (remote_location_info_.get(remote_id).file_id_ == file_id) {
      get_file_id_info(file_id)->pin_flag_ = true;
    }
  }
  return FileId(file_node->main_file_id_.get(), remote_id);
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
  if (file == nullptr) {
    if (allow_zero) {
      return FileId();
    }
    return Status::Error(6, "InputFile is not specified");
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
        string hash;
        if (false && new_type == FileType::Photo) {
          auto r_stat = stat(path);
          if (r_stat.is_ok() && r_stat.ok().size_ > 0 && r_stat.ok().size_ < 5000000) {
            auto r_file_content = read_file_str(path, r_stat.ok().size_);
            if (r_file_content.is_ok()) {
              hash = sha256(r_file_content.ok());
              auto it = file_hash_to_file_id_.find(hash);
              if (it != file_hash_to_file_id_.end()) {
                auto file_view = get_file_view(it->second);
                if (file_view.has_remote_location() && !file_view.remote_location().is_web()) {
                  return it->second;
                }
              }
            }
          }
        }
        TRY_RESULT(file_id, register_local(FullLocalFileLocation(new_type, path, 0), owner_dialog_id, 0, get_by_hash));
        if (!hash.empty()) {
          file_hash_to_file_id_[hash] = file_id;
        }
        return file_id;
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
  return register_generate(
      owner_dialog_id.get_type() == DialogType::SecretChat ? FileType::EncryptedThumbnail : FileType::Thumbnail,
      FileLocationSource::FromUser, string(), std::move(conversion), owner_dialog_id, 0);
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

bool FileManager::extract_was_uploaded(const tl_object_ptr<telegram_api::InputMedia> &input_media) {
  if (input_media == nullptr) {
    return false;
  }

  auto input_media_id = input_media->get_id();
  return input_media_id == telegram_api::inputMediaUploadedPhoto::ID ||
         input_media_id == telegram_api::inputMediaUploadedDocument::ID;
}

bool FileManager::extract_was_thumbnail_uploaded(const tl_object_ptr<telegram_api::InputMedia> &input_media) {
  if (input_media == nullptr || input_media->get_id() != telegram_api::inputMediaUploadedDocument::ID) {
    return false;
  }

  return static_cast<const telegram_api::inputMediaUploadedDocument *>(input_media.get())->thumb_ != nullptr;
}

string FileManager::extract_file_reference(const tl_object_ptr<telegram_api::InputMedia> &input_media) {
  if (input_media == nullptr) {
    return string();
  }

  switch (input_media->get_id()) {
    case telegram_api::inputMediaDocument::ID:
      return extract_file_reference(static_cast<const telegram_api::inputMediaDocument *>(input_media.get())->id_);
    case telegram_api::inputMediaPhoto::ID:
      return extract_file_reference(static_cast<const telegram_api::inputMediaPhoto *>(input_media.get())->id_);
    default:
      return string();
  }
}

string FileManager::extract_file_reference(const tl_object_ptr<telegram_api::InputDocument> &input_document) {
  if (input_document == nullptr || input_document->get_id() != telegram_api::inputDocument::ID) {
    return string();
  }

  return static_cast<const telegram_api::inputDocument *>(input_document.get())->file_reference_.as_slice().str();
}

string FileManager::extract_file_reference(const tl_object_ptr<telegram_api::InputPhoto> &input_photo) {
  if (input_photo == nullptr || input_photo->get_id() != telegram_api::inputPhoto::ID) {
    return string();
  }

  return static_cast<const telegram_api::inputPhoto *>(input_photo.get())->file_reference_.as_slice().str();
}

bool FileManager::extract_was_uploaded(const tl_object_ptr<telegram_api::InputChatPhoto> &input_chat_photo) {
  return input_chat_photo != nullptr && input_chat_photo->get_id() == telegram_api::inputChatUploadedPhoto::ID;
}

string FileManager::extract_file_reference(const tl_object_ptr<telegram_api::InputChatPhoto> &input_chat_photo) {
  if (input_chat_photo == nullptr || input_chat_photo->get_id() != telegram_api::inputChatPhoto::ID) {
    return string();
  }

  return extract_file_reference(static_cast<const telegram_api::inputChatPhoto *>(input_chat_photo.get())->id_);
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

void FileManager::on_partial_download(QueryId query_id, const PartialLocalFileLocation &partial_local, int64 ready_size,
                                      int64 size) {
  if (is_closed_) {
    return;
  }

  auto query = queries_container_.get(query_id);
  CHECK(query != nullptr);

  auto file_id = query->file_id_;
  auto file_node = get_file_node(file_id);
  LOG(DEBUG) << "Receive on_partial_download for file " << file_id << " with " << partial_local
             << ", ready_size = " << ready_size << " and size = " << size;
  if (!file_node) {
    return;
  }
  if (file_node->download_id_ != query_id) {
    return;
  }

  if (size != 0) {
    FileView file_view(file_node);
    if (!file_view.is_encrypted_secure()) {
      file_node->set_size(size);
    }
  }
  file_node->set_local_location(LocalFileLocation(partial_local), ready_size, -1, -1 /* TODO */);
  try_flush_node(file_node, "on_partial_download");
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
  LOG(DEBUG) << "Receive on_partial_upload for file " << file_id << " with " << partial_remote;
  if (!file_node) {
    return;
  }
  if (file_node->upload_id_ != query_id) {
    return;
  }

  file_node->set_partial_remote_location(partial_remote, ready_size);
  try_flush_node(file_node, "on_partial_upload");
}

void FileManager::on_download_ok(QueryId query_id, const FullLocalFileLocation &local, int64 size, bool is_new) {
  if (is_closed_) {
    return;
  }

  Query query;
  bool was_active;
  std::tie(query, was_active) = finish_query(query_id);
  auto file_id = query.file_id_;
  LOG(INFO) << "ON DOWNLOAD OK of " << (is_new ? "new" : "checked") << " file " << file_id << " of size " << size;
  auto r_new_file_id = register_local(local, DialogId(), size, false, false, true);
  Status status = Status::OK();
  if (r_new_file_id.is_error()) {
    status = Status::Error(PSLICE() << "Can't register local file after download: " << r_new_file_id.error().message());
  } else {
    if (is_new) {
      context_->on_new_file(size, get_file_view(r_new_file_id.ok()).get_allocated_local_size(), 1);
    }
    auto r_file_id = merge(r_new_file_id.ok(), file_id);
    if (r_file_id.is_error()) {
      status = r_file_id.move_as_error();
    }
  }
  if (status.is_error()) {
    LOG(ERROR) << status.message();
    return on_error_impl(get_file_node(file_id), query.type_, was_active, std::move(status));
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
  LOG(INFO) << "Found being uploaded file " << file_id << " with priority " << file_info->upload_priority_;
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
      file_node->set_upload_pause(file_id);
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
      file_node->set_upload_pause(file_id);
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
  auto bitmask = Bitmask(Bitmask::Decode{}, partial_local.ready_bitmask_);
  LOG(DEBUG) << "Receive on_partial_generate for file " << file_id << ": " << partial_local.path_ << " " << bitmask;
  if (!file_node) {
    return;
  }
  if (file_node->generate_id_ != query_id) {
    return;
  }
  auto ready_size = bitmask.get_total_size(partial_local.part_size_, file_node->size_);
  file_node->set_local_location(LocalFileLocation(partial_local), ready_size, -1, -1 /* TODO */);
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

  try_flush_node(file_node, "on_partial_generate");
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

  FileView file_view(file_node);
  if (!file_view.has_generate_location() || !begins_with(file_view.generate_location().conversion_, "#file_id#")) {
    context_->on_new_file(file_view.size(), file_view.get_allocated_local_size(), 1);
  }

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

  if (query.type_ == Query::Type::UploadByHash && !G()->close_flag()) {
    LOG(INFO) << "Upload By Hash failed: " << status << ", restart upload";
    node->get_by_hash_ = false;
    run_upload(node, {});
    return;
  }
  on_error_impl(node, query.type_, was_active, std::move(status));
}

void FileManager::on_error_impl(FileNodePtr node, Query::Type type, bool was_active, Status status) {
  SCOPE_EXIT {
    try_flush_node(node, "on_error");
  };
  if (status.code() != 1 && !G()->close_flag()) {
    LOG(WARNING) << "Failed to " << type << " file " << node->main_file_id_ << " of type " << FileView(node).get_type()
                 << ": " << status;
    if (status.code() == 0) {
      // Remove partial locations
      if (node->local_.type() == LocalFileLocation::Type::Partial &&
          !begins_with(status.message(), "FILE_UPLOAD_RESTART") &&
          !begins_with(status.message(), "FILE_DOWNLOAD_RESTART") &&
          !begins_with(status.message(), "FILE_DOWNLOAD_ID_INVALID") &&
          !begins_with(status.message(), "FILE_DOWNLOAD_LIMIT")) {
        CSlice path = node->local_.partial().path_;
        if (begins_with(path, get_files_temp_dir(FileType::Encrypted)) ||
            begins_with(path, get_files_temp_dir(FileType::Video))) {
          LOG(INFO) << "Unlink file " << path;
          unlink(path).ignore();
          node->drop_local_location();
        }
      }
      node->delete_partial_remote_location();
      status = Status::Error(400, status.message());
    }
  }

  if (status.message() == "FILE_PART_INVALID") {
    bool has_partial_small_location = node->remote_.partial && !node->remote_.partial->is_big_;
    FileView file_view(node);
    auto expected_size = file_view.expected_size(true);
    bool should_be_big_location = is_file_big(file_view.get_type(), expected_size);

    node->delete_partial_remote_location();
    if (has_partial_small_location && should_be_big_location) {
      run_upload(node, {});
      return;
    }

    LOG(WARNING) << "Failed to upload file " << node->main_file_id_ << ": unexpected " << status
                 << ", is_small = " << has_partial_small_location << ", should_be_big = " << should_be_big_location
                 << ", expected size = " << expected_size;
  }

  if (begins_with(status.message(), "FILE_GENERATE_LOCATION_INVALID")) {
    node->set_generate_location(nullptr);
  }

  if ((status.message() == "FILE_ID_INVALID" || status.message() == "LOCATION_INVALID") &&
      FileView(node).may_reload_photo()) {
    node->need_reload_photo_ = true;
    run_download(node);
    return;
  }

  if (FileReferenceManager::is_file_reference_error(status)) {
    string file_reference;
    Slice prefix = "#BASE64";
    auto pos = status.message().rfind('#');
    if (pos < status.message().size() && begins_with(status.message().substr(pos), prefix)) {
      auto r_file_reference = base64_decode(status.message().substr(pos + prefix.size()));
      if (r_file_reference.is_ok()) {
        file_reference = r_file_reference.move_as_ok();
      } else {
        LOG(ERROR) << "Can't decode file reference from error " << status << ": " << r_file_reference.error();
      }
    } else {
      LOG(ERROR) << "Unexpected error, file_reference will be deleted just in case " << status;
    }
    CHECK(!node->file_ids_.empty());
    delete_file_reference(node->file_ids_.back(), file_reference);
    run_download(node);
    return;
  }

  if (begins_with(status.message(), "FILE_UPLOAD_RESTART")) {
    if (ends_with(status.message(), "WITH_FILE_REFERENCE")) {
      node->upload_was_update_file_reference_ = true;
    }
    run_upload(node, {});
    return;
  }
  if (begins_with(status.message(), "FILE_DOWNLOAD_RESTART")) {
    if (ends_with(status.message(), "WITH_FILE_REFERENCE")) {
      node->download_was_update_file_reference_ = true;
      run_download(node);
      return;
    } else if (ends_with(status.message(), "INCREASE_PART_SIZE")) {
      if (try_fix_partial_local_location(node)) {
        run_download(node);
        return;
      }
    } else {
      node->can_search_locally_ = false;
      run_download(node);
      return;
    }
  }

  if (!was_active) {
    return;
  }

  // Stop everything on error
  do_cancel_generate(node);
  do_cancel_download(node);
  do_cancel_upload(node);

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
    node->download_was_update_file_reference_ = false;
    node->is_download_started_ = false;
    node->set_download_priority(0);
    was_active = true;
  }
  if (node->upload_id_ == query_id) {
    node->upload_id_ = 0;
    node->upload_was_update_file_reference_ = false;
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
