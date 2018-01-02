//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/net/TcpListener.h"

#include "td/utils/logging.h"
#include "td/utils/port/Fd.h"

namespace td {
// TcpListener implementation
TcpListener::TcpListener(int port, ActorShared<Callback> callback) : port_(port), callback_(std::move(callback)) {
}

void TcpListener::hangup() {
  stop();
}

void TcpListener::start_up() {
  auto r_socket = ServerSocketFd::open(port_);
  if (r_socket.is_error()) {
    LOG(ERROR) << "Can't open server socket: " << r_socket.error();
    set_timeout_in(5);
    return;
  }
  server_fd_ = r_socket.move_as_ok();
  server_fd_.get_fd().set_observer(this);
  subscribe(server_fd_.get_fd());
}

void TcpListener::tear_down() {
  LOG(ERROR) << "TcpListener closed";
  if (!server_fd_.empty()) {
    unsubscribe_before_close(server_fd_.get_fd());
    server_fd_.close();
  }
}

void TcpListener::loop() {
  if (server_fd_.empty()) {
    start_up();
  }
  while (can_read(server_fd_)) {
    auto r_socket_fd = server_fd_.accept();
    if (r_socket_fd.is_error()) {
      if (r_socket_fd.error().code() != -1) {
        LOG(ERROR) << r_socket_fd.error();
      }
      continue;
    }
    send_closure(callback_, &Callback::accept, r_socket_fd.move_as_ok());
  }

  if (can_close(server_fd_)) {
    LOG(ERROR) << "HELLO!";
    stop();
  }
}

}  // namespace td
