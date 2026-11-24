//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/ExportSettings.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/actor/actor.h"

#include "td/utils/common.h"
#include "td/utils/Promise.h"
#include "td/utils/Status.h"

namespace td {

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

  struct Export;

  static constexpr int32 MAX_DIRECTORY_ATTEMPTS = 1000;

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

  void finish_export();

  void on_finish_takeout_session(uint64 generation, Result<bool> result);

  void send_finish_takeout_session(bool success, Promise<bool> &&promise);

  void send_finish_takeout_session(int64 takeout_id, bool success, Promise<bool> &&promise);

  void on_api_error(Status status);

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
