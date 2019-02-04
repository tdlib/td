//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2019
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/utils/tests.h"

#include "td/actor/actor.h"
#include "td/actor/PromiseFuture.h"

#include "td/mtproto/crypto.h"
#include "td/mtproto/Handshake.h"
#include "td/mtproto/HandshakeActor.h"
#include "td/mtproto/HandshakeConnection.h"
#include "td/mtproto/PingConnection.h"
#include "td/mtproto/RawConnection.h"

#include "td/net/GetHostByNameActor.h"
#include "td/net/Socks5.h"
#include "td/net/TransparentProxy.h"

#include "td/telegram/ConfigManager.h"
#include "td/telegram/net/DcId.h"
#include "td/telegram/net/PublicRsaKeyShared.h"

#include "td/utils/logging.h"
#include "td/utils/port/IPAddress.h"
#include "td/utils/port/SocketFd.h"
#include "td/utils/Status.h"

REGISTER_TESTS(mtproto);

using namespace td;

TEST(Mtproto, GetHostByNameActor) {
  SET_VERBOSITY_LEVEL(VERBOSITY_NAME(WARNING));
  ConcurrentScheduler sched;
  int threads_n = 1;
  sched.init(threads_n);

  int cnt = 1;
  vector<ActorOwn<GetHostByNameActor>> actors;
  {
    auto guard = sched.get_main_guard();

    auto run = [&](ActorId<GetHostByNameActor> actor_id, string host, bool prefer_ipv6) {
      auto promise = PromiseCreator::lambda([&cnt, &actors, num = cnt, host](Result<IPAddress> r_ip_address) {
        if (r_ip_address.is_ok()) {
          LOG(WARNING) << num << " \"" << host << "\" " << r_ip_address.ok();
        } else {
          LOG(ERROR) << num << " \"" << host << "\" " << r_ip_address.error();
        }
        if (--cnt == 0) {
          actors.clear();
          Scheduler::instance()->finish();
        }
      });
      cnt++;
      send_closure(actor_id, &GetHostByNameActor::run, host, 443, prefer_ipv6, std::move(promise));
    };

    std::vector<std::string> hosts = {
        "127.0.0.2", "1.1.1.1", "localhost", "web.telegram.org", "web.telegram.org", "москва.рф", "", "%", " ", "a"};
    for (auto types : {vector<GetHostByNameActor::ResolverType>{GetHostByNameActor::ResolverType::Native},
                       vector<GetHostByNameActor::ResolverType>{GetHostByNameActor::ResolverType::Google},
                       vector<GetHostByNameActor::ResolverType>{GetHostByNameActor::ResolverType::Google,
                                                                GetHostByNameActor::ResolverType::Google,
                                                                GetHostByNameActor::ResolverType::Native}}) {
      GetHostByNameActor::Options options;
      options.resolver_types = types;
      options.scheduler_id = threads_n;

      auto actor = create_actor<GetHostByNameActor>("GetHostByNameActor", std::move(options));
      auto actor_id = actor.get();
      actors.push_back(std::move(actor));

      for (auto host : hosts) {
        for (auto prefer_ipv6 : {false, true}) {
          run(actor_id, host, prefer_ipv6);
        }
      }
    }
  }
  cnt--;
  sched.start();
  while (sched.run_main(10)) {
    // empty
  }
  sched.finish();
}

TEST(Mtproto, config) {
  ConcurrentScheduler sched;
  int threads_n = 0;
  sched.init(threads_n);

  int cnt = 1;
  {
    auto guard = sched.get_main_guard();

    auto run = [&](auto &func, bool is_test) {
      auto promise = PromiseCreator::lambda([&, num = cnt](Result<SimpleConfig> r_simple_config) {
        if (r_simple_config.is_ok()) {
          LOG(WARNING) << num << " " << to_string(r_simple_config.ok());
        } else {
          LOG(ERROR) << num << " " << r_simple_config.error();
        }
        if (--cnt == 0) {
          Scheduler::instance()->finish();
        }
      });
      cnt++;
      func(std::move(promise), nullptr, is_test, -1).release();
    };

    run(get_simple_config_azure, false);
    run(get_simple_config_google_dns, false);
    run(get_simple_config_azure, true);
    run(get_simple_config_google_dns, true);
  }
  cnt--;
  sched.start();
  while (sched.run_main(10)) {
    // empty;
  }
  sched.finish();
}

TEST(Mtproto, encrypted_config) {
  string data =
      "   hO//tt \b\n\tiwPVovorKtIYtQ8y2ik7CqfJiJ4pJOCLRa4fBmNPixuRPXnBFF/3mTAAZoSyHq4SNylGHz0Cv1/"
      "FnWWdEV+BPJeOTk+ARHcNkuJBt0CqnfcVCoDOpKqGyq0U31s2MOpQvHgAG+Tlpg02syuH0E4dCGRw5CbJPARiynteb9y5fT5x/"
      "kmdp6BMR5tWQSQF0liH16zLh8BDSIdiMsikdcwnAvBwdNhRqQBqGx9MTh62MDmlebjtczE9Gz0z5cscUO2yhzGdphgIy6SP+"
      "bwaqLWYF0XdPGjKLMUEJW+rou6fbL1t/EUXPtU0XmQAnO0Fh86h+AqDMOe30N4qKrPQ==   ";
  auto config = decode_config(data).move_as_ok();
}

class TestPingActor : public Actor {
 public:
  TestPingActor(IPAddress ip_address, Status *result) : ip_address_(ip_address), result_(result) {
  }

 private:
  IPAddress ip_address_;
  unique_ptr<mtproto::PingConnection> ping_connection_;
  Status *result_;

  void start_up() override {
    ping_connection_ = make_unique<mtproto::PingConnection>(
        make_unique<mtproto::RawConnection>(SocketFd::open(ip_address_).move_as_ok(),
                                            mtproto::TransportType{mtproto::TransportType::Tcp, 0, ""}, nullptr),
        3);

    Scheduler::subscribe(ping_connection_->get_poll_info().extract_pollable_fd(this));
    set_timeout_in(10);
    yield();
  }
  void tear_down() override {
    Scheduler::unsubscribe_before_close(ping_connection_->get_poll_info().get_pollable_fd_ref());
    ping_connection_->close();
    Scheduler::instance()->finish();
  }

  void loop() override {
    auto status = ping_connection_->flush();
    if (status.is_error()) {
      *result_ = std::move(status);
      return stop();
    }
    if (ping_connection_->was_pong()) {
      LOG(INFO) << "GOT PONG";
      return stop();
    }
  }

  void timeout_expired() override {
    *result_ = Status::Error("Timeout expired");
    stop();
  }
};

static IPAddress get_default_ip_address() {
  IPAddress ip_address;
#if TD_EMSCRIPTEN
  ip_address.init_host_port("venus.web.telegram.org/apiws", 443).ensure();
#else
  ip_address.init_ipv4_port("149.154.167.40", 80).ensure();
#endif
  return ip_address;
}

static int32 get_default_dc_id() {
  return 10002;
}

class Mtproto_ping : public Test {
 public:
  using Test::Test;
  bool step() final {
    if (!is_inited_) {
      sched_.init(0);
      sched_.create_actor_unsafe<TestPingActor>(0, "Pinger", get_default_ip_address(), &result_).release();
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
    return false;
  }

 private:
  bool is_inited_ = false;
  ConcurrentScheduler sched_;
  Status result_;
};
RegisterTest<Mtproto_ping> mtproto_ping("Mtproto_ping");

class HandshakeContext : public mtproto::AuthKeyHandshakeContext {
 public:
  DhCallback *get_dh_callback() override {
    return nullptr;
  }
  PublicRsaKeyInterface *get_public_rsa_key_interface() override {
    return &public_rsa_key;
  }

 private:
  PublicRsaKeyShared public_rsa_key{DcId::empty(), false};
};

class HandshakeTestActor : public Actor {
 public:
  HandshakeTestActor(int32 dc_id, Status *result) : dc_id_(dc_id), result_(result) {
  }

 private:
  int32 dc_id_ = 0;
  Status *result_;
  bool wait_for_raw_connection_ = false;
  unique_ptr<mtproto::RawConnection> raw_connection_;
  bool wait_for_handshake_ = false;
  unique_ptr<mtproto::AuthKeyHandshake> handshake_;
  Status status_;
  bool wait_for_result_ = false;

  void tear_down() override {
    if (raw_connection_) {
      raw_connection_->close();
    }
    finish(Status::Error("Interrupted"));
  }
  void loop() override {
    if (!wait_for_raw_connection_ && !raw_connection_) {
      raw_connection_ =
          make_unique<mtproto::RawConnection>(SocketFd::open(get_default_ip_address()).move_as_ok(),
                                              mtproto::TransportType{mtproto::TransportType::Tcp, 0, ""}, nullptr);
    }
    if (!wait_for_handshake_ && !handshake_) {
      handshake_ = make_unique<mtproto::AuthKeyHandshake>(dc_id_, 0);
    }
    if (raw_connection_ && handshake_) {
      if (wait_for_result_) {
        wait_for_result_ = false;
        if (status_.is_error()) {
          finish(std::move(status_));
          return stop();
        }
        if (!handshake_->is_ready_for_finish()) {
          finish(Status::Error("Key is not ready.."));
          return stop();
        }
        finish(Status::OK());
        return stop();
      }

      wait_for_result_ = true;
      create_actor<mtproto::HandshakeActor>(
          "HandshakeActor", std::move(handshake_), std::move(raw_connection_), make_unique<HandshakeContext>(), 10.0,
          PromiseCreator::lambda([self = actor_id(this)](Result<unique_ptr<mtproto::RawConnection>> raw_connection) {
            send_closure(self, &HandshakeTestActor::got_connection, std::move(raw_connection), 1);
          }),
          PromiseCreator::lambda([self = actor_id(this)](Result<unique_ptr<mtproto::AuthKeyHandshake>> handshake) {
            send_closure(self, &HandshakeTestActor::got_handshake, std::move(handshake), 1);
          }))
          .release();
      wait_for_raw_connection_ = true;
      wait_for_handshake_ = true;
    }
  }

  void got_connection(Result<unique_ptr<mtproto::RawConnection>> r_raw_connection, int32 dummy) {
    CHECK(wait_for_raw_connection_);
    wait_for_raw_connection_ = false;
    if (r_raw_connection.is_ok()) {
      raw_connection_ = r_raw_connection.move_as_ok();
      status_ = Status::OK();
    } else {
      status_ = r_raw_connection.move_as_error();
    }
    // TODO: save error
    loop();
  }

  void got_handshake(Result<unique_ptr<mtproto::AuthKeyHandshake>> r_handshake, int32 dummy) {
    CHECK(wait_for_handshake_);
    wait_for_handshake_ = false;
    CHECK(r_handshake.is_ok());
    handshake_ = r_handshake.move_as_ok();
    loop();
  }

  void finish(Status status) {
    if (!result_) {
      return;
    }
    *result_ = std::move(status);
    result_ = nullptr;
    Scheduler::instance()->finish();
  }
};

class Mtproto_handshake : public Test {
 public:
  using Test::Test;
  bool step() final {
    if (!is_inited_) {
      sched_.init(0);
      sched_.create_actor_unsafe<HandshakeTestActor>(0, "HandshakeTestActor", get_default_dc_id(), &result_).release();
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
    return false;
  }

 private:
  bool is_inited_ = false;
  ConcurrentScheduler sched_;
  Status result_;
};
RegisterTest<Mtproto_handshake> mtproto_handshake("Mtproto_handshake");

class Socks5TestActor : public Actor {
 public:
  void start_up() override {
    auto promise = PromiseCreator::lambda([actor_id = actor_id(this)](Result<SocketFd> res) {
      send_closure(actor_id, &Socks5TestActor::on_result, std::move(res), false);
    });

    class Callback : public TransparentProxy::Callback {
     public:
      explicit Callback(Promise<SocketFd> promise) : promise_(std::move(promise)) {
      }
      void set_result(Result<SocketFd> result) override {
        promise_.set_result(std::move(result));
      }
      void on_connected() override {
      }

     private:
      Promise<SocketFd> promise_;
    };

    IPAddress socks5_ip;
    socks5_ip.init_ipv4_port("131.191.89.104", 43077).ensure();
    IPAddress mtproto_ip = get_default_ip_address();

    auto r_socket = SocketFd::open(socks5_ip);
    create_actor<Socks5>("socks5", r_socket.move_as_ok(), mtproto_ip, "", "", make_unique<Callback>(std::move(promise)),
                         actor_shared())
        .release();
  }

 private:
  void on_result(Result<SocketFd> res, bool dummy) {
    res.ensure();
    Scheduler::instance()->finish();
  }
};

TEST(Mtproto, socks5) {
  return;
  ConcurrentScheduler sched;
  int threads_n = 0;
  sched.init(threads_n);

  sched.create_actor_unsafe<Socks5TestActor>(0, "Socks5TestActor").release();
  sched.start();
  while (sched.run_main(10)) {
    // empty;
  }
  sched.finish();
}
