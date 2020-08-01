//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/actor/actor.h"

#include "memprof/memprof.h"

#include "td/net/HttpQuery.h"
#include "td/net/HttpReader.h"

#include "td/telegram/ClientActor.h"
#include "td/telegram/Log.h"
#include "td/telegram/td_api_json.h"

#include "td/utils/base64.h"
#include "td/utils/buffer.h"
#include "td/utils/common.h"
#include "td/utils/FileLog.h"
#include "td/utils/format.h"
#include "td/utils/JsonBuilder.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/OptionParser.h"
#include "td/utils/port/FileFd.h"
#include "td/utils/port/PollFlags.h"
#include "td/utils/port/signals.h"
#include "td/utils/port/Stat.h"
#include "td/utils/port/StdStreams.h"
#include "td/utils/port/thread_local.h"
#include "td/utils/Random.h"
#include "td/utils/ScopeGuard.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"
#include "td/utils/StringBuilder.h"
#include "td/utils/Time.h"

#ifndef USE_READLINE
#include "td/utils/find_boundary.h"
#endif

#include <algorithm>
#include <atomic>
#include <clocale>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <limits>
#include <locale>
#include <memory>
#include <queue>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

#ifdef USE_READLINE
/* Standard readline include files. */
#include <readline/history.h>
#include <readline/readline.h>
#endif

namespace td {

static void dump_memory_usage() {
  if (is_memprof_on()) {
    LOG(WARNING) << "Memory dump:";
    clear_thread_locals();
    std::vector<AllocInfo> alloc_info;
    dump_alloc([&](const AllocInfo &info) { alloc_info.push_back(info); });
    std::sort(alloc_info.begin(), alloc_info.end(),
              [](const AllocInfo &lhs, const AllocInfo &rhs) { return lhs.size > rhs.size; });
    size_t total_size = 0;
    size_t other_size = 0;
    int cnt = 0;
    for (auto &info : alloc_info) {
      if (cnt++ < 50) {
        LOG(WARNING) << format::as_size(info.size) << format::as_array(info.backtrace);
      } else {
        other_size += info.size;
      }
      total_size += info.size;
    }
    LOG(WARNING) << tag("other", format::as_size(other_size));
    LOG(WARNING) << tag("total", format::as_size(total_size));
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
    SCOPE_EXIT {
      reactivate_readline();
    };
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
  CliClient(ConcurrentScheduler *scheduler, bool use_test_dc, bool get_chat_list, bool disable_network, int32 api_id,
            string api_hash)
      : scheduler_(scheduler)
      , use_test_dc_(use_test_dc)
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
    Logger log{*log_interface, LogOptions::plain(), VERBOSITY_NAME(PLAIN)};
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
          LOG(PLAIN) << oneline(static_cast<const td_api::messageText *>(m->content_.get())->text_->text_) << "\n";
        }
        last_message_id = m->id_;
      }

      if (last_message_id > 0) {
        send_request(td_api::make_object<td_api::getChatHistory>(get_history_chat_id_, last_message_id, 0, 100, false));
      } else {
        get_history_chat_id_ = 0;
      }
    }
    if (search_chat_id_ != 0) {
      if (!messages.messages_.empty()) {
        auto last_message_id = messages.messages_.back()->id_;
        LOG(ERROR) << (last_message_id >> 20);
        send_request(td_api::make_object<td_api::searchChatMessages>(
            search_chat_id_, "", 0, last_message_id, 0, 100,
            td_api::make_object<td_api::searchMessagesFilterPhotoAndVideo>()));
      } else {
        search_chat_id_ = 0;
      }
    }
  }

  void on_get_message(const td_api::message &message) {
    if (message.sending_state_ != nullptr &&
        message.sending_state_->get_id() == td_api::messageSendingStatePending::ID) {
      // send_request(td_api::make_object<td_api::deleteMessages>(message.chat_id_, vector<int64>{message.id_}, true));
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
    bool test_local_size_decrease = false;
  };

  vector<FileGeneration> pending_file_generations_;

  void on_file_generation_start(const td_api::updateFileGenerationStart &update) {
    FileGeneration file_generation;
    file_generation.id = update.generation_id_;
    file_generation.destination = update.destination_path_;
    if (update.conversion_ == "#url#" || update.conversion_ == "url") {
      // TODO: actually download
      file_generation.source = "test.jpg";
      file_generation.part_size = 1000000;
    } else if (update.conversion_ == "skip") {
      return;
    } else {
      file_generation.source = update.original_path_;
      file_generation.part_size = to_integer<int32>(update.conversion_);
      file_generation.test_local_size_decrease = !update.conversion_.empty() && update.conversion_.back() == 't';
    }

    auto r_stat = stat(file_generation.source);
    if (r_stat.is_ok()) {
      auto size = r_stat.ok().size_;
      if (size <= 0 || size > (2000 << 20)) {
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
      send_request(td_api::make_object<td_api::finishFileGeneration>(
          update.generation_id_, td_api::make_object<td_api::error>(400, r_stat.error().message().str())));
    }
  }

  void on_update_autorization_state(const td_api::AuthorizationState &state) {
    switch (state.get_id()) {
      case td_api::authorizationStateWaitTdlibParameters::ID: {
        auto parameters = td_api::make_object<td_api::tdlibParameters>();
        parameters->use_test_dc_ = use_test_dc_;
        parameters->use_message_database_ = true;
        parameters->use_chat_info_database_ = true;
        parameters->use_secret_chats_ = true;
        parameters->api_id_ = api_id_;
        parameters->api_hash_ = api_hash_;
        parameters->system_language_code_ = "en";
        parameters->device_model_ = "Desktop";
        parameters->application_version_ = "1.0";
        send_request(td_api::make_object<td_api::setTdlibParameters>(std::move(parameters)));
        break;
      }
      case td_api::authorizationStateWaitEncryptionKey::ID:
        send_request(td_api::make_object<td_api::checkDatabaseEncryptionKey>());
        break;
      case td_api::authorizationStateReady::ID:
        LOG(INFO) << "Logged in";
        break;
      case td_api::authorizationStateClosed::ID:
        LOG(WARNING) << "TD closed";
        td_client_.reset();
        if (!close_flag_) {
          create_td("ClientActor3");
        }
        break;
      default:
        break;
    }
  }

  static char get_delimiter(Slice str) {
    std::unordered_set<char> chars;
    for (auto c : trim(str)) {
      if (!is_alnum(c) && c != '-') {
        chars.insert(c);
      }
    }
    if (chars.empty()) {
      return ' ';
    }
    if (chars.size() == 1) {
      return *chars.begin();
    }
    LOG(ERROR) << "Failed to determine delimiter in \"" << str << '"';
    return ' ';
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

  static int32 as_chat_filter_id(Slice str) {
    return to_integer<int32>(trim(str));
  }

  static vector<int32> as_chat_filter_ids(Slice chat_filter_ids) {
    return transform(full_split(trim(chat_filter_ids), get_delimiter(chat_filter_ids)),
                     [](Slice str) { return as_chat_filter_id(str); });
  }

  static td_api::object_ptr<td_api::ChatList> as_chat_list(string chat_list) {
    if (!chat_list.empty() && chat_list.back() == 'a') {
      return td_api::make_object<td_api::chatListArchive>();
    }
    if (chat_list.find('-') != string::npos) {
      return td_api::make_object<td_api::chatListFilter>(as_chat_filter_id(chat_list.substr(chat_list.find('-') + 1)));
    }
    return td_api::make_object<td_api::chatListMain>();
  }

  vector<int64> as_chat_ids(Slice chat_ids) const {
    return transform(full_split(trim(chat_ids), get_delimiter(chat_ids)),
                     [this](Slice str) { return as_chat_id(str); });
  }

  static int64 as_message_id(Slice str) {
    str = trim(str);
    if (!str.empty() && str.back() == 's') {
      return to_integer<int64>(str) << 20;
    }
    return to_integer<int64>(str);
  }

  static vector<int64> as_message_ids(Slice message_ids) {
    return transform(full_split(trim(message_ids), get_delimiter(message_ids)), as_message_id);
  }

  static int32 as_button_id(Slice str) {
    return to_integer<int32>(trim(str));
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

  vector<int32> as_user_ids(Slice user_ids) const {
    return transform(full_split(user_ids, get_delimiter(user_ids)), [this](Slice str) { return as_user_id(str); });
  }

  static int32 as_basic_group_id(Slice str) {
    str = trim(str);
    auto result = to_integer<int32>(str);
    if (result < 0) {
      return -result;
    }
    return result;
  }

  int32 as_supergroup_id(Slice str) {
    str = trim(str);
    if (str[0] == '@') {
      return username_to_supergroup_id_[to_lower(str.substr(1))];
    }
    auto result = to_integer<int64>(str);
    int64 shift = static_cast<int64>(-1000000000000ll);
    if (result <= shift) {
      return static_cast<int32>(shift - result);
    }
    return static_cast<int32>(result);
  }

  static int32 as_secret_chat_id(Slice str) {
    str = trim(str);
    auto result = to_integer<int64>(str);
    int64 shift = static_cast<int64>(-2000000000000ll);
    if (result <= shift + std::numeric_limits<int32>::max()) {
      return static_cast<int32>(result - shift);
    }
    return static_cast<int32>(result);
  }

  static int32 as_file_id(Slice str) {
    return to_integer<int32>(trim(str));
  }

  static td_api::object_ptr<td_api::InputFile> as_input_file_id(Slice str) {
    return td_api::make_object<td_api::inputFileId>(as_file_id(str));
  }

  static td_api::object_ptr<td_api::InputFile> as_local_file(string path) {
    return td_api::make_object<td_api::inputFileLocal>(trim(std::move(path)));
  }

  static td_api::object_ptr<td_api::InputFile> as_remote_file(string id) {
    return td_api::make_object<td_api::inputFileRemote>(trim(std::move(id)));
  }

  static td_api::object_ptr<td_api::InputFile> as_generated_file(string original_path, string conversion,
                                                                 int32 expected_size = 0) {
    return td_api::make_object<td_api::inputFileGenerated>(trim(original_path), trim(conversion), expected_size);
  }

  static td_api::object_ptr<td_api::InputFile> as_input_file(string str) {
    str = trim(str);
    if ((str.size() >= 20 && is_base64url(str)) || begins_with(str, "http")) {
      return as_remote_file(str);
    }
    auto r_id = to_integer_safe<int32>(str);
    if (r_id.is_ok()) {
      return as_input_file_id(str);
    }
    if (str.find(';') < str.size()) {
      auto res = split(str, ';');
      return as_generated_file(res.first, res.second);
    }
    return as_local_file(str);
  }

  static td_api::object_ptr<td_api::inputThumbnail> as_input_thumbnail(td_api::object_ptr<td_api::InputFile> input_file,
                                                                       int32 width = 0, int32 height = 0) {
    return td_api::make_object<td_api::inputThumbnail>(std::move(input_file), width, height);
  }

  static td_api::object_ptr<td_api::inputThumbnail> as_input_thumbnail(const string &thumbnail, int32 width = 0,
                                                                       int32 height = 0) {
    return as_input_thumbnail(as_input_file(thumbnail), width, height);
  }

  static td_api::object_ptr<td_api::inputThumbnail> as_input_thumbnail(const string &original_path,
                                                                       const string &conversion, int32 width = 0,
                                                                       int32 height = 0) {
    return as_input_thumbnail(as_generated_file(original_path, conversion), width, height);
  }

  static int32 as_call_id(string str) {
    return to_integer<int32>(trim(std::move(str)));
  }

  static int32 as_proxy_id(string str) {
    return to_integer<int32>(trim(std::move(str)));
  }

  static td_api::object_ptr<td_api::location> as_location(string latitude, string longitude) {
    if (trim(latitude).empty() && trim(longitude).empty()) {
      return nullptr;
    }
    return td_api::make_object<td_api::location>(to_double(latitude), to_double(longitude));
  }

  static bool as_bool(string str) {
    str = to_lower(trim(str));
    return str == "true" || str == "1";
  }

  template <class T>
  static vector<T> to_integers(Slice ids_string) {
    return transform(transform(full_split(ids_string, get_delimiter(ids_string)), trim<Slice>), to_integer<T>);
  }

  void on_result(uint64 generation, uint64 id, td_api::object_ptr<td_api::Object> result) {
    auto result_str = to_string(result);
    if (result != nullptr) {
      switch (result->get_id()) {
        case td_api::stickerSets::ID: {
          auto sticker_sets = static_cast<const td_api::stickerSets *>(result.get());
          result_str = PSTRING() << "StickerSets { total_count = " << sticker_sets->total_count_
                                 << ", count = " << sticker_sets->sets_.size() << "}";
          break;
        }
        default:
          break;
      }
    }

    if (id > 0 && GET_VERBOSITY_LEVEL() < VERBOSITY_NAME(td_requests)) {
      LOG(ERROR) << "Receive result [" << generation << "][id=" << id << "] " << result_str;
    }

    auto as_json_str = json_encode<std::string>(ToJson(result));
    // LOG(INFO) << "Receive result [" << generation << "][id=" << id << "] " << as_json_str;
    //auto copy_as_json_str = as_json_str;
    //auto as_json_value = json_decode(copy_as_json_str).move_as_ok();
    //td_api::object_ptr<td_api::Object> object;
    //from_json(object, as_json_value).ensure();
    //CHECK(object != nullptr);
    //auto as_json_str2 = json_encode<std::string>(ToJson(object));
    //LOG_CHECK(as_json_str == as_json_str2) << "\n" << tag("a", as_json_str) << "\n" << tag("b", as_json_str2);
    // LOG(INFO) << "Receive result [" << generation << "][id=" << id << "] " << as_json_str;

    if (generation != generation_) {
      LOG(INFO) << "Drop received from previous Client " << result_str;
      return;
    }

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
      case td_api::updateAuthorizationState::ID:
        on_update_autorization_state(
            *(static_cast<const td_api::updateAuthorizationState *>(result.get())->authorization_state_));
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
      case td_api::updateConnectionState::ID:
        LOG(WARNING) << result_str;
        break;
    }
  }

  void on_error(uint64 generation, uint64 id, td_api::object_ptr<td_api::error> error) {
    if (id > 0 && GET_VERBOSITY_LEVEL() < VERBOSITY_NAME(td_requests)) {
      LOG(ERROR) << "Receive error [" << generation << "][id=" << id << "] " << to_string(error);
    }
  }

  void on_closed(uint64 generation) {
    LOG(WARNING) << "Td with generation " << generation << " is closed";
    closed_td_++;
    if (closed_td_ == generation_) {
      LOG(WARNING) << "Ready to stop";
      ready_to_stop_ = true;
      if (close_flag_) {
        yield();
      }
    }
  }

  void quit() {
    if (close_flag_) {
      return;
    }

    LOG(WARNING) << "QUIT";
    close_flag_ = true;
    dump_memory_usage();
    td_client_.reset();
    Scheduler::unsubscribe(stdin_.get_poll_info().get_pollable_fd_ref());
    is_stdin_reader_stopped_ = true;
    yield();
  }

  BufferedStdin stdin_;
  static CliClient *instance_;

#ifdef USE_READLINE
  /* Callback function called for each line when accept-line executed, EOF
   *    seen, or EOF character read.  This sets a flag and returns; it could
   *       also call exit. */
  static void static_add_cmd(char *line) {
    /* Can use ^D (stty eof) to exit. */
    if (line == nullptr) {
      LOG(FATAL) << "Closed";
      return;
    }
    if (*line) {
      add_history(line);
    }
    instance_->add_cmd(line);
    rl_free(line);
  }
  static int static_getc(FILE *) {
    return instance_->stdin_getc();
  }
#endif

  uint64 generation_ = 0;
  uint64 closed_td_ = 0;
  void create_td(Slice name) {
    if (ready_to_stop_) {
      return;
    }

    LOG(WARNING) << "Creating new TD " << name << " with generation " << generation_ + 1;
    class TdCallbackImpl : public TdCallback {
     public:
      TdCallbackImpl(CliClient *client, uint64 generation) : client_(client), generation_(generation) {
      }
      void on_result(uint64 id, td_api::object_ptr<td_api::Object> result) override {
        client_->on_result(generation_, id, std::move(result));
      }
      void on_error(uint64 id, td_api::object_ptr<td_api::error> error) override {
        client_->on_error(generation_, id, std::move(error));
      }
      TdCallbackImpl(const TdCallbackImpl &) = delete;
      TdCallbackImpl &operator=(const TdCallbackImpl &) = delete;
      TdCallbackImpl(TdCallbackImpl &&) = delete;
      TdCallbackImpl &operator=(TdCallbackImpl &&) = delete;
      ~TdCallbackImpl() override {
        client_->on_closed(generation_);
      }

     private:
      CliClient *client_;
      uint64 generation_;
    };

    ClientActor::Options options;
    options.net_query_stats = net_query_stats_;

    td_client_ = create_actor<ClientActor>(name, make_unique<TdCallbackImpl>(this, ++generation_), std::move(options));
  }

  void init_td() {
    close_flag_ = false;
    ready_to_stop_ = false;
    generation_ = 0;
    closed_td_ = 0;

    create_td("ClientActor1");

    bool test_init = false;
    if (test_init) {
      create_td("ClientActor2");

      for (int i = 0; i < 4; i++) {
        send_closure_later(td_client_, &ClientActor::request, std::numeric_limits<uint64>::max(),
                           td_api::make_object<td_api::setAlarm>(0.001 + 1000 * (i / 2)));
      }

      send_request(td_api::make_object<td_api::getStorageStatistics>(10));
      send_request(td_api::make_object<td_api::getStorageStatisticsFast>());

      send_request(td_api::make_object<td_api::getTextEntities>(
          "@telegram /test_command https://telegram.org telegram.me @gif @test"));

      send_request(td_api::make_object<td_api::getOption>("use_pfs"));
      send_request(td_api::make_object<td_api::setOption>(
          "use_pfs", td_api::make_object<td_api::optionValueBoolean>(std::time(nullptr) / 86400 % 2 == 0)));
      send_request(td_api::make_object<td_api::setOption>("notification_group_count_max",
                                                          td_api::make_object<td_api::optionValueInteger>(1)));
      send_request(td_api::make_object<td_api::setOption>("use_storage_optimizer",
                                                          td_api::make_object<td_api::optionValueBoolean>(false)));
      send_request(td_api::make_object<td_api::setOption>(
          "use_pfs", td_api::make_object<td_api::optionValueBoolean>(std::time(nullptr) / 86400 % 2 == 0)));
      send_request(td_api::make_object<td_api::setOption>("disable_contact_registered_notifications",
                                                          td_api::make_object<td_api::optionValueBoolean>(true)));

      send_request(td_api::make_object<td_api::setNetworkType>(td_api::make_object<td_api::networkTypeWiFi>()));
      send_request(td_api::make_object<td_api::getNetworkStatistics>());
      send_request(td_api::make_object<td_api::getCountryCode>());
      send_request(
          td_api::make_object<td_api::addProxy>("1.1.1.1", 1111, true, td_api::make_object<td_api::proxyTypeSocks5>()));
      send_request(td_api::make_object<td_api::addProxy>("1.1.1.1", 1112, false,
                                                         td_api::make_object<td_api::proxyTypeSocks5>()));
      send_request(td_api::make_object<td_api::pingProxy>(0));

      auto bad_parameters = td_api::make_object<td_api::tdlibParameters>();
      bad_parameters->database_directory_ = "/..";
      bad_parameters->api_id_ = api_id_;
      bad_parameters->api_hash_ = api_hash_;
      send_request(td_api::make_object<td_api::setTdlibParameters>(std::move(bad_parameters)));

      send_closure_later(actor_id(this), &CliClient::create_td, Slice("ClientActor3"));
    }
  }

  void init() {
    instance_ = this;

    init_td();

#ifdef USE_READLINE
    deactivate_readline();
    rl_getc_function = static_getc;
    rl_callback_handler_install(prompt, static_add_cmd);
    rl_attempted_completion_function = tg_cli_completion;
    reactivate_readline();
#endif
    Scheduler::subscribe(stdin_.get_poll_info().extract_pollable_fd(this), PollFlags::Read());

    if (get_chat_list_) {
      send_request(td_api::make_object<td_api::getChats>(nullptr, std::numeric_limits<int64>::max(), 0, 100));
    }
    if (disable_network_) {
      send_request(td_api::make_object<td_api::setNetworkType>(td_api::make_object<td_api::networkTypeNone>()));
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
    buffer->advance(1);
    buffer_pos_ = 0;
    return std::move(data);
  }
#endif

  static td_api::object_ptr<td_api::formattedText> as_formatted_text(
      string text, vector<td_api::object_ptr<td_api::textEntity>> entities = {}) {
    if (entities.empty() && !text.empty()) {
      auto parsed_text = execute(
          td_api::make_object<td_api::parseTextEntities>(text, td_api::make_object<td_api::textParseModeMarkdown>(2)));
      if (parsed_text->get_id() == td_api::formattedText::ID) {
        return td_api::move_object_as<td_api::formattedText>(parsed_text);
      }
    }
    return td_api::make_object<td_api::formattedText>(text, std::move(entities));
  }

  static td_api::object_ptr<td_api::formattedText> as_caption(
      string caption, vector<td_api::object_ptr<td_api::textEntity>> entities = {}) {
    return as_formatted_text(caption, std::move(entities));
  }

  static td_api::object_ptr<td_api::NotificationSettingsScope> get_notification_settings_scope(Slice scope) {
    if (scope.empty()) {
      return nullptr;
    }
    if (scope == "channels" || scope == "ch") {
      return td_api::make_object<td_api::notificationSettingsScopeChannelChats>();
    }
    if (scope == "chats" || scope == "groups" || as_bool(scope.str())) {
      return td_api::make_object<td_api::notificationSettingsScopeGroupChats>();
    }
    return td_api::make_object<td_api::notificationSettingsScopePrivateChats>();
  }

  static td_api::object_ptr<td_api::UserPrivacySetting> get_user_privacy_setting(MutableSlice setting) {
    setting = trim(setting);
    to_lower_inplace(setting);
    if (setting == "invite") {
      return td_api::make_object<td_api::userPrivacySettingAllowChatInvites>();
    }
    if (setting == "status") {
      return td_api::make_object<td_api::userPrivacySettingShowStatus>();
    }
    if (setting == "call") {
      return td_api::make_object<td_api::userPrivacySettingAllowCalls>();
    }
    if (setting == "p2p") {
      return td_api::make_object<td_api::userPrivacySettingAllowPeerToPeerCalls>();
    }
    if (setting == "forward") {
      return td_api::make_object<td_api::userPrivacySettingShowLinkInForwardedMessages>();
    }
    if (setting == "photo") {
      return td_api::make_object<td_api::userPrivacySettingShowProfilePhoto>();
    }
    if (setting == "phone_number") {
      return td_api::make_object<td_api::userPrivacySettingShowPhoneNumber>();
    }
    if (setting == "find") {
      return td_api::make_object<td_api::userPrivacySettingAllowFindingByPhoneNumber>();
    }
    return nullptr;
  }

  static td_api::object_ptr<td_api::SearchMessagesFilter> get_search_messages_filter(MutableSlice filter) {
    filter = trim(filter);
    to_lower_inplace(filter);
    if (filter == "an" || filter == "animation") {
      return td_api::make_object<td_api::searchMessagesFilterAnimation>();
    }
    if (filter == "au" || filter == "audio") {
      return td_api::make_object<td_api::searchMessagesFilterAudio>();
    }
    if (filter == "d" || filter == "document") {
      return td_api::make_object<td_api::searchMessagesFilterDocument>();
    }
    if (filter == "p" || filter == "photo") {
      return td_api::make_object<td_api::searchMessagesFilterPhoto>();
    }
    if (filter == "vi" || filter == "video") {
      return td_api::make_object<td_api::searchMessagesFilterVideo>();
    }
    if (filter == "vo" || filter == "voice") {
      return td_api::make_object<td_api::searchMessagesFilterVoiceNote>();
    }
    if (filter == "pvi") {
      return td_api::make_object<td_api::searchMessagesFilterPhotoAndVideo>();
    }
    if (filter == "u" || filter == "url") {
      return td_api::make_object<td_api::searchMessagesFilterUrl>();
    }
    if (filter == "cp" || filter == "chatphoto") {
      return td_api::make_object<td_api::searchMessagesFilterChatPhoto>();
    }
    if (filter == "c" || filter == "call") {
      return td_api::make_object<td_api::searchMessagesFilterCall>();
    }
    if (filter == "mc" || filter == "missedcall") {
      return td_api::make_object<td_api::searchMessagesFilterMissedCall>();
    }
    if (filter == "vn" || filter == "videonote") {
      return td_api::make_object<td_api::searchMessagesFilterVideoNote>();
    }
    if (filter == "vvn" || filter == "voicevideonote") {
      return td_api::make_object<td_api::searchMessagesFilterVoiceAndVideoNote>();
    }
    if (filter == "m" || filter == "mention") {
      return td_api::make_object<td_api::searchMessagesFilterMention>();
    }
    if (filter == "um" || filter == "umention") {
      return td_api::make_object<td_api::searchMessagesFilterUnreadMention>();
    }
    if (filter == "f" || filter == "failed") {
      return td_api::make_object<td_api::searchMessagesFilterFailedToSend>();
    }
    if (!filter.empty()) {
      LOG(ERROR) << "Unsupported message filter " << filter;
    }
    return nullptr;
  }

  static td_api::object_ptr<td_api::ChatMembersFilter> get_chat_members_filter(MutableSlice filter) {
    filter = trim(filter);
    to_lower_inplace(filter);
    if (filter == "a" || filter == "admin" || filter == "administrators") {
      return td_api::make_object<td_api::chatMembersFilterAdministrators>();
    }
    if (filter == "b" || filter == "banned") {
      return td_api::make_object<td_api::chatMembersFilterBanned>();
    }
    if (filter == "bot" || filter == "bots") {
      return td_api::make_object<td_api::chatMembersFilterBots>();
    }
    if (filter == "c" || filter == "contacts") {
      return td_api::make_object<td_api::chatMembersFilterContacts>();
    }
    if (filter == "m" || filter == "members") {
      return td_api::make_object<td_api::chatMembersFilterMembers>();
    }
    if (filter == "r" || filter == "rest" || filter == "restricted") {
      return td_api::make_object<td_api::chatMembersFilterRestricted>();
    }
    if (!filter.empty()) {
      LOG(ERROR) << "Unsupported chat member filter " << filter;
    }
    return nullptr;
  }

  td_api::object_ptr<td_api::chatFilter> as_chat_filter(string filter) const {
    string title;
    string icon_name;
    string pinned_chat_ids;
    string included_chat_ids;
    string excluded_chat_ids;
    std::tie(title, filter) = split(filter);
    std::tie(icon_name, filter) = split(filter);
    std::tie(pinned_chat_ids, filter) = split(filter);
    std::tie(included_chat_ids, filter) = split(filter);
    std::tie(excluded_chat_ids, filter) = split(filter);

    auto rand_bool = [] {
      return Random::fast(0, 1) == 1;
    };

    return td_api::make_object<td_api::chatFilter>(
        title, icon_name, as_chat_ids(pinned_chat_ids), as_chat_ids(included_chat_ids), as_chat_ids(excluded_chat_ids),
        rand_bool(), rand_bool(), rand_bool(), rand_bool(), rand_bool(), rand_bool(), rand_bool(), rand_bool());
  }

  static td_api::object_ptr<td_api::TopChatCategory> get_top_chat_category(MutableSlice category) {
    category = trim(category);
    to_lower_inplace(category);
    if (!category.empty() && category.back() == 's') {
      category.remove_suffix(1);
    }
    if (category == "bot") {
      return td_api::make_object<td_api::topChatCategoryBots>();
    } else if (category == "group") {
      return td_api::make_object<td_api::topChatCategoryGroups>();
    } else if (category == "channel") {
      return td_api::make_object<td_api::topChatCategoryChannels>();
    } else if (category == "inline") {
      return td_api::make_object<td_api::topChatCategoryInlineBots>();
    } else if (category == "call") {
      return td_api::make_object<td_api::topChatCategoryCalls>();
    } else if (category == "forward") {
      return td_api::make_object<td_api::topChatCategoryForwardChats>();
    } else {
      return td_api::make_object<td_api::topChatCategoryUsers>();
    }
  }

  static td_api::object_ptr<td_api::ChatAction> get_chat_action(MutableSlice action) {
    action = trim(action);
    to_lower_inplace(action);
    if (action == "c" || action == "cancel") {
      return td_api::make_object<td_api::chatActionCancel>();
    }
    if (action == "rvi" || action == "record_video") {
      return td_api::make_object<td_api::chatActionRecordingVideo>();
    }
    if (action == "uvi" || action == "upload_video") {
      return td_api::make_object<td_api::chatActionUploadingVideo>(50);
    }
    if (action == "rvo" || action == "record_voice") {
      return td_api::make_object<td_api::chatActionRecordingVoiceNote>();
    }
    if (action == "uvo" || action == "upload_voice") {
      return td_api::make_object<td_api::chatActionUploadingVoiceNote>(50);
    }
    if (action == "up" || action == "upload_photo") {
      return td_api::make_object<td_api::chatActionUploadingPhoto>(50);
    }
    if (action == "ud" || action == "upload_document") {
      return td_api::make_object<td_api::chatActionUploadingDocument>(50);
    }
    if (action == "fl" || action == "find_location") {
      return td_api::make_object<td_api::chatActionChoosingLocation>();
    }
    if (action == "cc" || action == "choose_contact") {
      return td_api::make_object<td_api::chatActionChoosingContact>();
    }
    if (action == "spg" || action == "start_play_game") {
      return td_api::make_object<td_api::chatActionStartPlayingGame>();
    }
    if (action == "rvn" || action == "record_video_note") {
      return td_api::make_object<td_api::chatActionRecordingVideoNote>();
    }
    if (action == "uvn" || action == "upload_video_note") {
      return td_api::make_object<td_api::chatActionUploadingVideoNote>(50);
    }
    return td_api::make_object<td_api::chatActionTyping>();
  }

  static td_api::object_ptr<td_api::NetworkType> get_network_type(MutableSlice type) {
    type = trim(type);
    to_lower_inplace(type);
    if (type == "none") {
      return td_api::make_object<td_api::networkTypeNone>();
    }
    if (type == "mobile") {
      return td_api::make_object<td_api::networkTypeMobile>();
    }
    if (type == "roaming") {
      return td_api::make_object<td_api::networkTypeMobileRoaming>();
    }
    if (type == "wifi") {
      return td_api::make_object<td_api::networkTypeWiFi>();
    }
    if (type == "other") {
      return td_api::make_object<td_api::networkTypeOther>();
    }
    return nullptr;
  }

  static td_api::object_ptr<td_api::SuggestedAction> as_suggested_action(Slice action) {
    if (action == "unarchive") {
      return td_api::make_object<td_api::suggestedActionEnableArchiveAndMuteNewChats>();
    }
    if (action == "number") {
      return td_api::make_object<td_api::suggestedActionCheckPhoneNumber>();
    }
    return nullptr;
  }

  static td_api::object_ptr<td_api::PassportElementType> as_passport_element_type(Slice passport_element_type) {
    if (passport_element_type == "address" || passport_element_type == "a") {
      return td_api::make_object<td_api::passportElementTypeAddress>();
    }
    if (passport_element_type == "email" || passport_element_type == "e") {
      return td_api::make_object<td_api::passportElementTypeEmailAddress>();
    }
    if (passport_element_type == "phone" || passport_element_type == "p") {
      return td_api::make_object<td_api::passportElementTypePhoneNumber>();
    }
    if (passport_element_type == "pd") {
      return td_api::make_object<td_api::passportElementTypePersonalDetails>();
    }
    if (passport_element_type == "dl") {
      return td_api::make_object<td_api::passportElementTypeDriverLicense>();
    }
    if (passport_element_type == "ip") {
      return td_api::make_object<td_api::passportElementTypeInternalPassport>();
    }
    if (passport_element_type == "ic") {
      return td_api::make_object<td_api::passportElementTypeIdentityCard>();
    }
    if (passport_element_type == "ra") {
      return td_api::make_object<td_api::passportElementTypeRentalAgreement>();
    }
    if (passport_element_type == "pr") {
      return td_api::make_object<td_api::passportElementTypePassportRegistration>();
    }
    if (passport_element_type == "tr") {
      return td_api::make_object<td_api::passportElementTypeTemporaryRegistration>();
    }
    return td_api::make_object<td_api::passportElementTypePassport>();
  }

  static auto as_passport_element_types(Slice types) {
    return transform(full_split(types, get_delimiter(types)), [](Slice str) { return as_passport_element_type(str); });
  }

  static td_api::object_ptr<td_api::InputPassportElement> as_input_passport_element(string passport_element_type,
                                                                                    string arg, bool with_selfie) {
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
      return td_api::make_object<td_api::inputPassportElementAddress>(
          td_api::make_object<td_api::address>("US", "CA", "Los Angeles", "Washington", "", "90001"));
    } else if (passport_element_type == "email" || passport_element_type == "e") {
      return td_api::make_object<td_api::inputPassportElementEmailAddress>(arg);
    } else if (passport_element_type == "phone" || passport_element_type == "p") {
      return td_api::make_object<td_api::inputPassportElementPhoneNumber>(arg);
    } else if (passport_element_type == "pd") {
      return td_api::make_object<td_api::inputPassportElementPersonalDetails>(
          td_api::make_object<td_api::personalDetails>("Mike", "Jr", "Towers", u8"Mike\u2708", u8"Jr\u26fd",
                                                       u8"Towers\u2757", td_api::make_object<td_api::date>(29, 2, 2000),
                                                       "male", "US", "GB"));
    } else if (passport_element_type == "driver_license" || passport_element_type == "dl") {
      if (input_files.size() >= 2) {
        auto front_side = std::move(input_files[0]);
        input_files.erase(input_files.begin());
        auto reverse_side = std::move(input_files[0]);
        input_files.erase(input_files.begin());
        return td_api::make_object<td_api::inputPassportElementDriverLicense>(
            td_api::make_object<td_api::inputIdentityDocument>(
                "1234567890", td_api::make_object<td_api::date>(1, 3, 2029), std::move(front_side),
                std::move(reverse_side), std::move(selfie), std::move(input_files)));
      }
    } else if (passport_element_type == "identity_card" || passport_element_type == "ic") {
      if (input_files.size() >= 2) {
        auto front_side = std::move(input_files[0]);
        input_files.erase(input_files.begin());
        auto reverse_side = std::move(input_files[0]);
        input_files.erase(input_files.begin());
        return td_api::make_object<td_api::inputPassportElementIdentityCard>(
            td_api::make_object<td_api::inputIdentityDocument>("1234567890", nullptr, std::move(front_side),
                                                               std::move(reverse_side), std::move(selfie),
                                                               std::move(input_files)));
      }
    } else if (passport_element_type == "internal_passport" || passport_element_type == "ip") {
      if (input_files.size() >= 1) {
        auto front_side = std::move(input_files[0]);
        input_files.erase(input_files.begin());
        return td_api::make_object<td_api::inputPassportElementInternalPassport>(
            td_api::make_object<td_api::inputIdentityDocument>("1234567890", nullptr, std::move(front_side), nullptr,
                                                               std::move(selfie), std::move(input_files)));
      }
    } else if (passport_element_type == "rental_agreement" || passport_element_type == "ra") {
      vector<td_api::object_ptr<td_api::InputFile>> translation;
      if (selfie != nullptr) {
        translation.push_back(std::move(selfie));
      }
      return td_api::make_object<td_api::inputPassportElementRentalAgreement>(
          td_api::make_object<td_api::inputPersonalDocument>(std::move(input_files), std::move(translation)));
    }

    LOG(ERROR) << "Unsupported passport element type " << passport_element_type;
    return nullptr;
  }

  static td_api::object_ptr<td_api::languagePackInfo> as_language_pack_info(const string &language_code,
                                                                            const string &name,
                                                                            const string &native_name) {
    return td_api::make_object<td_api::languagePackInfo>(language_code, "test", name, native_name, "en", true, true,
                                                         true, true, -1, 5, 3, "abacaba");
  }

  static td_api::object_ptr<td_api::MessageSchedulingState> as_message_scheduling_state(Slice date) {
    date = trim(date);
    if (date.empty()) {
      return nullptr;
    }
    auto send_date = to_integer<int32>(date);
    if (send_date == -1) {
      return td_api::make_object<td_api::messageSchedulingStateSendWhenOnline>();
    }
    return td_api::make_object<td_api::messageSchedulingStateSendAtDate>(send_date);
  }

  static td_api::object_ptr<td_api::BackgroundFill> get_background_fill(int32 color) {
    return td_api::make_object<td_api::backgroundFillSolid>(color);
  }

  static td_api::object_ptr<td_api::BackgroundFill> get_background_fill(int32 top_color, int32 bottom_color) {
    return td_api::make_object<td_api::backgroundFillGradient>(top_color, bottom_color, Random::fast(0, 7) * 45);
  }

  static td_api::object_ptr<td_api::BackgroundType> get_solid_pattern_background(int32 color, int32 intensity,
                                                                                 bool is_moving) {
    return get_gradient_pattern_background(color, color, intensity, is_moving);
  }

  static td_api::object_ptr<td_api::BackgroundType> get_gradient_pattern_background(int32 top_color, int32 bottom_color,
                                                                                    int32 intensity, bool is_moving) {
    return td_api::make_object<td_api::backgroundTypePattern>(get_background_fill(top_color, bottom_color), intensity,
                                                              is_moving);
  }

  static td_api::object_ptr<td_api::BackgroundType> get_solid_background(int32 color) {
    return td_api::make_object<td_api::backgroundTypeFill>(get_background_fill(color));
  }

  static td_api::object_ptr<td_api::BackgroundType> get_gradient_background(int32 top_color, int32 bottom_color) {
    return td_api::make_object<td_api::backgroundTypeFill>(get_background_fill(top_color, bottom_color));
  }

  static td_api::object_ptr<td_api::Object> execute(td_api::object_ptr<td_api::Function> f) {
    if (GET_VERBOSITY_LEVEL() < VERBOSITY_NAME(td_requests)) {
      LOG(ERROR) << "Execute request: " << to_string(f);
    }
    auto res = ClientActor::execute(std::move(f));
    if (GET_VERBOSITY_LEVEL() < VERBOSITY_NAME(td_requests)) {
      LOG(ERROR) << "Execute response: " << to_string(res);
    }
    return res;
  }

  uint64 send_request(td_api::object_ptr<td_api::Function> f) {
    static uint64 query_num = 1;
    if (!td_client_.empty()) {
      auto id = query_num++;
      send_closure_later(td_client_, &ClientActor::request, id, std::move(f));
      return id;
    } else {
      LOG(ERROR) << "Failed to send: " << to_string(f);
      return 0;
    }
  }

  void send_message(const string &chat_id, td_api::object_ptr<td_api::InputMessageContent> &&input_message_content,
                    bool disable_notification = false, bool from_background = false, int64 reply_to_message_id = 0) {
    auto chat = as_chat_id(chat_id);
    auto id = send_request(td_api::make_object<td_api::sendMessage>(
        chat, reply_to_message_id,
        td_api::make_object<td_api::sendMessageOptions>(disable_notification, from_background,
                                                        as_message_scheduling_state(schedule_date_)),
        nullptr, std::move(input_message_content)));
    query_id_to_send_message_info_[id].start_time = Time::now();
  }

  td_api::object_ptr<td_api::sendMessageOptions> default_send_message_options() const {
    return td_api::make_object<td_api::sendMessageOptions>(false, false, as_message_scheduling_state(schedule_date_));
  }

  void send_get_background_url(td_api::object_ptr<td_api::BackgroundType> &&background_type) {
    send_request(td_api::make_object<td_api::getBackgroundUrl>("asd", std::move(background_type)));
  }

  void on_cmd(string cmd) {
    // TODO: need to remove https://en.wikipedia.org/wiki/ANSI_escape_code from cmd
    td::remove_if(cmd, [](unsigned char c) { return c < 32; });
    LOG(INFO) << "CMD:[" << cmd << "]";

    string op;
    string args;
    std::tie(op, args) = split(cmd);

    const int32 OP_BLOCK_COUNT = 5;
    int32 op_not_found_count = 0;

    if (op == "gas") {
      send_request(td_api::make_object<td_api::getAuthorizationState>());
    } else if (op == "sap") {
      send_request(td_api::make_object<td_api::setAuthenticationPhoneNumber>(args, nullptr));
    } else if (op == "rac") {
      send_request(td_api::make_object<td_api::resendAuthenticationCode>());
    } else if (op == "cdek" || op == "CheckDatabaseEncryptionKey") {
      send_request(td_api::make_object<td_api::checkDatabaseEncryptionKey>(args));
    } else if (op == "sdek" || op == "SetDatabaseEncryptionKey") {
      send_request(td_api::make_object<td_api::setDatabaseEncryptionKey>(args));
    } else if (op == "cac") {
      send_request(td_api::make_object<td_api::checkAuthenticationCode>(args));
    } else if (op == "ru") {
      string first_name;
      string last_name;

      std::tie(first_name, last_name) = split(args);

      send_request(td_api::make_object<td_api::registerUser>(first_name, last_name));
    } else if (op == "cap") {
      send_request(td_api::make_object<td_api::checkAuthenticationPassword>(args));
    } else if (op == "cabt") {
      send_request(td_api::make_object<td_api::checkAuthenticationBotToken>(args));
    } else if (op == "qr") {
      send_request(td_api::make_object<td_api::requestQrCodeAuthentication>(as_user_ids(args)));
    } else if (op == "cqr") {
      send_request(td_api::make_object<td_api::confirmQrCodeAuthentication>(args));
    } else if (op == "gcs") {
      send_request(td_api::make_object<td_api::getCurrentState>());
    } else if (op == "rapr") {
      send_request(td_api::make_object<td_api::requestAuthenticationPasswordRecovery>());
    } else if (op == "rap") {
      send_request(td_api::make_object<td_api::recoverAuthenticationPassword>(args));
    } else if (op == "lo" || op == "LogOut" || op == "logout") {
      send_request(td_api::make_object<td_api::logOut>());
    } else if (op == "destroy") {
      send_request(td_api::make_object<td_api::destroy>());
    } else if (op == "reset") {
      td_client_.reset();
    } else if (op == "close_td") {
      // send_request(td_api::make_object<td_api::getCurrentState>());
      send_request(td_api::make_object<td_api::close>());
      // send_request(td_api::make_object<td_api::getCurrentState>());
      // send_request(td_api::make_object<td_api::close>());
    } else if (op == "DeleteAccountYesIReallyWantToDeleteMyAccount") {
      send_request(td_api::make_object<td_api::deleteAccount>(args));
    } else if (op == "gps" || op == "GetPasswordState") {
      send_request(td_api::make_object<td_api::getPasswordState>());
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
      send_request(
          td_api::make_object<td_api::setPassword>(password, new_password, new_hint, true, recovery_email_address));
    } else if (op == "gpafhttp") {
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
      send_request(
          td_api::make_object<td_api::getPassportAuthorizationForm>(as_user_id(bot_id), scope, public_key, payload));
    } else if (op == "gpaf") {
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

      std::tie(bot_id, args) = split(args);
      std::tie(scope, payload) = split(args);
      send_request(
          td_api::make_object<td_api::getPassportAuthorizationForm>(as_user_id(bot_id), scope, public_key, payload));
    } else if (op == "gpafae") {
      string form_id;
      string password;
      std::tie(form_id, password) = split(args);
      send_request(td_api::make_object<td_api::getPassportAuthorizationFormAvailableElements>(
          to_integer<int32>(form_id), password));
    } else if (op == "spaf") {
      string form_id;
      string types;
      std::tie(form_id, types) = split(args);
      send_request(td_api::make_object<td_api::sendPassportAuthorizationForm>(to_integer<int32>(form_id),
                                                                              as_passport_element_types(types)));
    } else if (op == "gpcl") {
      send_request(td_api::make_object<td_api::getPreferredCountryLanguage>(args));
    } else if (op == "spnvc" || op == "SendPhoneNumberVerificationCode") {
      send_request(td_api::make_object<td_api::sendPhoneNumberVerificationCode>(args, nullptr));
    } else if (op == "cpnvc" || op == "CheckPhoneNumberVerificationCode") {
      send_request(td_api::make_object<td_api::checkPhoneNumberVerificationCode>(args));
    } else if (op == "rpnvc" || op == "ResendPhoneNumberVerificationCode") {
      send_request(td_api::make_object<td_api::resendPhoneNumberVerificationCode>());
    } else if (op == "seavc" || op == "SendEmailAddressVerificationCode") {
      send_request(td_api::make_object<td_api::sendEmailAddressVerificationCode>(args));
    } else if (op == "ceavc" || op == "CheckEmailAddressVerificationCode") {
      send_request(td_api::make_object<td_api::checkEmailAddressVerificationCode>(args));
    } else if (op == "reavc" || op == "ResendEmailAddressVerificationCode") {
      send_request(td_api::make_object<td_api::resendEmailAddressVerificationCode>());
    } else if (op == "srea" || op == "SetRecoveryEmailAddress") {
      string password;
      string recovery_email_address;
      std::tie(password, recovery_email_address) = split(args);
      send_request(td_api::make_object<td_api::setRecoveryEmailAddress>(password, recovery_email_address));
    } else if (op == "grea" || op == "GetRecoveryEmailAddress") {
      send_request(td_api::make_object<td_api::getRecoveryEmailAddress>(args));
    } else if (op == "creac") {
      send_request(td_api::make_object<td_api::checkRecoveryEmailAddressCode>(args));
    } else if (op == "rreac") {
      send_request(td_api::make_object<td_api::resendRecoveryEmailAddressCode>());
    } else if (op == "spncc") {
      send_request(td_api::make_object<td_api::sendPhoneNumberVerificationCode>(args, nullptr));
    } else if (op == "cpncc") {
      send_request(td_api::make_object<td_api::checkPhoneNumberVerificationCode>(args));
    } else if (op == "rpncc") {
      send_request(td_api::make_object<td_api::resendPhoneNumberVerificationCode>());
    } else if (op == "rpr" || op == "RequestPasswordRecovery") {
      send_request(td_api::make_object<td_api::requestPasswordRecovery>());
    } else if (op == "rp" || op == "RecoverPassword") {
      send_request(td_api::make_object<td_api::recoverPassword>(args));
    } else if (op == "gtp" || op == "GetTemporaryPassword") {
      send_request(td_api::make_object<td_api::getTemporaryPasswordState>());
    } else if (op == "ctp" || op == "CreateTemporaryPassword") {
      send_request(td_api::make_object<td_api::createTemporaryPassword>(args, 60 * 6));
    } else if (op == "gpe") {
      string password;
      string passport_element_type;
      std::tie(password, passport_element_type) = split(args);
      send_request(
          td_api::make_object<td_api::getPassportElement>(as_passport_element_type(passport_element_type), password));
    } else if (op == "gape") {
      string password = args;
      send_request(td_api::make_object<td_api::getAllPassportElements>(password));
    } else if (op == "spe" || op == "spes") {
      string password;
      string passport_element_type;
      string arg;
      std::tie(password, args) = split(args);
      std::tie(passport_element_type, arg) = split(args);
      send_request(td_api::make_object<td_api::setPassportElement>(
          as_input_passport_element(passport_element_type, arg, op == "spes"), password));
    } else if (op == "dpe") {
      string passport_element_type = args;
      send_request(td_api::make_object<td_api::deletePassportElement>(as_passport_element_type(passport_element_type)));
    } else if (op == "ppn") {
      send_request(td_api::make_object<td_api::processPushNotification>(args));
    } else if (op == "gpri") {
      send_request(td_api::make_object<td_api::getPushReceiverId>(args));
    } else if (op == "rda") {
      send_request(td_api::make_object<td_api::registerDevice>(
          td_api::make_object<td_api::deviceTokenApplePush>(args, true), as_user_ids("")));
    } else if (op == "rdb") {
      send_request(td_api::make_object<td_api::registerDevice>(
          td_api::make_object<td_api::deviceTokenBlackBerryPush>(args), as_user_ids("")));
    } else if (op == "rdf") {
      send_request(td_api::make_object<td_api::registerDevice>(
          td_api::make_object<td_api::deviceTokenFirebaseCloudMessaging>(args, true), as_user_ids("")));
    } else if (op == "rdt") {
      string token;
      string other_user_ids_str;

      std::tie(token, other_user_ids_str) = split(args);
      send_request(td_api::make_object<td_api::registerDevice>(td_api::make_object<td_api::deviceTokenTizenPush>(token),
                                                               as_user_ids(other_user_ids_str)));
    } else if (op == "rdu") {
      string token;
      string other_user_ids_str;

      std::tie(token, other_user_ids_str) = split(args);
      send_request(td_api::make_object<td_api::registerDevice>(
          td_api::make_object<td_api::deviceTokenUbuntuPush>(token), as_user_ids(other_user_ids_str)));
    } else if (op == "rdw") {
      string endpoint;
      string key;
      string secret;
      string other_user_ids_str;

      std::tie(endpoint, args) = split(args);
      std::tie(key, args) = split(args);
      std::tie(secret, other_user_ids_str) = split(args);
      send_request(td_api::make_object<td_api::registerDevice>(
          td_api::make_object<td_api::deviceTokenWebPush>(endpoint, key, secret), as_user_ids(other_user_ids_str)));
    } else if (op == "gbci") {
      send_request(td_api::make_object<td_api::getBankCardInfo>(args));
    } else if (op == "gpf") {
      string chat_id;
      string message_id;

      std::tie(chat_id, message_id) = split(args);
      send_request(td_api::make_object<td_api::getPaymentForm>(as_chat_id(chat_id), as_message_id(message_id)));
    } else if (op == "voi") {
      string chat_id;
      string message_id;
      string allow_save;

      std::tie(chat_id, args) = split(args);
      std::tie(message_id, allow_save) = split(args);
      send_request(td_api::make_object<td_api::validateOrderInfo>(as_chat_id(chat_id), as_message_id(message_id),
                                                                  nullptr, as_bool(allow_save)));
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
      send_request(td_api::make_object<td_api::sendPaymentForm>(
          as_chat_id(chat_id), as_message_id(message_id), order_info_id, shipping_option_id,
          td_api::make_object<td_api::inputCredentialsSaved>(saved_credentials_id)));
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
      send_request(td_api::make_object<td_api::sendPaymentForm>(
          as_chat_id(chat_id), as_message_id(message_id), order_info_id, shipping_option_id,
          td_api::make_object<td_api::inputCredentialsNew>(data, true)));
    } else if (op == "gpre") {
      string chat_id;
      string message_id;

      std::tie(chat_id, message_id) = split(args);
      send_request(td_api::make_object<td_api::getPaymentReceipt>(as_chat_id(chat_id), as_message_id(message_id)));
    } else if (op == "gsoi") {
      send_request(td_api::make_object<td_api::getSavedOrderInfo>());
    } else if (op == "dsoi") {
      send_request(td_api::make_object<td_api::deleteSavedOrderInfo>());
    } else if (op == "dsc") {
      send_request(td_api::make_object<td_api::deleteSavedCredentials>());
      // } else if (op == "stlsr") {
      //   send_request(td_api::make_object<td_api::sendTonLiteServerRequest>());
      // } else if (op == "gtwps") {
      //   send_request(td_api::make_object<td_api::getTonWalletPasswordSalt>());
    } else if (op == "gpr") {
      send_request(td_api::make_object<td_api::getUserPrivacySettingRules>(get_user_privacy_setting(args)));
    } else if (op == "spr") {
      string setting;
      string allow;
      string ids;
      std::tie(setting, args) = split(args);
      std::tie(allow, ids) = split(args);

      std::vector<td_api::object_ptr<td_api::UserPrivacySettingRule>> rules;
      if (allow == "c" || allow == "contacts") {
        rules.push_back(td_api::make_object<td_api::userPrivacySettingRuleAllowContacts>());
      } else if (allow == "users") {
        rules.push_back(td_api::make_object<td_api::userPrivacySettingRuleAllowUsers>(as_user_ids(ids)));
      } else if (allow == "chats") {
        rules.push_back(td_api::make_object<td_api::userPrivacySettingRuleAllowChatMembers>(as_chat_ids(ids)));
      } else if (as_bool(allow)) {
        rules.push_back(td_api::make_object<td_api::userPrivacySettingRuleAllowAll>());
        rules.push_back(td_api::make_object<td_api::userPrivacySettingRuleRestrictAll>());
      } else {
        rules.push_back(td_api::make_object<td_api::userPrivacySettingRuleRestrictAll>());
      }
      send_request(td_api::make_object<td_api::setUserPrivacySettingRules>(
          get_user_privacy_setting(setting), td_api::make_object<td_api::userPrivacySettingRules>(std::move(rules))));
    } else if (op == "cp" || op == "ChangePhone") {
      send_request(td_api::make_object<td_api::changePhoneNumber>(args, nullptr));
    } else if (op == "ccpc" || op == "CheckChangePhoneCode") {
      send_request(td_api::make_object<td_api::checkChangePhoneNumberCode>(args));
    } else if (op == "rcpc" || op == "ResendChangePhoneCode") {
      send_request(td_api::make_object<td_api::resendChangePhoneNumberCode>());
    } else if (op == "gco") {
      if (args.empty()) {
        send_request(td_api::make_object<td_api::getContacts>());
      } else {
        auto limit = to_integer<int32>(args);
        send_request(td_api::make_object<td_api::searchContacts>("", limit));
      }
    } else if (op == "AddContact") {
      string user_id;
      string first_name;
      string last_name;
      std::tie(user_id, args) = split(args);
      std::tie(first_name, last_name) = split(args);

      send_request(td_api::make_object<td_api::addContact>(
          td_api::make_object<td_api::contact>(string(), first_name, last_name, string(), as_user_id(user_id)), false));
    } else if (op == "spn") {
      string user_id = args;

      send_request(td_api::make_object<td_api::sharePhoneNumber>(as_user_id(user_id)));
    } else if (op == "ImportContacts" || op == "cic") {
      vector<string> contacts_str = full_split(args, ';');
      vector<td_api::object_ptr<td_api::contact>> contacts;
      for (auto c : contacts_str) {
        string phone_number;
        string first_name;
        string last_name;
        std::tie(phone_number, c) = split(c, ',');
        std::tie(first_name, last_name) = split(c, ',');
        contacts.push_back(td_api::make_object<td_api::contact>(phone_number, first_name, last_name, string(), 0));
      }

      if (op == "cic") {
        send_request(td_api::make_object<td_api::changeImportedContacts>(std::move(contacts)));
      } else {
        send_request(td_api::make_object<td_api::importContacts>(std::move(contacts)));
      }
    } else if (op == "RemoveContacts") {
      send_request(td_api::make_object<td_api::removeContacts>(as_user_ids(args)));
    } else if (op == "gicc") {
      send_request(td_api::make_object<td_api::getImportedContactCount>());
    } else if (op == "ClearImportedContacts") {
      send_request(td_api::make_object<td_api::clearImportedContacts>());
    } else {
      op_not_found_count++;
    }

    if (op == "gc" || op == "GetChats" || op == "gca" || begins_with(op, "gc-")) {
      string limit;
      string offset_order_string;
      string offset_chat_id;

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
      send_request(td_api::make_object<td_api::getChats>(as_chat_list(op), offset_order, as_chat_id(offset_chat_id),
                                                         to_integer<int32>(limit)));
    } else if (op == "gctest") {
      send_request(td_api::make_object<td_api::getChats>(nullptr, std::numeric_limits<int64>::max(), 0, 1));
      send_request(td_api::make_object<td_api::getChats>(nullptr, std::numeric_limits<int64>::max(), 0, 10));
      send_request(td_api::make_object<td_api::getChats>(nullptr, std::numeric_limits<int64>::max(), 0, 5));
    } else if (op == "gcc" || op == "GetCommonChats") {
      string user_id;
      string offset_chat_id;
      string limit;

      std::tie(user_id, args) = split(args);
      std::tie(offset_chat_id, limit) = split(args);

      if (limit.empty()) {
        limit = "100";
      }
      send_request(td_api::make_object<td_api::getGroupsInCommon>(as_user_id(user_id), as_chat_id(offset_chat_id),
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
        send_request(td_api::make_object<td_api::getChatHistory>(as_chat_id(chat_id), as_message_id(from_message_id),
                                                                 to_integer<int32>(offset), to_integer<int32>(limit),
                                                                 op == "ghl"));
      }
    } else if (op == "gcsm") {
      string chat_id = args;
      send_request(td_api::make_object<td_api::getChatScheduledMessages>(as_chat_id(chat_id)));
    } else if (op == "ghf") {
      get_history_chat_id_ = as_chat_id(args);

      send_request(td_api::make_object<td_api::getChatHistory>(get_history_chat_id_, std::numeric_limits<int64>::max(),
                                                               0, 100, false));
    } else if (op == "spvf") {
      search_chat_id_ = as_chat_id(args);

      send_request(td_api::make_object<td_api::searchChatMessages>(
          search_chat_id_, "", 0, 0, 0, 100, td_api::make_object<td_api::searchMessagesFilterPhotoAndVideo>()));
    } else if (op == "Search" || op == "SearchA" || op == "SearchM") {
      string from_date;
      string limit;
      string query;

      std::tie(query, args) = split(args);
      std::tie(limit, from_date) = split(args);
      if (from_date.empty()) {
        from_date = "0";
      }
      td_api::object_ptr<td_api::ChatList> chat_list;
      if (op == "SearchA") {
        chat_list = td_api::make_object<td_api::chatListArchive>();
      }
      if (op == "SearchM") {
        chat_list = td_api::make_object<td_api::chatListMain>();
      }
      send_request(td_api::make_object<td_api::searchMessages>(
          std::move(chat_list), query, to_integer<int32>(from_date), 2147482647, 0, to_integer<int32>(limit)));
    } else if (op == "SCM") {
      string chat_id;
      string limit;
      string query;

      std::tie(chat_id, args) = split(args);
      std::tie(limit, query) = split(args);
      if (limit.empty()) {
        limit = "10";
      }

      send_request(td_api::make_object<td_api::searchChatMessages>(as_chat_id(chat_id), query, 0, 0, 0,
                                                                   to_integer<int32>(limit), nullptr));
    } else if (op == "SMME") {
      string chat_id;
      string limit;

      std::tie(chat_id, limit) = split(args);
      if (limit.empty()) {
        limit = "10";
      }

      send_request(td_api::make_object<td_api::searchChatMessages>(as_chat_id(chat_id), "", my_id_, 0, 0,
                                                                   to_integer<int32>(limit), nullptr));
    } else if (op == "SMU") {
      string chat_id;
      string user_id;
      string limit;

      std::tie(chat_id, args) = split(args);
      std::tie(user_id, limit) = split(args);
      if (limit.empty()) {
        limit = "10";
      }

      send_request(td_api::make_object<td_api::searchChatMessages>(as_chat_id(chat_id), "", as_user_id(user_id), 0, 0,
                                                                   to_integer<int32>(limit), nullptr));
    } else if (op == "SM") {
      string chat_id;
      string filter;
      string limit;
      string offset_message_id;
      string offset;

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

      send_request(td_api::make_object<td_api::searchChatMessages>(
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

      send_request(td_api::make_object<td_api::searchCallMessages>(as_message_id(offset_message_id),
                                                                   to_integer<int32>(limit), as_bool(only_missed)));
    } else if (op == "SCRLM") {
      string chat_id;
      string limit;

      std::tie(chat_id, limit) = split(args);
      if (limit.empty()) {
        limit = "10";
      }

      send_request(
          td_api::make_object<td_api::searchChatRecentLocationMessages>(as_chat_id(chat_id), to_integer<int32>(limit)));
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
      send_request(td_api::make_object<td_api::searchChatMessages>(
          as_chat_id(chat_id), query, 0, as_message_id(offset_message_id), 0, to_integer<int32>(limit),
          td_api::make_object<td_api::searchMessagesFilterAudio>()));
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
      send_request(td_api::make_object<td_api::searchChatMessages>(
          as_chat_id(chat_id), query, 0, to_integer<int64>(offset_message_id), 0, to_integer<int32>(limit),
          td_api::make_object<td_api::searchMessagesFilterDocument>()));
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
      send_request(td_api::make_object<td_api::searchChatMessages>(
          as_chat_id(chat_id), query, 0, as_message_id(offset_message_id), 0, to_integer<int32>(limit),
          td_api::make_object<td_api::searchMessagesFilterPhoto>()));
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
      send_request(td_api::make_object<td_api::searchChatMessages>(
          as_chat_id(chat_id), query, 0, as_message_id(offset_message_id), 0, to_integer<int32>(limit),
          td_api::make_object<td_api::searchMessagesFilterChatPhoto>()));
    } else if (op == "gcmc") {
      string chat_id;
      string filter;
      string return_local;

      std::tie(chat_id, args) = split(args);
      std::tie(filter, return_local) = split(args);

      send_request(td_api::make_object<td_api::getChatMessageCount>(
          as_chat_id(chat_id), get_search_messages_filter(filter), as_bool(return_local)));
    } else if (op == "gup" || op == "gupp") {
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
        send_request(td_api::make_object<td_api::getUserProfilePhotos>(as_user_id(user_id), to_integer<int32>(offset),
                                                                       to_integer<int32>(limit)));
      }
    } else if (op == "dcrm") {
      string chat_id;
      string message_id;

      std::tie(chat_id, message_id) = split(args);
      send_request(td_api::make_object<td_api::deleteChatReplyMarkup>(as_chat_id(chat_id), as_message_id(message_id)));
    } else if (op == "glti") {
      send_request(td_api::make_object<td_api::getLocalizationTargetInfo>(as_bool(args)));
    } else if (op == "glpi") {
      send_request(td_api::make_object<td_api::getLanguagePackInfo>(args));
    } else if (op == "glps") {
      string language_code;
      string keys;

      std::tie(language_code, keys) = split(args);
      send_request(td_api::make_object<td_api::getLanguagePackStrings>(language_code, full_split(keys)));
    } else if (op == "glpss") {
      string language_database_path;
      string language_pack;
      string language_code;
      string key;

      std::tie(language_database_path, args) = split(args);
      std::tie(language_pack, args) = split(args);
      std::tie(language_code, key) = split(args);
      send_request(td_api::make_object<td_api::getLanguagePackString>(language_database_path, language_pack,
                                                                      language_code, key));
    } else if (op == "synclp") {
      string language_code = args;
      send_request(td_api::make_object<td_api::synchronizeLanguagePack>(language_code));
    } else if (op == "acslp") {
      string language_code = args;
      send_request(td_api::make_object<td_api::addCustomServerLanguagePack>(language_code));
    } else if (op == "sclp") {
      string language_code;
      string name;
      string native_name;
      string key;

      std::tie(language_code, args) = split(args);
      std::tie(name, args) = split(args);
      std::tie(native_name, key) = split(args);

      vector<td_api::object_ptr<td_api::languagePackString>> strings;
      strings.push_back(td_api::make_object<td_api::languagePackString>(
          key, td_api::make_object<td_api::languagePackStringValueOrdinary>("Ordinary value")));
      strings.push_back(td_api::make_object<td_api::languagePackString>(
          "Plu", td_api::make_object<td_api::languagePackStringValuePluralized>("Zero", string("One\0One", 7), "Two",
                                                                                "Few", "Many", "Other")));
      strings.push_back(td_api::make_object<td_api::languagePackString>(
          "DELETED", td_api::make_object<td_api::languagePackStringValueDeleted>()));

      send_request(td_api::make_object<td_api::setCustomLanguagePack>(
          as_language_pack_info(language_code, name, native_name), std::move(strings)));
    } else if (op == "eclpi") {
      string language_code;
      string name;
      string native_name;

      std::tie(language_code, args) = split(args);
      std::tie(name, native_name) = split(args);

      send_request(td_api::make_object<td_api::editCustomLanguagePackInfo>(
          as_language_pack_info(language_code, name, native_name)));
    } else if (op == "sclpsv" || op == "sclpsp" || op == "sclpsd") {
      string language_code;
      string key;
      string value;

      std::tie(language_code, args) = split(args);
      std::tie(key, value) = split(args);

      td_api::object_ptr<td_api::languagePackString> str =
          td_api::make_object<td_api::languagePackString>(key, nullptr);
      if (op == "sclsv") {
        str->value_ = td_api::make_object<td_api::languagePackStringValueOrdinary>(value);
      } else if (op == "sclsp") {
        str->value_ = td_api::make_object<td_api::languagePackStringValuePluralized>(value, string("One\0One", 7),
                                                                                     "Two", "Few", "Many", "Other");
      } else {
        str->value_ = td_api::make_object<td_api::languagePackStringValueDeleted>();
      }

      send_request(td_api::make_object<td_api::setCustomLanguagePackString>(language_code, std::move(str)));
    } else if (op == "dlp") {
      send_request(td_api::make_object<td_api::deleteLanguagePack>(args));
    } else if (op == "go") {
      send_request(td_api::make_object<td_api::getOption>(args));
    } else if (op == "sob") {
      string name;
      string value;

      std::tie(name, value) = split(args);
      send_request(td_api::make_object<td_api::setOption>(
          name, td_api::make_object<td_api::optionValueBoolean>(as_bool(value))));
    } else if (op == "soe") {
      send_request(td_api::make_object<td_api::setOption>(args, td_api::make_object<td_api::optionValueEmpty>()));
    } else if (op == "soi") {
      string name;
      string value;

      std::tie(name, value) = split(args);
      int32 value_int = to_integer<int32>(value);
      send_request(
          td_api::make_object<td_api::setOption>(name, td_api::make_object<td_api::optionValueInteger>(value_int)));
    } else if (op == "sos") {
      string name;
      string value;

      std::tie(name, value) = split(args);
      send_request(td_api::make_object<td_api::setOption>(name, td_api::make_object<td_api::optionValueString>(value)));
    } else if (op == "me") {
      send_request(td_api::make_object<td_api::getMe>());
    } else if (op == "sattl") {
      send_request(
          td_api::make_object<td_api::setAccountTtl>(td_api::make_object<td_api::accountTtl>(to_integer<int32>(args))));
    } else if (op == "gattl") {
      send_request(td_api::make_object<td_api::getAccountTtl>());
    } else if (op == "GetActiveSessions") {
      send_request(td_api::make_object<td_api::getActiveSessions>());
    } else if (op == "TerminateSession") {
      send_request(td_api::make_object<td_api::terminateSession>(to_integer<int64>(args)));
    } else if (op == "TerminateAllOtherSessions") {
      send_request(td_api::make_object<td_api::terminateAllOtherSessions>());
    } else if (op == "gcw") {
      send_request(td_api::make_object<td_api::getConnectedWebsites>());
    } else if (op == "dw") {
      send_request(td_api::make_object<td_api::disconnectWebsite>(to_integer<int64>(args)));
    } else if (op == "daw") {
      send_request(td_api::make_object<td_api::disconnectAllWebsites>());
    } else if (op == "gbgs") {
      send_request(td_api::make_object<td_api::getBackgrounds>(as_bool(args)));
    } else if (op == "gbgu") {
      send_get_background_url(td_api::make_object<td_api::backgroundTypeWallpaper>(false, false));
      send_get_background_url(td_api::make_object<td_api::backgroundTypeWallpaper>(false, true));
      send_get_background_url(td_api::make_object<td_api::backgroundTypeWallpaper>(true, false));
      send_get_background_url(td_api::make_object<td_api::backgroundTypeWallpaper>(true, true));
      send_get_background_url(get_solid_pattern_background(-1, 0, false));
      send_get_background_url(get_solid_pattern_background(0x1000000, 0, true));
      send_get_background_url(get_solid_pattern_background(0, -1, false));
      send_get_background_url(get_solid_pattern_background(0, 101, false));
      send_get_background_url(get_solid_pattern_background(0, 0, false));
      send_get_background_url(get_solid_pattern_background(0xFFFFFF, 100, true));
      send_get_background_url(get_solid_pattern_background(0xABCDEF, 49, true));
      send_get_background_url(get_gradient_pattern_background(0, 0, 0, false));
      send_get_background_url(get_gradient_pattern_background(0xFFFFFF, 0, 100, true));
      send_get_background_url(get_gradient_pattern_background(0xABCDEF, 0xFEDCBA, 49, true));
      send_get_background_url(get_gradient_pattern_background(0, 0x1000000, 49, true));
      send_get_background_url(get_solid_background(-1));
      send_get_background_url(get_solid_background(0xABCDEF));
      send_get_background_url(get_solid_background(0x1000000));
      send_get_background_url(get_gradient_background(0xABCDEF, 0xFEDCBA));
      send_get_background_url(get_gradient_background(0, 0));
      send_get_background_url(get_gradient_background(-1, -1));
    } else if (op == "sbg") {
      send_request(td_api::make_object<td_api::searchBackground>(args));
    } else if (op == "sbgd") {
      send_request(td_api::make_object<td_api::setBackground>(nullptr, nullptr, as_bool(args)));
    } else if (op == "sbgw" || op == "sbgwd") {
      send_request(td_api::make_object<td_api::setBackground>(
          td_api::make_object<td_api::inputBackgroundLocal>(as_input_file(args)),
          td_api::make_object<td_api::backgroundTypeWallpaper>(true, true), op == "sbgwd"));
    } else if (op == "sbgp" || op == "sbgpd") {
      send_request(td_api::make_object<td_api::setBackground>(
          td_api::make_object<td_api::inputBackgroundLocal>(as_input_file(args)),
          get_solid_pattern_background(0xABCDEF, 49, true), op == "sbgpd"));
    } else if (op == "sbggp" || op == "sbggpd") {
      send_request(td_api::make_object<td_api::setBackground>(
          td_api::make_object<td_api::inputBackgroundLocal>(as_input_file(args)),
          get_gradient_pattern_background(0xABCDEF, 0xFE, 51, false), op == "sbggpd"));
    } else if (op == "sbgs" || op == "sbgsd") {
      send_request(td_api::make_object<td_api::setBackground>(nullptr, get_solid_background(to_integer<int32>(args)),
                                                              op == "sbgsd"));
    } else if (op == "sbgg" || op == "sbggd") {
      string top_color;
      string bottom_color;
      std::tie(top_color, bottom_color) = split(args);
      auto background_type = get_gradient_background(to_integer<int32>(top_color), to_integer<int32>(bottom_color));
      send_request(td_api::make_object<td_api::setBackground>(nullptr, std::move(background_type), op == "sbggd"));
    } else if (op == "sbgwid" || op == "sbgwidd") {
      send_request(td_api::make_object<td_api::setBackground>(
          td_api::make_object<td_api::inputBackgroundRemote>(to_integer<int64>(args)),
          td_api::make_object<td_api::backgroundTypeWallpaper>(true, true), op == "sbgwidd"));
    } else if (op == "sbgpid" || op == "sbgpidd") {
      send_request(td_api::make_object<td_api::setBackground>(
          td_api::make_object<td_api::inputBackgroundRemote>(to_integer<int64>(args)),
          get_solid_pattern_background(0xabcdef, 49, true), op == "sbgpidd"));
    } else if (op == "rbg") {
      send_request(td_api::make_object<td_api::removeBackground>(to_integer<int64>(args)));
    } else if (op == "rbgs") {
      send_request(td_api::make_object<td_api::resetBackgrounds>());
    } else if (op == "gccode") {
      send_request(td_api::make_object<td_api::getCountryCode>());
    } else if (op == "git") {
      send_request(td_api::make_object<td_api::getInviteText>());
    } else if (op == "atos") {
      send_request(td_api::make_object<td_api::acceptTermsOfService>(args));
    } else if (op == "gdli") {
      send_request(td_api::make_object<td_api::getDeepLinkInfo>(args));
    } else if (op == "tme") {
      send_request(td_api::make_object<td_api::getRecentlyVisitedTMeUrls>(args));
    } else if (op == "bu") {
      send_request(td_api::make_object<td_api::blockUser>(as_user_id(args)));
    } else if (op == "ubu") {
      send_request(td_api::make_object<td_api::unblockUser>(as_user_id(args)));
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
      send_request(td_api::make_object<td_api::getBlockedUsers>(to_integer<int32>(offset), to_integer<int32>(limit)));
    } else if (op == "gu") {
      send_request(td_api::make_object<td_api::getUser>(as_user_id(args)));
    } else if (op == "gsu") {
      send_request(td_api::make_object<td_api::getSupportUser>());
    } else if (op == "gs") {
      string limit;
      string emoji;
      std::tie(limit, emoji) = split(args);
      send_request(td_api::make_object<td_api::getStickers>(emoji, to_integer<int32>(limit)));
    } else if (op == "sst") {
      string limit;
      string emoji;
      std::tie(limit, emoji) = split(args);
      send_request(td_api::make_object<td_api::searchStickers>(emoji, to_integer<int32>(limit)));
    } else if (op == "gss") {
      send_request(td_api::make_object<td_api::getStickerSet>(to_integer<int64>(args)));
    } else if (op == "giss") {
      send_request(td_api::make_object<td_api::getInstalledStickerSets>(as_bool(args)));
    } else if (op == "gass") {
      string is_masks;
      string offset_sticker_set_id;
      string limit;

      std::tie(is_masks, args) = split(args);
      std::tie(offset_sticker_set_id, limit) = split(args);

      send_request(td_api::make_object<td_api::getArchivedStickerSets>(
          as_bool(is_masks), to_integer<int64>(offset_sticker_set_id), to_integer<int32>(limit)));
    } else if (op == "gtss") {
      string offset;
      string limit;

      std::tie(offset, limit) = split(args);
      if (limit.empty()) {
        limit = "1000";
      }
      send_request(
          td_api::make_object<td_api::getTrendingStickerSets>(to_integer<int32>(offset), to_integer<int32>(limit)));
    } else if (op == "gatss") {
      send_request(td_api::make_object<td_api::getAttachedStickerSets>(as_file_id(args)));
    } else if (op == "storage") {
      auto chat_limit = to_integer<int32>(args);
      send_request(td_api::make_object<td_api::getStorageStatistics>(chat_limit));
    } else if (op == "storage_fast") {
      send_request(td_api::make_object<td_api::getStorageStatisticsFast>());
    } else if (op == "database") {
      send_request(td_api::make_object<td_api::getDatabaseStatistics>());
    } else if (op == "optimize_storage" || op == "optimize_storage_all") {
      string chat_ids;
      string exclude_chat_ids;
      string chat_ids_limit;
      std::tie(chat_ids, args) = split(args);
      std::tie(exclude_chat_ids, chat_ids_limit) = split(args);
      send_request(td_api::make_object<td_api::optimizeStorage>(
          10000000, -1, -1, 0, std::vector<td_api::object_ptr<td_api::FileType>>(), as_chat_ids(chat_ids),
          as_chat_ids(exclude_chat_ids), op == "optimize_storage", to_integer<int32>(chat_ids_limit)));
    } else if (op == "clean_storage_default") {
      send_request(td_api::make_object<td_api::optimizeStorage>());
    } else if (op == "clean_photos") {
      std::vector<td_api::object_ptr<td_api::FileType>> types;
      types.push_back(td_api::make_object<td_api::fileTypePhoto>());
      send_request(td_api::make_object<td_api::optimizeStorage>(0, 0, 0, 0, std::move(types), as_chat_ids(""),
                                                                as_chat_ids(""), true, 20));
    } else if (op == "clean_storage") {
      std::vector<td_api::object_ptr<td_api::FileType>> types;
      types.push_back(td_api::make_object<td_api::fileTypeThumbnail>());
      types.push_back(td_api::make_object<td_api::fileTypeProfilePhoto>());
      types.push_back(td_api::make_object<td_api::fileTypePhoto>());
      types.push_back(td_api::make_object<td_api::fileTypeVoiceNote>());
      types.push_back(td_api::make_object<td_api::fileTypeVideo>());
      types.push_back(td_api::make_object<td_api::fileTypeDocument>());
      types.push_back(td_api::make_object<td_api::fileTypeSecret>());
      types.push_back(td_api::make_object<td_api::fileTypeUnknown>());
      types.push_back(td_api::make_object<td_api::fileTypeSticker>());
      types.push_back(td_api::make_object<td_api::fileTypeAudio>());
      types.push_back(td_api::make_object<td_api::fileTypeAnimation>());
      types.push_back(td_api::make_object<td_api::fileTypeVideoNote>());
      types.push_back(td_api::make_object<td_api::fileTypeSecure>());
      send_request(td_api::make_object<td_api::optimizeStorage>(0, -1, -1, 0, std::move(types), as_chat_ids(args),
                                                                as_chat_ids(""), true, 20));
    } else if (op == "network") {
      send_request(td_api::make_object<td_api::getNetworkStatistics>());
    } else if (op == "current_network") {
      send_request(td_api::make_object<td_api::getNetworkStatistics>(true));
    } else if (op == "reset_network") {
      send_request(td_api::make_object<td_api::resetNetworkStatistics>());
    } else if (op == "snt") {
      send_request(td_api::make_object<td_api::setNetworkType>(get_network_type(args)));
    } else if (op == "gadsp") {
      send_request(td_api::make_object<td_api::getAutoDownloadSettingsPresets>());
    } else if (op == "sads") {
      send_request(td_api::make_object<td_api::setAutoDownloadSettings>(
          td_api::make_object<td_api::autoDownloadSettings>(), get_network_type(args)));
    } else if (op == "ansc") {
      string sent_bytes;
      string received_bytes;
      string duration;
      string network_type;
      std::tie(sent_bytes, args) = split(args);
      std::tie(received_bytes, args) = split(args);
      std::tie(duration, network_type) = split(args);
      send_request(
          td_api::make_object<td_api::addNetworkStatistics>(td_api::make_object<td_api::networkStatisticsEntryCall>(
              get_network_type(network_type), to_integer<int32>(sent_bytes), to_integer<int32>(received_bytes),
              to_double(duration))));
    } else if (op == "ans") {
      string sent_bytes;
      string received_bytes;
      string network_type;
      std::tie(sent_bytes, args) = split(args);
      std::tie(received_bytes, network_type) = split(args);
      send_request(
          td_api::make_object<td_api::addNetworkStatistics>(td_api::make_object<td_api::networkStatisticsEntryFile>(
              td_api::make_object<td_api::fileTypeDocument>(), get_network_type(network_type),
              to_integer<int32>(sent_bytes), to_integer<int32>(received_bytes))));
    } else if (op == "top_chats") {
      send_request(td_api::make_object<td_api::getTopChats>(get_top_chat_category(args), 50));
    } else if (op == "rtc") {
      string chat_id;
      string category;
      std::tie(chat_id, category) = split(args);

      send_request(td_api::make_object<td_api::removeTopChat>(get_top_chat_category(category), as_chat_id(chat_id)));
    } else if (op == "sss") {
      send_request(td_api::make_object<td_api::searchStickerSet>(args));
    } else if (op == "siss") {
      send_request(td_api::make_object<td_api::searchInstalledStickerSets>(false, args, 2));
    } else if (op == "ssss") {
      send_request(td_api::make_object<td_api::searchStickerSets>(args));
    } else if (op == "css") {
      string set_id;
      string is_installed;
      string is_archived;

      std::tie(set_id, args) = split(args);
      std::tie(is_installed, is_archived) = split(args);

      send_request(td_api::make_object<td_api::changeStickerSet>(to_integer<int64>(set_id), as_bool(is_installed),
                                                                 as_bool(is_archived)));
    } else if (op == "vtss") {
      send_request(td_api::make_object<td_api::viewTrendingStickerSets>(to_integers<int64>(args)));
    } else if (op == "riss") {
      string is_masks;
      string new_order;

      std::tie(is_masks, new_order) = split(args);

      send_request(
          td_api::make_object<td_api::reorderInstalledStickerSets>(as_bool(is_masks), to_integers<int64>(new_order)));
    } else if (op == "grs") {
      send_request(td_api::make_object<td_api::getRecentStickers>(as_bool(args)));
    } else if (op == "ars") {
      string is_attached;
      string sticker_id;

      std::tie(is_attached, sticker_id) = split(args);

      send_request(td_api::make_object<td_api::addRecentSticker>(as_bool(is_attached), as_input_file_id(sticker_id)));
    } else if (op == "rrs") {
      string is_attached;
      string sticker_id;

      std::tie(is_attached, sticker_id) = split(args);

      send_request(
          td_api::make_object<td_api::removeRecentSticker>(as_bool(is_attached), as_input_file_id(sticker_id)));
    } else if (op == "gfs") {
      send_request(td_api::make_object<td_api::getFavoriteStickers>());
    } else if (op == "afs") {
      send_request(td_api::make_object<td_api::addFavoriteSticker>(as_input_file_id(args)));
    } else if (op == "rfs") {
      send_request(td_api::make_object<td_api::removeFavoriteSticker>(as_input_file_id(args)));
    } else if (op == "crs") {
      send_request(td_api::make_object<td_api::clearRecentStickers>(as_bool(args)));
    } else if (op == "gse") {
      send_request(td_api::make_object<td_api::getStickerEmojis>(as_input_file_id(args)));
    } else if (op == "se") {
      send_request(td_api::make_object<td_api::searchEmojis>(args, false, vector<string>()));
    } else if (op == "see") {
      send_request(td_api::make_object<td_api::searchEmojis>(args, true, vector<string>()));
    } else if (op == "seru") {
      send_request(td_api::make_object<td_api::searchEmojis>(args, false, vector<string>{"ru_RU"}));
    } else if (op == "gesu") {
      send_request(td_api::make_object<td_api::getEmojiSuggestionsUrl>(args));
    } else {
      op_not_found_count++;
    }

    if (op == "gsan") {
      send_request(td_api::make_object<td_api::getSavedAnimations>());
    } else if (op == "asan") {
      send_request(td_api::make_object<td_api::addSavedAnimation>(as_input_file_id(args)));
    } else if (op == "rsan") {
      send_request(td_api::make_object<td_api::removeSavedAnimation>(as_input_file_id(args)));
    } else if (op == "guf") {
      send_request(td_api::make_object<td_api::getUserFullInfo>(as_user_id(args)));
    } else if (op == "gbg") {
      send_request(td_api::make_object<td_api::getBasicGroup>(as_basic_group_id(args)));
    } else if (op == "gbgf") {
      send_request(td_api::make_object<td_api::getBasicGroupFullInfo>(as_basic_group_id(args)));
    } else if (op == "gsg" || op == "gch") {
      send_request(td_api::make_object<td_api::getSupergroup>(as_supergroup_id(args)));
    } else if (op == "gsgf" || op == "gchf") {
      send_request(td_api::make_object<td_api::getSupergroupFullInfo>(as_supergroup_id(args)));
    } else if (op == "gsc") {
      send_request(td_api::make_object<td_api::getSecretChat>(as_secret_chat_id(args)));
    } else if (op == "scm") {
      string chat_id;
      string limit;
      string query;
      string filter;

      std::tie(chat_id, args) = split(args);
      std::tie(limit, args) = split(args);
      std::tie(query, filter) = split(args);
      send_request(td_api::make_object<td_api::searchChatMembers>(as_chat_id(chat_id), query, to_integer<int32>(limit),
                                                                  get_chat_members_filter(filter)));
    } else if (op == "gcm") {
      string chat_id;
      string user_id;

      std::tie(chat_id, user_id) = split(args);
      send_request(td_api::make_object<td_api::getChatMember>(as_chat_id(chat_id), as_user_id(user_id)));
    } else if (op == "GetChatAdministrators") {
      string chat_id = args;
      send_request(td_api::make_object<td_api::getChatAdministrators>(as_chat_id(chat_id)));
    } else if (op == "GetSupergroupAdministrators" || op == "GetSupergroupBanned" || op == "GetSupergroupBots" ||
               op == "GetSupergroupContacts" || op == "GetSupergroupMembers" || op == "GetSupergroupRestricted" ||
               op == "SearchSupergroupMembers") {
      string supergroup_id;
      string query;
      string offset;
      string limit;

      std::tie(supergroup_id, args) = split(args);
      if (op == "GetSupergroupBanned" || op == "GetSupergroupContacts" || op == "GetSupergroupRestricted" ||
          op == "SearchSupergroupMembers") {
        std::tie(query, args) = split(args);
      }
      std::tie(offset, limit) = split(args);
      if (offset.empty()) {
        offset = "0";
      }
      if (limit.empty()) {
        limit = "10";
      }
      td_api::object_ptr<td_api::SupergroupMembersFilter> filter;
      if (op == "GetSupergroupAdministrators") {
        filter = td_api::make_object<td_api::supergroupMembersFilterAdministrators>();
      } else if (op == "GetSupergroupBanned") {
        filter = td_api::make_object<td_api::supergroupMembersFilterBanned>(query);
      } else if (op == "GetSupergroupBots") {
        filter = td_api::make_object<td_api::supergroupMembersFilterBots>();
      } else if (op == "GetSupergroupContacts") {
        filter = td_api::make_object<td_api::supergroupMembersFilterContacts>(query);
      } else if (op == "GetSupergroupMembers") {
        filter = td_api::make_object<td_api::supergroupMembersFilterRecent>();
      } else if (op == "GetSupergroupRestricted") {
        filter = td_api::make_object<td_api::supergroupMembersFilterRestricted>(query);
      } else if (op == "SearchSupergroupMembers") {
        filter = td_api::make_object<td_api::supergroupMembersFilterSearch>(query);
      }
      send_request(td_api::make_object<td_api::getSupergroupMembers>(
          as_supergroup_id(supergroup_id), std::move(filter), to_integer<int32>(offset), to_integer<int32>(limit)));
    } else if (op == "gdialog" || op == "gd") {
      send_request(td_api::make_object<td_api::getChat>(as_chat_id(args)));
    } else if (op == "open") {
      send_request(td_api::make_object<td_api::openChat>(as_chat_id(args)));
    } else if (op == "close") {
      send_request(td_api::make_object<td_api::closeChat>(as_chat_id(args)));
    } else if (op == "gm") {
      string chat_id;
      string message_id;
      std::tie(chat_id, message_id) = split(args);
      send_request(td_api::make_object<td_api::getMessage>(as_chat_id(chat_id), as_message_id(message_id)));
    } else if (op == "gmf") {
      string chat_id;
      string from_message_id_str;
      string to_message_id_str;
      std::tie(chat_id, args) = split(args);
      std::tie(from_message_id_str, to_message_id_str) = split(args);
      auto to_message_id = to_integer<int64>(to_message_id_str);
      for (auto message_id = to_integer<int64>(from_message_id_str); message_id <= to_message_id; message_id++) {
        send_request(td_api::make_object<td_api::getMessage>(as_chat_id(chat_id), message_id << 20));
      }
    } else if (op == "gml") {
      string chat_id;
      string message_id;
      std::tie(chat_id, message_id) = split(args);
      send_request(td_api::make_object<td_api::getMessageLocally>(as_chat_id(chat_id), as_message_id(message_id)));
    } else if (op == "grm") {
      string chat_id;
      string message_id;
      std::tie(chat_id, message_id) = split(args);
      send_request(td_api::make_object<td_api::getRepliedMessage>(as_chat_id(chat_id), as_message_id(message_id)));
    } else if (op == "gcpm") {
      string chat_id = args;
      send_request(td_api::make_object<td_api::getChatPinnedMessage>(as_chat_id(chat_id)));
    } else if (op == "gms") {
      string chat_id;
      string message_ids;
      std::tie(chat_id, message_ids) = split(args);
      send_request(td_api::make_object<td_api::getMessages>(as_chat_id(chat_id), as_message_ids(message_ids)));
    } else if (op == "gpml") {
      string chat_id;
      string message_id;
      string for_album;
      std::tie(chat_id, args) = split(args);
      std::tie(message_id, for_album) = split(args);
      send_request(td_api::make_object<td_api::getPublicMessageLink>(as_chat_id(chat_id), as_message_id(message_id),
                                                                     as_bool(for_album)));
    } else if (op == "gmlink") {
      string chat_id;
      string message_id;
      std::tie(chat_id, message_id) = split(args);
      send_request(td_api::make_object<td_api::getMessageLink>(as_chat_id(chat_id), as_message_id(message_id)));
    } else if (op == "gmli") {
      send_request(td_api::make_object<td_api::getMessageLinkInfo>(args));
    } else if (op == "gcmbd") {
      string chat_id;
      string date;
      std::tie(chat_id, date) = split(args);
      send_request(td_api::make_object<td_api::getChatMessageByDate>(as_chat_id(chat_id), to_integer<int32>(date)));
    } else if (op == "gf" || op == "GetFile") {
      send_request(td_api::make_object<td_api::getFile>(as_file_id(args)));
    } else if (op == "gfdps") {
      string file_id;
      string offset;
      std::tie(file_id, offset) = split(args);
      send_request(
          td_api::make_object<td_api::getFileDownloadedPrefixSize>(as_file_id(file_id), to_integer<int32>(offset)));
    } else if (op == "rfp") {
      string file_id;
      string offset;
      string count;
      std::tie(file_id, args) = split(args);
      std::tie(offset, count) = split(args);

      send_request(td_api::make_object<td_api::readFilePart>(as_file_id(file_id), to_integer<int32>(offset),
                                                             to_integer<int32>(count)));
    } else if (op == "grf") {
      send_request(td_api::make_object<td_api::getRemoteFile>(args, nullptr));
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

      send_request(td_api::make_object<td_api::getMapThumbnailFile>(
          as_location(latitude, longitude), to_integer<int32>(zoom), to_integer<int32>(width),
          to_integer<int32>(height), to_integer<int32>(scale), as_chat_id(chat_id)));
    } else if (op == "df" || op == "DownloadFile" || op == "dff" || op == "dfs") {
      string file_id;
      string priority;
      string offset;
      string limit;
      std::tie(file_id, args) = split(args);
      std::tie(offset, args) = split(args);
      std::tie(limit, priority) = split(args);
      if (priority.empty()) {
        priority = "1";
      }

      int32 max_file_id = as_file_id(file_id);
      int32 min_file_id = (op == "dff" ? 1 : max_file_id);
      for (int32 i = min_file_id; i <= max_file_id; i++) {
        send_request(td_api::make_object<td_api::downloadFile>(
            i, to_integer<int32>(priority), to_integer<int32>(offset), to_integer<int32>(limit), op == "dfs"));
      }
    } else if (op == "cdf") {
      send_request(td_api::make_object<td_api::cancelDownloadFile>(as_file_id(args), false));
    } else if (op == "uf" || op == "ufs" || op == "ufse") {
      string file_path;
      string priority;
      std::tie(file_path, priority) = split(args);
      if (priority.empty()) {
        priority = "1";
      }

      td_api::object_ptr<td_api::FileType> type = td_api::make_object<td_api::fileTypePhoto>();
      if (op == "ufs") {
        type = td_api::make_object<td_api::fileTypeSecret>();
      }
      if (op == "ufse") {
        type = td_api::make_object<td_api::fileTypeSecure>();
      }

      send_request(td_api::make_object<td_api::uploadFile>(as_input_file(file_path), std::move(type),
                                                           to_integer<int32>(priority)));
    } else if (op == "ufg") {
      string file_path;
      string conversion;
      std::tie(file_path, conversion) = split(args);
      send_request(td_api::make_object<td_api::uploadFile>(as_generated_file(file_path, conversion),
                                                           td_api::make_object<td_api::fileTypePhoto>(), 1));
    } else if (op == "cuf") {
      send_request(td_api::make_object<td_api::cancelUploadFile>(as_file_id(args)));
    } else if (op == "delf" || op == "DeleteFile") {
      string file_id = args;
      send_request(td_api::make_object<td_api::deleteFile>(as_file_id(file_id)));
    } else if (op == "dm") {
      string chat_id;
      string message_ids;
      string revoke;
      std::tie(chat_id, args) = split(args);
      std::tie(message_ids, revoke) = split(args);

      send_request(td_api::make_object<td_api::deleteMessages>(as_chat_id(chat_id), as_message_ids(message_ids),
                                                               as_bool(revoke)));
    } else if (op == "fm" || op == "fmg" || op == "cm" || op == "cmg") {
      string chat_id;
      string from_chat_id;
      string message_ids;
      std::tie(chat_id, args) = split(args);
      std::tie(from_chat_id, message_ids) = split(args);

      auto chat = as_chat_id(chat_id);
      send_request(td_api::make_object<td_api::forwardMessages>(
          chat, as_chat_id(from_chat_id), as_message_ids(message_ids), default_send_message_options(), op[2] == 'g',
          op[0] == 'c', Random::fast(0, 1) == 1));
    } else if (op == "resend") {
      string chat_id;
      string message_ids;
      std::tie(chat_id, message_ids) = split(args);

      send_request(td_api::make_object<td_api::resendMessages>(as_chat_id(chat_id), as_message_ids(message_ids)));
    } else if (op == "csc" || op == "CreateSecretChat") {
      send_request(td_api::make_object<td_api::createSecretChat>(as_secret_chat_id(args)));
    } else if (op == "cnsc" || op == "CreateNewSecretChat") {
      send_request(td_api::make_object<td_api::createNewSecretChat>(as_user_id(args)));
    } else if (op == "scstn") {
      send_request(td_api::make_object<td_api::sendChatScreenshotTakenNotification>(as_chat_id(args)));
    } else if (op == "sscttl" || op == "setSecretChatTtl") {
      string chat_id;
      string ttl;
      std::tie(chat_id, ttl) = split(args);

      send_request(td_api::make_object<td_api::sendChatSetTtlMessage>(as_chat_id(chat_id), to_integer<int32>(ttl)));
    } else if (op == "closeSC" || op == "cancelSC") {
      send_request(td_api::make_object<td_api::closeSecretChat>(as_secret_chat_id(args)));
    } else if (op == "cc" || op == "CreateCall") {
      send_request(td_api::make_object<td_api::createCall>(
          as_user_id(args), td_api::make_object<td_api::callProtocol>(true, true, 65, 65, vector<string>{"2.6"})));
    } else if (op == "dc" || op == "DiscardCall") {
      string call_id;
      string is_disconnected;
      std::tie(call_id, is_disconnected) = split(args);

      send_request(td_api::make_object<td_api::discardCall>(as_call_id(call_id), as_bool(is_disconnected), 0, 0));
    } else if (op == "ac" || op == "AcceptCall") {
      send_request(td_api::make_object<td_api::acceptCall>(
          as_call_id(args), td_api::make_object<td_api::callProtocol>(true, true, 65, 65, vector<string>{"2.6"})));
    } else if (op == "scr" || op == "SendCallRating") {
      string call_id;
      string rating;
      std::tie(call_id, rating) = split(args);

      vector<td_api::object_ptr<td_api::CallProblem>> problems;
      problems.emplace_back(td_api::make_object<td_api::callProblemNoise>());
      problems.emplace_back(td_api::make_object<td_api::callProblemNoise>());
      problems.emplace_back(nullptr);
      problems.emplace_back(td_api::make_object<td_api::callProblemNoise>());
      problems.emplace_back(td_api::make_object<td_api::callProblemEcho>());
      problems.emplace_back(td_api::make_object<td_api::callProblemDistortedSpeech>());
      send_request(td_api::make_object<td_api::sendCallRating>(
          as_call_id(call_id), to_integer<int32>(rating), "Wow, such good call! (TDLib test)", std::move(problems)));
    } else if (op == "scdi" || op == "SendCallDebugInformation") {
      send_request(td_api::make_object<td_api::sendCallDebugInformation>(as_call_id(args), "{}"));
    } else if (op == "gcil") {
      send_request(td_api::make_object<td_api::generateChatInviteLink>(as_chat_id(args)));
    } else if (op == "ccil") {
      send_request(td_api::make_object<td_api::checkChatInviteLink>(args));
    } else if (op == "jcbil") {
      send_request(td_api::make_object<td_api::joinChatByInviteLink>(args));
    } else if (op == "gte") {
      send_request(td_api::make_object<td_api::getTextEntities>(args));
    } else if (op == "gtes") {
      execute(td_api::make_object<td_api::getTextEntities>(args));
    } else if (op == "pm") {
      send_request(
          td_api::make_object<td_api::parseMarkdown>(td_api::make_object<td_api::formattedText>(args, Auto())));
    } else if (op == "pte") {
      send_request(
          td_api::make_object<td_api::parseTextEntities>(args, td_api::make_object<td_api::textParseModeMarkdown>(2)));
    } else if (op == "pteh") {
      send_request(
          td_api::make_object<td_api::parseTextEntities>(args, td_api::make_object<td_api::textParseModeHTML>()));
    } else if (op == "ptes") {
      execute(
          td_api::make_object<td_api::parseTextEntities>(args, td_api::make_object<td_api::textParseModeMarkdown>(2)));
    } else if (op == "ptehs") {
      execute(td_api::make_object<td_api::parseTextEntities>(args, td_api::make_object<td_api::textParseModeHTML>()));
    } else if (op == "gfmt") {
      execute(td_api::make_object<td_api::getFileMimeType>(trim(args)));
    } else if (op == "gfe") {
      execute(td_api::make_object<td_api::getFileExtension>(trim(args)));
    } else if (op == "cfn") {
      execute(td_api::make_object<td_api::cleanFileName>(args));
    } else if (op == "gjv") {
      execute(td_api::make_object<td_api::getJsonValue>(args));
    } else if (op == "gjvtest") {
      execute(td_api::make_object<td_api::getJsonValue>("\"aba\200caba\""));
      execute(td_api::make_object<td_api::getJsonValue>("\"\\u0080\""));
      execute(td_api::make_object<td_api::getJsonValue>("\"\\uD800\""));
    } else if (op == "gjs") {
      auto test_get_json_string = [&](auto &&json_value) {
        execute(td_api::make_object<td_api::getJsonString>(std::move(json_value)));
      };

      test_get_json_string(nullptr);
      test_get_json_string(td_api::make_object<td_api::jsonValueNull>());
      test_get_json_string(td_api::make_object<td_api::jsonValueBoolean>(true));
      test_get_json_string(td_api::make_object<td_api::jsonValueNumber>(123456789123.0));
      test_get_json_string(td_api::make_object<td_api::jsonValueString>(string("aba\0caba", 8)));
      test_get_json_string(td_api::make_object<td_api::jsonValueString>("aba\200caba"));

      auto inner_array = td_api::make_object<td_api::jsonValueArray>();
      inner_array->values_.push_back(td_api::make_object<td_api::jsonValueBoolean>(false));
      auto array = td_api::make_object<td_api::jsonValueArray>();
      array->values_.push_back(nullptr);
      array->values_.push_back(std::move(inner_array));
      array->values_.push_back(td_api::make_object<td_api::jsonValueNull>());
      array->values_.push_back(td_api::make_object<td_api::jsonValueNumber>(-1));
      test_get_json_string(std::move(array));

      auto object = td_api::make_object<td_api::jsonValueObject>();
      object->members_.push_back(
          td_api::make_object<td_api::jsonObjectMember>("", td_api::make_object<td_api::jsonValueString>("test")));
      object->members_.push_back(td_api::make_object<td_api::jsonObjectMember>("a", nullptr));
      object->members_.push_back(td_api::make_object<td_api::jsonObjectMember>("\x80", nullptr));
      object->members_.push_back(nullptr);
      object->members_.push_back(
          td_api::make_object<td_api::jsonObjectMember>("a", td_api::make_object<td_api::jsonValueNull>()));
      test_get_json_string(std::move(object));
    } else if (op == "gac") {
      send_request(td_api::make_object<td_api::getApplicationConfig>());
    } else if (op == "sale") {
      string type;
      string chat_id;
      string json;
      std::tie(type, args) = split(args);
      std::tie(chat_id, json) = split(args);

      auto result = execute(td_api::make_object<td_api::getJsonValue>(json));
      if (result->get_id() == td_api::error::ID) {
        LOG(ERROR) << to_string(result);
      } else {
        send_request(td_api::make_object<td_api::saveApplicationLogEvent>(
            type, as_chat_id(chat_id), move_tl_object_as<td_api::JsonValue>(result)));
      }
    } else {
      op_not_found_count++;
    }

    if (op == "scdm") {
      string chat_id;
      string reply_to_message_id;
      string message;
      std::tie(chat_id, args) = split(args);
      std::tie(reply_to_message_id, message) = split(args);
      td_api::object_ptr<td_api::draftMessage> draft_message;
      if (!reply_to_message_id.empty() || !message.empty()) {
        vector<td_api::object_ptr<td_api::textEntity>> entities;
        entities.push_back(
            td_api::make_object<td_api::textEntity>(0, 1, td_api::make_object<td_api::textEntityTypePre>()));

        draft_message = td_api::make_object<td_api::draftMessage>(
            as_message_id(reply_to_message_id), 0,
            td_api::make_object<td_api::inputMessageText>(as_formatted_text(message, std::move(entities)), true,
                                                          false));
      }
      send_request(td_api::make_object<td_api::setChatDraftMessage>(as_chat_id(chat_id), std::move(draft_message)));
    } else if (op == "cadm") {
      send_request(td_api::make_object<td_api::clearAllDraftMessages>());
    } else if (op == "tcip" || op == "tcipa" || begins_with(op, "tcip-")) {
      string chat_id;
      string is_pinned;
      std::tie(chat_id, is_pinned) = split(args);
      send_request(
          td_api::make_object<td_api::toggleChatIsPinned>(as_chat_list(op), as_chat_id(chat_id), as_bool(is_pinned)));
    } else if (op == "tcimar") {
      string chat_id;
      string is_marked_as_read;
      std::tie(chat_id, is_marked_as_read) = split(args);
      send_request(
          td_api::make_object<td_api::toggleChatIsMarkedAsUnread>(as_chat_id(chat_id), as_bool(is_marked_as_read)));
    } else if (op == "tcddn") {
      string chat_id;
      string default_disable_notification;
      std::tie(chat_id, default_disable_notification) = split(args);
      send_request(td_api::make_object<td_api::toggleChatDefaultDisableNotification>(
          as_chat_id(chat_id), as_bool(default_disable_notification)));
    } else if (op == "spchats" || op == "spchatsa" || begins_with(op, "spchats-")) {
      vector<string> chat_ids_str = full_split(args, ' ');
      vector<int64> chat_ids;
      for (auto &chat_id_str : chat_ids_str) {
        chat_ids.push_back(as_chat_id(chat_id_str));
      }
      send_request(td_api::make_object<td_api::setPinnedChats>(as_chat_list(op), std::move(chat_ids)));
    } else if (op == "sca") {
      string chat_id;
      string action;
      std::tie(chat_id, action) = split(args);
      send_request(td_api::make_object<td_api::sendChatAction>(as_chat_id(chat_id), get_chat_action(action)));
    } else if (op == "smt" || op == "smtp" || op == "smtf" || op == "smtpf") {
      const string &chat_id = args;
      for (int i = 1; i <= 200; i++) {
        string message = PSTRING() << "#" << i;
        if (i == 6 || (op.back() == 'f' && i % 2 == 0)) {
          message = string(4097, 'a');
        }
        if (op[3] == 'p') {
          send_message(chat_id, td_api::make_object<td_api::inputMessagePhoto>(as_local_file("rgb.jpg"), nullptr,
                                                                               Auto(), 0, 0, as_caption(message), 0));
        } else {
          send_message(chat_id, td_api::make_object<td_api::inputMessageText>(as_formatted_text(message), false, true));
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

      send_request(td_api::make_object<td_api::searchSecretMessages>(
          as_chat_id(chat_id), query, to_integer<int64>(from_search_id), to_integer<int32>(limit),
          get_search_messages_filter(filter)));
    } else if (op == "ssd") {
      schedule_date_ = args;
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

      send_message(chat_id, td_api::make_object<td_api::inputMessageText>(as_formatted_text(message), false, true),
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

      send_request(td_api::make_object<td_api::addLocalMessage>(
          as_chat_id(chat_id), as_user_id(user_id), as_message_id(reply_to_message_id), false,
          td_api::make_object<td_api::inputMessageText>(as_formatted_text(message), false, true)));
    } else if (op == "smap" || op == "smapr") {
      string chat_id;
      string reply_to_message_id;
      vector<string> photos;

      std::tie(chat_id, args) = split(args);
      if (op == "smapr") {
        std::tie(reply_to_message_id, args) = split(args);
      }
      photos = full_split(args);

      send_request(td_api::make_object<td_api::sendMessageAlbum>(
          as_chat_id(chat_id), as_message_id(reply_to_message_id), default_send_message_options(),
          transform(photos, [](const string &photo_path) {
            td_api::object_ptr<td_api::InputMessageContent> content = td_api::make_object<td_api::inputMessagePhoto>(
                as_input_file(photo_path), nullptr, Auto(), 0, 0, as_caption(""), 0);
            return content;
          })));
    } else if (op == "em") {
      string chat_id;
      string message_id;
      string message;
      std::tie(chat_id, args) = split(args);
      std::tie(message_id, message) = split(args);
      send_request(td_api::make_object<td_api::editMessageText>(
          as_chat_id(chat_id), as_message_id(message_id), nullptr,
          td_api::make_object<td_api::inputMessageText>(as_formatted_text(message), true, true)));
    } else if (op == "eman") {
      string chat_id;
      string message_id;
      string animation;
      std::tie(chat_id, args) = split(args);
      std::tie(message_id, animation) = split(args);
      send_request(td_api::make_object<td_api::editMessageMedia>(
          as_chat_id(chat_id), as_message_id(message_id), nullptr,
          td_api::make_object<td_api::inputMessageAnimation>(as_input_file(animation), nullptr, vector<int32>(), 0, 0,
                                                             0, as_caption("animation"))));
    } else if (op == "emc") {
      string chat_id;
      string message_id;
      string caption;
      std::tie(chat_id, args) = split(args);
      std::tie(message_id, caption) = split(args);
      send_request(td_api::make_object<td_api::editMessageCaption>(as_chat_id(chat_id), as_message_id(message_id),
                                                                   nullptr, as_caption(caption)));
    } else if (op == "emd") {
      string chat_id;
      string message_id;
      string document;
      std::tie(chat_id, args) = split(args);
      std::tie(message_id, document) = split(args);
      send_request(td_api::make_object<td_api::editMessageMedia>(
          as_chat_id(chat_id), as_message_id(message_id), nullptr,
          td_api::make_object<td_api::inputMessageDocument>(as_input_file(document), nullptr, false, as_caption(""))));
    } else if (op == "emp") {
      string chat_id;
      string message_id;
      string photo;
      std::tie(chat_id, args) = split(args);
      std::tie(message_id, photo) = split(args);
      send_request(td_api::make_object<td_api::editMessageMedia>(
          as_chat_id(chat_id), as_message_id(message_id), nullptr,
          td_api::make_object<td_api::inputMessagePhoto>(as_input_file(photo), as_input_thumbnail(photo), Auto(), 0, 0,
                                                         as_caption(""), 0)));
    } else if (op == "empttl") {
      string chat_id;
      string message_id;
      string photo;
      std::tie(chat_id, args) = split(args);
      std::tie(message_id, photo) = split(args);
      send_request(td_api::make_object<td_api::editMessageMedia>(
          as_chat_id(chat_id), as_message_id(message_id), nullptr,
          td_api::make_object<td_api::inputMessagePhoto>(as_input_file(photo), as_input_thumbnail(photo), Auto(), 0, 0,
                                                         as_caption(""), 10)));
    } else if (op == "emvt") {
      string chat_id;
      string message_id;
      string video;
      string thumbnail;
      std::tie(chat_id, args) = split(args);
      std::tie(message_id, args) = split(args);
      std::tie(video, thumbnail) = split(args);
      send_request(td_api::make_object<td_api::editMessageMedia>(
          as_chat_id(chat_id), as_message_id(message_id), nullptr,
          td_api::make_object<td_api::inputMessageVideo>(as_input_file(video), as_input_thumbnail(thumbnail), Auto(), 1,
                                                         2, 3, true, as_caption(""), 0)));
    } else if (op == "emll") {
      string chat_id;
      string message_id;
      string latitude;
      string longitude;
      std::tie(chat_id, args) = split(args);
      std::tie(message_id, args) = split(args);
      std::tie(latitude, longitude) = split(args);
      send_request(td_api::make_object<td_api::editMessageLiveLocation>(as_chat_id(chat_id), as_message_id(message_id),
                                                                        nullptr, as_location(latitude, longitude)));
    } else if (op == "emss") {
      string chat_id;
      string message_id;
      string date;
      std::tie(chat_id, args) = split(args);
      std::tie(message_id, date) = split(args);
      send_request(td_api::make_object<td_api::editMessageSchedulingState>(
          as_chat_id(chat_id), as_message_id(message_id), as_message_scheduling_state(date)));
    } else if (op == "gallm") {
      send_request(td_api::make_object<td_api::getActiveLiveLocationMessages>());
    } else if (op == "sbsm") {
      string bot_id;
      string chat_id;
      string parameter;
      std::tie(bot_id, args) = split(args);
      std::tie(chat_id, parameter) = split(args);
      send_request(
          td_api::make_object<td_api::sendBotStartMessage>(as_user_id(bot_id), as_chat_id(chat_id), parameter));
    } else if (op == "giqr") {
      string bot_id;
      string query;
      std::tie(bot_id, query) = split(args);
      send_request(td_api::make_object<td_api::getInlineQueryResults>(as_user_id(bot_id), 0, nullptr, query, ""));
    } else if (op == "giqro") {
      string bot_id;
      string offset;
      string query;
      std::tie(bot_id, args) = split(args);
      std::tie(offset, query) = split(args);
      send_request(td_api::make_object<td_api::getInlineQueryResults>(as_user_id(bot_id), 0, nullptr, query, offset));
    } else if (op == "giqrl") {
      string bot_id;
      string query;
      std::tie(bot_id, query) = split(args);
      send_request(td_api::make_object<td_api::getInlineQueryResults>(as_user_id(bot_id), 0, as_location("1.1", "2.2"),
                                                                      query, ""));
    } else if (op == "siqr" || op == "siqrh") {
      string chat_id;
      string query_id;
      string result_id;
      std::tie(chat_id, args) = split(args);
      std::tie(query_id, result_id) = split(args);

      auto chat = as_chat_id(chat_id);
      send_request(td_api::make_object<td_api::sendInlineQueryResultMessage>(
          chat, 0, default_send_message_options(), to_integer<int64>(query_id), result_id, op == "siqrh"));
    } else if (op == "gcqr") {
      string chat_id;
      string message_id;
      string data;
      std::tie(chat_id, args) = split(args);
      std::tie(message_id, data) = split(args);
      send_request(td_api::make_object<td_api::getCallbackQueryAnswer>(
          as_chat_id(chat_id), as_message_id(message_id), td_api::make_object<td_api::callbackQueryPayloadData>(data)));
    } else if (op == "gcgqr") {
      string chat_id;
      string message_id;
      std::tie(chat_id, message_id) = split(args);
      send_request(td_api::make_object<td_api::getCallbackQueryAnswer>(
          as_chat_id(chat_id), as_message_id(message_id), td_api::make_object<td_api::callbackQueryPayloadGame>("")));
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

      send_message(chat_id, td_api::make_object<td_api::inputMessageAnimation>(
                                as_input_file(animation_path), nullptr, vector<int32>(), 60, to_integer<int32>(width),
                                to_integer<int32>(height), as_caption(caption)));
    } else if (op == "sang") {
      string chat_id;
      string animation_path;
      string animation_conversion;
      std::tie(chat_id, args) = split(args);
      std::tie(animation_path, animation_conversion) = split(args);
      send_message(chat_id, td_api::make_object<td_api::inputMessageAnimation>(
                                as_generated_file(animation_path, animation_conversion), nullptr, vector<int32>(), 60,
                                0, 0, as_caption("")));
    } else if (op == "sanid") {
      string chat_id;
      string file_id;
      std::tie(chat_id, file_id) = split(args);

      send_message(chat_id, td_api::make_object<td_api::inputMessageAnimation>(
                                as_input_file_id(file_id), nullptr, vector<int32>(), 0, 0, 0, as_caption("")));
    } else if (op == "sanurl") {
      string chat_id;
      string url;
      std::tie(chat_id, url) = split(args);

      send_message(chat_id, td_api::make_object<td_api::inputMessageAnimation>(
                                as_generated_file(url, "#url#"), nullptr, vector<int32>(), 0, 0, 0, as_caption("")));
    } else if (op == "sanurl2") {
      string chat_id;
      string url;
      std::tie(chat_id, url) = split(args);

      send_message(chat_id, td_api::make_object<td_api::inputMessageAnimation>(
                                as_remote_file(url), nullptr, vector<int32>(), 0, 0, 0, as_caption("")));
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

      send_message(chat_id, td_api::make_object<td_api::inputMessageAudio>(as_input_file(audio_path), nullptr,
                                                                           to_integer<int32>(duration), title,
                                                                           performer, as_caption("audio caption")));
    } else if (op == "svoice") {
      string chat_id;
      string voice_path;
      std::tie(chat_id, voice_path) = split(args);

      send_message(chat_id, td_api::make_object<td_api::inputMessageVoiceNote>(as_input_file(voice_path), 0, "abacaba",
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

      send_message(chat_id, td_api::make_object<td_api::inputMessageContact>(td_api::make_object<td_api::contact>(
                                phone_number, first_name, last_name, string(), as_user_id(user_id))));
    } else if (op == "sf" || op == "scopy") {
      string chat_id;
      string from_chat_id;
      string from_message_id;
      std::tie(chat_id, args) = split(args);
      std::tie(from_chat_id, from_message_id) = split(args);

      send_message(chat_id, td_api::make_object<td_api::inputMessageForwarded>(as_chat_id(from_chat_id),
                                                                               as_message_id(from_message_id), true,
                                                                               op == "scopy", Random::fast(0, 1) == 0));
    } else if (op == "sdice" || op == "sdicecd") {
      string chat_id;
      string emoji;
      std::tie(chat_id, emoji) = split(args);

      send_message(chat_id, td_api::make_object<td_api::inputMessageDice>(emoji, op == "sdicecd"));
    } else if (op == "sd" || op == "sdf") {
      string chat_id;
      string document_path;
      std::tie(chat_id, document_path) = split(args);
      send_message(chat_id, td_api::make_object<td_api::inputMessageDocument>(
                                as_input_file(document_path), nullptr, op == "sdf",
                                as_caption(u8"\u1680\u180Etest \u180E\n\u180E\n\u180E\n cap\ttion\u180E\u180E")));
    } else if (op == "sdt" || op == "sdtf") {
      string chat_id;
      string document_path;
      string thumbnail_path;
      std::tie(chat_id, args) = split(args);
      std::tie(document_path, thumbnail_path) = split(args);
      send_message(chat_id, td_api::make_object<td_api::inputMessageDocument>(
                                as_input_file(document_path), as_input_thumbnail(thumbnail_path), op == "sdtf",
                                as_caption("test caption")));
    } else if (op == "sdg" || op == "sdgu") {
      string chat_id;
      string document_path;
      string document_conversion;
      std::tie(chat_id, args) = split(args);
      std::tie(document_path, document_conversion) = split(args);
      if (op == "sdgu") {
        send_request(
            td_api::make_object<td_api::uploadFile>(as_generated_file(document_path, document_conversion), nullptr, 1));
      }
      send_message(chat_id, td_api::make_object<td_api::inputMessageDocument>(
                                as_generated_file(document_path, document_conversion), nullptr, false,
                                as_caption("test caption")));
    } else if (op == "sdtg") {
      string chat_id;
      string document_path;
      string thumbnail_path;
      string thumbnail_conversion;
      std::tie(chat_id, args) = split(args);
      std::tie(document_path, args) = split(args);
      std::tie(thumbnail_path, thumbnail_conversion) = split(args);
      send_message(chat_id, td_api::make_object<td_api::inputMessageDocument>(
                                as_input_file(document_path), as_input_thumbnail(thumbnail_path, thumbnail_conversion),
                                false, as_caption("test caption")));
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
      send_message(chat_id,
                   td_api::make_object<td_api::inputMessageDocument>(
                       as_generated_file(document_path, document_conversion),
                       as_input_thumbnail(thumbnail_path, thumbnail_conversion), false, as_caption("test caption")));
    } else if (op == "sdid") {
      string chat_id;
      string file_id;
      std::tie(chat_id, file_id) = split(args);
      send_message(chat_id, td_api::make_object<td_api::inputMessageDocument>(as_input_file_id(file_id), nullptr, false,
                                                                              as_caption("")));
    } else if (op == "sdurl") {
      string chat_id;
      string url;
      std::tie(chat_id, url) = split(args);

      send_message(chat_id, td_api::make_object<td_api::inputMessageDocument>(as_remote_file(url), nullptr, false,
                                                                              as_caption("")));
    } else if (op == "sg") {
      string chat_id;
      string bot_user_id;
      string game_short_name;
      std::tie(chat_id, args) = split(args);
      std::tie(bot_user_id, game_short_name) = split(args);
      send_message(chat_id, td_api::make_object<td_api::inputMessageGame>(as_user_id(bot_user_id), game_short_name));
    } else if (op == "sl") {
      string chat_id;
      std::tie(chat_id, args) = split(args);

      string latitude;
      string longitude;
      std::tie(latitude, longitude) = split(args);

      send_message(chat_id, td_api::make_object<td_api::inputMessageLocation>(as_location(latitude, longitude), 0));
    } else if (op == "sll") {
      string chat_id;
      string period;
      string latitude;
      string longitude;
      std::tie(chat_id, args) = split(args);
      std::tie(period, args) = split(args);
      std::tie(latitude, longitude) = split(args);

      send_message(chat_id, td_api::make_object<td_api::inputMessageLocation>(as_location(latitude, longitude),
                                                                              to_integer<int32>(period)));
    } else if (op == "spoll" || op == "spollm" || op == "spollp" || op == "squiz") {
      string chat_id;
      string question;
      std::tie(chat_id, args) = split(args);
      std::tie(question, args) = split(args);
      auto options = full_split(args);

      td_api::object_ptr<td_api::PollType> poll_type;
      if (op == "squiz") {
        poll_type = td_api::make_object<td_api::pollTypeQuiz>(narrow_cast<int32>(options.size() - 1),
                                                              as_formatted_text("_te*st*_"));
      } else {
        poll_type = td_api::make_object<td_api::pollTypeRegular>(op == "spollm");
      }
      send_message(chat_id, td_api::make_object<td_api::inputMessagePoll>(question, std::move(options), op != "spollp",
                                                                          std::move(poll_type), 0, 0, false));
    } else if (op == "sp" || op == "spcaption" || op == "spttl") {
      string chat_id;
      string photo_path;
      string sticker_file_ids_str;
      vector<int32> sticker_file_ids;
      std::tie(chat_id, args) = split(args);
      std::tie(sticker_file_ids_str, photo_path) = split(args);
      if (trim(photo_path).empty()) {
        photo_path = sticker_file_ids_str;
      } else {
        sticker_file_ids = to_integers<int32>(sticker_file_ids_str);
      }

      send_message(chat_id, td_api::make_object<td_api::inputMessagePhoto>(
                                as_input_file(photo_path), nullptr, std::move(sticker_file_ids), 0, 0,
                                as_caption(op == "spcaption" ? "cap \n\n\n\n tion " : ""), op == "spttl" ? 10 : 0));
    } else if (op == "spg" || op == "spgttl") {
      string chat_id;
      string photo_path;
      string conversion;
      string expected_size;
      std::tie(chat_id, args) = split(args);
      std::tie(photo_path, args) = split(args);
      std::tie(conversion, expected_size) = split(args);

      send_message(chat_id, td_api::make_object<td_api::inputMessagePhoto>(
                                as_generated_file(photo_path, conversion, to_integer<int32>(expected_size)), nullptr,
                                vector<int32>(), 0, 0, as_caption(""), op == "spgttl" ? 10 : 0));
    } else if (op == "spt") {
      string chat_id;
      string photo_path;
      string thumbnail_path;
      std::tie(chat_id, args) = split(args);
      std::tie(photo_path, thumbnail_path) = split(args);

      send_message(chat_id, td_api::make_object<td_api::inputMessagePhoto>(as_input_file(photo_path),
                                                                           as_input_thumbnail(thumbnail_path, 90, 89),
                                                                           vector<int32>(), 0, 0, as_caption(""), 0));
    } else if (op == "sptg") {
      string chat_id;
      string photo_path;
      string thumbnail_path;
      string thumbnail_conversion;
      std::tie(chat_id, args) = split(args);
      std::tie(photo_path, args) = split(args);
      std::tie(thumbnail_path, thumbnail_conversion) = split(args);

      send_message(chat_id,
                   td_api::make_object<td_api::inputMessagePhoto>(
                       as_input_file(photo_path), as_input_thumbnail(thumbnail_path, thumbnail_conversion, 90, 89),
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

      send_message(chat_id, td_api::make_object<td_api::inputMessagePhoto>(
                                as_generated_file(photo_path, conversion),
                                as_input_thumbnail(thumbnail_path, thumbnail_conversion, 90, 89), vector<int32>(), 0, 0,
                                as_caption(""), 0));
    } else if (op == "spid") {
      string chat_id;
      string file_id;
      std::tie(chat_id, file_id) = split(args);
      send_message(chat_id, td_api::make_object<td_api::inputMessagePhoto>(as_input_file_id(file_id), nullptr,
                                                                           vector<int32>(), 0, 0, as_caption(""), 0));
    } else if (op == "ss") {
      string chat_id;
      string sticker_path;
      std::tie(chat_id, sticker_path) = split(args);

      send_message(chat_id,
                   td_api::make_object<td_api::inputMessageSticker>(as_input_file(sticker_path), nullptr, 0, 0));
    } else if (op == "sstt") {
      string chat_id;
      string sticker_path;
      string thumbnail_path;
      std::tie(chat_id, args) = split(args);
      std::tie(sticker_path, thumbnail_path) = split(args);

      send_message(chat_id, td_api::make_object<td_api::inputMessageSticker>(as_input_file(sticker_path),
                                                                             as_input_thumbnail(thumbnail_path), 0, 0));
    } else if (op == "ssid") {
      string chat_id;
      string file_id;
      std::tie(chat_id, file_id) = split(args);

      send_message(chat_id, td_api::make_object<td_api::inputMessageSticker>(as_input_file_id(file_id), nullptr, 0, 0));
    } else if (op == "sv" || op == "svttl") {
      string chat_id;
      string video_path;
      string sticker_file_ids_str;
      vector<int32> sticker_file_ids;
      std::tie(chat_id, args) = split(args);
      std::tie(sticker_file_ids_str, video_path) = split(args);
      if (trim(video_path).empty()) {
        video_path = sticker_file_ids_str;
      } else {
        sticker_file_ids = to_integers<int32>(sticker_file_ids_str);
      }

      send_message(chat_id, td_api::make_object<td_api::inputMessageVideo>(as_input_file(video_path), nullptr,
                                                                           std::move(sticker_file_ids), 1, 2, 3, true,
                                                                           as_caption(""), op == "svttl" ? 10 : 0));
    } else if (op == "svt" || op == "svtttl") {
      string chat_id;
      string video;
      string thumbnail;
      std::tie(chat_id, args) = split(args);
      std::tie(video, thumbnail) = split(args);

      send_message(chat_id, td_api::make_object<td_api::inputMessageVideo>(
                                as_input_file(video), as_input_thumbnail(thumbnail), vector<int32>(), 0, 0, 0, true,
                                as_caption(""), op == "svtttl" ? 10 : 0));
    } else if (op == "svn") {
      string chat_id;
      string video_path;
      std::tie(chat_id, video_path) = split(args);
      send_message(chat_id,
                   td_api::make_object<td_api::inputMessageVideoNote>(as_input_file(video_path), nullptr, 1, 5));
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

      send_message(chat_id, td_api::make_object<td_api::inputMessageVenue>(td_api::make_object<td_api::venue>(
                                as_location(latitude, longitude), title, address, provider, venue_id, venue_type)));
    } else if (op == "test") {
      send_request(td_api::make_object<td_api::testNetwork>());
    } else if (op == "alarm") {
      send_request(td_api::make_object<td_api::setAlarm>(to_double(args)));
    } else if (op == "delete") {
      string chat_id;
      string remove_from_the_chat_list;
      string revoke;
      std::tie(chat_id, args) = split(args);
      std::tie(remove_from_the_chat_list, revoke) = split(args);
      send_request(td_api::make_object<td_api::deleteChatHistory>(as_chat_id(chat_id),
                                                                  as_bool(remove_from_the_chat_list), as_bool(revoke)));
    } else if (op == "dmfu") {
      string chat_id;
      string user_id;
      std::tie(chat_id, user_id) = split(args);
      send_request(td_api::make_object<td_api::deleteChatMessagesFromUser>(as_chat_id(chat_id), as_user_id(user_id)));
    } else if (op == "cnbgc") {
      string user_ids_string;
      string title;
      std::tie(user_ids_string, title) = split(args);

      send_request(td_api::make_object<td_api::createNewBasicGroupChat>(as_user_ids(user_ids_string), title));
    } else if (op == "cnch") {
      send_request(td_api::make_object<td_api::createNewSupergroupChat>(args, true, "Description", nullptr));
    } else if (op == "cnsg") {
      send_request(td_api::make_object<td_api::createNewSupergroupChat>(args, false, "Description", nullptr));
    } else if (op == "cngc") {
      send_request(td_api::make_object<td_api::createNewSupergroupChat>(
          args, false, "Description",
          td_api::make_object<td_api::chatLocation>(td_api::make_object<td_api::location>(40.0, 60.0), "address")));
    } else if (op == "UpgradeBasicGroupChatToSupergroupChat") {
      send_request(td_api::make_object<td_api::upgradeBasicGroupChatToSupergroupChat>(as_chat_id(args)));
    } else if (op == "DeleteSupergroup") {
      send_request(td_api::make_object<td_api::deleteSupergroup>(as_supergroup_id(args)));
    } else if (op == "gcpc") {
      send_request(td_api::make_object<td_api::getCreatedPublicChats>());
    } else if (op == "gcpcl") {
      send_request(td_api::make_object<td_api::getCreatedPublicChats>(
          td_api::make_object<td_api::publicChatTypeIsLocationBased>()));
    } else if (op == "ccpcl") {
      send_request(td_api::make_object<td_api::checkCreatedPublicChatsLimit>());
    } else if (op == "ccpcll") {
      send_request(td_api::make_object<td_api::checkCreatedPublicChatsLimit>(
          td_api::make_object<td_api::publicChatTypeIsLocationBased>()));
    } else if (op == "gsdc") {
      send_request(td_api::make_object<td_api::getSuitableDiscussionChats>());
    } else if (op == "gisc") {
      send_request(td_api::make_object<td_api::getInactiveSupergroupChats>());
    } else if (op == "cpc") {
      string user_id;
      string force;

      std::tie(user_id, force) = split(args);
      send_request(td_api::make_object<td_api::createPrivateChat>(as_user_id(user_id), as_bool(force)));
    } else if (op == "cbgc") {
      string basic_group_id;
      string force;

      std::tie(basic_group_id, force) = split(args);
      send_request(
          td_api::make_object<td_api::createBasicGroupChat>(as_basic_group_id(basic_group_id), as_bool(force)));
    } else if (op == "csgc" || op == "cchc") {
      string supergroup_id;
      string force;

      std::tie(supergroup_id, force) = split(args);
      send_request(td_api::make_object<td_api::createSupergroupChat>(as_supergroup_id(supergroup_id), as_bool(force)));
    } else if (op == "gcltac") {
      string chat_id = args;
      send_request(td_api::make_object<td_api::getChatListsToAddChat>(as_chat_id(chat_id)));
    } else if (op == "actl" || op == "actla" || begins_with(op, "actl-")) {
      string chat_id = args;
      send_request(td_api::make_object<td_api::addChatToList>(as_chat_id(chat_id), as_chat_list(op)));
    } else if (op == "gcf") {
      send_request(td_api::make_object<td_api::getChatFilter>(as_chat_filter_id(args)));
    } else if (op == "ccf") {
      send_request(td_api::make_object<td_api::createChatFilter>(as_chat_filter(args)));
    } else if (op == "ccfe") {
      auto chat_filter = td_api::make_object<td_api::chatFilter>();
      chat_filter->title_ = "empty";
      chat_filter->included_chat_ids_ = as_chat_ids(args);
      send_request(td_api::make_object<td_api::createChatFilter>(std::move(chat_filter)));
    } else if (op == "ecf") {
      string chat_filter_id;
      string filter;

      std::tie(chat_filter_id, filter) = split(args);
      send_request(
          td_api::make_object<td_api::editChatFilter>(as_chat_filter_id(chat_filter_id), as_chat_filter(filter)));
    } else if (op == "dcf") {
      send_request(td_api::make_object<td_api::deleteChatFilter>(as_chat_filter_id(args)));
    } else if (op == "rcf") {
      send_request(td_api::make_object<td_api::reorderChatFilters>(as_chat_filter_ids(args)));
    } else if (op == "grcf") {
      send_request(td_api::make_object<td_api::getRecommendedChatFilters>());
    } else if (op == "gcfdin") {
      execute(td_api::make_object<td_api::getChatFilterDefaultIconName>(as_chat_filter(args)));
    } else if (op == "sct") {
      string chat_id;
      string title;

      std::tie(chat_id, title) = split(args);
      send_request(td_api::make_object<td_api::setChatTitle>(as_chat_id(chat_id), title));
    } else if (op == "scpp") {
      string chat_id;
      string photo_id;

      std::tie(chat_id, photo_id) = split(args);
      send_request(td_api::make_object<td_api::setChatPhoto>(
          as_chat_id(chat_id), td_api::make_object<td_api::inputChatPhotoPrevious>(to_integer<int64>(photo_id))));
    } else if (op == "scp") {
      string chat_id;
      string photo_path;

      std::tie(chat_id, photo_path) = split(args);
      send_request(td_api::make_object<td_api::setChatPhoto>(
          as_chat_id(chat_id), td_api::make_object<td_api::inputChatPhotoStatic>(as_input_file(photo_path))));
    } else if (op == "scpa" || op == "scpv") {
      string chat_id;
      string animation;
      string main_frame_timestamp;

      std::tie(chat_id, args) = split(args);
      std::tie(animation, main_frame_timestamp) = split(args);
      send_request(td_api::make_object<td_api::setChatPhoto>(
          as_chat_id(chat_id), td_api::make_object<td_api::inputChatPhotoAnimation>(as_input_file(animation),
                                                                                    to_double(main_frame_timestamp))));
    } else if (op == "scperm") {
      string chat_id;
      string permissions;

      std::tie(chat_id, permissions) = split(args);
      if (permissions.size() == 8) {
        auto &s = permissions;
        send_request(td_api::make_object<td_api::setChatPermissions>(
            as_chat_id(chat_id),
            td_api::make_object<td_api::chatPermissions>(s[0] == '1', s[1] == '1', s[2] == '1', s[3] == '1',
                                                         s[4] == '1', s[5] == '1', s[6] == '1', s[7] == '1')));
      } else {
        LOG(ERROR) << "Wrong permissions size, expected 8";
      }
    } else if (op == "sccd") {
      string chat_id;
      string client_data;

      std::tie(chat_id, client_data) = split(args);
      send_request(td_api::make_object<td_api::setChatClientData>(as_chat_id(chat_id), client_data));
    } else if (op == "acm") {
      string chat_id;
      string user_id;
      string forward_limit;

      std::tie(chat_id, args) = split(args);
      std::tie(user_id, forward_limit) = split(args);
      send_request(td_api::make_object<td_api::addChatMember>(as_chat_id(chat_id), as_user_id(user_id),
                                                              to_integer<int32>(forward_limit)));
    } else if (op == "acms") {
      string chat_id;
      string user_ids;

      std::tie(chat_id, user_ids) = split(args);
      send_request(td_api::make_object<td_api::addChatMembers>(as_chat_id(chat_id), as_user_ids(user_ids)));
    } else if (op == "spolla") {
      string chat_id;
      string message_id;
      string option_ids;

      std::tie(chat_id, args) = split(args);
      std::tie(message_id, option_ids) = split(args);
      send_request(td_api::make_object<td_api::setPollAnswer>(as_chat_id(chat_id), as_message_id(message_id),
                                                              to_integers<int32>(option_ids)));
    } else if (op == "gpollv") {
      string chat_id;
      string message_id;
      string option_id;
      string offset;
      string limit;

      std::tie(chat_id, args) = split(args);
      std::tie(message_id, args) = split(args);
      std::tie(option_id, args) = split(args);
      std::tie(offset, limit) = split(args);
      send_request(td_api::make_object<td_api::getPollVoters>(as_chat_id(chat_id), as_message_id(message_id),
                                                              to_integer<int32>(option_id), to_integer<int32>(offset),
                                                              to_integer<int32>(limit)));
    } else if (op == "stoppoll") {
      string chat_id;
      string message_id;

      std::tie(chat_id, message_id) = split(args);
      send_request(td_api::make_object<td_api::stopPoll>(as_chat_id(chat_id), as_message_id(message_id), nullptr));
    } else {
      op_not_found_count++;
    }

    if (op == "scms") {
      string chat_id;
      string user_id;
      string status_str;
      td_api::object_ptr<td_api::ChatMemberStatus> status;

      std::tie(chat_id, args) = split(args);
      std::tie(user_id, status_str) = split(args);
      if (status_str == "member") {
        status = td_api::make_object<td_api::chatMemberStatusMember>();
      } else if (status_str == "left") {
        status = td_api::make_object<td_api::chatMemberStatusLeft>();
      } else if (status_str == "banned") {
        status = td_api::make_object<td_api::chatMemberStatusBanned>(std::numeric_limits<int32>::max());
      } else if (status_str == "creator") {
        status = td_api::make_object<td_api::chatMemberStatusCreator>("", true);
      } else if (status_str == "uncreator") {
        status = td_api::make_object<td_api::chatMemberStatusCreator>("", false);
      } else if (status_str == "admin") {
        status = td_api::make_object<td_api::chatMemberStatusAdministrator>("", true, true, true, true, true, true,
                                                                            true, true, true);
      } else if (status_str == "adminq") {
        status = td_api::make_object<td_api::chatMemberStatusAdministrator>("title", true, true, true, true, true, true,
                                                                            true, true, true);
      } else if (status_str == "minadmin") {
        status = td_api::make_object<td_api::chatMemberStatusAdministrator>("", true, true, false, false, false, false,
                                                                            false, false, false);
      } else if (status_str == "unadmin") {
        status = td_api::make_object<td_api::chatMemberStatusAdministrator>("", true, false, false, false, false, false,
                                                                            false, false, false);
      } else if (status_str == "rest") {
        status = td_api::make_object<td_api::chatMemberStatusRestricted>(
            true, static_cast<int32>(120 + std::time(nullptr)),
            td_api::make_object<td_api::chatPermissions>(false, false, false, false, false, false, false, false));
      } else if (status_str == "restkick") {
        status = td_api::make_object<td_api::chatMemberStatusRestricted>(
            false, static_cast<int32>(120 + std::time(nullptr)),
            td_api::make_object<td_api::chatPermissions>(true, false, false, false, false, false, false, false));
      } else if (status_str == "restunkick") {
        status = td_api::make_object<td_api::chatMemberStatusRestricted>(
            true, static_cast<int32>(120 + std::time(nullptr)),
            td_api::make_object<td_api::chatPermissions>(true, false, false, false, false, false, false, false));
      } else if (status_str == "unrest") {
        status = td_api::make_object<td_api::chatMemberStatusRestricted>(
            true, 0, td_api::make_object<td_api::chatPermissions>(true, true, true, true, true, true, true, true));
      }
      if (status != nullptr) {
        send_request(td_api::make_object<td_api::setChatMemberStatus>(as_chat_id(chat_id), as_user_id(user_id),
                                                                      std::move(status)));
      } else {
        LOG(ERROR) << "Unknown status \"" << status_str << "\"";
      }
    } else if (op == "cto") {
      send_request(td_api::make_object<td_api::canTransferOwnership>());
    } else if (op == "transferChatOwnership") {
      string chat_id;
      string user_id;
      string password;

      std::tie(chat_id, args) = split(args);
      std::tie(user_id, password) = split(args);
      send_request(
          td_api::make_object<td_api::transferChatOwnership>(as_chat_id(chat_id), as_user_id(user_id), password));
    } else if (op == "log") {
      string chat_id;
      string limit;

      std::tie(chat_id, limit) = split(args);
      send_request(td_api::make_object<td_api::getChatEventLog>(as_chat_id(chat_id), "", 0, to_integer<int32>(limit),
                                                                nullptr, vector<int32>()));
    } else if (op == "join") {
      send_request(td_api::make_object<td_api::joinChat>(as_chat_id(args)));
    } else if (op == "leave") {
      send_request(td_api::make_object<td_api::leaveChat>(as_chat_id(args)));
    } else if (op == "dcm") {
      string chat_id;
      string user_id_str;

      std::tie(chat_id, user_id_str) = split(args);
      auto user_id = as_user_id(user_id_str);
      td_api::object_ptr<td_api::ChatMemberStatus> status = td_api::make_object<td_api::chatMemberStatusBanned>();
      if (user_id == my_id_) {
        status = td_api::make_object<td_api::chatMemberStatusLeft>();
      }
      send_request(td_api::make_object<td_api::setChatMemberStatus>(as_chat_id(chat_id), user_id, std::move(status)));
    } else if (op == "sn") {
      string first_name;
      string last_name;
      std::tie(first_name, last_name) = split(args);
      send_request(td_api::make_object<td_api::setName>(first_name, last_name));
    } else if (op == "sb") {
      send_request(td_api::make_object<td_api::setBio>("\n" + args + "\n" + args + "\n"));
    } else if (op == "sun") {
      send_request(td_api::make_object<td_api::setUsername>(args));
    } else if (op == "ccun") {
      string chat_id;
      string username;

      std::tie(chat_id, username) = split(args);
      send_request(td_api::make_object<td_api::checkChatUsername>(as_chat_id(chat_id), username));
    } else if (op == "ssgun" || op == "schun") {
      string supergroup_id;
      string username;

      std::tie(supergroup_id, username) = split(args);
      send_request(td_api::make_object<td_api::setSupergroupUsername>(as_supergroup_id(supergroup_id), username));
    } else if (op == "ssgss") {
      string supergroup_id;
      string sticker_set_id;

      std::tie(supergroup_id, sticker_set_id) = split(args);
      send_request(td_api::make_object<td_api::setSupergroupStickerSet>(as_supergroup_id(supergroup_id),
                                                                        to_integer<int64>(sticker_set_id)));
    } else if (op == "tsgp") {
      string supergroup_id;
      string is_all_history_available;

      std::tie(supergroup_id, is_all_history_available) = split(args);
      send_request(td_api::make_object<td_api::toggleSupergroupIsAllHistoryAvailable>(
          as_supergroup_id(supergroup_id), as_bool(is_all_history_available)));
    } else if (op == "tsgsm") {
      string supergroup_id;
      string sign_messages;

      std::tie(supergroup_id, sign_messages) = split(args);
      send_request(td_api::make_object<td_api::toggleSupergroupSignMessages>(as_supergroup_id(supergroup_id),
                                                                             as_bool(sign_messages)));
    } else if (op == "scd") {
      string chat_id;
      string description;

      std::tie(chat_id, description) = split(args);
      send_request(td_api::make_object<td_api::setChatDescription>(as_chat_id(chat_id), description));
    } else if (op == "scdg") {
      string chat_id;
      string group_chat_id;

      std::tie(chat_id, group_chat_id) = split(args);
      send_request(td_api::make_object<td_api::setChatDiscussionGroup>(as_chat_id(chat_id), as_chat_id(group_chat_id)));
    } else if (op == "scl") {
      string chat_id;
      string latitude;
      string longitude;

      std::tie(chat_id, args) = split(args);
      std::tie(latitude, longitude) = split(args);
      send_request(td_api::make_object<td_api::setChatLocation>(
          as_chat_id(chat_id), td_api::make_object<td_api::chatLocation>(as_location(latitude, longitude), "address")));
    } else if (op == "scsmd") {
      string chat_id;
      string slow_mode_delay;

      std::tie(chat_id, slow_mode_delay) = split(args);
      send_request(
          td_api::make_object<td_api::setChatSlowModeDelay>(as_chat_id(chat_id), to_integer<int32>(slow_mode_delay)));
    } else if (op == "pcm" || op == "pcms") {
      string chat_id;
      string message_id;

      std::tie(chat_id, message_id) = split(args);
      send_request(
          td_api::make_object<td_api::pinChatMessage>(as_chat_id(chat_id), as_message_id(message_id), op == "pcms"));
    } else if (op == "upcm") {
      send_request(td_api::make_object<td_api::unpinChatMessage>(as_chat_id(args)));
    } else if (op == "grib") {
      send_request(td_api::make_object<td_api::getRecentInlineBots>());
    } else if (op == "spc" || op == "su" || op == "sch") {
      send_request(td_api::make_object<td_api::searchPublicChat>(args));
    } else if (op == "spcs") {
      send_request(td_api::make_object<td_api::searchPublicChats>(args));
    } else if (op == "sc") {
      string limit;
      string query;
      std::tie(limit, query) = split(args);
      send_request(td_api::make_object<td_api::searchChats>(query, to_integer<int32>(limit)));
    } else if (op == "scos") {
      string limit;
      string query;
      std::tie(limit, query) = split(args);
      send_request(td_api::make_object<td_api::searchChatsOnServer>(query, to_integer<int32>(limit)));
    } else if (op == "scn") {
      string latitude;
      string longitude;

      std::tie(latitude, longitude) = split(args);
      send_request(td_api::make_object<td_api::searchChatsNearby>(as_location(latitude, longitude)));
    } else if (op == "sloc") {
      string latitude;
      string longitude;

      std::tie(latitude, longitude) = split(args);
      send_request(td_api::make_object<td_api::setLocation>(as_location(latitude, longitude)));
    } else if (op == "sco") {
      string limit;
      string query;
      std::tie(limit, query) = split(args);
      send_request(td_api::make_object<td_api::searchContacts>(query, to_integer<int32>(limit)));
    } else if (op == "arfc") {
      send_request(td_api::make_object<td_api::addRecentlyFoundChat>(as_chat_id(args)));
    } else if (op == "rrfc") {
      send_request(td_api::make_object<td_api::removeRecentlyFoundChat>(as_chat_id(args)));
    } else if (op == "crfcs") {
      send_request(td_api::make_object<td_api::clearRecentlyFoundChats>());
    } else if (op == "gwpp") {
      send_request(td_api::make_object<td_api::getWebPagePreview>(as_caption(args)));
    } else if (op == "gwpiv") {
      string url;
      string force_full;
      std::tie(url, force_full) = split(args);

      send_request(td_api::make_object<td_api::getWebPageInstantView>(url, as_bool(force_full)));
    } else if (op == "sppp") {
      send_request(td_api::make_object<td_api::setProfilePhoto>(
          td_api::make_object<td_api::inputChatPhotoPrevious>(to_integer<int64>(args))));
    } else if (op == "spp") {
      send_request(td_api::make_object<td_api::setProfilePhoto>(
          td_api::make_object<td_api::inputChatPhotoStatic>(as_input_file(args))));
    } else if (op == "sppa" || op == "sppv") {
      string animation;
      string main_frame_timestamp;
      std::tie(animation, main_frame_timestamp) = split(args);

      send_request(td_api::make_object<td_api::setProfilePhoto>(td_api::make_object<td_api::inputChatPhotoAnimation>(
          as_input_file(animation), to_double(main_frame_timestamp))));
    } else if (op == "sh") {
      auto prefix = std::move(args);
      send_request(td_api::make_object<td_api::searchHashtags>(prefix, 10));
    } else if (op == "rrh") {
      auto hashtag = std::move(args);
      send_request(td_api::make_object<td_api::removeRecentHashtag>(hashtag));
    } else if (op == "view") {
      string chat_id;
      string message_ids;
      std::tie(chat_id, message_ids) = split(args);

      send_request(td_api::make_object<td_api::viewMessages>(as_chat_id(chat_id), as_message_ids(message_ids), true));
    } else if (op == "omc") {
      string chat_id;
      string message_id;
      std::tie(chat_id, message_id) = split(args);

      send_request(td_api::make_object<td_api::openMessageContent>(as_chat_id(chat_id), as_message_id(message_id)));
    } else if (op == "racm") {
      string chat_id = args;
      send_request(td_api::make_object<td_api::readAllChatMentions>(as_chat_id(chat_id)));
    } else if (op == "tre") {
      send_request(td_api::make_object<td_api::testReturnError>(
          args.empty() ? nullptr : td_api::make_object<td_api::error>(-1, args)));
    } else if (op == "dpp") {
      send_request(td_api::make_object<td_api::deleteProfilePhoto>(to_integer<int64>(args)));
    } else if (op == "gcnse" || op == "gcnses") {
      send_request(td_api::make_object<td_api::getChatNotificationSettingsExceptions>(
          get_notification_settings_scope(args), op == "gcnses"));
    } else if (op == "gsns") {
      send_request(td_api::make_object<td_api::getScopeNotificationSettings>(get_notification_settings_scope(args)));
    } else if (op == "scns" || op == "ssns") {
      string chat_id_or_scope;
      string settings;

      std::tie(chat_id_or_scope, settings) = split(args);

      string mute_for;
      string sound;
      string show_preview;
      string disable_pinned_message_notifications;
      string disable_mention_notifications;

      std::tie(mute_for, settings) = split(settings, ',');
      std::tie(sound, settings) = split(settings, ',');
      std::tie(show_preview, settings) = split(settings, ',');
      std::tie(disable_pinned_message_notifications, disable_mention_notifications) = split(settings, ',');

      if (op == "scns") {
        send_request(td_api::make_object<td_api::setChatNotificationSettings>(
            as_chat_id(chat_id_or_scope),
            td_api::make_object<td_api::chatNotificationSettings>(
                mute_for.empty(), to_integer<int32>(mute_for), sound.empty(), sound, show_preview.empty(),
                as_bool(show_preview), disable_pinned_message_notifications.empty(),
                as_bool(disable_pinned_message_notifications), disable_mention_notifications.empty(),
                as_bool(disable_mention_notifications))));
      } else {
        send_request(td_api::make_object<td_api::setScopeNotificationSettings>(
            get_notification_settings_scope(chat_id_or_scope),
            td_api::make_object<td_api::scopeNotificationSettings>(
                to_integer<int32>(mute_for), sound, as_bool(show_preview),
                as_bool(disable_pinned_message_notifications), as_bool(disable_mention_notifications))));
      }
    } else if (op == "rans") {
      send_request(td_api::make_object<td_api::resetAllNotificationSettings>());
    } else if (op == "rn") {
      string group_id;
      string notification_ids;
      std::tie(group_id, notification_ids) = split(args);
      for (auto notification_id : to_integers<int32>(notification_ids)) {
        send_request(td_api::make_object<td_api::removeNotification>(to_integer<int32>(group_id), notification_id));
      }
    } else if (op == "rng") {
      string group_id;
      string max_notification_id;
      std::tie(group_id, max_notification_id) = split(args);
      send_request(td_api::make_object<td_api::removeNotificationGroup>(to_integer<int32>(group_id),
                                                                        to_integer<int32>(max_notification_id)));
    } else if (op == "rcab") {
      string chat_id = args;
      send_request(td_api::make_object<td_api::removeChatActionBar>(as_chat_id(chat_id)));
    } else if (op == "rc") {
      string chat_id;
      string reason_str;
      string message_ids;
      std::tie(chat_id, args) = split(args);
      std::tie(reason_str, message_ids) = split(args);

      td_api::object_ptr<td_api::ChatReportReason> reason;
      if (reason_str == "spam") {
        reason = td_api::make_object<td_api::chatReportReasonSpam>();
      } else if (reason_str == "violence") {
        reason = td_api::make_object<td_api::chatReportReasonViolence>();
      } else if (reason_str == "porno") {
        reason = td_api::make_object<td_api::chatReportReasonPornography>();
      } else if (reason_str == "ca") {
        reason = td_api::make_object<td_api::chatReportReasonChildAbuse>();
      } else if (reason_str == "copyright") {
        reason = td_api::make_object<td_api::chatReportReasonCopyright>();
      } else if (reason_str == "geo" || reason_str == "location") {
        reason = td_api::make_object<td_api::chatReportReasonUnrelatedLocation>();
      } else {
        reason = td_api::make_object<td_api::chatReportReasonCustom>(reason_str);
      }

      send_request(
          td_api::make_object<td_api::reportChat>(as_chat_id(chat_id), std::move(reason), as_message_ids(message_ids)));
    } else if (op == "gcsu") {
      string chat_id;
      string parameters;
      string is_dark;
      std::tie(chat_id, args) = split(args);
      std::tie(parameters, is_dark) = split(args);

      send_request(
          td_api::make_object<td_api::getChatStatisticsUrl>(as_chat_id(chat_id), parameters, as_bool(is_dark)));
    } else if (op == "gcst") {
      string chat_id;
      string is_dark;
      std::tie(chat_id, is_dark) = split(args);

      send_request(td_api::make_object<td_api::getChatStatistics>(as_chat_id(chat_id), as_bool(is_dark)));
    } else if (op == "gcstg") {
      string chat_id;
      string token;
      string x;
      std::tie(chat_id, args) = split(args);
      std::tie(token, x) = split(args);

      send_request(
          td_api::make_object<td_api::getChatStatisticsGraph>(as_chat_id(chat_id), token, to_integer<int64>(x)));
    } else if (op == "hsa" || op == "glu" || op == "glua") {
      send_request(td_api::make_object<td_api::hideSuggestedAction>(as_suggested_action(args)));
    } else if (op == "glui" || op == "glu" || op == "glua") {
      string chat_id;
      string message_id;
      string button_id;
      std::tie(chat_id, args) = split(args);
      std::tie(message_id, button_id) = split(args);

      if (op == "glui") {
        send_request(td_api::make_object<td_api::getLoginUrlInfo>(as_chat_id(chat_id), as_message_id(message_id),
                                                                  as_button_id(button_id)));
      } else {
        send_request(td_api::make_object<td_api::getLoginUrl>(as_chat_id(chat_id), as_message_id(message_id),
                                                              as_button_id(button_id), op == "glua"));
      }
    } else if (op == "rsgs" || op == "rchs") {
      string supergroup_id;
      string user_id;
      string message_ids;
      std::tie(supergroup_id, args) = split(args);
      std::tie(user_id, message_ids) = split(args);

      send_request(td_api::make_object<td_api::reportSupergroupSpam>(as_supergroup_id(supergroup_id),
                                                                     as_user_id(user_id), as_message_ids(message_ids)));
    } else if (op == "gdiff") {
      send_request(td_api::make_object<td_api::testGetDifference>());
    } else if (op == "dproxy") {
      send_request(td_api::make_object<td_api::disableProxy>());
    } else if (op == "eproxy") {
      send_request(td_api::make_object<td_api::enableProxy>(as_proxy_id(args)));
    } else if (op == "rproxy") {
      send_request(td_api::make_object<td_api::removeProxy>(as_proxy_id(args)));
    } else if (op == "aproxy" || op == "aeproxy" || op == "aeproxytcp" || op == "editproxy" || op == "editeproxy" ||
               op == "editeproxytcp" || op == "tproxy") {
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
        type = td_api::make_object<td_api::proxyTypeMtproto>(user);
      } else {
        if (port == "80" || port == "8080") {
          type = td_api::make_object<td_api::proxyTypeHttp>(user, password, op.back() != 'p');
        } else {
          type = td_api::make_object<td_api::proxyTypeSocks5>(user, password);
        }
      }
      auto port_int = to_integer<int32>(port);
      if (op[0] == 'e') {
        send_request(
            td_api::make_object<td_api::editProxy>(as_proxy_id(proxy_id), server, port_int, enable, std::move(type)));
      } else if (op == "tproxy") {
        send_request(td_api::make_object<td_api::testProxy>(server, port_int, std::move(type), 2, 10.0));
      } else {
        send_request(td_api::make_object<td_api::addProxy>(server, port_int, enable, std::move(type)));
      }
    } else if (op == "gproxy" || op == "gproxies") {
      send_request(td_api::make_object<td_api::getProxies>());
    } else if (op == "gproxyl" || op == "gpl") {
      send_request(td_api::make_object<td_api::getProxyLink>(as_proxy_id(args)));
    } else if (op == "pproxy") {
      send_request(td_api::make_object<td_api::pingProxy>(as_proxy_id(args)));
    } else if (op == "touch") {
      auto r_fd = FileFd::open(args, FileFd::Read | FileFd::Write);
      if (r_fd.is_error()) {
        LOG(ERROR) << r_fd.error();
        return;
      }

      auto fd = r_fd.move_as_ok();
      auto size = fd.get_size().move_as_ok();
      fd.seek(size).ignore();
      fd.write("a").ignore();
      fd.seek(size).ignore();
      fd.truncate_to_current_position(size).ignore();
    } else if (op == "mem") {
      auto r_mem_stats = mem_stat();
      if (r_mem_stats.is_error()) {
        LOG(ERROR) << r_mem_stats.error();
      } else {
        auto stats = r_mem_stats.move_as_ok();
        LOG(ERROR) << "RSS = " << stats.resident_size_ << ", peak RSS = " << stats.resident_size_peak_ << ", VSZ "
                   << stats.virtual_size_ << ", peak VSZ = " << stats.virtual_size_peak_;
      }
    } else if (op == "cpu") {
      uint32 inc_count = to_integer<uint32>(args);
      while (inc_count-- > 0) {
        cpu_counter_++;
      }
      auto r_cpu_stats = cpu_stat();
      if (r_cpu_stats.is_error()) {
        LOG(ERROR) << r_cpu_stats.error();
      } else {
        auto stats = r_cpu_stats.move_as_ok();
        LOG(ERROR) << cpu_counter_ << ", total ticks = " << stats.total_ticks_
                   << ", user ticks = " << stats.process_user_ticks_
                   << ", system ticks = " << stats.process_system_ticks_;
      }
    } else if (op == "SetVerbosity" || op == "SV") {
      Log::set_verbosity_level(to_integer<int>(args));
    } else if (op[0] == 'v' && op[1] == 'v') {
      Log::set_verbosity_level(static_cast<int>(op.size()));
    } else if (op[0] == 'v' && ('0' <= op[1] && op[1] <= '9')) {
      Log::set_verbosity_level(to_integer<int>(op.substr(1)));
    } else if (op == "slse") {
      execute(td_api::make_object<td_api::setLogStream>(td_api::make_object<td_api::logStreamEmpty>()));
    } else if (op == "slsd") {
      execute(td_api::make_object<td_api::setLogStream>(td_api::make_object<td_api::logStreamDefault>()));
    } else if (op == "gls") {
      execute(td_api::make_object<td_api::getLogStream>());
    } else if (op == "slvl") {
      execute(td_api::make_object<td_api::setLogVerbosityLevel>(to_integer<int32>(args)));
    } else if (op == "glvl") {
      execute(td_api::make_object<td_api::getLogVerbosityLevel>());
    } else if (op == "gtags" || op == "glt") {
      execute(td_api::make_object<td_api::getLogTags>());
    } else if (op == "sltvl" || op == "sltvle" || op == "tag") {
      string tag;
      string level;
      std::tie(tag, level) = split(args);
      auto request = td_api::make_object<td_api::setLogTagVerbosityLevel>(tag, to_integer<int32>(level));
      if (op == "sltvl") {
        send_request(std::move(request));
      } else {
        execute(std::move(request));
      }
    } else if (op == "gltvl" || op == "gltvle" || op == "gtag") {
      string tag = args;
      auto request = td_api::make_object<td_api::getLogTagVerbosityLevel>(tag);
      if (op == "gltvl") {
        send_request(std::move(request));
      } else {
        execute(std::move(request));
      }
    } else if (op == "alog" || op == "aloge") {
      string level;
      string text;
      std::tie(level, text) = split(args);

      auto request = td_api::make_object<td_api::addLogMessage>(to_integer<int32>(level), text);
      if (op == "alog") {
        send_request(std::move(request));
      } else {
        execute(std::move(request));
      }
    } else if (op == "q" || op == "Quit") {
      quit();
    } else if (op == "dnq" || op == "DumpNetQueries") {
      dump_pending_network_queries(*net_query_stats_);
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

  bool is_inited_ = false;
  void loop() override {
    if (!is_inited_) {
      is_inited_ = true;
      init();
    }
    stdin_.flush_read().ensure();
#ifdef USE_READLINE
    while (!stdin_.input_buffer().empty()) {
      rl_callback_read_char();
    }
#else
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
      if (Random::fast(0, 1) == 0) {
        auto open_flags = FileFd::Flags::Write | (it->local_size ? 0 : FileFd::Flags::Truncate | FileFd::Flags::Create);
        FileFd::open(it->destination, open_flags).move_as_ok().pwrite(block.as_slice(), it->local_size).ensure();
      } else {
        send_request(
            td_api::make_object<td_api::writeGeneratedFilePart>(it->id, it->local_size, block.as_slice().str()));
      }
      it->local_size += it->part_size;
      if (it->local_size == it->size) {
        send_request(td_api::make_object<td_api::setFileGenerationProgress>(it->id, it->size, it->size));
        send_request(td_api::make_object<td_api::finishFileGeneration>(it->id, nullptr));
        it = pending_file_generations_.erase(it);
      } else {
        auto local_size = it->local_size;
        if (it->test_local_size_decrease && local_size > it->size / 2) {
          local_size = local_size * 2 - it->size;
        }
        send_request(td_api::make_object<td_api::setFileGenerationProgress>(it->id, (it->size + 3 * it->local_size) / 4,
                                                                            local_size));
        ++it;
      }
    }

    if (!pending_file_generations_.empty()) {
      set_timeout_in(0.01);
    }
  }

  void notify() override {
    auto guard = scheduler_->get_send_guard();
    send_event_later(actor_id(), Event::yield());
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
  int stdin_getc() {
    auto slice = stdin_.input_buffer().prepare_read();
    if (slice.empty()) {
      return EOF;
    }
    int res = slice[0];
    stdin_.input_buffer().confirm_read(1);
    return res;
  }

  std::unordered_map<int32, double> being_downloaded_files_;

  int32 my_id_ = 0;
  string schedule_date_;

  ConcurrentScheduler *scheduler_{nullptr};

  bool use_test_dc_ = false;
  std::shared_ptr<NetQueryStats> net_query_stats_ = create_net_query_stats();
  ActorOwn<ClientActor> td_client_;
  std::queue<string> cmd_queue_;
  bool close_flag_ = false;
  bool ready_to_stop_ = false;
  bool is_stdin_reader_stopped_ = false;

  bool get_chat_list_ = false;
  bool disable_network_ = false;
  int api_id_ = 0;
  std::string api_hash_;

  static std::atomic<uint64> cpu_counter_;
};
CliClient *CliClient::instance_ = nullptr;
std::atomic<uint64> CliClient::cpu_counter_;

void quit() {
  CliClient::quit_instance();
}

static void fail_signal(int sig) {
  signal_safe_write_signal_number(sig);
  while (true) {
    // spin forever to allow debugger to attach
  }
}

static void on_fatal_error(const char *error) {
  std::cerr << "Fatal error: " << error << std::endl;
}

void main(int argc, char **argv) {
  ignore_signal(SignalType::HangUp).ensure();
  ignore_signal(SignalType::Pipe).ensure();
  set_signal_handler(SignalType::Error, fail_signal).ensure();
  set_signal_handler(SignalType::Abort, fail_signal).ensure();
  Log::set_fatal_error_callback(on_fatal_error);

  const char *locale_name = (std::setlocale(LC_ALL, "fr-FR") == nullptr ? "" : "fr-FR");
  std::locale new_locale(locale_name);
  std::locale::global(new_locale);
  SCOPE_EXIT {
    std::locale::global(std::locale::classic());
    static NullLog null_log;
    log_interface = &null_log;
  };

  CliLog cli_log;
  log_interface = &cli_log;

  FileLog file_log;
  TsLog ts_log(&file_log);

  int new_verbosity_level = VERBOSITY_NAME(INFO);
  bool use_test_dc = false;
  bool get_chat_list = false;
  bool disable_network = false;
  auto api_id = [](auto x) -> int32 {
    if (x) {
      return to_integer<int32>(Slice(x));
    }
    return 0;
  }(std::getenv("TD_API_ID"));
  auto api_hash = [](auto x) -> std::string {
    if (x) {
      return x;
    }
    return std::string();
  }(std::getenv("TD_API_HASH"));

  OptionParser options;
  options.set_description("TDLib test client");
  options.add_option('\0', "test", "Use test DC", [&] { use_test_dc = true; });
  options.add_option('v', "verbosity", "Set verbosity level", [&](Slice level) {
    int new_verbosity = 1;
    while (begins_with(level, "v")) {
      new_verbosity++;
      level.remove_prefix(1);
    }
    if (!level.empty()) {
      new_verbosity += to_integer<int>(level) - (new_verbosity == 1);
    }
    new_verbosity_level = VERBOSITY_NAME(FATAL) + new_verbosity;
  });
  options.add_option('l', "log", "Log to file", [&](Slice file_name) {
    if (file_log.init(file_name.str()).is_ok() && file_log.init(file_name.str()).is_ok() &&
        file_log.init(file_name.str(), 1000 << 20).is_ok()) {
      log_interface = &ts_log;
    }
  });
  options.add_option('W', "", "Preload chat list", [&] { get_chat_list = true; });
  options.add_option('n', "disable-network", "Disable network", [&] { disable_network = true; });
  options.add_option('\0', "api-id", "Set Telegram API ID",
                     [&](Slice parameter) { api_id = to_integer<int32>(parameter); });
  options.add_option('\0', "api_id", "Set Telegram API ID",
                     [&](Slice parameter) { api_id = to_integer<int32>(parameter); });
  options.add_option('\0', "api-hash", "Set Telegram API hash", [&](Slice parameter) { api_hash = parameter.str(); });
  options.add_option('\0', "api_hash", "Set Telegram API hash", [&](Slice parameter) { api_hash = parameter.str(); });
  options.add_check([&] {
    if (api_id == 0 || api_hash.empty()) {
      return Status::Error("You must provide valid api-id and api-hash obtained at https://my.telegram.org");
    }
    return Status::OK();
  });
  auto r_non_options = options.run(argc, argv, 0);
  if (r_non_options.is_error()) {
    LOG(PLAIN) << argv[0] << ": " << r_non_options.error().message();
    LOG(PLAIN) << options;
    return;
  }

  SET_VERBOSITY_LEVEL(new_verbosity_level);

  {
    ConcurrentScheduler scheduler;
    scheduler.init(3);

    class CreateClient : public Actor {
     public:
      CreateClient(ConcurrentScheduler *scheduler, bool use_test_dc, bool get_chat_list, bool disable_network,
                   int32 api_id, std::string api_hash)
          : scheduler_(scheduler)
          , use_test_dc_(use_test_dc)
          , get_chat_list_(get_chat_list)
          , disable_network_(disable_network)
          , api_id_(api_id)
          , api_hash_(std::move(api_hash)) {
      }

     private:
      void start_up() override {
        create_actor<CliClient>("CliClient", scheduler_, use_test_dc_, get_chat_list_, disable_network_, api_id_,
                                api_hash_)
            .release();
      }

      ConcurrentScheduler *scheduler_;
      bool use_test_dc_;
      bool get_chat_list_;
      bool disable_network_;
      int32 api_id_;
      std::string api_hash_;
    };
    scheduler
        .create_actor_unsafe<CreateClient>(0, "CreateClient", &scheduler, use_test_dc, get_chat_list, disable_network,
                                           api_id, api_hash)
        .release();

    scheduler.start();
    while (scheduler.run_main(Timestamp::in(100))) {
    }
    scheduler.finish();
  }

  dump_memory_usage();
}

}  // namespace td

int main(int argc, char **argv) {
  td::main(argc, argv);
}
