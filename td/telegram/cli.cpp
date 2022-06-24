//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/Client.h"
#include "td/telegram/ClientActor.h"
#include "td/telegram/td_api_json.h"

#include "td/net/HttpQuery.h"
#include "td/net/HttpReader.h"

#include "td/actor/actor.h"
#include "td/actor/ConcurrentScheduler.h"

#include "memprof/memprof.h"

#include "td/utils/algorithm.h"
#include "td/utils/base64.h"
#include "td/utils/buffer.h"
#include "td/utils/CombinedLog.h"
#include "td/utils/common.h"
#include "td/utils/crypto.h"
#include "td/utils/ExitGuard.h"
#include "td/utils/FileLog.h"
#include "td/utils/filesystem.h"
#include "td/utils/FlatHashMap.h"
#include "td/utils/FlatHashSet.h"
#include "td/utils/format.h"
#include "td/utils/JsonBuilder.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/NullLog.h"
#include "td/utils/OptionParser.h"
#include "td/utils/port/detail/ThreadIdGuard.h"
#include "td/utils/port/FileFd.h"
#include "td/utils/port/PollFlags.h"
#include "td/utils/port/signals.h"
#include "td/utils/port/Stat.h"
#include "td/utils/port/StdStreams.h"
#include "td/utils/port/thread_local.h"
#include "td/utils/Random.h"
#include "td/utils/ScopeGuard.h"
#include "td/utils/Slice.h"
#include "td/utils/SliceBuilder.h"
#include "td/utils/Status.h"
#include "td/utils/StringBuilder.h"
#include "td/utils/Time.h"
#include "td/utils/TsLog.h"
#include "td/utils/utf8.h"

#ifndef USE_READLINE
#include "td/utils/find_boundary.h"
#endif

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <limits>
#include <locale>
#include <memory>
#include <queue>
#include <tuple>
#include <utility>

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
  static const vector<CSlice> commands{"GetHistory",          "SetVerbosity",         "SendVideo",
                                       "SearchDocument",      "GetChatMember",        "GetSupergroupAdministrators",
                                       "GetSupergroupBanned", "GetSupergroupMembers", "GetFile",
                                       "DownloadFile",        "CancelDownloadFile",   "ImportContacts",
                                       "RemoveContacts",      "CreateSecretChat",     "CreateNewSecretChat"};
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

class CliLog final : public LogInterface {
  void do_append(int log_level, CSlice slice) final {
#ifdef USE_READLINE
    deactivate_readline();
    SCOPE_EXIT {
      reactivate_readline();
    };
#endif
    default_log_interface->do_append(log_level, slice);
  }
};

static CombinedLog combined_log;

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
      , api_hash_(std::move(api_hash)) {
  }

  static void quit_instance() {
    instance_->quit();
  }

 private:
  void start_up() final {
    yield();
  }

  FlatHashMap<uint64, SendMessageInfo> query_id_to_send_message_info_;
  FlatHashMap<uint64, SendMessageInfo> message_id_to_send_message_info_;

  struct User {
    string first_name;
    string last_name;
    string username;
  };

  FlatHashMap<int64, unique_ptr<User>> users_;
  FlatHashMap<string, int64> username_to_user_id_;

  vector<string> authentication_tokens_;

  void register_user(const td_api::user &user) {
    auto &new_user_ptr = users_[user.id_];
    if (new_user_ptr == nullptr) {
      new_user_ptr = make_unique<User>();
    }
    User &new_user = *new_user_ptr;
    new_user.first_name = user.first_name_;
    new_user.last_name = user.last_name_;
    new_user.username = user.username_;
    if (!new_user.username.empty()) {
      username_to_user_id_[to_lower(new_user.username)] = user.id_;
    }
  }

  void print_user(Logger &log, int64 user_id, bool full = false) {
    const User *user = users_[user_id].get();
    CHECK(user != nullptr);
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

  FlatHashMap<string, int64> username_to_supergroup_id_;
  void register_supergroup(const td_api::supergroup &supergroup) {
    if (!supergroup.username_.empty()) {
      username_to_supergroup_id_[to_lower(supergroup.username_)] = supergroup.id_;
    }
  }

  void update_option(const td_api::updateOption &option) {
    if (option.name_ == "my_id" && option.value_->get_id() == td_api::optionValueInteger::ID) {
      my_id_ = static_cast<const td_api::optionValueInteger *>(option.value_.get())->value_;
      LOG(INFO) << "Set my user identifier to " << my_id_;
    }
    if (option.name_ == "authentication_token" && option.value_->get_id() == td_api::optionValueString::ID) {
      authentication_tokens_.insert(authentication_tokens_.begin(),
                                    static_cast<const td_api::optionValueString *>(option.value_.get())->value_);
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
        send_request(td_api::make_object<td_api::searchChatMessages>(search_chat_id_, "", nullptr, last_message_id, 0,
                                                                     100, as_search_messages_filter("pvi"), 0));
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
    int64 part_size = 0;
    int64 local_size = 0;
    int64 size = 0;
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
      file_generation.part_size = to_integer<int64>(update.conversion_);
      file_generation.test_local_size_decrease = !update.conversion_.empty() && update.conversion_.back() == 't';
    }

    auto r_stat = stat(file_generation.source);
    if (r_stat.is_ok()) {
      auto size = r_stat.ok().size_;
      if (size <= 0 || size > (static_cast<int64>(4000) << 20)) {
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

  void on_update_autorization_state(td_api::object_ptr<td_api::AuthorizationState> &&state) {
    authorization_state_ = std::move(state);
    switch (authorization_state_->get_id()) {
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
        send_request(
            td_api::make_object<td_api::setOption>("use_pfs", td_api::make_object<td_api::optionValueBoolean>(true)));
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
        LOG(WARNING) << "Td closed";
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
    FlatHashSet<char> chars;
    for (auto c : trim(str)) {
      if (!is_alnum(c) && c != '-' && c != '@' && c != '.' && c != '/' && c != '\0' && static_cast<uint8>(c) <= 127) {
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

  static vector<Slice> autosplit(Slice str) {
    return full_split(trim(str), get_delimiter(str));
  }

  static vector<string> autosplit_str(Slice str) {
    return transform(autosplit(str), [](Slice slice) { return slice.str(); });
  }

  int64 as_chat_id(Slice str) const {
    str = trim(str);
    if (str == "me") {
      return my_id_;
    }
    if (str == ".") {
      return opened_chat_id_;
    }
    if (str[0] == '@') {
      str.remove_prefix(1);
    }
    if (is_alpha(str[0])) {
      auto it = username_to_user_id_.find(to_lower(str));
      if (it != username_to_user_id_.end()) {
        return it->second;
      }
      auto it2 = username_to_supergroup_id_.find(to_lower(str));
      if (it2 != username_to_supergroup_id_.end()) {
        auto supergroup_id = it2->second;
        return static_cast<int64>(-1000'000'000'000ll) - supergroup_id;
      }
      LOG(ERROR) << "Can't resolve " << str;
      return 0;
    }
    return to_integer<int64>(str);
  }

  static int32 as_chat_filter_id(Slice str) {
    return to_integer<int32>(trim(str));
  }

  static vector<int32> as_chat_filter_ids(Slice chat_filter_ids) {
    return transform(autosplit(chat_filter_ids), as_chat_filter_id);
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
    return transform(autosplit(chat_ids), [this](Slice str) { return as_chat_id(str); });
  }

  static int64 as_message_id(Slice str) {
    str = trim(str);
    if (!str.empty() && str.back() == 's') {
      return to_integer<int64>(str) << 20;
    }
    return to_integer<int64>(str);
  }

  static vector<int64> as_message_ids(Slice message_ids) {
    return transform(autosplit(message_ids), as_message_id);
  }

  static int64 as_message_thread_id(Slice str) {
    return as_message_id(str);
  }

  td_api::object_ptr<td_api::MessageSender> as_message_sender(Slice sender_id) const {
    sender_id = trim(sender_id);
    if (sender_id.empty() || sender_id[0] != '-') {
      return td_api::make_object<td_api::messageSenderUser>(as_user_id(sender_id));
    } else {
      return td_api::make_object<td_api::messageSenderChat>(as_chat_id(sender_id));
    }
  }

  static int32 as_button_id(Slice str) {
    return to_integer<int32>(trim(str));
  }

  static td_api::object_ptr<td_api::StickerType> as_sticker_type(string sticker_type) {
    if (!sticker_type.empty() && sticker_type.back() == 'a') {
      return td_api::make_object<td_api::stickerTypeAnimated>();
    }
    if (!sticker_type.empty() && sticker_type.back() == 'v') {
      return td_api::make_object<td_api::stickerTypeVideo>();
    }
    if (!sticker_type.empty() && sticker_type.back() == 'm') {
      auto position = td_api::make_object<td_api::maskPosition>(td_api::make_object<td_api::maskPointEyes>(),
                                                                Random::fast(-5, 5), Random::fast(-5, 5), 1.0);
      return td_api::make_object<td_api::stickerTypeMask>(Random::fast_bool() ? nullptr : std::move(position));
    }
    return td_api::make_object<td_api::stickerTypeStatic>();
  }

  static int32 as_limit(Slice str, int32 default_limit = 10) {
    if (str.empty()) {
      return default_limit;
    }
    return to_integer<int32>(trim(str));
  }

  int64 as_user_id(Slice str) const {
    str = trim(str);
    if (str == "me") {
      return my_id_;
    }
    if (str[0] == '@') {
      str.remove_prefix(1);
    }
    if (is_alpha(str[0])) {
      auto it = username_to_user_id_.find(to_lower(str));
      if (it != username_to_user_id_.end()) {
        return it->second;
      }
      LOG(ERROR) << "Can't find user " << str;
      return 0;
    }
    return to_integer<int64>(str);
  }

  vector<int64> as_user_ids(Slice user_ids) const {
    return transform(autosplit(user_ids), [this](Slice str) { return as_user_id(str); });
  }

  static int64 as_basic_group_id(Slice str) {
    str = trim(str);
    auto result = to_integer<int64>(str);
    if (result < 0) {
      return -result;
    }
    return result;
  }

  int64 as_supergroup_id(Slice str) const {
    str = trim(str);
    if (str[0] == '@') {
      str.remove_prefix(1);
    }
    if (is_alpha(str[0])) {
      auto it = username_to_supergroup_id_.find(to_lower(str));
      if (it == username_to_supergroup_id_.end()) {
        return 0;
      }
      return it->second;
    }
    auto result = to_integer<int64>(str);
    auto shift = static_cast<int64>(-1000000000000ll);
    if (result <= shift) {
      return shift - result;
    }
    return result;
  }

  static int32 as_secret_chat_id(Slice str) {
    str = trim(str);
    auto result = to_integer<int64>(str);
    auto shift = static_cast<int64>(-2000000000000ll);
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
                                                                 int64 expected_size = 0) {
    return td_api::make_object<td_api::inputFileGenerated>(trim(std::move(original_path)), trim(std::move(conversion)),
                                                           expected_size);
  }

  static td_api::object_ptr<td_api::InputFile> as_input_file(Slice str) {
    str = trim(str);
    if ((str.size() >= 20 && is_base64url(str)) || begins_with(str, "http")) {
      return as_remote_file(str.str());
    }
    auto r_file_id = to_integer_safe<int32>(str);
    if (r_file_id.is_ok()) {
      return as_input_file_id(str);
    }
    if (str.find(';') < str.size()) {
      auto res = split(str, ';');
      return as_generated_file(res.first.str(), res.second.str());
    }
    return as_local_file(str.str());
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

  struct CallId {
    int32 call_id = 0;

    operator int32() const {
      return call_id;
    }
  };

  void get_args(string &args, CallId &arg) const {
    arg.call_id = to_integer<int32>(trim(args));
  }

  struct GroupCallId {
    int32 group_call_id = 0;

    operator int32() const {
      return group_call_id;
    }
  };

  void get_args(string &args, GroupCallId &arg) const {
    arg.group_call_id = to_integer<int32>(trim(args));
  }

  static int32 as_proxy_id(string str) {
    return to_integer<int32>(trim(std::move(str)));
  }

  static td_api::object_ptr<td_api::location> as_location(const string &latitude, const string &longitude,
                                                          const string &accuracy) {
    if (trim(latitude).empty() && trim(longitude).empty()) {
      return nullptr;
    }
    return td_api::make_object<td_api::location>(to_double(latitude), to_double(longitude), to_double(accuracy));
  }

  static bool as_bool(string str) {
    str = to_lower(trim(str));
    return str == "true" || str == "1";
  }

  template <class T>
  static vector<T> to_integers(Slice integers) {
    return transform(transform(autosplit(integers), trim<Slice>), to_integer<T>);
  }

  static void get_args(string &args, string &arg) {
    if (&args != &arg) {
      arg = std::move(args);
    }
  }

  static void get_args(string &args, bool &arg) {
    arg = as_bool(args);
  }

  struct SearchQuery {
    int32 limit = 0;
    string query;
  };

  static void get_args(string &args, SearchQuery &arg) {
    string limit;
    std::tie(limit, arg.query) = split(trim(args));
    auto r_limit = to_integer_safe<int32>(limit);
    if (r_limit.is_ok() && r_limit.ok() > 0) {
      arg.limit = r_limit.ok();
    } else {
      arg.limit = 10;
      arg.query = std::move(args);
    }
    args.clear();
  }

  static void get_args(string &args, int32 &arg) {
    arg = to_integer<int32>(args);
  }

  static void get_args(string &args, int64 &arg) {
    arg = to_integer<int64>(args);
  }

  struct ChatId {
    int64 chat_id = 0;

    operator int64() const {
      return chat_id;
    }
  };

  void get_args(string &args, ChatId &arg) const {
    arg.chat_id = as_chat_id(args);
  }

  struct MessageId {
    int64 message_id = 0;

    operator int64() const {
      return message_id;
    }
  };

  void get_args(string &args, MessageId &arg) const {
    arg.message_id = as_message_id(args);
  }

  struct UserId {
    int64 user_id = 0;

    operator int64() const {
      return user_id;
    }
  };

  void get_args(string &args, UserId &arg) const {
    arg.user_id = as_user_id(args);
  }

  struct FileId {
    int32 file_id = 0;

    operator int32() const {
      return file_id;
    }
  };

  void get_args(string &args, FileId &arg) const {
    arg.file_id = as_file_id(args);
  }

  struct InputInvoice {
    int64 chat_id = 0;
    int64 message_id = 0;
    string invoice_name;

    operator td_api::object_ptr<td_api::InputInvoice>() const {
      if (invoice_name.empty()) {
        return td_api::make_object<td_api::inputInvoiceMessage>(chat_id, message_id);
      } else {
        return td_api::make_object<td_api::inputInvoiceName>(invoice_name);
      }
    }
  };

  void get_args(string &args, InputInvoice &arg) const {
    if (args.size() > 1 && args[0] == '#') {
      arg.invoice_name = args;
    } else {
      string chat_id;
      string message_id;
      std::tie(chat_id, message_id) = split(args, get_delimiter(args));
      arg.chat_id = as_chat_id(chat_id);
      arg.message_id = as_message_id(message_id);
    }
  }

  template <class FirstType, class SecondType, class... Types>
  void get_args(string &args, FirstType &first_arg, SecondType &second_arg, Types &...other_args) const {
    string arg;
    std::tie(arg, args) = split(args);
    get_args(arg, first_arg);
    get_args(args, second_arg, other_args...);
  }

  void on_result(uint64 generation, uint64 id, td_api::object_ptr<td_api::Object> result) {
    auto result_str = to_string(result);
    if (result != nullptr) {
      switch (result->get_id()) {
        case td_api::stickerSets::ID: {
          auto sticker_sets = static_cast<const td_api::stickerSets *>(result.get());
          result_str = PSTRING() << "StickerSets { total_count = " << sticker_sets->total_count_
                                 << ", count = " << sticker_sets->sets_.size();
          for (auto &sticker_set : sticker_sets->sets_) {
            result_str += PSTRING() << ", " << sticker_set->name_;
          }
          result_str += " }";
          break;
        }
        case td_api::trendingStickerSets::ID: {
          auto sticker_sets = static_cast<const td_api::trendingStickerSets *>(result.get());
          result_str = PSTRING() << "TrendingStickerSets { is_premium = " << sticker_sets->is_premium_
                                 << ", total_count = " << sticker_sets->total_count_
                                 << ", count = " << sticker_sets->sets_.size();
          for (auto &sticker_set : sticker_sets->sets_) {
            result_str += PSTRING() << ", " << sticker_set->name_;
          }
          result_str += " }";
          break;
        }
        default:
          break;
      }
    }

    if (id > 0 && combined_log.get_first_verbosity_level() < get_log_tag_verbosity_level("td_requests")) {
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
        LOG(WARNING) << result_str;
        on_update_autorization_state(
            std::move(static_cast<td_api::updateAuthorizationState *>(result.get())->authorization_state_));
        break;
      case td_api::updateChatLastMessage::ID: {
        auto message = static_cast<const td_api::updateChatLastMessage *>(result.get())->last_message_.get();
        if (message != nullptr && message->content_->get_id() == td_api::messageText::ID) {
          // auto text = static_cast<const td_api::messageText *>(message->content_.get())->text_->text_;
        }
        break;
      }
      case td_api::updateNewMessage::ID: {
        auto message = static_cast<const td_api::updateNewMessage *>(result.get())->message_.get();
        if (message != nullptr && message->content_->get_id() == td_api::messageText::ID) {
          auto chat_id = message->chat_id_;
          auto text = static_cast<const td_api::messageText *>(message->content_.get())->text_->text_;
          if (text == "/start" && !message->is_outgoing_ && use_test_dc_) {
            on_cmd(PSTRING() << "sm " << chat_id << " Hi!");
          }
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
      default:
        break;
    }
  }

  void on_error(uint64 generation, uint64 id, td_api::object_ptr<td_api::error> error) {
    if (id > 0 && combined_log.get_first_verbosity_level() < get_log_tag_verbosity_level("td_requests")) {
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

    LOG(WARNING) << "Creating new Td " << name << " with generation " << generation_ + 1;
    class TdCallbackImpl final : public TdCallback {
     public:
      TdCallbackImpl(CliClient *client, uint64 generation) : client_(client), generation_(generation) {
      }
      void on_result(uint64 id, td_api::object_ptr<td_api::Object> result) final {
        client_->on_result(generation_, id, std::move(result));
      }
      void on_error(uint64 id, td_api::object_ptr<td_api::error> error) final {
        client_->on_error(generation_, id, std::move(error));
      }
      TdCallbackImpl(const TdCallbackImpl &) = delete;
      TdCallbackImpl &operator=(const TdCallbackImpl &) = delete;
      TdCallbackImpl(TdCallbackImpl &&) = delete;
      TdCallbackImpl &operator=(TdCallbackImpl &&) = delete;
      ~TdCallbackImpl() final {
        client_->on_closed(generation_);
      }

     private:
      CliClient *client_;
      uint64 generation_;
    };

    ClientActor::Options options;
    options.net_query_stats = net_query_stats_;

    td_client_ = create_actor<ClientActor>(name, make_unique<TdCallbackImpl>(this, ++generation_), std::move(options));

    if (get_chat_list_) {
      send_request(td_api::make_object<td_api::getChats>(nullptr, 10000));
    }
    if (disable_network_) {
      send_request(td_api::make_object<td_api::setNetworkType>(td_api::make_object<td_api::networkTypeNone>()));
    }
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
      const string &text, vector<td_api::object_ptr<td_api::textEntity>> entities = {}) {
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
      const string &caption, vector<td_api::object_ptr<td_api::textEntity>> entities = {}) {
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

  td_api::object_ptr<td_api::userPrivacySettingRules> get_user_privacy_setting_rules(Slice allow, Slice ids) const {
    vector<td_api::object_ptr<td_api::UserPrivacySettingRule>> rules;
    if (allow == "c" || allow == "contacts") {
      rules.push_back(td_api::make_object<td_api::userPrivacySettingRuleAllowContacts>());
    } else if (allow == "users") {
      rules.push_back(td_api::make_object<td_api::userPrivacySettingRuleAllowUsers>(as_user_ids(ids)));
    } else if (allow == "chats") {
      rules.push_back(td_api::make_object<td_api::userPrivacySettingRuleAllowChatMembers>(as_chat_ids(ids)));
    } else if (as_bool(allow.str())) {
      rules.push_back(td_api::make_object<td_api::userPrivacySettingRuleAllowAll>());
      rules.push_back(td_api::make_object<td_api::userPrivacySettingRuleRestrictAll>());
    } else {
      rules.push_back(td_api::make_object<td_api::userPrivacySettingRuleRestrictAll>());
    }
    return td_api::make_object<td_api::userPrivacySettingRules>(std::move(rules));
  }

  static td_api::object_ptr<td_api::SearchMessagesFilter> as_search_messages_filter(Slice filter) {
    filter = trim(filter);
    string lowered_filter = to_lower(filter);
    filter = lowered_filter;
    if (begins_with(filter, "search")) {
      filter.remove_prefix(6);
    }
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
    if (filter == "ur" || filter == "ureaction") {
      return td_api::make_object<td_api::searchMessagesFilterUnreadReaction>();
    }
    if (filter == "f" || filter == "failed") {
      return td_api::make_object<td_api::searchMessagesFilterFailedToSend>();
    }
    if (filter == "pi" || filter == "pinned") {
      return td_api::make_object<td_api::searchMessagesFilterPinned>();
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
    if (begins_with(filter, "@")) {
      return td_api::make_object<td_api::chatMembersFilterMention>(as_message_thread_id(filter.substr(1)));
    }
    if (filter == "r" || filter == "rest" || filter == "restricted") {
      return td_api::make_object<td_api::chatMembersFilterRestricted>();
    }
    if (!filter.empty()) {
      LOG(ERROR) << "Unsupported chat member filter " << filter;
    }
    return nullptr;
  }

  static td_api::object_ptr<td_api::SupergroupMembersFilter> get_supergroup_members_filter(MutableSlice filter,
                                                                                           const string &query,
                                                                                           Slice message_thread_id) {
    filter = trim(filter);
    to_lower_inplace(filter);
    if (begins_with(filter, "get")) {
      filter.remove_prefix(3);
    }
    if (begins_with(filter, "search")) {
      filter.remove_prefix(6);
    }
    if (begins_with(filter, "supergroup")) {
      filter.remove_prefix(10);
    }
    if (filter == "administrators") {
      return td_api::make_object<td_api::supergroupMembersFilterAdministrators>();
    }
    if (filter == "banned") {
      return td_api::make_object<td_api::supergroupMembersFilterBanned>(query);
    }
    if (filter == "bots") {
      return td_api::make_object<td_api::supergroupMembersFilterBots>();
    }
    if (filter == "contacts") {
      return td_api::make_object<td_api::supergroupMembersFilterContacts>(query);
    }
    if (filter == "members") {
      if (query.empty()) {
        return td_api::make_object<td_api::supergroupMembersFilterRecent>();
      } else {
        return td_api::make_object<td_api::supergroupMembersFilterSearch>(query);
      }
    }
    if (filter == "restricted") {
      return td_api::make_object<td_api::supergroupMembersFilterRestricted>(query);
    }
    if (filter == "nentions") {
      return td_api::make_object<td_api::supergroupMembersFilterMention>(query,
                                                                         as_message_thread_id(message_thread_id));
    }
    return nullptr;
  }

  static bool rand_bool() {
    return Random::fast_bool();
  }

  td_api::object_ptr<td_api::chatFilter> as_chat_filter(string filter) const {
    string title;
    string icon_name;
    string pinned_chat_ids;
    string included_chat_ids;
    string excluded_chat_ids;
    get_args(filter, title, icon_name, pinned_chat_ids, included_chat_ids, excluded_chat_ids);
    return td_api::make_object<td_api::chatFilter>(
        title, icon_name, as_chat_ids(pinned_chat_ids), as_chat_ids(included_chat_ids), as_chat_ids(excluded_chat_ids),
        rand_bool(), rand_bool(), rand_bool(), rand_bool(), rand_bool(), rand_bool(), rand_bool(), rand_bool());
  }

  static td_api::object_ptr<td_api::chatAdministratorRights> as_chat_administrator_rights(
      bool can_manage_chat, bool can_change_info, bool can_post_messages, bool can_edit_messages,
      bool can_delete_messages, bool can_invite_users, bool can_restrict_members, bool can_pin_messages,
      bool can_promote_members, bool can_manage_video_chats, bool is_anonymous) {
    return td_api::make_object<td_api::chatAdministratorRights>(
        can_manage_chat, can_change_info, can_post_messages, can_edit_messages, can_delete_messages, can_invite_users,
        can_restrict_members, can_pin_messages, can_promote_members, can_manage_video_chats, is_anonymous);
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
    if (action == "cs" || action == "choose_sticker") {
      return td_api::make_object<td_api::chatActionChoosingSticker>();
    }
    if (begins_with(action, "wa")) {
      return td_api::make_object<td_api::chatActionWatchingAnimations>(action.substr(2).str());
    }
    return td_api::make_object<td_api::chatActionTyping>();
  }

  static td_api::object_ptr<td_api::ChatReportReason> get_chat_report_reason(MutableSlice reason) {
    reason = trim(reason);
    if (reason == "null") {
      return nullptr;
    }
    if (reason == "spam") {
      return td_api::make_object<td_api::chatReportReasonSpam>();
    }
    if (reason == "violence") {
      return td_api::make_object<td_api::chatReportReasonViolence>();
    }
    if (reason == "porno") {
      return td_api::make_object<td_api::chatReportReasonPornography>();
    }
    if (reason == "ca") {
      return td_api::make_object<td_api::chatReportReasonChildAbuse>();
    }
    if (reason == "copyright") {
      return td_api::make_object<td_api::chatReportReasonCopyright>();
    }
    if (reason == "geo" || reason == "location") {
      return td_api::make_object<td_api::chatReportReasonUnrelatedLocation>();
    }
    if (reason == "fake") {
      return td_api::make_object<td_api::chatReportReasonFake>();
    }
    if (reason == "drugs") {
      return td_api::make_object<td_api::chatReportReasonIllegalDrugs>();
    }
    if (reason == "pd") {
      return td_api::make_object<td_api::chatReportReasonPersonalDetails>();
    }
    return td_api::make_object<td_api::chatReportReasonCustom>();
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

  td_api::object_ptr<td_api::SuggestedAction> as_suggested_action(Slice action) const {
    if (action == "unarchive") {
      return td_api::make_object<td_api::suggestedActionEnableArchiveAndMuteNewChats>();
    }
    if (action == "pass") {
      return td_api::make_object<td_api::suggestedActionCheckPassword>();
    }
    if (action == "number") {
      return td_api::make_object<td_api::suggestedActionCheckPhoneNumber>();
    }
    if (action == "checks") {
      return td_api::make_object<td_api::suggestedActionViewChecksHint>();
    }
    if (begins_with(action, "giga")) {
      return td_api::make_object<td_api::suggestedActionConvertToBroadcastGroup>(as_supergroup_id(action.substr(4)));
    }
    if (begins_with(action, "spass")) {
      return td_api::make_object<td_api::suggestedActionSetPassword>(to_integer<int32>(action.substr(5)));
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
    return transform(autosplit(types), as_passport_element_type);
  }

  static td_api::object_ptr<td_api::InputPassportElement> as_input_passport_element(const string &passport_element_type,
                                                                                    const string &arg,
                                                                                    bool with_selfie) {
    vector<td_api::object_ptr<td_api::InputFile>> input_files;
    td_api::object_ptr<td_api::InputFile> selfie;
    if (!arg.empty()) {
      auto files = autosplit(arg);
      CHECK(!files.empty());
      if (with_selfie) {
        selfie = as_input_file(files.back());
        files.pop_back();
      }
      for (const auto &file : files) {
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
      if (!input_files.empty()) {
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

  static td_api::object_ptr<td_api::themeParameters> get_theme_parameters() {
    return td_api::make_object<td_api::themeParameters>(0, 1, -1, 256, 65536, 123456789, 65535);
  }

  static td_api::object_ptr<td_api::BackgroundFill> get_background_fill(int32 color) {
    return td_api::make_object<td_api::backgroundFillSolid>(color);
  }

  static td_api::object_ptr<td_api::BackgroundFill> get_background_fill(int32 top_color, int32 bottom_color) {
    return td_api::make_object<td_api::backgroundFillGradient>(top_color, bottom_color, Random::fast(0, 7) * 45);
  }

  static td_api::object_ptr<td_api::BackgroundFill> get_background_fill(vector<int32> colors) {
    return td_api::make_object<td_api::backgroundFillFreeformGradient>(std::move(colors));
  }

  static td_api::object_ptr<td_api::BackgroundType> get_solid_pattern_background(int32 color, int32 intensity,
                                                                                 bool is_moving) {
    return get_gradient_pattern_background(color, color, intensity, false, is_moving);
  }

  static td_api::object_ptr<td_api::BackgroundType> get_gradient_pattern_background(int32 top_color, int32 bottom_color,
                                                                                    int32 intensity, bool is_inverted,
                                                                                    bool is_moving) {
    return td_api::make_object<td_api::backgroundTypePattern>(get_background_fill(top_color, bottom_color), intensity,
                                                              is_inverted, is_moving);
  }

  static td_api::object_ptr<td_api::BackgroundType> get_freeform_gradient_pattern_background(vector<int32> colors,
                                                                                             int32 intensity,
                                                                                             bool is_inverted,
                                                                                             bool is_moving) {
    return td_api::make_object<td_api::backgroundTypePattern>(get_background_fill(std::move(colors)), intensity,
                                                              is_inverted, is_moving);
  }

  static td_api::object_ptr<td_api::BackgroundType> get_solid_background(int32 color) {
    return td_api::make_object<td_api::backgroundTypeFill>(get_background_fill(color));
  }

  static td_api::object_ptr<td_api::BackgroundType> get_gradient_background(int32 top_color, int32 bottom_color) {
    return td_api::make_object<td_api::backgroundTypeFill>(get_background_fill(top_color, bottom_color));
  }

  static td_api::object_ptr<td_api::BackgroundType> get_freeform_gradient_background(vector<int32> colors) {
    return td_api::make_object<td_api::backgroundTypeFill>(get_background_fill(std::move(colors)));
  }

  td_api::object_ptr<td_api::phoneNumberAuthenticationSettings> get_phone_number_authentication_settings() const {
    return td_api::make_object<td_api::phoneNumberAuthenticationSettings>(false, true, false, false,
                                                                          vector<string>(authentication_tokens_));
  }

  static td_api::object_ptr<td_api::Object> execute(td_api::object_ptr<td_api::Function> f) {
    if (combined_log.get_first_verbosity_level() < get_log_tag_verbosity_level("td_requests")) {
      LOG(ERROR) << "Execute request: " << to_string(f);
    }
    auto res = ClientActor::execute(std::move(f));
    if (combined_log.get_first_verbosity_level() < get_log_tag_verbosity_level("td_requests")) {
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

  static int32 get_log_tag_verbosity_level(const string &name) {
    auto level = ClientActor::execute(td_api::make_object<td_api::getLogTagVerbosityLevel>(name));
    if (level->get_id() == td_api::error::ID) {
      return -1;
    }
    CHECK(level->get_id() == td_api::logVerbosityLevel::ID);
    return static_cast<const td_api::logVerbosityLevel *>(level.get())->verbosity_level_;
  }

  void send_message(int64 chat_id, td_api::object_ptr<td_api::InputMessageContent> &&input_message_content,
                    bool disable_notification = false, bool from_background = false, int64 reply_to_message_id = 0) {
    auto id = send_request(td_api::make_object<td_api::sendMessage>(
        chat_id, as_message_thread_id(message_thread_id_), reply_to_message_id,
        td_api::make_object<td_api::messageSendOptions>(disable_notification, from_background, true,
                                                        as_message_scheduling_state(schedule_date_)),
        nullptr, std::move(input_message_content)));
    if (id != 0) {
      query_id_to_send_message_info_[id].start_time = Time::now();
    }
  }

  td_api::object_ptr<td_api::messageSendOptions> default_message_send_options() const {
    return td_api::make_object<td_api::messageSendOptions>(false, false, false,
                                                           as_message_scheduling_state(schedule_date_));
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

    const int32 OP_BLOCK_COUNT = 10;
    int32 op_not_found_count = 0;

    if (op == "gas") {
      LOG(ERROR) << to_string(authorization_state_);
    } else if (op == "sap" || op == "sapn") {
      send_request(
          td_api::make_object<td_api::setAuthenticationPhoneNumber>(args, get_phone_number_authentication_settings()));
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
      get_args(args, first_name, last_name);
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
    } else if (op == "caprc") {
      string recovery_code = args;
      send_request(td_api::make_object<td_api::checkAuthenticationPasswordRecoveryCode>(recovery_code));
    } else if (op == "rap") {
      string recovery_code;
      string new_password;
      string new_hint;
      get_args(args, recovery_code, new_password, new_hint);
      send_request(td_api::make_object<td_api::recoverAuthenticationPassword>(recovery_code, new_password, new_hint));
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
      get_args(args, password, new_password, new_hint, recovery_email_address);
      if (password == "#") {
        password = "";
      }
      if (new_password == "#") {
        new_password = "";
      }
      if (new_hint == "#") {
        new_hint = "";
      }
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
      string bot_user_id = query.get_arg("bot_id").str();
      string scope = query.get_arg("scope").str();
      string public_key = query.get_arg("public_key").str();
      string payload = query.get_arg("payload").str();
      LOG(INFO) << "Callback URL:" << query.get_arg("callback_url");
      send_request(td_api::make_object<td_api::getPassportAuthorizationForm>(as_user_id(bot_user_id), scope, public_key,
                                                                             payload));
    } else if (op == "gpaf") {
      UserId bot_user_id;
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
      get_args(args, bot_user_id, scope, payload);
      send_request(td_api::make_object<td_api::getPassportAuthorizationForm>(bot_user_id, scope, public_key, payload));
    } else if (op == "gpafae") {
      int32 form_id;
      string password;
      get_args(args, form_id, password);
      send_request(td_api::make_object<td_api::getPassportAuthorizationFormAvailableElements>(form_id, password));
    } else if (op == "spaf") {
      int32 form_id;
      string types;
      get_args(args, form_id, types);
      send_request(
          td_api::make_object<td_api::sendPassportAuthorizationForm>(form_id, as_passport_element_types(types)));
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
      get_args(args, password, recovery_email_address);
      send_request(td_api::make_object<td_api::setRecoveryEmailAddress>(password, recovery_email_address));
    } else if (op == "grea" || op == "GetRecoveryEmailAddress") {
      send_request(td_api::make_object<td_api::getRecoveryEmailAddress>(args));
    } else if (op == "creac") {
      send_request(td_api::make_object<td_api::checkRecoveryEmailAddressCode>(args));
    } else if (op == "rreac") {
      send_request(td_api::make_object<td_api::resendRecoveryEmailAddressCode>());
    } else if (op == "spncc") {
      string hash;
      string phone_number;
      get_args(args, hash, phone_number);
      send_request(td_api::make_object<td_api::sendPhoneNumberConfirmationCode>(hash, phone_number, nullptr));
    } else if (op == "cpncc") {
      send_request(td_api::make_object<td_api::checkPhoneNumberConfirmationCode>(args));
    } else if (op == "rpncc") {
      send_request(td_api::make_object<td_api::resendPhoneNumberConfirmationCode>());
    } else if (op == "rpr") {
      send_request(td_api::make_object<td_api::requestPasswordRecovery>());
    } else if (op == "cprc") {
      string recovery_code = args;
      send_request(td_api::make_object<td_api::checkPasswordRecoveryCode>(recovery_code));
    } else if (op == "rp") {
      string recovery_code;
      string new_password;
      string new_hint;
      get_args(args, recovery_code, new_password, new_hint);
      send_request(td_api::make_object<td_api::recoverPassword>(recovery_code, new_password, new_hint));
    } else if (op == "resetp") {
      send_request(td_api::make_object<td_api::resetPassword>());
    } else if (op == "cpr") {
      send_request(td_api::make_object<td_api::cancelPasswordReset>());
    } else if (op == "gtp" || op == "GetTemporaryPassword") {
      send_request(td_api::make_object<td_api::getTemporaryPasswordState>());
    } else if (op == "ctp" || op == "CreateTemporaryPassword") {
      send_request(td_api::make_object<td_api::createTemporaryPassword>(args, 60 * 6));
    } else if (op == "gpe") {
      string password;
      string passport_element_type;
      get_args(args, password, passport_element_type);
      send_request(
          td_api::make_object<td_api::getPassportElement>(as_passport_element_type(passport_element_type), password));
    } else if (op == "gape") {
      string password = args;
      send_request(td_api::make_object<td_api::getAllPassportElements>(password));
    } else if (op == "spe" || op == "spes") {
      string password;
      string passport_element_type;
      string arg;
      get_args(args, password, passport_element_type, arg);
      send_request(td_api::make_object<td_api::setPassportElement>(
          as_input_passport_element(passport_element_type, arg, op == "spes"), password));
    } else if (op == "dpe") {
      const string &passport_element_type = args;
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
      get_args(args, token, other_user_ids_str);
      send_request(td_api::make_object<td_api::registerDevice>(td_api::make_object<td_api::deviceTokenTizenPush>(token),
                                                               as_user_ids(other_user_ids_str)));
    } else if (op == "rdu") {
      string token;
      string other_user_ids_str;
      get_args(args, token, other_user_ids_str);
      send_request(td_api::make_object<td_api::registerDevice>(
          td_api::make_object<td_api::deviceTokenUbuntuPush>(token), as_user_ids(other_user_ids_str)));
    } else if (op == "rdw") {
      string endpoint;
      string key;
      string secret;
      string other_user_ids_str;
      get_args(args, endpoint, key, secret, other_user_ids_str);
      send_request(td_api::make_object<td_api::registerDevice>(
          td_api::make_object<td_api::deviceTokenWebPush>(endpoint, key, secret), as_user_ids(other_user_ids_str)));
    } else if (op == "gbci") {
      send_request(td_api::make_object<td_api::getBankCardInfo>(args));
    } else if (op == "gpf") {
      InputInvoice input_invoice;
      get_args(args, input_invoice);
      send_request(td_api::make_object<td_api::getPaymentForm>(input_invoice, get_theme_parameters()));
    } else if (op == "voi") {
      InputInvoice input_invoice;
      bool allow_save;
      get_args(args, input_invoice, allow_save);
      send_request(td_api::make_object<td_api::validateOrderInfo>(input_invoice, nullptr, allow_save));
    } else if (op == "spfs") {
      InputInvoice input_invoice;
      int64 tip_amount;
      int64 payment_form_id;
      string order_info_id;
      string shipping_option_id;
      string saved_credentials_id;
      get_args(args, input_invoice, tip_amount, payment_form_id, order_info_id, shipping_option_id,
               saved_credentials_id);
      send_request(td_api::make_object<td_api::sendPaymentForm>(
          input_invoice, payment_form_id, order_info_id, shipping_option_id,
          td_api::make_object<td_api::inputCredentialsSaved>(saved_credentials_id), tip_amount));
    } else if (op == "spfn") {
      InputInvoice input_invoice;
      int64 tip_amount;
      int64 payment_form_id;
      string order_info_id;
      string shipping_option_id;
      string data;
      get_args(args, input_invoice, tip_amount, payment_form_id, order_info_id, shipping_option_id, data);
      send_request(td_api::make_object<td_api::sendPaymentForm>(
          input_invoice, payment_form_id, order_info_id, shipping_option_id,
          td_api::make_object<td_api::inputCredentialsNew>(data, true), tip_amount));
    } else if (op == "gpre") {
      ChatId chat_id;
      MessageId message_id;
      get_args(args, chat_id, message_id);
      send_request(td_api::make_object<td_api::getPaymentReceipt>(chat_id, message_id));
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
      get_args(args, setting, allow, ids);
      send_request(td_api::make_object<td_api::setUserPrivacySettingRules>(get_user_privacy_setting(setting),
                                                                           get_user_privacy_setting_rules(allow, ids)));
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
        send_request(td_api::make_object<td_api::searchContacts>("", as_limit(args)));
      }
    } else if (op == "AddContact") {
      UserId user_id;
      string first_name;
      string last_name;
      get_args(args, user_id, first_name, last_name);
      send_request(td_api::make_object<td_api::addContact>(
          td_api::make_object<td_api::contact>(string(), first_name, last_name, string(), user_id), false));
    } else if (op == "subpn") {
      string phone_number;
      get_args(args, phone_number);
      send_request(td_api::make_object<td_api::searchUserByPhoneNumber>(phone_number));
    } else if (op == "spn") {
      UserId user_id;
      get_args(args, user_id);
      send_request(td_api::make_object<td_api::sharePhoneNumber>(user_id));
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

    if (op == "gc" || op == "gca" || begins_with(op, "gc-")) {
      send_request(td_api::make_object<td_api::getChats>(as_chat_list(op), as_limit(args, 10000)));
    } else if (op == "lc" || op == "lca" || begins_with(op, "lc-")) {
      send_request(td_api::make_object<td_api::loadChats>(as_chat_list(op), as_limit(args, 10000)));
    } else if (op == "gctest") {
      send_request(td_api::make_object<td_api::getChats>(nullptr, 1));
      send_request(td_api::make_object<td_api::getChats>(nullptr, 10));
      send_request(td_api::make_object<td_api::getChats>(nullptr, 5));
    } else if (op == "gcc" || op == "GetCommonChats") {
      UserId user_id;
      ChatId offset_chat_id;
      string limit;
      get_args(args, user_id, offset_chat_id, limit);
      send_request(td_api::make_object<td_api::getGroupsInCommon>(user_id, offset_chat_id, as_limit(limit, 100)));
    } else if (op == "gh" || op == "GetHistory" || op == "ghl" || op == "gmth") {
      ChatId chat_id;
      MessageId thread_message_id;
      MessageId from_message_id;
      int32 offset;
      string limit;
      if (op == "gmth") {
        get_args(args, thread_message_id, args);
      }
      get_args(args, chat_id, from_message_id, offset, limit);
      if (op == "gmth") {
        send_request(td_api::make_object<td_api::getMessageThreadHistory>(chat_id, thread_message_id, from_message_id,
                                                                          offset, as_limit(limit)));
      } else {
        send_request(td_api::make_object<td_api::getChatHistory>(chat_id, from_message_id, offset, as_limit(limit),
                                                                 op == "ghl"));
      }
    } else if (op == "gcsm") {
      ChatId chat_id;
      get_args(args, chat_id);
      send_request(td_api::make_object<td_api::getChatScheduledMessages>(chat_id));
    } else if (op == "gmar") {
      ChatId chat_id;
      MessageId message_id;
      get_args(args, chat_id, message_id);
      send_request(td_api::make_object<td_api::getMessageAvailableReactions>(chat_id, message_id));
    } else if (op == "react") {
      ChatId chat_id;
      MessageId message_id;
      string reaction;
      bool is_big;
      get_args(args, chat_id, message_id, reaction, is_big);
      send_request(td_api::make_object<td_api::setMessageReaction>(chat_id, message_id, reaction, is_big));
    } else if (op == "gmars") {
      ChatId chat_id;
      MessageId message_id;
      string reaction;
      string offset;
      string limit;
      get_args(args, chat_id, message_id, reaction, offset, limit);
      send_request(td_api::make_object<td_api::getMessageAddedReactions>(chat_id, message_id, reaction, offset,
                                                                         as_limit(limit)));
    } else if (op == "gmpf") {
      ChatId chat_id;
      MessageId message_id;
      string offset;
      string limit;
      get_args(args, chat_id, message_id, offset, limit);
      send_request(td_api::make_object<td_api::getMessagePublicForwards>(chat_id, message_id, offset, as_limit(limit)));
    } else if (op == "ghf") {
      get_history_chat_id_ = as_chat_id(args);
      send_request(td_api::make_object<td_api::getChatHistory>(get_history_chat_id_, std::numeric_limits<int64>::max(),
                                                               0, 100, false));
    } else if (op == "replies") {
      ChatId chat_id;
      string message_thread_id;
      get_args(args, chat_id, message_thread_id);
      send_request(td_api::make_object<td_api::searchChatMessages>(chat_id, "", nullptr, 0, 0, 100, nullptr,
                                                                   as_message_thread_id(message_thread_id)));
    } else if (op == "spvf") {
      search_chat_id_ = as_chat_id(args);
      send_request(td_api::make_object<td_api::searchChatMessages>(search_chat_id_, "", nullptr, 0, 0, 100,
                                                                   as_search_messages_filter("pvi"), 0));
    } else if (op == "Search" || op == "SearchA" || op == "SearchM") {
      string query;
      string limit;
      string filter;
      int32 from_date;
      get_args(args, query, limit, filter, from_date);
      td_api::object_ptr<td_api::ChatList> chat_list;
      if (op == "SearchA") {
        chat_list = td_api::make_object<td_api::chatListArchive>();
      }
      if (op == "SearchM") {
        chat_list = td_api::make_object<td_api::chatListMain>();
      }
      send_request(td_api::make_object<td_api::searchMessages>(std::move(chat_list), query, from_date, 2147483647, 0,
                                                               as_limit(limit), as_search_messages_filter(filter), 1,
                                                               2147483647));
    } else if (op == "SCM") {
      ChatId chat_id;
      SearchQuery query;
      get_args(args, chat_id, query);
      send_request(td_api::make_object<td_api::searchChatMessages>(chat_id, query.query, nullptr, 0, 0, query.limit,
                                                                   nullptr, 0));
    } else if (op == "SMME") {
      ChatId chat_id;
      string limit;
      get_args(args, chat_id, limit);
      send_request(td_api::make_object<td_api::searchChatMessages>(
          chat_id, "", td_api::make_object<td_api::messageSenderUser>(my_id_), 0, 0, as_limit(limit), nullptr, 0));
    } else if (op == "SMU" || op == "SMC") {
      ChatId chat_id;
      string sender_id;
      MessageId from_message_id;
      string limit;
      get_args(args, chat_id, sender_id, from_message_id, limit);
      send_request(td_api::make_object<td_api::searchChatMessages>(chat_id, "", as_message_sender(sender_id),
                                                                   from_message_id, 0, as_limit(limit), nullptr, 0));
    } else if (op == "SM") {
      ChatId chat_id;
      string filter;
      string limit;
      MessageId offset_message_id;
      int32 offset;
      get_args(args, chat_id, filter, limit, offset_message_id, offset);
      send_request(td_api::make_object<td_api::searchChatMessages>(
          chat_id, "", nullptr, offset_message_id, offset, as_limit(limit), as_search_messages_filter(filter), 0));
    } else if (op == "SC") {
      string limit;
      MessageId offset_message_id;
      bool only_missed;
      get_args(args, limit, offset_message_id, only_missed);
      send_request(td_api::make_object<td_api::searchCallMessages>(offset_message_id, as_limit(limit), only_missed));
    } else if (op == "sodm") {
      SearchQuery query;
      get_args(args, query);
      send_request(td_api::make_object<td_api::searchOutgoingDocumentMessages>(query.query, query.limit));
    } else if (op == "DeleteAllCallMessages") {
      bool revoke = as_bool(args);
      send_request(td_api::make_object<td_api::deleteAllCallMessages>(revoke));
    } else if (op == "SCRLM") {
      ChatId chat_id;
      string limit;
      get_args(args, chat_id, limit);
      send_request(td_api::make_object<td_api::searchChatRecentLocationMessages>(chat_id, as_limit(limit)));
    } else if (op == "gcmca") {
      ChatId chat_id;
      string filter;
      MessageId from_message_id;
      get_args(args, chat_id, filter, from_message_id);
      send_request(td_api::make_object<td_api::getChatMessageCalendar>(chat_id, as_search_messages_filter(filter),
                                                                       from_message_id));
    } else if (op == "SearchAudio" || op == "SearchDocument" || op == "SearchPhoto" || op == "SearchChatPhoto") {
      ChatId chat_id;
      MessageId offset_message_id;
      SearchQuery query;
      get_args(args, chat_id, offset_message_id, query);
      send_request(td_api::make_object<td_api::searchChatMessages>(chat_id, query.query, nullptr, offset_message_id, 0,
                                                                   query.limit, as_search_messages_filter(op), 0));
    } else if (op == "gcmbd") {
      ChatId chat_id;
      int32 date;
      get_args(args, chat_id, date);
      send_request(td_api::make_object<td_api::getChatMessageByDate>(chat_id, date));
    } else if (op == "gcsmp") {
      ChatId chat_id;
      string filter;
      MessageId from_message_id;
      string limit;
      get_args(args, chat_id, filter, from_message_id, limit);
      send_request(td_api::make_object<td_api::getChatSparseMessagePositions>(
          chat_id, as_search_messages_filter(filter), from_message_id, as_limit(limit)));
    } else if (op == "gcmc") {
      ChatId chat_id;
      string filter;
      bool return_local;
      get_args(args, chat_id, filter, return_local);
      send_request(
          td_api::make_object<td_api::getChatMessageCount>(chat_id, as_search_messages_filter(filter), return_local));
    } else if (op == "gup" || op == "gupp") {
      UserId user_id;
      int32 offset;
      string limit;
      get_args(args, user_id, offset, limit);
      send_request(td_api::make_object<td_api::getUserProfilePhotos>(user_id, offset, as_limit(limit)));
    } else if (op == "dcrm") {
      ChatId chat_id;
      MessageId message_id;
      get_args(args, chat_id, message_id);
      send_request(td_api::make_object<td_api::deleteChatReplyMarkup>(chat_id, message_id));
    } else if (op == "glti") {
      send_request(td_api::make_object<td_api::getLocalizationTargetInfo>(as_bool(args)));
    } else if (op == "glpi") {
      send_request(td_api::make_object<td_api::getLanguagePackInfo>(args));
    } else if (op == "glps") {
      string language_code;
      string keys;
      get_args(args, language_code, keys);
      send_request(td_api::make_object<td_api::getLanguagePackStrings>(language_code, autosplit_str(keys)));
    } else if (op == "glpss") {
      string language_database_path;
      string language_pack;
      string language_code;
      string key;
      get_args(args, language_database_path, language_pack, language_code, key);
      send_request(td_api::make_object<td_api::getLanguagePackString>(language_database_path, language_pack,
                                                                      language_code, key));
    } else if (op == "synclp") {
      const string &language_code = args;
      send_request(td_api::make_object<td_api::synchronizeLanguagePack>(language_code));
    } else if (op == "acslp") {
      const string &language_code = args;
      send_request(td_api::make_object<td_api::addCustomServerLanguagePack>(language_code));
    } else if (op == "sclp") {
      string language_code;
      string name;
      string native_name;
      string key;
      get_args(args, language_code, name, native_name, key);
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
      get_args(args, language_code, name, native_name);
      send_request(td_api::make_object<td_api::editCustomLanguagePackInfo>(
          as_language_pack_info(language_code, name, native_name)));
    } else if (op == "sclpsv" || op == "sclpsp" || op == "sclpsd") {
      string language_code;
      string key;
      string value;
      get_args(args, language_code, key, value);
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
    } else if (op == "on" || op == "off") {
      send_request(td_api::make_object<td_api::setOption>("online",
                                                          td_api::make_object<td_api::optionValueBoolean>(op == "on")));
    } else if (op == "go") {
      send_request(td_api::make_object<td_api::getOption>(args));
    } else if (op == "sob") {
      string name;
      bool value;
      get_args(args, name, value);
      send_request(
          td_api::make_object<td_api::setOption>(name, td_api::make_object<td_api::optionValueBoolean>(value)));
    } else if (op == "soe") {
      send_request(td_api::make_object<td_api::setOption>(args, td_api::make_object<td_api::optionValueEmpty>()));
    } else if (op == "soi") {
      string name;
      int64 value;
      get_args(args, name, value);
      send_request(
          td_api::make_object<td_api::setOption>(name, td_api::make_object<td_api::optionValueInteger>(value)));
    } else if (op == "sos") {
      string name;
      string value;
      get_args(args, name, value);
      send_request(td_api::make_object<td_api::setOption>(name, td_api::make_object<td_api::optionValueString>(value)));
    } else if (op == "me") {
      send_request(td_api::make_object<td_api::getMe>());
    } else if (op == "sattl") {
      int32 days;
      get_args(args, days);
      send_request(td_api::make_object<td_api::setAccountTtl>(td_api::make_object<td_api::accountTtl>(days)));
    } else if (op == "gattl") {
      send_request(td_api::make_object<td_api::getAccountTtl>());
    } else if (op == "GetActiveSessions" || op == "devices" || op == "sessions") {
      send_request(td_api::make_object<td_api::getActiveSessions>());
    } else if (op == "TerminateSession") {
      int64 session_id;
      get_args(args, session_id);
      send_request(td_api::make_object<td_api::terminateSession>(session_id));
    } else if (op == "TerminateAllOtherSessions") {
      send_request(td_api::make_object<td_api::terminateAllOtherSessions>());
    } else if (op == "tscac") {
      int64 session_id;
      bool can_accept_calls;
      get_args(args, session_id, can_accept_calls);
      send_request(td_api::make_object<td_api::toggleSessionCanAcceptCalls>(session_id, can_accept_calls));
    } else if (op == "tscasc") {
      int64 session_id;
      bool can_accept_secret_chats;
      get_args(args, session_id, can_accept_secret_chats);
      send_request(td_api::make_object<td_api::toggleSessionCanAcceptSecretChats>(session_id, can_accept_secret_chats));
    } else if (op == "sist") {
      int32 inactive_session_ttl_days;
      get_args(args, inactive_session_ttl_days);
      send_request(td_api::make_object<td_api::setInactiveSessionTtl>(inactive_session_ttl_days));
    } else if (op == "gcw") {
      send_request(td_api::make_object<td_api::getConnectedWebsites>());
    } else if (op == "dw") {
      int64 website_id;
      get_args(args, website_id);
      send_request(td_api::make_object<td_api::disconnectWebsite>(website_id));
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
      send_get_background_url(get_gradient_pattern_background(0, 0, 0, false, false));
      send_get_background_url(get_gradient_pattern_background(0, 0, 0, true, false));
      send_get_background_url(get_gradient_pattern_background(0xFFFFFF, 0, 100, false, true));
      send_get_background_url(get_gradient_pattern_background(0xFFFFFF, 0, 100, true, true));
      send_get_background_url(get_gradient_pattern_background(0xABCDEF, 0xFEDCBA, 49, false, true));
      send_get_background_url(get_gradient_pattern_background(0, 0x1000000, 49, false, true));
      send_get_background_url(get_freeform_gradient_pattern_background({0xABCDEF, 0xFEDCBA}, 49, false, true));
      send_get_background_url(get_freeform_gradient_pattern_background({0xABCDEF, 0x111111, 0x222222}, 49, true, true));
      send_get_background_url(
          get_freeform_gradient_pattern_background({0xABCDEF, 0xFEDCBA, 0x111111, 0x222222}, 49, false, true));
      send_get_background_url(get_solid_background(-1));
      send_get_background_url(get_solid_background(0xABCDEF));
      send_get_background_url(get_solid_background(0x1000000));
      send_get_background_url(get_gradient_background(0xABCDEF, 0xFEDCBA));
      send_get_background_url(get_gradient_background(0, 0));
      send_get_background_url(get_gradient_background(-1, -1));
      send_get_background_url(get_freeform_gradient_background({0xFEDCBA, 0x222222}));
      send_get_background_url(get_freeform_gradient_background({0xFEDCBA, 0x111111, 0x222222}));
      send_get_background_url(get_freeform_gradient_background({0xABCDEF, 0xFEDCBA, 0x111111, 0x222222}));
    } else {
      op_not_found_count++;
    }

    if (op == "sbg") {
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
          get_gradient_pattern_background(0xABCDEF, 0xFE, 51, op == "sbggpd", false), op == "sbggpd"));
    } else if (op == "sbgs" || op == "sbgsd") {
      int32 color;
      get_args(args, color);
      send_request(td_api::make_object<td_api::setBackground>(nullptr, get_solid_background(color), op == "sbgsd"));
    } else if (op == "sbgg" || op == "sbggd") {
      int32 top_color;
      int32 bottom_color;
      get_args(args, top_color, bottom_color);
      auto background_type = get_gradient_background(top_color, bottom_color);
      send_request(td_api::make_object<td_api::setBackground>(nullptr, std::move(background_type), op == "sbggd"));
    } else if (op == "sbgfg" || op == "sbgfgd") {
      auto background_type = get_freeform_gradient_background(to_integers<int32>(args));
      send_request(td_api::make_object<td_api::setBackground>(nullptr, std::move(background_type), op == "sbgfgd"));
    } else if (op == "sbgfid" || op == "sbgfidd") {
      int64 background_id;
      get_args(args, background_id);
      send_request(td_api::make_object<td_api::setBackground>(
          td_api::make_object<td_api::inputBackgroundRemote>(background_id), nullptr, op == "sbgfidd"));
    } else if (op == "sbgwid" || op == "sbgwidd") {
      int64 background_id;
      get_args(args, background_id);
      send_request(td_api::make_object<td_api::setBackground>(
          td_api::make_object<td_api::inputBackgroundRemote>(background_id),
          td_api::make_object<td_api::backgroundTypeWallpaper>(true, true), op == "sbgwidd"));
    } else if (op == "sbgpid" || op == "sbgpidd") {
      int64 background_id;
      get_args(args, background_id);
      send_request(td_api::make_object<td_api::setBackground>(
          td_api::make_object<td_api::inputBackgroundRemote>(background_id),
          get_solid_pattern_background(0xabcdef, 49, true), op == "sbgpidd"));
    } else if (op == "rbg") {
      int64 background_id;
      get_args(args, background_id);
      send_request(td_api::make_object<td_api::removeBackground>(background_id));
    } else if (op == "rbgs") {
      send_request(td_api::make_object<td_api::resetBackgrounds>());
    } else if (op == "gcos") {
      send_request(td_api::make_object<td_api::getCountries>());
    } else if (op == "gcoc") {
      send_request(td_api::make_object<td_api::getCountryCode>());
    } else if (op == "gpni") {
      send_request(td_api::make_object<td_api::getPhoneNumberInfo>(args));
    } else if (op == "gpnis") {
      execute(td_api::make_object<td_api::getPhoneNumberInfoSync>(rand_bool() ? "en" : "", args));
    } else if (op == "gadl") {
      send_request(td_api::make_object<td_api::getApplicationDownloadLink>());
    } else if (op == "gprl") {
      auto limit_type = td_api::make_object<td_api::premiumLimitTypeChatFilterCount>();
      send_request(td_api::make_object<td_api::getPremiumLimit>(std::move(limit_type)));
    } else if (op == "gprf") {
      auto source = td_api::make_object<td_api::premiumSourceLimitExceeded>(
          td_api::make_object<td_api::premiumLimitTypeChatFilterCount>());
      send_request(td_api::make_object<td_api::getPremiumFeatures>(std::move(source)));
    } else if (op == "gprst") {
      send_request(td_api::make_object<td_api::getPremiumStickers>());
    } else if (op == "vprf") {
      auto feature = td_api::make_object<td_api::premiumFeatureProfileBadge>();
      send_request(td_api::make_object<td_api::viewPremiumFeature>(std::move(feature)));
    } else if (op == "cprsb") {
      send_request(td_api::make_object<td_api::clickPremiumSubscriptionButton>());
    } else if (op == "gprs") {
      send_request(td_api::make_object<td_api::getPremiumState>());
    } else if (op == "cppr") {
      send_request(td_api::make_object<td_api::canPurchasePremium>());
    } else if (op == "atos") {
      send_request(td_api::make_object<td_api::acceptTermsOfService>(args));
    } else if (op == "gdli") {
      send_request(td_api::make_object<td_api::getDeepLinkInfo>(args));
    } else if (op == "tme") {
      send_request(td_api::make_object<td_api::getRecentlyVisitedTMeUrls>(args));
    } else if (op == "gbms") {
      int32 offset;
      string limit;
      get_args(args, offset, limit);
      send_request(td_api::make_object<td_api::getBlockedMessageSenders>(offset, as_limit(limit)));
    } else if (op == "gu") {
      UserId user_id;
      get_args(args, user_id);
      send_request(td_api::make_object<td_api::getUser>(user_id));
    } else if (op == "gsu") {
      send_request(td_api::make_object<td_api::getSupportUser>());
    } else if (op == "gs") {
      SearchQuery query;
      get_args(args, query);
      send_request(td_api::make_object<td_api::getStickers>(query.query, query.limit));
    } else if (op == "sst") {
      SearchQuery query;
      get_args(args, query);
      send_request(td_api::make_object<td_api::searchStickers>(query.query, query.limit));
    } else if (op == "gss") {
      int64 sticker_set_id;
      get_args(args, sticker_set_id);
      send_request(td_api::make_object<td_api::getStickerSet>(sticker_set_id));
    } else if (op == "giss") {
      send_request(td_api::make_object<td_api::getInstalledStickerSets>(as_bool(args)));
    } else if (op == "gass") {
      bool is_masks;
      int64 offset_sticker_set_id;
      string limit;
      get_args(args, is_masks, offset_sticker_set_id, limit);
      send_request(
          td_api::make_object<td_api::getArchivedStickerSets>(is_masks, offset_sticker_set_id, as_limit(limit)));
    } else if (op == "gtss") {
      int32 offset;
      string limit;
      get_args(args, offset, limit);
      send_request(td_api::make_object<td_api::getTrendingStickerSets>(offset, as_limit(limit, 1000)));
    } else if (op == "gatss") {
      FileId file_id;
      get_args(args, file_id);
      send_request(td_api::make_object<td_api::getAttachedStickerSets>(file_id));
    } else if (op == "storage") {
      int32 chat_limit;
      get_args(args, chat_limit);
      send_request(td_api::make_object<td_api::getStorageStatistics>(chat_limit));
    } else if (op == "storage_fast") {
      send_request(td_api::make_object<td_api::getStorageStatisticsFast>());
    } else if (op == "database") {
      send_request(td_api::make_object<td_api::getDatabaseStatistics>());
    } else if (op == "optimize_storage" || op == "optimize_storage_all") {
      string chat_ids;
      string exclude_chat_ids;
      int32 chat_ids_limit;
      get_args(args, chat_ids, exclude_chat_ids, chat_ids_limit);
      send_request(td_api::make_object<td_api::optimizeStorage>(
          10000000, -1, -1, 0, std::vector<td_api::object_ptr<td_api::FileType>>(), as_chat_ids(chat_ids),
          as_chat_ids(exclude_chat_ids), op == "optimize_storage", chat_ids_limit));
    } else if (op == "clean_storage_default") {
      send_request(td_api::make_object<td_api::optimizeStorage>());
    } else if (op == "clean_photos") {
      std::vector<td_api::object_ptr<td_api::FileType>> types;
      types.emplace_back(td_api::make_object<td_api::fileTypePhoto>());
      send_request(td_api::make_object<td_api::optimizeStorage>(0, 0, 0, 0, std::move(types), as_chat_ids(""),
                                                                as_chat_ids(""), true, 20));
    } else if (op == "clean_storage") {
      std::vector<td_api::object_ptr<td_api::FileType>> types;
      types.emplace_back(td_api::make_object<td_api::fileTypeThumbnail>());
      types.emplace_back(td_api::make_object<td_api::fileTypeProfilePhoto>());
      types.emplace_back(td_api::make_object<td_api::fileTypePhoto>());
      types.emplace_back(td_api::make_object<td_api::fileTypeVoiceNote>());
      types.emplace_back(td_api::make_object<td_api::fileTypeVideo>());
      types.emplace_back(td_api::make_object<td_api::fileTypeDocument>());
      types.emplace_back(td_api::make_object<td_api::fileTypeSecret>());
      types.emplace_back(td_api::make_object<td_api::fileTypeUnknown>());
      types.emplace_back(td_api::make_object<td_api::fileTypeSticker>());
      types.emplace_back(td_api::make_object<td_api::fileTypeAudio>());
      types.emplace_back(td_api::make_object<td_api::fileTypeAnimation>());
      types.emplace_back(td_api::make_object<td_api::fileTypeVideoNote>());
      types.emplace_back(td_api::make_object<td_api::fileTypeSecure>());
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
      int32 sent_bytes;
      int32 received_bytes;
      string duration;
      string network_type;
      get_args(args, sent_bytes, received_bytes, duration, network_type);
      send_request(
          td_api::make_object<td_api::addNetworkStatistics>(td_api::make_object<td_api::networkStatisticsEntryCall>(
              get_network_type(network_type), sent_bytes, received_bytes, to_double(duration))));
    } else if (op == "ans") {
      int32 sent_bytes;
      int32 received_bytes;
      string network_type;
      get_args(args, sent_bytes, received_bytes, network_type);
      send_request(
          td_api::make_object<td_api::addNetworkStatistics>(td_api::make_object<td_api::networkStatisticsEntryFile>(
              td_api::make_object<td_api::fileTypeDocument>(), get_network_type(network_type), sent_bytes,
              received_bytes)));
    } else if (op == "gtc") {
      send_request(td_api::make_object<td_api::getTopChats>(get_top_chat_category(args), 50));
    } else if (op == "rtc") {
      ChatId chat_id;
      string category;
      get_args(args, chat_id, category);
      send_request(td_api::make_object<td_api::removeTopChat>(get_top_chat_category(category), chat_id));
    } else if (op == "gsssn") {
      const string &title = args;
      send_request(td_api::make_object<td_api::getSuggestedStickerSetName>(title));
    } else if (op == "cssn") {
      const string &name = args;
      send_request(td_api::make_object<td_api::checkStickerSetName>(name));
    } else if (op == "usf" || op == "usfa" || op == "usfv" || op == "usfm") {
      send_request(td_api::make_object<td_api::uploadStickerFile>(
          -1, td_api::make_object<td_api::inputSticker>(as_input_file(args), "😀", as_sticker_type(op))));
    } else if (op == "cnss" || op == "cnssa" || op == "cnssv" || op == "cnssm") {
      string title;
      string name;
      string stickers;
      get_args(args, title, name, stickers);
      auto input_stickers =
          transform(autosplit(stickers), [op](Slice sticker) -> td_api::object_ptr<td_api::inputSticker> {
            return td_api::make_object<td_api::inputSticker>(as_input_file(sticker), "😀", as_sticker_type(op));
          });
      send_request(
          td_api::make_object<td_api::createNewStickerSet>(my_id_, title, name, std::move(input_stickers), "tg_cli"));
    } else if (op == "sss") {
      send_request(td_api::make_object<td_api::searchStickerSet>(args));
    } else if (op == "siss") {
      send_request(td_api::make_object<td_api::searchInstalledStickerSets>(false, args, 2));
    } else if (op == "ssss") {
      send_request(td_api::make_object<td_api::searchStickerSets>(args));
    } else if (op == "css") {
      int64 set_id;
      bool is_installed;
      bool is_archived;
      get_args(args, set_id, is_installed, is_archived);
      send_request(td_api::make_object<td_api::changeStickerSet>(set_id, is_installed, is_archived));
    } else if (op == "vtss") {
      send_request(td_api::make_object<td_api::viewTrendingStickerSets>(to_integers<int64>(args)));
    } else if (op == "riss") {
      bool is_masks;
      string new_order;
      get_args(args, is_masks, new_order);
      send_request(td_api::make_object<td_api::reorderInstalledStickerSets>(is_masks, to_integers<int64>(new_order)));
    } else if (op == "grs") {
      send_request(td_api::make_object<td_api::getRecentStickers>(as_bool(args)));
    } else if (op == "ars") {
      bool is_attached;
      string sticker_id;
      get_args(args, is_attached, sticker_id);
      send_request(td_api::make_object<td_api::addRecentSticker>(is_attached, as_input_file_id(sticker_id)));
    } else if (op == "rrs") {
      bool is_attached;
      string sticker_id;
      get_args(args, is_attached, sticker_id);
      send_request(td_api::make_object<td_api::removeRecentSticker>(is_attached, as_input_file_id(sticker_id)));
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
    } else if (op == "gae") {
      send_request(td_api::make_object<td_api::getAnimatedEmoji>(args));
    } else if (op == "gaae") {
      send_request(td_api::make_object<td_api::getAllAnimatedEmojis>());
    } else if (op == "gesu") {
      send_request(td_api::make_object<td_api::getEmojiSuggestionsUrl>(args));
    } else if (op == "gsan") {
      send_request(td_api::make_object<td_api::getSavedAnimations>());
    } else if (op == "asan") {
      send_request(td_api::make_object<td_api::addSavedAnimation>(as_input_file_id(args)));
    } else if (op == "rsan") {
      send_request(td_api::make_object<td_api::removeSavedAnimation>(as_input_file_id(args)));
    } else {
      op_not_found_count++;
    }

    if (op == "guf") {
      UserId user_id;
      get_args(args, user_id);
      send_request(td_api::make_object<td_api::getUserFullInfo>(user_id));
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
      ChatId chat_id;
      string filter;
      SearchQuery query;
      get_args(args, chat_id, filter, query);
      send_request(td_api::make_object<td_api::searchChatMembers>(chat_id, query.query, query.limit,
                                                                  get_chat_members_filter(filter)));
    } else if (op == "gcm") {
      ChatId chat_id;
      string member_id;
      get_args(args, chat_id, member_id);
      send_request(td_api::make_object<td_api::getChatMember>(chat_id, as_message_sender(member_id)));
    } else if (op == "GetChatAdministrators") {
      ChatId chat_id;
      get_args(args, chat_id);
      send_request(td_api::make_object<td_api::getChatAdministrators>(chat_id));
    } else if (op == "GetSupergroupAdministrators" || op == "GetSupergroupBanned" || op == "GetSupergroupBots" ||
               op == "GetSupergroupContacts" || op == "GetSupergroupMembers" || op == "GetSupergroupRestricted" ||
               op == "SearchSupergroupMembers" || op == "SearchSupergroupMentions") {
      string supergroup_id;
      string message_thread_id;
      int32 offset;
      SearchQuery query;
      if (op == "SearchSupergroupMentions") {
        get_args(args, message_thread_id, args);
      }
      get_args(args, supergroup_id, offset, query);
      send_request(td_api::make_object<td_api::getSupergroupMembers>(
          as_supergroup_id(supergroup_id), get_supergroup_members_filter(op, query.query, message_thread_id), offset,
          query.limit));
    } else if (op == "gdialog" || op == "gd") {
      ChatId chat_id;
      get_args(args, chat_id);
      send_request(td_api::make_object<td_api::getChat>(chat_id));
    } else if (op == "open") {
      ChatId chat_id;
      get_args(args, chat_id);
      send_request(td_api::make_object<td_api::openChat>(chat_id));
      opened_chat_id_ = chat_id;
    } else if (op == "close") {
      ChatId chat_id;
      get_args(args, chat_id);
      send_request(td_api::make_object<td_api::closeChat>(chat_id));
    } else if (op == "gm") {
      ChatId chat_id;
      MessageId message_id;
      get_args(args, chat_id, message_id);
      send_request(td_api::make_object<td_api::getMessage>(chat_id, message_id));
    } else if (op == "gmf") {
      ChatId chat_id;
      int64 from_message_id;
      int64 to_message_id;
      get_args(args, chat_id, from_message_id, to_message_id);
      for (auto message_id = from_message_id; message_id <= to_message_id; message_id++) {
        send_request(td_api::make_object<td_api::getMessage>(chat_id, message_id << 20));
      }
    } else if (op == "gml") {
      ChatId chat_id;
      MessageId message_id;
      get_args(args, chat_id, message_id);
      send_request(td_api::make_object<td_api::getMessageLocally>(chat_id, message_id));
    } else if (op == "grm") {
      ChatId chat_id;
      MessageId message_id;
      get_args(args, chat_id, message_id);
      send_request(td_api::make_object<td_api::getRepliedMessage>(chat_id, message_id));
    } else if (op == "gmt") {
      ChatId chat_id;
      MessageId message_id;
      get_args(args, chat_id, message_id);
      send_request(td_api::make_object<td_api::getMessageThread>(chat_id, message_id));
    } else if (op == "gmv") {
      ChatId chat_id;
      MessageId message_id;
      get_args(args, chat_id, message_id);
      send_request(td_api::make_object<td_api::getMessageViewers>(chat_id, message_id));
    } else if (op == "gcpm") {
      ChatId chat_id;
      get_args(args, chat_id);
      send_request(td_api::make_object<td_api::getChatPinnedMessage>(chat_id));
    } else if (op == "gms") {
      ChatId chat_id;
      string message_ids;
      get_args(args, chat_id, message_ids);
      send_request(td_api::make_object<td_api::getMessages>(chat_id, as_message_ids(message_ids)));
    } else if (op == "gsm") {
      ChatId chat_id;
      get_args(args, chat_id);
      send_request(td_api::make_object<td_api::getChatSponsoredMessage>(chat_id));
    } else if (op == "gmlink") {
      ChatId chat_id;
      MessageId message_id;
      int32 media_timestamp;
      bool for_album;
      bool for_comment;
      get_args(args, chat_id, message_id, media_timestamp, for_album, for_comment);
      send_request(
          td_api::make_object<td_api::getMessageLink>(chat_id, message_id, media_timestamp, for_album, for_comment));
    } else if (op == "gmec") {
      ChatId chat_id;
      MessageId message_id;
      bool for_album;
      get_args(args, chat_id, message_id, for_album);
      send_request(td_api::make_object<td_api::getMessageEmbeddingCode>(chat_id, message_id, for_album));
    } else if (op == "gmli") {
      send_request(td_api::make_object<td_api::getMessageLinkInfo>(args));
    } else if (op == "tt") {
      string text;
      string from_language_code;
      string to_language_code;
      get_args(args, text, from_language_code, to_language_code);
      send_request(td_api::make_object<td_api::translateText>(text, from_language_code, to_language_code));
    } else if (op == "rs") {
      ChatId chat_id;
      MessageId message_id;
      get_args(args, chat_id, message_id);
      send_request(td_api::make_object<td_api::recognizeSpeech>(chat_id, message_id));
    } else if (op == "rsr") {
      ChatId chat_id;
      MessageId message_id;
      bool is_good;
      get_args(args, chat_id, message_id, is_good);
      send_request(td_api::make_object<td_api::rateSpeechRecognition>(chat_id, message_id, is_good));
    } else if (op == "gf" || op == "GetFile") {
      FileId file_id;
      get_args(args, file_id);
      send_request(td_api::make_object<td_api::getFile>(file_id));
    } else if (op == "gfdps") {
      FileId file_id;
      int64 offset;
      get_args(args, file_id, offset);
      send_request(td_api::make_object<td_api::getFileDownloadedPrefixSize>(file_id, offset));
    } else if (op == "rfp") {
      FileId file_id;
      int64 offset;
      int64 count;
      get_args(args, file_id, offset, count);
      send_request(td_api::make_object<td_api::readFilePart>(file_id, offset, count));
    } else if (op == "grf") {
      send_request(td_api::make_object<td_api::getRemoteFile>(args, nullptr));
    } else if (op == "gmtf") {
      string latitude;
      string longitude;
      int32 zoom;
      int32 width;
      int32 height;
      int32 scale;
      ChatId chat_id;
      get_args(args, latitude, longitude, zoom, width, height, scale, chat_id);
      send_request(td_api::make_object<td_api::getMapThumbnailFile>(as_location(latitude, longitude, string()), zoom,
                                                                    width, height, scale, chat_id));
    } else if (op == "df" || op == "DownloadFile" || op == "dff" || op == "dfs") {
      FileId file_id;
      int64 offset;
      int64 limit;
      int32 priority;
      get_args(args, file_id, offset, limit, priority);
      if (priority <= 0) {
        priority = 1;
      }
      int32 max_file_id = file_id.file_id;
      int32 min_file_id = (op == "dff" ? 1 : max_file_id);
      for (int32 i = min_file_id; i <= max_file_id; i++) {
        send_request(td_api::make_object<td_api::downloadFile>(i, priority, offset, limit, op == "dfs"));
      }
    } else if (op == "cdf") {
      FileId file_id;
      get_args(args, file_id);
      send_request(td_api::make_object<td_api::cancelDownloadFile>(file_id, false));
    } else if (op == "gsfn") {
      FileId file_id;
      string directory_name;
      get_args(args, file_id, directory_name);
      send_request(td_api::make_object<td_api::getSuggestedFileName>(file_id, directory_name));
    } else if (op == "uf" || op == "ufs" || op == "ufse") {
      string file_path;
      int32 priority;
      get_args(args, file_path, priority);
      if (priority <= 0) {
        priority = 1;
      }
      td_api::object_ptr<td_api::FileType> type = td_api::make_object<td_api::fileTypePhoto>();
      if (op == "ufs") {
        type = td_api::make_object<td_api::fileTypeSecret>();
      }
      if (op == "ufse") {
        type = td_api::make_object<td_api::fileTypeSecure>();
      }
      send_request(td_api::make_object<td_api::uploadFile>(as_input_file(file_path), std::move(type), priority));
    } else if (op == "ufg") {
      string file_path;
      string conversion;
      get_args(args, file_path, conversion);
      send_request(td_api::make_object<td_api::uploadFile>(as_generated_file(file_path, conversion),
                                                           td_api::make_object<td_api::fileTypePhoto>(), 1));
    } else if (op == "cuf") {
      FileId file_id;
      get_args(args, file_id);
      send_request(td_api::make_object<td_api::cancelUploadFile>(file_id));
    } else if (op == "delf" || op == "DeleteFile") {
      FileId file_id;
      get_args(args, file_id);
      send_request(td_api::make_object<td_api::deleteFile>(file_id));
    } else if (op == "aftd") {
      FileId file_id;
      ChatId chat_id;
      MessageId message_id;
      int32 priority;
      get_args(args, file_id, chat_id, message_id, priority);
      send_request(td_api::make_object<td_api::addFileToDownloads>(file_id, chat_id, message_id, max(priority, 1)));
    } else if (op == "tdip") {
      FileId file_id;
      bool is_paused;
      get_args(args, file_id, is_paused);
      send_request(td_api::make_object<td_api::toggleDownloadIsPaused>(file_id, is_paused));
    } else if (op == "tadap") {
      bool are_paused;
      get_args(args, are_paused);
      send_request(td_api::make_object<td_api::toggleAllDownloadsArePaused>(are_paused));
    } else if (op == "rffd") {
      FileId file_id;
      bool delete_from_cache;
      get_args(args, file_id, delete_from_cache);
      send_request(td_api::make_object<td_api::removeFileFromDownloads>(file_id, delete_from_cache));
    } else if (op == "raffd" || op == "raffda" || op == "raffdc") {
      bool delete_from_cache;
      get_args(args, delete_from_cache);
      send_request(td_api::make_object<td_api::removeAllFilesFromDownloads>(op.back() == 'a', op.back() == 'c',
                                                                            delete_from_cache));
    } else if (op == "sfd" || op == "sfda" || op == "sfdc") {
      string offset;
      SearchQuery query;
      get_args(args, offset, query);
      send_request(td_api::make_object<td_api::searchFileDownloads>(query.query, op.back() == 'a', op.back() == 'c',
                                                                    offset, query.limit));
    } else if (op == "dm" || op == "dmr") {
      ChatId chat_id;
      string message_ids;
      get_args(args, chat_id, message_ids);
      send_request(td_api::make_object<td_api::deleteMessages>(chat_id, as_message_ids(message_ids), op == "dmr"));
    } else if (op == "fm" || op == "cm" || op == "fmp" || op == "cmp") {
      ChatId chat_id;
      ChatId from_chat_id;
      string message_ids;
      get_args(args, chat_id, from_chat_id, message_ids);
      auto chat = chat_id;
      send_request(td_api::make_object<td_api::forwardMessages>(chat, from_chat_id, as_message_ids(message_ids),
                                                                default_message_send_options(), op[0] == 'c',
                                                                rand_bool(), op.back() == 'p'));
    } else if (op == "resend") {
      ChatId chat_id;
      string message_ids;
      get_args(args, chat_id, message_ids);
      send_request(td_api::make_object<td_api::resendMessages>(chat_id, as_message_ids(message_ids)));
    } else if (op == "csc" || op == "CreateSecretChat") {
      send_request(td_api::make_object<td_api::createSecretChat>(as_secret_chat_id(args)));
    } else if (op == "cnsc" || op == "CreateNewSecretChat") {
      UserId user_id;
      get_args(args, user_id);
      send_request(td_api::make_object<td_api::createNewSecretChat>(user_id));
    } else if (op == "scstn") {
      ChatId chat_id;
      get_args(args, chat_id);
      send_request(td_api::make_object<td_api::sendChatScreenshotTakenNotification>(chat_id));
    } else if (op == "closeSC" || op == "cancelSC") {
      send_request(td_api::make_object<td_api::closeSecretChat>(as_secret_chat_id(args)));
    } else {
      op_not_found_count++;
    }

    if (op == "cc" || op == "CreateCall") {
      UserId user_id;
      get_args(args, user_id);
      send_request(td_api::make_object<td_api::createCall>(
          user_id, td_api::make_object<td_api::callProtocol>(true, true, 65, 65, vector<string>{"2.6", "3.0"}),
          rand_bool()));
    } else if (op == "ac" || op == "AcceptCall") {
      CallId call_id;
      get_args(args, call_id);
      send_request(td_api::make_object<td_api::acceptCall>(
          call_id, td_api::make_object<td_api::callProtocol>(true, true, 65, 65, vector<string>{"2.6", "3.0"})));
    } else if (op == "scsd") {
      CallId call_id;
      get_args(args, call_id);
      send_request(td_api::make_object<td_api::sendCallSignalingData>(call_id, "abacaba"));
    } else if (op == "dc" || op == "DiscardCall") {
      CallId call_id;
      bool is_disconnected;
      get_args(args, call_id, is_disconnected);
      send_request(td_api::make_object<td_api::discardCall>(call_id, is_disconnected, 0, rand_bool(), 0));
    } else if (op == "scr" || op == "SendCallRating") {
      CallId call_id;
      int32 rating;
      get_args(args, call_id, rating);
      vector<td_api::object_ptr<td_api::CallProblem>> problems;
      problems.emplace_back(td_api::make_object<td_api::callProblemNoise>());
      problems.emplace_back(td_api::make_object<td_api::callProblemNoise>());
      problems.emplace_back(td_api::make_object<td_api::callProblemDistortedVideo>());
      problems.emplace_back(nullptr);
      problems.emplace_back(td_api::make_object<td_api::callProblemNoise>());
      problems.emplace_back(td_api::make_object<td_api::callProblemEcho>());
      problems.emplace_back(td_api::make_object<td_api::callProblemPixelatedVideo>());
      problems.emplace_back(td_api::make_object<td_api::callProblemDistortedSpeech>());
      send_request(td_api::make_object<td_api::sendCallRating>(call_id, rating, "Wow, such good call! (TDLib test)",
                                                               std::move(problems)));
    } else if (op == "scdi") {
      CallId call_id;
      get_args(args, call_id);
      send_request(td_api::make_object<td_api::sendCallDebugInformation>(call_id, "{}"));
    } else if (op == "sclog") {
      CallId call_id;
      string log_file;
      get_args(args, call_id, log_file);
      send_request(td_api::make_object<td_api::sendCallLog>(call_id, as_input_file(log_file)));
    } else if (op == "gvcap") {
      ChatId chat_id;
      get_args(args, chat_id);
      send_request(td_api::make_object<td_api::getVideoChatAvailableParticipants>(chat_id));
    } else if (op == "svcdp") {
      ChatId chat_id;
      string participant_id;
      get_args(args, chat_id, participant_id);
      send_request(
          td_api::make_object<td_api::setVideoChatDefaultParticipant>(chat_id, as_message_sender(participant_id)));
    } else if (op == "cvc") {
      ChatId chat_id;
      string title;
      int32 start_date;
      bool is_rtmp_stream;
      get_args(args, chat_id, title, start_date, is_rtmp_stream);
      send_request(td_api::make_object<td_api::createVideoChat>(chat_id, title, start_date, is_rtmp_stream));
    } else if (op == "gvcru") {
      ChatId chat_id;
      get_args(args, chat_id);
      send_request(td_api::make_object<td_api::getVideoChatRtmpUrl>(chat_id));
    } else if (op == "rvcru") {
      ChatId chat_id;
      get_args(args, chat_id);
      send_request(td_api::make_object<td_api::replaceVideoChatRtmpUrl>(chat_id));
    } else if (op == "ggc") {
      GroupCallId group_call_id;
      get_args(args, group_call_id);
      send_request(td_api::make_object<td_api::getGroupCall>(group_call_id));
    } else if (op == "ggcs") {
      GroupCallId group_call_id;
      get_args(args, group_call_id);
      send_request(td_api::make_object<td_api::getGroupCallStreams>(group_call_id));
    } else if (op == "ggcss") {
      GroupCallId group_call_id;
      int32 channel_id;
      get_args(args, group_call_id, channel_id);
      send_request(td_api::make_object<td_api::getGroupCallStreamSegment>(
          group_call_id, (std::time(nullptr) - 5) * 1000, 0, channel_id, nullptr));
    } else if (op == "ssgc") {
      GroupCallId group_call_id;
      get_args(args, group_call_id);
      send_request(td_api::make_object<td_api::startScheduledGroupCall>(group_call_id));
    } else if (op == "tgcesn" || op == "tgcesne") {
      GroupCallId group_call_id;
      get_args(args, group_call_id);
      send_request(
          td_api::make_object<td_api::toggleGroupCallEnabledStartNotification>(group_call_id, op == "tgcesne"));
    } else if (op == "jgc" || op == "jgcv" || op == "sgcss") {
      GroupCallId group_call_id;
      string participant_id;
      string invite_hash;
      get_args(args, group_call_id, participant_id, invite_hash);

      auto payload = PSTRING() << "{\"ufrag\":\"ufrag\",\"pwd\":\"pwd\",\"fingerprints\":[{\"hash\":\"hash\",\"setup\":"
                                  "\"setup\",\"fingerprint\":\"fingerprint\"},{\"hash\":\"h2\",\"setup\":\"s2\","
                                  "\"fingerprint\":\"fingerprint2\"}],\"ssrc\":"
                               << group_call_source_ << ',';
      if (op == "jgc") {
        payload.back() = '}';
      } else {
        string sim_sources = "[1,2]";
        string fid_sources = "[3,4]";
        if (op == "sgcss") {
          sim_sources = "[5,6]";
          fid_sources = "[7,8]";
        }
        payload +=
            "\"payload-types\":[{\"id\":12345,\"name\":\"opus\",\"clockrate\":48000,\"channels\":2,\"rtcp-fbs\":[{"
            "\"type\":\"transport-cc\",\"subtype\":\"subtype1\"},{\"type\":\"type2\",\"subtype\":\"subtype2\"}],"
            "\"parameters\":{\"minptime\":\"10\",\"useinbandfec\":\"1\"}}],\"rtp-hdrexts\":[{\"id\":1,\"uri\":\"urn:"
            "ietf:params:rtp-hdrext:ssrc-audio-level\"}],\"ssrc-groups\":[{\"sources\":" +
            sim_sources + ",\"semantics\":\"SIM\"},{\"sources\":" + fid_sources + ",\"semantics\":\"FID\"}]}";
      }
      if (op == "sgcss") {
        send_request(td_api::make_object<td_api::startGroupCallScreenSharing>(group_call_id, group_call_source_ + 1,
                                                                              std::move(payload)));
      } else {
        send_request(td_api::make_object<td_api::joinGroupCall>(group_call_id, as_message_sender(participant_id),
                                                                group_call_source_, std::move(payload), true, true,
                                                                invite_hash));
      }
    } else if (op == "tgcssip") {
      GroupCallId group_call_id;
      bool is_paused;
      get_args(args, group_call_id, is_paused);
      send_request(td_api::make_object<td_api::toggleGroupCallScreenSharingIsPaused>(group_call_id, is_paused));
    } else if (op == "egcss") {
      GroupCallId group_call_id;
      get_args(args, group_call_id);
      send_request(td_api::make_object<td_api::endGroupCallScreenSharing>(group_call_id));
    } else if (op == "sgct") {
      GroupCallId group_call_id;
      string title;
      get_args(args, group_call_id, title);
      send_request(td_api::make_object<td_api::setGroupCallTitle>(group_call_id, title));
    } else if (op == "tgcmnp" || op == "tgcmnpe") {
      GroupCallId group_call_id;
      get_args(args, group_call_id);
      send_request(td_api::make_object<td_api::toggleGroupCallMuteNewParticipants>(group_call_id, op == "tgcmnpe"));
    } else if (op == "rgcil") {
      GroupCallId group_call_id;
      get_args(args, group_call_id);
      send_request(td_api::make_object<td_api::revokeGroupCallInviteLink>(group_call_id));
    } else if (op == "tgcimvp") {
      GroupCallId group_call_id;
      bool is_my_video_paused;
      get_args(args, group_call_id, is_my_video_paused);
      send_request(td_api::make_object<td_api::toggleGroupCallIsMyVideoPaused>(group_call_id, is_my_video_paused));
    } else if (op == "tgcimve") {
      GroupCallId group_call_id;
      bool is_my_video_enabled;
      get_args(args, group_call_id, is_my_video_enabled);
      send_request(td_api::make_object<td_api::toggleGroupCallIsMyVideoEnabled>(group_call_id, is_my_video_enabled));
    } else if (op == "sgcpis") {
      GroupCallId group_call_id;
      int32 source_id;
      bool is_speaking;
      get_args(args, group_call_id, source_id, is_speaking);
      send_request(
          td_api::make_object<td_api::setGroupCallParticipantIsSpeaking>(group_call_id, source_id, is_speaking));
    } else if (op == "igcp") {
      GroupCallId group_call_id;
      string user_ids;
      get_args(args, group_call_id, user_ids);
      send_request(td_api::make_object<td_api::inviteGroupCallParticipants>(group_call_id, as_user_ids(user_ids)));
    } else if (op == "ggcil") {
      GroupCallId group_call_id;
      bool can_self_unmute;
      get_args(args, group_call_id, can_self_unmute);
      send_request(td_api::make_object<td_api::getGroupCallInviteLink>(group_call_id, can_self_unmute));
    } else if (op == "sgcr") {
      GroupCallId group_call_id;
      string title;
      bool record_video;
      bool use_portrait_orientation;
      get_args(args, group_call_id, title, record_video, use_portrait_orientation);
      send_request(td_api::make_object<td_api::startGroupCallRecording>(group_call_id, title, record_video,
                                                                        use_portrait_orientation));
    } else if (op == "egcr") {
      GroupCallId group_call_id;
      get_args(args, group_call_id);
      send_request(td_api::make_object<td_api::endGroupCallRecording>(group_call_id));
    } else if (op == "tgcpim") {
      GroupCallId group_call_id;
      string participant_id;
      bool is_muted;
      get_args(args, group_call_id, participant_id, is_muted);
      send_request(td_api::make_object<td_api::toggleGroupCallParticipantIsMuted>(
          group_call_id, as_message_sender(participant_id), is_muted));
    } else if (op == "sgcpvl") {
      GroupCallId group_call_id;
      string participant_id;
      int32 volume_level;
      get_args(args, group_call_id, participant_id, volume_level);
      send_request(td_api::make_object<td_api::setGroupCallParticipantVolumeLevel>(
          group_call_id, as_message_sender(participant_id), volume_level));
    } else if (op == "tgcpihr") {
      GroupCallId group_call_id;
      string participant_id;
      bool is_hand_raised;
      get_args(args, group_call_id, participant_id, is_hand_raised);
      send_request(td_api::make_object<td_api::toggleGroupCallParticipantIsHandRaised>(
          group_call_id, as_message_sender(participant_id), is_hand_raised));
    } else if (op == "lgcp") {
      GroupCallId group_call_id;
      string limit;
      get_args(args, group_call_id, limit);
      send_request(td_api::make_object<td_api::loadGroupCallParticipants>(group_call_id, as_limit(limit)));
    } else if (op == "lgc") {
      GroupCallId group_call_id;
      get_args(args, group_call_id);
      send_request(td_api::make_object<td_api::leaveGroupCall>(group_call_id));
    } else if (op == "egc") {
      GroupCallId group_call_id;
      get_args(args, group_call_id);
      send_request(td_api::make_object<td_api::endGroupCall>(group_call_id));
    } else if (op == "rpcil") {
      ChatId chat_id;
      get_args(args, chat_id);
      send_request(td_api::make_object<td_api::replacePrimaryChatInviteLink>(chat_id));
    } else if (op == "ccilt") {
      ChatId chat_id;
      string name;
      int32 expiration_date;
      int32 member_limit;
      bool creates_join_request;
      get_args(args, chat_id, name, expiration_date, member_limit, creates_join_request);
      send_request(td_api::make_object<td_api::createChatInviteLink>(chat_id, name, expiration_date, member_limit,
                                                                     creates_join_request));
    } else if (op == "ecil") {
      ChatId chat_id;
      string invite_link;
      string name;
      int32 expiration_date;
      int32 member_limit;
      bool creates_join_request;
      get_args(args, chat_id, invite_link, name, expiration_date, member_limit, creates_join_request);
      send_request(td_api::make_object<td_api::editChatInviteLink>(chat_id, invite_link, name, expiration_date,
                                                                   member_limit, creates_join_request));
    } else if (op == "rcil") {
      ChatId chat_id;
      string invite_link;
      get_args(args, chat_id, invite_link);
      send_request(td_api::make_object<td_api::revokeChatInviteLink>(chat_id, invite_link));
    } else if (op == "gcilc") {
      ChatId chat_id;
      get_args(args, chat_id);
      send_request(td_api::make_object<td_api::getChatInviteLinkCounts>(chat_id));
    } else if (op == "gcil") {
      ChatId chat_id;
      string invite_link;
      get_args(args, chat_id, invite_link);
      send_request(td_api::make_object<td_api::getChatInviteLink>(chat_id, invite_link));
    } else if (op == "gcils" || op == "gcilr") {
      ChatId chat_id;
      UserId creator_user_id;
      int32 offset_date;
      string offset_invite_link;
      string limit;
      get_args(args, chat_id, creator_user_id, offset_date, offset_invite_link, limit);
      send_request(td_api::make_object<td_api::getChatInviteLinks>(chat_id, creator_user_id, op == "gcilr", offset_date,
                                                                   offset_invite_link, as_limit(limit)));
    } else if (op == "gcilm") {
      ChatId chat_id;
      string invite_link;
      UserId offset_user_id;
      int32 offset_date;
      string limit;
      get_args(args, chat_id, invite_link, offset_user_id, offset_date, limit);
      send_request(td_api::make_object<td_api::getChatInviteLinkMembers>(
          chat_id, invite_link, td_api::make_object<td_api::chatInviteLinkMember>(offset_user_id, offset_date, 0),
          as_limit(limit)));
    } else if (op == "gcjr") {
      ChatId chat_id;
      string invite_link;
      string query;
      UserId offset_user_id;
      int32 offset_date;
      string limit;
      get_args(args, chat_id, invite_link, query, offset_user_id, offset_date, limit);
      send_request(td_api::make_object<td_api::getChatJoinRequests>(
          chat_id, invite_link, query,
          td_api::make_object<td_api::chatJoinRequest>(offset_user_id, offset_date, string()), as_limit(limit)));
    } else if (op == "pcjr") {
      ChatId chat_id;
      UserId user_id;
      bool approve;
      get_args(args, chat_id, user_id, approve);
      send_request(td_api::make_object<td_api::processChatJoinRequest>(chat_id, user_id, approve));
    } else if (op == "pcjrs") {
      ChatId chat_id;
      string invite_link;
      bool approve;
      get_args(args, chat_id, invite_link, approve);
      send_request(td_api::make_object<td_api::processChatJoinRequests>(chat_id, invite_link, approve));
    } else if (op == "drcil") {
      ChatId chat_id;
      string invite_link;
      get_args(args, chat_id, invite_link);
      send_request(td_api::make_object<td_api::deleteRevokedChatInviteLink>(chat_id, invite_link));
    } else if (op == "darcil") {
      ChatId chat_id;
      UserId creator_user_id;
      get_args(args, chat_id, creator_user_id);
      send_request(td_api::make_object<td_api::deleteAllRevokedChatInviteLinks>(chat_id, creator_user_id));
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
      auto test_get_json_string = [&](td_api::object_ptr<td_api::JsonValue> &&json_value) {
        execute(td_api::make_object<td_api::getJsonString>(std::move(json_value)));
      };

      test_get_json_string(nullptr);
      test_get_json_string(td_api::make_object<td_api::jsonValueNull>());
      test_get_json_string(td_api::make_object<td_api::jsonValueBoolean>(true));
      test_get_json_string(td_api::make_object<td_api::jsonValueNumber>(123456789123.0));
      test_get_json_string(td_api::make_object<td_api::jsonValueString>(string("aba\0caba", 8)));
      test_get_json_string(td_api::make_object<td_api::jsonValueString>("aba\200caba"));

      auto inner_array = td_api::make_object<td_api::jsonValueArray>();
      inner_array->values_.emplace_back(td_api::make_object<td_api::jsonValueBoolean>(false));
      auto array = td_api::make_object<td_api::jsonValueArray>();
      array->values_.emplace_back(nullptr);
      array->values_.emplace_back(std::move(inner_array));
      array->values_.emplace_back(td_api::make_object<td_api::jsonValueNull>());
      array->values_.emplace_back(td_api::make_object<td_api::jsonValueNumber>(-1));
      test_get_json_string(std::move(array));

      auto object = td_api::make_object<td_api::jsonValueObject>();
      object->members_.emplace_back(
          td_api::make_object<td_api::jsonObjectMember>("", td_api::make_object<td_api::jsonValueString>("test")));
      object->members_.emplace_back(td_api::make_object<td_api::jsonObjectMember>("a", nullptr));
      object->members_.emplace_back(td_api::make_object<td_api::jsonObjectMember>("\x80", nullptr));
      object->members_.emplace_back(nullptr);
      object->members_.emplace_back(
          td_api::make_object<td_api::jsonObjectMember>("a", td_api::make_object<td_api::jsonValueNull>()));
      test_get_json_string(std::move(object));
    } else if (op == "gtpjs") {
      execute(td_api::make_object<td_api::getThemeParametersJsonString>(get_theme_parameters()));
    } else if (op == "gac") {
      send_request(td_api::make_object<td_api::getApplicationConfig>());
    } else if (op == "sale") {
      string type;
      ChatId chat_id;
      string json;
      get_args(args, type, chat_id, json);
      auto result = execute(td_api::make_object<td_api::getJsonValue>(json));
      if (result->get_id() == td_api::error::ID) {
        LOG(ERROR) << to_string(result);
      } else {
        send_request(td_api::make_object<td_api::saveApplicationLogEvent>(
            type, chat_id, move_tl_object_as<td_api::JsonValue>(result)));
      }
    } else {
      op_not_found_count++;
    }

    if (op == "scdm" || op == "scdmt") {
      ChatId chat_id;
      string message_thread_id;
      string reply_to_message_id;
      string message;
      if (op == "scdmt") {
        get_args(args, message_thread_id, args);
      }
      get_args(args, chat_id, reply_to_message_id, message);
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
      send_request(td_api::make_object<td_api::setChatDraftMessage>(chat_id, as_message_thread_id(message_thread_id),
                                                                    std::move(draft_message)));
    } else if (op == "cadm") {
      send_request(td_api::make_object<td_api::clearAllDraftMessages>());
    } else if (op == "tchpc") {
      ChatId chat_id;
      bool has_protected_content;
      get_args(args, chat_id, has_protected_content);
      send_request(td_api::make_object<td_api::toggleChatHasProtectedContent>(chat_id, has_protected_content));
    } else if (op == "tcip" || op == "tcipa" || begins_with(op, "tcip-")) {
      ChatId chat_id;
      bool is_pinned;
      get_args(args, chat_id, is_pinned);
      send_request(td_api::make_object<td_api::toggleChatIsPinned>(as_chat_list(op), chat_id, is_pinned));
    } else if (op == "tcimau") {
      ChatId chat_id;
      bool is_marked_as_read;
      get_args(args, chat_id, is_marked_as_read);
      send_request(td_api::make_object<td_api::toggleChatIsMarkedAsUnread>(chat_id, is_marked_as_read));
    } else if (op == "tmsib") {
      string sender_id;
      bool is_blocked;
      get_args(args, sender_id, is_blocked);
      send_request(td_api::make_object<td_api::toggleMessageSenderIsBlocked>(as_message_sender(sender_id), is_blocked));
    } else if (op == "bmsfr") {
      MessageId message_id;
      bool delete_message;
      bool delete_all_messages;
      bool report_spam;
      get_args(args, message_id, delete_message, delete_all_messages, report_spam);
      send_request(td_api::make_object<td_api::blockMessageSenderFromReplies>(message_id, delete_message,
                                                                              delete_all_messages, report_spam));
    } else if (op == "tcddn") {
      ChatId chat_id;
      bool default_disable_notification;
      get_args(args, chat_id, default_disable_notification);
      send_request(
          td_api::make_object<td_api::toggleChatDefaultDisableNotification>(chat_id, default_disable_notification));
    } else if (op == "spchats" || op == "spchatsa" || begins_with(op, "spchats-")) {
      send_request(td_api::make_object<td_api::setPinnedChats>(as_chat_list(op), as_chat_ids(args)));
    } else if (op == "gamb") {
      UserId user_id;
      get_args(args, user_id);
      send_request(td_api::make_object<td_api::getAttachmentMenuBot>(user_id));
    } else if (op == "tbiatam") {
      UserId user_id;
      bool is_added;
      get_args(args, user_id, is_added);
      send_request(td_api::make_object<td_api::toggleBotIsAddedToAttachmentMenu>(user_id, is_added));
    } else if (op == "gwau") {
      UserId user_id;
      string url;
      get_args(args, user_id, url);
      send_request(td_api::make_object<td_api::getWebAppUrl>(user_id, url, get_theme_parameters()));
    } else if (op == "swad") {
      UserId user_id;
      string button_text;
      string data;
      get_args(args, user_id, button_text, data);
      send_request(td_api::make_object<td_api::sendWebAppData>(user_id, button_text, data));
    } else if (op == "owa") {
      ChatId chat_id;
      UserId bot_user_id;
      string url;
      MessageId reply_to_message_id;
      get_args(args, chat_id, bot_user_id, url, reply_to_message_id);
      send_request(td_api::make_object<td_api::openWebApp>(chat_id, bot_user_id, url, get_theme_parameters(),
                                                           reply_to_message_id));
    } else if (op == "cwa") {
      int64 launch_id;
      get_args(args, launch_id);
      send_request(td_api::make_object<td_api::closeWebApp>(launch_id));
    } else if (op == "sca") {
      ChatId chat_id;
      string message_thread_id;
      string action;
      get_args(args, chat_id, message_thread_id, action);
      send_request(td_api::make_object<td_api::sendChatAction>(chat_id, as_message_thread_id(message_thread_id),
                                                               get_chat_action(action)));
    } else if (op == "smt" || op == "smtp" || op == "smtf" || op == "smtpf") {
      ChatId chat_id;
      get_args(args, chat_id);
      for (int i = 1; i <= 200; i++) {
        string message = PSTRING() << (Random::fast(0, 3) == 0 && i > 90 ? "sleep " : "") << "#" << i;
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
      ChatId chat_id;
      string filter;
      string offset;
      SearchQuery query;
      get_args(args, chat_id, filter, offset, query);
      send_request(td_api::make_object<td_api::searchSecretMessages>(chat_id, query.query, offset, query.limit,
                                                                     as_search_messages_filter(filter)));
    } else if (op == "ssd") {
      schedule_date_ = std::move(args);
    } else if (op == "smti") {
      message_thread_id_ = std::move(args);
    } else if (op == "gcams") {
      ChatId chat_id;
      get_args(args, chat_id);
      send_request(td_api::make_object<td_api::getChatAvailableMessageSenders>(chat_id));
    } else if (op == "scmsr") {
      ChatId chat_id;
      string sender_id;
      get_args(args, chat_id, sender_id);
      send_request(td_api::make_object<td_api::setChatMessageSender>(chat_id, as_message_sender(sender_id)));
    } else if (op == "sm" || op == "sms" || op == "smr" || op == "smf") {
      ChatId chat_id;
      MessageId reply_to_message_id;
      string message;
      get_args(args, chat_id, message);
      if (op == "smr") {
        get_args(message, reply_to_message_id, message);
      }
      if (op == "smf") {
        message = string(5097, 'a');
      }
      send_message(chat_id, td_api::make_object<td_api::inputMessageText>(as_formatted_text(message), false, true),
                   op == "sms", false, reply_to_message_id);
    } else if (op == "alm" || op == "almr") {
      ChatId chat_id;
      string sender_id;
      MessageId reply_to_message_id;
      string message;
      get_args(args, chat_id, sender_id, message);
      if (op == "almr") {
        get_args(message, reply_to_message_id, message);
      }
      send_request(td_api::make_object<td_api::addLocalMessage>(
          chat_id, as_message_sender(sender_id), reply_to_message_id, false,
          td_api::make_object<td_api::inputMessageText>(as_formatted_text(message), false, true)));
    } else if (op == "smap" || op == "smapr" || op == "smapp" || op == "smaprp") {
      ChatId chat_id;
      MessageId reply_to_message_id;
      get_args(args, chat_id, args);
      if (op == "smapr" || op == "smaprp") {
        get_args(args, reply_to_message_id, args);
      }
      auto input_message_contents = transform(full_split(args), [](const string &photo) {
        td_api::object_ptr<td_api::InputMessageContent> content = td_api::make_object<td_api::inputMessagePhoto>(
            as_input_file(photo), nullptr, Auto(), 0, 0, as_caption(""), 0);
        return content;
      });
      send_request(td_api::make_object<td_api::sendMessageAlbum>(
          chat_id, as_message_thread_id(message_thread_id_), reply_to_message_id, default_message_send_options(),
          std::move(input_message_contents), op == "smapp" || op == "smaprp"));
    } else if (op == "smad" || op == "smadp") {
      ChatId chat_id;
      get_args(args, chat_id, args);
      auto input_message_contents = transform(full_split(args), [](const string &document) {
        td_api::object_ptr<td_api::InputMessageContent> content =
            td_api::make_object<td_api::inputMessageDocument>(as_input_file(document), nullptr, true, as_caption(""));
        return content;
      });
      send_request(td_api::make_object<td_api::sendMessageAlbum>(chat_id, as_message_thread_id(message_thread_id_), 0,
                                                                 default_message_send_options(),
                                                                 std::move(input_message_contents), op.back() == 'p'));
    } else if (op == "gmft") {
      auto r_message_file_head = read_file_str(args, 2 << 10);
      if (r_message_file_head.is_error()) {
        LOG(ERROR) << r_message_file_head.error();
      } else {
        auto message_file_head = r_message_file_head.move_as_ok();
        while (!check_utf8(message_file_head)) {
          message_file_head.pop_back();
        }
        send_request(td_api::make_object<td_api::getMessageFileType>(message_file_head));
      }
    } else if (op == "gmict") {
      ChatId chat_id;
      get_args(args, chat_id);
      send_request(td_api::make_object<td_api::getMessageImportConfirmationText>(chat_id));
    } else if (op == "im") {
      ChatId chat_id;
      string message_file;
      vector<string> attached_files;
      get_args(args, chat_id, message_file, args);
      attached_files = full_split(args);
      send_request(td_api::make_object<td_api::importMessages>(chat_id, as_input_file(message_file),
                                                               transform(attached_files, as_input_file)));
    } else if (op == "em") {
      ChatId chat_id;
      MessageId message_id;
      string message;
      get_args(args, chat_id, message_id, message);
      send_request(td_api::make_object<td_api::editMessageText>(
          chat_id, message_id, nullptr,
          td_api::make_object<td_api::inputMessageText>(as_formatted_text(message), true, true)));
    } else if (op == "eman") {
      ChatId chat_id;
      MessageId message_id;
      string animation;
      get_args(args, chat_id, message_id, animation);
      send_request(td_api::make_object<td_api::editMessageMedia>(
          chat_id, message_id, nullptr,
          td_api::make_object<td_api::inputMessageAnimation>(as_input_file(animation), nullptr, vector<int32>(), 0, 0,
                                                             0, as_caption("animation"))));
    } else if (op == "emc") {
      ChatId chat_id;
      MessageId message_id;
      string caption;
      get_args(args, chat_id, message_id, caption);
      send_request(td_api::make_object<td_api::editMessageCaption>(chat_id, message_id, nullptr, as_caption(caption)));
    } else if (op == "emd") {
      ChatId chat_id;
      MessageId message_id;
      string document;
      get_args(args, chat_id, message_id, document);
      send_request(td_api::make_object<td_api::editMessageMedia>(
          chat_id, message_id, nullptr,
          td_api::make_object<td_api::inputMessageDocument>(as_input_file(document), nullptr, false, as_caption(""))));
    } else if (op == "emp" || op == "empttl") {
      ChatId chat_id;
      MessageId message_id;
      string photo;
      get_args(args, chat_id, message_id, photo);
      send_request(td_api::make_object<td_api::editMessageMedia>(
          chat_id, message_id, nullptr,
          td_api::make_object<td_api::inputMessagePhoto>(as_input_file(photo), as_input_thumbnail(photo), Auto(), 0, 0,
                                                         as_caption(""), op == "empttl" ? 10 : 0)));
    } else if (op == "emvt") {
      ChatId chat_id;
      MessageId message_id;
      string video;
      string thumbnail;
      get_args(args, chat_id, message_id, video, thumbnail);
      send_request(td_api::make_object<td_api::editMessageMedia>(
          chat_id, message_id, nullptr,
          td_api::make_object<td_api::inputMessageVideo>(as_input_file(video), as_input_thumbnail(thumbnail), Auto(), 1,
                                                         2, 3, true, as_caption(""), 0)));
    } else if (op == "emll") {
      ChatId chat_id;
      MessageId message_id;
      string latitude;
      string longitude;
      string accuracy;
      int32 heading;
      int32 proximity_alert_radius;
      get_args(args, chat_id, message_id, latitude, longitude, accuracy, heading, proximity_alert_radius);
      send_request(td_api::make_object<td_api::editMessageLiveLocation>(
          chat_id, message_id, nullptr, as_location(latitude, longitude, accuracy), heading, proximity_alert_radius));
    } else if (op == "emss") {
      ChatId chat_id;
      MessageId message_id;
      string date;
      get_args(args, chat_id, message_id, date);
      send_request(td_api::make_object<td_api::editMessageSchedulingState>(chat_id, message_id,
                                                                           as_message_scheduling_state(date)));
    } else if (op == "gallm") {
      send_request(td_api::make_object<td_api::getActiveLiveLocationMessages>());
    } else if (op == "sbsm") {
      UserId bot_user_id;
      ChatId chat_id;
      string parameter;
      get_args(args, bot_user_id, chat_id, parameter);
      send_request(td_api::make_object<td_api::sendBotStartMessage>(bot_user_id, chat_id, parameter));
    } else if (op == "giqr") {
      string bot_id;
      string query;
      get_args(args, bot_id, query);
      send_request(td_api::make_object<td_api::getInlineQueryResults>(as_user_id(bot_id), as_chat_id(bot_id), nullptr,
                                                                      query, ""));
    } else if (op == "giqro") {
      UserId bot_user_id;
      string offset;
      string query;
      get_args(args, bot_user_id, offset, query);
      send_request(td_api::make_object<td_api::getInlineQueryResults>(bot_user_id, 0, nullptr, query, offset));
    } else if (op == "giqrl") {
      UserId bot_user_id;
      string query;
      get_args(args, bot_user_id, query);
      send_request(
          td_api::make_object<td_api::getInlineQueryResults>(bot_user_id, 0, as_location("1.1", "2.2", ""), query, ""));
    } else if (op == "siqr" || op == "siqrh") {
      ChatId chat_id;
      int64 query_id;
      string result_id;
      get_args(args, chat_id, query_id, result_id);
      auto chat = chat_id;
      send_request(td_api::make_object<td_api::sendInlineQueryResultMessage>(
          chat, as_message_thread_id(message_thread_id_), 0, default_message_send_options(), query_id, result_id,
          op == "siqrh"));
    } else if (op == "gcqa") {
      ChatId chat_id;
      MessageId message_id;
      string data;
      get_args(args, chat_id, message_id, data);
      send_request(td_api::make_object<td_api::getCallbackQueryAnswer>(
          chat_id, message_id, td_api::make_object<td_api::callbackQueryPayloadData>(data)));
    } else if (op == "gcpqa") {
      ChatId chat_id;
      MessageId message_id;
      string password;
      string data;
      get_args(args, chat_id, message_id, password, data);
      send_request(td_api::make_object<td_api::getCallbackQueryAnswer>(
          chat_id, message_id, td_api::make_object<td_api::callbackQueryPayloadDataWithPassword>(password, data)));
    } else if (op == "gcgqa") {
      ChatId chat_id;
      MessageId message_id;
      get_args(args, chat_id, message_id);
      send_request(td_api::make_object<td_api::getCallbackQueryAnswer>(
          chat_id, message_id, td_api::make_object<td_api::callbackQueryPayloadGame>("")));
    } else {
      op_not_found_count++;
    }

    if (op == "san") {
      ChatId chat_id;
      string animation_path;
      int32 width;
      int32 height;
      string caption;
      get_args(args, chat_id, animation_path, width, height, caption);
      send_message(chat_id, td_api::make_object<td_api::inputMessageAnimation>(as_input_file(animation_path), nullptr,
                                                                               vector<int32>(), 60, width, height,
                                                                               as_caption(caption)));
    } else if (op == "sang") {
      ChatId chat_id;
      string animation_path;
      string animation_conversion;
      get_args(args, chat_id, animation_path, animation_conversion);
      send_message(chat_id, td_api::make_object<td_api::inputMessageAnimation>(
                                as_generated_file(animation_path, animation_conversion), nullptr, vector<int32>(), 60,
                                0, 0, as_caption("")));
    } else if (op == "sanid") {
      ChatId chat_id;
      string file_id;
      get_args(args, chat_id, file_id);
      send_message(chat_id, td_api::make_object<td_api::inputMessageAnimation>(
                                as_input_file_id(file_id), nullptr, vector<int32>(), 0, 0, 0, as_caption("")));
    } else if (op == "sanurl") {
      ChatId chat_id;
      string url;
      get_args(args, chat_id, url);
      send_message(chat_id, td_api::make_object<td_api::inputMessageAnimation>(
                                as_generated_file(url, "#url#"), nullptr, vector<int32>(), 0, 0, 0, as_caption("")));
    } else if (op == "sanurl2") {
      ChatId chat_id;
      string url;
      get_args(args, chat_id, url);
      send_message(chat_id, td_api::make_object<td_api::inputMessageAnimation>(
                                as_remote_file(url), nullptr, vector<int32>(), 0, 0, 0, as_caption("")));
    } else if (op == "sau") {
      ChatId chat_id;
      string audio_path;
      int32 duration;
      string title;
      string performer;
      get_args(args, chat_id, audio_path, duration, title, performer);
      send_message(chat_id,
                   td_api::make_object<td_api::inputMessageAudio>(as_input_file(audio_path), nullptr, duration, title,
                                                                  performer, as_caption("audio caption")));
    } else if (op == "svoice") {
      ChatId chat_id;
      string voice_path;
      get_args(args, chat_id, voice_path);
      send_message(chat_id, td_api::make_object<td_api::inputMessageVoiceNote>(as_input_file(voice_path), 0, "abacaba",
                                                                               as_caption("voice caption")));
    } else if (op == "SendContact" || op == "scontact") {
      ChatId chat_id;
      string phone_number;
      string first_name;
      string last_name;
      UserId user_id;
      get_args(args, chat_id, phone_number, first_name, last_name, user_id);
      send_message(chat_id, td_api::make_object<td_api::inputMessageContact>(td_api::make_object<td_api::contact>(
                                phone_number, first_name, last_name, string(), user_id)));
    } else if (op == "sf" || op == "scopy") {
      ChatId chat_id;
      ChatId from_chat_id;
      MessageId from_message_id;
      get_args(args, chat_id, from_chat_id, from_message_id);
      td_api::object_ptr<td_api::messageCopyOptions> copy_options;
      if (op == "scopy") {
        copy_options = td_api::make_object<td_api::messageCopyOptions>(true, rand_bool(), as_caption("_as_d"));
      }
      send_message(chat_id, td_api::make_object<td_api::inputMessageForwarded>(from_chat_id, from_message_id, true,
                                                                               std::move(copy_options)));
    } else if (op == "sdice" || op == "sdicecd") {
      ChatId chat_id;
      string emoji;
      get_args(args, chat_id, emoji);
      send_message(chat_id, td_api::make_object<td_api::inputMessageDice>(emoji, op == "sdicecd"));
    } else if (op == "sd" || op == "sdf") {
      ChatId chat_id;
      string document_path;
      get_args(args, chat_id, document_path);
      send_message(chat_id, td_api::make_object<td_api::inputMessageDocument>(
                                as_input_file(document_path), nullptr, op == "sdf",
                                as_caption(u8"\u1680\u180Etest \u180E\n\u180E\n\u180E\n cap\ttion\u180E\u180E")));
    } else if (op == "sdt" || op == "sdtf") {
      ChatId chat_id;
      string document_path;
      string thumbnail_path;
      get_args(args, chat_id, document_path, thumbnail_path);
      send_message(chat_id, td_api::make_object<td_api::inputMessageDocument>(
                                as_input_file(document_path), as_input_thumbnail(thumbnail_path), op == "sdtf",
                                as_caption("test caption")));
    } else if (op == "sdg" || op == "sdgu") {
      ChatId chat_id;
      string document_path;
      string document_conversion;
      get_args(args, chat_id, document_path, document_conversion);
      if (op == "sdgu") {
        send_request(
            td_api::make_object<td_api::uploadFile>(as_generated_file(document_path, document_conversion), nullptr, 1));
      }
      send_message(chat_id, td_api::make_object<td_api::inputMessageDocument>(
                                as_generated_file(document_path, document_conversion), nullptr, false,
                                as_caption("test caption")));
    } else if (op == "sdtg") {
      ChatId chat_id;
      string document_path;
      string thumbnail_path;
      string thumbnail_conversion;
      get_args(args, chat_id, document_path, thumbnail_path, thumbnail_conversion);
      send_message(chat_id, td_api::make_object<td_api::inputMessageDocument>(
                                as_input_file(document_path), as_input_thumbnail(thumbnail_path, thumbnail_conversion),
                                false, as_caption("test caption")));
    } else if (op == "sdgtg") {
      ChatId chat_id;
      string document_path;
      string document_conversion;
      string thumbnail_path;
      string thumbnail_conversion;
      get_args(args, chat_id, document_path, document_conversion, thumbnail_path, thumbnail_conversion);
      send_message(chat_id,
                   td_api::make_object<td_api::inputMessageDocument>(
                       as_generated_file(document_path, document_conversion),
                       as_input_thumbnail(thumbnail_path, thumbnail_conversion), false, as_caption("test caption")));
    } else if (op == "sdid") {
      ChatId chat_id;
      string file_id;
      get_args(args, chat_id, file_id);
      send_message(chat_id, td_api::make_object<td_api::inputMessageDocument>(as_input_file_id(file_id), nullptr, false,
                                                                              as_caption("")));
    } else if (op == "sdurl") {
      ChatId chat_id;
      string url;
      get_args(args, chat_id, url);
      send_message(chat_id, td_api::make_object<td_api::inputMessageDocument>(as_remote_file(url), nullptr, false,
                                                                              as_caption("")));
    } else if (op == "sg") {
      ChatId chat_id;
      UserId bot_user_id;
      string game_short_name;
      get_args(args, chat_id, bot_user_id, game_short_name);
      send_message(chat_id, td_api::make_object<td_api::inputMessageGame>(bot_user_id, game_short_name));
    } else if (op == "sl") {
      ChatId chat_id;
      string latitude;
      string longitude;
      string accuracy;
      get_args(args, chat_id, latitude, longitude, accuracy);
      send_message(chat_id, td_api::make_object<td_api::inputMessageLocation>(
                                as_location(latitude, longitude, accuracy), 0, 0, 0));
    } else if (op == "sll") {
      ChatId chat_id;
      int32 period;
      string latitude;
      string longitude;
      string accuracy;
      int32 heading;
      int32 proximity_alert_radius;
      get_args(args, chat_id, period, latitude, longitude, accuracy, heading, proximity_alert_radius);
      send_message(chat_id, td_api::make_object<td_api::inputMessageLocation>(
                                as_location(latitude, longitude, accuracy), period, heading, proximity_alert_radius));
    } else if (op == "spoll" || op == "spollm" || op == "spollp" || op == "squiz") {
      ChatId chat_id;
      string question;
      get_args(args, chat_id, question, args);
      auto options = autosplit_str(args);
      td_api::object_ptr<td_api::PollType> poll_type;
      if (op == "squiz") {
        poll_type = td_api::make_object<td_api::pollTypeQuiz>(narrow_cast<int32>(options.size() - 1),
                                                              as_formatted_text("_te*st*_"));
      } else {
        poll_type = td_api::make_object<td_api::pollTypeRegular>(op == "spollm");
      }
      send_message(chat_id, td_api::make_object<td_api::inputMessagePoll>(question, std::move(options), op != "spollp",
                                                                          std::move(poll_type), 0, 0, false));
    } else if (op == "sp" || op == "spttl") {
      ChatId chat_id;
      string photo;
      string caption;
      string sticker_file_ids;
      get_args(args, chat_id, photo, caption, sticker_file_ids);
      send_message(chat_id, td_api::make_object<td_api::inputMessagePhoto>(
                                as_input_file(photo), nullptr, to_integers<int32>(sticker_file_ids), 0, 0,
                                as_caption(caption), op == "spttl" ? 10 : 0));
    } else if (op == "spg" || op == "spgttl") {
      ChatId chat_id;
      string photo_path;
      string conversion;
      int64 expected_size;
      get_args(args, chat_id, photo_path, conversion, expected_size);
      send_message(chat_id, td_api::make_object<td_api::inputMessagePhoto>(
                                as_generated_file(photo_path, conversion, expected_size), nullptr, vector<int32>(), 0,
                                0, as_caption(""), op == "spgttl" ? 10 : 0));
    } else if (op == "spt") {
      ChatId chat_id;
      string photo_path;
      string thumbnail_path;
      get_args(args, chat_id, photo_path, thumbnail_path);
      send_message(chat_id, td_api::make_object<td_api::inputMessagePhoto>(as_input_file(photo_path),
                                                                           as_input_thumbnail(thumbnail_path, 90, 89),
                                                                           vector<int32>(), 0, 0, as_caption(""), 0));
    } else if (op == "sptg") {
      ChatId chat_id;
      string photo_path;
      string thumbnail_path;
      string thumbnail_conversion;
      get_args(args, chat_id, photo_path, thumbnail_path, thumbnail_conversion);
      send_message(chat_id,
                   td_api::make_object<td_api::inputMessagePhoto>(
                       as_input_file(photo_path), as_input_thumbnail(thumbnail_path, thumbnail_conversion, 90, 89),
                       vector<int32>(), 0, 0, as_caption(""), 0));
    } else if (op == "spgtg") {
      ChatId chat_id;
      string photo_path;
      string conversion;
      string thumbnail_path;
      string thumbnail_conversion;
      get_args(args, chat_id, photo_path, conversion, thumbnail_path, thumbnail_conversion);
      send_message(chat_id, td_api::make_object<td_api::inputMessagePhoto>(
                                as_generated_file(photo_path, conversion),
                                as_input_thumbnail(thumbnail_path, thumbnail_conversion, 90, 89), vector<int32>(), 0, 0,
                                as_caption(""), 0));
    } else if (op == "spid") {
      ChatId chat_id;
      string file_id;
      get_args(args, chat_id, file_id);
      send_message(chat_id, td_api::make_object<td_api::inputMessagePhoto>(as_input_file_id(file_id), nullptr,
                                                                           vector<int32>(), 0, 0, as_caption(""), 0));
    } else if (op == "ss") {
      ChatId chat_id;
      string sticker_path;
      get_args(args, chat_id, sticker_path);
      send_message(chat_id, td_api::make_object<td_api::inputMessageSticker>(as_input_file(sticker_path), nullptr, 0, 0,
                                                                             string()));
    } else if (op == "sstt") {
      ChatId chat_id;
      string sticker_path;
      string thumbnail_path;
      get_args(args, chat_id, sticker_path, thumbnail_path);
      send_message(chat_id, td_api::make_object<td_api::inputMessageSticker>(
                                as_input_file(sticker_path), as_input_thumbnail(thumbnail_path), 0, 0, string()));
    } else if (op == "ssid") {
      ChatId chat_id;
      string file_id;
      string emoji;
      get_args(args, chat_id, file_id, emoji);
      send_message(chat_id,
                   td_api::make_object<td_api::inputMessageSticker>(as_input_file_id(file_id), nullptr, 0, 0, emoji));
    } else if (op == "sv" || op == "svttl") {
      ChatId chat_id;
      string video_path;
      string sticker_file_ids_str;
      vector<int32> sticker_file_ids;
      get_args(args, chat_id, sticker_file_ids_str, video_path);
      if (trim(video_path).empty()) {
        video_path = sticker_file_ids_str;
      } else {
        sticker_file_ids = to_integers<int32>(sticker_file_ids_str);
      }
      send_message(chat_id, td_api::make_object<td_api::inputMessageVideo>(as_input_file(video_path), nullptr,
                                                                           std::move(sticker_file_ids), 1, 2, 3, true,
                                                                           as_caption(""), op == "svttl" ? 10 : 0));
    } else if (op == "svt" || op == "svtttl") {
      ChatId chat_id;
      string video;
      string thumbnail;
      get_args(args, chat_id, video, thumbnail);
      send_message(chat_id, td_api::make_object<td_api::inputMessageVideo>(
                                as_input_file(video), as_input_thumbnail(thumbnail), vector<int32>(), 0, 0, 0, true,
                                as_caption(""), op == "svtttl" ? 10 : 0));
    } else if (op == "svn") {
      ChatId chat_id;
      string video_path;
      get_args(args, chat_id, video_path);
      send_message(chat_id,
                   td_api::make_object<td_api::inputMessageVideoNote>(as_input_file(video_path), nullptr, 10, 5));
    } else if (op == "svenue") {
      ChatId chat_id;
      string latitude;
      string longitude;
      string accuracy;
      string title;
      string address;
      string provider;
      string venue_id;
      string venue_type;
      get_args(args, chat_id, latitude, longitude, accuracy, title, address, provider, venue_id, venue_type);
      send_message(chat_id,
                   td_api::make_object<td_api::inputMessageVenue>(td_api::make_object<td_api::venue>(
                       as_location(latitude, longitude, accuracy), title, address, provider, venue_id, venue_type)));
    } else {
      op_not_found_count++;
    }

    if (op == "test") {
      send_request(td_api::make_object<td_api::testNetwork>());
    } else if (op == "alarm") {
      send_request(td_api::make_object<td_api::setAlarm>(to_double(args)));
    } else if (op == "delete") {
      ChatId chat_id;
      bool remove_from_the_chat_list;
      bool revoke;
      get_args(args, chat_id, remove_from_the_chat_list, revoke);
      send_request(td_api::make_object<td_api::deleteChatHistory>(chat_id, remove_from_the_chat_list, revoke));
    } else if (op == "dcmbd") {
      ChatId chat_id;
      int32 min_date;
      int32 max_date;
      bool revoke;
      get_args(args, chat_id, min_date, max_date, revoke);
      send_request(td_api::make_object<td_api::deleteChatMessagesByDate>(chat_id, min_date, max_date, revoke));
    } else if (op == "dcmbs") {
      ChatId chat_id;
      string sender_id;
      get_args(args, chat_id, sender_id);
      send_request(td_api::make_object<td_api::deleteChatMessagesBySender>(chat_id, as_message_sender(sender_id)));
    } else if (op == "cnbgc") {
      string user_ids_string;
      string title;
      get_args(args, user_ids_string, title);
      send_request(td_api::make_object<td_api::createNewBasicGroupChat>(as_user_ids(user_ids_string), title));
    } else if (op == "cnchc") {
      send_request(td_api::make_object<td_api::createNewSupergroupChat>(args, true, "Description", nullptr, false));
    } else if (op == "cnsgc") {
      send_request(td_api::make_object<td_api::createNewSupergroupChat>(args, false, "Description", nullptr, false));
    } else if (op == "cnsgcloc") {
      send_request(td_api::make_object<td_api::createNewSupergroupChat>(
          args, false, "Description",
          td_api::make_object<td_api::chatLocation>(as_location("40.0", "60.0", ""), "address"), false));
    } else if (op == "cnsgcimport") {
      send_request(td_api::make_object<td_api::createNewSupergroupChat>(args, false, "Description", nullptr, true));
    } else if (op == "UpgradeBasicGroupChatToSupergroupChat") {
      ChatId chat_id;
      get_args(args, chat_id);
      send_request(td_api::make_object<td_api::upgradeBasicGroupChatToSupergroupChat>(chat_id));
    } else if (op == "DeleteChat") {
      ChatId chat_id;
      get_args(args, chat_id);
      send_request(td_api::make_object<td_api::deleteChat>(chat_id));
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
      UserId user_id;
      bool force;
      get_args(args, user_id, force);
      send_request(td_api::make_object<td_api::createPrivateChat>(user_id, force));
    } else if (op == "cbgc") {
      string basic_group_id;
      bool force;
      get_args(args, basic_group_id, force);
      send_request(td_api::make_object<td_api::createBasicGroupChat>(as_basic_group_id(basic_group_id), force));
    } else if (op == "csgc" || op == "cchc") {
      string supergroup_id;
      bool force;
      get_args(args, supergroup_id, force);
      send_request(td_api::make_object<td_api::createSupergroupChat>(as_supergroup_id(supergroup_id), force));
    } else if (op == "gcltac") {
      ChatId chat_id;
      get_args(args, chat_id);
      send_request(td_api::make_object<td_api::getChatListsToAddChat>(chat_id));
    } else if (op == "actl" || op == "actla" || begins_with(op, "actl-")) {
      ChatId chat_id;
      get_args(args, chat_id);
      send_request(td_api::make_object<td_api::addChatToList>(chat_id, as_chat_list(op)));
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
      get_args(args, chat_filter_id, filter);
      send_request(
          td_api::make_object<td_api::editChatFilter>(as_chat_filter_id(chat_filter_id), as_chat_filter(filter)));
    } else if (op == "dcf") {
      send_request(td_api::make_object<td_api::deleteChatFilter>(as_chat_filter_id(args)));
    } else if (op == "rcf") {
      int32 main_chat_list_position;
      string chat_filter_ids;
      get_args(args, main_chat_list_position, chat_filter_ids);
      send_request(td_api::make_object<td_api::reorderChatFilters>(as_chat_filter_ids(chat_filter_ids),
                                                                   main_chat_list_position));
    } else if (op == "grcf") {
      send_request(td_api::make_object<td_api::getRecommendedChatFilters>());
    } else if (op == "gcfdin") {
      execute(td_api::make_object<td_api::getChatFilterDefaultIconName>(as_chat_filter(args)));
    } else if (op == "sct") {
      ChatId chat_id;
      string title;
      get_args(args, chat_id, title);
      send_request(td_api::make_object<td_api::setChatTitle>(chat_id, title));
    } else if (op == "scpe") {
      ChatId chat_id;
      get_args(args, chat_id);
      send_request(td_api::make_object<td_api::setChatPhoto>(chat_id, nullptr));
    } else if (op == "scpp") {
      ChatId chat_id;
      int64 photo_id;
      get_args(args, chat_id, photo_id);
      send_request(td_api::make_object<td_api::setChatPhoto>(
          chat_id, td_api::make_object<td_api::inputChatPhotoPrevious>(photo_id)));
    } else if (op == "scp") {
      ChatId chat_id;
      string photo_path;
      get_args(args, chat_id, photo_path);
      send_request(td_api::make_object<td_api::setChatPhoto>(
          chat_id, td_api::make_object<td_api::inputChatPhotoStatic>(as_input_file(photo_path))));
    } else if (op == "scpa" || op == "scpv") {
      ChatId chat_id;
      string animation;
      string main_frame_timestamp;
      get_args(args, chat_id, animation, main_frame_timestamp);
      send_request(td_api::make_object<td_api::setChatPhoto>(
          chat_id, td_api::make_object<td_api::inputChatPhotoAnimation>(as_input_file(animation),
                                                                        to_double(main_frame_timestamp))));
    } else if (op == "scmt") {
      ChatId chat_id;
      int32 ttl;
      get_args(args, chat_id, ttl);
      send_request(td_api::make_object<td_api::setChatMessageTtl>(chat_id, ttl));
    } else if (op == "scperm") {
      ChatId chat_id;
      string permissions;
      get_args(args, chat_id, permissions);
      if (permissions.size() == 8) {
        auto &s = permissions;
        send_request(td_api::make_object<td_api::setChatPermissions>(
            chat_id, td_api::make_object<td_api::chatPermissions>(s[0] == '1', s[1] == '1', s[2] == '1', s[3] == '1',
                                                                  s[4] == '1', s[5] == '1', s[6] == '1', s[7] == '1')));
      } else {
        LOG(ERROR) << "Wrong permissions size, expected 8";
      }
    } else if (op == "sctn") {
      ChatId chat_id;
      string theme_name;
      get_args(args, chat_id, theme_name);
      send_request(td_api::make_object<td_api::setChatTheme>(chat_id, theme_name));
    } else if (op == "sccd") {
      ChatId chat_id;
      string client_data;
      get_args(args, chat_id, client_data);
      send_request(td_api::make_object<td_api::setChatClientData>(chat_id, client_data));
    } else if (op == "acm") {
      ChatId chat_id;
      UserId user_id;
      int32 forward_limit;
      get_args(args, chat_id, user_id, forward_limit);
      send_request(td_api::make_object<td_api::addChatMember>(chat_id, user_id, forward_limit));
    } else if (op == "acms") {
      ChatId chat_id;
      string user_ids;
      get_args(args, chat_id, user_ids);
      send_request(td_api::make_object<td_api::addChatMembers>(chat_id, as_user_ids(user_ids)));
    } else if (op == "bcm") {
      ChatId chat_id;
      string member_id;
      int32 banned_until_date;
      bool revoke_messages;
      get_args(args, chat_id, member_id, banned_until_date, revoke_messages);
      send_request(td_api::make_object<td_api::banChatMember>(chat_id, as_message_sender(member_id), banned_until_date,
                                                              revoke_messages));
    } else if (op == "spolla") {
      ChatId chat_id;
      MessageId message_id;
      string option_ids;
      get_args(args, chat_id, message_id, option_ids);
      send_request(td_api::make_object<td_api::setPollAnswer>(chat_id, message_id, to_integers<int32>(option_ids)));
    } else if (op == "gpollv") {
      ChatId chat_id;
      MessageId message_id;
      int32 option_id;
      int32 offset;
      string limit;
      get_args(args, chat_id, message_id, option_id, offset, limit);
      send_request(td_api::make_object<td_api::getPollVoters>(chat_id, message_id, option_id, offset, as_limit(limit)));
    } else if (op == "stoppoll") {
      ChatId chat_id;
      MessageId message_id;
      get_args(args, chat_id, message_id);
      send_request(td_api::make_object<td_api::stopPoll>(chat_id, message_id, nullptr));
    } else {
      op_not_found_count++;
    }

    if (op == "scms") {
      ChatId chat_id;
      string member_id;
      string status_str;
      td_api::object_ptr<td_api::ChatMemberStatus> status;
      get_args(args, chat_id, member_id, status_str);
      if (status_str == "member") {
        status = td_api::make_object<td_api::chatMemberStatusMember>();
      } else if (status_str == "left") {
        status = td_api::make_object<td_api::chatMemberStatusLeft>();
      } else if (status_str == "banned") {
        status = td_api::make_object<td_api::chatMemberStatusBanned>(std::numeric_limits<int32>::max());
      } else if (status_str == "creator") {
        status = td_api::make_object<td_api::chatMemberStatusCreator>("", false, true);
      } else if (status_str == "creatortitle") {
        status = td_api::make_object<td_api::chatMemberStatusCreator>("owner", false, true);
      } else if (status_str == "creatoranon") {
        status = td_api::make_object<td_api::chatMemberStatusCreator>("", true, true);
      } else if (status_str == "uncreator") {
        status = td_api::make_object<td_api::chatMemberStatusCreator>("", false, false);
      } else if (status_str == "anonadmin") {
        status = td_api::make_object<td_api::chatMemberStatusAdministrator>(
            "anon", true,
            as_chat_administrator_rights(true, true, true, true, true, true, true, true, true, true, true));
      } else if (status_str == "anon") {
        status = td_api::make_object<td_api::chatMemberStatusAdministrator>(
            "anon", false,
            as_chat_administrator_rights(false, false, false, false, false, false, false, false, false, false, true));
      } else if (status_str == "addadmin") {
        status = td_api::make_object<td_api::chatMemberStatusAdministrator>(
            "anon", false,
            as_chat_administrator_rights(false, false, false, false, false, false, false, false, true, false, false));
      } else if (status_str == "calladmin") {
        status = td_api::make_object<td_api::chatMemberStatusAdministrator>(
            "anon", false,
            as_chat_administrator_rights(false, false, false, false, false, false, false, false, false, true, false));
      } else if (status_str == "admin") {
        status = td_api::make_object<td_api::chatMemberStatusAdministrator>(
            "", true, as_chat_administrator_rights(false, true, true, true, true, true, true, true, true, true, false));
      } else if (status_str == "adminq") {
        status = td_api::make_object<td_api::chatMemberStatusAdministrator>(
            "title", true,
            as_chat_administrator_rights(false, true, true, true, true, true, true, true, true, true, false));
      } else if (status_str == "minadmin") {
        status = td_api::make_object<td_api::chatMemberStatusAdministrator>(
            "", true,
            as_chat_administrator_rights(true, false, false, false, false, false, false, false, false, false, false));
      } else if (status_str == "unadmin") {
        status = td_api::make_object<td_api::chatMemberStatusAdministrator>("", true, nullptr);
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
        send_request(
            td_api::make_object<td_api::setChatMemberStatus>(chat_id, as_message_sender(member_id), std::move(status)));
      } else {
        LOG(ERROR) << "Unknown status \"" << status_str << "\"";
      }
    } else if (op == "cto") {
      send_request(td_api::make_object<td_api::canTransferOwnership>());
    } else if (op == "transferChatOwnership") {
      ChatId chat_id;
      UserId user_id;
      string password;
      get_args(args, chat_id, user_id, password);
      send_request(td_api::make_object<td_api::transferChatOwnership>(chat_id, user_id, password));
    } else if (op == "log") {
      ChatId chat_id;
      string limit;
      string user_ids;
      get_args(args, chat_id, limit, user_ids);
      send_request(td_api::make_object<td_api::getChatEventLog>(chat_id, "", 0, as_limit(limit), nullptr,
                                                                as_user_ids(user_ids)));
    } else if (op == "join") {
      ChatId chat_id;
      get_args(args, chat_id);
      send_request(td_api::make_object<td_api::joinChat>(chat_id));
    } else if (op == "leave") {
      ChatId chat_id;
      get_args(args, chat_id);
      send_request(td_api::make_object<td_api::leaveChat>(chat_id));
    } else if (op == "dcm") {
      ChatId chat_id;
      string member_id;
      get_args(args, chat_id, member_id);
      td_api::object_ptr<td_api::ChatMemberStatus> status = td_api::make_object<td_api::chatMemberStatusBanned>();
      if (as_user_id(member_id) == my_id_) {
        status = td_api::make_object<td_api::chatMemberStatusLeft>();
      }
      send_request(
          td_api::make_object<td_api::setChatMemberStatus>(chat_id, as_message_sender(member_id), std::move(status)));
    } else if (op == "sn") {
      string first_name;
      string last_name;
      get_args(args, first_name, last_name);
      send_request(td_api::make_object<td_api::setName>(first_name, last_name));
    } else if (op == "sb") {
      send_request(td_api::make_object<td_api::setBio>("\n" + args + "\n" + args + "\n"));
    } else if (op == "sun") {
      send_request(td_api::make_object<td_api::setUsername>(args));
    } else if (op == "ccun") {
      ChatId chat_id;
      string username;
      get_args(args, chat_id, username);
      send_request(td_api::make_object<td_api::checkChatUsername>(chat_id, username));
    } else if (op == "ssgun" || op == "schun") {
      string supergroup_id;
      string username;
      get_args(args, supergroup_id, username);
      send_request(td_api::make_object<td_api::setSupergroupUsername>(as_supergroup_id(supergroup_id), username));
    } else if (op == "ssgss") {
      string supergroup_id;
      int64 sticker_set_id;
      get_args(args, supergroup_id, sticker_set_id);
      send_request(
          td_api::make_object<td_api::setSupergroupStickerSet>(as_supergroup_id(supergroup_id), sticker_set_id));
    } else if (op == "tsgp") {
      string supergroup_id;
      bool is_all_history_available;
      get_args(args, supergroup_id, is_all_history_available);
      send_request(td_api::make_object<td_api::toggleSupergroupIsAllHistoryAvailable>(as_supergroup_id(supergroup_id),
                                                                                      is_all_history_available));
    } else if (op == "ToggleSupergroupIsBroadcastGroup") {
      string supergroup_id;
      get_args(args, supergroup_id);
      send_request(td_api::make_object<td_api::toggleSupergroupIsBroadcastGroup>(as_supergroup_id(supergroup_id)));
    } else if (op == "tsgsm") {
      string supergroup_id;
      bool sign_messages;
      get_args(args, supergroup_id, sign_messages);
      send_request(
          td_api::make_object<td_api::toggleSupergroupSignMessages>(as_supergroup_id(supergroup_id), sign_messages));
    } else if (op == "tsgjtsm") {
      string supergroup_id;
      bool join_to_send_message;
      get_args(args, supergroup_id, join_to_send_message);
      send_request(td_api::make_object<td_api::toggleSupergroupJoinToSendMessages>(as_supergroup_id(supergroup_id),
                                                                                   join_to_send_message));
    } else if (op == "tsgjbr") {
      string supergroup_id;
      bool join_by_request;
      get_args(args, supergroup_id, join_by_request);
      send_request(
          td_api::make_object<td_api::toggleSupergroupJoinByRequest>(as_supergroup_id(supergroup_id), join_by_request));
    } else if (op == "scar") {
      ChatId chat_id;
      string available_reactions;
      get_args(args, chat_id, available_reactions);
      send_request(td_api::make_object<td_api::setChatAvailableReactions>(chat_id, autosplit_str(available_reactions)));
    } else if (op == "scd") {
      ChatId chat_id;
      string description;
      get_args(args, chat_id, description);
      send_request(td_api::make_object<td_api::setChatDescription>(chat_id, description));
    } else if (op == "scdg") {
      ChatId chat_id;
      ChatId group_chat_id;
      get_args(args, chat_id, group_chat_id);
      send_request(td_api::make_object<td_api::setChatDiscussionGroup>(chat_id, group_chat_id));
    } else if (op == "scl") {
      ChatId chat_id;
      string latitude;
      string longitude;
      get_args(args, chat_id, latitude, longitude);
      send_request(td_api::make_object<td_api::setChatLocation>(
          chat_id, td_api::make_object<td_api::chatLocation>(as_location(latitude, longitude, string()), "address")));
    } else if (op == "scsmd") {
      ChatId chat_id;
      int32 slow_mode_delay;
      get_args(args, chat_id, slow_mode_delay);
      send_request(td_api::make_object<td_api::setChatSlowModeDelay>(chat_id, slow_mode_delay));
    } else if (op == "pcm" || op == "pcms" || op == "pcmo") {
      ChatId chat_id;
      MessageId message_id;
      get_args(args, chat_id, message_id);
      send_request(td_api::make_object<td_api::pinChatMessage>(chat_id, message_id, op == "pcms", op == "pcmo"));
    } else if (op == "upcm") {
      ChatId chat_id;
      MessageId message_id;
      get_args(args, chat_id, message_id);
      send_request(td_api::make_object<td_api::unpinChatMessage>(chat_id, message_id));
    } else if (op == "uacm") {
      ChatId chat_id;
      get_args(args, chat_id);
      send_request(td_api::make_object<td_api::unpinAllChatMessages>(chat_id));
    } else if (op == "grib") {
      send_request(td_api::make_object<td_api::getRecentInlineBots>());
    } else if (op == "spc" || op == "su") {
      send_request(td_api::make_object<td_api::searchPublicChat>(args));
    } else if (op == "spcs") {
      send_request(td_api::make_object<td_api::searchPublicChats>(args));
    } else if (op == "sc") {
      SearchQuery query;
      get_args(args, query);
      send_request(td_api::make_object<td_api::searchChats>(query.query, query.limit));
    } else if (op == "scos") {
      SearchQuery query;
      get_args(args, query);
      send_request(td_api::make_object<td_api::searchChatsOnServer>(query.query, query.limit));
    } else if (op == "scn") {
      string latitude;
      string longitude;
      get_args(args, latitude, longitude);
      send_request(td_api::make_object<td_api::searchChatsNearby>(as_location(latitude, longitude, string())));
    } else if (op == "sloc") {
      string latitude;
      string longitude;
      get_args(args, latitude, longitude);
      send_request(td_api::make_object<td_api::setLocation>(as_location(latitude, longitude, string())));
    } else if (op == "sco") {
      SearchQuery query;
      get_args(args, query);
      send_request(td_api::make_object<td_api::searchContacts>(query.query, query.limit));
    } else if (op == "arfc") {
      ChatId chat_id;
      get_args(args, chat_id);
      send_request(td_api::make_object<td_api::addRecentlyFoundChat>(chat_id));
    } else if (op == "rrfc") {
      ChatId chat_id;
      get_args(args, chat_id);
      send_request(td_api::make_object<td_api::removeRecentlyFoundChat>(chat_id));
    } else if (op == "crfcs") {
      send_request(td_api::make_object<td_api::clearRecentlyFoundChats>());
    } else if (op == "groc") {
      send_request(td_api::make_object<td_api::getRecentlyOpenedChats>(as_limit(args)));
    } else if (op == "gwpp") {
      send_request(td_api::make_object<td_api::getWebPagePreview>(as_caption(args)));
    } else if (op == "gwpiv") {
      string url;
      bool force_full;
      get_args(args, url, force_full);
      send_request(td_api::make_object<td_api::getWebPageInstantView>(url, force_full));
    } else if (op == "sppp") {
      int64 profile_photo_id;
      get_args(args, profile_photo_id);
      send_request(td_api::make_object<td_api::setProfilePhoto>(
          td_api::make_object<td_api::inputChatPhotoPrevious>(profile_photo_id)));
    } else if (op == "spp") {
      send_request(td_api::make_object<td_api::setProfilePhoto>(
          td_api::make_object<td_api::inputChatPhotoStatic>(as_input_file(args))));
    } else if (op == "sppa" || op == "sppv") {
      string animation;
      string main_frame_timestamp;
      get_args(args, animation, main_frame_timestamp);
      send_request(td_api::make_object<td_api::setProfilePhoto>(td_api::make_object<td_api::inputChatPhotoAnimation>(
          as_input_file(animation), to_double(main_frame_timestamp))));
    } else if (op == "sh") {
      const string &prefix = args;
      send_request(td_api::make_object<td_api::searchHashtags>(prefix, 10));
    } else if (op == "rrh") {
      const string &hashtag = args;
      send_request(td_api::make_object<td_api::removeRecentHashtag>(hashtag));
    } else if (op == "view" || op == "viewt") {
      ChatId chat_id;
      string message_thread_id;
      string message_ids;
      get_args(args, chat_id, message_ids);
      if (op == "viewt") {
        get_args(message_ids, message_thread_id, message_ids);
      }
      send_request(td_api::make_object<td_api::viewMessages>(chat_id, as_message_thread_id(message_thread_id),
                                                             as_message_ids(message_ids), true));
    } else if (op == "omc") {
      ChatId chat_id;
      MessageId message_id;
      get_args(args, chat_id, message_id);
      send_request(td_api::make_object<td_api::openMessageContent>(chat_id, message_id));
    } else if (op == "caem") {
      ChatId chat_id;
      MessageId message_id;
      get_args(args, chat_id, message_id);
      send_request(td_api::make_object<td_api::clickAnimatedEmojiMessage>(chat_id, message_id));
    } else if (op == "gilt") {
      const string &link = args;
      send_request(td_api::make_object<td_api::getInternalLinkType>(link));
    } else if (op == "geli") {
      const string &link = args;
      send_request(td_api::make_object<td_api::getExternalLinkInfo>(link));
    } else if (op == "gel" || op == "gelw") {
      const string &link = args;
      send_request(td_api::make_object<td_api::getExternalLink>(link, op == "gelw"));
    } else if (op == "racm") {
      ChatId chat_id;
      get_args(args, chat_id);
      send_request(td_api::make_object<td_api::readAllChatMentions>(chat_id));
    } else if (op == "racr") {
      ChatId chat_id;
      get_args(args, chat_id);
      send_request(td_api::make_object<td_api::readAllChatReactions>(chat_id));
    } else if (op == "tre") {
      send_request(td_api::make_object<td_api::testReturnError>(
          args.empty() ? nullptr : td_api::make_object<td_api::error>(-1, args)));
    } else if (op == "dpp") {
      int64 profile_photo_id;
      get_args(args, profile_photo_id);
      send_request(td_api::make_object<td_api::deleteProfilePhoto>(profile_photo_id));
    } else if (op == "gns") {
      int64 notification_sound_id;
      get_args(args, notification_sound_id);
      send_request(td_api::make_object<td_api::getSavedNotificationSound>(notification_sound_id));
    } else if (op == "gnss") {
      send_request(td_api::make_object<td_api::getSavedNotificationSounds>());
    } else if (op == "asns") {
      string sound;
      get_args(args, sound);
      send_request(td_api::make_object<td_api::addSavedNotificationSound>(as_input_file(sound)));
    } else if (op == "rns") {
      int64 notification_sound_id;
      get_args(args, notification_sound_id);
      send_request(td_api::make_object<td_api::removeSavedNotificationSound>(notification_sound_id));
    } else if (op == "gcnse" || op == "gcnses") {
      send_request(td_api::make_object<td_api::getChatNotificationSettingsExceptions>(
          get_notification_settings_scope(args), op == "gcnses"));
    } else if (op == "gsns") {
      send_request(td_api::make_object<td_api::getScopeNotificationSettings>(get_notification_settings_scope(args)));
    } else if (op == "scns" || op == "ssns") {
      string chat_id_or_scope;
      string mute_for;
      int64 sound_id;
      string show_preview;
      string disable_pinned_message_notifications;
      string disable_mention_notifications;
      get_args(args, chat_id_or_scope, mute_for, sound_id, show_preview, disable_pinned_message_notifications,
               disable_mention_notifications);
      if (op == "scns") {
        send_request(td_api::make_object<td_api::setChatNotificationSettings>(
            as_chat_id(chat_id_or_scope),
            td_api::make_object<td_api::chatNotificationSettings>(
                mute_for.empty(), to_integer<int32>(mute_for), sound_id == -1, sound_id, show_preview.empty(),
                as_bool(show_preview), disable_pinned_message_notifications.empty(),
                as_bool(disable_pinned_message_notifications), disable_mention_notifications.empty(),
                as_bool(disable_mention_notifications))));
      } else {
        send_request(td_api::make_object<td_api::setScopeNotificationSettings>(
            get_notification_settings_scope(chat_id_or_scope),
            td_api::make_object<td_api::scopeNotificationSettings>(
                to_integer<int32>(mute_for), sound_id, as_bool(show_preview),
                as_bool(disable_pinned_message_notifications), as_bool(disable_mention_notifications))));
      }
    } else if (op == "rans") {
      send_request(td_api::make_object<td_api::resetAllNotificationSettings>());
    } else if (op == "rn") {
      int32 group_id;
      string notification_ids;
      get_args(args, group_id, notification_ids);
      for (auto notification_id : to_integers<int32>(notification_ids)) {
        send_request(td_api::make_object<td_api::removeNotification>(group_id, notification_id));
      }
    } else if (op == "rng") {
      int32 group_id;
      int32 max_notification_id;
      get_args(args, group_id, max_notification_id);
      send_request(td_api::make_object<td_api::removeNotificationGroup>(group_id, max_notification_id));
    } else if (op == "rcab") {
      ChatId chat_id;
      get_args(args, chat_id);
      send_request(td_api::make_object<td_api::removeChatActionBar>(chat_id));
    } else if (op == "rc") {
      ChatId chat_id;
      string message_ids;
      string reason;
      string text;
      get_args(args, chat_id, message_ids, reason, text);
      send_request(td_api::make_object<td_api::reportChat>(chat_id, as_message_ids(message_ids),
                                                           get_chat_report_reason(reason), text));
    } else if (op == "rcp") {
      ChatId chat_id;
      FileId file_id;
      string reason;
      string text;
      get_args(args, chat_id, file_id, reason, text);
      send_request(
          td_api::make_object<td_api::reportChatPhoto>(chat_id, file_id, get_chat_report_reason(reason), text));
    } else if (op == "gcst") {
      ChatId chat_id;
      bool is_dark;
      get_args(args, chat_id, is_dark);
      send_request(td_api::make_object<td_api::getChatStatistics>(chat_id, is_dark));
    } else {
      op_not_found_count++;
    }

    if (op == "sgs") {
      ChatId chat_id;
      MessageId message_id;
      UserId user_id;
      int32 score;
      get_args(args, chat_id, message_id, user_id, score);
      send_request(td_api::make_object<td_api::setGameScore>(chat_id, message_id, true, user_id, score, true));
    } else if (op == "gghs") {
      ChatId chat_id;
      MessageId message_id;
      UserId user_id;
      get_args(args, chat_id, message_id, user_id);
      send_request(td_api::make_object<td_api::getGameHighScores>(chat_id, message_id, user_id));
    } else if (op == "gmst") {
      ChatId chat_id;
      MessageId message_id;
      bool is_dark;
      get_args(args, chat_id, message_id, is_dark);
      send_request(td_api::make_object<td_api::getMessageStatistics>(chat_id, message_id, is_dark));
    } else if (op == "gstg") {
      ChatId chat_id;
      string token;
      int64 x;
      get_args(args, chat_id, token, x);
      send_request(td_api::make_object<td_api::getStatisticalGraph>(chat_id, token, x));
    } else if (op == "hsa") {
      send_request(td_api::make_object<td_api::hideSuggestedAction>(as_suggested_action(args)));
    } else if (op == "glui" || op == "glu" || op == "glua") {
      ChatId chat_id;
      MessageId message_id;
      string button_id;
      get_args(args, chat_id, message_id, button_id);
      if (op == "glui") {
        send_request(td_api::make_object<td_api::getLoginUrlInfo>(chat_id, message_id, as_button_id(button_id)));
      } else {
        send_request(
            td_api::make_object<td_api::getLoginUrl>(chat_id, message_id, as_button_id(button_id), op == "glua"));
      }
    } else if (op == "rsgs") {
      string supergroup_id;
      string message_ids;
      get_args(args, supergroup_id, message_ids);
      send_request(td_api::make_object<td_api::reportSupergroupSpam>(as_supergroup_id(supergroup_id),
                                                                     as_message_ids(message_ids)));
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
      int32 port;
      string user;
      string password;
      if (op[0] == 'e') {
        get_args(args, proxy_id, args);
      }
      get_args(args, server, port, user, password);
      bool enable = op != "aproxy" && op != "editproxy";
      td_api::object_ptr<td_api::ProxyType> type;
      if (!user.empty() && password.empty()) {
        type = td_api::make_object<td_api::proxyTypeMtproto>(user);
      } else {
        if (port == 80 || port == 8080) {
          type = td_api::make_object<td_api::proxyTypeHttp>(user, password, op.back() != 'p');
        } else {
          type = td_api::make_object<td_api::proxyTypeSocks5>(user, password);
        }
      }
      if (op[0] == 'e') {
        send_request(
            td_api::make_object<td_api::editProxy>(as_proxy_id(proxy_id), server, port, enable, std::move(type)));
      } else if (op == "tproxy") {
        send_request(td_api::make_object<td_api::testProxy>(server, port, std::move(type), 2, 10.0));
      } else {
        send_request(td_api::make_object<td_api::addProxy>(server, port, enable, std::move(type)));
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
      auto inc_count = to_integer<uint32>(args);
      while (inc_count-- > 0) {
        cpu_counter_++;
      }
      auto r_cpu_stats = cpu_stat();
      if (r_cpu_stats.is_error()) {
        LOG(ERROR) << r_cpu_stats.error();
      } else {
        auto stats = r_cpu_stats.move_as_ok();
        LOG(ERROR) << cpu_counter_.load() << ", total ticks = " << stats.total_ticks_
                   << ", user ticks = " << stats.process_user_ticks_
                   << ", system ticks = " << stats.process_system_ticks_;
      }
    } else if (op[0] == 'v' && (op[1] == 'v' || is_digit(op[1]))) {
      int new_verbosity_level = op[1] == 'v' ? static_cast<int>(op.size()) : to_integer<int>(op.substr(1));
      SET_VERBOSITY_LEVEL(td::max(new_verbosity_level, VERBOSITY_NAME(DEBUG)));
      combined_log.set_first_verbosity_level(new_verbosity_level);
    } else if (op == "slse") {
      execute(td_api::make_object<td_api::setLogStream>(td_api::make_object<td_api::logStreamEmpty>()));
    } else if (op == "slsd") {
      execute(td_api::make_object<td_api::setLogStream>(td_api::make_object<td_api::logStreamDefault>()));
    } else if (op == "gls") {
      execute(td_api::make_object<td_api::getLogStream>());
    } else if (op == "slvl") {
      int32 new_verbosity_level;
      get_args(args, new_verbosity_level);
      execute(td_api::make_object<td_api::setLogVerbosityLevel>(new_verbosity_level));
    } else if (op == "glvl") {
      execute(td_api::make_object<td_api::getLogVerbosityLevel>());
    } else if (op == "gtags" || op == "glt") {
      execute(td_api::make_object<td_api::getLogTags>());
    } else if (op == "sltvl" || op == "sltvle" || op == "tag") {
      string tag;
      int32 level;
      get_args(args, tag, level);
      auto request = td_api::make_object<td_api::setLogTagVerbosityLevel>(tag, level);
      if (op == "sltvl") {
        send_request(std::move(request));
      } else {
        execute(std::move(request));
      }
    } else if (op == "gltvl" || op == "gltvle" || op == "gtag") {
      const string &tag = args;
      auto request = td_api::make_object<td_api::getLogTagVerbosityLevel>(tag);
      if (op == "gltvl") {
        send_request(std::move(request));
      } else {
        execute(std::move(request));
      }
    } else if (op == "alog" || op == "aloge") {
      int32 level;
      string text;
      get_args(args, level, text);
      auto request = td_api::make_object<td_api::addLogMessage>(level, text);
      if (op == "alog") {
        send_request(std::move(request));
      } else {
        execute(std::move(request));
      }
    } else if (op == "q" || op == "Quit") {
      quit();
    } else if (op == "dnq") {
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
  void loop() final {
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
      stop();
    }
  }

  void timeout_expired() final {
    if (close_flag_) {
      return;
    }

    for (auto it = pending_file_generations_.begin(); it != pending_file_generations_.end();) {
      auto left_size = it->size - it->local_size;
      CHECK(left_size > 0);
      if (it->part_size > left_size) {
        it->part_size = left_size;
      }
      BufferSlice block(narrow_cast<size_t>(it->part_size));
      FileFd::open(it->source, FileFd::Flags::Read).move_as_ok().pread(block.as_slice(), it->local_size).ensure();
      if (rand_bool()) {
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

  void notify() final {
    auto guard = scheduler_->get_send_guard();
    send_event_later(actor_id(), Event::yield());
  }

  void hangup_shared() final {
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
    int res = static_cast<unsigned char>(slice[0]);
    stdin_.input_buffer().confirm_read(1);
    return res;
  }

  FlatHashMap<int32, double> being_downloaded_files_;

  int64 my_id_ = 0;
  td_api::object_ptr<td_api::AuthorizationState> authorization_state_;
  string schedule_date_;
  string message_thread_id_;
  int64 opened_chat_id_ = 0;

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

  int32 group_call_source_ = Random::fast(1, 1000000000);

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

static void on_log_message(int verbosity_level, const char *message) {
  if (verbosity_level == 0) {
    std::cerr << "Fatal error: " << message;
  }
  std::cerr << "Log message: " << message;
}

void main(int argc, char **argv) {
  ExitGuard exit_guard;
  detail::ThreadIdGuard thread_id_guard;
  ignore_signal(SignalType::HangUp).ensure();
  ignore_signal(SignalType::Pipe).ensure();
  set_signal_handler(SignalType::Error, fail_signal).ensure();
  set_signal_handler(SignalType::Abort, fail_signal).ensure();
  ClientManager::set_log_message_callback(0, on_log_message);
  init_openssl_threads();

  std::locale new_locale("C");
  std::locale::global(new_locale);
  SCOPE_EXIT {
    std::locale::global(std::locale::classic());
    static NullLog null_log;
    log_interface = &null_log;
  };

  CliLog cli_log;

  FileLog file_log;
  TsLog ts_log(&file_log);

  combined_log.set_first(&cli_log);

  log_interface = &combined_log;

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
      combined_log.set_first(&ts_log);
    }
  });
  options.add_option('W', "", "Preload chat list", [&] { get_chat_list = true; });
  options.add_option('n', "disable-network", "Disable network", [&] { disable_network = true; });
  options.add_checked_option('\0', "api-id", "Set Telegram API ID", OptionParser::parse_integer(api_id));
  options.add_checked_option('\0', "api_id", "Set Telegram API ID", OptionParser::parse_integer(api_id));
  options.add_option('\0', "api-hash", "Set Telegram API hash", OptionParser::parse_string(api_hash));
  options.add_option('\0', "api_hash", "Set Telegram API hash", OptionParser::parse_string(api_hash));
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

  SET_VERBOSITY_LEVEL(td::max(new_verbosity_level, VERBOSITY_NAME(DEBUG)));
  combined_log.set_first_verbosity_level(new_verbosity_level);

  if (combined_log.get_first() == &cli_log) {
    file_log.init("tg_cli.log", 1000 << 20, false).ensure();
    file_log.lazy_rotate();
    combined_log.set_second(&ts_log);
    combined_log.set_second_verbosity_level(VERBOSITY_NAME(DEBUG));
  }

  {
    ConcurrentScheduler scheduler;
    scheduler.init(3);

    class CreateClient final : public Actor {
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
      void start_up() final {
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
