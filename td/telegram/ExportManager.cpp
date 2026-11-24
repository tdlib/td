//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/ExportManager.h"

#include "td/telegram/AccessRights.h"
#include "td/telegram/DialogManager.h"
#include "td/telegram/ForumTopicId.h"
#include "td/telegram/Global.h"
#include "td/telegram/MessageTopic.h"
#include "td/telegram/net/DcId.h"
#include "td/telegram/net/NetQueryCreator.h"
#include "td/telegram/OptionManager.h"
#include "td/telegram/Td.h"

#include "td/utils/buffer.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/port/path.h"
#include "td/utils/port/Stat.h"
#include "td/utils/SliceBuilder.h"

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

struct ExportManager::Export {
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
  settings.directory = directory;
  return std::move(settings);
}

string ExportManager::get_local_date_string() const {
  auto local_time = static_cast<int64>(G()->unix_time()) + td_->option_manager_->get_option_integer("utc_time_offset");
  auto days = local_time / 86400 - (local_time % 86400 < 0 ? 1 : 0);

  // converts days since 1970-01-01 to a civil date in the proleptic Gregorian calendar
  days += 719468;
  auto era = (days >= 0 ? days : days - 146096) / 146097;
  auto day_of_era = days - era * 146097;
  auto year_of_era = (day_of_era - day_of_era / 1460 + day_of_era / 36524 - day_of_era / 146096) / 365;
  auto day_of_year = day_of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
  auto shifted_month = (5 * day_of_year + 2) / 153;
  auto day = day_of_year - (153 * shifted_month + 2) / 5 + 1;
  auto month = shifted_month < 10 ? shifted_month + 3 : shifted_month - 9;
  auto year = year_of_era + era * 400 + (month <= 2 ? 1 : 0);

  return PSTRING() << year << '-' << (month < 10 ? "0" : "") << month << '-' << (day < 10 ? "0" : "") << day;
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
    substeps[static_cast<size_t>(step)] = 1;
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
    case Step::Dialogs:
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

  fill_substeps_in_steps();
  export_next();
}

void ExportManager::finish_export() {
  CHECK(export_ != nullptr);
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
  stop_export(td_api::make_object<td_api::exportStateFinished>(directory, 0, 0));
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
