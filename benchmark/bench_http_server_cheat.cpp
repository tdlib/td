//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/actor/actor.h"

#include "td/net/HttpHeaderCreator.h"
#include "td/net/HttpInboundConnection.h"
#include "td/net/TcpListener.h"

#include "td/utils/buffer.h"
#include "td/utils/logging.h"
#include "td/utils/port/detail/PollableFd.h"
#include "td/utils/port/SocketFd.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"

#include <array>

namespace td {

// HttpInboundConnection header
static int cnt = 0;
class HelloWorld : public Actor {
 public:
  explicit HelloWorld(SocketFd socket_fd) : socket_fd_(std::move(socket_fd)) {
  }

 private:
  SocketFd socket_fd_;

  std::array<char, 1024> read_buf;
  size_t read_new_lines{0};

  std::string hello_;
  std::string write_buf_;
  size_t write_pos_{0};

  void start_up() override {
    Scheduler::subscribe(socket_fd_.get_poll_info().extract_pollable_fd(this));
    HttpHeaderCreator hc;
    Slice content = "hello world";
    //auto content = BufferSlice("hello world");
    hc.init_ok();
    hc.set_keep_alive();
    hc.set_content_size(content.size());
    hc.add_header("Server", "TDLib/test");
    hc.add_header("Date", "Thu Dec 14 01:41:50 2017");
    hc.add_header("Content-Type:", "text/html");
    hello_ = hc.finish(content).ok().str();
  }

  void loop() override {
    auto status = do_loop();
    if (status.is_error()) {
      Scheduler::unsubscribe(socket_fd_.get_poll_info().get_pollable_fd_ref());
      stop();
      LOG(ERROR) << "CLOSE: " << status;
    }
  }
  Status do_loop() {
    sync_with_poll(socket_fd_);
    TRY_STATUS(read_loop());
    TRY_STATUS(write_loop());
    if (can_close_local(socket_fd_)) {
      return Status::Error("CLOSE");
    }
    return Status::OK();
  }
  Status write_loop() {
    while (can_write_local(socket_fd_) && write_pos_ < write_buf_.size()) {
      TRY_RESULT(written, socket_fd_.write(Slice(write_buf_).substr(write_pos_)));
      write_pos_ += written;
      if (write_pos_ == write_buf_.size()) {
        write_pos_ = 0;
        write_buf_.clear();
      }
    }
    return Status::OK();
  }
  Status read_loop() {
    while (can_read_local(socket_fd_)) {
      TRY_RESULT(read_size, socket_fd_.read(MutableSlice(read_buf.data(), read_buf.size())));
      for (size_t i = 0; i < read_size; i++) {
        if (read_buf[i] == '\n') {
          read_new_lines++;
          if (read_new_lines == 2) {
            read_new_lines = 0;
            write_buf_.append(hello_);
          }
        }
      }
    }
    return Status::OK();
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
    create_actor_on_scheduler<HelloWorld>("HttpInboundConnection", scheduler_id, std::move(fd)).release();
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
