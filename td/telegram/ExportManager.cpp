//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/ExportManager.h"

#include "td/telegram/AccessRights.h"
#include "td/telegram/ChannelId.h"
#include "td/telegram/ChatId.h"
#include "td/telegram/ChatManager.h"
#include "td/telegram/CustomEmojiId.h"
#include "td/telegram/DialogId.h"
#include "td/telegram/DialogManager.h"
#include "td/telegram/ExportWriter.h"
#include "td/telegram/files/FileId.h"
#include "td/telegram/files/FileLocation.h"
#include "td/telegram/files/FileManager.h"
#include "td/telegram/ForumTopicId.h"
#include "td/telegram/Global.h"
#include "td/telegram/MessageId.h"
#include "td/telegram/MessagesManager.h"
#include "td/telegram/MessageTopic.h"
#include "td/telegram/net/DcId.h"
#include "td/telegram/net/NetQuery.h"
#include "td/telegram/net/NetQueryCreator.h"
#include "td/telegram/OptionManager.h"
#include "td/telegram/ServerMessageId.h"
#include "td/telegram/StickerFormat.h"
#include "td/telegram/StickersManager.h"
#include "td/telegram/Td.h"
#include "td/telegram/UserId.h"
#include "td/telegram/UserManager.h"

#include "td/utils/algorithm.h"
#include "td/utils/buffer.h"
#include "td/utils/filesystem.h"
#include "td/utils/FlatHashMap.h"
#include "td/utils/logging.h"
#include "td/utils/MimeType.h"
#include "td/utils/misc.h"
#include "td/utils/port/path.h"
#include "td/utils/port/Stat.h"
#include "td/utils/SliceBuilder.h"
#include "td/utils/utf8.h"
#include "td/utils/VectorQueue.h"

#include <algorithm>
#include <limits>

namespace td {

template <class FunctionT>
class ExportQuery final : public Td::ResultHandler {
  Promise<typename FunctionT::ReturnType> promise_;

 public:
  explicit ExportQuery(Promise<typename FunctionT::ReturnType> &&promise) : promise_(std::move(promise)) {
  }

  void send(const vector<telegram_api::object_ptr<telegram_api::Function>> &prefixes, const FunctionT &function,
            DcId dc_id = DcId::main(), NetQuery::Type type = NetQuery::Type::Common) {
    send_query(G()->net_query_creator().create_with_prefixes(prefixes, function, dc_id, {}, type));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<FunctionT>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }
    promise_.set_value(result_ptr.move_as_ok());
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

static int32 get_settings_type(ExportDialogInfo::Type type) {
  using Type = ExportDialogInfo::Type;
  switch (type) {
    case Type::Self:
    case Type::Personal:
      return ExportSettings::PersonalChats;
    case Type::Bot:
      return ExportSettings::BotChats;
    case Type::PrivateGroup:
    case Type::PrivateSupergroup:
      return ExportSettings::PrivateGroups;
    case Type::PublicSupergroup:
      return ExportSettings::PublicGroups;
    case Type::PrivateChannel:
      return ExportSettings::PrivateChannels;
    case Type::PublicChannel:
      return ExportSettings::PublicChannels;
    default:
      return 0;
  }
}

static ExportDialogInfo::Type get_export_dialog_type(const Td *td, DialogId dialog_id) {
  using Type = ExportDialogInfo::Type;
  switch (dialog_id.get_type()) {
    case DialogType::User: {
      auto user_id = dialog_id.get_user_id();
      if (user_id == td->user_manager_->get_my_id()) {
        return Type::Self;
      }
      if (user_id == UserManager::get_replies_bot_user_id()) {
        return Type::Replies;
      }
      if (user_id == UserManager::get_verification_codes_bot_user_id()) {
        return Type::VerifyCodes;
      }
      return td->user_manager_->is_user_bot(user_id) ? Type::Bot : Type::Personal;
    }
    case DialogType::Chat:
      return Type::PrivateGroup;
    case DialogType::Channel: {
      auto channel_id = dialog_id.get_channel_id();
      const auto *chat_manager = td->chat_manager_.get();
      if (chat_manager->is_monoforum_channel(channel_id)) {
        if (!chat_manager->is_admined_monoforum_channel(channel_id)) {
          return Type::Personal;
        }
        auto broadcast_channel_id = chat_manager->get_monoforum_channel_id(channel_id);
        return chat_manager->get_channel_first_username(broadcast_channel_id).empty() ? Type::PrivateSupergroup
                                                                                      : Type::PublicSupergroup;
      }
      auto is_public = !chat_manager->get_channel_first_username(channel_id).empty();
      if (chat_manager->is_broadcast_channel(channel_id)) {
        return is_public ? Type::PublicChannel : Type::PrivateChannel;
      }
      return is_public ? Type::PublicSupergroup : Type::PrivateSupergroup;
    }
    default:
      UNREACHABLE();
      return Type::Unknown;
  }
}

static const vector<telegram_api::object_ptr<telegram_api::Message>> *get_message_list(
    const telegram_api::messages_Messages *messages) {
  switch (messages->get_id()) {
    case telegram_api::messages_messages::ID:
      return &static_cast<const telegram_api::messages_messages *>(messages)->messages_;
    case telegram_api::messages_messagesSlice::ID:
      return &static_cast<const telegram_api::messages_messagesSlice *>(messages)->messages_;
    case telegram_api::messages_channelMessages::ID:
      return &static_cast<const telegram_api::messages_channelMessages *>(messages)->messages_;
    case telegram_api::messages_messagesNotModified::ID:
      return nullptr;
    default:
      UNREACHABLE();
      return nullptr;
  }
}

static int32 get_message_date(const telegram_api::Message *message) {
  switch (message->get_id()) {
    case telegram_api::message::ID:
      return static_cast<const telegram_api::message *>(message)->date_;
    case telegram_api::messageService::ID:
      return static_cast<const telegram_api::messageService *>(message)->date_;
    default:
      return 0;
  }
}

// HTML links to the files aren't encoded, and Windows reserves the names of devices whatever their extension is
static string get_export_file_name(Slice file_name) {
  auto name = clean_filename(file_name);
  for (auto &c : name) {
    if (c == '#' || c == '%') {
      c = '_';
    }
  }
  auto stem = to_upper(trim(Slice(name).substr(0, name.find('.'))));
  auto is_numbered_device = [&stem] {
    if (!begins_with(stem, "COM") && !begins_with(stem, "LPT")) {
      return false;
    }
    auto number = Slice(stem).substr(3);
    return (number.size() == 1 && is_digit(number[0])) || number == "\xC2\xB9" || number == "\xC2\xB2" ||
           number == "\xC2\xB3";
  };
  if (stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL" || is_numbered_device()) {
    name = '_' + name;
  }
  return name;
}

static int32 get_single_message_date(const telegram_api::object_ptr<telegram_api::messages_Messages> &messages) {
  const auto *message_list = get_message_list(messages.get());
  CHECK(message_list != nullptr);
  if (message_list->empty()) {
    return 0;
  }
  return get_message_date((*message_list)[0].get());
}

static const telegram_api::Photo *get_message_photo(const telegram_api::Message *message) {
  switch (message->get_id()) {
    case telegram_api::message::ID: {
      const auto *media = static_cast<const telegram_api::message *>(message)->media_.get();
      if (media != nullptr && media->get_id() == telegram_api::messageMediaPhoto::ID) {
        return static_cast<const telegram_api::messageMediaPhoto *>(media)->photo_.get();
      }
      return nullptr;
    }
    case telegram_api::messageService::ID: {
      const auto *action = static_cast<const telegram_api::messageService *>(message)->action_.get();
      switch (action->get_id()) {
        case telegram_api::messageActionChatEditPhoto::ID:
          return static_cast<const telegram_api::messageActionChatEditPhoto *>(action)->photo_.get();
        case telegram_api::messageActionSuggestProfilePhoto::ID:
          return static_cast<const telegram_api::messageActionSuggestProfilePhoto *>(action)->photo_.get();
        default:
          return nullptr;
      }
    }
    default:
      return nullptr;
  }
}

static const telegram_api::Document *get_message_document(const telegram_api::Message *message) {
  if (message->get_id() != telegram_api::message::ID) {
    return nullptr;
  }
  const auto *media = static_cast<const telegram_api::message *>(message)->media_.get();
  if (media == nullptr || media->get_id() != telegram_api::messageMediaDocument::ID) {
    return nullptr;
  }
  return static_cast<const telegram_api::messageMediaDocument *>(media)->document_.get();
}

// replaces the file reference in the location with the one from the document, if it is the document of the file
static bool update_file_reference(telegram_api::InputFileLocation *location, const telegram_api::Document *document) {
  if (location->get_id() != telegram_api::inputDocumentFileLocation::ID || document == nullptr ||
      document->get_id() != telegram_api::document::ID) {
    return false;
  }
  auto *document_location = static_cast<telegram_api::inputDocumentFileLocation *>(location);
  const auto *full_document = static_cast<const telegram_api::document *>(document);
  if (full_document->id_ != document_location->id_) {
    return false;
  }
  document_location->file_reference_ = full_document->file_reference_.copy();
  return true;
}

// replaces the file reference in the location with the one from the message, if the message has the file
static bool update_file_reference(telegram_api::InputFileLocation *location, const telegram_api::Message *message) {
  switch (location->get_id()) {
    case telegram_api::inputPhotoFileLocation::ID: {
      auto *photo_location = static_cast<telegram_api::inputPhotoFileLocation *>(location);
      const auto *photo = get_message_photo(message);
      if (photo == nullptr || photo->get_id() != telegram_api::photo::ID) {
        return false;
      }
      const auto *full_photo = static_cast<const telegram_api::photo *>(photo);
      if (full_photo->id_ != photo_location->id_) {
        return false;
      }
      photo_location->file_reference_ = full_photo->file_reference_.copy();
      return true;
    }
    case telegram_api::inputDocumentFileLocation::ID:
      return update_file_reference(location, get_message_document(message));
    default:
      return false;
  }
}

struct ExportManager::MessageFileWork {
  ExportFile *target = nullptr;
  const td_api::file *file = nullptr;
  // the data to write instead of downloading the file
  string content;
  string suggested_path;
  int32 media_type = 0;
  // the size, which decides whether the file is exported; the size of the main file for a thumbnail
  int64 controlling_size = 0;
  bool is_available = true;
  MessageId message_id;
  CustomEmojiId custom_emoji_id;
};

struct ExportManager::FileProcess {
  FileProcess(string path, ExportStats *stats) : output(std::move(path), stats) {
  }

  int64 id = 0;
  // the main file identifier, under which the file is remembered as loaded
  int32 key = 0;
  string relative_path;
  ExportOutputFile output;
  DcId dc_id;
  telegram_api::object_ptr<telegram_api::upload_getFile> query;
  int64 size = 0;
  int64 offset = 0;
  int64 downloaded_size = 0;
  MessageId message_id;
  CustomEmojiId custom_emoji_id;
  bool is_file_reference_refreshed = false;
};

struct ExportManager::DialogProcess {
  ExportDialogInfo info;
  DialogId migrated_from_dialog_id;

  // indices of the message ranges to export; negative ones are of the basic group the chat was upgraded from
  vector<int32> splits;
  vector<int32> split_message_counts;

  size_t split_index = 0;
  int32 largest_id_plus_one = 1;
  int32 pending_count = 0;
  bool is_last_slice = false;

  int32 received_message_count = 0;
  int32 message_count = 0;
  // whether slices of the whole history were exported before access to the chat was lost
  bool has_other_messages = false;

  // the received slice, which is written after its files are loaded
  vector<ExportMessage> slice;
  int32 slice_message_count = 0;
  vector<MessageFileWork> file_works;
  size_t file_work_index = 0;
  // the custom emoji first used in the slice; their files are loaded after the files of the messages
  vector<td_api::object_ptr<td_api::sticker>> custom_emoji_stickers;

  // counters, which number files without a name
  int32 photo_count = 0;
  int32 audio_count = 0;
  int32 video_count = 0;
  int32 document_count = 0;
  int32 contact_count = 0;
};

struct ExportManager::DialogsList {
  vector<unique_ptr<DialogProcess>> chats;
  FlatHashMap<DialogId, size_t, DialogIdHash> chat_indices;
  int32 processed_count = 0;

  // the ranges are walked from the last one
  size_t split_index_plus_one = 0;
  int32 offset_date = 0;
  int32 offset_message_id = 0;
  DialogId offset_dialog_id;

  vector<unique_ptr<DialogProcess>> left_channels;
  FlatHashMap<DialogId, size_t, DialogIdHash> left_channel_indices;
  int32 left_channel_offset = 0;
  bool are_left_channels_finished = false;
};

struct ExportManager::Export {
  struct SplitRange {
    int32 min_id = 0;
    int32 max_id = 0;
  };

  uint64 generation = 0;
  ExportSettings settings;

  vector<Step> steps;
  size_t step_index = 0;

  vector<int32> substeps_in_step;
  int32 substeps_total = 0;
  int32 substeps_passed = 0;
  Step last_processing_step = Step::Initializing;

  int64 takeout_id = 0;
  bool is_takeout_finished = false;

  bool has_processing_state = false;
  ProcessingState processing_state;

  vector<SplitRange> splits;
  int32 dialogs_count = 0;

  ExportStats stats;
  unique_ptr<ExportWriter> writer;

  unique_ptr<DialogsList> dialogs_list;
  // the chats to export; left channels are the last
  vector<unique_ptr<DialogProcess>> dialogs;
  size_t dialog_index = 0;
  unique_ptr<DialogProcess> dialog;

  int64 last_file_id = 0;
  unique_ptr<FileProcess> file;

  // a file, which is shared by several messages, is exported once
  FlatHashMap<int32, string> loaded_files;
  VectorQueue<int32> loaded_file_keys;

  // each custom emoji is resolved and loaded once; entries aren't added while files of a slice are loaded,
  // because the work points to them
  ExportCustomEmojiFiles custom_emoji_files;
};

ExportManager::ExportManager(Td *td, ActorShared<> parent) : td_(td), parent_(std::move(parent)) {
}

ExportManager::~ExportManager() = default;

void ExportManager::tear_down() {
  parent_.reset();
}

Result<ExportSettings> ExportManager::get_export_settings(
    td_api::object_ptr<td_api::ExportSource> &&source, vector<td_api::object_ptr<td_api::ExportFormat>> &&formats,
    td_api::object_ptr<td_api::exportMediaSettings> &&media_settings, const string &directory) const {
  ExportSettings settings;
  if (source == nullptr) {
    return Status::Error(400, "Export source must be non-empty");
  }
  switch (source->get_id()) {
    case td_api::exportSourceAccount::ID: {
      auto data = std::move(static_cast<td_api::exportSourceAccount *>(source.get())->data_);
      if (data == nullptr) {
        return Status::Error(400, "Exported data must be non-empty");
      }
      auto add_type = [&settings](bool is_added, int32 type) {
        if (is_added) {
          settings.types |= type;
        }
      };
      add_type(data->personal_information_, ExportSettings::PersonalInfo);
      add_type(data->profile_photos_, ExportSettings::Userpics);
      add_type(data->stories_, ExportSettings::Stories);
      add_type(data->profile_audios_, ExportSettings::ProfileMusic);
      add_type(data->contacts_, ExportSettings::Contacts);
      add_type(data->sessions_, ExportSettings::Sessions);
      add_type(data->other_data_, ExportSettings::OtherData);
      add_type(data->private_chats_, ExportSettings::PersonalChats);
      add_type(data->bot_chats_, ExportSettings::BotChats);
      add_type(data->private_groups_, ExportSettings::PrivateGroups);
      add_type(data->public_groups_, ExportSettings::PublicGroups);
      add_type(data->private_channels_, ExportSettings::PrivateChannels);
      add_type(data->public_channels_, ExportSettings::PublicChannels);
      if (settings.types == 0) {
        return Status::Error(400, "Nothing to export");
      }
      if ((settings.types & ~ExportSettings::AnyChatsMask) != 0) {
        return Status::Error(400, "Only chats of the account can be exported yet");
      }

      // private chats are always exported whole and public chats only with own messages
      settings.full_chats = ExportSettings::PersonalChats | ExportSettings::BotChats;
      if (!data->only_own_private_group_messages_) {
        settings.full_chats |= ExportSettings::PrivateGroups;
      }
      if (!data->only_own_private_channel_messages_) {
        settings.full_chats |= ExportSettings::PrivateChannels;
      }
      break;
    }
    case td_api::exportSourceChat::ID: {
      auto chat_source = static_cast<const td_api::exportSourceChat *>(source.get());
      DialogId dialog_id(chat_source->chat_id_);
      TRY_STATUS(td_->dialog_manager_->check_dialog_access(dialog_id, false, AccessRights::Read, "start_export"));
      if (dialog_id.get_type() == DialogType::SecretChat) {
        return Status::Error(400, "Secret chats can't be exported");
      }
      if (chat_source->topic_id_ != nullptr) {
        TRY_RESULT(message_topic, MessageTopic::get_message_topic(td_, dialog_id, chat_source->topic_id_));
        if (!message_topic.is_forum() || dialog_id.get_type() != DialogType::Channel) {
          return Status::Error(400, "Only topics of forum supergroups can be exported");
        }
        settings.single_topic_top_thread_message_id = message_topic.get_forum_topic_id().to_top_thread_message_id();
        return Status::Error(400, "Topic export isn't supported yet");
      }
      if (chat_source->min_date_ < 0 || chat_source->max_date_ < 0) {
        return Status::Error(400, "Invalid date specified");
      }
      if (chat_source->max_date_ > 0 && chat_source->max_date_ <= chat_source->min_date_) {
        return Status::Error(400, "Invalid date range specified");
      }
      settings.single_dialog_id = dialog_id;
      settings.single_dialog_min_date = chat_source->min_date_;
      settings.single_dialog_max_date = chat_source->max_date_;
      settings.types = ExportSettings::AnyChatsMask;
      settings.full_chats = ExportSettings::AnyChatsMask;
      break;
    }
    default:
      UNREACHABLE();
  }

  bool is_html = false;
  bool is_json = false;
  for (auto &format : formats) {
    if (format == nullptr) {
      return Status::Error(400, "Export format must be non-empty");
    }
    bool is_repeated = false;
    switch (format->get_id()) {
      case td_api::exportFormatHtml::ID:
        is_repeated = is_html;
        is_html = true;
        settings.html_stylesheet = std::move(static_cast<td_api::exportFormatHtml *>(format.get())->stylesheet_);
        break;
      case td_api::exportFormatJson::ID:
        is_repeated = is_json;
        is_json = true;
        break;
      default:
        UNREACHABLE();
    }
    if (is_repeated) {
      return Status::Error(400, "Each export format can be specified only once");
    }
  }
  if (!is_html && !is_json) {
    return Status::Error(400, "Export formats must be non-empty");
  }
  settings.format =
      is_html && is_json ? ExportFormat::HtmlAndJson : (is_html ? ExportFormat::Html : ExportFormat::Json);
  if (!check_utf8(settings.html_stylesheet)) {
    return Status::Error(400, "Style sheet must be encoded in UTF-8");
  }

  if (media_settings != nullptr) {
    auto add_media_type = [&settings](bool is_added, int32 type) {
      if (is_added) {
        settings.media_types |= type;
      }
    };
    add_media_type(media_settings->photos_, ExportSettings::Photo);
    add_media_type(media_settings->videos_, ExportSettings::Video);
    add_media_type(media_settings->voice_notes_, ExportSettings::VoiceMessage);
    add_media_type(media_settings->video_notes_, ExportSettings::VideoMessage);
    add_media_type(media_settings->stickers_, ExportSettings::Sticker);
    add_media_type(media_settings->animations_, ExportSettings::Gif);
    add_media_type(media_settings->documents_, ExportSettings::File);
    if (media_settings->max_file_size_ <= 0 || media_settings->max_file_size_ > ExportSettings::MAX_FILE_SIZE) {
      return Status::Error(400, "Invalid maximum file size specified");
    }
    settings.media_size_limit = media_settings->max_file_size_;
  }

  if (directory.empty()) {
    return Status::Error(400, "Export directory must be non-empty");
  }
  auto r_stat = stat(directory);
  if (r_stat.is_error() || !r_stat.ok().is_dir_) {
    return Status::Error(400, "Export directory not found");
  }
  settings.utc_time_offset = narrow_cast<int32>(td_->option_manager_->get_option_integer("utc_time_offset"));
  settings.directory = directory;
  return std::move(settings);
}

string ExportManager::get_local_date_string() const {
  auto local_time = get_export_local_time(
      G()->unix_time(), narrow_cast<int32>(td_->option_manager_->get_option_integer("utc_time_offset")));
  return get_export_date_time_string(local_time).substr(0, 10);
}

Result<string> ExportManager::create_export_directory(const string &directory, bool only_single_dialog) const {
  string base = directory;
  if (base.back() != TD_DIR_SLASH && base.back() != '/') {
    base += TD_DIR_SLASH;
  }
  base += only_single_dialog ? "ChatExport_" : "DataExport_";
  base += get_local_date_string();
  for (int32 attempt = 0; attempt < MAX_DIRECTORY_ATTEMPTS; attempt++) {
    auto path = attempt == 0 ? base : PSTRING() << base << " (" << attempt << ')';
    if (stat(path).is_ok()) {
      continue;
    }
    auto status = mkdir(path);
    if (status.is_error()) {
      LOG(WARNING) << "Failed to create export directory " << path << ": " << status;
      return Status::Error(400, "Failed to create export directory");
    }
    return path + TD_DIR_SLASH;
  }
  return Status::Error(400, "Failed to create export directory");
}

void ExportManager::start_export(td_api::object_ptr<td_api::ExportSource> &&source,
                                 vector<td_api::object_ptr<td_api::ExportFormat>> &&formats,
                                 td_api::object_ptr<td_api::exportMediaSettings> &&media_settings,
                                 const string &directory, Promise<Unit> &&promise) {
  if (export_ != nullptr) {
    return promise.set_error(400, "Another export is in progress");
  }
  TRY_RESULT_PROMISE(promise, settings,
                     get_export_settings(std::move(source), std::move(formats), std::move(media_settings), directory));

  LOG(INFO) << "Start export to " << settings.directory;
  export_ = make_unique<Export>();
  export_->generation = ++current_generation_;
  export_->settings = std::move(settings);
  fill_export_steps();
  promise.set_value(Unit());

  export_->step_index = static_cast<size_t>(-1);
  export_next();
}

void ExportManager::skip_export_file(int64 file_id, Promise<Unit> &&promise) {
  if (export_ == nullptr) {
    return promise.set_error(400, "There is no export in progress");
  }
  promise.set_value(Unit());
  if (export_->file != nullptr && export_->file->id == file_id) {
    LOG(INFO) << "Skip file " << export_->file->relative_path;
    finish_file(false);
  }
}

void ExportManager::cancel_export(Promise<Unit> &&promise) {
  if (export_ == nullptr) {
    return promise.set_error(400, "There is no export in progress");
  }
  LOG(INFO) << "Cancel export";
  stop_export(td_api::make_object<td_api::exportStateCancelled>());
  promise.set_value(Unit());
}

void ExportManager::get_current_state(vector<td_api::object_ptr<td_api::Update>> &updates) const {
  if (export_ == nullptr || !export_->has_processing_state) {
    return;
  }
  updates.push_back(
      td_api::make_object<td_api::updateExportState>(get_processing_state_object(export_->processing_state)));
}

void ExportManager::fill_export_steps() {
  CHECK(export_ != nullptr);
  const auto &settings = export_->settings;
  auto &steps = export_->steps;
  steps.push_back(Step::Initializing);
  if (settings.only_single_topic()) {
    steps.push_back(Step::Topic);
    return;
  }
  if (settings.types & ExportSettings::AnyChatsMask) {
    steps.push_back(Step::DialogsList);
  }
  if (settings.types & ExportSettings::PersonalInfo) {
    steps.push_back(Step::PersonalInfo);
  }
  if (settings.types & ExportSettings::Userpics) {
    steps.push_back(Step::Userpics);
  }
  if (settings.types & ExportSettings::Stories) {
    steps.push_back(Step::Stories);
  }
  if (settings.types & ExportSettings::ProfileMusic) {
    steps.push_back(Step::ProfileMusic);
  }
  if (settings.types & ExportSettings::Contacts) {
    steps.push_back(Step::Contacts);
  }
  if (settings.types & ExportSettings::Sessions) {
    steps.push_back(Step::Sessions);
  }
  if (settings.types & ExportSettings::OtherData) {
    steps.push_back(Step::OtherData);
  }
  if (settings.types & ExportSettings::AnyChatsMask) {
    steps.push_back(Step::Dialogs);
  }
}

void ExportManager::fill_substeps_in_steps() {
  CHECK(export_ != nullptr);
  auto &substeps = export_->substeps_in_step;
  substeps.assign(static_cast<size_t>(Step::Count), 0);
  for (auto step : export_->steps) {
    substeps[static_cast<size_t>(step)] = step == Step::Dialogs ? export_->dialogs_count : 1;
  }
  export_->substeps_total = 0;
  for (auto count : substeps) {
    export_->substeps_total += count;
  }
}

int32 ExportManager::get_substeps_in_step(Step step) const {
  CHECK(export_ != nullptr);
  auto index = static_cast<size_t>(step);
  if (index >= export_->substeps_in_step.size()) {
    return 0;
  }
  return export_->substeps_in_step[index];
}

void ExportManager::export_next() {
  CHECK(export_ != nullptr);
  if (++export_->step_index >= export_->steps.size()) {
    return finish_export();
  }

  switch (export_->steps[export_->step_index]) {
    case Step::Initializing:
      return initialize();
    case Step::DialogsList:
      return collect_dialogs_list();
    case Step::Dialogs:
      return export_dialogs();
    case Step::PersonalInfo:
    case Step::Userpics:
    case Step::Stories:
    case Step::ProfileMusic:
    case Step::Contacts:
    case Step::Sessions:
    case Step::OtherData:
    case Step::Topic:
      return export_next();
    default:
      UNREACHABLE();
  }
}

void ExportManager::initialize() {
  set_processing_state(ProcessingState());

  const auto &settings = export_->settings;
  auto has_files = settings.has_media() || (settings.types & (ExportSettings::Userpics | ExportSettings::Stories)) != 0;
  auto file_max_size = settings.media_size_limit > 0 ? settings.media_size_limit : ExportSettings::MAX_FILE_SIZE;
  auto promise =
      PromiseCreator::lambda([actor_id = actor_id(this), generation = export_->generation](
                                 Result<telegram_api::object_ptr<telegram_api::account_takeout>> r_takeout) mutable {
        send_closure(actor_id, &ExportManager::on_init_takeout_session, generation, std::move(r_takeout));
      });
  TRY_STATUS_PROMISE(promise, G()->close_status());
  td_->create_handler<ExportQuery<telegram_api::account_initTakeoutSession>>(std::move(promise))
      ->send({}, telegram_api::account_initTakeoutSession(
                     has_files ? telegram_api::account_initTakeoutSession::FILE_MAX_SIZE_MASK : 0,
                     (settings.types & ExportSettings::Contacts) != 0,
                     (settings.types & (ExportSettings::PersonalChats | ExportSettings::BotChats)) != 0,
                     (settings.types & ExportSettings::PrivateGroups) != 0,
                     (settings.types & ExportSettings::GroupsMask) != 0,
                     (settings.types & ExportSettings::ChannelsMask) != 0, has_files, file_max_size));
}

void ExportManager::on_init_takeout_session(uint64 generation,
                                            Result<telegram_api::object_ptr<telegram_api::account_takeout>> r_takeout) {
  if (export_ == nullptr || export_->generation != generation) {
    if (r_takeout.is_ok()) {
      // the export was cancelled before the session was created, so nothing else will finish it
      send_finish_takeout_session(r_takeout.ok()->id_, false, Promise<bool>());
    }
    return;
  }
  if (r_takeout.is_error()) {
    return on_api_error(r_takeout.move_as_error());
  }
  export_->takeout_id = r_takeout.ok()->id_;
  LOG(INFO) << "Receive takeout session " << export_->takeout_id;

  // initTakeoutSession is refused until the export is confirmed, so nothing is created on the disk before the session
  auto r_path = create_export_directory(export_->settings.directory, export_->settings.only_single_dialog());
  if (r_path.is_error()) {
    return on_api_error(r_path.move_as_error());
  }
  export_->settings.path = r_path.move_as_ok();

  if (export_->settings.types & ExportSettings::AnyChatsMask) {
    request_split_ranges();
  } else {
    start_writer();
  }
}

bool ExportManager::is_current_export(uint64 generation) const {
  return export_ != nullptr && export_->generation == generation;
}

template <class FunctionT>
void ExportManager::send_export_query(const FunctionT &function, int32 split_index,
                                      Promise<typename FunctionT::ReturnType> &&promise) {
  CHECK(export_ != nullptr);
  CHECK(export_->takeout_id != 0);
  // Td fails the queries in flight when it closes, and no new ones can be sent then
  TRY_STATUS_PROMISE(promise, G()->close_status());
  vector<telegram_api::object_ptr<telegram_api::Function>> prefixes;
  prefixes.push_back(telegram_api::make_object<telegram_api::invokeWithTakeoutPrefix>(export_->takeout_id));
  if (split_index >= 0) {
    CHECK(static_cast<size_t>(split_index) < export_->splits.size());
    const auto &split = export_->splits[split_index];
    prefixes.push_back(telegram_api::make_object<telegram_api::invokeWithMessagesRangePrefix>(
        telegram_api::make_object<telegram_api::messageRange>(split.min_id, split.max_id)));
  }
  td_->create_handler<ExportQuery<FunctionT>>(std::move(promise))->send(prefixes, function);
}

void ExportManager::request_split_ranges() {
  auto promise = PromiseCreator::lambda(
      [actor_id = actor_id(this), generation = export_->generation](
          Result<vector<telegram_api::object_ptr<telegram_api::messageRange>>> r_ranges) mutable {
        send_closure(actor_id, &ExportManager::on_get_split_ranges, generation, std::move(r_ranges));
      });
  send_export_query(telegram_api::messages_getSplitRanges(), -1, std::move(promise));
}

void ExportManager::on_get_split_ranges(uint64 generation,
                                        Result<vector<telegram_api::object_ptr<telegram_api::messageRange>>> r_ranges) {
  if (!is_current_export(generation)) {
    return;
  }
  if (r_ranges.is_error()) {
    return on_api_error(r_ranges.move_as_error());
  }
  auto &splits = export_->splits;
  for (auto &range : r_ranges.ok()) {
    splits.push_back(Export::SplitRange{range->min_id_, range->max_id_});
  }
  if (splits.empty()) {
    splits.push_back(Export::SplitRange{1, std::numeric_limits<int32>::max()});
  }

  const auto &settings = export_->settings;
  if (settings.only_single_dialog()) {
    export_->dialogs_count =
        settings.single_dialog_id.get_type() == DialogType::Channel ? 1 : narrow_cast<int32>(splits.size());
    return start_writer();
  }
  export_->dialogs_list = make_unique<DialogsList>();
  request_dialogs_count(0);
}

void ExportManager::request_dialogs_count(size_t split_index) {
  auto promise =
      PromiseCreator::lambda([actor_id = actor_id(this), generation = export_->generation,
                              split_index](Result<telegram_api::object_ptr<telegram_api::messages_Dialogs>> r_dialogs) {
        send_closure(actor_id, &ExportManager::on_get_dialogs_count, generation, split_index, std::move(r_dialogs));
      });
  send_export_query(telegram_api::messages_getDialogs(0, false, 0, 0, 0,
                                                      telegram_api::make_object<telegram_api::inputPeerEmpty>(), 1, 0),
                    narrow_cast<int32>(split_index), std::move(promise));
}

void ExportManager::on_get_dialogs_count(uint64 generation, size_t split_index,
                                         Result<telegram_api::object_ptr<telegram_api::messages_Dialogs>> r_dialogs) {
  if (!is_current_export(generation)) {
    return;
  }
  if (r_dialogs.is_error()) {
    return on_api_error(r_dialogs.move_as_error());
  }
  auto dialogs = r_dialogs.move_as_ok();
  switch (dialogs->get_id()) {
    case telegram_api::messages_dialogs::ID:
      export_->dialogs_count +=
          narrow_cast<int32>(static_cast<const telegram_api::messages_dialogs *>(dialogs.get())->dialogs_.size());
      break;
    case telegram_api::messages_dialogsSlice::ID:
      export_->dialogs_count += static_cast<const telegram_api::messages_dialogsSlice *>(dialogs.get())->count_;
      break;
    case telegram_api::messages_dialogsNotModified::ID:
      return on_api_error(Status::Error(500, "Receive dialogsNotModified"));
    default:
      UNREACHABLE();
  }
  if (split_index + 1 < export_->splits.size()) {
    return request_dialogs_count(split_index + 1);
  }
  if (export_->settings.types & ExportSettings::GroupsChannelsMask) {
    // the first slice of left channels is kept for the list of chats
    return request_left_channels(true);
  }
  start_writer();
}

void ExportManager::request_left_channels(bool is_count) {
  CHECK(export_->dialogs_list != nullptr);
  auto promise = PromiseCreator::lambda([actor_id = actor_id(this), generation = export_->generation, is_count](
                                            Result<telegram_api::object_ptr<telegram_api::messages_Chats>> r_chats) {
    send_closure(actor_id, &ExportManager::on_get_left_channels, generation, is_count, std::move(r_chats));
  });
  send_export_query(telegram_api::channels_getLeftChannels(export_->dialogs_list->left_channel_offset), -1,
                    std::move(promise));
}

void ExportManager::on_get_left_channels(uint64 generation, bool is_count,
                                         Result<telegram_api::object_ptr<telegram_api::messages_Chats>> r_chats) {
  if (!is_current_export(generation)) {
    return;
  }
  if (r_chats.is_error()) {
    return on_api_error(r_chats.move_as_error());
  }
  auto &list = *export_->dialogs_list;
  auto chats = r_chats.move_as_ok();
  vector<telegram_api::object_ptr<telegram_api::Chat>> chat_list;
  int32 full_count = 0;
  bool is_finished = false;
  switch (chats->get_id()) {
    case telegram_api::messages_chats::ID:
      chat_list = std::move(static_cast<telegram_api::messages_chats *>(chats.get())->chats_);
      full_count = narrow_cast<int32>(chat_list.size());
      is_finished = true;
      break;
    case telegram_api::messages_chatsSlice::ID: {
      auto *slice = static_cast<telegram_api::messages_chatsSlice *>(chats.get());
      chat_list = std::move(slice->chats_);
      full_count = slice->count_;
      is_finished = chat_list.empty();
      break;
    }
    default:
      UNREACHABLE();
  }
  // the offset counts the returned chats, including the ones without a valid identifier
  list.left_channel_offset += narrow_cast<int32>(chat_list.size());
  vector<DialogId> dialog_ids;
  for (const auto &chat : chat_list) {
    auto dialog_id = ChatManager::get_dialog_id(chat);
    if (dialog_id.is_valid()) {
      dialog_ids.push_back(dialog_id);
    }
  }
  td_->chat_manager_->on_get_chats(std::move(chat_list), "on_get_left_channels");
  list.are_left_channels_finished = is_finished;
  // left channels have messages only in the last range
  append_dialogs(list.left_channels, list.left_channel_indices, dialog_ids, true,
                 narrow_cast<int32>(export_->splits.size()) - 1);

  if (is_count) {
    export_->dialogs_count += full_count;
    return start_writer();
  }
  set_dialogs_list_state(list.processed_count + narrow_cast<int32>(list.left_channels.size()) - 1);
  if (is_finished) {
    return finish_dialogs_list();
  }
  request_left_channels(false);
}

void ExportManager::append_dialogs(vector<unique_ptr<DialogProcess>> &dialogs,
                                   FlatHashMap<DialogId, size_t, DialogIdHash> &dialog_indices,
                                   const vector<DialogId> &dialog_ids, bool is_left_channel, int32 split_index) {
  auto types = export_->settings.types;
  auto split_count = narrow_cast<int32>(export_->splits.size());
  for (auto dialog_id : dialog_ids) {
    auto type = get_export_dialog_type(td_, dialog_id);
    auto is_good_by_type = (types & get_settings_type(type)) != 0;
    ChannelId migrated_to_channel_id;
    if (dialog_id.get_type() == DialogType::Chat) {
      migrated_to_channel_id = td_->chat_manager_->get_chat_migrated_to_channel_id(dialog_id.get_chat_id());
    }
    if (migrated_to_channel_id.is_valid() && (types & ExportSettings::GroupsMask) != 0) {
      // messages of the basic group are exported before the messages of the supergroup it was upgraded to
      auto it = dialog_indices.find(DialogId(migrated_to_channel_id));
      if (it != dialog_indices.end()) {
        auto &channel_dialog = *dialogs[it->second];
        if (!channel_dialog.migrated_from_dialog_id.is_valid() || channel_dialog.migrated_from_dialog_id == dialog_id) {
          channel_dialog.migrated_from_dialog_id = dialog_id;
          auto migrated_split = split_index - split_count;
          if (!td::contains(channel_dialog.splits, migrated_split)) {
            channel_dialog.splits.push_back(migrated_split);
          }
          continue;
        }
      }
    }
    if (!is_good_by_type) {
      continue;
    }
    auto it = dialog_indices.emplace(dialog_id, dialogs.size());
    if (it.second) {
      auto dialog = make_unique<DialogProcess>();
      dialog->info.dialog_id = dialog_id;
      dialog->info.type = type;
      dialog->info.name = td_->dialog_manager_->get_dialog_title(dialog_id);
      dialog->info.is_left_channel = is_left_channel;
      dialogs.push_back(std::move(dialog));
    }
    // a chat can be returned twice when the chat list changes while it is loaded
    auto &splits = dialogs[it.first->second]->splits;
    if (!td::contains(splits, split_index)) {
      splits.push_back(split_index);
    }
  }
}

void ExportManager::request_dialogs_slice() {
  const auto &list = *export_->dialogs_list;
  CHECK(list.split_index_plus_one > 0);
  telegram_api::object_ptr<telegram_api::InputPeer> offset_peer;
  if (list.offset_dialog_id.is_valid()) {
    offset_peer = td_->dialog_manager_->get_input_peer(list.offset_dialog_id, AccessRights::Know);
  }
  if (offset_peer == nullptr) {
    offset_peer = telegram_api::make_object<telegram_api::inputPeerEmpty>();
  }
  auto promise =
      PromiseCreator::lambda([actor_id = actor_id(this), generation = export_->generation](
                                 Result<telegram_api::object_ptr<telegram_api::messages_Dialogs>> r_dialogs) {
        send_closure(actor_id, &ExportManager::on_get_dialogs_slice, generation, std::move(r_dialogs));
      });
  send_export_query(telegram_api::messages_getDialogs(0, false, 0, list.offset_date, list.offset_message_id,
                                                      std::move(offset_peer), DIALOGS_SLICE_LIMIT, 0),
                    narrow_cast<int32>(list.split_index_plus_one - 1), std::move(promise));
}

void ExportManager::on_get_dialogs_slice(uint64 generation,
                                         Result<telegram_api::object_ptr<telegram_api::messages_Dialogs>> r_dialogs) {
  if (!is_current_export(generation)) {
    return;
  }
  if (r_dialogs.is_error()) {
    return on_api_error(r_dialogs.move_as_error());
  }
  auto dialogs = r_dialogs.move_as_ok();
  vector<telegram_api::object_ptr<telegram_api::Dialog>> dialog_list;
  vector<telegram_api::object_ptr<telegram_api::Message>> messages;
  bool is_finished = false;
  switch (dialogs->get_id()) {
    case telegram_api::messages_dialogs::ID: {
      auto result = telegram_api::move_object_as<telegram_api::messages_dialogs>(dialogs);
      td_->user_manager_->on_get_users(std::move(result->users_), "on_get_dialogs_slice");
      td_->chat_manager_->on_get_chats(std::move(result->chats_), "on_get_dialogs_slice");
      dialog_list = std::move(result->dialogs_);
      messages = std::move(result->messages_);
      is_finished = true;
      break;
    }
    case telegram_api::messages_dialogsSlice::ID: {
      auto result = telegram_api::move_object_as<telegram_api::messages_dialogsSlice>(dialogs);
      td_->user_manager_->on_get_users(std::move(result->users_), "on_get_dialogs_slice");
      td_->chat_manager_->on_get_chats(std::move(result->chats_), "on_get_dialogs_slice");
      dialog_list = std::move(result->dialogs_);
      messages = std::move(result->messages_);
      is_finished = dialog_list.empty();
      break;
    }
    case telegram_api::messages_dialogsNotModified::ID:
      return on_api_error(Status::Error(500, "Receive dialogsNotModified"));
    default:
      UNREACHABLE();
  }

  auto &list = *export_->dialogs_list;
  vector<DialogId> dialog_ids;
  int32 last_top_message_id = 0;
  int32 last_top_message_date = 0;
  for (const auto &dialog_ptr : dialog_list) {
    if (dialog_ptr->get_id() != telegram_api::dialog::ID) {
      LOG(ERROR) << "Receive unexpected " << to_string(dialog_ptr);
      continue;
    }
    const auto *dialog = static_cast<const telegram_api::dialog *>(dialog_ptr.get());
    DialogId dialog_id(dialog->peer_);
    if (!dialog_id.is_valid()) {
      LOG(ERROR) << "Receive dialog in invalid " << dialog_id;
      continue;
    }
    dialog_ids.push_back(dialog_id);
    last_top_message_id = dialog->top_message_;
    last_top_message_date = 0;
    MessageId top_message_id(ServerMessageId(dialog->top_message_));
    for (const auto &message : messages) {
      if (MessageId::get_message_id(message, false) == top_message_id &&
          DialogId::get_message_dialog_id(message) == dialog_id) {
        last_top_message_date = get_message_date(message.get());
        break;
      }
    }
  }
  list.processed_count += narrow_cast<int32>(dialog_ids.size());
  append_dialogs(list.chats, list.chat_indices, dialog_ids, false, narrow_cast<int32>(list.split_index_plus_one - 1));
  if (list.processed_count > 0) {
    set_dialogs_list_state(list.processed_count - 1);
  }

  if (!is_finished && last_top_message_date > 0) {
    list.offset_date = last_top_message_date;
    list.offset_message_id = last_top_message_id;
    list.offset_dialog_id = dialog_ids.back();
    return request_dialogs_slice();
  }
  // a channel has all its messages in the last range, so other ranges are needed only for other chats
  if ((export_->settings.types & ExportSettings::NonChannelChatsMask) != 0 && --list.split_index_plus_one > 0) {
    list.offset_date = 0;
    list.offset_message_id = 0;
    list.offset_dialog_id = DialogId();
    return request_dialogs_slice();
  }
  if ((export_->settings.types & ExportSettings::GroupsChannelsMask) == 0) {
    return finish_dialogs_list();
  }
  if (list.are_left_channels_finished) {
    set_dialogs_list_state(list.processed_count + narrow_cast<int32>(list.left_channels.size()) - 1);
    return finish_dialogs_list();
  }
  request_left_channels(false);
}

void ExportManager::finish_dialogs_list() {
  auto list = std::move(export_->dialogs_list);
  CHECK(list != nullptr);
  const auto &settings = export_->settings;
  auto full_count = narrow_cast<int32>(list->chats.size() + list->left_channels.size());
  auto digit_count = to_string(full_count - 1).size();
  int32 index = 0;
  auto get_relative_path = [&] {
    auto number = to_string(++index);
    if (number.size() < digit_count) {
      number.insert(0, digit_count - number.size(), '0');
    }
    return PSTRING() << "chats/chat_" << number << '/';
  };
  for (auto &dialog : list->chats) {
    auto &info = dialog->info;
    info.relative_path = get_relative_path();
    auto settings_type = get_settings_type(info.type);
    info.only_my_messages =
        info.type != ExportDialogInfo::Type::Personal && (settings.full_chats & settings_type) != settings_type;
    std::sort(dialog->splits.begin(), dialog->splits.end());
    export_->dialogs.push_back(std::move(dialog));
  }
  for (auto &dialog : list->left_channels) {
    dialog->info.relative_path = get_relative_path();
    dialog->info.only_my_messages = true;
    export_->dialogs.push_back(std::move(dialog));
  }
  export_next();
}

void ExportManager::start_writer() {
  CHECK(export_ != nullptr);
  export_->writer = create_export_writer(td_, export_->settings.format);
  CHECK(export_->writer != nullptr);
  if (!on_io_result(export_->writer->start(export_->settings, &export_->stats))) {
    return;
  }
  fill_substeps_in_steps();
  export_next();
}

void ExportManager::collect_dialogs_list() {
  set_dialogs_list_state(0);
  if (!export_->settings.only_single_dialog()) {
    CHECK(export_->dialogs_list != nullptr);
    export_->dialogs_list->split_index_plus_one = export_->splits.size();
    return request_dialogs_slice();
  }
  auto dialog_id = export_->settings.single_dialog_id;
  if (dialog_id.get_type() == DialogType::Channel &&
      td_->chat_manager_->is_megagroup_channel(dialog_id.get_channel_id())) {
    // the basic group the supergroup was upgraded from is known only from the full info
    auto promise = PromiseCreator::lambda(
        [actor_id = actor_id(this), generation = export_->generation](Result<Unit> result) mutable {
          send_closure(actor_id, &ExportManager::on_load_single_dialog_full_info, generation, std::move(result));
        });
    TRY_STATUS_PROMISE(promise, G()->close_status());
    return td_->chat_manager_->load_channel_full(dialog_id.get_channel_id(), false, std::move(promise),
                                                 "collect_dialogs_list");
  }
  add_single_dialog();
}

void ExportManager::on_load_single_dialog_full_info(uint64 generation, Result<Unit> result) {
  if (!is_current_export(generation)) {
    return;
  }
  if (result.is_error()) {
    return on_api_error(result.move_as_error());
  }
  add_single_dialog();
}

void ExportManager::set_dialogs_list_state(int32 processed) {
  ProcessingState state;
  state.step = Step::DialogsList;
  state.entity_index = max(processed, 0);
  state.entity_count = max(processed, get_substeps_in_step(Step::Dialogs));
  set_processing_state(std::move(state));
}

void ExportManager::add_single_dialog() {
  const auto &settings = export_->settings;
  auto dialog_id = settings.single_dialog_id;
  auto dialog = make_unique<DialogProcess>();
  dialog->info.dialog_id = dialog_id;
  dialog->info.type = get_export_dialog_type(td_, dialog_id);
  dialog->info.name = td_->dialog_manager_->get_dialog_title(dialog_id);
  auto settings_type = get_settings_type(dialog->info.type);
  dialog->info.only_my_messages =
      dialog->info.type != ExportDialogInfo::Type::Personal && (settings.full_chats & settings_type) != settings_type;

  // message identifiers of private chats and basic groups are shared by the account and split into ranges,
  // while a channel has all its messages in the last range
  auto split_count = narrow_cast<int32>(export_->splits.size());
  if (dialog_id.get_type() == DialogType::Channel) {
    auto migrated_from_chat_id = td_->chat_manager_->get_channel_migrated_from_chat_id(dialog_id.get_channel_id());
    if (migrated_from_chat_id.is_valid()) {
      dialog->migrated_from_dialog_id = DialogId(migrated_from_chat_id);
      for (int32 i = 0; i < split_count; i++) {
        dialog->splits.push_back(i - split_count);
      }
    }
    dialog->splits.push_back(split_count - 1);
  } else {
    for (int32 i = 0; i < split_count; i++) {
      dialog->splits.push_back(i);
    }
  }
  export_->dialogs.push_back(std::move(dialog));
  export_next();
}

void ExportManager::export_dialogs() {
  if (!on_io_result(export_->writer->write_dialogs_start(narrow_cast<int32>(export_->dialogs.size())))) {
    return;
  }
  export_->dialog_index = 0;
  export_next_dialog();
}

void ExportManager::export_next_dialog() {
  CHECK(export_->dialog == nullptr);
  if (export_->dialog_index == export_->dialogs.size()) {
    export_->dialogs.clear();
    if (!on_io_result(export_->writer->write_dialogs_end())) {
      return;
    }
    return export_next();
  }
  export_->dialog = std::move(export_->dialogs[export_->dialog_index]);
  CHECK(export_->dialog != nullptr);
  export_->dialog->split_message_counts.resize(export_->dialog->splits.size());
  request_messages_count(0);
}

void ExportManager::request_messages_count(size_t split_index) {
  request_chat_messages(MessagesRequestType::Count, split_index, 0, 0, 1);
}

void ExportManager::request_chat_messages(MessagesRequestType type, size_t split_index, int32 offset_id,
                                          int32 add_offset, int32 limit) {
  CHECK(export_ != nullptr);
  CHECK(export_->dialog != nullptr);
  const auto &dialog = *export_->dialog;
  CHECK(split_index < dialog.splits.size());
  auto split = dialog.splits[split_index];
  auto dialog_id = split >= 0 ? dialog.info.dialog_id : dialog.migrated_from_dialog_id;
  auto real_split = split >= 0 ? split : narrow_cast<int32>(export_->splits.size()) + split;
  // access to left channels and to basic groups, which were upgraded, is checked by the server
  auto input_peer = td_->dialog_manager_->get_input_peer(dialog_id, AccessRights::Know);
  if (input_peer == nullptr) {
    return on_api_error(Status::Error(400, "Chat not found"));
  }

  // the first slice of a range starts at the minimum date instead of the first message; the server puts messages
  // dated offset_date before the offset, so the slice starts a second earlier
  auto history_offset_id = offset_id;
  int32 history_offset_date = 0;
  if (type == MessagesRequestType::Slice && offset_id == 1 && export_->settings.single_dialog_min_date > 1) {
    history_offset_id = 0;
    history_offset_date = export_->settings.single_dialog_min_date - 1;
  }
  auto promise = PromiseCreator::lambda(
      [actor_id = actor_id(this), generation = export_->generation, type, split_index, offset_id, add_offset,
       limit](Result<telegram_api::object_ptr<telegram_api::messages_Messages>> r_messages) mutable {
        send_closure(actor_id, &ExportManager::on_get_chat_messages, generation, type, split_index, offset_id,
                     add_offset, limit, std::move(r_messages));
      });
  if (dialog.info.only_my_messages) {
    telegram_api::object_ptr<telegram_api::InputPeer> from;
    if (dialog_id.get_type() == DialogType::Channel &&
        td_->chat_manager_->is_admined_monoforum_channel(dialog_id.get_channel_id())) {
      // own messages in an administered monoforum are sent on behalf of its channel
      from = td_->dialog_manager_->get_input_peer(
          DialogId(td_->chat_manager_->get_monoforum_channel_id(dialog_id.get_channel_id())), AccessRights::Know);
    }
    if (from == nullptr) {
      from = telegram_api::make_object<telegram_api::inputPeerSelf>();
    }
    send_export_query(telegram_api::messages_search(0, std::move(input_peer), string(), std::move(from), nullptr, {}, 0,
                                                    telegram_api::make_object<telegram_api::inputMessagesFilterEmpty>(),
                                                    0, 0, offset_id, add_offset, limit, 0, 0, 0),
                      real_split, std::move(promise));
  } else {
    send_export_query(telegram_api::messages_getHistory(std::move(input_peer), history_offset_id, history_offset_date,
                                                        add_offset, limit, 0, 0, 0),
                      real_split, std::move(promise));
  }
}

void ExportManager::on_get_chat_messages(uint64 generation, MessagesRequestType type, size_t split_index,
                                         int32 offset_id, int32 add_offset, int32 limit,
                                         Result<telegram_api::object_ptr<telegram_api::messages_Messages>> r_messages) {
  if (!is_current_export(generation)) {
    return;
  }
  CHECK(export_->dialog != nullptr);
  auto &dialog = *export_->dialog;
  if (r_messages.is_error()) {
    auto split = dialog.splits[split_index];
    auto dialog_id = split >= 0 ? dialog.info.dialog_id : dialog.migrated_from_dialog_id;
    if (r_messages.error().message() == "CHANNEL_PRIVATE" && !dialog.info.only_my_messages &&
        dialog_id.get_type() == DialogType::Channel) {
      // the chat was left or the user was banned from it, so only own messages can be exported
      dialog.info.only_my_messages = true;
      return request_chat_messages(type, split_index, offset_id, add_offset, limit);
    }
    return on_api_error(r_messages.move_as_error());
  }

  auto messages = r_messages.move_as_ok();
  if (messages->get_id() == telegram_api::messages_messagesNotModified::ID) {
    return on_api_error(Status::Error(500, "Receive messagesNotModified"));
  }
  switch (type) {
    case MessagesRequestType::Count: {
      int32 count = 0;
      switch (messages->get_id()) {
        case telegram_api::messages_messages::ID:
          count = narrow_cast<int32>(
              static_cast<const telegram_api::messages_messages *>(messages.get())->messages_.size());
          break;
        case telegram_api::messages_messagesSlice::ID:
          count = static_cast<const telegram_api::messages_messagesSlice *>(messages.get())->count_;
          break;
        case telegram_api::messages_channelMessages::ID:
          count = static_cast<const telegram_api::messages_channelMessages *>(messages.get())->count_;
          break;
        default:
          UNREACHABLE();
      }
      // the last message of the range is the only one returned
      auto last_date = get_single_message_date(messages);
      if (last_date <= 0 || last_date < export_->settings.single_dialog_min_date) {
        return on_messages_count_loaded(split_index, 0);
      }
      return check_first_message_date(split_index, count);
    }
    case MessagesRequestType::FirstDate: {
      auto date = get_single_message_date(messages);
      auto max_date = export_->settings.single_dialog_max_date;
      auto is_skipped = date <= 0 || date >= max_date;
      return on_messages_count_loaded(split_index, is_skipped ? 0 : dialog.pending_count);
    }
    case MessagesRequestType::Slice: {
      vector<telegram_api::object_ptr<telegram_api::Message>> message_list;
      switch (messages->get_id()) {
        case telegram_api::messages_messages::ID: {
          auto result = telegram_api::move_object_as<telegram_api::messages_messages>(messages);
          td_->user_manager_->on_get_users(std::move(result->users_), "on_get_chat_messages");
          td_->chat_manager_->on_get_chats(std::move(result->chats_), "on_get_chat_messages");
          message_list = std::move(result->messages_);
          dialog.is_last_slice = true;
          break;
        }
        case telegram_api::messages_messagesSlice::ID: {
          auto result = telegram_api::move_object_as<telegram_api::messages_messagesSlice>(messages);
          td_->user_manager_->on_get_users(std::move(result->users_), "on_get_chat_messages");
          td_->chat_manager_->on_get_chats(std::move(result->chats_), "on_get_chat_messages");
          message_list = std::move(result->messages_);
          break;
        }
        case telegram_api::messages_channelMessages::ID: {
          auto result = telegram_api::move_object_as<telegram_api::messages_channelMessages>(messages);
          td_->user_manager_->on_get_users(std::move(result->users_), "on_get_chat_messages");
          td_->chat_manager_->on_get_chats(std::move(result->chats_), "on_get_chat_messages");
          message_list = std::move(result->messages_);
          break;
        }
        default:
          UNREACHABLE();
      }
      return on_get_messages_slice(std::move(message_list));
    }
    default:
      UNREACHABLE();
  }
}

void ExportManager::check_first_message_date(size_t split_index, int32 count) {
  if (export_->settings.single_dialog_max_date <= 0) {
    return on_messages_count_loaded(split_index, count);
  }
  // request the first message of the range to check whether it is before the maximum date
  export_->dialog->pending_count = count;
  request_chat_messages(MessagesRequestType::FirstDate, split_index, 1, -1, 1);
}

void ExportManager::on_messages_count_loaded(size_t split_index, int32 count) {
  auto &dialog = *export_->dialog;
  dialog.split_message_counts[split_index] = count;
  if (split_index + 1 < dialog.splits.size()) {
    return request_messages_count(split_index + 1);
  }
  if (!on_io_result(export_->writer->write_dialog_start(dialog.info))) {
    return;
  }
  dialog.received_message_count = 0;
  dialog.message_count = 0;
  for (auto split_count : dialog.split_message_counts) {
    dialog.message_count += split_count;
  }
  set_dialogs_state();
  request_messages_slice();
}

void ExportManager::request_messages_slice() {
  auto &dialog = *export_->dialog;
  if (dialog.split_message_counts[dialog.split_index] == 0) {
    return on_get_messages_slice({});
  }
  request_chat_messages(MessagesRequestType::Slice, dialog.split_index, dialog.largest_id_plus_one,
                        -MESSAGES_SLICE_LIMIT, MESSAGES_SLICE_LIMIT);
}

void ExportManager::on_get_messages_slice(vector<telegram_api::object_ptr<telegram_api::Message>> &&messages) {
  auto &dialog = *export_->dialog;
  if (messages.empty()) {
    dialog.is_last_slice = true;
    return request_next_messages_slice();
  }
  auto max_date = export_->settings.single_dialog_max_date;
  if (max_date > 0 && get_message_date(messages[0].get()) >= max_date) {
    // the newest message of the slice is after the range, so the next slices are too
    dialog.is_last_slice = true;
  }

  auto is_migrated = dialog.splits[dialog.split_index] < 0;
  auto dialog_id = is_migrated ? dialog.migrated_from_dialog_id : dialog.info.dialog_id;
  CHECK(dialog.slice.empty());
  dialog.slice.reserve(messages.size());
  // the messages are received from the newest to the oldest
  for (auto it = messages.rbegin(); it != messages.rend(); ++it) {
    auto message_id = MessageId::get_message_id(*it, false);
    if (message_id.is_server()) {
      dialog.largest_id_plus_one = max(dialog.largest_id_plus_one, message_id.get_server_message_id().get() + 1);
    }
    ExportMessage export_message;
    export_message.is_history_cleared =
        (*it)->get_id() == telegram_api::messageService::ID &&
        static_cast<const telegram_api::messageService *>(it->get())->action_->get_id() ==
            telegram_api::messageActionHistoryClear::ID;
    export_message.message = td_->messages_manager_->get_export_message_object(dialog_id, std::move(*it));
    if (export_message.message != nullptr) {
      dialog.slice.push_back(std::move(export_message));
    }
  }
  dialog.slice_message_count = narrow_cast<int32>(messages.size());

  // the work points to the messages of the slice, so it is added after the slice is complete
  for (auto &export_message : dialog.slice) {
    add_message_file_work(export_message);
  }
  dialog.file_work_index = 0;

  vector<CustomEmojiId> custom_emoji_ids;
  auto add_custom_emoji = [&](int64 id) {
    CustomEmojiId custom_emoji_id(id);
    if (custom_emoji_id.is_valid() &&
        export_->custom_emoji_files.emplace(custom_emoji_id, ExportFile{string(), ExportFile::SkipReason::Unavailable})
            .second) {
      custom_emoji_ids.push_back(custom_emoji_id);
    }
  };
  auto add_text_custom_emoji = [&](const td_api::formattedText *text) {
    if (text == nullptr) {
      return;
    }
    // only the entities that aren't nested in another one are written
    for (const auto &part : get_export_text_parts(text->text_, &text->entities_)) {
      if (part.type != nullptr && part.type->get_id() == td_api::textEntityTypeCustomEmoji::ID) {
        add_custom_emoji(static_cast<const td_api::textEntityTypeCustomEmoji *>(part.type)->custom_emoji_id_);
      }
    }
  };
  for (const auto &export_message : dialog.slice) {
    const auto &message = *export_message.message;
    if (!export_->settings.is_inside_date_range(message.date_)) {
      continue;
    }
    const auto *content = message.content_.get();
    add_text_custom_emoji(get_export_message_text(content));
    // only the HTML pages show the custom emoji of polls and checklists
    switch (export_->settings.format == ExportFormat::Json ? 0 : content->get_id()) {
      case td_api::messagePoll::ID: {
        const auto *poll = static_cast<const td_api::messagePoll *>(content)->poll_.get();
        add_text_custom_emoji(poll->question_.get());
        for (const auto &option : poll->options_) {
          add_text_custom_emoji(option->text_.get());
        }
        break;
      }
      case td_api::messageChecklist::ID: {
        const auto *checklist = static_cast<const td_api::messageChecklist *>(content)->list_.get();
        add_text_custom_emoji(checklist->title_.get());
        for (const auto &task : checklist->tasks_) {
          add_text_custom_emoji(task->text_.get());
        }
        break;
      }
      default:
        break;
    }
    if (message.interaction_info_ != nullptr && message.interaction_info_->reactions_ != nullptr) {
      for (const auto &reaction : message.interaction_info_->reactions_->reactions_) {
        if (reaction->type_->get_id() == td_api::reactionTypeCustomEmoji::ID) {
          add_custom_emoji(
              static_cast<const td_api::reactionTypeCustomEmoji *>(reaction->type_.get())->custom_emoji_id_);
        }
      }
    }
  }
  if (custom_emoji_ids.empty()) {
    return load_next_message_file();
  }
  auto promise = PromiseCreator::lambda([actor_id = actor_id(this), generation = export_->generation](
                                            Result<td_api::object_ptr<td_api::stickers>> r_stickers) mutable {
    send_closure(actor_id, &ExportManager::on_get_custom_emoji_stickers, generation, std::move(r_stickers));
  });
  TRY_STATUS_PROMISE(promise, G()->close_status());
  td_->stickers_manager_->get_custom_emoji_stickers_unlimited(std::move(custom_emoji_ids), std::move(promise));
}

void ExportManager::on_get_custom_emoji_stickers(uint64 generation,
                                                 Result<td_api::object_ptr<td_api::stickers>> r_stickers) {
  if (!is_current_export(generation)) {
    return;
  }
  auto &dialog = *export_->dialog;
  if (r_stickers.is_error()) {
    // the custom emoji stay unavailable
    LOG(INFO) << "Failed to get custom emoji: " << r_stickers.error();
  } else {
    dialog.custom_emoji_stickers = std::move(r_stickers.ok_ref()->stickers_);
  }
  for (const auto &sticker : dialog.custom_emoji_stickers) {
    if (sticker->full_type_ == nullptr || sticker->full_type_->get_id() != td_api::stickerFullTypeCustomEmoji::ID) {
      continue;
    }
    CustomEmojiId custom_emoji_id(
        static_cast<const td_api::stickerFullTypeCustomEmoji *>(sticker->full_type_.get())->custom_emoji_id_);
    auto it = export_->custom_emoji_files.find(custom_emoji_id);
    if (it == export_->custom_emoji_files.end()) {
      continue;
    }
    MessageFileWork work;
    work.target = &it->second;
    work.file = sticker->sticker_.get();
    work.suggested_path = get_document_path(
        ExportSettings::Sticker, Slice(), get_sticker_format_mime_type(get_sticker_format(sticker->format_)), Slice());
    work.media_type = ExportSettings::Sticker;
    work.controlling_size = get_export_file_size(work.file);
    work.custom_emoji_id = custom_emoji_id;
    dialog.file_works.push_back(std::move(work));
  }
  load_next_message_file();
}

void ExportManager::add_message_file_work(ExportMessage &export_message) {
  const auto &settings = export_->settings;
  const auto &message = *export_message.message;
  auto &dialog = *export_->dialog;
  auto &file_works = dialog.file_works;
  // files are numbered even in skipped messages, so that the numbers don't depend on the date range
  auto is_inside_date_range = settings.is_inside_date_range(message.date_);
  auto is_available = message.self_destruct_type_ == nullptr;
  string date_part;
  if (message.date_ > 0) {
    date_part = "@" + get_export_file_date_time_string(static_cast<int64>(message.date_) + settings.utc_time_offset);
  }
  auto add_work = [&](ExportFile &target, const td_api::file *file, string content, string suggested_path,
                      int32 media_type, int64 controlling_size, bool is_file_available) {
    if (!is_inside_date_range) {
      return;
    }
    MessageFileWork work;
    work.target = &target;
    work.file = file;
    work.content = std::move(content);
    work.suggested_path = std::move(suggested_path);
    work.media_type = media_type;
    work.controlling_size = controlling_size;
    work.is_available = is_file_available;
    work.message_id = MessageId(message.id_);
    file_works.push_back(std::move(work));
  };
  auto add_photo = [&](const vector<td_api::object_ptr<td_api::photoSize>> *sizes, bool is_photo_available,
                       int32 thumbnail_box_size) {
    string path = PSTRING() << dialog.info.relative_path << "photos/photo_" << ++dialog.photo_count << date_part;
    const auto *size = sizes == nullptr ? nullptr : get_export_photo_size(*sizes);
    const auto *file = size == nullptr ? nullptr : size->photo_.get();
    auto file_size = get_export_file_size(file);
    add_work(export_message.file, file, string(), path + ".jpg", ExportSettings::Photo, file_size, is_photo_available);
    if (settings.format != ExportFormat::Json && file != nullptr) {
      // HTML pages show a smaller size instead of scaling the photo
      const auto *thumbnail_size = get_export_photo_thumbnail_size(*sizes, thumbnail_box_size);
      if (thumbnail_size != nullptr) {
        add_work(export_message.thumbnail, thumbnail_size->photo_.get(), string(), path + "_thumb.jpg",
                 ExportSettings::Photo, file_size, is_photo_available);
      }
    }
  };
  auto add_document = [&](int32 media_type, const td_api::file *file, Slice file_name, Slice mime_type,
                          const td_api::thumbnail *thumbnail) {
    auto path = get_document_path(media_type, file_name, mime_type, date_part);
    auto size = get_export_file_size(file);
    add_work(export_message.file, file, string(), path, media_type, size, is_available);
    if (thumbnail != nullptr && thumbnail->width_ > 0) {
      // the thumbnail of a self-destructing media is kept for the sender
      add_work(export_message.thumbnail, thumbnail->file_.get(), string(), path + "_thumb.jpg", media_type, size,
               is_available || message.is_outgoing_);
    }
  };

  const auto *content = message.content_.get();
  switch (content->get_id()) {
    case td_api::messagePhoto::ID:
      add_photo(&static_cast<const td_api::messagePhoto *>(content)->photo_->sizes_, is_available,
                EXPORT_HTML_PHOTO_SIZE);
      break;
    case td_api::messageChatChangePhoto::ID: {
      const auto *photo = static_cast<const td_api::messageChatChangePhoto *>(content)->photo_.get();
      add_photo(photo == nullptr ? nullptr : &photo->sizes_, true, 2 * EXPORT_HTML_SERVICE_PHOTO_SIZE);
      break;
    }
    case td_api::messageSuggestProfilePhoto::ID: {
      const auto *photo = static_cast<const td_api::messageSuggestProfilePhoto *>(content)->photo_.get();
      add_photo(photo == nullptr ? nullptr : &photo->sizes_, true, 2 * EXPORT_HTML_SERVICE_PHOTO_SIZE);
      break;
    }
    case td_api::messageAnimation::ID: {
      const auto *animation = static_cast<const td_api::messageAnimation *>(content)->animation_.get();
      add_document(ExportSettings::Gif, animation->animation_.get(), animation->file_name_, animation->mime_type_,
                   animation->thumbnail_.get());
      break;
    }
    case td_api::messageAudio::ID: {
      const auto *audio = static_cast<const td_api::messageAudio *>(content)->audio_.get();
      add_document(ExportSettings::File, audio->audio_.get(), audio->file_name_, audio->mime_type_,
                   audio->album_cover_thumbnail_.get());
      break;
    }
    case td_api::messageDocument::ID: {
      const auto *document = static_cast<const td_api::messageDocument *>(content)->document_.get();
      add_document(ExportSettings::File, document->document_.get(), document->file_name_, document->mime_type_,
                   document->thumbnail_.get());
      break;
    }
    case td_api::messageSticker::ID: {
      const auto *sticker = static_cast<const td_api::messageSticker *>(content)->sticker_.get();
      add_document(ExportSettings::Sticker, sticker->sticker_.get(), Slice(),
                   get_sticker_format_mime_type(get_sticker_format(sticker->format_)), sticker->thumbnail_.get());
      break;
    }
    case td_api::messageVideo::ID: {
      const auto *video = static_cast<const td_api::messageVideo *>(content)->video_.get();
      add_document(ExportSettings::Video, video->video_.get(), video->file_name_, video->mime_type_,
                   video->thumbnail_.get());
      break;
    }
    case td_api::messageVideoNote::ID: {
      const auto *video_note = static_cast<const td_api::messageVideoNote *>(content)->video_note_.get();
      add_document(ExportSettings::VideoMessage, video_note->video_.get(), Slice(), Slice("video/mp4"),
                   video_note->thumbnail_.get());
      break;
    }
    case td_api::messageVoiceNote::ID: {
      const auto *voice_note = static_cast<const td_api::messageVoiceNote *>(content)->voice_note_.get();
      add_document(ExportSettings::VoiceMessage, voice_note->voice_.get(), Slice(), voice_note->mime_type_, nullptr);
      break;
    }
    case td_api::messageContact::ID: {
      const auto &vcard = static_cast<const td_api::messageContact *>(content)->contact_->vcard_;
      if (!vcard.empty()) {
        string path = PSTRING() << dialog.info.relative_path << "contacts/contact_" << ++dialog.contact_count
                                << ".vcard";
        // a vCard is exported together with photos
        add_work(export_message.file, nullptr, vcard, std::move(path), ExportSettings::Photo,
                 static_cast<int64>(vcard.size()), true);
      }
      break;
    }
    default:
      break;
  }
}

string ExportManager::get_document_path(int32 media_type, Slice file_name, Slice mime_type, Slice date_part) {
  Slice folder;
  switch (media_type) {
    case ExportSettings::File:
      folder = Slice("files");
      break;
    case ExportSettings::Video:
      folder = Slice("video_files");
      break;
    case ExportSettings::Gif:
      folder = Slice("animations");
      break;
    case ExportSettings::Sticker:
      folder = Slice("stickers");
      break;
    case ExportSettings::VoiceMessage:
      folder = Slice("voice_messages");
      break;
    case ExportSettings::VideoMessage:
      folder = Slice("round_video_messages");
      break;
    default:
      UNREACHABLE();
  }
  auto &dialog = *export_->dialog;
  auto name = get_export_file_name(file_name);
  if (name.empty()) {
    if (media_type == ExportSettings::VoiceMessage) {
      name = PSTRING() << "audio_" << ++dialog.audio_count << date_part
                       << (to_lower(mime_type) == "audio/mp3" ? ".mp3" : ".ogg");
    } else if (media_type == ExportSettings::Video) {
      name = PSTRING() << "video_" << ++dialog.video_count << date_part << '.'
                       << MimeType::to_extension(mime_type, "mov");
    } else {
      name = PSTRING() << "file_" << ++dialog.document_count << date_part << '.'
                       << MimeType::to_extension(mime_type, "unknown");
    }
  }
  return PSTRING() << dialog.info.relative_path << folder << '/' << name;
}

void ExportManager::load_next_message_file() {
  auto &dialog = *export_->dialog;
  while (dialog.file_work_index < dialog.file_works.size()) {
    if (!process_message_file(dialog.file_works[dialog.file_work_index])) {
      // the file is being downloaded or the export has failed
      return;
    }
    dialog.file_work_index++;
  }
  write_messages_slice();
}

bool ExportManager::process_message_file(MessageFileWork &work) {
  const auto &settings = export_->settings;
  auto &target = *work.target;
  if (!work.is_available) {
    target.skip_reason = ExportFile::SkipReason::Unavailable;
    return true;
  }
  FileView file_view;
  const FullRemoteFileLocation *remote_location = nullptr;
  if (work.file != nullptr) {
    file_view = td_->file_manager_->get_file_view(FileId(work.file->id_, 0));
    if (!file_view.empty()) {
      remote_location = file_view.get_full_remote_location();
      if (remote_location != nullptr && remote_location->is_web()) {
        remote_location = nullptr;
      }
    }
  }
  if (remote_location == nullptr && work.content.empty()) {
    target.skip_reason = ExportFile::SkipReason::Unavailable;
    return true;
  }
  if ((settings.media_types & work.media_type) != work.media_type) {
    target.skip_reason = ExportFile::SkipReason::FileType;
    return true;
  }
  if (work.controlling_size > settings.media_size_limit) {
    target.skip_reason = ExportFile::SkipReason::FileSize;
    return true;
  }
  int32 key = 0;
  if (remote_location != nullptr) {
    key = file_view.get_main_file_id().get();
    auto it = export_->loaded_files.find(key);
    if (it != export_->loaded_files.end()) {
      target.relative_path = it->second;
      target.skip_reason = ExportFile::SkipReason::None;
      return true;
    }
  }

  auto relative_path = get_unique_relative_path(work.suggested_path);
  if (!work.content.empty()) {
    ExportOutputFile output(get_file_path(relative_path), &export_->stats);
    if (!on_io_result(output.write(work.content))) {
      return false;
    }
    target.relative_path = std::move(relative_path);
    target.skip_reason = ExportFile::SkipReason::None;
    return true;
  }

  auto file = td::make_unique<FileProcess>(get_file_path(relative_path), &export_->stats);
  file->id = ++export_->last_file_id;
  file->key = key;
  file->relative_path = std::move(relative_path);
  file->dc_id = remote_location->get_dc_id();
  file->query = telegram_api::make_object<telegram_api::upload_getFile>(
      0, false, false, remote_location->as_input_file_location(), 0, FILE_PART_SIZE);
  // the expected size can be an estimate, so a file of unknown size is downloaded until an empty part
  file->size = work.file->size_;
  file->message_id = work.message_id;
  file->custom_emoji_id = work.custom_emoji_id;
  LOG(INFO) << "Load file " << file->relative_path << " of size " << file->size;
  export_->file = std::move(file);
  set_dialogs_state();
  load_file_part();
  return false;
}

string ExportManager::get_unique_relative_path(const string &suggested_path) const {
  if (stat(get_file_path(suggested_path)).is_error()) {
    return suggested_path;
  }
  // the first dot is used, so that "file.tar.gz" keeps its extension
  auto dot_pos = suggested_path.find('.');
  auto base = suggested_path.substr(0, dot_pos);
  auto extension = dot_pos == string::npos ? string() : suggested_path.substr(dot_pos);
  for (int32 attempt = 1;; attempt++) {
    string path = PSTRING() << base << " (" << attempt << ')' << extension;
    if (stat(get_file_path(path)).is_error()) {
      return path;
    }
  }
}

string ExportManager::get_file_path(Slice relative_path) const {
  auto result = export_->settings.path + relative_path.str();
  for (auto i = export_->settings.path.size(); i < result.size(); i++) {
    if (result[i] == '/') {
      result[i] = TD_DIR_SLASH;
    }
  }
  return result;
}

void ExportManager::load_file_part() {
  CHECK(export_ != nullptr);
  CHECK(export_->file != nullptr);
  auto &file = *export_->file;
  file.query->offset_ = file.offset;
  auto promise = PromiseCreator::lambda(
      [actor_id = actor_id(this), generation = export_->generation, file_id = file.id,
       offset = file.offset](Result<telegram_api::object_ptr<telegram_api::upload_File>> r_file) mutable {
        send_closure(actor_id, &ExportManager::on_get_file_part, generation, file_id, offset, std::move(r_file));
      });
  vector<telegram_api::object_ptr<telegram_api::Function>> prefixes;
  prefixes.push_back(telegram_api::make_object<telegram_api::invokeWithTakeoutPrefix>(export_->takeout_id));
  TRY_STATUS_PROMISE(promise, G()->close_status());
  td_->create_handler<ExportQuery<telegram_api::upload_getFile>>(std::move(promise))
      ->send(prefixes, *file.query, file.dc_id, NetQuery::Type::Download);
}

void ExportManager::on_get_file_part(uint64 generation, int64 file_id, int64 offset,
                                     Result<telegram_api::object_ptr<telegram_api::upload_File>> r_file) {
  if (!is_current_export(generation) || export_->file == nullptr || export_->file->id != file_id ||
      export_->file->offset != offset) {
    return;
  }
  auto &file = *export_->file;
  if (r_file.is_error()) {
    auto error = r_file.move_as_error();
    if (error.message() == "LOCATION_INVALID" || error.message() == "VERSION_INVALID" ||
        error.message() == "LOCATION_NOT_AVAILABLE") {
      LOG(INFO) << "File " << file.relative_path << " is unavailable: " << error;
      return finish_file(false);
    }
    if (error.code() == 400 && begins_with(error.message(), "FILE_REFERENCE_")) {
      return refresh_file_reference();
    }
    return on_api_error(std::move(error));
  }

  auto result = r_file.move_as_ok();
  if (result->get_id() != telegram_api::upload_file::ID) {
    return on_api_error(Status::Error(500, "Receive CDN redirect"));
  }
  auto bytes = std::move(static_cast<telegram_api::upload_file *>(result.get())->bytes_);
  if (bytes.empty()) {
    if (file.size > 0) {
      return on_api_error(Status::Error(500, "Receive empty file part"));
    }
    // the file of unknown size ends with an empty part; an empty file must still be created
    if (!on_io_result(file.output.write(Slice()))) {
      return;
    }
    return finish_file(true);
  }
  if (!on_io_result(file.output.write(bytes.as_slice()))) {
    return;
  }
  file.is_file_reference_refreshed = false;
  file.downloaded_size += static_cast<int64>(bytes.size());
  file.offset += FILE_PART_SIZE;
  if (file.size > 0 && file.offset >= file.size) {
    return finish_file(true);
  }
  set_dialogs_state();
  load_file_part();
}

void ExportManager::refresh_file_reference() {
  auto &file = *export_->file;
  if (file.is_file_reference_refreshed) {
    LOG(INFO) << "File reference of " << file.relative_path << " is invalid after refresh";
    return finish_file(false);
  }
  file.is_file_reference_refreshed = true;

  if (file.custom_emoji_id.is_valid()) {
    auto promise = PromiseCreator::lambda(
        [actor_id = actor_id(this), generation = export_->generation,
         file_id = file.id](Result<vector<telegram_api::object_ptr<telegram_api::Document>>> r_documents) mutable {
          send_closure(actor_id, &ExportManager::on_refresh_custom_emoji_file_reference, generation, file_id,
                       std::move(r_documents));
        });
    return send_export_query(telegram_api::messages_getCustomEmojiDocuments({file.custom_emoji_id.get()}), -1,
                             std::move(promise));
  }

  const auto &dialog = *export_->dialog;
  auto split = dialog.splits[dialog.split_index];
  auto dialog_id = split >= 0 ? dialog.info.dialog_id : dialog.migrated_from_dialog_id;
  vector<telegram_api::object_ptr<telegram_api::InputMessage>> message_ids;
  message_ids.push_back(
      telegram_api::make_object<telegram_api::inputMessageID>(file.message_id.get_server_message_id().get()));
  auto promise =
      PromiseCreator::lambda([actor_id = actor_id(this), generation = export_->generation, file_id = file.id](
                                 Result<telegram_api::object_ptr<telegram_api::messages_Messages>> r_messages) mutable {
        send_closure(actor_id, &ExportManager::on_refresh_file_reference, generation, file_id, std::move(r_messages));
      });
  if (dialog_id.get_type() == DialogType::Channel) {
    auto input_channel = td_->chat_manager_->get_input_channel(dialog_id.get_channel_id());
    if (input_channel == nullptr) {
      return finish_file(false);
    }
    send_export_query(telegram_api::channels_getMessages(std::move(input_channel), std::move(message_ids)), -1,
                      std::move(promise));
  } else {
    auto real_split = split >= 0 ? split : narrow_cast<int32>(export_->splits.size()) + split;
    send_export_query(telegram_api::messages_getMessages(std::move(message_ids)), real_split, std::move(promise));
  }
}

void ExportManager::on_refresh_file_reference(
    uint64 generation, int64 file_id, Result<telegram_api::object_ptr<telegram_api::messages_Messages>> r_messages) {
  if (!is_current_export(generation) || export_->file == nullptr || export_->file->id != file_id) {
    return;
  }
  auto &file = *export_->file;
  if (r_messages.is_error()) {
    LOG(INFO) << "Failed to refresh file reference of " << file.relative_path << ": " << r_messages.error();
    return finish_file(false);
  }
  const auto *message_list = get_message_list(r_messages.ok().get());
  if (message_list != nullptr) {
    for (const auto &message : *message_list) {
      if (MessageId::get_message_id(message, false) == file.message_id &&
          update_file_reference(file.query->location_.get(), message.get())) {
        return load_file_part();
      }
    }
  }
  LOG(INFO) << "Failed to find file reference of " << file.relative_path;
  finish_file(false);
}

void ExportManager::on_refresh_custom_emoji_file_reference(
    uint64 generation, int64 file_id, Result<vector<telegram_api::object_ptr<telegram_api::Document>>> r_documents) {
  if (!is_current_export(generation) || export_->file == nullptr || export_->file->id != file_id) {
    return;
  }
  auto &file = *export_->file;
  if (r_documents.is_ok()) {
    for (const auto &document : r_documents.ok()) {
      if (update_file_reference(file.query->location_.get(), document.get())) {
        return load_file_part();
      }
    }
  }
  LOG(INFO) << "Failed to refresh file reference of " << file.relative_path;
  finish_file(false);
}

void ExportManager::finish_file(bool is_loaded) {
  CHECK(export_ != nullptr);
  CHECK(export_->file != nullptr);
  auto file = std::move(export_->file);
  auto &dialog = *export_->dialog;
  CHECK(dialog.file_work_index < dialog.file_works.size());
  auto &target = *dialog.file_works[dialog.file_work_index].target;
  if (is_loaded) {
    save_loaded_file(file->key, file->relative_path);
    target.relative_path = std::move(file->relative_path);
    target.skip_reason = ExportFile::SkipReason::None;
  } else {
    // a partially downloaded file is left in place
    target.skip_reason = ExportFile::SkipReason::Unavailable;
  }
  file = nullptr;
  dialog.file_work_index++;
  load_next_message_file();
}

void ExportManager::save_loaded_file(int32 file_key, const string &relative_path) {
  if (!export_->loaded_files.emplace(file_key, relative_path).second) {
    return;
  }
  export_->loaded_file_keys.push(file_key);
  if (export_->loaded_file_keys.size() > MAX_LOADED_FILES) {
    export_->loaded_files.erase(export_->loaded_file_keys.pop());
  }
}

void ExportManager::write_messages_slice() {
  auto &dialog = *export_->dialog;
  auto is_migrated = dialog.splits[dialog.split_index] < 0;
  if (!on_io_result(export_->writer->write_dialog_messages(dialog.slice, is_migrated, export_->custom_emoji_files))) {
    return;
  }
  dialog.received_message_count += dialog.slice_message_count;
  if (!dialog.info.only_my_messages && dialog.slice_message_count > 0) {
    dialog.has_other_messages = true;
  }
  dialog.slice.clear();
  dialog.slice_message_count = 0;
  dialog.file_works.clear();
  dialog.file_work_index = 0;
  dialog.custom_emoji_stickers.clear();
  set_dialogs_state();
  request_next_messages_slice();
}

void ExportManager::request_next_messages_slice() {
  auto &dialog = *export_->dialog;
  if (dialog.is_last_slice && ++dialog.split_index < dialog.splits.size()) {
    dialog.is_last_slice = false;
    dialog.largest_id_plus_one = 1;
  }
  if (!dialog.is_last_slice) {
    request_messages_slice();
  } else {
    finish_messages();
  }
}

void ExportManager::finish_messages() {
  auto only_my_messages = export_->dialog->info.only_my_messages && !export_->dialog->has_other_messages;
  export_->dialog = nullptr;
  if (!on_io_result(export_->writer->write_dialog_end(only_my_messages))) {
    return;
  }
  export_->dialog_index++;
  export_next_dialog();
}

void ExportManager::set_dialogs_state() {
  CHECK(export_->dialog != nullptr);
  const auto &dialog = *export_->dialog;
  ProcessingState state;
  state.step = Step::Dialogs;
  state.entity_index = narrow_cast<int32>(export_->dialog_index);
  state.entity_count = narrow_cast<int32>(export_->dialogs.size());
  state.entity_name = dialog.info.name;
  state.item_index = dialog.received_message_count;
  state.item_count = max(dialog.message_count, state.item_index);
  if (export_->file != nullptr) {
    const auto &file = *export_->file;
    state.file_id = file.id;
    state.file_name = file.relative_path;
    state.file_size = file.size;
    state.file_downloaded_size = file.downloaded_size;
  }
  set_processing_state(std::move(state));
}

void ExportManager::finish_export() {
  CHECK(export_ != nullptr);
  if (!on_io_result(export_->writer->finish())) {
    return;
  }
  auto promise = PromiseCreator::lambda(
      [actor_id = actor_id(this), generation = export_->generation](Result<bool> result) mutable {
        send_closure(actor_id, &ExportManager::on_finish_takeout_session, generation, std::move(result));
      });
  send_finish_takeout_session(true, std::move(promise));
}

void ExportManager::on_finish_takeout_session(uint64 generation, Result<bool> result) {
  if (export_ == nullptr || export_->generation != generation) {
    return;
  }
  if (result.is_error()) {
    return on_api_error(result.move_as_error());
  }
  auto directory = export_->settings.path;
  stop_export(td_api::make_object<td_api::exportStateFinished>(directory, export_->stats.file_count,
                                                               export_->stats.total_size));
}

void ExportManager::send_finish_takeout_session(bool success, Promise<bool> &&promise) {
  CHECK(export_ != nullptr);
  if (export_->takeout_id == 0 || export_->is_takeout_finished) {
    return promise.set_value(true);
  }
  export_->is_takeout_finished = true;
  send_finish_takeout_session(export_->takeout_id, success, std::move(promise));
}

void ExportManager::send_finish_takeout_session(int64 takeout_id, bool success, Promise<bool> &&promise) {
  // the session stays open and is replaced by the next one
  TRY_STATUS_PROMISE(promise, G()->close_status());
  vector<telegram_api::object_ptr<telegram_api::Function>> prefixes;
  prefixes.push_back(telegram_api::make_object<telegram_api::invokeWithTakeoutPrefix>(takeout_id));
  td_->create_handler<ExportQuery<telegram_api::account_finishTakeoutSession>>(std::move(promise))
      ->send(prefixes, telegram_api::account_finishTakeoutSession(0, success));
}

void ExportManager::on_api_error(Status status) {
  CHECK(export_ != nullptr);
  LOG(INFO) << "Export failed: " << status;
  if (status.code() == 400 && begins_with(status.message(), "TAKEOUT_INIT_DELAY_")) {
    auto delay = to_integer<int32>(status.message().substr(Slice("TAKEOUT_INIT_DELAY_").size()));
    return stop_export(td_api::make_object<td_api::exportStateWaitingForConfirmation>(G()->unix_time() + delay));
  }
  stop_export(td_api::make_object<td_api::exportStateFailed>(
      td_api::make_object<td_api::error>(status.code(), status.message().str())));
}

bool ExportManager::on_io_result(Status status) {
  CHECK(export_ != nullptr);
  if (status.is_ok()) {
    return true;
  }
  LOG(WARNING) << "Export failed: " << status;
  stop_export(
      td_api::make_object<td_api::exportStateFailed>(td_api::make_object<td_api::error>(400, status.message().str())));
  return false;
}

void ExportManager::stop_export(td_api::object_ptr<td_api::ExportState> &&state) {
  CHECK(export_ != nullptr);
  CHECK(state != nullptr);
  CHECK(state->get_id() != td_api::exportStateInProgress::ID);
  send_finish_takeout_session(state->get_id() == td_api::exportStateFinished::ID, Promise<bool>());
  export_ = nullptr;
  send_update_export_state(std::move(state));
}

void ExportManager::set_processing_state(ProcessingState &&state) {
  CHECK(export_ != nullptr);
  if (state.step != export_->last_processing_step) {
    export_->substeps_passed += get_substeps_in_step(export_->last_processing_step);
    export_->last_processing_step = state.step;
  }
  send_update_export_state(get_processing_state_object(state));
  export_->has_processing_state = true;
  export_->processing_state = std::move(state);
}

td_api::object_ptr<td_api::ExportState> ExportManager::get_processing_state_object(const ProcessingState &state) const {
  CHECK(export_ != nullptr);
  double progress = 0.0;
  auto substeps_total = export_->substeps_total;
  if (substeps_total > 0) {
    auto substeps_now = get_substeps_in_step(export_->last_processing_step);
    auto get_part = [&](int32 index, int32 count) {
      return count > 0 ? (static_cast<double>(substeps_now) * index) / (static_cast<double>(substeps_total) * count)
                       : 0.0;
    };
    progress = export_->substeps_passed / static_cast<double>(substeps_total) +
               (state.entity_count == 1 && state.entity_index == 0 ? get_part(state.item_index, state.item_count)
                                                                   : get_part(state.entity_index, state.entity_count));
  }
  td_api::object_ptr<td_api::exportFile> file;
  if (state.file_id != 0) {
    file = td_api::make_object<td_api::exportFile>(state.file_id, state.file_name, state.file_size,
                                                   state.file_downloaded_size);
  }
  return td_api::make_object<td_api::exportStateInProgress>(
      get_export_step_object(state.step), clamp(progress, 0.0, 1.0), state.entity_name, state.entity_index,
      state.entity_count, state.item_index, state.item_count, std::move(file));
}

td_api::object_ptr<td_api::ExportStep> ExportManager::get_export_step_object(Step step) {
  switch (step) {
    case Step::Initializing:
      return td_api::make_object<td_api::exportStepInitializing>();
    case Step::DialogsList:
      return td_api::make_object<td_api::exportStepChatList>();
    case Step::PersonalInfo:
      return td_api::make_object<td_api::exportStepPersonalInformation>();
    case Step::Userpics:
      return td_api::make_object<td_api::exportStepProfilePhotos>();
    case Step::Stories:
      return td_api::make_object<td_api::exportStepStories>();
    case Step::ProfileMusic:
      return td_api::make_object<td_api::exportStepProfileAudios>();
    case Step::Contacts:
      return td_api::make_object<td_api::exportStepContacts>();
    case Step::Sessions:
      return td_api::make_object<td_api::exportStepSessions>();
    case Step::OtherData:
      return td_api::make_object<td_api::exportStepOtherData>();
    case Step::Dialogs:
      return td_api::make_object<td_api::exportStepChats>();
    case Step::Topic:
      return td_api::make_object<td_api::exportStepTopic>();
    default:
      UNREACHABLE();
      return nullptr;
  }
}

void ExportManager::send_update_export_state(td_api::object_ptr<td_api::ExportState> &&state) {
  send_closure(G()->td(), &Td::send_update, td_api::make_object<td_api::updateExportState>(std::move(state)));
}

}  // namespace td
