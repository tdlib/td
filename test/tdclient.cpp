//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "data.h"

#include "td/actor/actor.h"
#include "td/actor/PromiseFuture.h"

#include "td/telegram/Client.h"
#include "td/telegram/ClientActor.h"
#include "td/telegram/files/PartsManager.h"

#include "td/telegram/td_api.h"

#include "td/utils/base64.h"
#include "td/utils/BufferedFd.h"
#include "td/utils/common.h"
#include "td/utils/filesystem.h"
#include "td/utils/format.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/port/FileFd.h"
#include "td/utils/port/path.h"
#include "td/utils/port/thread.h"
#include "td/utils/Random.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"
#include "td/utils/tests.h"

#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <utility>

REGISTER_TESTS(tdclient);

namespace td {

template <class T>
static void check_td_error(T &result) {
  LOG_CHECK(result->get_id() != td_api::error::ID) << to_string(result);
}

class TestClient : public Actor {
 public:
  explicit TestClient(string name) : name_(std::move(name)) {
  }
  struct Update {
    uint64 id;
    tl_object_ptr<td_api::Object> object;
    Update(uint64 id, tl_object_ptr<td_api::Object> object) : id(id), object(std::move(object)) {
    }
  };
  class Listener {
   public:
    Listener() = default;
    Listener(const Listener &) = delete;
    Listener &operator=(const Listener &) = delete;
    Listener(Listener &&) = delete;
    Listener &operator=(Listener &&) = delete;
    virtual ~Listener() = default;
    virtual void start_listen(TestClient *client) {
    }
    virtual void stop_listen() {
    }
    virtual void on_update(std::shared_ptr<Update> update) = 0;
  };
  void close(Promise<> close_promise) {
    close_promise_ = std::move(close_promise);
    td_client_.reset();
  }

  unique_ptr<TdCallback> make_td_callback() {
    class TdCallbackImpl : public TdCallback {
     public:
      explicit TdCallbackImpl(ActorId<TestClient> client) : client_(client) {
      }
      void on_result(uint64 id, tl_object_ptr<td_api::Object> result) override {
        send_closure(client_, &TestClient::on_result, id, std::move(result));
      }
      void on_error(uint64 id, tl_object_ptr<td_api::error> error) override {
        send_closure(client_, &TestClient::on_error, id, std::move(error));
      }
      TdCallbackImpl(const TdCallbackImpl &) = delete;
      TdCallbackImpl &operator=(const TdCallbackImpl &) = delete;
      TdCallbackImpl(TdCallbackImpl &&) = delete;
      TdCallbackImpl &operator=(TdCallbackImpl &&) = delete;
      ~TdCallbackImpl() override {
        send_closure(client_, &TestClient::on_closed);
      }

     private:
      ActorId<TestClient> client_;
    };
    return make_unique<TdCallbackImpl>(actor_id(this));
  }

  void add_listener(unique_ptr<Listener> listener) {
    auto *ptr = listener.get();
    listeners_.push_back(std::move(listener));
    ptr->start_listen(this);
  }
  void remove_listener(Listener *listener) {
    pending_remove_.push_back(listener);
  }
  void do_pending_remove_listeners() {
    for (auto listener : pending_remove_) {
      do_remove_listener(listener);
    }
    pending_remove_.clear();
  }
  void do_remove_listener(Listener *listener) {
    for (size_t i = 0; i < listeners_.size(); i++) {
      if (listeners_[i].get() == listener) {
        listener->stop_listen();
        listeners_.erase(listeners_.begin() + i);
        break;
      }
    }
  }

  void on_result(uint64 id, tl_object_ptr<td_api::Object> result) {
    on_update(std::make_shared<Update>(id, std::move(result)));
  }
  void on_error(uint64 id, tl_object_ptr<td_api::error> error) {
    on_update(std::make_shared<Update>(id, std::move(error)));
  }
  void on_update(std::shared_ptr<Update> update) {
    for (auto &listener : listeners_) {
      listener->on_update(update);
    }
    do_pending_remove_listeners();
  }

  void on_closed() {
    stop();
  }

  void start_up() override {
    rmrf(name_).ignore();
    auto old_context = set_context(std::make_shared<td::ActorContext>());
    set_tag(name_);
    LOG(INFO) << "START UP!";

    td_client_ = create_actor<ClientActor>("Td-proxy", make_td_callback());
  }

  ActorOwn<ClientActor> td_client_;

  string name_;

 private:
  std::vector<unique_ptr<Listener>> listeners_;
  std::vector<Listener *> pending_remove_;

  Promise<> close_promise_;
};

class Task : public TestClient::Listener {
 public:
  void on_update(std::shared_ptr<TestClient::Update> update) override {
    auto it = sent_queries_.find(update->id);
    if (it != sent_queries_.end()) {
      it->second(std::move(update->object));
      sent_queries_.erase(it);
    }
    process_update(update);
  }
  void start_listen(TestClient *client) override {
    client_ = client;
    start_up();
  }
  virtual void process_update(std::shared_ptr<TestClient::Update> update) {
  }

  template <class F>
  void send_query(tl_object_ptr<td_api::Function> function, F callback) {
    auto id = current_query_id_++;
    sent_queries_[id] = std::forward<F>(callback);
    send_closure(client_->td_client_, &ClientActor::request, id, std::move(function));
  }

 protected:
  std::map<uint64, std::function<void(tl_object_ptr<td_api::Object>)>> sent_queries_;
  TestClient *client_ = nullptr;
  uint64 current_query_id_ = 1;

  virtual void start_up() {
  }
  void stop() {
    client_->remove_listener(this);
  }
};

class DoAuthentication : public Task {
 public:
  DoAuthentication(string name, string phone, string code, Promise<> promise)
      : name_(std::move(name)), phone_(std::move(phone)), code_(std::move(code)), promise_(std::move(promise)) {
  }
  void start_up() override {
    send_query(make_tl_object<td_api::getAuthorizationState>(),
               [this](auto res) { this->process_authorization_state(std::move(res)); });
  }
  void process_authorization_state(tl_object_ptr<td_api::Object> authorization_state) {
    start_flag_ = true;
    tl_object_ptr<td_api::Function> function;
    switch (authorization_state->get_id()) {
      case td_api::authorizationStateWaitEncryptionKey::ID:
        function = make_tl_object<td_api::checkDatabaseEncryptionKey>();
        break;
      case td_api::authorizationStateWaitPhoneNumber::ID:
        function = make_tl_object<td_api::setAuthenticationPhoneNumber>(phone_, nullptr);
        break;
      case td_api::authorizationStateWaitCode::ID:
        function = make_tl_object<td_api::checkAuthenticationCode>(code_);
        break;
      case td_api::authorizationStateWaitRegistration::ID:
        function = make_tl_object<td_api::registerUser>(name_, "");
        break;
      case td_api::authorizationStateWaitTdlibParameters::ID: {
        auto parameters = td_api::make_object<td_api::tdlibParameters>();
        parameters->use_test_dc_ = true;
        parameters->database_directory_ = name_ + TD_DIR_SLASH;
        parameters->use_message_database_ = true;
        parameters->use_secret_chats_ = true;
        parameters->api_id_ = 94575;
        parameters->api_hash_ = "a3406de8d171bb422bb6ddf3bbd800e2";
        parameters->system_language_code_ = "en";
        parameters->device_model_ = "Desktop";
        parameters->application_version_ = "tdclient-test";
        parameters->ignore_file_names_ = false;
        parameters->enable_storage_optimizer_ = true;
        function = td_api::make_object<td_api::setTdlibParameters>(std::move(parameters));
        break;
      }
      case td_api::authorizationStateReady::ID:
        on_authorization_ready();
        return;
      default:
        LOG(ERROR) << "Unexpected authorization state " << to_string(authorization_state);
        UNREACHABLE();
    }
    send_query(std::move(function), [](auto res) { LOG_CHECK(res->get_id() == td_api::ok::ID) << to_string(res); });
  }
  void on_authorization_ready() {
    LOG(INFO) << "GOT AUTHORIZED";
    stop();
  }

 private:
  string name_;
  string phone_;
  string code_;
  Promise<> promise_;
  bool start_flag_{false};
  void process_update(std::shared_ptr<TestClient::Update> update) override {
    if (!start_flag_) {
      return;
    }
    if (!update->object) {
      return;
    }
    if (update->object->get_id() == td_api::updateAuthorizationState::ID) {
      auto o = std::move(update->object);
      process_authorization_state(std::move(static_cast<td_api::updateAuthorizationState &>(*o).authorization_state_));
    }
  }
};

class SetUsername : public Task {
 public:
  SetUsername(string username, Promise<> promise) : username_(std::move(username)), promise_(std::move(promise)) {
  }

 private:
  string username_;
  Promise<> promise_;
  int32 self_id_ = 0;
  string tag_;

  void start_up() override {
    send_query(make_tl_object<td_api::getMe>(), [this](auto res) { this->process_me_user(std::move(res)); });
  }

  void process_me_user(tl_object_ptr<td_api::Object> res) {
    CHECK(res->get_id() == td_api::user::ID);
    auto user = move_tl_object_as<td_api::user>(res);
    self_id_ = user->id_;
    if (user->username_ != username_) {
      LOG(INFO) << "SET USERNAME: " << username_;
      send_query(make_tl_object<td_api::setUsername>(username_), [this](auto res) {
        CHECK(res->get_id() == td_api::ok::ID);
        this->send_self_message();
      });
    } else {
      send_self_message();
    }
  }
  void send_self_message() {
    tag_ = PSTRING() << format::as_hex(Random::secure_int64());

    send_query(make_tl_object<td_api::createPrivateChat>(self_id_, false), [this](auto res) {
      CHECK(res->get_id() == td_api::chat::ID);
      auto chat = move_tl_object_as<td_api::chat>(res);
      this->send_query(
          make_tl_object<td_api::sendMessage>(
              chat->id_, 0, nullptr, nullptr,
              make_tl_object<td_api::inputMessageText>(
                  make_tl_object<td_api::formattedText>(PSTRING() << tag_ << " INIT", Auto()), false, false)),
          [](auto res) {});
    });
  }

  void process_update(std::shared_ptr<TestClient::Update> update) override {
    if (!update->object) {
      return;
    }
    if (update->object->get_id() == td_api::updateMessageSendSucceeded::ID) {
      auto updateNewMessage = move_tl_object_as<td_api::updateMessageSendSucceeded>(update->object);
      auto &message = updateNewMessage->message_;
      if (message->content_->get_id() == td_api::messageText::ID) {
        auto messageText = move_tl_object_as<td_api::messageText>(message->content_);
        auto text = messageText->text_->text_;
        if (text.substr(0, tag_.size()) == tag_) {
          LOG(INFO) << "GOT SELF MESSAGE";
          return stop();
        }
      }
    }
  }
};

class CheckTestA : public Task {
 public:
  CheckTestA(string tag, Promise<> promise) : tag_(std::move(tag)), promise_(std::move(promise)) {
  }

 private:
  string tag_;
  Promise<> promise_;
  string previous_text_;
  int cnt_ = 20;
  void process_update(std::shared_ptr<TestClient::Update> update) override {
    if (update->object->get_id() == td_api::updateNewMessage::ID) {
      auto updateNewMessage = move_tl_object_as<td_api::updateNewMessage>(update->object);
      auto &message = updateNewMessage->message_;
      if (message->content_->get_id() == td_api::messageText::ID) {
        auto messageText = move_tl_object_as<td_api::messageText>(message->content_);
        auto text = messageText->text_->text_;
        if (text.substr(0, tag_.size()) == tag_) {
          LOG_CHECK(text > previous_text_) << tag("now", text) << tag("previous", previous_text_);
          previous_text_ = text;
          cnt_--;
          LOG(INFO) << "GOT " << tag("text", text) << tag("left", cnt_);
          if (cnt_ == 0) {
            return stop();
          }
        }
      }
    }
  }
};

class TestA : public Task {
 public:
  TestA(string tag, string username) : tag_(std::move(tag)), username_(std::move(username)) {
  }
  void start_up() override {
    send_query(make_tl_object<td_api::searchPublicChat>(username_), [this](auto res) {
      CHECK(res->get_id() == td_api::chat::ID);
      auto chat = move_tl_object_as<td_api::chat>(res);
      for (int i = 0; i < 20; i++) {
        this->send_query(make_tl_object<td_api::sendMessage>(
                             chat->id_, 0, nullptr, nullptr,
                             make_tl_object<td_api::inputMessageText>(
                                 make_tl_object<td_api::formattedText>(PSTRING() << tag_ << " " << (1000 + i), Auto()),
                                 false, false)),
                         [&](auto res) { this->stop(); });
      }
    });
  }

 private:
  string tag_;
  string username_;
};

class TestSecretChat : public Task {
 public:
  TestSecretChat(string tag, string username) : tag_(std::move(tag)), username_(std::move(username)) {
  }

  void start_up() override {
    auto f = [this](auto res) {
      CHECK(res->get_id() == td_api::chat::ID);
      auto chat = move_tl_object_as<td_api::chat>(res);
      this->chat_id_ = chat->id_;
      this->secret_chat_id_ = move_tl_object_as<td_api::chatTypeSecret>(chat->type_)->secret_chat_id_;
    };
    send_query(make_tl_object<td_api::searchPublicChat>(username_), [this, f = std::move(f)](auto res) mutable {
      CHECK(res->get_id() == td_api::chat::ID);
      auto chat = move_tl_object_as<td_api::chat>(res);
      CHECK(chat->type_->get_id() == td_api::chatTypePrivate::ID);
      auto info = move_tl_object_as<td_api::chatTypePrivate>(chat->type_);
      this->send_query(make_tl_object<td_api::createNewSecretChat>(info->user_id_), std::move(f));
    });
  }

  void process_update(std::shared_ptr<TestClient::Update> update) override {
    if (!update->object) {
      return;
    }
    if (update->object->get_id() == td_api::updateSecretChat::ID) {
      auto update_secret_chat = move_tl_object_as<td_api::updateSecretChat>(update->object);
      if (update_secret_chat->secret_chat_->id_ != secret_chat_id_ ||
          update_secret_chat->secret_chat_->state_->get_id() != td_api::secretChatStateReady::ID) {
        return;
      }
      LOG(INFO) << "SEND ENCRYPTED MESSAGES";
      for (int i = 0; i < 20; i++) {
        send_query(make_tl_object<td_api::sendMessage>(
                       chat_id_, 0, nullptr, nullptr,
                       make_tl_object<td_api::inputMessageText>(
                           make_tl_object<td_api::formattedText>(PSTRING() << tag_ << " " << (1000 + i), Auto()), false,
                           false)),
                   [](auto res) {});
      }
    }
  }

 private:
  string tag_;
  string username_;
  int64 secret_chat_id_ = 0;
  int64 chat_id_ = 0;
};

class TestFileGenerated : public Task {
 public:
  TestFileGenerated(string tag, string username) : tag_(std::move(tag)), username_(std::move(username)) {
  }

  void start_up() override {
  }

  void process_update(std::shared_ptr<TestClient::Update> update) override {
    if (!update->object) {
      return;
    }
    if (update->object->get_id() == td_api::updateNewMessage::ID) {
      auto updateNewMessage = move_tl_object_as<td_api::updateNewMessage>(update->object);
      auto &message = updateNewMessage->message_;
      chat_id_ = message->chat_id_;
      if (message->content_->get_id() == td_api::messageText::ID) {
        auto messageText = move_tl_object_as<td_api::messageText>(message->content_);
        auto text = messageText->text_->text_;
        if (text.substr(0, tag_.size()) == tag_) {
          if (text.substr(tag_.size() + 1) == "ONE_FILE") {
            return one_file();
          }
        }
      }
    } else if (update->object->get_id() == td_api::updateFileGenerationStart::ID) {
      auto info = move_tl_object_as<td_api::updateFileGenerationStart>(update->object);
      generate_file(info->generation_id_, info->original_path_, info->destination_path_, info->conversion_);
    } else if (update->object->get_id() == td_api::updateFile::ID) {
      auto file = move_tl_object_as<td_api::updateFile>(update->object);
      LOG(INFO) << to_string(file);
    }
  }
  void one_file() {
    LOG(ERROR) << "Start ONE_FILE test";
    string file_path = string("test_documents") + TD_DIR_SLASH + "a.txt";
    mkpath(file_path).ensure();
    auto raw_file =
        FileFd::open(file_path, FileFd::Flags::Create | FileFd::Flags::Truncate | FileFd::Flags::Write).move_as_ok();
    auto file = BufferedFd<FileFd>(std::move(raw_file));
    for (int i = 1; i < 100000; i++) {
      file.write(PSLICE() << i << "\n").ensure();
    }
    file.flush_write().ensure();  // important
    file.close();
    this->send_query(make_tl_object<td_api::sendMessage>(
                         chat_id_, 0, nullptr, nullptr,
                         make_tl_object<td_api::inputMessageDocument>(
                             make_tl_object<td_api::inputFileGenerated>(file_path, "square", 0),
                             make_tl_object<td_api::inputThumbnail>(
                                 make_tl_object<td_api::inputFileGenerated>(file_path, "thumbnail", 0), 0, 0),
                             true, make_tl_object<td_api::formattedText>(tag_, Auto()))),
                     [](auto res) { check_td_error(res); });

    this->send_query(
        make_tl_object<td_api::sendMessage>(chat_id_, 0, nullptr, nullptr,
                                            make_tl_object<td_api::inputMessageDocument>(
                                                make_tl_object<td_api::inputFileGenerated>(file_path, "square", 0),
                                                nullptr, true, make_tl_object<td_api::formattedText>(tag_, Auto()))),
        [](auto res) { check_td_error(res); });
  }

  friend class GenerateFile;
  class GenerateFile : public Actor {
   public:
    GenerateFile(Task *parent, int64 id, string original_path, string destination_path, string conversion)
        : parent_(parent)
        , id_(id)
        , original_path_(std::move(original_path))
        , destination_path_(std::move(destination_path))
        , conversion_(std::move(conversion)) {
    }

   private:
    Task *parent_;
    int64 id_;
    string original_path_;
    string destination_path_;
    string conversion_;

    FILE *from = nullptr;
    FILE *to = nullptr;

    void start_up() override {
      from = std::fopen(original_path_.c_str(), "rb");
      CHECK(from);
      to = std::fopen(destination_path_.c_str(), "wb");
      CHECK(to);
      yield();
    }
    void loop() override {
      int cnt = 0;
      while (true) {
        uint32 x;
        auto r = std::fscanf(from, "%u", &x);
        if (r != 1) {
          return stop();
        }
        std::fprintf(to, "%u\n", x * x);
        if (++cnt >= 10000) {
          break;
        }
      }
      auto ready = std::ftell(to);
      LOG(ERROR) << "READY: " << ready;
      parent_->send_query(make_tl_object<td_api::setFileGenerationProgress>(
                              id_, 1039823 /*yeah, exact size of this file*/, narrow_cast<int32>(ready)),
                          [](auto result) { check_td_error(result); });
      set_timeout_in(0.02);
    }
    void tear_down() override {
      std::fclose(from);
      std::fclose(to);
      parent_->send_query(make_tl_object<td_api::finishFileGeneration>(id_, nullptr),
                          [](auto result) { check_td_error(result); });
    }
  };
  void generate_file(int64 id, string original_path, string destination_path, string conversion) {
    LOG(ERROR) << "Generate file " << tag("id", id) << tag("original_path", original_path)
               << tag("destination_path", destination_path) << tag("conversion", conversion);
    if (conversion == "square") {
      create_actor<GenerateFile>("GenerateFile", this, id, original_path, destination_path, conversion).release();
    } else if (conversion == "thumbnail") {
      write_file(destination_path, base64url_decode(Slice(thumbnail, thumbnail_size)).ok()).ensure();
      this->send_query(make_tl_object<td_api::finishFileGeneration>(id, nullptr),
                       [](auto result) { check_td_error(result); });
    } else {
      LOG(FATAL) << "Unknown " << tag("conversion", conversion);
    }
  }

 private:
  string tag_;
  string username_;
  int64 chat_id_ = 0;
};

class CheckTestC : public Task {
 public:
  CheckTestC(string username, string tag, Promise<> promise)
      : username_(std::move(username)), tag_(std::move(tag)), promise_(std::move(promise)) {
  }

  void start_up() override {
    send_query(make_tl_object<td_api::searchPublicChat>(username_), [this](auto res) {
      CHECK(res->get_id() == td_api::chat::ID);
      auto chat = move_tl_object_as<td_api::chat>(res);
      chat_id_ = chat->id_;
      this->one_file();
    });
  }

 private:
  string username_;
  string tag_;
  Promise<> promise_;
  int64 chat_id_ = 0;

  void one_file() {
    this->send_query(
        make_tl_object<td_api::sendMessage>(
            chat_id_, 0, nullptr, nullptr,
            make_tl_object<td_api::inputMessageText>(
                make_tl_object<td_api::formattedText>(PSTRING() << tag_ << " ONE_FILE", Auto()), false, false)),
        [](auto res) { check_td_error(res); });
  }

  void process_update(std::shared_ptr<TestClient::Update> update) override {
    if (!update->object) {
      return;
    }
    if (update->object->get_id() == td_api::updateNewMessage::ID) {
      auto updateNewMessage = move_tl_object_as<td_api::updateNewMessage>(update->object);
      auto &message = updateNewMessage->message_;
      if (message->content_->get_id() == td_api::messageDocument::ID) {
        auto messageDocument = move_tl_object_as<td_api::messageDocument>(message->content_);
        auto text = messageDocument->caption_->text_;
        if (text.substr(0, tag_.size()) == tag_) {
          file_id_to_check_ = messageDocument->document_->document_->id_;
          LOG(ERROR) << "GOT FILE " << to_string(messageDocument->document_->document_);
          this->send_query(make_tl_object<td_api::downloadFile>(file_id_to_check_, 1, 0, 0, false),
                           [](auto res) { check_td_error(res); });
        }
      }
    } else if (update->object->get_id() == td_api::updateFile::ID) {
      auto updateFile = move_tl_object_as<td_api::updateFile>(update->object);
      if (updateFile->file_->id_ == file_id_to_check_ && (updateFile->file_->local_->is_downloading_completed_)) {
        check_file(updateFile->file_->local_->path_);
      }
    }
  }

  void check_file(CSlice path) {
    FILE *from = std::fopen(path.c_str(), "rb");
    CHECK(from);
    uint32 x;
    uint32 y = 1;
    while (std::fscanf(from, "%u", &x) == 1) {
      CHECK(x == y * y);
      y++;
    }
    std::fclose(from);
    stop();
  }
  int32 file_id_to_check_ = 0;
};

class LoginTestActor : public Actor {
 public:
  explicit LoginTestActor(Status *status) : status_(status) {
    *status_ = Status::OK();
  }

 private:
  Status *status_;
  ActorOwn<TestClient> alice_;
  ActorOwn<TestClient> bob_;

  string alice_phone_ = "9996636437";
  string bob_phone_ = "9996636438";
  string alice_username_ = "alice_" + alice_phone_;
  string bob_username_ = "bob_" + bob_phone_;

  string stage_name_;

  void begin_stage(string stage_name, double timeout) {
    LOG(WARNING) << "Begin stage '" << stage_name << "'";
    stage_name_ = std::move(stage_name);
    set_timeout_in(timeout);
  }

  void start_up() override {
    begin_stage("Logging in", 160);
    alice_ = create_actor<TestClient>("AliceClient", "alice");
    bob_ = create_actor<TestClient>("BobClient", "bob");

    send_closure(alice_, &TestClient::add_listener,
                 td::make_unique<DoAuthentication>(
                     "alice", alice_phone_, "33333",
                     PromiseCreator::event(self_closure(this, &LoginTestActor::start_up_fence_dec))));

    send_closure(bob_, &TestClient::add_listener,
                 td::make_unique<DoAuthentication>(
                     "bob", bob_phone_, "33333",
                     PromiseCreator::event(self_closure(this, &LoginTestActor::start_up_fence_dec))));
  }

  int start_up_fence_ = 3;
  void start_up_fence_dec() {
    --start_up_fence_;
    if (start_up_fence_ == 0) {
      init();
    } else if (start_up_fence_ == 1) {
      return init();
      class WaitActor : public Actor {
       public:
        WaitActor(double timeout, Promise<> promise) : timeout_(timeout), promise_(std::move(promise)) {
        }
        void start_up() override {
          set_timeout_in(timeout_);
        }
        void timeout_expired() override {
          stop();
        }

       private:
        double timeout_;
        Promise<> promise_;
      };
      create_actor<WaitActor>("WaitActor", 2,
                              PromiseCreator::event(self_closure(this, &LoginTestActor::start_up_fence_dec)))
          .release();
    }
  }

  void init() {
    send_closure(alice_, &TestClient::add_listener,
                 td::make_unique<SetUsername>(
                     alice_username_, PromiseCreator::event(self_closure(this, &LoginTestActor::init_fence_dec))));
    send_closure(bob_, &TestClient::add_listener,
                 td::make_unique<SetUsername>(
                     bob_username_, PromiseCreator::event(self_closure(this, &LoginTestActor::init_fence_dec))));
  }

  int init_fence_ = 2;
  void init_fence_dec() {
    if (--init_fence_ == 0) {
      test_a();
    }
  }

  int32 test_a_fence_ = 2;
  void test_a_fence() {
    if (--test_a_fence_ == 0) {
      test_b();
    }
  }

  void test_a() {
    begin_stage("Ready to create chats", 80);
    string alice_tag = PSTRING() << format::as_hex(Random::secure_int64());
    string bob_tag = PSTRING() << format::as_hex(Random::secure_int64());

    send_closure(bob_, &TestClient::add_listener,
                 td::make_unique<CheckTestA>(alice_tag,
                                             PromiseCreator::event(self_closure(this, &LoginTestActor::test_a_fence))));
    send_closure(
        alice_, &TestClient::add_listener,
        td::make_unique<CheckTestA>(bob_tag, PromiseCreator::event(self_closure(this, &LoginTestActor::test_a_fence))));

    send_closure(alice_, &TestClient::add_listener, td::make_unique<TestA>(alice_tag, bob_username_));
    send_closure(bob_, &TestClient::add_listener, td::make_unique<TestA>(bob_tag, alice_username_));
    // send_closure(alice_, &TestClient::add_listener, td::make_unique<TestChat>(bob_username_));
  }

  void timeout_expired() override {
    LOG(FATAL) << "Timeout expired in stage '" << stage_name_ << "'";
  }

  int32 test_b_fence_ = 1;
  void test_b_fence() {
    if (--test_b_fence_ == 0) {
      test_c();
    }
  }

  int32 test_c_fence_ = 1;
  void test_c_fence() {
    if (--test_c_fence_ == 0) {
      finish();
    }
  }

  void test_b() {
    begin_stage("Create secret chat", 40);
    string tag = PSTRING() << format::as_hex(Random::secure_int64());

    send_closure(
        bob_, &TestClient::add_listener,
        td::make_unique<CheckTestA>(tag, PromiseCreator::event(self_closure(this, &LoginTestActor::test_b_fence))));
    send_closure(alice_, &TestClient::add_listener, td::make_unique<TestSecretChat>(tag, bob_username_));
  }

  void test_c() {
    begin_stage("Send generated file", 240);
    string tag = PSTRING() << format::as_hex(Random::secure_int64());

    send_closure(bob_, &TestClient::add_listener,
                 td::make_unique<CheckTestC>(alice_username_, tag,
                                             PromiseCreator::event(self_closure(this, &LoginTestActor::test_c_fence))));
    send_closure(alice_, &TestClient::add_listener, td::make_unique<TestFileGenerated>(tag, bob_username_));
  }

  int32 finish_fence_ = 2;
  void finish_fence() {
    finish_fence_--;
    if (finish_fence_ == 0) {
      Scheduler::instance()->finish();
      stop();
    }
  }
  void finish() {
    send_closure(alice_, &TestClient::close, PromiseCreator::event(self_closure(this, &LoginTestActor::finish_fence)));
    send_closure(bob_, &TestClient::close, PromiseCreator::event(self_closure(this, &LoginTestActor::finish_fence)));
  }
};

class Tdclient_login : public Test {
 public:
  using Test::Test;
  bool step() final {
    if (!is_inited_) {
      SET_VERBOSITY_LEVEL(VERBOSITY_NAME(DEBUG) + 2);
      sched_.init(4);
      sched_.create_actor_unsafe<LoginTestActor>(0, "LoginTestActor", &result_).release();
      sched_.start();
      is_inited_ = true;
    }

    bool ret = sched_.run_main(10);
    if (ret) {
      return true;
    }
    sched_.finish();
    if (result_.is_error()) {
      LOG(ERROR) << result_;
    }
    ASSERT_TRUE(result_.is_ok());
    return false;
  }

 private:
  bool is_inited_ = false;
  ConcurrentScheduler sched_;
  Status result_;
};
//RegisterTest<Tdclient_login> Tdclient_login("Tdclient_login");

TEST(Client, Simple) {
  td::Client client;
  //client.execute({1, td::td_api::make_object<td::td_api::setLogTagVerbosityLevel>("actor", 1)});
  client.send({3, td::make_tl_object<td::td_api::testSquareInt>(3)});
  while (true) {
    auto result = client.receive(10);
    if (result.id == 3) {
      auto test_int = td::td_api::move_object_as<td::td_api::testInt>(result.object);
      ASSERT_EQ(test_int->value_, 9);
      break;
    }
  }
}

TEST(Client, SimpleMulti) {
  std::vector<td::Client> clients(50);
  //for (auto &client : clients) {
  //client.execute({1, td::td_api::make_object<td::td_api::setLogTagVerbosityLevel>("td_requests", 1)});
  //}

  for (auto &client : clients) {
    client.send({3, td::make_tl_object<td::td_api::testSquareInt>(3)});
  }

  for (auto &client : clients) {
    while (true) {
      auto result = client.receive(10);
      if (result.id == 3) {
        auto test_int = td::td_api::move_object_as<td::td_api::testInt>(result.object);
        ASSERT_EQ(test_int->value_, 9);
        break;
      }
    }
  }
}

#if !TD_THREAD_UNSUPPORTED
TEST(Client, Multi) {
  td::vector<td::thread> threads;
  for (int i = 0; i < 4; i++) {
    threads.emplace_back([] {
      for (int i = 0; i < 1000; i++) {
        td::Client client;
        client.send({3, td::make_tl_object<td::td_api::testSquareInt>(3)});
        while (true) {
          auto result = client.receive(10);
          if (result.id == 3) {
            break;
          }
        }
      }
    });
  }

  for (auto &thread : threads) {
    thread.join();
  }
}

TEST(Client, MultiNew) {
  td::vector<td::thread> threads;
  td::MultiClient client;
  int threads_n = 4;
  int clients_n = 1000;
  for (int i = 0; i < threads_n; i++) {
    threads.emplace_back([&] {
      for (int i = 0; i < clients_n; i++) {
        auto id = client.create_client();
        client.send(id, 3, td::make_tl_object<td::td_api::testSquareInt>(3));
      }
    });
  }
  for (auto &thread : threads) {
    thread.join();
  }

  std::set<int32> ids;
  while (ids.size() != static_cast<size_t>(threads_n) * clients_n) {
    auto event = client.receive(10);
    if (event.client_id != 0 && event.id == 3) {
      ids.insert(event.client_id);
    }
  }
}
#endif

TEST(PartsManager, hands) {
  //Status init(int64 size, int64 expected_size, bool is_size_final, size_t part_size,
  //const std::vector<int> &ready_parts, bool use_part_count_limit) TD_WARN_UNUSED_RESULT;
  {
    PartsManager pm;
    pm.init(0, 100000, false, 10, {0, 1, 2}, false, true).ensure_error();
    //pm.set_known_prefix(0, false).ensure();
  }
  {
    PartsManager pm;
    pm.init(1, 100000, true, 10, {0, 1, 2}, false, true).ensure_error();
  }
}

}  // namespace td
