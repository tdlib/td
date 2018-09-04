//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/ClientActor.h"
#include "td/telegram/Log.h"

#include "td/telegram/td_api_json.h"

#include "td/actor/actor.h"

#include "td/tl/tl_json.h"  // should be included after td_api_json?

#include "memprof/memprof.h"

#include "td/net/HttpQuery.h"
#include "td/net/HttpReader.h"

#include "td/utils/base64.h"
#include "td/utils/buffer.h"
#include "td/utils/BufferedFd.h"
#include "td/utils/FileLog.h"
#include "td/utils/format.h"
#include "td/utils/JsonBuilder.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/port/Fd.h"
#include "td/utils/port/FileFd.h"
#include "td/utils/port/signals.h"
#include "td/utils/port/Stat.h"
#include "td/utils/port/thread_local.h"
#include "td/utils/ScopeGuard.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"
#include "td/utils/StringBuilder.h"
#include "td/utils/Time.h"

#ifndef USE_READLINE
#include "td/utils/find_boundary.h"
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <clocale>
#include <cstdlib>
#include <cstring>  // for strcmp
#include <ctime>
#include <iostream>
#include <limits>
#include <locale>
#include <memory>
#include <queue>
#include <tuple>
#include <unordered_map>

#ifdef USE_READLINE
/* Standard readline include files. */
#include <readline/history.h>
#include <readline/readline.h>
#endif

namespace td {

static void dump_memory_usage() {
  if (is_memprof_on()) {
    LOG(WARNING) << "memory_dump";
    clear_thread_locals();
    std::vector<AllocInfo> v;
    dump_alloc([&](const AllocInfo &info) { v.push_back(info); });
    std::sort(v.begin(), v.end(), [](const AllocInfo &a, const AllocInfo &b) { return a.size > b.size; });
    size_t total_size = 0;
    size_t other_size = 0;
    int cnt = 0;
    for (auto &info : v) {
      if (cnt++ < 50) {
        LOG(WARNING) << td::format::as_size(info.size) << td::format::as_array(info.backtrace);
      } else {
        other_size += info.size;
      }
      total_size += info.size;
    }
    LOG(WARNING) << tag("other", td::format::as_size(other_size));
    LOG(WARNING) << tag("total", td::format::as_size(total_size));
    LOG(WARNING) << tag("total traces", get_ht_size());
    LOG(WARNING) << tag("fast_backtrace_success_rate", get_fast_backtrace_success_rate());
  }
}

#ifdef USE_READLINE
const char *prompt = "td_cli> ";
static int32 saved_point;
static string saved_line;
static std::atomic_flag readline_lock = ATOMIC_FLAG_INIT;

static void deactivate_readline() {
  while (readline_lock.test_and_set(std::memory_order_acquire)) {
    // spin
  }

  saved_point = rl_point;
  saved_line = string(rl_line_buffer, rl_end);

  rl_set_prompt("");
  rl_replace_line("", 0);
  rl_redisplay();
}

static void reactivate_readline() {
  rl_set_prompt(prompt);
  rl_replace_line(saved_line.c_str(), 0);
  rl_point = saved_point;
  rl_redisplay();

  readline_lock.clear(std::memory_order_release);
}

static char *command_generator(const char *text, int state) {
  static const vector<CSlice> commands{"GetChats",
                                       "GetHistory",
                                       "SetVerbosity",
                                       "SendVideo",
                                       "SearchDocument",
                                       "GetChatMember",
                                       "GetSupergroupAdministrators",
                                       "GetSupergroupBanned",
                                       "GetSupergroupMembers",
                                       "GetFile",
                                       "DownloadFile",
                                       "CancelDownloadFile",
                                       "ImportContacts",
                                       "RemoveContacts",
                                       "DumpNetQueries",
                                       "CreateSecretChat",
                                       "CreateNewSecretChat"};
  static size_t cmd_i;
  if (state == 0) {
    cmd_i = 0;
  }

  while (cmd_i < commands.size()) {
    const char *a = commands[cmd_i++].c_str();
    const char *b = text;
    const char *c = b;
    while (*c && to_lower(*c) == *c) {
      c++;
    }
    bool only_lowercase = !is_alpha(*c);
    while (*a && *b) {
      if (*a == *b || (only_lowercase && *a == to_upper(*b))) {
        b++;
      }
      a++;
    }
    if (*b == 0) {
// TODO call to strdup is completely wrong. Readline will try to call std::free() on the returned char*,
// which may be incompatible with the std::malloc() called by strdup
// It is especially likely to happen if Readline is used as dynamic library
// Unfortunately Readline doesn't provide memory allocation functions/memory deallocation callbacks to fix this
#if TD_MSVC
      return _strdup(commands[cmd_i - 1].c_str());
#else
      return strdup(commands[cmd_i - 1].c_str());
#endif
    }
  }
  return nullptr;
}

static char **tg_cli_completion(const char *text, int start, int end) {
  char **matches = nullptr;
  if (start == 0) {
    matches = rl_completion_matches(text, command_generator);
  }
  return matches;
}
#endif

class CliLog : public LogInterface {
 public:
  void append(CSlice slice, int log_level) override {
#ifdef USE_READLINE
    deactivate_readline();
#endif
    if (log_level == VERBOSITY_NAME(PLAIN)) {
#if TD_WINDOWS
      TsCerr() << slice;
#else
      TsCerr() << TC_GREEN << slice << TC_EMPTY;
#endif
    } else {
      default_log_interface->append(slice, log_level);
    }
#ifdef USE_READLINE
    reactivate_readline();
#endif
  }
  void rotate() override {
  }
};

struct SendMessageInfo {
  double start_time = 0;
  double quick_ack_time = 0;
  double ack_time = 0;
  bool empty() const {
    return quick_ack_time != 0 || ack_time != 0;
  }
};

StringBuilder &operator<<(StringBuilder &sb, const SendMessageInfo &info) {
  sb << format::cond(info.quick_ack_time != 0, tag("quick_ack", info.quick_ack_time - info.start_time));
  sb << format::cond(info.ack_time != 0, tag("ack", info.ack_time - info.start_time));
  return sb;
}

class CliClient final : public Actor {
 public:
  CliClient(bool use_test_dc, bool get_chat_list, bool disable_network, int32 api_id, string api_hash)
      : use_test_dc_(use_test_dc)
      , get_chat_list_(get_chat_list)
      , disable_network_(disable_network)
      , api_id_(api_id)
      , api_hash_(api_hash) {
  }

  static void quit_instance() {
    instance_->quit();
  }

 private:
  void start_up() override {
    yield();
  }

  std::unordered_map<uint64, SendMessageInfo> query_id_to_send_message_info_;
  std::unordered_map<uint64, SendMessageInfo> message_id_to_send_message_info_;

  struct User {
    string first_name;
    string last_name;
    string username;
  };

  std::unordered_map<int32, User> users_;
  std::unordered_map<string, int32> username_to_user_id_;

  void register_user(const td_api::user &user) {
    User &new_user = users_[user.id_];
    new_user.first_name = user.first_name_;
    new_user.last_name = user.last_name_;
    new_user.username = user.username_;
    username_to_user_id_[to_lower(new_user.username)] = user.id_;
  }

  void print_user(Logger &log, int32 user_id, bool full = false) {
    const User *user = &users_[user_id];
    log << user->first_name << " " << user->last_name << " #" << user_id;
    if (!user->username.empty()) {
      log << " @" << user->username;
    }
  }

  void update_users(const td_api::users &users) {
    Logger log{*log_interface, VERBOSITY_NAME(PLAIN)};
    for (auto &user_id : users.user_ids_) {
      if (user_id == 0) {
        continue;
      }
      print_user(log, user_id);
      log << "\n";
    }
  }

  std::unordered_map<string, int32> username_to_supergroup_id_;
  void register_supergroup(const td_api::supergroup &supergroup) {
    if (!supergroup.username_.empty()) {
      username_to_supergroup_id_[to_lower(supergroup.username_)] = supergroup.id_;
    }
  }

  void update_option(const td_api::updateOption &option) {
    if (option.name_ == "my_id") {
      if (option.value_->get_id() == td_api::optionValueInteger::ID) {
        my_id_ = static_cast<const td_api::optionValueInteger *>(option.value_.get())->value_;
        LOG(INFO) << "Set my id to " << my_id_;
      }
    }
  }

  int64 get_history_chat_id_ = 0;
  int64 search_chat_id_ = 0;
  void on_get_messages(const td_api::messages &messages) {
    if (get_history_chat_id_ != 0) {
      int64 last_message_id = 0;
      for (auto &m : messages.messages_) {
        // LOG(PLAIN) << to_string(m);
        if (m->content_->get_id() == td_api::messageText::ID) {
          LOG(PLAIN) << td::oneline(static_cast<const td_api::messageText *>(m->content_.get())->text_->text_) << "\n";
        }
        last_message_id = m->id_;
      }

      if (last_message_id > 0) {
        send_request(make_tl_object<td_api::getChatHistory>(get_history_chat_id_, last_message_id, 0, 100, false));
      } else {
        get_history_chat_id_ = 0;
      }
    }
    if (search_chat_id_ != 0) {
      if (!messages.messages_.empty()) {
        auto last_message_id = messages.messages_.back()->id_;
        LOG(ERROR) << (last_message_id >> 20);
        send_request(
            make_tl_object<td_api::searchChatMessages>(search_chat_id_, "", 0, last_message_id, 0, 100,
                                                       make_tl_object<td_api::searchMessagesFilterPhotoAndVideo>()));
      } else {
        search_chat_id_ = 0;
      }
    }
  }

  void on_get_message(const td_api::message &message) {
    if (message.sending_state_ != nullptr &&
        message.sending_state_->get_id() == td_api::messageSendingStatePending::ID) {
      // send_request(make_tl_object<td_api::deleteMessages>(message.chat_id_, vector<int64>{message.id_}, true));
    }
  }

  void on_get_file(const td_api::file &file) {
    if (being_downloaded_files_.count(file.id_) == 0 && file.local_->is_downloading_active_) {
      being_downloaded_files_[file.id_] = Time::now();
    }

    if (being_downloaded_files_.count(file.id_) != 0 && !file.local_->is_downloading_active_) {
      double elapsed_time = Time::now() - being_downloaded_files_[file.id_];
      being_downloaded_files_.erase(file.id_);
      if (file.local_->is_downloading_completed_) {
        LOG(ERROR) << "File " << file.id_ << " was downloaded in " << elapsed_time << " seconds";
      } else {
        LOG(ERROR) << "File " << file.id_ << " has failed to download in " << elapsed_time << " seconds";
      }
    }
  }

  struct FileGeneration {
    int64 id = 0;
    string destination;
    string source;
    int32 part_size = 0;
    int32 local_size = 0;
    int32 size = 0;
  };

  vector<FileGeneration> pending_file_generations_;

  void on_file_generation_start(const td_api::updateFileGenerationStart &update) {
    FileGeneration file_generation;
    file_generation.id = update.generation_id_;
    file_generation.destination = update.destination_path_;
    if (update.conversion_ == "#url#") {
      // TODO: actually download
      file_generation.source = "test.jpg";
      file_generation.part_size = 1000000;
    } else if (update.conversion_ == "skip") {
      return;
    } else {
      file_generation.source = update.original_path_;
      file_generation.part_size = to_integer<int32>(update.conversion_);
    }

    auto r_stat = stat(file_generation.source);
    if (r_stat.is_ok()) {
      auto size = r_stat.ok().size_;
      if (size <= 0 || size > 1500000000) {
        r_stat = Status::Error(400, size == 0 ? Slice("File is empty") : Slice("File is too big"));
      }
    }
    if (r_stat.is_ok()) {
      file_generation.size = narrow_cast<int32>(r_stat.ok().size_);
      if (file_generation.part_size <= 0) {
        file_generation.part_size = file_generation.size;
      }
      pending_file_generations_.push_back(std::move(file_generation));
      timeout_expired();
    } else {
      send_request(make_tl_object<td_api::finishFileGeneration>(
          update.generation_id_, td_api::make_object<td_api::error>(400, r_stat.error().message().str())));
    }
  }

  int64 as_chat_id(Slice str) const {
    str = trim(str);
    if (str[0] == '@') {
      auto it = username_to_user_id_.find(to_lower(str.substr(1)));
      if (it != username_to_user_id_.end()) {
        return it->second;
      }
      auto it2 = username_to_supergroup_id_.find(to_lower(str.substr(1)));
      if (it2 != username_to_supergroup_id_.end()) {
        auto supergroup_id = it2->second;
        return static_cast<int64>(-1000'000'000'000ll) - supergroup_id;
      }
      LOG(ERROR) << "Can't resolve " << str;
      return 0;
    }
    if (str == "me") {
      return my_id_;
    }
    return to_integer<int64>(str);
  }

  vector<int64> as_chat_ids(Slice chat_ids, char delimiter = ' ') const {
    return transform(full_split(chat_ids, delimiter), [this](Slice str) { return as_chat_id(str); });
  }

  static int64 as_message_id(Slice str) {
    str = trim(str);
    if (!str.empty() && str.back() == 's') {
      return to_integer<int64>(str) << 20;
    }
    return to_integer<int64>(str);
  }

  static vector<int64> as_message_ids(Slice message_ids, char delimiter = ' ') {
    return transform(full_split(message_ids, delimiter), as_message_id);
  }

  int32 as_user_id(Slice str) const {
    str = trim(str);
    if (str[0] == '@') {
      auto it = username_to_user_id_.find(to_lower(str.substr(1)));
      if (it != username_to_user_id_.end()) {
        return it->second;
      }
      LOG(ERROR) << "Can't find user " << str;
      return 0;
    }
    if (str == "me") {
      return my_id_;
    }
    return to_integer<int32>(str);
  }

  vector<int32> as_user_ids(Slice user_ids, char delimiter = ' ') const {
    return transform(full_split(user_ids, delimiter), [this](Slice str) { return as_user_id(str); });
  }

  static int32 as_file_id(string str) {
    return to_integer<int32>(trim(std::move(str)));
  }

  static td_api::object_ptr<td_api::InputFile> as_input_file_id(string str) {
    return make_tl_object<td_api::inputFileId>(as_file_id(str));
  }

  static tl_object_ptr<td_api::InputFile> as_local_file(string path) {
    return make_tl_object<td_api::inputFileLocal>(trim(std::move(path)));
  }

  static tl_object_ptr<td_api::InputFile> as_remote_file(string id) {
    return make_tl_object<td_api::inputFileRemote>(trim(std::move(id)));
  }

  static tl_object_ptr<td_api::InputFile> as_generated_file(string original_path, string conversion,
                                                            int32 expected_size = 0) {
    return make_tl_object<td_api::inputFileGenerated>(trim(original_path), trim(conversion), expected_size);
  }

  static tl_object_ptr<td_api::InputFile> as_input_file(string str) {
    if ((str.size() >= 20 && is_base64(str)) || begins_with(str, "http")) {
      return as_remote_file(str);
    }
    auto r_id = to_integer_safe<int32>(trim(str));
    if (r_id.is_ok()) {
      return as_input_file_id(str);
    }
    if (str.find(';') < str.size()) {
      auto res = split(str, ';');
      return as_generated_file(res.first, res.second);
    }
    return as_local_file(str);
  }

  static tl_object_ptr<td_api::inputThumbnail> as_input_thumbnail(tl_object_ptr<td_api::InputFile> input_file,
                                                                  int32 width = 0, int32 height = 0) {
    return td_api::make_object<td_api::inputThumbnail>(std::move(input_file), width, height);
  }

  static int32 as_call_id(string str) {
    return to_integer<int32>(trim(std::move(str)));
  }

  static int32 as_proxy_id(string str) {
    return to_integer<int32>(trim(std::move(str)));
  }

  static tl_object_ptr<td_api::location> as_location(string latitude, string longitude) {
    return make_tl_object<td_api::location>(to_double(latitude), to_double(longitude));
  }

  static bool as_bool(string str) {
    str = to_lower(str);
    return str == "true" || str == "1";
  }

  template <class T>
  static vector<T> to_integers(Slice ids_string, char delimiter = ' ') {
    return transform(full_split(ids_string, delimiter), to_integer<T>);
  }

  void on_result(uint64 id, tl_object_ptr<td_api::Object> result) {
    if (id > 0 && GET_VERBOSITY_LEVEL() < VERBOSITY_NAME(td_requests)) {
      LOG(ERROR) << "on_result [id=" << id << "] " << to_string(result);
    }

    auto as_json_str = json_encode<std::string>(ToJson(result));
    // LOG(INFO) << "on_result [id=" << id << "] " << as_json_str;
    auto copy_as_json_str = as_json_str;
    auto as_json_value = json_decode(copy_as_json_str).move_as_ok();
    td_api::object_ptr<td_api::Object> object;
    from_json(object, as_json_value).ensure();
    CHECK(object != nullptr);
    auto as_json_str2 = json_encode<std::string>(ToJson(object));
    CHECK(as_json_str == as_json_str2) << "\n" << tag("a", as_json_str) << "\n" << tag("b", as_json_str2);
    // LOG(INFO) << "on_result [id=" << id << "] " << as_json_str;

    int32 result_id = result == nullptr ? 0 : result->get_id();

    [&] {
      if (id != 0) {
        auto it = query_id_to_send_message_info_.find(id);
        if (it == query_id_to_send_message_info_.end()) {
          return;
        }
        auto info = it->second;
        query_id_to_send_message_info_.erase(id);

        if (result_id == td_api::message::ID) {
          auto *message = static_cast<const td_api::message *>(result.get());
          message_id_to_send_message_info_[message->id_] = info;
        }
      }
    }();
    [&] {
      if (result_id == td_api::updateMessageSendAcknowledged::ID) {
        auto *message = static_cast<const td_api::updateMessageSendAcknowledged *>(result.get());
        auto it = message_id_to_send_message_info_.find(message->message_id_);
        if (it == message_id_to_send_message_info_.end()) {
          return;
        }
        auto &info = it->second;
        info.quick_ack_time = Time::now();
      }
    }();
    [&] {
      if (result_id == td_api::updateMessageSendSucceeded::ID) {
        auto *message = static_cast<const td_api::updateMessageSendSucceeded *>(result.get());
        auto it = message_id_to_send_message_info_.find(message->old_message_id_);
        if (it == message_id_to_send_message_info_.end()) {
          return;
        }
        auto info = it->second;
        message_id_to_send_message_info_.erase(it);
        info.ack_time = Time::now();
        LOG(INFO) << info;
      }
    }();

    switch (result_id) {
      case td_api::updateUser::ID:
        register_user(*static_cast<const td_api::updateUser *>(result.get())->user_);
        break;
      case td_api::updateSupergroup::ID:
        register_supergroup(*static_cast<const td_api::updateSupergroup *>(result.get())->supergroup_);
        break;
      case td_api::users::ID:
        update_users(*static_cast<const td_api::users *>(result.get()));
        break;
      case td_api::updateOption::ID:
        update_option(*static_cast<const td_api::updateOption *>(result.get()));
        break;
      case td_api::message::ID:
        on_get_message(*static_cast<const td_api::message *>(result.get()));
        break;
      case td_api::messages::ID:
        on_get_messages(*static_cast<const td_api::messages *>(result.get()));
        break;
      case td_api::updateFileGenerationStart::ID:
        on_file_generation_start(*static_cast<const td_api::updateFileGenerationStart *>(result.get()));
        break;
      case td_api::updateChatLastMessage::ID: {
        auto message = static_cast<const td_api::updateChatLastMessage *>(result.get())->last_message_.get();
        if (message != nullptr && message->content_->get_id() == td_api::messageText::ID) {
          // auto text = static_cast<const td_api::messageText *>(message->content_.get())->text_->text_;
        }
        break;
      }
      case td_api::file::ID:
        on_get_file(*static_cast<const td_api::file *>(result.get()));
        break;
      case td_api::updateFile::ID:
        on_get_file(*static_cast<const td_api::updateFile *>(result.get())->file_);
        break;
    }
  }

  void on_error(uint64 id, tl_object_ptr<td_api::error> error) {
    if (id > 0 && GET_VERBOSITY_LEVEL() < VERBOSITY_NAME(td_requests)) {
      LOG(ERROR) << "on_error [id=" << id << "] " << to_string(error);
    }
  }

  void on_closed() {
    LOG(INFO) << "on_closed";
    ready_to_stop_ = true;
    if (close_flag_) {
      yield();
      return;
    }
  }

  void quit() {
    if (close_flag_) {
      return;
    }

    LOG(WARNING) << "QUIT";
    close_flag_ = true;
    dump_memory_usage();
    td_.reset();
#if TD_WINDOWS
    stdin_reader_.reset();
#else
    is_stdin_reader_stopped_ = true;
#endif
    yield();
  }

#ifdef USE_READLINE
  Fd stdin_;
#else
  using StreamConnection = BufferedFd<Fd>;
  StreamConnection stdin_;
#endif
  static CliClient *instance_;

#ifdef USE_READLINE
  /* Callback function called for each line when accept-line executed, EOF
   *    seen, or EOF character read.  This sets a flag and returns; it could
   *       also call exit. */
  static void cb_linehandler(char *line) {
    /* Can use ^D (stty eof) to exit. */
    if (line == nullptr) {
      LOG(FATAL) << "closed";
      return;
    }
    if (*line) {
      add_history(line);
    }
    instance_->add_cmd(line);
    rl_free(line);
  }
#endif

  unique_ptr<TdCallback> make_td_callback() {
    class TdCallbackImpl : public TdCallback {
     public:
      explicit TdCallbackImpl(CliClient *client) : client_(client) {
      }
      void on_result(uint64 id, tl_object_ptr<td_api::Object> result) override {
        client_->on_result(id, std::move(result));
      }
      void on_error(uint64 id, tl_object_ptr<td_api::error> error) override {
        client_->on_error(id, std::move(error));
      }
      void on_closed() override {
        client_->on_closed();
      }

     private:
      CliClient *client_;
    };
    return make_unique<TdCallbackImpl>(this);
  }

  void init_td() {
    close_flag_ = false;
    ready_to_stop_ = false;

    bool test_init = false;

    if (test_init) {
      td_ = create_actor<ClientActor>("ClientActor1", make_td_callback());
    }
    td_ = create_actor<ClientActor>("ClientActor2", make_td_callback());
    ready_to_stop_ = false;

    if (test_init) {
      for (int i = 0; i < 4; i++) {
        send_closure_later(td_, &ClientActor::request, std::numeric_limits<uint64>::max(),
                           td_api::make_object<td_api::setAlarm>(0.001 + 1000 * (i / 2)));
      }

      send_request(td_api::make_object<td_api::getTextEntities>(
          "@telegram /test_command https://telegram.org telegram.me @gif @test"));

      send_request(td_api::make_object<td_api::getOption>("use_pfs"));
      send_request(td_api::make_object<td_api::setOption>(
          "use_pfs", td_api::make_object<td_api::optionValueBoolean>(std::time(nullptr) / 86400 % 2 == 0)));
      send_request(td_api::make_object<td_api::setOption>("use_storage_optimizer",
                                                          td_api::make_object<td_api::optionValueBoolean>(false)));

      send_request(td_api::make_object<td_api::setNetworkType>(td_api::make_object<td_api::networkTypeWiFi>()));
      send_request(td_api::make_object<td_api::getNetworkStatistics>());
      send_request(td_api::make_object<td_api::getCountryCode>());

      auto bad_parameters = td_api::make_object<td_api::tdlibParameters>();
      bad_parameters->database_directory_ = "/..";
      bad_parameters->api_id_ = api_id_;
      bad_parameters->api_hash_ = api_hash_;
      send_request(td_api::make_object<td_api::setTdlibParameters>(std::move(bad_parameters)));
    }

    auto parameters = td_api::make_object<td_api::tdlibParameters>();
    parameters->use_test_dc_ = use_test_dc_;
    parameters->use_message_database_ = true;
    parameters->use_secret_chats_ = true;
    parameters->api_id_ = api_id_;
    parameters->api_hash_ = api_hash_;
    parameters->system_language_code_ = "en";
    parameters->device_model_ = "Desktop";
    parameters->system_version_ = "Unknown";
    parameters->application_version_ = "tg_cli";
    send_request(td_api::make_object<td_api::setTdlibParameters>(std::move(parameters)));
    send_request(td_api::make_object<td_api::checkDatabaseEncryptionKey>());
  }

  void init() {
    instance_ = this;

    init_td();

#if TD_WINDOWS
    auto stdin_id = Scheduler::instance()->sched_count() - 1;
    class StdinReader : public Actor {
     public:
      explicit StdinReader(ActorShared<CliClient> parent) : parent_(std::move(parent)) {
      }
      void start_up() override {
        stdin_ = &Fd::Stdin();
        set_timeout_in(0.001);
      }
      void timeout_expired() override {
        std::array<char, 100> buf;
        auto t_res = stdin_->read(MutableSlice(buf.data(), buf.size()));
        if (t_res.is_error()) {
          LOG(FATAL) << "Can't read from stdin";
        }
        auto res = t_res.ok();
        VLOG(fd) << res << " " << string(buf.data(), res);
        data_.append(string(buf.data(), res));
        process();
        set_timeout_in(0.05);
      }

     private:
      Fd *stdin_ = nullptr;
      string data_;
      ActorShared<CliClient> parent_;
      void process() {
        while (true) {
          auto pos = data_.find('\n');
          if (pos == string::npos) {
            break;
          }
          auto cmd = string(data_.data(), pos);
          while (!cmd.empty() && cmd.back() == '\r') {
            cmd.pop_back();
          }
          send_closure(parent_, &CliClient::on_cmd, cmd);
          data_ = data_.substr(pos + 1);
        }
      }
    };
    stdin_reader_ = create_actor_on_scheduler<StdinReader>("stdin_reader", stdin_id, actor_shared(this, 1));
#else
    Fd::Stdin().set_is_blocking(false).ensure();
#ifdef USE_READLINE
    deactivate_readline();
    rl_callback_handler_install(prompt, cb_linehandler);
    rl_attempted_completion_function = tg_cli_completion;
    reactivate_readline();

    stdin_ = Fd::Stdin().clone();
#else
    stdin_ = StreamConnection(Fd::Stdin().clone());
#endif
    stdin_.get_fd().set_observer(this);
    subscribe(stdin_, Fd::Read);
#endif

    if (get_chat_list_) {
      send_request(make_tl_object<td_api::getChats>(std::numeric_limits<int64>::max(), 0, 100));
    }
    if (disable_network_) {
      send_request(make_tl_object<td_api::setNetworkType>(make_tl_object<td_api::networkTypeNone>()));
    }
  }
#ifndef USE_READLINE
  size_t buffer_pos_ = 0;
  Result<BufferSlice> process_stdin(ChainBufferReader *buffer) {
    auto found = find_boundary(buffer->clone(), "\n", buffer_pos_);

    if (!found) {
      return Status::Error("End of line not found");
    }

    auto data = buffer->cut_head(buffer_pos_).move_as_buffer_slice();
    if (!data.empty() && data[data.size() - 1] == '\r') {
      data.truncate(data.size() - 1);
    }
    buffer->cut_head(1);
    buffer_pos_ = 0;
    return std::move(data);
  }
#endif

  static tl_object_ptr<td_api::formattedText> as_formatted_text(
      string text, vector<td_api::object_ptr<td_api::textEntity>> entities = {}) {
    if (entities.empty()) {
      auto parsed_text =
          execute(make_tl_object<td_api::parseTextEntities>(text, make_tl_object<td_api::textParseModeMarkdown>()));
      if (parsed_text->get_id() == td_api::formattedText::ID) {
        return td_api::move_object_as<td_api::formattedText>(parsed_text);
      }
    }
    return make_tl_object<td_api::formattedText>(text, std::move(entities));
  }

  static tl_object_ptr<td_api::formattedText> as_caption(string caption,
                                                         vector<td_api::object_ptr<td_api::textEntity>> entities = {}) {
    return as_formatted_text(caption, std::move(entities));
  }

  tl_object_ptr<td_api::NotificationSettingsScope> get_notification_settings_scope(Slice scope) const {
    if (scope == "chats" || scope == "groups" || scope == "channels" || as_bool(scope.str())) {
      return make_tl_object<td_api::notificationSettingsScopeGroupChats>();
    }
    return make_tl_object<td_api::notificationSettingsScopePrivateChats>();
  }

  static tl_object_ptr<td_api::UserPrivacySetting> get_user_privacy_setting(MutableSlice setting) {
    setting = trim(setting);
    to_lower_inplace(setting);
    if (setting == "invite") {
      return make_tl_object<td_api::userPrivacySettingAllowChatInvites>();
    }
    if (setting == "status") {
      return make_tl_object<td_api::userPrivacySettingShowStatus>();
    }
    if (setting == "call") {
      return make_tl_object<td_api::userPrivacySettingAllowCalls>();
    }
    return nullptr;
  }

  static tl_object_ptr<td_api::SearchMessagesFilter> get_search_messages_filter(MutableSlice filter) {
    filter = trim(filter);
    to_lower_inplace(filter);
    if (filter == "an" || filter == "animation") {
      return make_tl_object<td_api::searchMessagesFilterAnimation>();
    }
    if (filter == "au" || filter == "audio") {
      return make_tl_object<td_api::searchMessagesFilterAudio>();
    }
    if (filter == "d" || filter == "document") {
      return make_tl_object<td_api::searchMessagesFilterDocument>();
    }
    if (filter == "p" || filter == "photo") {
      return make_tl_object<td_api::searchMessagesFilterPhoto>();
    }
    if (filter == "vi" || filter == "video") {
      return make_tl_object<td_api::searchMessagesFilterVideo>();
    }
    if (filter == "vo" || filter == "voice") {
      return make_tl_object<td_api::searchMessagesFilterVoiceNote>();
    }
    if (filter == "pvi") {
      return make_tl_object<td_api::searchMessagesFilterPhotoAndVideo>();
    }
    if (filter == "u" || filter == "url") {
      return make_tl_object<td_api::searchMessagesFilterUrl>();
    }
    if (filter == "cp" || filter == "chatphoto") {
      return make_tl_object<td_api::searchMessagesFilterChatPhoto>();
    }
    if (filter == "c" || filter == "call") {
      return make_tl_object<td_api::searchMessagesFilterCall>();
    }
    if (filter == "mc" || filter == "missedcall") {
      return make_tl_object<td_api::searchMessagesFilterMissedCall>();
    }
    if (filter == "vn" || filter == "videonote") {
      return make_tl_object<td_api::searchMessagesFilterVideoNote>();
    }
    if (filter == "vvn" || filter == "voicevideonote") {
      return make_tl_object<td_api::searchMessagesFilterVoiceAndVideoNote>();
    }
    if (filter == "m" || filter == "mention") {
      return make_tl_object<td_api::searchMessagesFilterMention>();
    }
    if (filter == "um" || filter == "umention") {
      return make_tl_object<td_api::searchMessagesFilterUnreadMention>();
    }
    if (!filter.empty()) {
      LOG(ERROR) << "Unsupported message filter " << filter;
    }
    return nullptr;
  }

  static tl_object_ptr<td_api::ChatMembersFilter> get_chat_members_filter(MutableSlice filter) {
    filter = trim(filter);
    to_lower_inplace(filter);
    if (filter == "a" || filter == "admin" || filter == "administrators") {
      return make_tl_object<td_api::chatMembersFilterAdministrators>();
    }
    if (filter == "b" || filter == "banned") {
      return make_tl_object<td_api::chatMembersFilterBanned>();
    }
    if (filter == "bot" || filter == "bots") {
      return make_tl_object<td_api::chatMembersFilterBots>();
    }
    if (filter == "m" || filter == "members") {
      return make_tl_object<td_api::chatMembersFilterMembers>();
    }
    if (filter == "r" || filter == "rest" || filter == "restricted") {
      return make_tl_object<td_api::chatMembersFilterRestricted>();
    }
    if (!filter.empty()) {
      LOG(ERROR) << "Unsupported chat member filter " << filter;
    }
    return nullptr;
  }

  tl_object_ptr<td_api::TopChatCategory> get_top_chat_category(MutableSlice category) {
    category = trim(category);
    to_lower_inplace(category);
    if (!category.empty() && category.back() == 's') {
      category.remove_suffix(1);
    }
    if (category == "bot") {
      return make_tl_object<td_api::topChatCategoryBots>();
    } else if (category == "group") {
      return make_tl_object<td_api::topChatCategoryGroups>();
    } else if (category == "channel") {
      return make_tl_object<td_api::topChatCategoryChannels>();
    } else if (category == "inline") {
      return make_tl_object<td_api::topChatCategoryInlineBots>();
    } else if (category == "call") {
      return make_tl_object<td_api::topChatCategoryCalls>();
    } else {
      return make_tl_object<td_api::topChatCategoryUsers>();
    }
  }

  static tl_object_ptr<td_api::ChatAction> get_chat_action(MutableSlice action) {
    action = trim(action);
    to_lower_inplace(action);
    if (action == "c" || action == "cancel") {
      return make_tl_object<td_api::chatActionCancel>();
    }
    if (action == "rvi" || action == "record_video") {
      return make_tl_object<td_api::chatActionRecordingVideo>();
    }
    if (action == "uvi" || action == "upload_video") {
      return make_tl_object<td_api::chatActionUploadingVideo>(50);
    }
    if (action == "rvo" || action == "record_voice") {
      return make_tl_object<td_api::chatActionRecordingVoiceNote>();
    }
    if (action == "uvo" || action == "upload_voice") {
      return make_tl_object<td_api::chatActionUploadingVoiceNote>(50);
    }
    if (action == "up" || action == "upload_photo") {
      return make_tl_object<td_api::chatActionUploadingPhoto>(50);
    }
    if (action == "ud" || action == "upload_document") {
      return make_tl_object<td_api::chatActionUploadingDocument>(50);
    }
    if (action == "fl" || action == "find_location") {
      return make_tl_object<td_api::chatActionChoosingLocation>();
    }
    if (action == "cc" || action == "choose_contact") {
      return make_tl_object<td_api::chatActionChoosingContact>();
    }
    if (action == "spg" || action == "start_play_game") {
      return make_tl_object<td_api::chatActionStartPlayingGame>();
    }
    if (action == "rvn" || action == "record_video_note") {
      return make_tl_object<td_api::chatActionRecordingVideoNote>();
    }
    if (action == "uvn" || action == "upload_video_note") {
      return make_tl_object<td_api::chatActionUploadingVideoNote>(50);
    }
    return make_tl_object<td_api::chatActionTyping>();
  }

  static tl_object_ptr<td_api::NetworkType> get_network_type(MutableSlice type) {
    type = trim(type);
    to_lower_inplace(type);
    if (type == "none") {
      return make_tl_object<td_api::networkTypeNone>();
    }
    if (type == "mobile") {
      return make_tl_object<td_api::networkTypeMobile>();
    }
    if (type == "roaming") {
      return make_tl_object<td_api::networkTypeMobileRoaming>();
    }
    if (type == "wifi") {
      return make_tl_object<td_api::networkTypeWiFi>();
    }
    if (type == "other") {
      return make_tl_object<td_api::networkTypeOther>();
    }
    return nullptr;
  }

  static tl_object_ptr<td_api::PassportElementType> as_passport_element_type(Slice passport_element_type) {
    if (passport_element_type == "address" || passport_element_type == "a") {
      return make_tl_object<td_api::passportElementTypeAddress>();
    }
    if (passport_element_type == "email" || passport_element_type == "e") {
      return make_tl_object<td_api::passportElementTypeEmailAddress>();
    }
    if (passport_element_type == "phone" || passport_element_type == "p") {
      return make_tl_object<td_api::passportElementTypePhoneNumber>();
    }
    if (passport_element_type == "pd") {
      return make_tl_object<td_api::passportElementTypePersonalDetails>();
    }
    if (passport_element_type == "dl") {
      return make_tl_object<td_api::passportElementTypeDriverLicense>();
    }
    if (passport_element_type == "ip") {
      return make_tl_object<td_api::passportElementTypeInternalPassport>();
    }
    if (passport_element_type == "ic") {
      return make_tl_object<td_api::passportElementTypeIdentityCard>();
    }
    if (passport_element_type == "ra") {
      return make_tl_object<td_api::passportElementTypeRentalAgreement>();
    }
    if (passport_element_type == "pr") {
      return make_tl_object<td_api::passportElementTypePassportRegistration>();
    }
    if (passport_element_type == "tr") {
      return make_tl_object<td_api::passportElementTypeTemporaryRegistration>();
    }
    return make_tl_object<td_api::passportElementTypePassport>();
  }

  static auto as_passport_element_types(Slice types, char delimiter = ',') {
    return transform(full_split(types, delimiter), [](Slice str) { return as_passport_element_type(str); });
  }

  static tl_object_ptr<td_api::InputPassportElement> as_input_passport_element(string passport_element_type, string arg,
                                                                               bool with_selfie) {
    vector<td_api::object_ptr<td_api::InputFile>> input_files;
    td_api::object_ptr<td_api::InputFile> selfie;
    if (!arg.empty()) {
      auto files = full_split(arg);
      CHECK(!files.empty());
      if (with_selfie) {
        selfie = as_input_file(files.back());
        files.pop_back();
      }
      for (auto file : files) {
        input_files.push_back(as_input_file(file));
      }
    }
    if (passport_element_type == "address" || passport_element_type == "a") {
      return make_tl_object<td_api::inputPassportElementAddress>(
          make_tl_object<td_api::address>("US", "CA", "Los Angeles", "Washington", "", "90001"));
    } else if (passport_element_type == "email" || passport_element_type == "e") {
      return make_tl_object<td_api::inputPassportElementEmailAddress>(arg);
    } else if (passport_element_type == "phone" || passport_element_type == "p") {
      return make_tl_object<td_api::inputPassportElementPhoneNumber>(arg);
    } else if (passport_element_type == "pd") {
      return make_tl_object<td_api::inputPassportElementPersonalDetails>(make_tl_object<td_api::personalDetails>(
          "Mike", "Jr", "Towers", u8"Mike\u2708", u8"Jr\u26fd", u8"Towers\u2757",
          make_tl_object<td_api::date>(29, 2, 2000), "male", "US", "GB"));
    } else if (passport_element_type == "driver_license" || passport_element_type == "dl") {
      if (input_files.size() >= 2) {
        auto front_side = std::move(input_files[0]);
        input_files.erase(input_files.begin());
        auto reverse_side = std::move(input_files[0]);
        input_files.erase(input_files.begin());
        return make_tl_object<td_api::inputPassportElementDriverLicense>(make_tl_object<td_api::inputIdentityDocument>(
            "1234567890", make_tl_object<td_api::date>(1, 3, 2029), std::move(front_side), std::move(reverse_side),
            std::move(selfie), std::move(input_files)));
      }
    } else if (passport_element_type == "identity_card" || passport_element_type == "ic") {
      if (input_files.size() >= 2) {
        auto front_side = std::move(input_files[0]);
        input_files.erase(input_files.begin());
        auto reverse_side = std::move(input_files[0]);
        input_files.erase(input_files.begin());
        return make_tl_object<td_api::inputPassportElementIdentityCard>(make_tl_object<td_api::inputIdentityDocument>(
            "1234567890", nullptr, std::move(front_side), std::move(reverse_side), std::move(selfie),
            std::move(input_files)));
      }
    } else if (passport_element_type == "internal_passport" || passport_element_type == "ip") {
      if (input_files.size() >= 1) {
        auto front_side = std::move(input_files[0]);
        input_files.erase(input_files.begin());
        return make_tl_object<td_api::inputPassportElementInternalPassport>(
            make_tl_object<td_api::inputIdentityDocument>("1234567890", nullptr, std::move(front_side), nullptr,
                                                          std::move(selfie), std::move(input_files)));
      }
    } else if (passport_element_type == "rental_agreement" || passport_element_type == "ra") {
      vector<td_api::object_ptr<td_api::InputFile>> translation;
      if (selfie != nullptr) {
        translation.push_back(std::move(selfie));
      }
      return make_tl_object<td_api::inputPassportElementRentalAgreement>(
          make_tl_object<td_api::inputPersonalDocument>(std::move(input_files), std::move(translation)));
    }

    LOG(ERROR) << "Unsupported passport element type " << passport_element_type;
    return nullptr;
  }

  static td_api::object_ptr<td_api::Object> execute(tl_object_ptr<td_api::Function> f) {
    LOG(INFO) << "Execute request: " << to_string(f);
    auto res = ClientActor::execute(std::move(f));
    LOG(INFO) << "Execute response: " << to_string(res);
    return res;
  }

  uint64 send_request(tl_object_ptr<td_api::Function> f) {
    static uint64 query_num = 1;
    if (!td_.empty()) {
      auto id = query_num++;
      send_closure_later(td_, &ClientActor::request, id, std::move(f));
      return id;
    } else {
      LOG(ERROR) << "Failed to send: " << to_string(f);
      return 0;
    }
  }

  void send_message(const string &chat_id, tl_object_ptr<td_api::InputMessageContent> &&input_message_content,
                    bool disable_notification = false, bool from_background = false, int64 reply_to_message_id = 0) {
    auto chat = as_chat_id(chat_id);
    auto id = send_request(make_tl_object<td_api::sendMessage>(
        chat, reply_to_message_id, disable_notification, from_background, nullptr, std::move(input_message_content)));
    query_id_to_send_message_info_[id].start_time = Time::now();
  }

  void on_cmd(string cmd) {
    // TODO: need to remove https://en.wikipedia.org/wiki/ANSI_escape_code from cmd
    cmd.erase(std::remove_if(cmd.begin(), cmd.end(), [](char c) { return 0 <= c && c < 32; }), cmd.end());
    LOG(INFO) << "CMD:[" << cmd << "]";

    string op;
    string args;
    std::tie(op, args) = split(cmd);

    const int32 OP_BLOCK_COUNT = 5;
    int32 op_not_found_count = 0;

    if (op == "gas") {
      send_request(make_tl_object<td_api::getAuthorizationState>());
    } else if (op == "sap") {
      send_request(make_tl_object<td_api::setAuthenticationPhoneNumber>(args, false, false));
    } else if (op == "rac") {
      send_request(make_tl_object<td_api::resendAuthenticationCode>());
    } else if (op == "cdek" || op == "CheckDatabaseEncryptionKey") {
      send_request(make_tl_object<td_api::checkDatabaseEncryptionKey>(args));
    } else if (op == "sdek" || op == "SetDatabaseEncryptionKey") {
      send_request(make_tl_object<td_api::setDatabaseEncryptionKey>(args));
    } else if (op == "cac") {
      string code;
      string first_name;
      string last_name;

      std::tie(code, args) = split(args);
      std::tie(first_name, last_name) = split(args);

      send_request(make_tl_object<td_api::checkAuthenticationCode>(code, first_name, last_name));
    } else if (op == "cap") {
      send_request(make_tl_object<td_api::checkAuthenticationPassword>(args));
    } else if (op == "cab" || op == "cabt") {
      send_request(make_tl_object<td_api::checkAuthenticationBotToken>(args));
    } else if (op == "rapr") {
      send_request(make_tl_object<td_api::requestAuthenticationPasswordRecovery>());
    } else if (op == "rap") {
      send_request(make_tl_object<td_api::recoverAuthenticationPassword>(args));
    } else if (op == "lo" || op == "LogOut" || op == "logout") {
      send_request(make_tl_object<td_api::logOut>());
    } else if (op == "ra" || op == "destroy") {
      send_request(make_tl_object<td_api::destroy>());
    } else if (op == "reset") {
      init_td();
    } else if (op == "close_td") {
      send_request(make_tl_object<td_api::close>());
    } else if (op == "DeleteAccountYesIReallyWantToDeleteMyAccount") {
      send_request(make_tl_object<td_api::deleteAccount>(args));
    } else if (op == "gps" || op == "GetPasswordState") {
      send_request(make_tl_object<td_api::getPasswordState>());
    } else if (op == "spass" || op == "SetPassword") {
      string password;
      string new_password;
      string new_hint;
      string recovery_email_address;
      std::tie(password, args) = split(args);
      if (password == "#") {
        password = "";
      }
      std::tie(new_password, args) = split(args);
      if (new_password == "#") {
        new_password = "";
      }
      std::tie(new_hint, args) = split(args);
      if (new_hint == "#") {
        new_hint = "";
      }
      recovery_email_address = args;
      if (recovery_email_address == "#") {
        recovery_email_address = "";
      }
      send_request(make_tl_object<td_api::setPassword>(password, new_password, new_hint, true, recovery_email_address));
    } else if (op == "gpafhttp") {
      string password;
      std::tie(password, args) = split(args);
      ChainBufferWriter writer;
      writer.append(PSLICE() << "GET " << args << " HTTP/1.1\r\n\r\n\r\n");
      ChainBufferReader reader = writer.extract_reader();
      HttpReader http_reader;
      http_reader.init(&reader);
      HttpQuery query;
      auto status = http_reader.read_next(&query);
      if (status.is_error()) {
        LOG(ERROR) << status.error();
        return;
      }
      string bot_id = query.get_arg("bot_id").str();
      string scope = query.get_arg("scope").str();
      string public_key = query.get_arg("public_key").str();
      string payload = query.get_arg("payload").str();
      LOG(INFO) << "Callback URL:" << query.get_arg("callback_url");
      send_request(make_tl_object<td_api::getPassportAuthorizationForm>(to_integer<int32>(bot_id), scope, public_key,
                                                                        payload, password));
    } else if (op == "gpaf") {
      string password;
      string bot_id;
      string scope;
      string public_key =
          "-----BEGIN PUBLIC KEY-----\n"
          "MIICIjANBgkqhkiG9w0BAQEFAAOCAg8AMIICCgKCAgEAzmgKr0fPP4rB/TsNEweC\n"
          "hoG3ntUxuBTmHsFBW6CpABGdaTmKZSjAI/cTofhBgtRQIOdX0YRGHHHhwyLf49Wv\n"
          "9l+XexbJOa0lTsJSNMj8Y/9sZbqUl5ur8ZOTM0sxbXC0XKexu1tM9YavH+Lbrobk\n"
          "jt0+cmo/zEYZWNtLVihnR2IDv+7tSgiDoFWi/koAUdfJ1VMw+hReUaLg3vE9CmPK\n"
          "tQiTy+NvmrYaBPb75I0Jz3Lrz1+mZSjLKO25iT84RIsxarBDd8iYh2avWkCmvtiR\n"
          "Lcif8wLxi2QWC1rZoCA3Ip+Hg9J9vxHlzl6xT01WjUStMhfwrUW6QBpur7FJ+aKM\n"
          "oaMoHieFNCG4qIkWVEHHSsUpLum4SYuEnyNH3tkjbrdldZanCvanGq+TZyX0buRt\n"
          "4zk7FGcu8iulUkAP/o/WZM0HKinFN/vuzNVA8iqcO/BBhewhzpqmmTMnWmAO8WPP\n"
          "DJMABRtXJnVuPh1CI5pValzomLJM4/YvnJGppzI1QiHHNA9JtxVmj2xf8jaXa1LJ\n"
          "WUNJK+RvUWkRUxpWiKQQO9FAyTPLRtDQGN9eUeDR1U0jqRk/gNT8smHGN6I4H+NR\n"
          "3X3/1lMfcm1dvk654ql8mxjCA54IpTPr/icUMc7cSzyIiQ7Tp9PZTl1gHh281ZWf\n"
          "P7d2+fuJMlkjtM7oAwf+tI8CAwEAAQ==\n"
          "-----END PUBLIC KEY-----";
      string payload;

      std::tie(password, args) = split(args);
      std::tie(bot_id, args) = split(args);
      std::tie(scope, payload) = split(args);
      send_request(make_tl_object<td_api::getPassportAuthorizationForm>(to_integer<int32>(bot_id), scope, public_key,
                                                                        payload, password));
    } else if (op == "spaf") {
      string id;
      string types;
      std::tie(id, types) = split(args);
      send_request(make_tl_object<td_api::sendPassportAuthorizationForm>(to_integer<int32>(id),
                                                                         as_passport_element_types(types)));
    } else if (op == "gpcl") {
      send_request(make_tl_object<td_api::getPreferredCountryLanguage>(args));
    } else if (op == "spnvc" || op == "SendPhoneNumberVerificationCode") {
      send_request(make_tl_object<td_api::sendPhoneNumberVerificationCode>(args, false, false));
    } else if (op == "cpnvc" || op == "CheckPhoneNumberVerificationCode") {
      send_request(make_tl_object<td_api::checkPhoneNumberVerificationCode>(args));
    } else if (op == "rpnvc" || op == "ResendPhoneNumberVerificationCode") {
      send_request(make_tl_object<td_api::resendPhoneNumberVerificationCode>());
    } else if (op == "seavc" || op == "SendEmailAddressVerificationCode") {
      send_request(make_tl_object<td_api::sendEmailAddressVerificationCode>(args));
    } else if (op == "ceavc" || op == "CheckEmailAddressVerificationCode") {
      send_request(make_tl_object<td_api::checkEmailAddressVerificationCode>(args));
    } else if (op == "reavc" || op == "ResendEmailAddressVerificationCode") {
      send_request(make_tl_object<td_api::resendEmailAddressVerificationCode>());
    } else if (op == "srea" || op == "SetRecoveryEmailAddress") {
      string password;
      string recovery_email_address;
      std::tie(password, recovery_email_address) = split(args);
      send_request(make_tl_object<td_api::setRecoveryEmailAddress>(password, recovery_email_address));
    } else if (op == "spncc") {
      send_request(make_tl_object<td_api::sendPhoneNumberVerificationCode>(args, false, false));
    } else if (op == "cpncc") {
      send_request(make_tl_object<td_api::checkPhoneNumberVerificationCode>(args));
    } else if (op == "rpncc") {
      send_request(make_tl_object<td_api::resendPhoneNumberVerificationCode>());
    } else if (op == "rpr" || op == "RequestPasswordRecovery") {
      send_request(make_tl_object<td_api::requestPasswordRecovery>());
    } else if (op == "rp" || op == "RecoverPassword") {
      send_request(make_tl_object<td_api::recoverPassword>(args));
    } else if (op == "grea" || op == "GetRecoveryEmailAddress") {
      send_request(make_tl_object<td_api::getRecoveryEmailAddress>(args));
    } else if (op == "gtp" || op == "GetTemporaryPassword") {
      send_request(make_tl_object<td_api::getTemporaryPasswordState>());
    } else if (op == "ctp" || op == "CreateTemporaryPassword") {
      send_request(make_tl_object<td_api::createTemporaryPassword>(args, 60 * 6));
    } else if (op == "gpe") {
      string password;
      string passport_element_type;
      std::tie(password, passport_element_type) = split(args);
      send_request(
          make_tl_object<td_api::getPassportElement>(as_passport_element_type(passport_element_type), password));
    } else if (op == "gape") {
      string password = args;
      send_request(make_tl_object<td_api::getAllPassportElements>(password));
    } else if (op == "spe" || op == "spes") {
      string password;
      string passport_element_type;
      string arg;
      std::tie(password, args) = split(args);
      std::tie(passport_element_type, arg) = split(args);
      send_request(make_tl_object<td_api::setPassportElement>(
          as_input_passport_element(passport_element_type, arg, op == "spes"), password));
    } else if (op == "dpe") {
      string passport_element_type = args;
      send_request(make_tl_object<td_api::deletePassportElement>(as_passport_element_type(passport_element_type)));
    } else if (op == "pdu" || op == "processDcUpdate") {
      string dc_id;
      string ip_port;
      std::tie(dc_id, ip_port) = split(args);
      send_request(make_tl_object<td_api::processDcUpdate>(dc_id, ip_port));
    } else if (op == "rda") {
      send_request(make_tl_object<td_api::registerDevice>(make_tl_object<td_api::deviceTokenApplePush>(args, true),
                                                          as_user_ids("")));
    } else if (op == "rdb") {
      send_request(make_tl_object<td_api::registerDevice>(make_tl_object<td_api::deviceTokenBlackBerryPush>(args),
                                                          as_user_ids("")));
    } else if (op == "rdt") {
      string token;
      string other_user_ids_str;

      std::tie(token, other_user_ids_str) = split(args);
      send_request(make_tl_object<td_api::registerDevice>(make_tl_object<td_api::deviceTokenTizenPush>(token),
                                                          as_user_ids(other_user_ids_str)));
    } else if (op == "rdu") {
      string token;
      string other_user_ids_str;

      std::tie(token, other_user_ids_str) = split(args);
      send_request(make_tl_object<td_api::registerDevice>(make_tl_object<td_api::deviceTokenUbuntuPush>(token),
                                                          as_user_ids(other_user_ids_str)));
    } else if (op == "rdw") {
      string endpoint;
      string key;
      string secret;
      string other_user_ids_str;

      std::tie(endpoint, args) = split(args);
      std::tie(key, args) = split(args);
      std::tie(secret, other_user_ids_str) = split(args);
      send_request(make_tl_object<td_api::registerDevice>(
          make_tl_object<td_api::deviceTokenWebPush>(endpoint, key, secret), as_user_ids(other_user_ids_str)));
    } else if (op == "gpf") {
      string chat_id;
      string message_id;

      std::tie(chat_id, message_id) = split(args);
      send_request(make_tl_object<td_api::getPaymentForm>(as_chat_id(chat_id), as_message_id(message_id)));
    } else if (op == "voi") {
      string chat_id;
      string message_id;
      string allow_save;

      std::tie(chat_id, args) = split(args);
      std::tie(message_id, allow_save) = split(args);
      send_request(make_tl_object<td_api::validateOrderInfo>(as_chat_id(chat_id), as_message_id(message_id), nullptr,
                                                             as_bool(allow_save)));
    } else if (op == "spfs") {
      string chat_id;
      string message_id;
      string order_info_id;
      string shipping_option_id;
      string saved_credentials_id;

      std::tie(chat_id, args) = split(args);
      std::tie(message_id, args) = split(args);
      std::tie(order_info_id, args) = split(args);
      std::tie(shipping_option_id, saved_credentials_id) = split(args);
      send_request(make_tl_object<td_api::sendPaymentForm>(
          as_chat_id(chat_id), as_message_id(message_id), order_info_id, shipping_option_id,
          make_tl_object<td_api::inputCredentialsSaved>(saved_credentials_id)));
    } else if (op == "spfn") {
      string chat_id;
      string message_id;
      string order_info_id;
      string shipping_option_id;
      string data;

      std::tie(chat_id, args) = split(args);
      std::tie(message_id, args) = split(args);
      std::tie(order_info_id, args) = split(args);
      std::tie(shipping_option_id, data) = split(args);
      send_request(make_tl_object<td_api::sendPaymentForm>(as_chat_id(chat_id), as_message_id(message_id),
                                                           order_info_id, shipping_option_id,
                                                           make_tl_object<td_api::inputCredentialsNew>(data, true)));
    } else if (op == "gpre") {
      string chat_id;
      string message_id;

      std::tie(chat_id, message_id) = split(args);
      send_request(make_tl_object<td_api::getPaymentReceipt>(as_chat_id(chat_id), as_message_id(message_id)));
    } else if (op == "gsoi") {
      send_request(make_tl_object<td_api::getSavedOrderInfo>());
    } else if (op == "dsoi") {
      send_request(make_tl_object<td_api::deleteSavedOrderInfo>());
    } else if (op == "dsc") {
      send_request(make_tl_object<td_api::deleteSavedCredentials>());
    } else if (op == "gpr") {
      send_request(make_tl_object<td_api::getUserPrivacySettingRules>(get_user_privacy_setting(args)));
    } else if (op == "spr") {
      string setting;
      string allow;
      std::tie(setting, allow) = split(args);

      std::vector<tl_object_ptr<td_api::UserPrivacySettingRule>> rules;
      if (as_bool(allow)) {
        rules.push_back(make_tl_object<td_api::userPrivacySettingRuleAllowAll>());
      } else {
        rules.push_back(make_tl_object<td_api::userPrivacySettingRuleRestrictAll>());
      }
      send_request(make_tl_object<td_api::setUserPrivacySettingRules>(
          get_user_privacy_setting(setting), make_tl_object<td_api::userPrivacySettingRules>(std::move(rules))));
    } else if (op == "cp" || op == "ChangePhone") {
      send_request(make_tl_object<td_api::changePhoneNumber>(args, false, false));
    } else if (op == "ccpc" || op == "CheckChangePhoneCode") {
      send_request(make_tl_object<td_api::checkChangePhoneNumberCode>(args));
    } else if (op == "rcpc" || op == "ResendChangePhoneCode") {
      send_request(make_tl_object<td_api::resendChangePhoneNumberCode>());
    } else if (op == "gco") {
      if (args.empty()) {
        send_request(make_tl_object<td_api::getContacts>());
      } else {
        send_request(make_tl_object<td_api::searchContacts>("", to_integer<int32>(args)));
      }
    } else if (op == "ImportContacts" || op == "cic") {
      vector<string> contacts_str = full_split(args, ';');
      vector<tl_object_ptr<td_api::contact>> contacts;
      for (auto c : contacts_str) {
        string phone_number;
        string first_name;
        string last_name;
        std::tie(phone_number, c) = split(c, ',');
        std::tie(first_name, last_name) = split(c, ',');
        contacts.push_back(make_tl_object<td_api::contact>(phone_number, first_name, last_name, string(), 0));
      }

      if (op == "cic") {
        send_request(make_tl_object<td_api::changeImportedContacts>(std::move(contacts)));
      } else {
        send_request(make_tl_object<td_api::importContacts>(std::move(contacts)));
      }
    } else if (op == "RemoveContacts") {
      send_request(make_tl_object<td_api::removeContacts>(as_user_ids(args)));
    } else if (op == "gicc") {
      send_request(make_tl_object<td_api::getImportedContactCount>());
    } else if (op == "ClearImportedContacts") {
      send_request(make_tl_object<td_api::clearImportedContacts>());
    } else {
      op_not_found_count++;
    }

    if (op == "gc" || op == "GetChats") {
      string offset_order_string;
      string offset_chat_id;
      string limit;

      std::tie(limit, args) = split(args);
      std::tie(offset_order_string, offset_chat_id) = split(args);

      if (limit.empty()) {
        limit = "10000";
      }
      int64 offset_order;
      if (offset_order_string.empty()) {
        offset_order = std::numeric_limits<int64>::max();
      } else {
        offset_order = to_integer<int64>(offset_order_string);
      }
      send_request(
          make_tl_object<td_api::getChats>(offset_order, as_chat_id(offset_chat_id), to_integer<int32>(limit)));
    } else if (op == "gcc" || op == "GetCommonChats") {
      string user_id;
      string offset_chat_id;
      string limit;

      std::tie(user_id, args) = split(args);
      std::tie(offset_chat_id, limit) = split(args);

      if (limit.empty()) {
        limit = "100";
      }
      send_request(make_tl_object<td_api::getGroupsInCommon>(as_user_id(user_id), as_chat_id(offset_chat_id),
                                                             to_integer<int32>(limit)));
    } else if (op == "gh" || op == "GetHistory" || op == "ghl") {
      string chat_id;
      string from_message_id;
      string offset;
      string limit;

      std::tie(chat_id, args) = split(args);
      std::tie(from_message_id, args) = split(args);
      if (from_message_id.empty()) {
        from_message_id = "0";
      }
      std::tie(offset, args) = split(args);
      if (offset.empty()) {
        offset = "0";
      }
      std::tie(limit, args) = split(args);
      if (limit.empty()) {
        limit = "10";
      }
      if (!args.empty()) {
        LOG(ERROR) << "Wrong parameters to function getChatHistory specified";
      } else {
        send_request(make_tl_object<td_api::getChatHistory>(as_chat_id(chat_id), as_message_id(from_message_id),
                                                            to_integer<int32>(offset), to_integer<int32>(limit),
                                                            op == "ghl"));
      }
    } else if (op == "ghf") {
      get_history_chat_id_ = as_chat_id(args);

      send_request(make_tl_object<td_api::getChatHistory>(get_history_chat_id_, std::numeric_limits<int64>::max(), 0,
                                                          100, false));
    } else if (op == "spvf") {
      search_chat_id_ = as_chat_id(args);

      send_request(make_tl_object<td_api::searchChatMessages>(
          search_chat_id_, "", 0, 0, 0, 100, make_tl_object<td_api::searchMessagesFilterPhotoAndVideo>()));
    } else if (op == "Search") {
      string from_date;
      string limit;
      string query;

      std::tie(query, args) = split(args);
      std::tie(limit, from_date) = split(args);
      if (from_date.empty()) {
        from_date = "0";
      }
      send_request(make_tl_object<td_api::searchMessages>(query, to_integer<int32>(from_date), 2147482647, 0,
                                                          to_integer<int32>(limit)));
    } else if (op == "SCM") {
      string chat_id;
      string limit;
      string query;

      std::tie(chat_id, args) = split(args);
      std::tie(limit, query) = split(args);
      if (limit.empty()) {
        limit = "10";
      }

      send_request(make_tl_object<td_api::searchChatMessages>(as_chat_id(chat_id), query, 0, 0, 0,
                                                              to_integer<int32>(limit), nullptr));
    } else if (op == "SMME") {
      string chat_id;
      string limit;

      std::tie(chat_id, limit) = split(args);
      if (limit.empty()) {
        limit = "10";
      }

      send_request(make_tl_object<td_api::searchChatMessages>(as_chat_id(chat_id), "", my_id_, 0, 0,
                                                              to_integer<int32>(limit), nullptr));
    } else if (op == "SM") {
      string chat_id;
      string offset_message_id;
      string offset;
      string limit;
      string filter;

      std::tie(chat_id, args) = split(args);
      std::tie(filter, args) = split(args);
      std::tie(limit, args) = split(args);
      std::tie(offset_message_id, offset) = split(args);
      if (limit.empty()) {
        limit = "10";
      }
      if (offset_message_id.empty()) {
        offset_message_id = "0";
      }
      if (offset.empty()) {
        offset = "0";
      }

      send_request(make_tl_object<td_api::searchChatMessages>(
          as_chat_id(chat_id), "", 0, as_message_id(offset_message_id), to_integer<int32>(offset),
          to_integer<int32>(limit), get_search_messages_filter(filter)));
    } else if (op == "SC") {
      string limit;
      string offset_message_id;
      string only_missed;

      std::tie(limit, args) = split(args);
      std::tie(offset_message_id, only_missed) = split(args);
      if (limit.empty()) {
        limit = "10";
      }
      if (offset_message_id.empty()) {
        offset_message_id = "0";
      }

      send_request(make_tl_object<td_api::searchCallMessages>(as_message_id(offset_message_id),
                                                              to_integer<int32>(limit), as_bool(only_missed)));
    } else if (op == "SCRLM") {
      string chat_id;
      string limit;

      std::tie(chat_id, limit) = split(args);
      if (limit.empty()) {
        limit = "10";
      }

      send_request(
          make_tl_object<td_api::searchChatRecentLocationMessages>(as_chat_id(chat_id), to_integer<int32>(limit)));
    } else if (op == "SearchAudio") {
      string chat_id;
      string offset_message_id;
      string limit;
      string query;

      std::tie(chat_id, args) = split(args);
      std::tie(offset_message_id, args) = split(args);
      if (offset_message_id.empty()) {
        offset_message_id = "0";
      }
      std::tie(limit, query) = split(args);
      if (limit.empty()) {
        limit = "10";
      }
      send_request(make_tl_object<td_api::searchChatMessages>(
          as_chat_id(chat_id), query, 0, as_message_id(offset_message_id), 0, to_integer<int32>(limit),
          make_tl_object<td_api::searchMessagesFilterAudio>()));
    } else if (op == "SearchDocument") {
      string chat_id;
      string offset_message_id;
      string limit;
      string query;

      std::tie(chat_id, args) = split(args);
      std::tie(offset_message_id, args) = split(args);
      if (offset_message_id.empty()) {
        offset_message_id = "0";
      }
      std::tie(limit, query) = split(args);
      if (limit.empty()) {
        limit = "10";
      }
      send_request(make_tl_object<td_api::searchChatMessages>(
          as_chat_id(chat_id), query, 0, to_integer<int64>(offset_message_id), 0, to_integer<int32>(limit),
          make_tl_object<td_api::searchMessagesFilterDocument>()));
    } else if (op == "SearchPhoto") {
      string chat_id;
      string offset_message_id;
      string limit;
      string query;

      std::tie(chat_id, args) = split(args);
      std::tie(offset_message_id, args) = split(args);
      if (offset_message_id.empty()) {
        offset_message_id = "2000000000000000000";
      }
      std::tie(limit, query) = split(args);
      if (limit.empty()) {
        limit = "10";
      }
      send_request(make_tl_object<td_api::searchChatMessages>(
          as_chat_id(chat_id), query, 0, as_message_id(offset_message_id), 0, to_integer<int32>(limit),
          make_tl_object<td_api::searchMessagesFilterPhoto>()));
    } else if (op == "SearchChatPhoto") {
      string chat_id;
      string offset_message_id;
      string limit;
      string query;

      std::tie(chat_id, args) = split(args);
      std::tie(offset_message_id, args) = split(args);
      if (offset_message_id.empty()) {
        offset_message_id = "2000000000000000000";
      }
      std::tie(limit, query) = split(args);
      if (limit.empty()) {
        limit = "10";
      }
      send_request(make_tl_object<td_api::searchChatMessages>(
          as_chat_id(chat_id), query, 0, as_message_id(offset_message_id), 0, to_integer<int32>(limit),
          make_tl_object<td_api::searchMessagesFilterChatPhoto>()));
    } else if (op == "gcmc") {
      string chat_id;
      string filter;
      string return_local;

      std::tie(chat_id, args) = split(args);
      std::tie(filter, return_local) = split(args);

      send_request(make_tl_object<td_api::getChatMessageCount>(as_chat_id(chat_id), get_search_messages_filter(filter),
                                                               as_bool(return_local)));
    } else if (op == "gup" || op == "GetUserPhotos") {
      string user_id;
      string offset;
      string limit;

      std::tie(user_id, args) = split(args);
      std::tie(offset, args) = split(args);
      if (offset.empty()) {
        offset = "0";
      }
      std::tie(limit, args) = split(args);
      if (limit.empty()) {
        limit = "10";
      }
      if (!args.empty()) {
        LOG(ERROR) << "Wrong parameters to function getUserProfilePhotos specified";
      } else {
        send_request(make_tl_object<td_api::getUserProfilePhotos>(as_user_id(user_id), to_integer<int32>(offset),
                                                                  to_integer<int32>(limit)));
      }
    } else if (op == "dcrm") {
      string chat_id;
      string message_id;

      std::tie(chat_id, message_id) = split(args);
      send_request(make_tl_object<td_api::deleteChatReplyMarkup>(as_chat_id(chat_id), as_message_id(message_id)));
    } else if (op == "glti") {
      send_request(make_tl_object<td_api::getLocalizationTargetInfo>(as_bool(args)));
    } else if (op == "glps") {
      string language_code;
      string keys;

      std::tie(language_code, keys) = split(args);
      send_request(make_tl_object<td_api::getLanguagePackStrings>(language_code, full_split(keys)));
    } else if (op == "glpss") {
      string language_database_path;
      string language_pack;
      string language_code;
      string key;

      std::tie(language_database_path, args) = split(args);
      std::tie(language_pack, args) = split(args);
      std::tie(language_code, key) = split(args);
      send_request(
          make_tl_object<td_api::getLanguagePackString>(language_database_path, language_pack, language_code, key));
    } else if (op == "sclp") {
      string language_code;
      string name;
      string native_name;
      string key;

      std::tie(language_code, args) = split(args);
      std::tie(name, args) = split(args);
      std::tie(native_name, key) = split(args);

      vector<tl_object_ptr<td_api::languagePackString>> strings;
      strings.push_back(make_tl_object<td_api::languagePackString>(
          key, make_tl_object<td_api::languagePackStringValueOrdinary>("Ordinary value")));
      strings.push_back(make_tl_object<td_api::languagePackString>(
          "Plu", make_tl_object<td_api::languagePackStringValuePluralized>("Zero", string("One\0One", 7), "Two", "Few",
                                                                           "Many", "Other")));
      strings.push_back(make_tl_object<td_api::languagePackString>(
          "DELETED", make_tl_object<td_api::languagePackStringValueDeleted>()));

      send_request(make_tl_object<td_api::setCustomLanguagePack>(
          make_tl_object<td_api::languagePackInfo>(language_code, name, native_name, 3), std::move(strings)));
    } else if (op == "eclpi") {
      string language_code;
      string name;
      string native_name;

      std::tie(language_code, args) = split(args);
      std::tie(name, native_name) = split(args);

      send_request(make_tl_object<td_api::editCustomLanguagePackInfo>(
          make_tl_object<td_api::languagePackInfo>(language_code, name, native_name, 3)));
    } else if (op == "sclpsv" || op == "sclpsp" || op == "sclpsd") {
      string language_code;
      string key;
      string value;

      std::tie(language_code, args) = split(args);
      std::tie(key, value) = split(args);

      tl_object_ptr<td_api::languagePackString> str = make_tl_object<td_api::languagePackString>(key, nullptr);
      if (op == "sclsv") {
        str->value_ = make_tl_object<td_api::languagePackStringValueOrdinary>(value);
      } else if (op == "sclsp") {
        str->value_ = make_tl_object<td_api::languagePackStringValuePluralized>(value, string("One\0One", 7), "Two",
                                                                                "Few", "Many", "Other");
      } else {
        str->value_ = make_tl_object<td_api::languagePackStringValueDeleted>();
      }

      send_request(make_tl_object<td_api::setCustomLanguagePackString>(language_code, std::move(str)));
    } else if (op == "dlp") {
      send_request(make_tl_object<td_api::deleteLanguagePack>(args));
    } else if (op == "go") {
      send_request(make_tl_object<td_api::getOption>(args));
    } else if (op == "sob") {
      string name;
      string value;

      std::tie(name, value) = split(args);
      send_request(make_tl_object<td_api::setOption>(name, make_tl_object<td_api::optionValueBoolean>(as_bool(value))));
    } else if (op == "soe") {
      send_request(make_tl_object<td_api::setOption>(args, make_tl_object<td_api::optionValueEmpty>()));
    } else if (op == "soi") {
      string name;
      string value;

      std::tie(name, value) = split(args);
      int32 value_int = to_integer<int32>(value);
      send_request(make_tl_object<td_api::setOption>(name, make_tl_object<td_api::optionValueInteger>(value_int)));
    } else if (op == "sos") {
      string name;
      string value;

      std::tie(name, value) = split(args);
      send_request(make_tl_object<td_api::setOption>(name, make_tl_object<td_api::optionValueString>(value)));
    } else if (op == "me") {
      send_request(make_tl_object<td_api::getMe>());
    } else if (op == "sattl") {
      send_request(make_tl_object<td_api::setAccountTtl>(make_tl_object<td_api::accountTtl>(to_integer<int32>(args))));
    } else if (op == "gattl") {
      send_request(make_tl_object<td_api::getAccountTtl>());
    } else if (op == "GetActiveSessions") {
      send_request(make_tl_object<td_api::getActiveSessions>());
    } else if (op == "TerminateSession") {
      send_request(make_tl_object<td_api::terminateSession>(to_integer<int64>(args)));
    } else if (op == "TerminateAllOtherSessions") {
      send_request(make_tl_object<td_api::terminateAllOtherSessions>());
    } else if (op == "gcw") {
      send_request(make_tl_object<td_api::getConnectedWebsites>());
    } else if (op == "dw") {
      send_request(make_tl_object<td_api::disconnectWebsite>(to_integer<int64>(args)));
    } else if (op == "daw") {
      send_request(make_tl_object<td_api::disconnectAllWebsites>());
    } else if (op == "gw") {
      send_request(make_tl_object<td_api::getWallpapers>());
    } else if (op == "gccode") {
      send_request(make_tl_object<td_api::getCountryCode>());
    } else if (op == "git") {
      send_request(make_tl_object<td_api::getInviteText>());
    } else if (op == "atos") {
      send_request(make_tl_object<td_api::acceptTermsOfService>(args));
    } else if (op == "gdli") {
      send_request(make_tl_object<td_api::getDeepLinkInfo>(args));
    } else if (op == "tme") {
      send_request(make_tl_object<td_api::getRecentlyVisitedTMeUrls>(args));
    } else if (op == "bu") {
      send_request(make_tl_object<td_api::blockUser>(as_user_id(args)));
    } else if (op == "ubu") {
      send_request(make_tl_object<td_api::unblockUser>(as_user_id(args)));
    } else if (op == "gbu") {
      string offset;
      string limit;

      std::tie(offset, limit) = split(args);
      if (offset.empty()) {
        offset = "0";
      }
      if (limit.empty()) {
        limit = "10";
      }
      send_request(make_tl_object<td_api::getBlockedUsers>(to_integer<int32>(offset), to_integer<int32>(limit)));
    } else if (op == "gu") {
      send_request(make_tl_object<td_api::getUser>(as_user_id(args)));
    } else if (op == "gsu") {
      send_request(make_tl_object<td_api::getSupportUser>());
    } else if (op == "gs") {
      string limit;
      string emoji;
      std::tie(limit, emoji) = split(args);
      send_request(make_tl_object<td_api::getStickers>(emoji, to_integer<int32>(limit)));
    } else if (op == "sst") {
      string limit;
      string emoji;
      std::tie(limit, emoji) = split(args);
      send_request(make_tl_object<td_api::searchStickers>(emoji, to_integer<int32>(limit)));
    } else if (op == "gss") {
      send_request(make_tl_object<td_api::getStickerSet>(to_integer<int64>(args)));
    } else if (op == "giss") {
      send_request(make_tl_object<td_api::getInstalledStickerSets>(as_bool(args)));
    } else if (op == "gass") {
      string is_masks;
      string offset_sticker_set_id;
      string limit;

      std::tie(is_masks, args) = split(args);
      std::tie(offset_sticker_set_id, limit) = split(args);

      send_request(make_tl_object<td_api::getArchivedStickerSets>(
          as_bool(is_masks), to_integer<int64>(offset_sticker_set_id), to_integer<int32>(limit)));
    } else if (op == "gtss") {
      send_request(make_tl_object<td_api::getTrendingStickerSets>());
    } else if (op == "gatss") {
      send_request(make_tl_object<td_api::getAttachedStickerSets>(to_integer<int32>(args)));
    } else if (op == "storage") {
      send_request(make_tl_object<td_api::getStorageStatistics>(to_integer<int32>(args)));
    } else if (op == "storage_fast") {
      send_request(make_tl_object<td_api::getStorageStatisticsFast>());
    } else if (op == "optimize_storage") {
      string chat_ids;
      string exclude_chat_ids;
      string chat_ids_limit;
      std::tie(chat_ids, args) = split(args);
      std::tie(exclude_chat_ids, chat_ids_limit) = split(args);
      send_request(make_tl_object<td_api::optimizeStorage>(
          10000000, -1, -1, 0, std::vector<tl_object_ptr<td_api::FileType>>(), as_chat_ids(chat_ids, ','),
          as_chat_ids(exclude_chat_ids, ','), to_integer<int32>(chat_ids_limit)));
    } else if (op == "clean_storage_default") {
      send_request(make_tl_object<td_api::optimizeStorage>());
    } else if (op == "clean_photos") {
      std::vector<tl_object_ptr<td_api::FileType>> types;
      types.push_back(make_tl_object<td_api::fileTypePhoto>());
      send_request(
          make_tl_object<td_api::optimizeStorage>(0, 0, 0, 0, std::move(types), as_chat_ids(""), as_chat_ids(""), 20));
    } else if (op == "clean_storage") {
      std::vector<tl_object_ptr<td_api::FileType>> types;
      types.push_back(make_tl_object<td_api::fileTypeThumbnail>());
      types.push_back(make_tl_object<td_api::fileTypeProfilePhoto>());
      types.push_back(make_tl_object<td_api::fileTypePhoto>());
      types.push_back(make_tl_object<td_api::fileTypeVoiceNote>());
      types.push_back(make_tl_object<td_api::fileTypeVideo>());
      types.push_back(make_tl_object<td_api::fileTypeDocument>());
      types.push_back(make_tl_object<td_api::fileTypeSecret>());
      types.push_back(make_tl_object<td_api::fileTypeUnknown>());
      types.push_back(make_tl_object<td_api::fileTypeSticker>());
      types.push_back(make_tl_object<td_api::fileTypeAudio>());
      types.push_back(make_tl_object<td_api::fileTypeAnimation>());
      types.push_back(make_tl_object<td_api::fileTypeVideoNote>());
      types.push_back(make_tl_object<td_api::fileTypeSecure>());
      send_request(make_tl_object<td_api::optimizeStorage>(0, -1, -1, 0, std::move(types), as_chat_ids(args, ','),
                                                           as_chat_ids(""), 20));
    } else if (op == "network") {
      send_request(make_tl_object<td_api::getNetworkStatistics>());
    } else if (op == "current_network") {
      send_request(make_tl_object<td_api::getNetworkStatistics>(true));
    } else if (op == "reset_network") {
      send_request(make_tl_object<td_api::resetNetworkStatistics>());
    } else if (op == "snt") {
      send_request(make_tl_object<td_api::setNetworkType>(get_network_type(args)));
    } else if (op == "ansc") {
      string sent_bytes;
      string received_bytes;
      string duration;
      string network_type;
      std::tie(sent_bytes, args) = split(args);
      std::tie(received_bytes, args) = split(args);
      std::tie(duration, network_type) = split(args);
      send_request(make_tl_object<td_api::addNetworkStatistics>(make_tl_object<td_api::networkStatisticsEntryCall>(
          get_network_type(network_type), to_integer<int32>(sent_bytes), to_integer<int32>(received_bytes),
          to_double(duration))));
    } else if (op == "ans") {
      string sent_bytes;
      string received_bytes;
      string network_type;
      std::tie(sent_bytes, args) = split(args);
      std::tie(received_bytes, network_type) = split(args);
      send_request(make_tl_object<td_api::addNetworkStatistics>(make_tl_object<td_api::networkStatisticsEntryFile>(
          make_tl_object<td_api::fileTypeDocument>(), get_network_type(network_type), to_integer<int32>(sent_bytes),
          to_integer<int32>(received_bytes))));
    } else if (op == "top_chats") {
      send_request(make_tl_object<td_api::getTopChats>(get_top_chat_category(args), 50));
    } else if (op == "rtc") {
      string chat_id;
      string category;
      std::tie(chat_id, category) = split(args);

      send_request(make_tl_object<td_api::removeTopChat>(get_top_chat_category(category), as_chat_id(chat_id)));
    } else if (op == "sss") {
      send_request(make_tl_object<td_api::searchStickerSet>(args));
    } else if (op == "siss") {
      send_request(make_tl_object<td_api::searchInstalledStickerSets>(false, args, 2));
    } else if (op == "ssss") {
      send_request(make_tl_object<td_api::searchStickerSets>(args));
    } else if (op == "css") {
      string set_id;
      string is_installed;
      string is_archived;

      std::tie(set_id, args) = split(args);
      std::tie(is_installed, is_archived) = split(args);

      send_request(make_tl_object<td_api::changeStickerSet>(to_integer<int64>(set_id), as_bool(is_installed),
                                                            as_bool(is_archived)));
    } else if (op == "vtss") {
      send_request(make_tl_object<td_api::viewTrendingStickerSets>(to_integers<int64>(args)));
    } else if (op == "riss") {
      string is_masks;
      string new_order;

      std::tie(is_masks, new_order) = split(args);

      send_request(
          make_tl_object<td_api::reorderInstalledStickerSets>(as_bool(is_masks), to_integers<int64>(new_order)));
    } else if (op == "grs") {
      send_request(make_tl_object<td_api::getRecentStickers>(as_bool(args)));
    } else if (op == "ars") {
      string is_attached;
      string sticker_id;

      std::tie(is_attached, sticker_id) = split(args);

      send_request(make_tl_object<td_api::addRecentSticker>(as_bool(is_attached), as_input_file_id(sticker_id)));
    } else if (op == "rrs") {
      string is_attached;
      string sticker_id;

      std::tie(is_attached, sticker_id) = split(args);

      send_request(make_tl_object<td_api::removeRecentSticker>(as_bool(is_attached), as_input_file_id(sticker_id)));
    } else if (op == "gfs") {
      send_request(make_tl_object<td_api::getFavoriteStickers>());
    } else if (op == "afs") {
      send_request(make_tl_object<td_api::addFavoriteSticker>(as_input_file_id(args)));
    } else if (op == "rfs") {
      send_request(make_tl_object<td_api::removeFavoriteSticker>(as_input_file_id(args)));
    } else if (op == "crs") {
      send_request(make_tl_object<td_api::clearRecentStickers>(as_bool(args)));
    } else if (op == "gse") {
      send_request(make_tl_object<td_api::getStickerEmojis>(as_input_file_id(args)));
    } else {
      op_not_found_count++;
    }

    if (op == "gsan") {
      send_request(make_tl_object<td_api::getSavedAnimations>());
    } else if (op == "asan") {
      send_request(make_tl_object<td_api::addSavedAnimation>(as_input_file_id(args)));
    } else if (op == "rsan") {
      send_request(make_tl_object<td_api::removeSavedAnimation>(as_input_file_id(args)));
    } else if (op == "guf") {
      send_request(make_tl_object<td_api::getUserFullInfo>(as_user_id(args)));
    } else if (op == "gbg") {
      send_request(make_tl_object<td_api::getBasicGroup>(to_integer<int32>(args)));
    } else if (op == "gbgf") {
      send_request(make_tl_object<td_api::getBasicGroupFullInfo>(to_integer<int32>(args)));
    } else if (op == "gsg" || op == "gch") {
      send_request(make_tl_object<td_api::getSupergroup>(to_integer<int32>(args)));
    } else if (op == "gsgf" || op == "gchf") {
      send_request(make_tl_object<td_api::getSupergroupFullInfo>(to_integer<int32>(args)));
    } else if (op == "gsc") {
      send_request(make_tl_object<td_api::getSecretChat>(to_integer<int32>(args)));
    } else if (op == "scm") {
      string chat_id;
      string limit;
      string query;
      string filter;

      std::tie(chat_id, args) = split(args);
      std::tie(limit, args) = split(args);
      std::tie(query, filter) = split(args);
      send_request(make_tl_object<td_api::searchChatMembers>(as_chat_id(chat_id), query, to_integer<int32>(limit),
                                                             get_chat_members_filter(filter)));
    } else if (op == "gcm") {
      string chat_id;
      string user_id;

      std::tie(chat_id, user_id) = split(args);
      send_request(make_tl_object<td_api::getChatMember>(as_chat_id(chat_id), as_user_id(user_id)));
    } else if (op == "GetSupergroupAdministrators") {
      string supergroup_id;
      string offset;
      string limit;

      std::tie(supergroup_id, args) = split(args);
      std::tie(offset, limit) = split(args);
      if (offset.empty()) {
        offset = "0";
      }
      if (limit.empty()) {
        limit = "10";
      }
      send_request(make_tl_object<td_api::getSupergroupMembers>(
          to_integer<int32>(supergroup_id), make_tl_object<td_api::supergroupMembersFilterAdministrators>(),
          to_integer<int32>(offset), to_integer<int32>(limit)));
    } else if (op == "GetChatAdministrators") {
      string chat_id = args;
      send_request(make_tl_object<td_api::getChatAdministrators>(as_chat_id(chat_id)));
    } else if (op == "GetSupergroupBanned") {
      string supergroup_id;
      string query;
      string offset;
      string limit;

      std::tie(supergroup_id, args) = split(args);
      std::tie(query, args) = split(args);
      std::tie(offset, limit) = split(args);
      if (offset.empty()) {
        offset = "0";
      }
      if (limit.empty()) {
        limit = "10";
      }
      send_request(make_tl_object<td_api::getSupergroupMembers>(
          to_integer<int32>(supergroup_id), make_tl_object<td_api::supergroupMembersFilterBanned>(query),
          to_integer<int32>(offset), to_integer<int32>(limit)));
    } else if (op == "GetSupergroupBots") {
      string supergroup_id;
      string offset;
      string limit;

      std::tie(supergroup_id, args) = split(args);
      std::tie(offset, limit) = split(args);
      if (offset.empty()) {
        offset = "0";
      }
      if (limit.empty()) {
        limit = "10";
      }
      send_request(make_tl_object<td_api::getSupergroupMembers>(to_integer<int32>(supergroup_id),
                                                                make_tl_object<td_api::supergroupMembersFilterBots>(),
                                                                to_integer<int32>(offset), to_integer<int32>(limit)));
    } else if (op == "GetSupergroupMembers") {
      string supergroup_id;
      string offset;
      string limit;

      std::tie(supergroup_id, args) = split(args);
      std::tie(offset, limit) = split(args);
      if (offset.empty()) {
        offset = "0";
      }
      if (limit.empty()) {
        limit = "10";
      }
      send_request(make_tl_object<td_api::getSupergroupMembers>(to_integer<int32>(supergroup_id),
                                                                make_tl_object<td_api::supergroupMembersFilterRecent>(),
                                                                to_integer<int32>(offset), to_integer<int32>(limit)));
    } else if (op == "SearchSupergroupMembers") {
      string supergroup_id;
      string query;
      string offset;
      string limit;

      std::tie(supergroup_id, args) = split(args);
      std::tie(query, args) = split(args);
      std::tie(offset, limit) = split(args);
      if (offset.empty()) {
        offset = "0";
      }
      if (limit.empty()) {
        limit = "10";
      }
      send_request(make_tl_object<td_api::getSupergroupMembers>(
          to_integer<int32>(supergroup_id), make_tl_object<td_api::supergroupMembersFilterSearch>(query),
          to_integer<int32>(offset), to_integer<int32>(limit)));
    } else if (op == "GetSupergroupRestricted") {
      string supergroup_id;
      string query;
      string offset;
      string limit;

      std::tie(supergroup_id, args) = split(args);
      std::tie(query, args) = split(args);
      std::tie(offset, limit) = split(args);
      if (offset.empty()) {
        offset = "0";
      }
      if (limit.empty()) {
        limit = "10";
      }
      send_request(make_tl_object<td_api::getSupergroupMembers>(
          to_integer<int32>(supergroup_id), make_tl_object<td_api::supergroupMembersFilterRestricted>(query),
          to_integer<int32>(offset), to_integer<int32>(limit)));
    } else if (op == "gdialog" || op == "gd") {
      send_request(make_tl_object<td_api::getChat>(as_chat_id(args)));
    } else if (op == "open") {
      send_request(make_tl_object<td_api::openChat>(as_chat_id(args)));
    } else if (op == "close") {
      send_request(make_tl_object<td_api::closeChat>(as_chat_id(args)));
    } else if (op == "gm") {
      string chat_id;
      string message_id;
      std::tie(chat_id, message_id) = split(args);
      send_request(make_tl_object<td_api::getMessage>(as_chat_id(chat_id), as_message_id(message_id)));
    } else if (op == "grm") {
      string chat_id;
      string message_id;
      std::tie(chat_id, message_id) = split(args);
      send_request(make_tl_object<td_api::getRepliedMessage>(as_chat_id(chat_id), as_message_id(message_id)));
    } else if (op == "gcpm") {
      string chat_id = args;
      send_request(make_tl_object<td_api::getChatPinnedMessage>(as_chat_id(chat_id)));
    } else if (op == "gms") {
      string chat_id;
      string message_ids;
      std::tie(chat_id, message_ids) = split(args);
      send_request(make_tl_object<td_api::getMessages>(as_chat_id(chat_id), as_message_ids(message_ids)));
    } else if (op == "gpml") {
      string chat_id;
      string message_id;
      string for_album;
      std::tie(chat_id, args) = split(args);
      std::tie(message_id, for_album) = split(args);
      send_request(make_tl_object<td_api::getPublicMessageLink>(as_chat_id(chat_id), as_message_id(message_id),
                                                                as_bool(for_album)));
    } else if (op == "gcmbd") {
      string chat_id;
      string date;
      std::tie(chat_id, date) = split(args);
      send_request(make_tl_object<td_api::getChatMessageByDate>(as_chat_id(chat_id), to_integer<int32>(date)));
    } else if (op == "gf" || op == "GetFile") {
      send_request(make_tl_object<td_api::getFile>(as_file_id(args)));
    } else if (op == "grf") {
      send_request(make_tl_object<td_api::getRemoteFile>(args, nullptr));
    } else if (op == "gmtf") {
      string latitude;
      string longitude;
      string zoom;
      string width;
      string height;
      string scale;
      string chat_id;
      std::tie(latitude, args) = split(args);
      std::tie(longitude, args) = split(args);
      std::tie(zoom, args) = split(args);
      std::tie(width, args) = split(args);
      std::tie(height, args) = split(args);
      std::tie(scale, chat_id) = split(args);

      send_request(make_tl_object<td_api::getMapThumbnailFile>(
          as_location(latitude, longitude), to_integer<int32>(zoom), to_integer<int32>(width),
          to_integer<int32>(height), to_integer<int32>(scale), as_chat_id(chat_id)));
    } else if (op == "df" || op == "DownloadFile") {
      string file_id_str;
      string priority;
      std::tie(file_id_str, priority) = split(args);
      if (priority.empty()) {
        priority = "1";
      }

      auto file_id = as_file_id(file_id_str);
      send_request(make_tl_object<td_api::downloadFile>(file_id, to_integer<int32>(priority)));
    } else if (op == "dff") {
      string file_id;
      string priority;
      std::tie(file_id, priority) = split(args);
      if (priority.empty()) {
        priority = "1";
      }

      for (int i = 1; i <= as_file_id(file_id); i++) {
        send_request(make_tl_object<td_api::downloadFile>(i, to_integer<int32>(priority)));
      }
    } else if (op == "cdf") {
      send_request(make_tl_object<td_api::cancelDownloadFile>(as_file_id(args), false));
    } else if (op == "uf" || op == "ufs" || op == "ufse") {
      string file_path;
      string priority;
      std::tie(file_path, priority) = split(args);
      if (priority.empty()) {
        priority = "1";
      }

      td_api::object_ptr<td_api::FileType> type = make_tl_object<td_api::fileTypePhoto>();
      if (op == "ufs") {
        type = make_tl_object<td_api::fileTypeSecret>();
      }
      if (op == "ufse") {
        type = make_tl_object<td_api::fileTypeSecure>();
      }

      send_request(
          make_tl_object<td_api::uploadFile>(as_local_file(file_path), std::move(type), to_integer<int32>(priority)));
    } else if (op == "ufg") {
      string file_path;
      string conversion;
      std::tie(file_path, conversion) = split(args);
      send_request(make_tl_object<td_api::uploadFile>(as_generated_file(file_path, conversion),
                                                      make_tl_object<td_api::fileTypePhoto>(), 1));
    } else if (op == "cuf") {
      send_request(make_tl_object<td_api::cancelUploadFile>(as_file_id(args)));
    } else if (op == "delf" || op == "DeleteFile") {
      string file_id = args;
      send_request(make_tl_object<td_api::deleteFile>(as_file_id(file_id)));
    } else if (op == "dm") {
      string chat_id;
      string message_ids;
      string revoke;
      std::tie(chat_id, args) = split(args);
      std::tie(message_ids, revoke) = split(args);

      send_request(make_tl_object<td_api::deleteMessages>(as_chat_id(chat_id), as_message_ids(message_ids, ','),
                                                          as_bool(revoke)));
    } else if (op == "fm" || op == "fmg") {
      string chat_id;
      string from_chat_id;
      string message_ids;
      std::tie(chat_id, args) = split(args);
      std::tie(from_chat_id, message_ids) = split(args);

      auto chat = as_chat_id(chat_id);
      send_request(make_tl_object<td_api::forwardMessages>(chat, as_chat_id(from_chat_id), as_message_ids(message_ids),
                                                           false, false, op == "fmg"));
    } else if (op == "csc" || op == "CreateSecretChat") {
      send_request(make_tl_object<td_api::createSecretChat>(to_integer<int32>(args)));
    } else if (op == "cnsc" || op == "CreateNewSecretChat") {
      send_request(make_tl_object<td_api::createNewSecretChat>(as_user_id(args)));
    } else if (op == "scstn") {
      send_request(make_tl_object<td_api::sendChatScreenshotTakenNotification>(as_chat_id(args)));
    } else if (op == "sscttl" || op == "setSecretChatTtl") {
      string chat_id;
      string ttl;
      std::tie(chat_id, ttl) = split(args);

      send_request(make_tl_object<td_api::sendChatSetTtlMessage>(as_chat_id(chat_id), to_integer<int32>(ttl)));
    } else if (op == "closeSC" || op == "cancelSC") {
      send_request(make_tl_object<td_api::closeSecretChat>(to_integer<int32>(args)));
    } else if (op == "cc" || op == "CreateCall") {
      send_request(make_tl_object<td_api::createCall>(as_user_id(args),
                                                      make_tl_object<td_api::callProtocol>(true, true, 65, 65)));
    } else if (op == "dc" || op == "DiscardCall") {
      string call_id;
      string is_disconnected;
      std::tie(call_id, is_disconnected) = split(args);

      send_request(make_tl_object<td_api::discardCall>(as_call_id(call_id), as_bool(is_disconnected), 0, 0));
    } else if (op == "ac" || op == "AcceptCall") {
      send_request(make_tl_object<td_api::acceptCall>(as_call_id(args),
                                                      make_tl_object<td_api::callProtocol>(true, true, 65, 65)));
    } else if (op == "scr" || op == "SendCallRating") {
      send_request(make_tl_object<td_api::sendCallRating>(as_call_id(args), 5, "Wow, such good call! (TDLib test)"));
    } else if (op == "scdi" || op == "SendCallDebugInformation") {
      send_request(make_tl_object<td_api::sendCallDebugInformation>(as_call_id(args), "{}"));
    } else if (op == "gcil") {
      send_request(make_tl_object<td_api::generateChatInviteLink>(as_chat_id(args)));
    } else if (op == "ccil") {
      send_request(make_tl_object<td_api::checkChatInviteLink>(args));
    } else if (op == "jcbil") {
      send_request(make_tl_object<td_api::joinChatByInviteLink>(args));
    } else if (op == "gte") {
      send_request(make_tl_object<td_api::getTextEntities>(args));
    } else if (op == "gtes") {
      execute(make_tl_object<td_api::getTextEntities>(args));
    } else if (op == "pte") {
      send_request(make_tl_object<td_api::parseTextEntities>(args, make_tl_object<td_api::textParseModeMarkdown>()));
    } else if (op == "ptes") {
      execute(make_tl_object<td_api::parseTextEntities>(args, make_tl_object<td_api::textParseModeMarkdown>()));
    } else if (op == "gfmt") {
      send_request(make_tl_object<td_api::getFileMimeType>(trim(args)));
    } else if (op == "gfe") {
      send_request(make_tl_object<td_api::getFileExtension>(trim(args)));
    } else if (op == "cfn") {
      send_request(make_tl_object<td_api::cleanFileName>(args));
    } else {
      op_not_found_count++;
    }

    if (op == "scdm") {
      string chat_id;
      string reply_to_message_id;
      string message;
      std::tie(chat_id, args) = split(args);
      std::tie(reply_to_message_id, message) = split(args);
      tl_object_ptr<td_api::draftMessage> draft_message;
      if (!reply_to_message_id.empty() || !message.empty()) {
        vector<tl_object_ptr<td_api::textEntity>> entities;
        entities.push_back(make_tl_object<td_api::textEntity>(0, 1, make_tl_object<td_api::textEntityTypePre>()));

        draft_message = make_tl_object<td_api::draftMessage>(
            as_message_id(reply_to_message_id),
            make_tl_object<td_api::inputMessageText>(as_formatted_text(message, std::move(entities)), true, false));
      }
      send_request(make_tl_object<td_api::setChatDraftMessage>(as_chat_id(chat_id), std::move(draft_message)));
    } else if (op == "cadm") {
      send_request(make_tl_object<td_api::clearAllDraftMessages>());
    } else if (op == "tcip") {
      string chat_id;
      string is_pinned;
      std::tie(chat_id, is_pinned) = split(args);
      send_request(make_tl_object<td_api::toggleChatIsPinned>(as_chat_id(chat_id), as_bool(is_pinned)));
    } else if (op == "tcimar") {
      string chat_id;
      string is_marked_as_read;
      std::tie(chat_id, is_marked_as_read) = split(args);
      send_request(make_tl_object<td_api::toggleChatIsMarkedAsUnread>(as_chat_id(chat_id), as_bool(is_marked_as_read)));
    } else if (op == "tcddn") {
      string chat_id;
      string default_disable_notification;
      std::tie(chat_id, default_disable_notification) = split(args);
      send_request(make_tl_object<td_api::toggleChatDefaultDisableNotification>(as_chat_id(chat_id),
                                                                                as_bool(default_disable_notification)));
    } else if (op == "spchats") {
      vector<string> chat_ids_str = full_split(args, ' ');
      vector<int64> chat_ids;
      for (auto &chat_id_str : chat_ids_str) {
        chat_ids.push_back(as_chat_id(chat_id_str));
      }
      send_request(make_tl_object<td_api::setPinnedChats>(std::move(chat_ids)));
    } else if (op == "sca") {
      string chat_id;
      string action;
      std::tie(chat_id, action) = split(args);
      send_request(make_tl_object<td_api::sendChatAction>(as_chat_id(chat_id), get_chat_action(action)));
    } else if (op == "smt" || op == "smtp" || op == "smtf" || op == "smtpf") {
      const string &chat_id = args;
      for (int i = 1; i <= 200; i++) {
        string message = PSTRING() << "#" << i;
        if (i == 6 || (op.back() == 'f' && i % 2 == 0)) {
          message = string(4097, 'a');
        }
        if (op[3] == 'p') {
          send_message(chat_id, make_tl_object<td_api::inputMessagePhoto>(as_local_file("rgb.jpg"), nullptr, Auto(), 0,
                                                                          0, as_caption(message), 0));
        } else {
          send_message(chat_id, make_tl_object<td_api::inputMessageText>(as_formatted_text(message), false, true));
        }
      }
    } else if (op == "ssm") {
      string chat_id;
      string from_search_id;
      string limit;
      string filter;
      string query;

      std::tie(chat_id, args) = split(args);
      std::tie(from_search_id, args) = split(args);
      std::tie(limit, args) = split(args);
      std::tie(filter, query) = split(args);

      send_request(
          make_tl_object<td_api::searchSecretMessages>(as_chat_id(chat_id), query, to_integer<int64>(from_search_id),
                                                       to_integer<int32>(limit), get_search_messages_filter(filter)));
    } else if (op == "sm" || op == "sms" || op == "smr" || op == "smf") {
      string chat_id;
      string reply_to_message_id;
      string message;

      std::tie(chat_id, message) = split(args);
      if (op == "smr") {
        std::tie(reply_to_message_id, message) = split(message);
      }
      if (op == "smf") {
        message = string(5097, 'a');
      }

      send_message(chat_id, make_tl_object<td_api::inputMessageText>(as_formatted_text(message), false, true),
                   op == "sms", false, as_message_id(reply_to_message_id));
    } else if (op == "alm" || op == "almr") {
      string chat_id;
      string user_id;
      string reply_to_message_id;
      string message;

      std::tie(chat_id, args) = split(args);
      std::tie(user_id, message) = split(args);
      if (op == "almr") {
        std::tie(reply_to_message_id, message) = split(message);
      }

      send_request(make_tl_object<td_api::addLocalMessage>(
          as_chat_id(chat_id), as_user_id(user_id), as_message_id(reply_to_message_id), false,
          make_tl_object<td_api::inputMessageText>(as_formatted_text(message), false, true)));
    } else if (op == "smap" || op == "smapr") {
      string chat_id;
      string reply_to_message_id;
      vector<string> photos;

      std::tie(chat_id, args) = split(args);
      if (op == "smapr") {
        std::tie(reply_to_message_id, args) = split(args);
      }
      photos = full_split(args);

      send_request(make_tl_object<td_api::sendMessageAlbum>(
          as_chat_id(chat_id), as_message_id(reply_to_message_id), false, false,
          transform(photos, [](const string &photo_path) {
            tl_object_ptr<td_api::InputMessageContent> content = make_tl_object<td_api::inputMessagePhoto>(
                as_local_file(photo_path), nullptr, Auto(), 0, 0, as_caption(""), 0);
            return content;
          })));
    } else if (op == "em") {
      string chat_id;
      string message_id;
      string message;
      std::tie(chat_id, args) = split(args);
      std::tie(message_id, message) = split(args);
      send_request(make_tl_object<td_api::editMessageText>(
          as_chat_id(chat_id), as_message_id(message_id), nullptr,
          make_tl_object<td_api::inputMessageText>(as_formatted_text(message), true, true)));
    } else if (op == "eman") {
      string chat_id;
      string message_id;
      string animation;
      std::tie(chat_id, args) = split(args);
      std::tie(message_id, animation) = split(args);
      send_request(make_tl_object<td_api::editMessageMedia>(
          as_chat_id(chat_id), as_message_id(message_id), nullptr,
          make_tl_object<td_api::inputMessageAnimation>(as_input_file(animation), nullptr, 0, 0, 0,
                                                        as_caption("animation"))));
    } else if (op == "emc") {
      string chat_id;
      string message_id;
      string caption;
      std::tie(chat_id, args) = split(args);
      std::tie(message_id, caption) = split(args);
      send_request(make_tl_object<td_api::editMessageCaption>(as_chat_id(chat_id), as_message_id(message_id), nullptr,
                                                              as_caption(caption)));
    } else if (op == "emd") {
      string chat_id;
      string message_id;
      string document;
      std::tie(chat_id, args) = split(args);
      std::tie(message_id, document) = split(args);
      send_request(make_tl_object<td_api::editMessageMedia>(
          as_chat_id(chat_id), as_message_id(message_id), nullptr,
          make_tl_object<td_api::inputMessageDocument>(as_input_file(document), nullptr, as_caption(""))));
    } else if (op == "emp") {
      string chat_id;
      string message_id;
      string photo;
      std::tie(chat_id, args) = split(args);
      std::tie(message_id, photo) = split(args);
      send_request(make_tl_object<td_api::editMessageMedia>(
          as_chat_id(chat_id), as_message_id(message_id), nullptr,
          make_tl_object<td_api::inputMessagePhoto>(as_input_file(photo), as_input_thumbnail(as_input_file(photo)),
                                                    Auto(), 0, 0, as_caption(""), 0)));
    } else if (op == "empttl") {
      string chat_id;
      string message_id;
      string photo;
      std::tie(chat_id, args) = split(args);
      std::tie(message_id, photo) = split(args);
      send_request(make_tl_object<td_api::editMessageMedia>(
          as_chat_id(chat_id), as_message_id(message_id), nullptr,
          make_tl_object<td_api::inputMessagePhoto>(as_input_file(photo), as_input_thumbnail(as_input_file(photo)),
                                                    Auto(), 0, 0, as_caption(""), 10)));
    } else if (op == "emvt") {
      string chat_id;
      string message_id;
      string video;
      string thumbnail;
      std::tie(chat_id, args) = split(args);
      std::tie(message_id, args) = split(args);
      std::tie(video, thumbnail) = split(args);
      send_request(make_tl_object<td_api::editMessageMedia>(
          as_chat_id(chat_id), as_message_id(message_id), nullptr,
          make_tl_object<td_api::inputMessageVideo>(as_input_file(video), as_input_thumbnail(as_input_file(thumbnail)),
                                                    Auto(), 1, 2, 3, true, as_caption(""), 0)));
    } else if (op == "emll") {
      string chat_id;
      string message_id;
      string latitude;
      string longitude;
      std::tie(chat_id, args) = split(args);
      std::tie(message_id, args) = split(args);
      std::tie(latitude, longitude) = split(args);
      send_request(make_tl_object<td_api::editMessageLiveLocation>(as_chat_id(chat_id), as_message_id(message_id),
                                                                   nullptr, as_location(latitude, longitude)));
    } else if (op == "gallm") {
      send_request(make_tl_object<td_api::getActiveLiveLocationMessages>());
    } else if (op == "sbsm") {
      string bot_id;
      string chat_id;
      string parameter;
      std::tie(bot_id, args) = split(args);
      std::tie(chat_id, parameter) = split(args);
      send_request(make_tl_object<td_api::sendBotStartMessage>(as_user_id(bot_id), as_chat_id(chat_id), parameter));
    } else if (op == "giqr") {
      string bot_id;
      string query;
      std::tie(bot_id, query) = split(args);
      send_request(make_tl_object<td_api::getInlineQueryResults>(as_user_id(bot_id), 0, nullptr, query, ""));
    } else if (op == "giqro") {
      string bot_id;
      string offset;
      string query;
      std::tie(bot_id, args) = split(args);
      std::tie(offset, query) = split(args);
      send_request(make_tl_object<td_api::getInlineQueryResults>(as_user_id(bot_id), 0, nullptr, query, offset));
    } else if (op == "giqrl") {
      string bot_id;
      string query;
      std::tie(bot_id, query) = split(args);
      send_request(
          make_tl_object<td_api::getInlineQueryResults>(as_user_id(bot_id), 0, as_location("1.1", "2.2"), query, ""));
    } else if (op == "siqr") {
      string chat_id;
      string query_id;
      string result_id;
      std::tie(chat_id, args) = split(args);
      std::tie(query_id, result_id) = split(args);

      auto chat = as_chat_id(chat_id);
      send_request(make_tl_object<td_api::sendInlineQueryResultMessage>(chat, 0, false, false,
                                                                        to_integer<int64>(query_id), result_id));
    } else if (op == "gcqr") {
      string chat_id;
      string message_id;
      string data;
      std::tie(chat_id, args) = split(args);
      std::tie(message_id, data) = split(args);
      send_request(make_tl_object<td_api::getCallbackQueryAnswer>(
          as_chat_id(chat_id), as_message_id(message_id), make_tl_object<td_api::callbackQueryPayloadData>(data)));
    } else if (op == "gcgqr") {
      string chat_id;
      string message_id;
      std::tie(chat_id, message_id) = split(args);
      send_request(make_tl_object<td_api::getCallbackQueryAnswer>(
          as_chat_id(chat_id), as_message_id(message_id), make_tl_object<td_api::callbackQueryPayloadGame>("")));
    } else if (op == "san") {
      string chat_id;
      string animation_path;
      string width;
      string height;
      string caption;
      std::tie(chat_id, args) = split(args);
      std::tie(animation_path, args) = split(args);
      std::tie(width, args) = split(args);
      std::tie(height, caption) = split(args);

      send_message(chat_id, make_tl_object<td_api::inputMessageAnimation>(
                                as_local_file(animation_path), nullptr, 60, to_integer<int32>(width),
                                to_integer<int32>(height), as_caption(caption)));
    } else if (op == "sang") {
      string chat_id;
      string animation_path;
      string animation_conversion;
      std::tie(chat_id, args) = split(args);
      std::tie(animation_path, animation_conversion) = split(args);
      send_message(chat_id,
                   make_tl_object<td_api::inputMessageAnimation>(
                       as_generated_file(animation_path, animation_conversion), nullptr, 60, 0, 0, as_caption("")));
    } else if (op == "sanid") {
      string chat_id;
      string file_id;
      std::tie(chat_id, file_id) = split(args);

      send_message(chat_id, make_tl_object<td_api::inputMessageAnimation>(as_input_file_id(file_id), nullptr, 0, 0, 0,
                                                                          as_caption("")));
    } else if (op == "sanurl") {
      string chat_id;
      string url;
      std::tie(chat_id, url) = split(args);

      send_message(chat_id, make_tl_object<td_api::inputMessageAnimation>(as_generated_file(url, "#url#"), nullptr, 0,
                                                                          0, 0, as_caption("")));
    } else if (op == "sanurl2") {
      string chat_id;
      string url;
      std::tie(chat_id, url) = split(args);

      send_message(chat_id, make_tl_object<td_api::inputMessageAnimation>(as_remote_file(url), nullptr, 0, 0, 0,
                                                                          as_caption("")));
    } else if (op == "sau") {
      string chat_id;
      string audio_path;
      string duration;
      string title;
      string performer;
      std::tie(chat_id, args) = split(args);
      std::tie(audio_path, args) = split(args);
      std::tie(duration, args) = split(args);
      std::tie(title, performer) = split(args);

      send_message(chat_id, make_tl_object<td_api::inputMessageAudio>(as_local_file(audio_path), nullptr,
                                                                      to_integer<int32>(duration), title, performer,
                                                                      as_caption("audio caption")));
    } else if (op == "svoice") {
      string chat_id;
      string voice_path;
      std::tie(chat_id, voice_path) = split(args);

      send_message(chat_id, make_tl_object<td_api::inputMessageVoiceNote>(as_local_file(voice_path), 0, "abacaba",
                                                                          as_caption("voice caption")));
    } else if (op == "SendContact" || op == "scontact") {
      string chat_id;
      string phone_number;
      string first_name;
      string last_name;
      string user_id;
      std::tie(chat_id, args) = split(args);
      std::tie(phone_number, args) = split(args);
      std::tie(first_name, args) = split(args);
      std::tie(last_name, user_id) = split(args);

      send_message(chat_id, make_tl_object<td_api::inputMessageContact>(make_tl_object<td_api::contact>(
                                phone_number, first_name, last_name, string(), as_user_id(user_id))));
    } else if (op == "sf") {
      string chat_id;
      string from_chat_id;
      string from_message_id;
      std::tie(chat_id, args) = split(args);
      std::tie(from_chat_id, from_message_id) = split(args);

      send_message(chat_id, make_tl_object<td_api::inputMessageForwarded>(as_chat_id(from_chat_id),
                                                                          as_message_id(from_message_id), true));
    } else if (op == "sd") {
      string chat_id;
      string document_path;
      std::tie(chat_id, document_path) = split(args);
      send_message(chat_id, make_tl_object<td_api::inputMessageDocument>(
                                as_local_file(document_path), nullptr,
                                as_caption(u8"\u1680\u180Etest \u180E\n\u180E\n\u180E\n cap\ttion\u180E\u180E")));
    } else if (op == "sdt") {
      string chat_id;
      string document_path;
      string thumbnail_path;
      std::tie(chat_id, args) = split(args);
      std::tie(document_path, thumbnail_path) = split(args);
      send_message(chat_id, make_tl_object<td_api::inputMessageDocument>(
                                as_local_file(document_path), as_input_thumbnail(as_local_file(thumbnail_path)),
                                as_caption("test caption")));
    } else if (op == "sdg") {
      string chat_id;
      string document_path;
      string document_conversion;
      std::tie(chat_id, args) = split(args);
      std::tie(document_path, document_conversion) = split(args);
      send_message(chat_id,
                   make_tl_object<td_api::inputMessageDocument>(as_generated_file(document_path, document_conversion),
                                                                nullptr, as_caption("test caption")));
    } else if (op == "sdtg") {
      string chat_id;
      string document_path;
      string thumbnail_path;
      string thumbnail_conversion;
      std::tie(chat_id, args) = split(args);
      std::tie(document_path, args) = split(args);
      std::tie(thumbnail_path, thumbnail_conversion) = split(args);
      send_message(chat_id, make_tl_object<td_api::inputMessageDocument>(
                                as_local_file(document_path),
                                as_input_thumbnail(as_generated_file(thumbnail_path, thumbnail_conversion)),
                                as_caption("test caption")));
    } else if (op == "sdgtg") {
      string chat_id;
      string document_path;
      string document_conversion;
      string thumbnail_path;
      string thumbnail_conversion;
      std::tie(chat_id, args) = split(args);
      std::tie(document_path, args) = split(args);
      std::tie(document_conversion, args) = split(args);
      std::tie(thumbnail_path, thumbnail_conversion) = split(args);
      send_message(chat_id, make_tl_object<td_api::inputMessageDocument>(
                                as_generated_file(document_path, document_conversion),
                                as_input_thumbnail(as_generated_file(thumbnail_path, thumbnail_conversion)),
                                as_caption("test caption")));
    } else if (op == "sdid") {
      string chat_id;
      string file_id;
      std::tie(chat_id, file_id) = split(args);
      send_message(chat_id,
                   make_tl_object<td_api::inputMessageDocument>(as_input_file_id(file_id), nullptr, as_caption("")));
    } else if (op == "sdurl") {
      string chat_id;
      string url;
      std::tie(chat_id, url) = split(args);

      send_message(chat_id, make_tl_object<td_api::inputMessageDocument>(as_remote_file(url), nullptr, as_caption("")));
    } else if (op == "sg") {
      string chat_id;
      string bot_user_id;
      string game_short_name;
      std::tie(chat_id, args) = split(args);
      std::tie(bot_user_id, game_short_name) = split(args);
      send_message(chat_id, make_tl_object<td_api::inputMessageGame>(as_user_id(bot_user_id), game_short_name));
    } else if (op == "sl") {
      string chat_id;
      std::tie(chat_id, args) = split(args);

      string latitude;
      string longitude;
      std::tie(latitude, longitude) = split(args);

      send_message(chat_id, make_tl_object<td_api::inputMessageLocation>(as_location(latitude, longitude), 0));
    } else if (op == "sll") {
      string chat_id;
      string period;
      string latitude;
      string longitude;
      std::tie(chat_id, args) = split(args);
      std::tie(period, args) = split(args);
      std::tie(latitude, longitude) = split(args);

      send_message(chat_id, make_tl_object<td_api::inputMessageLocation>(as_location(latitude, longitude),
                                                                         to_integer<int32>(period)));
    } else if (op == "sp") {
      string chat_id;
      string photo_path;
      string sticker_file_ids_str;
      vector<int32> sticker_file_ids;
      std::tie(chat_id, args) = split(args);
      std::tie(sticker_file_ids_str, photo_path) = split(args);
      if (trim(photo_path).empty()) {
        photo_path = sticker_file_ids_str;
      } else {
        sticker_file_ids = to_integers<int32>(sticker_file_ids_str, ',');
      }

      send_message(chat_id,
                   make_tl_object<td_api::inputMessagePhoto>(as_local_file(photo_path), nullptr,
                                                             std::move(sticker_file_ids), 0, 0, as_caption(""), 0));
    } else if (op == "spttl") {
      string chat_id;
      string photo_path;
      std::tie(chat_id, photo_path) = split(args);

      send_message(chat_id, make_tl_object<td_api::inputMessagePhoto>(as_local_file(photo_path), nullptr, Auto(), 0, 0,
                                                                      as_caption(""), 10));
    } else if (op == "spg") {
      string chat_id;
      string photo_path;
      string conversion;
      std::tie(chat_id, args) = split(args);
      std::tie(photo_path, conversion) = split(args);

      send_message(chat_id,
                   make_tl_object<td_api::inputMessagePhoto>(as_generated_file(photo_path, conversion), nullptr,
                                                             vector<int32>(), 0, 0, as_caption(""), 0));
    } else if (op == "spt") {
      string chat_id;
      string photo_path;
      string thumbnail_path;
      std::tie(chat_id, args) = split(args);
      std::tie(photo_path, thumbnail_path) = split(args);

      send_message(chat_id, make_tl_object<td_api::inputMessagePhoto>(
                                as_local_file(photo_path), as_input_thumbnail(as_local_file(thumbnail_path), 90, 89),
                                vector<int32>(), 0, 0, as_caption(""), 0));
    } else if (op == "sptg") {
      string chat_id;
      string photo_path;
      string thumbnail_path;
      string thumbnail_conversion;
      std::tie(chat_id, args) = split(args);
      std::tie(photo_path, args) = split(args);
      std::tie(thumbnail_path, thumbnail_conversion) = split(args);

      send_message(chat_id, make_tl_object<td_api::inputMessagePhoto>(
                                as_local_file(photo_path),
                                as_input_thumbnail(as_generated_file(thumbnail_path, thumbnail_conversion), 90, 89),
                                vector<int32>(), 0, 0, as_caption(""), 0));
    } else if (op == "spgtg") {
      string chat_id;
      string photo_path;
      string conversion;
      string thumbnail_path;
      string thumbnail_conversion;

      std::tie(chat_id, args) = split(args);
      std::tie(photo_path, args) = split(args);
      std::tie(conversion, args) = split(args);
      std::tie(thumbnail_path, thumbnail_conversion) = split(args);

      send_message(chat_id, make_tl_object<td_api::inputMessagePhoto>(
                                as_generated_file(photo_path, conversion),
                                as_input_thumbnail(as_generated_file(thumbnail_path, thumbnail_conversion), 90, 89),
                                vector<int32>(), 0, 0, as_caption(""), 0));
    } else if (op == "spid") {
      string chat_id;
      string file_id;
      std::tie(chat_id, file_id) = split(args);
      send_message(chat_id, make_tl_object<td_api::inputMessagePhoto>(as_input_file_id(file_id), nullptr,
                                                                      vector<int32>(), 0, 0, as_caption(""), 0));
    } else if (op == "ss") {
      string chat_id;
      string sticker_path;
      std::tie(chat_id, sticker_path) = split(args);

      send_message(chat_id, make_tl_object<td_api::inputMessageSticker>(as_local_file(sticker_path), nullptr, 0, 0));
    } else if (op == "sstt") {
      string chat_id;
      string sticker_path;
      string thumbnail_path;
      std::tie(chat_id, args) = split(args);
      std::tie(sticker_path, thumbnail_path) = split(args);

      send_message(chat_id, make_tl_object<td_api::inputMessageSticker>(
                                as_local_file(sticker_path), as_input_thumbnail(as_local_file(thumbnail_path)), 0, 0));
    } else if (op == "ssid") {
      string chat_id;
      string file_id;
      std::tie(chat_id, file_id) = split(args);

      send_message(chat_id, make_tl_object<td_api::inputMessageSticker>(as_input_file_id(file_id), nullptr, 0, 0));
    } else if (op == "sv") {
      string chat_id;
      string video_path;
      string sticker_file_ids_str;
      vector<int32> sticker_file_ids;
      std::tie(chat_id, args) = split(args);
      std::tie(sticker_file_ids_str, video_path) = split(args);
      if (trim(video_path).empty()) {
        video_path = sticker_file_ids_str;
      } else {
        sticker_file_ids = to_integers<int32>(sticker_file_ids_str, ',');
      }

      send_message(chat_id, make_tl_object<td_api::inputMessageVideo>(as_local_file(video_path), nullptr,
                                                                      std::move(sticker_file_ids), 1, 2, 3, true,
                                                                      as_caption(""), 0));
    } else if (op == "svn") {
      string chat_id;
      string video_path;
      std::tie(chat_id, video_path) = split(args);
      send_message(chat_id, make_tl_object<td_api::inputMessageVideoNote>(as_local_file(video_path), nullptr, 1, 5));
    } else if (op == "svenue") {
      string chat_id;

      string latitude;
      string longitude;
      string title;
      string address;
      string provider;
      string venue_id;
      string venue_type;
      std::tie(chat_id, args) = split(args);
      std::tie(latitude, args) = split(args);
      std::tie(longitude, args) = split(args);
      std::tie(title, args) = split(args);
      std::tie(address, args) = split(args);
      std::tie(provider, args) = split(args);
      std::tie(venue_id, venue_type) = split(args);

      send_message(chat_id, make_tl_object<td_api::inputMessageVenue>(make_tl_object<td_api::venue>(
                                as_location(latitude, longitude), title, address, provider, venue_id, venue_type)));
    } else if (op == "test") {
      send_request(make_tl_object<td_api::testNetwork>());
    } else if (op == "alarm") {
      send_request(make_tl_object<td_api::setAlarm>(to_double(args)));
    } else if (op == "delete") {
      string chat_id;
      string remove_from_the_chat_list;
      std::tie(chat_id, remove_from_the_chat_list) = split(args);
      send_request(make_tl_object<td_api::deleteChatHistory>(as_chat_id(chat_id), as_bool(remove_from_the_chat_list)));
    } else if (op == "dmfu") {
      string chat_id;
      string user_id;
      std::tie(chat_id, user_id) = split(args);
      send_request(make_tl_object<td_api::deleteChatMessagesFromUser>(as_chat_id(chat_id), as_user_id(user_id)));
    } else if (op == "cnbgc") {
      string user_ids_string;
      string title;
      std::tie(user_ids_string, title) = split(args);

      send_request(make_tl_object<td_api::createNewBasicGroupChat>(as_user_ids(user_ids_string, ','), title));
    } else if (op == "cnch") {
      send_request(make_tl_object<td_api::createNewSupergroupChat>(args, true, "Description"));
    } else if (op == "cnsg") {
      send_request(make_tl_object<td_api::createNewSupergroupChat>(args, false, "Description"));
    } else if (op == "UpgradeBasicGroupChatToSupergroupChat") {
      send_request(make_tl_object<td_api::upgradeBasicGroupChatToSupergroupChat>(as_chat_id(args)));
    } else if (op == "DeleteSupergroup") {
      send_request(make_tl_object<td_api::deleteSupergroup>(to_integer<int32>(args)));
    } else if (op == "gcpc") {
      send_request(make_tl_object<td_api::getCreatedPublicChats>());
    } else if (op == "cpc") {
      string user_id;
      string force;

      std::tie(user_id, force) = split(args);
      send_request(make_tl_object<td_api::createPrivateChat>(as_user_id(user_id), as_bool(force)));
    } else if (op == "cbgc") {
      string basic_group_id;
      string force;

      std::tie(basic_group_id, force) = split(args);
      send_request(make_tl_object<td_api::createBasicGroupChat>(to_integer<int32>(basic_group_id), as_bool(force)));
    } else if (op == "csgc" || op == "cchc") {
      string supergroup_id;
      string force;

      std::tie(supergroup_id, force) = split(args);
      send_request(make_tl_object<td_api::createSupergroupChat>(to_integer<int32>(supergroup_id), as_bool(force)));
    } else if (op == "sct") {
      string chat_id;
      string title;

      std::tie(chat_id, title) = split(args);
      send_request(make_tl_object<td_api::setChatTitle>(as_chat_id(chat_id), title));
    } else if (op == "scp") {
      string chat_id;
      string photo_path;

      std::tie(chat_id, photo_path) = split(args);
      send_request(make_tl_object<td_api::setChatPhoto>(as_chat_id(chat_id), as_local_file(photo_path)));
    } else if (op == "scpid") {
      string chat_id;
      string file_id;

      std::tie(chat_id, file_id) = split(args);
      send_request(make_tl_object<td_api::setChatPhoto>(as_chat_id(chat_id), as_input_file_id(file_id)));
    } else if (op == "sccd") {
      string chat_id;
      string client_data;

      std::tie(chat_id, client_data) = split(args);
      send_request(make_tl_object<td_api::setChatClientData>(as_chat_id(chat_id), client_data));
    } else if (op == "acm") {
      string chat_id;
      string user_id;
      string forward_limit;

      std::tie(chat_id, args) = split(args);
      std::tie(user_id, forward_limit) = split(args);
      send_request(make_tl_object<td_api::addChatMember>(as_chat_id(chat_id), as_user_id(user_id),
                                                         to_integer<int32>(forward_limit)));
    } else if (op == "acms") {
      string chat_id;
      string user_ids;

      std::tie(chat_id, user_ids) = split(args);
      send_request(make_tl_object<td_api::addChatMembers>(as_chat_id(chat_id), as_user_ids(user_ids, ',')));
    } else {
      op_not_found_count++;
    }

    if (op == "scms") {
      string chat_id;
      string user_id;
      string status_str;
      tl_object_ptr<td_api::ChatMemberStatus> status;

      std::tie(chat_id, args) = split(args);
      std::tie(user_id, status_str) = split(args);
      if (status_str == "member") {
        status = make_tl_object<td_api::chatMemberStatusMember>();
      } else if (status_str == "left") {
        status = make_tl_object<td_api::chatMemberStatusLeft>();
      } else if (status_str == "banned") {
        status = make_tl_object<td_api::chatMemberStatusBanned>(std::numeric_limits<int32>::max());
      } else if (status_str == "creator") {
        status = make_tl_object<td_api::chatMemberStatusCreator>(true);
      } else if (status_str == "uncreator") {
        status = make_tl_object<td_api::chatMemberStatusCreator>(false);
      } else if (status_str == "admin") {
        status =
            make_tl_object<td_api::chatMemberStatusAdministrator>(true, true, true, true, true, true, true, true, true);
      } else if (status_str == "unadmin") {
        status = make_tl_object<td_api::chatMemberStatusAdministrator>(true, false, false, false, false, false, false,
                                                                       false, false);
      } else if (status_str == "rest") {
        status = make_tl_object<td_api::chatMemberStatusRestricted>(true, static_cast<int32>(60 + std::time(nullptr)),
                                                                    false, false, false, false);
      } else if (status_str == "restkick") {
        status = make_tl_object<td_api::chatMemberStatusRestricted>(false, static_cast<int32>(60 + std::time(nullptr)),
                                                                    true, false, false, false);
      } else if (status_str == "unrest") {
        status = make_tl_object<td_api::chatMemberStatusRestricted>(true, 0, true, true, true, true);
      }
      if (status != nullptr) {
        send_request(
            make_tl_object<td_api::setChatMemberStatus>(as_chat_id(chat_id), as_user_id(user_id), std::move(status)));
      } else {
        LOG(ERROR) << "Unknown status \"" << status_str << "\"";
      }
    } else if (op == "log") {
      string chat_id;
      string limit;

      std::tie(chat_id, limit) = split(args);
      send_request(make_tl_object<td_api::getChatEventLog>(as_chat_id(chat_id), "", 0, to_integer<int32>(limit),
                                                           nullptr, vector<int32>()));
    } else if (op == "join") {
      send_request(make_tl_object<td_api::joinChat>(as_chat_id(args)));
    } else if (op == "leave") {
      send_request(make_tl_object<td_api::leaveChat>(as_chat_id(args)));
    } else if (op == "dcm") {
      string chat_id;
      string user_id_str;

      std::tie(chat_id, user_id_str) = split(args);
      auto user_id = as_user_id(user_id_str);
      td_api::object_ptr<td_api::ChatMemberStatus> status = make_tl_object<td_api::chatMemberStatusBanned>();
      if (user_id == my_id_) {
        status = make_tl_object<td_api::chatMemberStatusLeft>();
      }
      send_request(make_tl_object<td_api::setChatMemberStatus>(as_chat_id(chat_id), user_id, std::move(status)));
    } else if (op == "sn") {
      string first_name;
      string last_name;
      std::tie(first_name, last_name) = split(args);
      send_request(make_tl_object<td_api::setName>(first_name, last_name));
    } else if (op == "sb") {
      send_request(make_tl_object<td_api::setBio>("\n" + args + "\n" + args + "\n"));
    } else if (op == "sun") {
      send_request(make_tl_object<td_api::setUsername>(args));
    } else if (op == "tbga") {
      string group_id;
      string everyone_is_administrator;

      std::tie(group_id, everyone_is_administrator) = split(args);
      send_request(make_tl_object<td_api::toggleBasicGroupAdministrators>(to_integer<int32>(group_id),
                                                                          as_bool(everyone_is_administrator)));
    } else if (op == "ccun") {
      string chat_id;
      string username;

      std::tie(chat_id, username) = split(args);
      send_request(make_tl_object<td_api::checkChatUsername>(as_chat_id(chat_id), username));
    } else if (op == "ssgun" || op == "schun") {
      string supergroup_id;
      string username;

      std::tie(supergroup_id, username) = split(args);
      send_request(make_tl_object<td_api::setSupergroupUsername>(to_integer<int32>(supergroup_id), username));
    } else if (op == "ssgss") {
      string supergroup_id;
      string sticker_set_id;

      std::tie(supergroup_id, sticker_set_id) = split(args);
      send_request(make_tl_object<td_api::setSupergroupStickerSet>(to_integer<int32>(supergroup_id),
                                                                   to_integer<int64>(sticker_set_id)));
    } else if (op == "tsgi") {
      string supergroup_id;
      string anyone_can_invite;

      std::tie(supergroup_id, anyone_can_invite) = split(args);
      send_request(make_tl_object<td_api::toggleSupergroupInvites>(to_integer<int32>(supergroup_id),
                                                                   as_bool(anyone_can_invite)));
    } else if (op == "tsgp") {
      string supergroup_id;
      string is_all_history_available;

      std::tie(supergroup_id, is_all_history_available) = split(args);
      send_request(make_tl_object<td_api::toggleSupergroupIsAllHistoryAvailable>(to_integer<int32>(supergroup_id),
                                                                                 as_bool(is_all_history_available)));
    } else if (op == "tsgsm") {
      string supergroup_id;
      string sign_messages;

      std::tie(supergroup_id, sign_messages) = split(args);
      send_request(make_tl_object<td_api::toggleSupergroupSignMessages>(to_integer<int32>(supergroup_id),
                                                                        as_bool(sign_messages)));
    } else if (op == "csgd" || op == "cchd") {
      string supergroup_id;
      string description;

      std::tie(supergroup_id, description) = split(args);
      send_request(make_tl_object<td_api::setSupergroupDescription>(to_integer<int32>(supergroup_id), description));
    } else if (op == "psgm" || op == "pchm") {
      string supergroup_id;
      string message_id;

      std::tie(supergroup_id, message_id) = split(args);
      send_request(make_tl_object<td_api::pinSupergroupMessage>(to_integer<int32>(supergroup_id),
                                                                as_message_id(message_id), false));
    } else if (op == "psgms" || op == "pchms") {
      string supergroup_id;
      string message_id;

      std::tie(supergroup_id, message_id) = split(args);
      send_request(make_tl_object<td_api::pinSupergroupMessage>(to_integer<int32>(supergroup_id),
                                                                as_message_id(message_id), false));
    } else if (op == "upsgm" || op == "upchm") {
      send_request(make_tl_object<td_api::unpinSupergroupMessage>(to_integer<int32>(args)));
    } else if (op == "grib") {
      send_request(make_tl_object<td_api::getRecentInlineBots>());
    } else if (op == "spc" || op == "su" || op == "sch") {
      send_request(make_tl_object<td_api::searchPublicChat>(args));
    } else if (op == "spcs") {
      send_request(make_tl_object<td_api::searchPublicChats>(args));
    } else if (op == "sc") {
      string limit;
      string query;
      std::tie(limit, query) = split(args);
      send_request(make_tl_object<td_api::searchChats>(query, to_integer<int32>(limit)));
    } else if (op == "scos") {
      string limit;
      string query;
      std::tie(limit, query) = split(args);
      send_request(make_tl_object<td_api::searchChatsOnServer>(query, to_integer<int32>(limit)));
    } else if (op == "sco") {
      string limit;
      string query;
      std::tie(limit, query) = split(args);
      send_request(make_tl_object<td_api::searchContacts>(query, to_integer<int32>(limit)));
    } else if (op == "arfc") {
      send_request(make_tl_object<td_api::addRecentlyFoundChat>(as_chat_id(args)));
    } else if (op == "rrfc") {
      send_request(make_tl_object<td_api::removeRecentlyFoundChat>(as_chat_id(args)));
    } else if (op == "crfcs") {
      send_request(make_tl_object<td_api::clearRecentlyFoundChats>());
    } else if (op == "gwpp") {
      send_request(make_tl_object<td_api::getWebPagePreview>(as_caption(args)));
    } else if (op == "gwpiv") {
      string url;
      string force_full;
      std::tie(url, force_full) = split(args);

      send_request(make_tl_object<td_api::getWebPageInstantView>(url, as_bool(force_full)));
    } else if (op == "spp") {
      send_request(make_tl_object<td_api::setProfilePhoto>(as_local_file(args)));
    } else if (op == "sppg") {
      string path;
      string conversion;
      std::tie(path, conversion) = split(args);
      send_request(make_tl_object<td_api::setProfilePhoto>(as_generated_file(path, conversion)));
    } else if (op == "sh") {
      auto prefix = std::move(args);
      send_request(make_tl_object<td_api::searchHashtags>(prefix, 10));
    } else if (op == "rrh") {
      auto hashtag = std::move(args);
      send_request(make_tl_object<td_api::removeRecentHashtag>(hashtag));
    } else if (op == "view") {
      string chat_id;
      string message_ids;
      std::tie(chat_id, message_ids) = split(args);

      send_request(make_tl_object<td_api::viewMessages>(as_chat_id(chat_id), as_message_ids(message_ids), true));
    } else if (op == "omc") {
      string chat_id;
      string message_id;
      std::tie(chat_id, message_id) = split(args);

      send_request(make_tl_object<td_api::openMessageContent>(as_chat_id(chat_id), as_message_id(message_id)));
    } else if (op == "racm") {
      string chat_id = args;
      send_request(make_tl_object<td_api::readAllChatMentions>(as_chat_id(chat_id)));
    } else if (op == "dpp") {
      send_request(make_tl_object<td_api::deleteProfilePhoto>(to_integer<int64>(args)));
    } else if (op == "gsns") {
      send_request(make_tl_object<td_api::getScopeNotificationSettings>(get_notification_settings_scope(args)));
    } else if (op == "scns") {
      string chat_id;
      string settings;

      std::tie(chat_id, settings) = split(args);

      string mute_for;
      string sound;
      string show_previews;

      std::tie(mute_for, settings) = split(settings, ',');
      std::tie(sound, show_previews) = split(settings, ',');

      send_request(make_tl_object<td_api::setChatNotificationSettings>(
          as_chat_id(chat_id),
          make_tl_object<td_api::chatNotificationSettings>(mute_for.empty(), to_integer<int32>(mute_for), sound.empty(),
                                                           sound, show_previews.empty(), as_bool(show_previews))));
    } else if (op == "ssns") {
      string scope;
      string settings;

      std::tie(scope, settings) = split(args);

      string mute_for;
      string sound;
      string show_previews;

      std::tie(mute_for, settings) = split(settings, ',');
      std::tie(sound, show_previews) = split(settings, ',');

      send_request(make_tl_object<td_api::setScopeNotificationSettings>(
          get_notification_settings_scope(scope), make_tl_object<td_api::scopeNotificationSettings>(
                                                      to_integer<int32>(mute_for), sound, as_bool(show_previews))));
    } else if (op == "rans") {
      send_request(make_tl_object<td_api::resetAllNotificationSettings>());
    } else if (op == "gcrss") {
      send_request(make_tl_object<td_api::getChatReportSpamState>(as_chat_id(args)));
    } else if (op == "ccrss") {
      string chat_id;
      string is_spam_chat;
      std::tie(chat_id, is_spam_chat) = split(args);

      send_request(make_tl_object<td_api::changeChatReportSpamState>(as_chat_id(chat_id), as_bool(is_spam_chat)));
    } else if (op == "rc") {
      string chat_id;
      string reason_str;
      string message_ids;
      std::tie(chat_id, args) = split(args);
      std::tie(reason_str, message_ids) = split(args);

      tl_object_ptr<td_api::ChatReportReason> reason;
      if (reason_str == "spam") {
        reason = make_tl_object<td_api::chatReportReasonSpam>();
      } else if (reason_str == "violence") {
        reason = make_tl_object<td_api::chatReportReasonViolence>();
      } else if (reason_str == "porno") {
        reason = make_tl_object<td_api::chatReportReasonPornography>();
      } else if (reason_str == "copyright") {
        reason = make_tl_object<td_api::chatReportReasonCopyright>();
      } else {
        reason = make_tl_object<td_api::chatReportReasonCustom>(reason_str);
      }

      send_request(
          make_tl_object<td_api::reportChat>(as_chat_id(chat_id), std::move(reason), as_message_ids(message_ids)));
    } else if (op == "rsgs" || op == "rchs") {
      string supergroup_id;
      string user_id;
      string message_ids;
      std::tie(supergroup_id, args) = split(args);
      std::tie(user_id, message_ids) = split(args);

      send_request(make_tl_object<td_api::reportSupergroupSpam>(to_integer<int32>(supergroup_id), as_user_id(user_id),
                                                                as_message_ids(message_ids)));
    } else if (op == "gdiff") {
      send_request(make_tl_object<td_api::testGetDifference>());
    } else if (op == "dproxy") {
      send_request(make_tl_object<td_api::disableProxy>());
    } else if (op == "eproxy") {
      send_request(make_tl_object<td_api::enableProxy>(as_proxy_id(args)));
    } else if (op == "rproxy") {
      send_request(make_tl_object<td_api::removeProxy>(as_proxy_id(args)));
    } else if (op == "aproxy" || op == "aeproxy" || op == "aeproxytcp" || op == "editproxy" || op == "editeproxy" ||
               op == "editeproxytcp") {
      string proxy_id;
      string server;
      string port;
      string user;
      string password;
      if (op[0] == 'e') {
        std::tie(proxy_id, args) = split(args);
      }
      std::tie(server, args) = split(args);
      std::tie(port, args) = split(args);
      std::tie(user, password) = split(args);
      bool enable = op != "aproxy" && op != "editproxy";
      td_api::object_ptr<td_api::ProxyType> type;
      if (!user.empty() && password.empty()) {
        type = make_tl_object<td_api::proxyTypeMtproto>(user);
      } else {
        if (port == "80") {
          type = make_tl_object<td_api::proxyTypeHttp>(user, password, op.back() != 'p');
        } else {
          type = make_tl_object<td_api::proxyTypeSocks5>(user, password);
        }
      }
      if (op[0] == 'e') {
        send_request(make_tl_object<td_api::editProxy>(as_proxy_id(proxy_id), server, to_integer<int32>(port), enable,
                                                       std::move(type)));
      } else {
        send_request(make_tl_object<td_api::addProxy>(server, to_integer<int32>(port), enable, std::move(type)));
      }
    } else if (op == "gproxy" || op == "gproxies") {
      send_request(make_tl_object<td_api::getProxies>());
    } else if (op == "gproxyl" || op == "gpl") {
      send_request(make_tl_object<td_api::getProxyLink>(as_proxy_id(args)));
    } else if (op == "pproxy") {
      send_request(make_tl_object<td_api::pingProxy>(as_proxy_id(args)));
    } else if (op == "touch") {
      auto r_fd = FileFd::open(args, FileFd::Read | FileFd::Write);
      if (r_fd.is_error()) {
        LOG(ERROR) << r_fd.error();
        return;
      }

      auto fd = r_fd.move_as_ok();
      auto size = fd.get_size();
      fd.seek(size).ignore();
      fd.write("a").ignore();
      fd.seek(size).ignore();
      fd.truncate_to_current_position(size).ignore();
    } else if (op == "SetVerbosity" || op == "SV") {
      td::Log::set_verbosity_level(to_integer<int>(args));
    } else if (op[0] == 'v' && op[1] == 'v') {
      td::Log::set_verbosity_level(static_cast<int>(op.size()));
    } else if (op[0] == 'v' && ('0' <= op[1] && op[1] <= '9')) {
      td::Log::set_verbosity_level(to_integer<int>(op.substr(1)));
    } else if (op == "q" || op == "Quit") {
      quit();
    } else if (op == "dnq" || op == "DumpNetQueries") {
      dump_pending_network_queries();
    } else if (op == "fatal") {
      LOG(FATAL) << "Fatal!";
    } else if (op == "unreachable") {
      UNREACHABLE();
    } else {
      op_not_found_count++;
    }

    if (op_not_found_count == OP_BLOCK_COUNT && !cmd.empty()) {
      LOG(ERROR) << "Unknown command \"" << cmd << "\" of length " << cmd.size();
    }
  }

  bool inited_ = false;
  void loop() override {
    if (!inited_) {
      inited_ = true;
      init();
    }
#if !TD_WINDOWS
#ifdef USE_READLINE
    if (can_read(stdin_)) {
      rl_callback_read_char();
      stdin_.get_fd().clear_flags(Fd::Read);
    }
#else
    auto r = stdin_.flush_read();
    CHECK(r.is_ok());
    while (true) {
      auto cmd = process_stdin(&stdin_.input_buffer());
      if (cmd.is_error()) {
        break;
      }
      add_cmd(cmd.ok().as_slice().str());
    }
#endif

    while (!cmd_queue_.empty() && !close_flag_) {
      auto cmd = std::move(cmd_queue_.front());
      cmd_queue_.pop();
      on_cmd(std::move(cmd));
    }
#endif

    if (ready_to_stop_ && close_flag_ && is_stdin_reader_stopped_) {
#ifdef USE_READLINE
      rl_callback_handler_remove();
#endif
      Scheduler::instance()->finish();
      LOG(WARNING) << "STOP";
      stop();
    }
  }

  void timeout_expired() override {
    if (close_flag_) {
      return;
    }

    for (auto it = pending_file_generations_.begin(); it != pending_file_generations_.end();) {
      auto left_size = it->size - it->local_size;
      CHECK(left_size > 0);
      if (it->part_size > left_size) {
        it->part_size = left_size;
      }
      BufferSlice block(it->part_size);
      FileFd::open(it->source, FileFd::Flags::Read).move_as_ok().pread(block.as_slice(), it->local_size).ensure();
      auto open_flags = FileFd::Flags::Write | (it->local_size ? 1 : FileFd::Flags::Truncate | FileFd::Flags::Create);
      FileFd::open(it->destination, open_flags).move_as_ok().pwrite(block.as_slice(), it->local_size).ensure();
      it->local_size += it->part_size;
      if (it->local_size == it->size) {
        send_request(make_tl_object<td_api::setFileGenerationProgress>(it->id, it->size, it->size));
        send_request(make_tl_object<td_api::finishFileGeneration>(it->id, nullptr));
        it = pending_file_generations_.erase(it);
      } else {
        send_request(
            make_tl_object<td_api::setFileGenerationProgress>(it->id, (it->size + it->local_size) / 2, it->local_size));
        ++it;
      }
    }

    if (!pending_file_generations_.empty()) {
      set_timeout_in(0.01);
    }
  }

  void hangup_shared() override {
    CHECK(get_link_token() == 1);
    LOG(INFO) << "StdinReader stopped";
    is_stdin_reader_stopped_ = true;
    yield();
  }

  void add_cmd(string cmd) {
    cmd_queue_.push(std::move(cmd));
  }

  std::unordered_map<int32, double> being_downloaded_files_;

  int32 my_id_ = 0;

  bool use_test_dc_ = false;
  ActorOwn<ClientActor> td_;
  std::queue<string> cmd_queue_;
  bool close_flag_ = false;
  bool ready_to_stop_ = false;
  bool is_stdin_reader_stopped_ = false;

  bool get_chat_list_ = false;
  bool disable_network_ = false;
  int api_id_ = 0;
  std::string api_hash_;

#if TD_WINDOWS
  ActorOwn<> stdin_reader_;
#endif
};
CliClient *CliClient::instance_ = nullptr;

void quit() {
  CliClient::quit_instance();
}

static void fail_signal(int sig) {
  signal_safe_write_signal_number(sig);
  while (true) {
    // spin forever to allow debugger to attach
  }
}

static void usage() {
  //TODO:
}

static void on_fatal_error(const char *error) {
  std::cerr << "Fatal error: " << error << std::endl;
}

void main(int argc, char **argv) {
  ignore_signal(SignalType::HangUp).ensure();
  ignore_signal(SignalType::Pipe).ensure();
  set_signal_handler(SignalType::Error, fail_signal).ensure();
  set_signal_handler(SignalType::Abort, fail_signal).ensure();
  td::Log::set_fatal_error_callback(on_fatal_error);

  const char *locale_name = (std::setlocale(LC_ALL, "fr-FR") == nullptr ? "" : "fr-FR");
  std::locale new_locale(locale_name);
  std::locale::global(new_locale);
  SCOPE_EXIT {
    std::locale::global(std::locale::classic());
  };

  CliLog cli_log;
  log_interface = &cli_log;

  td::FileLog file_log;
  td::TsLog ts_log(&file_log);

  int new_verbosity_level = VERBOSITY_NAME(INFO);
  bool use_test_dc = false;
  bool get_chat_list = false;
  bool disable_network = false;
  auto api_id = [](auto x) -> int32 {
    if (x) {
      return td::to_integer<int32>(Slice(x));
    }
    return 0;
  }(std::getenv("TD_API_ID"));
  auto api_hash = [](auto x) -> std::string {
    if (x) {
      return x;
    }
    return "";
  }(std::getenv("TD_API_HASH"));
  // TODO port OptionsParser to Windows
  for (int i = 1; i < argc; i++) {
    if (!std::strcmp(argv[i], "--test")) {
      use_test_dc = true;
    } else if (!std::strncmp(argv[i], "-v", 2)) {
      const char *arg = argv[i] + 2;
      if (*arg == '\0' && i + 1 < argc) {
        arg = argv[++i];
      }
      int new_verbosity = 0;
      if (arg[0] == 'v') {
        new_verbosity = 1;
        while (arg[0] == 'v') {
          new_verbosity++;
          arg++;
        }
      }
      new_verbosity += to_integer<int>(Slice(arg));
      new_verbosity_level = VERBOSITY_NAME(FATAL) + new_verbosity;
    } else if (!std::strncmp(argv[i], "-l", 2)) {
      const char *arg = argv[i] + 2;
      if (*arg == '\0' && i + 1 < argc) {
        arg = argv[++i];
      }
      if (file_log.init(arg) && file_log.init(arg) && file_log.init(arg, 1000 << 20)) {
        log_interface = &ts_log;
      }
    } else if (!std::strcmp(argv[i], "-W")) {
      get_chat_list = true;
    } else if (!std::strcmp(argv[i], "--disable-network") || !std::strcmp(argv[i], "-n")) {
      disable_network = true;
    } else if (!std::strcmp(argv[i], "--api_id")) {
      if (i + 1 >= argc) {
        return usage();
      }
      api_id = td::to_integer<int32>(Slice(argv[++i]));
    } else if (!std::strcmp(argv[i], "--api_hash")) {
      if (i + 1 >= argc) {
        return usage();
      }
      api_hash = argv[++i];
    }
  }

  if (api_id == 0 || api_hash == "") {
    LOG(ERROR) << "You should provide some valid api_id and api_hash";
    return usage();
  }

  SET_VERBOSITY_LEVEL(new_verbosity_level);

  {
    ConcurrentScheduler scheduler;
    scheduler.init(4);

    scheduler
        .create_actor_unsafe<CliClient>(0, "CliClient", use_test_dc, get_chat_list, disable_network, api_id, api_hash)
        .release();

    scheduler.start();
    while (scheduler.run_main(100)) {
    }
    scheduler.finish();
  }

  dump_memory_usage();
}
}  // namespace td

int main(int argc, char **argv) {
  td::main(argc, argv);
}
