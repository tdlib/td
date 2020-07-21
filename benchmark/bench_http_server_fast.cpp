//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/actor/actor.h"

#include "td/net/HttpHeaderCreator.h"
#include "td/net/HttpQuery.h"
#include "td/net/HttpReader.h"
#include "td/net/TcpListener.h"

#include "td/utils/buffer.h"
#include "td/utils/BufferedFd.h"
#include "td/utils/logging.h"
#include "td/utils/port/detail/PollableFd.h"
#include "td/utils/port/SocketFd.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"

namespace td {

class HttpEchoConnection : public Actor {
 public:
  explicit HttpEchoConnection(SocketFd fd) : fd_(std::move(fd)) {
  }

 private:
  BufferedFd<SocketFd> fd_;
  HttpReader reader_;
  HttpQuery query_;
  void start_up() override {
    Scheduler::subscribe(fd_.get_poll_info().extract_pollable_fd(this));
    reader_.init(&fd_.input_buffer(), 1024 * 1024, 0);
  }
  void tear_down() override {
    Scheduler::unsubscribe_before_close(fd_.get_poll_info().get_pollable_fd_ref());
    fd_.close();
  }

  void handle_query() {
    query_ = HttpQuery();
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
    fd_.output_buffer().append(res.ok());
  }

  void loop() override {
    sync_with_poll(fd_);
    auto status = [&] {
      TRY_STATUS(loop_read());
      TRY_STATUS(loop_write());
      return Status::OK();
    }();
    if (status.is_error() || can_close_local(fd_)) {
      stop();
    }
  }
  Status loop_read() {
    TRY_STATUS(fd_.flush_read());
    while (true) {
      TRY_RESULT(need, reader_.read_next(&query_));
      if (need == 0) {
        handle_query();
      } else {
        break;
      }
    }
    return Status::OK();
  }
  Status loop_write() {
    TRY_STATUS(fd_.flush_write());
    return Status::OK();
  }
};

const int N = 8;
class Server : public TcpListener::Callback {
 public:
  void start_up() override {
    listener_ = create_actor<TcpListener>("Listener", 8082, ActorOwn<TcpListener::Callback>(actor_id(this)));
  }
  void accept(SocketFd fd) override {
    pos_++;
    auto scheduler_id = pos_ % (N != 0 ? N : 1) + (N != 0);
    create_actor_on_scheduler<HttpEchoConnection>("HttpInboundConnection", scheduler_id, std::move(fd)).release();
  }
  void hangup() override {
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
