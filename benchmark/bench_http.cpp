//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/actor/actor.h"

#include "td/net/HttpOutboundConnection.h"
#include "td/net/HttpQuery.h"
#include "td/net/SslStream.h"

#include "td/utils/buffer.h"
#include "td/utils/logging.h"
#include "td/utils/port/IPAddress.h"
#include "td/utils/port/SocketFd.h"
#include "td/utils/Status.h"

#include <atomic>
#include <limits>

namespace td {

std::atomic<int> counter;

class HttpClient : public HttpOutboundConnection::Callback {
  void start_up() override {
    IPAddress addr;
    addr.init_ipv4_port("127.0.0.1", 8082).ensure();
    auto fd = SocketFd::open(addr);
    LOG_CHECK(fd.is_ok()) << fd.error();
    connection_ = create_actor<HttpOutboundConnection>("Connect", fd.move_as_ok(), SslStream{},
                                                       std::numeric_limits<size_t>::max(), 0, 0,
                                                       ActorOwn<HttpOutboundConnection::Callback>(actor_id(this)));
    yield();
    cnt_ = 100000;
    counter++;
  }
  void tear_down() override {
    if (--counter == 0) {
      Scheduler::instance()->finish();
    }
  }
  void loop() override {
    if (cnt_-- < 0) {
      return stop();
    }
    send_closure(connection_, &HttpOutboundConnection::write_next, BufferSlice("GET / HTTP/1.1\r\n\r\n"));
    send_closure(connection_, &HttpOutboundConnection::write_ok);
    LOG(INFO) << "SEND";
  }
  void handle(unique_ptr<HttpQuery> result) override {
    loop();
  }
  void on_connection_error(Status error) override {
    LOG(ERROR) << "ERROR: " << error;
  }

  ActorOwn<HttpOutboundConnection> connection_;
  int cnt_;
};

int main() {
  SET_VERBOSITY_LEVEL(VERBOSITY_NAME(ERROR));
  auto scheduler = make_unique<ConcurrentScheduler>();
  scheduler->init(0);
  scheduler->create_actor_unsafe<HttpClient>(0, "Client1").release();
  scheduler->create_actor_unsafe<HttpClient>(0, "Client2").release();
  scheduler->start();
  while (scheduler->run_main(10)) {
    // empty
  }
  scheduler->finish();
  return 0;
}
}  // namespace td

int main() {
  return td::main();
}
