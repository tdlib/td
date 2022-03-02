//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/FileReferenceManager.h"
#include "td/telegram/files/FileId.h"
#include "td/telegram/files/FileManager.h"
#include "td/telegram/files/FileSourceId.h"
#include "td/telegram/td_api.h"

#include "td/actor/actor.h"
#include "td/actor/PromiseFuture.h"

#include "td/utils/common.h"
#include "td/utils/Status.h"

namespace td {

class DownloadManagerCallback final : public DownloadManager::Callback {
 public:
  DownloadManagerCallback(Td *td, ActorShared<> parent) : td_(td), parent_(std::move(parent)) {
  }

  void update_counters(DownloadManager::Counters counters) final {
    send_closure(td_->actor_id(td_), &Td::send_update, counters.get_update_file_downloads_object());
  }
  void update_file_removed(FileId file_id) final {
    send_closure(td_->actor_id(td_), &Td::send_update,
                 td_api::make_object<td_api::updateFileRemovedFromDownloads>(file_id.get()));
  }
  void start_file(FileId file_id, int8 priority, ActorShared<DownloadManager> download_manager) final {
    send_closure(td_->file_manager_actor_, &FileManager::download, file_id,
                 make_download_file_callback(td_, std::move(download_manager)), priority,
                 FileManager::KEEP_DOWNLOAD_OFFSET, FileManager::IGNORE_DOWNLOAD_LIMIT);
  }
  void pause_file(FileId file_id) final {
    send_closure(td_->file_manager_actor_, &FileManager::download, file_id, nullptr, 0,
                 FileManager::KEEP_DOWNLOAD_OFFSET, FileManager::KEEP_DOWNLOAD_LIMIT);
  }
  void delete_file(FileId file_id) final {
    send_closure(td_->file_manager_actor_, &FileManager::delete_file, file_id, Promise<Unit>(),
                 "download manager callback");
  }
  FileId dup_file_id(FileId file_id) final {
    return td_->file_manager_->dup_file_id(file_id);
  }
  FileView get_file_view(FileId file_id) final {
    return td_->file_manager_->get_file_view(file_id);
  }
  td_api::object_ptr<td_api::fileDownload> get_file_download_object(FileId file_id, FileSourceId file_source_id,
                                                                    int32 add_date, int32 complete_date,
                                                                    bool is_paused) {
    return td_api::make_object<td_api::fileDownload>(td_->file_manager_->get_file_view(file_id).file_id().get(),
                                                     td_->file_reference_manager_->get_message_object(file_source_id),
                                                     add_date, complete_date, is_paused);
  }

 private:
  Td *td_;
  ActorShared<> parent_;

  static std::shared_ptr<FileManager::DownloadCallback> make_download_file_callback(
      Td *td, ActorShared<DownloadManager> download_manager) {
    class Impl final : public FileManager::DownloadCallback {
     public:
      Impl(Td *td, ActorShared<DownloadManager> download_manager)
          : td_(td), download_manager_(std::move(download_manager)) {
      }
      void on_progress(FileId file_id) final {
        send_update(file_id, false);
      }
      void on_download_ok(FileId file_id) final {
        send_update(file_id, true);
      }
      void on_download_error(FileId file_id, Status error) final {
        send_update(file_id, true);
      }

     private:
      Td *td_;
      ActorShared<DownloadManager> download_manager_;

      void send_update(FileId file_id, bool is_paused) const {
        auto td = G()->td().get_actor_unsafe();
        auto file_view = td->file_manager_->get_file_view(file_id);
        send_closure(download_manager_, &DownloadManager::update_file_download_state, file_id,
                     file_view.local_total_size(), file_view.size(), is_paused);
        // TODO: handle deleted state?
      }
    };
    return std::make_shared<Impl>(td, std::move(download_manager));
  }
};

}  // namespace td
