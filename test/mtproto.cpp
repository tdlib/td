//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
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

#include "td/net/Socks5.h"

#include "td/telegram/ConfigManager.h"
#include "td/telegram/net/PublicRsaKeyShared.h"

#include "td/utils/logging.h"
#include "td/utils/port/IPAddress.h"
#include "td/utils/port/SocketFd.h"
#include "td/utils/Status.h"

REGISTER_TESTS(mtproto);

using namespace td;
using namespace mtproto;

#if !TD_WINDOWS && !TD_EMSCRIPTEN  // TODO
TEST(Mtproto, config) {
  ConcurrentScheduler sched;
  int threads_n = 0;
  sched.init(threads_n);

  int cnt = 2;
  {
    auto guard = sched.get_current_guard();
    get_simple_config_google_app(PromiseCreator::lambda([&](Result<SimpleConfig> r_simple_config) {
      LOG(ERROR) << to_string(r_simple_config.ok());
      if (--cnt == 0) {
        Scheduler::instance()->finish();
      }
    }))
        .release();

    get_simple_config_google_dns(PromiseCreator::lambda([&](Result<SimpleConfig> r_simple_config) {
      LOG(ERROR) << to_string(r_simple_config.ok());
      if (--cnt == 0) {
        Scheduler::instance()->finish();
      }
    }))
        .release();
  }
  sched.start();
  while (sched.run_main(10)) {
    // empty;
  }
  sched.finish();
}
#endif

TEST(Mtproto, encrypted_config) {
  string data =
      "   LQ2 \b\n\tru6xVXpHHckW4eQWK0X3uThupVOor5sXT8t298IjDksYeUseQTOIrnUqiQj7o"
      "+ZgPfhnfe+lfcQA+naax9akgllimjlJtL5riTf3O7iqZSnJ614qmCucxqqVTbXk/"
      "hY2KaJTtsMqk7cShJjM3aQ4DD40h2InTaG7uyVO2q7K0GMUTeY3AM0Rt1lUjKHLD"
      "g4RwjTzZaG8TwfgL/mZ7jsvgTTTATPWKUo7SmxQ9Hsj+07NMGqr6JKZS6aiU1Knz"
      "VGCZ3OJEyRYocktN4HjaLpkguilaHWlVM2UNFUd5a+ajfLIiiKlH0FRC3XZ12CND"
      "Y+NBjv0I57N2O4fBfswTlA==  ";
  auto config = decode_config(data).move_as_ok();
}

class TestPingActor : public Actor {
 public:
  TestPingActor(IPAddress ip_address, Status *result) : ip_address_(ip_address), result_(result) {
  }

 private:
  IPAddress ip_address_;
  std::unique_ptr<mtproto::PingConnection> ping_connection_;
  Status *result_;

  void start_up() override {
    ping_connection_ = std::make_unique<mtproto::PingConnection>(std::make_unique<mtproto::RawConnection>(
        SocketFd::open(ip_address_).move_as_ok(), mtproto::TransportType::Tcp, nullptr));

    ping_connection_->get_pollable().set_observer(this);
    subscribe(ping_connection_->get_pollable());
    set_timeout_in(10);
    yield();
  }
  void tear_down() override {
    unsubscribe_before_close(ping_connection_->get_pollable());
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
      LOG(ERROR) << "GOT PONG";
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
  ip_address.init_ipv4_port("149.154.167.40", 80).ensure();
  return ip_address;
}

class Mtproto_ping : public td::Test {
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
    ASSERT_TRUE(result_.is_ok());
    return false;
  }

 private:
  bool is_inited_ = false;
  ConcurrentScheduler sched_;
  Status result_;
};
Mtproto_ping mtproto_ping("Mtproto_ping");

class Context : public AuthKeyHandshakeContext {
 public:
  DhCallback *get_dh_callback() override {
    return nullptr;
  }
  PublicRsaKeyInterface *get_public_rsa_key_interface() override {
    return &public_rsa_key;
  }

 private:
  PublicRsaKeyShared public_rsa_key{DcId::empty()};
};

class HandshakeTestActor : public Actor {
 public:
  explicit HandshakeTestActor(Status *result) : result_(result) {
  }

 private:
  Status *result_;
  bool wait_for_raw_connection_ = false;
  std::unique_ptr<RawConnection> raw_connection_;
  bool wait_for_handshake_ = false;
  std::unique_ptr<AuthKeyHandshake> handshake_;
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
      raw_connection_ = std::make_unique<mtproto::RawConnection>(SocketFd::open(get_default_ip_address()).move_as_ok(),
                                                                 mtproto::TransportType::Tcp, nullptr);
    }
    if (!wait_for_handshake_ && !handshake_) {
      handshake_ = std::make_unique<AuthKeyHandshake>(0);
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
      create_actor<HandshakeActor>(
          "HandshakeActor", std::move(handshake_), std::move(raw_connection_), std::make_unique<Context>(), 10.0,
          PromiseCreator::lambda([self = actor_id(this)](Result<std::unique_ptr<RawConnection>> raw_connection) {
            send_closure(self, &HandshakeTestActor::got_connection, std::move(raw_connection), 1);
          }),
          PromiseCreator::lambda([self = actor_id(this)](Result<std::unique_ptr<AuthKeyHandshake>> handshake) {
            send_closure(self, &HandshakeTestActor::got_handshake, std::move(handshake), 1);
          }))
          .release();
      wait_for_raw_connection_ = true;
      wait_for_handshake_ = true;
    }
  }

  void got_connection(Result<std::unique_ptr<RawConnection>> r_raw_connection, int32 dummy) {
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

  void got_handshake(Result<std::unique_ptr<AuthKeyHandshake>> r_handshake, int32 dummy) {
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

class Mtproto_handshake : public td::Test {
 public:
  using Test::Test;
  bool step() final {
    if (!is_inited_) {
      sched_.init(0);
      sched_.create_actor_unsafe<HandshakeTestActor>(0, "HandshakeTestActor", &result_).release();
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
Mtproto_handshake mtproto_handshake("Mtproto_handshake");

class Socks5TestActor : public Actor {
 public:
  void start_up() override {
    auto promise = PromiseCreator::lambda([actor_id = actor_id(this)](Result<SocketFd> res) {
      send_closure(actor_id, &Socks5TestActor::on_result, std::move(res), false);
    });

    class Callback : public Socks5::Callback {
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
    create_actor<Socks5>("socks5", r_socket.move_as_ok(), mtproto_ip, "", "",
                         std::make_unique<Callback>(std::move(promise)), actor_shared())
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
