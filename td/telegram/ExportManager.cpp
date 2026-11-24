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
#include "td/telegram/DialogManager.h"
#include "td/telegram/ExportWriter.h"
#include "td/telegram/ForumTopicId.h"
#include "td/telegram/Global.h"
#include "td/telegram/MessageId.h"
#include "td/telegram/MessagesManager.h"
#include "td/telegram/MessageTopic.h"
#include "td/telegram/net/DcId.h"
#include "td/telegram/net/NetQueryCreator.h"
#include "td/telegram/OptionManager.h"
#include "td/telegram/Td.h"
#include "td/telegram/UserId.h"
#include "td/telegram/UserManager.h"

#include "td/utils/buffer.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/port/path.h"
#include "td/utils/port/Stat.h"
#include "td/utils/SliceBuilder.h"

#include <limits>

namespace td {

template <class FunctionT>
class ExportQuery final : public Td::ResultHandler {
  Promise<typename FunctionT::ReturnType> promise_;

 public:
  explicit ExportQuery(Promise<typename FunctionT::ReturnType> &&promise) : promise_(std::move(promise)) {
  }

  void send(const vector<telegram_api::object_ptr<telegram_api::Function>> &prefixes, const FunctionT &function) {
    send_query(G()->net_query_creator().create_with_prefixes(prefixes, function, DcId::main()));
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

static int32 get_single_message_date(const telegram_api::object_ptr<telegram_api::messages_Messages> &messages) {
  const vector<telegram_api::object_ptr<telegram_api::Message>> *message_list = nullptr;
  switch (messages->get_id()) {
    case telegram_api::messages_messages::ID:
      message_list = &static_cast<const telegram_api::messages_messages *>(messages.get())->messages_;
      break;
    case telegram_api::messages_messagesSlice::ID:
      message_list = &static_cast<const telegram_api::messages_messagesSlice *>(messages.get())->messages_;
      break;
    case telegram_api::messages_channelMessages::ID:
      message_list = &static_cast<const telegram_api::messages_channelMessages *>(messages.get())->messages_;
      break;
    default:
      UNREACHABLE();
  }
  if (message_list->empty()) {
    return 0;
  }
  return get_message_date((*message_list)[0].get());
}

struct ExportManager::DialogProcess {
  ExportDialogInfo info;
  DialogId migrated_from_dialog_id;
  bool only_my_messages = false;

  // indices of the message ranges to export; negative ones are of the basic group the chat was upgraded from
  vector<int32> splits;
  vector<int32> split_message_counts;

  size_t split_index = 0;
  int32 largest_id_plus_one = 1;
  int32 pending_count = 0;
  bool is_last_slice = false;

  int32 received_message_count = 0;
  int32 message_count = 0;
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

  unique_ptr<DialogProcess> dialog;
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
      return Status::Error(400, "Account export isn't supported yet");

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

  bool is_json = false;
  for (auto &format : formats) {
    if (format == nullptr) {
      return Status::Error(400, "Export format must be non-empty");
    }
    bool is_repeated = false;
    switch (format->get_id()) {
      case td_api::exportFormatHtml::ID:
        return Status::Error(400, "HTML export isn't supported yet");
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
  if (!is_json) {
    return Status::Error(400, "Export formats must be non-empty");
  }
  settings.format = ExportFormat::Json;

  if (media_settings != nullptr) {
    return Status::Error(400, "Media export isn't supported yet");
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
  CHECK(settings.only_single_dialog());
  export_->dialogs_count =
      settings.single_dialog_id.get_type() == DialogType::Channel ? 1 : narrow_cast<int32>(splits.size());
  start_writer();
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
  CHECK(export_->settings.only_single_dialog());
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
  dialog->only_my_messages =
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
  dialog->split_message_counts.resize(dialog->splits.size());
  export_->dialog = std::move(dialog);
  export_next();
}

void ExportManager::export_dialogs() {
  CHECK(export_->dialog != nullptr);
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
  auto input_peer = td_->dialog_manager_->get_input_peer(dialog_id, AccessRights::Read);
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
  if (dialog.only_my_messages) {
    telegram_api::object_ptr<telegram_api::InputPeer> from;
    if (dialog_id.get_type() == DialogType::Channel &&
        td_->chat_manager_->is_admined_monoforum_channel(dialog_id.get_channel_id())) {
      // own messages in an administered monoforum are sent on behalf of its channel
      from = td_->dialog_manager_->get_input_peer(
          DialogId(td_->chat_manager_->get_monoforum_channel_id(dialog_id.get_channel_id())), AccessRights::Read);
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
    if (r_messages.error().message() == "CHANNEL_PRIVATE" && !dialog.only_my_messages &&
        dialog_id.get_type() == DialogType::Channel) {
      // the chat was left or the user was banned from it, so only own messages can be exported
      dialog.only_my_messages = true;
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
  } else {
    auto max_date = export_->settings.single_dialog_max_date;
    if (max_date > 0 && get_message_date(messages[0].get()) >= max_date) {
      // the newest message of the slice is after the range, so the next slices are too
      dialog.is_last_slice = true;
    }

    auto is_migrated = dialog.splits[dialog.split_index] < 0;
    auto dialog_id = is_migrated ? dialog.migrated_from_dialog_id : dialog.info.dialog_id;
    vector<ExportMessage> export_messages;
    export_messages.reserve(messages.size());
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
        export_messages.push_back(std::move(export_message));
      }
    }
    if (!on_io_result(export_->writer->write_dialog_messages(export_messages, is_migrated))) {
      return;
    }
    dialog.received_message_count += narrow_cast<int32>(messages.size());
    set_dialogs_state();
  }
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
  export_->dialog = nullptr;
  if (!on_io_result(export_->writer->write_dialog_end())) {
    return;
  }
  export_next();
}

void ExportManager::set_dialogs_state() {
  CHECK(export_->dialog != nullptr);
  const auto &dialog = *export_->dialog;
  ProcessingState state;
  state.step = Step::Dialogs;
  state.entity_index = 0;
  state.entity_count = 1;
  state.entity_name = dialog.info.name;
  state.item_index = dialog.received_message_count;
  state.item_count = max(dialog.message_count, state.item_index);
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
