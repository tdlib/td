//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/actor/actor.h"

#include "td/net/HttpHeaderCreator.h"
#include "td/net/HttpInboundConnection.h"
#include "td/net/HttpQuery.h"
#include "td/net/TcpListener.h"

#include "td/utils/buffer.h"
#include "td/utils/logging.h"
#include "td/utils/port/SocketFd.h"
#include "td/utils/Slice.h"

namespace td {

static int cnt = 0;

class HelloWorld : public HttpInboundConnection::Callback {
 public:
  void handle(unique_ptr<HttpQuery> query, ActorOwn<HttpInboundConnection> connection) override {
    // LOG(ERROR) << *query;
    HttpHeaderCreator hc;
    Slice content = "hello world";
    //auto content = BufferSlice("hello world");
    hc.init_ok();
    hc.set_keep_alive();
    hc.set_content_size(content.size());
    hc.add_header("Server", "TDLib/test");
    hc.add_header("Date", "Thu Dec 14 01:41:50 2017");
    hc.add_header("Content-Type:", "text/html");

    auto res = hc.finish(content);
    LOG_IF(FATAL, res.is_error()) << res.error();
    send_closure(connection, &HttpInboundConnection::write_next, BufferSlice(res.ok()));
    send_closure(connection.release(), &HttpInboundConnection::write_ok);
  }
  void hangup() override {
    LOG(ERROR) << "CLOSE " << cnt--;
    stop();
  }
};

const int N = 0;
class Server : public TcpListener::Callback {
 public:
  void start_up() override {
    listener_ = create_actor<TcpListener>("Listener", 8082, ActorOwn<TcpListener::Callback>(actor_id(this)));
  }
  void accept(SocketFd fd) override {
    LOG(ERROR) << "ACCEPT " << cnt++;
    pos_++;
    auto scheduler_id = pos_ % (N != 0 ? N : 1) + (N != 0);
    create_actor_on_scheduler<HttpInboundConnection>("HttpInboundConnection", scheduler_id, std::move(fd), 1024 * 1024,
                                                     0, 0,
                                                     create_actor_on_scheduler<HelloWorld>("HelloWorld", scheduler_id))
        .release();
  }
  void hangup() override {
    // may be it should be default?..
    LOG(ERROR) << "Hanging up..";
    stop();
  }

 private:
  ActorOwn<TcpListener> listener_;
  int pos_{0};
};

int main() {
  SET_VERBOSITY_LEVEL(VERBOSITY_NAME(ERROR));
  auto scheduler = make_unique<ConcurrentScheduler>();
  scheduler->init(N);
  scheduler->create_actor_unsafe<Server>(0, "Server").release();
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
