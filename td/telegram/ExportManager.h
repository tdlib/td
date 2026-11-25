//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DialogId.h"
#include "td/telegram/ExportSettings.h"
#include "td/telegram/StoryId.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/actor/actor.h"

#include "td/utils/common.h"
#include "td/utils/FlatHashMap.h"
#include "td/utils/Promise.h"
#include "td/utils/Status.h"

namespace td {

struct ExportFile;
struct ExportMessage;
class Td;

class ExportManager final : public Actor {
 public:
  ExportManager(Td *td, ActorShared<> parent);
  ExportManager(const ExportManager &) = delete;
  ExportManager &operator=(const ExportManager &) = delete;
  ExportManager(ExportManager &&) = delete;
  ExportManager &operator=(ExportManager &&) = delete;
  ~ExportManager() final;

  void start_export(td_api::object_ptr<td_api::ExportSource> &&source,
                    vector<td_api::object_ptr<td_api::ExportFormat>> &&formats,
                    td_api::object_ptr<td_api::exportMediaSettings> &&media_settings, const string &directory,
                    Promise<Unit> &&promise);

  void skip_export_file(int64 file_id, Promise<Unit> &&promise);

  void cancel_export(Promise<Unit> &&promise);

  void get_current_state(vector<td_api::object_ptr<td_api::Update>> &updates) const;

 private:
  enum class Step : int32 {
    Initializing,
    DialogsList,
    PersonalInfo,
    Userpics,
    Stories,
    ProfileMusic,
    Contacts,
    Sessions,
    OtherData,
    Dialogs,
    Topic,
    Count
  };

  struct ProcessingState {
    Step step = Step::Initializing;
    string entity_name;
    int32 entity_index = 0;
    int32 entity_count = 0;
    int32 item_index = 0;
    int32 item_count = 0;
    int64 file_id = 0;
    string file_name;
    int64 file_size = 0;
    int64 file_downloaded_size = 0;
  };

  enum class MessagesRequestType : int32 { Count, FirstDate, Slice, TopicRoot };

  struct MessageFileWork;
  struct FileCounters;
  struct ListProcess;
  struct DialogProcess;
  struct DialogsList;
  struct FileProcess;
  struct Export;

  static constexpr int32 MAX_DIRECTORY_ATTEMPTS = 1000;
  static constexpr int32 MESSAGES_SLICE_LIMIT = 100;
  static constexpr int32 DIALOGS_SLICE_LIMIT = 100;
  static constexpr int32 USERPICS_SLICE_LIMIT = 100;
  static constexpr int32 STORIES_SLICE_LIMIT = 100;
  static constexpr int32 PROFILE_AUDIOS_SLICE_LIMIT = 100;
  static constexpr int32 TOP_PEERS_SLICE_LIMIT = 100;
  static constexpr int32 FILE_PART_SIZE = 128 << 10;
  static constexpr size_t MAX_LOADED_FILES = 100000;

  void tear_down() final;

  Result<ExportSettings> get_export_settings(td_api::object_ptr<td_api::ExportSource> &&source,
                                             vector<td_api::object_ptr<td_api::ExportFormat>> &&formats,
                                             td_api::object_ptr<td_api::exportMediaSettings> &&media_settings,
                                             const string &directory) const;

  string get_local_date_string() const;

  Result<string> create_export_directory(const string &directory, bool only_single_dialog) const;

  void fill_export_steps();

  void fill_substeps_in_steps();

  int32 get_substeps_in_step(Step step) const;

  void export_next();

  void initialize();

  void on_init_takeout_session(uint64 generation,
                               Result<telegram_api::object_ptr<telegram_api::account_takeout>> r_takeout);

  bool is_current_export(uint64 generation) const;

  template <class FunctionT>
  void send_export_query(const FunctionT &function, int32 split_index,
                         Promise<typename FunctionT::ReturnType> &&promise);

  void request_split_ranges();

  void on_get_split_ranges(uint64 generation,
                           Result<vector<telegram_api::object_ptr<telegram_api::messageRange>>> r_ranges);

  void request_dialogs_count(size_t split_index);

  void on_get_dialogs_count(uint64 generation, size_t split_index,
                            Result<telegram_api::object_ptr<telegram_api::messages_Dialogs>> r_dialogs);

  void request_left_channels(bool is_count);

  void on_get_left_channels(uint64 generation, bool is_count,
                            Result<telegram_api::object_ptr<telegram_api::messages_Chats>> r_chats);

  void append_dialogs(vector<unique_ptr<DialogProcess>> &dialogs,
                      FlatHashMap<DialogId, size_t, DialogIdHash> &dialog_indices, const vector<DialogId> &dialog_ids,
                      bool is_left_channel, int32 split_index);

  void start_writer();

  void set_step_state(Step step);

  void export_personal_info();

  void on_get_personal_info(uint64 generation,
                            Result<telegram_api::object_ptr<telegram_api::users_userFull>> r_user_full);

  void export_contacts();

  void on_get_saved_contacts(uint64 generation,
                             Result<vector<telegram_api::object_ptr<telegram_api::savedPhoneContact>>> r_contacts);

  void request_top_peers();

  void on_get_top_peers(uint64 generation,
                        Result<telegram_api::object_ptr<telegram_api::contacts_TopPeers>> r_top_peers);

  void export_sessions();

  void on_get_authorizations(uint64 generation,
                             Result<telegram_api::object_ptr<telegram_api::account_authorizations>> r_authorizations);

  void on_get_web_authorizations(
      uint64 generation, Result<telegram_api::object_ptr<telegram_api::account_webAuthorizations>> r_authorizations);

  void export_other_data();

  void on_other_data_loaded();

  void add_list_file_work(ExportFile &target, const td_api::file *file, string suggested_path, int32 media_type,
                          int64 controlling_size, int32 item_index, StoryId story_id);

  void start_list();

  bool on_list_count(int32 count);

  void export_userpics();

  void request_userpics_slice();

  void on_get_userpics_slice(uint64 generation, Result<telegram_api::object_ptr<telegram_api::photos_Photos>> r_photos);

  void export_stories();

  void request_stories_slice();

  void on_get_stories_slice(uint64 generation,
                            Result<telegram_api::object_ptr<telegram_api::stories_stories>> r_stories);

  void export_profile_audios();

  void request_profile_audios_slice();

  void on_get_profile_audios_slice(uint64 generation,
                                   Result<telegram_api::object_ptr<telegram_api::users_SavedMusic>> r_saved_music);

  void load_next_list_file();

  void write_list_slice();

  void set_list_state();

  void refresh_userpic_file_reference();

  void on_refresh_userpic_file_reference(uint64 generation, int64 file_id,
                                         Result<telegram_api::object_ptr<telegram_api::photos_Photos>> r_photos);

  void refresh_profile_audio_file_reference();

  void on_refresh_profile_audio_file_reference(
      uint64 generation, int64 file_id, Result<telegram_api::object_ptr<telegram_api::users_SavedMusic>> r_music);

  void on_refresh_story_file_reference(uint64 generation, int64 file_id,
                                       Result<telegram_api::object_ptr<telegram_api::stories_stories>> r_stories);

  void collect_dialogs_list();

  void request_dialogs_slice();

  void on_get_dialogs_slice(uint64 generation,
                            Result<telegram_api::object_ptr<telegram_api::messages_Dialogs>> r_dialogs);

  void finish_dialogs_list();

  void on_load_single_dialog_full_info(uint64 generation, Result<Unit> result);

  void set_dialogs_list_state(int32 processed);

  void add_single_dialog();

  void export_topic();

  void on_get_topic(uint64 generation, Result<td_api::object_ptr<td_api::forumTopic>> r_topic);

  void export_dialogs();

  void export_next_dialog();

  void request_messages_count(size_t split_index);

  void request_chat_messages(MessagesRequestType type, size_t split_index, int32 offset_id, int32 add_offset,
                             int32 limit);

  void on_get_chat_messages(uint64 generation, MessagesRequestType type, size_t split_index, int32 offset_id,
                            int32 add_offset, int32 limit,
                            Result<telegram_api::object_ptr<telegram_api::messages_Messages>> r_messages);

  void check_first_message_date(size_t split_index, int32 count);

  void on_messages_count_loaded(size_t split_index, int32 count);

  void request_messages_slice();

  void on_get_messages_slice(vector<telegram_api::object_ptr<telegram_api::Message>> &&messages);

  void add_message_file_work(ExportMessage &export_message);

  string get_file_date_part(int32 date) const;

  // the path is in the folder of the base path
  string get_document_path(Slice base_path, FileCounters &counters, int32 media_type, Slice file_name, Slice mime_type,
                           Slice date_part);

  void on_get_custom_emoji_stickers(uint64 generation, Result<td_api::object_ptr<td_api::stickers>> r_stickers);

  void load_next_message_file();

  bool process_message_file(MessageFileWork &work);

  string get_unique_relative_path(const string &suggested_path) const;

  string get_file_path(Slice relative_path) const;

  void set_file_state();

  void load_file_part();

  void on_get_file_part(uint64 generation, int64 file_id, int64 offset,
                        Result<telegram_api::object_ptr<telegram_api::upload_File>> r_file);

  void refresh_file_reference();

  void on_refresh_file_reference(uint64 generation, int64 file_id,
                                 Result<telegram_api::object_ptr<telegram_api::messages_Messages>> r_messages);

  void on_refresh_custom_emoji_file_reference(
      uint64 generation, int64 file_id, Result<vector<telegram_api::object_ptr<telegram_api::Document>>> r_documents);

  void finish_file(bool is_loaded);

  void save_loaded_file(int32 file_key, const string &relative_path);

  void write_messages_slice();

  void request_next_messages_slice();

  void finish_messages();

  void set_dialogs_state();

  void finish_export();

  void on_finish_takeout_session(uint64 generation, Result<bool> result);

  void send_finish_takeout_session(bool success, Promise<bool> &&promise);

  void send_finish_takeout_session(int64 takeout_id, bool success, Promise<bool> &&promise);

  void on_api_error(Status status);

  bool on_io_result(Status status);

  void stop_export(td_api::object_ptr<td_api::ExportState> &&state);

  void set_processing_state(ProcessingState &&state);

  td_api::object_ptr<td_api::ExportState> get_processing_state_object(const ProcessingState &state) const;

  static td_api::object_ptr<td_api::ExportStep> get_export_step_object(Step step);

  void send_update_export_state(td_api::object_ptr<td_api::ExportState> &&state);

  Td *td_;
  ActorShared<> parent_;

  uint64 current_generation_ = 0;
  unique_ptr<Export> export_;
};

}  // namespace td
