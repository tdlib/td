// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/telegram/files/FileManager.h"
#include "td/telegram/Global.h"
#include "td/telegram/MessageContent.h"
#include "td/telegram/MessageExtendedMedia.h"
#include "td/telegram/Photo.h"
#include "td/telegram/Td.h"
#include "td/telegram/TdCallback.h"
#include "td/telegram/TdDb.h"

#include "td/actor/ConcurrentScheduler.h"

#include "td/utils/port/path.h"
#include "td/utils/Promise.h"
#include "td/utils/tests.h"

#include <memory>

namespace td {

class MessagePaidMedia final : public MessageContent {
 public:
  vector<MessageExtendedMedia> media;
  FormattedText caption;
  int64 star_count = 0;
  string payload;

  MessagePaidMedia() = default;
  MessagePaidMedia(vector<MessageExtendedMedia> &&media, FormattedText &&caption, int64 star_count, string payload)
      : media(std::move(media)), caption(std::move(caption)), star_count(star_count), payload(std::move(payload)) {
  }

  MessageContentType get_type() const final {
    return MessageContentType::PaidMedia;
  }
};

}  // namespace td

namespace {

struct MessageExtendedMediaAccessor {
  enum class Type : td::int32 { Empty, Unsupported, Preview, Photo, Video };

  Type type_ = Type::Empty;
  td::int32 unsupported_version_ = 0;
  td::int32 duration_ = 0;
  td::Dimensions dimensions_;
  td::string minithumbnail_;
  td::Photo photo_;
  td::FileId video_file_id_;
  td::int32 start_timestamp_ = 0;
};

static_assert(sizeof(MessageExtendedMediaAccessor) == sizeof(td::MessageExtendedMedia),
              "MessageExtendedMediaAccessor layout drifted");
static_assert(alignof(MessageExtendedMediaAccessor) == alignof(td::MessageExtendedMedia),
              "MessageExtendedMediaAccessor alignment drifted");

class GlobalContextScope final {
 public:
  GlobalContextScope() : old_context_(td::Scheduler::context()), context_(std::make_shared<td::Global>()) {
    context_->this_ptr_ = context_;
    td::Scheduler::context() = context_.get();
  }

  ~GlobalContextScope() {
    td::Scheduler::context() = old_context_;
  }

 private:
  td::ActorContext *old_context_ = nullptr;
  std::shared_ptr<td::Global> context_;
};

class DummyTdCallback final : public td::TdCallback {
 public:
  void on_result(std::uint64_t, td::td_api::object_ptr<td::td_api::Object>) final {
  }

  void on_error(std::uint64_t, td::td_api::object_ptr<td::td_api::error>) final {
  }
};

class DummyFileManagerContext final : public td::FileManager::Context {
 public:
  bool need_notify_on_new_files() final {
    return false;
  }

  void on_new_file(td::int64, td::int64, td::int32) final {
  }

  void on_file_updated(td::FileId) final {
  }

  bool add_file_source(td::FileId, td::FileSourceId, const char *) final {
    return true;
  }

  bool remove_file_source(td::FileId, td::FileSourceId, const char *) final {
    return true;
  }

  void on_merge_files(td::FileId, td::FileId) final {
  }

  td::vector<td::FileSourceId> get_some_file_sources(td::FileId) final {
    return {};
  }

  void repair_file_reference(td::FileId, td::Promise<td::Unit> promise) final {
    promise.set_value(td::Unit());
  }

  void reload_photo(td::PhotoSizeSource, td::Promise<td::Unit> promise) final {
    promise.set_value(td::Unit());
  }

  bool keep_exact_remote_location() final {
    return false;
  }

  td::ActorShared<> create_reference() final {
    return {};
  }
};

template <class PredicateT>
void spin_until(td::ConcurrentScheduler &scheduler, PredicateT &&predicate) {
  for (int iteration = 0; iteration < 10000 && !predicate(); iteration++) {
    scheduler.run_main(0.01);
  }
  CHECK(predicate());
}

td::MessageExtendedMedia make_invalid_paid_photo_media() {
  td::Photo photo;
  photo.id = 1;

  td::PhotoSize photo_size;
  photo_size.type = td::PhotoSizeType('i');
  photo_size.size = 1024;
  photo_size.dimensions = td::Dimensions{64, 64};
  photo.photos.push_back(std::move(photo_size));

  td::MessageExtendedMedia extended_media;
  auto &accessor = reinterpret_cast<MessageExtendedMediaAccessor &>(extended_media);
  accessor.type_ = MessageExtendedMediaAccessor::Type::Photo;
  accessor.photo_ = std::move(photo);
  return extended_media;
}

// Invalid paid media reaches the resend logic through derived file IDs. The direct input-media builders still require
// a resolvable FileView, so this harness validates the runtime transition that the paid-media upload guard depends on.
TEST(InvalidFileIdHandlingRuntime, paid_media_invalid_any_file_id_maps_to_empty_internal_upload_slot) {
  td::vector<td::MessageExtendedMedia> media;
  media.push_back(make_invalid_paid_photo_media());
  td::MessagePaidMedia content(std::move(media), td::FormattedText(), 7, td::string());

  auto file_ids = td::get_message_content_any_file_ids(&content);
  ASSERT_EQ(1u, file_ids.size());
  ASSERT_FALSE(file_ids[0].is_valid());

  td::vector<td::FileUploadId> file_upload_ids;
  for (auto file_id : file_ids) {
    file_upload_ids.push_back(file_id.is_valid() ? td::FileUploadId(file_id, td::FileManager::get_internal_upload_id())
                                                 : td::FileUploadId());
  }

  ASSERT_EQ(1u, file_upload_ids.size());
  ASSERT_FALSE(file_upload_ids[0].is_valid());
}

TEST(InvalidFileIdHandlingRuntime, paid_media_empty_upload_slot_resolves_to_empty_runtime_file_view) {
  td::ConcurrentScheduler scheduler(0, 0);
  scheduler.start();

  td::vector<td::MessageExtendedMedia> media;
  media.push_back(make_invalid_paid_photo_media());
  td::MessagePaidMedia content(std::move(media), td::FormattedText(), 7, td::string());

  auto file_ids = td::get_message_content_any_file_ids(&content);
  ASSERT_EQ(1u, file_ids.size());
  ASSERT_FALSE(file_ids[0].is_valid());

  auto file_upload_id = file_ids[0].is_valid()
                            ? td::FileUploadId(file_ids[0], td::FileManager::get_internal_upload_id())
                            : td::FileUploadId();
  ASSERT_FALSE(file_upload_id.is_valid());

  {
    auto dir = td::mkdtemp(td::get_temporary_dir(), "invalid-file-id-runtime").move_as_ok();

    td::TdDb::Parameters parameters;
    parameters.database_directory_ = dir;
    parameters.files_directory_ = dir;

    td::Result<td::TdDb::OpenedDatabase> open_result;
    bool is_open = false;
    {
      auto guard = scheduler.get_main_guard();
      td::TdDb::open(0, std::move(parameters),
                     td::PromiseCreator::lambda([&](td::Result<td::TdDb::OpenedDatabase> result) mutable {
                       open_result = std::move(result);
                       is_open = true;
                     }));
    }
    spin_until(scheduler, [&] { return is_open; });
    ASSERT_TRUE(open_result.is_ok());

    {
      auto guard = scheduler.get_main_guard();
      GlobalContextScope context_scope;
      auto init_status = td::G()->init(td::ActorId<td::Td>(), open_result.move_as_ok().database);
      ASSERT_TRUE(init_status.is_ok());

      auto td = td::make_unique<td::Td>(td::make_unique<DummyTdCallback>(), td::Td::Options());
      td->file_manager_ = td::make_unique<td::FileManager>(td::make_unique<DummyFileManagerContext>());

      auto file_view = td->file_manager_->get_file_view(file_upload_id.get_file_id());
      ASSERT_TRUE(file_view.empty());

      td->file_manager_.reset();
    }

    td::rmrf(dir).ignore();
  }

  scheduler.finish();
}

}  // namespace