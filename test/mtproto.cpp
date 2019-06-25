//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2019
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/utils/tests.h"

#include "td/actor/actor.h"
#include "td/actor/PromiseFuture.h"

#include "td/mtproto/AuthData.h"
#include "td/mtproto/crypto.h"
#include "td/mtproto/DhHandshake.h"
#include "td/mtproto/Handshake.h"
#include "td/mtproto/HandshakeActor.h"
#include "td/mtproto/Ping.h"
#include "td/mtproto/PingConnection.h"
#include "td/mtproto/RawConnection.h"
#include "td/mtproto/TransportType.h"

#include "td/net/GetHostByNameActor.h"
#include "td/net/Socks5.h"
#include "td/net/TransparentProxy.h"

#include "td/telegram/ConfigManager.h"
#include "td/telegram/net/DcId.h"
#include "td/telegram/net/PublicRsaKeyShared.h"
#include "td/telegram/net/Session.h"
#include "td/telegram/NotificationManager.h"

#include "td/utils/as.h"
#include "td/utils/base64.h"
#include "td/utils/common.h"
#include "td/utils/crypto.h"
#include "td/utils/logging.h"
#include "td/utils/port/IPAddress.h"
#include "td/utils/port/SocketFd.h"
#include "td/utils/Random.h"
#include "td/utils/Span.h"
#include "td/utils/Status.h"
#include "td/utils/Time.h"

REGISTER_TESTS(mtproto);

using namespace td;

TEST(Mtproto, GetHostByNameActor) {
  SET_VERBOSITY_LEVEL(VERBOSITY_NAME(ERROR));
  ConcurrentScheduler sched;
  int threads_n = 1;
  sched.init(threads_n);

  int cnt = 1;
  vector<ActorOwn<GetHostByNameActor>> actors;
  {
    auto guard = sched.get_main_guard();

    auto run = [&](ActorId<GetHostByNameActor> actor_id, string host, bool prefer_ipv6, bool allow_ok,
                   bool allow_error) {
      auto promise = PromiseCreator::lambda([&cnt, &actors, num = cnt, host, allow_ok,
                                             allow_error](Result<IPAddress> r_ip_address) {
        if (r_ip_address.is_error() && !allow_error) {
          LOG(ERROR) << num << " \"" << host << "\" " << r_ip_address.error();
        }
        if (r_ip_address.is_ok() && !allow_ok && (r_ip_address.ok().is_ipv6() || r_ip_address.ok().get_ipv4() != 0)) {
          LOG(ERROR) << num << " \"" << host << "\" " << r_ip_address.ok();
        }
        if (--cnt == 0) {
          actors.clear();
          Scheduler::instance()->finish();
        }
      });
      cnt++;
      send_closure(actor_id, &GetHostByNameActor::run, host, 443, prefer_ipv6, std::move(promise));
    };

    std::vector<std::string> hosts = {"127.0.0.2",
                                      "1.1.1.1",
                                      "localhost",
                                      "web.telegram.org",
                                      "web.telegram.org.",
                                      "москва.рф",
                                      "",
                                      "%",
                                      " ",
                                      "a",
                                      "\x80",
                                      "127.0.0.1.",
                                      "0x12.0x34.0x56.0x78",
                                      "0x7f.001",
                                      "2001:0db8:85a3:0000:0000:8a2e:0370:7334"};
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
          bool allow_ok = host.size() > 2;
          bool allow_both = host == "127.0.0.1." || host == "localhost" || (host == "москва.рф" && prefer_ipv6);
          bool allow_error = !allow_ok || allow_both;
          run(actor_id, host, prefer_ipv6, allow_ok, allow_error);
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
    ping_connection_ = mtproto::PingConnection::create_req_pq(
        make_unique<mtproto::RawConnection>(SocketFd::open(ip_address_).move_as_ok(),
                                            mtproto::TransportType{mtproto::TransportType::Tcp, 0, ""}, nullptr),
        3);

    Scheduler::subscribe(ping_connection_->get_poll_info().extract_pollable_fd(this));
    set_timeout_in(10);
    yield();
  }
  void tear_down() override {
    Scheduler::unsubscribe_before_close(ping_connection_->get_poll_info().get_pollable_fd_ref());
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

TEST(Mtproto, notifications) {
  vector<string> pushes = {
      "eyJwIjoiSkRnQ3NMRWxEaWhyVWRRN1pYM3J1WVU4TlRBMFhMb0N6UWRNdzJ1cWlqMkdRbVR1WXVvYXhUeFJHaG1QQm8yVElYZFBzX2N3b2RIb3lY"
      "b2drVjM1dVl0UzdWeElNX1FNMDRKMG1mV3ZZWm4zbEtaVlJ0aFVBNGhYUWlaN0pfWDMyZDBLQUlEOWgzRnZwRjNXUFRHQWRaVkdFYzg3bnFPZ3hD"
      "NUNMRkM2SU9fZmVqcEpaV2RDRlhBWWpwc1k2aktrbVNRdFZ1MzE5ZW04UFVieXZudFpfdTNud2hjQ0czMk96TGp4S1kyS1lzU21JZm1GMzRmTmw1"
      "QUxaa2JvY2s2cE5rZEdrak9qYmRLckJyU0ZtWU8tQ0FsRE10dEplZFFnY1U5bVJQdU80b1d2NG5sb1VXS19zSlNTaXdIWEZyb1pWTnZTeFJ0Z1dN"
      "ZyJ9",
      "eyJwIjoiSkRnQ3NMRWxEaWlZby1GRWJndk9WaTFkUFdPVmZndzBBWHYwTWNzWDFhWEtNZC03T1Q2WWNfT0taRURHZDJsZ0h0WkhMSllyVG50RE95"
      "TkY1aXJRQlZ4UUFLQlRBekhPTGZIS3BhQXdoaWd5b3NQd0piWnJVV2xRWmh4eEozUFUzZjBNRTEwX0xNT0pFN0xsVUFaY2dabUNaX2V1QmNPZWNK"
      "VERxRkpIRGZjN2pBOWNrcFkyNmJRT2dPUEhCeHlEMUVrNVdQcFpLTnlBODVuYzQ1eHFPdERqcU5aVmFLU3pKb2VIcXBQMnJqR29kN2M5YkxsdGd5"
      "Q0NGd2NBU3dJeDc3QWNWVXY1UnVZIn0"};
  string key =
      "uBa5yu01a-nJJeqsR3yeqMs6fJLYXjecYzFcvS6jIwS3nefBIr95LWrTm-IbRBNDLrkISz1Sv0KYpDzhU8WFRk1D0V_"
      "qyO7XsbDPyrYxRBpGxofJUINSjb1uCxoSdoh1_F0UXEA2fWWKKVxL0DKUQssZfbVj3AbRglsWpH-jDK1oc6eBydRiS3i4j-"
      "H0yJkEMoKRgaF9NaYI4u26oIQ-Ez46kTVU-R7e3acdofOJKm7HIKan_5ZMg82Dvec2M6vc_"
      "I54Vs28iBx8IbBO1y5z9WSScgW3JCvFFKP2MXIu7Jow5-cpUx6jXdzwRUb9RDApwAFKi45zpv8eb3uPCDAmIQ";
  vector<string> decrypted_payloads = {
      "eyJsb2Nfa2V5IjoiTUVTU0FHRV9URVhUIiwibG9jX2FyZ3MiOlsiQXJzZW55IFNtaXJub3YiLCJhYmNkZWZnIl0sImN1c3RvbSI6eyJtc2dfaWQi"
      "OiI1OTAwNDciLCJmcm9tX2lkIjoiNjI4MTQifSwiYmFkZ2UiOiI0MDkifQ",
      "eyJsb2Nfa2V5IjoiIiwibG9jX2FyZ3MiOltdLCJjdXN0b20iOnsiY2hhbm5lbF9pZCI6IjExNzY4OTU0OTciLCJtYXhfaWQiOiIxMzU5In0sImJh"
      "ZGdlIjoiMCJ9"};
  key = base64url_decode(key).move_as_ok();

  for (size_t i = 0; i < pushes.size(); i++) {
    auto push = base64url_decode(pushes[i]).move_as_ok();
    auto decrypted_payload = base64url_decode(decrypted_payloads[i]).move_as_ok();

    auto key_id = DhHandshake::calc_key_id(key);
    ASSERT_EQ(key_id, NotificationManager::get_push_receiver_id(push).ok());
    ASSERT_EQ(decrypted_payload, NotificationManager::decrypt_push(key_id, key, push).ok());
  }
}

class FastPingTestActor : public Actor {
 public:
  explicit FastPingTestActor(Status *result) : result_(result) {
  }

 private:
  Status *result_;
  unique_ptr<mtproto::RawConnection> connection_;
  unique_ptr<mtproto::AuthKeyHandshake> handshake_;
  ActorOwn<> fast_ping_;
  int iteration_{0};

  void start_up() override {
    // Run handshake to create key and salt
    auto raw_connection =
        make_unique<mtproto::RawConnection>(SocketFd::open(get_default_ip_address()).move_as_ok(),
                                            mtproto::TransportType{mtproto::TransportType::Tcp, 0, ""}, nullptr);
    auto handshake = make_unique<mtproto::AuthKeyHandshake>(get_default_dc_id(), 60 * 100 /*temp*/);
    create_actor<mtproto::HandshakeActor>(
        "HandshakeActor", std::move(handshake), std::move(raw_connection), make_unique<HandshakeContext>(), 10.0,
        PromiseCreator::lambda([self = actor_id(this)](Result<unique_ptr<mtproto::RawConnection>> raw_connection) {
          send_closure(self, &FastPingTestActor::got_connection, std::move(raw_connection), 1);
        }),
        PromiseCreator::lambda([self = actor_id(this)](Result<unique_ptr<mtproto::AuthKeyHandshake>> handshake) {
          send_closure(self, &FastPingTestActor::got_handshake, std::move(handshake), 1);
        }))
        .release();
  }
  void got_connection(Result<unique_ptr<mtproto::RawConnection>> r_raw_connection, int32 dummy) {
    if (r_raw_connection.is_error()) {
      *result_ = r_raw_connection.move_as_error();
      return stop();
    }
    connection_ = r_raw_connection.move_as_ok();
    loop();
  }

  void got_handshake(Result<unique_ptr<mtproto::AuthKeyHandshake>> r_handshake, int32 dummy) {
    if (r_handshake.is_error()) {
      *result_ = r_handshake.move_as_error();
      return stop();
    }
    handshake_ = r_handshake.move_as_ok();
    loop();
  }

  void got_raw_connection(Result<unique_ptr<mtproto::RawConnection>> r_connection) {
    if (r_connection.is_error()) {
      Scheduler::instance()->finish();
      *result_ = r_connection.move_as_error();
      return stop();
    }
    connection_ = r_connection.move_as_ok();
    LOG(INFO) << "RTT: " << connection_->rtt_;
    connection_->rtt_ = 0;
    loop();
  }

  void loop() override {
    if (handshake_ && connection_) {
      LOG(INFO) << "Iteration " << iteration_;
      if (iteration_ == 6) {
        Scheduler::instance()->finish();
        return stop();
      }
      unique_ptr<mtproto::AuthData> auth_data;
      if (iteration_ % 2 == 0) {
        auth_data = make_unique<mtproto::AuthData>();
        auth_data->set_tmp_auth_key(handshake_->auth_key);
        auth_data->set_server_time_difference(handshake_->server_time_diff);
        auth_data->set_server_salt(handshake_->server_salt, Time::now());
        auth_data->set_future_salts({mtproto::ServerSalt{0u, 1e20, 1e30}}, Time::now());
        auth_data->set_use_pfs(true);
        uint64 session_id = 0;
        do {
          Random::secure_bytes(reinterpret_cast<uint8 *>(&session_id), sizeof(session_id));
        } while (session_id == 0);
        auth_data->set_session_id(session_id);
      }
      iteration_++;
      fast_ping_ = create_ping_actor(
          "", std::move(connection_), std::move(auth_data),
          PromiseCreator::lambda([self = actor_id(this)](Result<unique_ptr<mtproto::RawConnection>> r_raw_connection) {
            send_closure(self, &FastPingTestActor::got_raw_connection, std::move(r_raw_connection));
          }),
          ActorShared<>());
    }
  }
};

class Mtproto_FastPing : public Test {
 public:
  using Test::Test;
  bool step() final {
    if (!is_inited_) {
      sched_.init(0);
      sched_.create_actor_unsafe<FastPingTestActor>(0, "FastPingTestActor", &result_).release();
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
RegisterTest<Mtproto_FastPing> mtproto_fastping("Mtproto_FastPing");

class Grease {
 public:
  static void init(MutableSlice res) {
    Random::secure_bytes(res);
    for (auto &c : res) {
      c = (c & 0xF0) + 0x0A;
    }
    for (size_t i = 1; i < res.size(); i += 2) {
      if (res[i] == res[i - 1]) {
        res[i] ^= 0x10;
      }
    }
  }
};

class TlsHello {
 public:
  struct Op {
    enum class Type { String, Random, Zero, Domain, Grease, BeginScope, EndScope };
    Type type;
    int length;
    int seed;
    std::string data;

    static Op string(Slice str) {
      Op res;
      res.type = Type::String;
      res.data = str.str();
      return res;
    }
    static Op random(int length) {
      Op res;
      res.type = Type::Random;
      res.length = length;
      return res;
    }
    static Op zero(int length) {
      Op res;
      res.type = Type::Zero;
      res.length = length;
      return res;
    }
    static Op domain() {
      Op res;
      res.type = Type::Domain;
      return res;
    }
    static Op grease(int seed) {
      Op res;
      res.type = Type::Grease;
      res.seed = seed;
      return res;
    }
    static Op begin_scope() {
      Op res;
      res.type = Type::BeginScope;
      return res;
    }
    static Op end_scope() {
      Op res;
      res.type = Type::EndScope;
      return res;
    }
  };

  static const TlsHello &get_default() {
    static TlsHello res = [] {
      TlsHello res;
      res.ops_ = {
          Op::string("\x16\x03\x01\x02\x00\x01\x00\x01\xfc\x03\x03"),
          Op::zero(32),
          Op::string("\x20"),
          Op::random(32),
          Op::string("\x00\x22"),
          Op::grease(0),
          Op::string("\x13\x01\x13\x02\x13\x03\xc0\x2b\xc0\x2f\xc0\x2c\xc0\x30\xcc\xa9\xcc\xa8\xc0\x13\xc0\x14\x00\x9c"
                     "\x00\x9d\x00\x2f\x00\x35\x00\x0a\x01\x00\x01\x91"),
          Op::grease(2),
          Op::string("\x00\x00\x00\x00"),
          Op::begin_scope(),
          Op::begin_scope(),
          Op::string("\x00"),
          Op::begin_scope(),
          Op::domain(),
          Op::end_scope(),
          Op::end_scope(),
          Op::end_scope(),
          Op::string("\x00\x17\x00\x00\xff\x01\x00\x01\x00\x00\x0a\x00\x0a\x00\x08"),
          Op::grease(4),
          Op::string(
              "\x00\x1d\x00\x17\x00\x18\x00\x0b\x00\x02\x01\x00\x00\x23\x00\x00\x00\x10\x00\x0e\x00\x0c\x02\x68\x32\x08"
              "\x68\x74\x74\x70\x2f\x31\x2e\x31\x00\x05\x00\x05\x01\x00\x00\x00\x00\x00\x0d\x00\x14\x00\x12\x04\x03\x08"
              "\x04\x04\x01\x05\x03\x08\x05\x05\x01\x08\x06\x06\x01\x02\x01\x00\x12\x00\x00\x00\x33\x00\x2b\x00\x29"),
          Op::grease(4),
          Op::string("\x00\x01\x00\x00\x1d\x00\x20"),
          Op::random(32),
          Op::string("\x00\x2d\x00\x02\x01\x01\x00\x2b\x00\x0b\x0a"),
          Op::grease(6),
          Op::string("\x03\x04\x03\x03\x03\x02\x03\x01\x00\x1b\x00\x03\x02\x00\x02"),
          Op::grease(3),
          Op::string("\x00\x01\x00\x00\x15")};
      return res;
    }();
    return res;
  }
  Span<Op> get_ops() const {
    return ops_;
  }

 private:
  std::vector<Op> ops_;
};

class TlsHelloContext {
 public:
  explicit TlsHelloContext(std::string domain) {
    Grease::init(MutableSlice(grease_.data(), grease_.size()));
    domain_ = std::move(domain);
  }
  char get_grease(size_t i) const {
    CHECK(i < grease_.size());
    return grease_[i];
  }
  size_t grease_size() const {
    return grease_.size();
  }
  Slice get_domain() const {
    return domain_;
  }

 private:
  constexpr static size_t MAX_GREASE = 8;
  std::array<char, MAX_GREASE> grease_;
  std::string domain_;
};

class TlsHelloCalcLength {
 public:
  void do_op(const TlsHello::Op &op, const TlsHelloContext *context) {
    using Type = TlsHello::Op::Type;
    switch (op.type) {
      case Type::String:
        size_ += op.data.size();
        break;
      case Type::Random:
        if (op.length <= 0 || op.length > 1024) {
          on_error(Status::Error("Invalid random length"));
        }
        size_ += op.length;
        break;
      case Type::Zero:
        if (op.length < 0 || op.length > 1024) {
          on_error(Status::Error("Invalid zero length"));
        }
        size_ += op.length;
        break;
      case Type::Domain:
        CHECK(context);
        size_ += context->get_domain().size();
        break;
      case Type::Grease:
        CHECK(context);
        if (op.seed < 0 || static_cast<size_t>(op.seed) > context->grease_size()) {
          on_error(Status::Error("Invalid grease seed"));
        }
        size_ += 2;
        break;
      case Type::BeginScope:
        size_ += 2;
        scope_offset_.push_back(size_);
        break;
      case Type::EndScope:
        if (scope_offset_.empty()) {
          on_error(Status::Error("Unbalanced scopes"));
        }
        auto begin_offset = scope_offset_.back();
        scope_offset_.pop_back();
        auto end_offset = size_;
        auto size = end_offset - begin_offset;
        if (size >= (1 << 14)) {
          on_error(Status::Error("Scope is too big"));
        }
        break;
    }
  }

  Result<int64> finish() {
    if (size_ > 515) {
      on_error(Status::Error("Too long for zero padding"));
    }
    if (size_ < 11 + 32) {
      on_error(Status::Error("Too small for hash"));
    }
    int zero_pad = 515 - static_cast<int>(size_);
    using Op = TlsHello::Op;
    do_op(Op::begin_scope(), nullptr);
    do_op(Op::zero(zero_pad), nullptr);
    do_op(Op::end_scope(), nullptr);
    if (!scope_offset_.empty()) {
      on_error(Status::Error("Unbalanced scopes"));
    }
    TRY_STATUS(std::move(status_));
    return size_;
  }

 private:
  int64 size_{0};
  Status status_;
  std::vector<size_t> scope_offset_;

  void on_error(Status error) {
    if (status_.is_ok()) {
      status_ = std::move(error);
    }
  }
};

class TlsHelloStore {
 public:
  TlsHelloStore(MutableSlice dest) : data_(dest), dest_(dest) {
  }
  void do_op(const TlsHello::Op &op, TlsHelloContext *context) {
    using Type = TlsHello::Op::Type;
    switch (op.type) {
      case Type::String:
        dest_.copy_from(op.data);
        dest_.remove_prefix(op.data.size());
        break;
      case Type::Random:
        Random::secure_bytes(dest_.substr(0, op.length));
        dest_.remove_prefix(op.length);
        break;
      case Type::Zero:
        std::memset(dest_.begin(), 0, op.length);
        dest_.remove_prefix(op.length);
        break;
      case Type::Domain: {
        CHECK(context);
        auto domain = context->get_domain();
        dest_.copy_from(domain);
        dest_.remove_prefix(domain.size());
        break;
      }
      case Type::Grease: {
        CHECK(context)
        auto grease = context->get_grease(op.seed);
        dest_[0] = grease;
        dest_[1] = grease;
        dest_.remove_prefix(2);
        break;
      }
      case Type::BeginScope:
        scope_offset_.push_back(get_offset());
        dest_.remove_prefix(2);
        break;
      case Type::EndScope: {
        CHECK(!scope_offset_.empty());
        auto begin_offset = scope_offset_.back();
        scope_offset_.pop_back();
        auto end_offset = get_offset();
        size_t size = end_offset - begin_offset - 2;
        CHECK(size < (1 << 14));
        data_[begin_offset] = static_cast<char>((size >> 8) & 0xff);
        data_[begin_offset + 1] = static_cast<char>(size & 0xff);
        break;
      }
    }
  }
  void finish(int32 unix_time) {
    int zero_pad = 515 - static_cast<int>(get_offset());
    using Op = TlsHello::Op;
    do_op(Op::begin_scope(), nullptr);
    do_op(Op::zero(zero_pad), nullptr);
    do_op(Op::end_scope(), nullptr);

    auto tmp = sha256(data_);
    auto hash_dest = data_.substr(11);
    hash_dest.copy_from(tmp);
    int32 old = as<int32>(hash_dest.substr(28).data());
    as<int32>(hash_dest.substr(28).data()) = old ^ unix_time;
    CHECK(dest_.empty());
  }

 private:
  MutableSlice data_;
  MutableSlice dest_;
  std::vector<size_t> scope_offset_;
  size_t get_offset() {
    return data_.size() - dest_.size();
  }
};

class TlsObfusaction {
 public:
  static std::string generate_header(std::string domain, int32 unix_time) {
    auto &hello = TlsHello::get_default();
    TlsHelloContext context(domain);
    TlsHelloCalcLength calc_length;
    for (auto &op : hello.get_ops()) {
      calc_length.do_op(op, &context);
    }
    auto length = calc_length.finish().move_as_ok();
    std::string data(length, 0);
    TlsHelloStore storer(data);
    for (auto &op : hello.get_ops()) {
      storer.do_op(op, &context);
    }
    storer.finish(0);
    return data;
  }
};

class TlsInit : public TransparentProxy {
 public:
  using TransparentProxy::TransparentProxy;

 private:
  enum class State {
    SendHello,
    WaitHelloResponse,
  } state_ = State::SendHello;

  void send_hello() {
    auto hello = TlsObfusaction::generate_header(username_, 0);
    fd_.output_buffer().append(hello);
    state_ = State::WaitHelloResponse;
  }

  Status wait_hello_response() {
    //[
    auto it = fd_.input_buffer().clone();
    //S "\x16\x03\x03"
    {
      Slice first = "\x16\x03\x03";
      std::string got_first(first.size(), 0);
      if (it.size() < first.size()) {
        return td::Status::OK();
      }
      it.advance(first.size(), got_first);
      if (first != got_first) {
        return Status::Error("First part of response to hello is invalid");
      }
    }

    //[
    {
      if (it.size() < 2) {
        return td::Status::OK();
      }
      uint8 tmp[2];
      it.advance(2, MutableSlice(tmp, 2));
      size_t skip_size = (tmp[0] << 8) + tmp[1];
      if (it.size() < skip_size) {
        return td::Status::OK();
      }
      it.advance(skip_size);
    }

    //S "\x14\x03\x03\x00\x01\x01\x17\x03\x03"
    {
      Slice first = "\x14\x03\x03\x00\x01\x01\x17\x03\x03";
      std::string got_first(first.size(), 0);
      if (it.size() < first.size()) {
        return td::Status::OK();
      }
      it.advance(first.size(), got_first);
      if (first != got_first) {
        return Status::Error("Second part of response to hello is invalid");
      }
    }

    //[
    {
      if (it.size() < 2) {
        return td::Status::OK();
      }
      uint8 tmp[2];
      it.advance(2, MutableSlice(tmp, 2));
      size_t skip_size = (tmp[0] << 8) + tmp[1];
      if (it.size() < skip_size) {
        return td::Status::OK();
      }
      it.advance(skip_size);
    }
    fd_.input_buffer() = std::move(it);

    stop();
    return td::Status::OK();
  }

  Status loop_impl() override {
    switch (state_) {
      case State::SendHello:
        send_hello();
        break;
      case State::WaitHelloResponse:
        TRY_STATUS(wait_hello_response());
        break;
    }
    return Status::OK();
  }
};

TEST(Mtproto, TlsObfusaction) {
  std::string s(10000, 'a');
  Grease::init(s);
  for (auto c : s) {
    CHECK((c & 0xf) == 0xa);
  }
  for (size_t i = 1; i < s.size(); i += 2) {
    CHECK(s[i] != s[i - 1]);
  }
  std::string domain = "www.google.com";
  SET_VERBOSITY_LEVEL(VERBOSITY_NAME(ERROR));
  ConcurrentScheduler sched;
  int threads_n = 1;
  sched.init(threads_n);
  {
    auto guard = sched.get_main_guard();
    class Callback : public TransparentProxy::Callback {
     public:
      void set_result(Result<SocketFd> result) override {
        result.ensure();
        Scheduler::instance()->finish();
      }
      void on_connected() override {
      }
    };
    IPAddress ip_address;
    ip_address.init_host_port(domain, 443).ensure();
    SocketFd fd = SocketFd::open(ip_address).move_as_ok();
    create_actor<TlsInit>("TlsInit", std::move(fd), IPAddress(), domain, "", make_unique<Callback>(), ActorShared<>())
        .release();
  }

  sched.start();
  while (sched.run_main(10)) {
    // empty
  }
  sched.finish();
}
