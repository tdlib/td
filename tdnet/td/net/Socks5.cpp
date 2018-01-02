//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/net/Socks5.h"

#include "td/utils/format.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/port/Fd.h"
#include "td/utils/Slice.h"

namespace td {
Socks5::Socks5(SocketFd socket_fd, IPAddress ip_address, string username, string password,
               std::unique_ptr<Callback> callback, ActorShared<> parent)
    : fd_(std::move(socket_fd))
    , ip_address_(std::move(ip_address))
    , username_(std::move(username))
    , password_(std::move(password))
    , callback_(std::move(callback))
    , parent_(std::move(parent)) {
}
void Socks5::on_error(Status status) {
  CHECK(status.is_error());
  if (callback_) {
    callback_->set_result(std::move(status));
    callback_.reset();
  }
  stop();
}
void Socks5::tear_down() {
  unsubscribe(fd_.get_fd());
  fd_.get_fd().set_observer(nullptr);
  if (callback_) {
    callback_->set_result(std::move(fd_));
    callback_.reset();
  }
}

void Socks5::hangup() {
  on_error(Status::Error("Cancelled"));
}

void Socks5::start_up() {
  fd_.get_fd().set_observer(this);
  subscribe(fd_.get_fd());
  set_timeout_in(10);
}

void Socks5::send_greeting() {
  CHECK(state_ == State::SendGreeting);
  state_ = State::WaitGreetingResponse;

  string greeting;
  greeting += '\x05';
  bool use_username = !username_.empty();
  char authentication_count = use_username ? '\x02' : '\x01';
  greeting += authentication_count;
  greeting += '\0';
  if (use_username) {
    greeting += '\x02';
  }

  fd_.output_buffer().append(greeting);
}

Status Socks5::wait_greeting_response() {
  auto &buf = fd_.input_buffer();
  if (buf.size() < 2) {
    return Status::OK();
  }
  auto buffer_slice = buf.read_as_buffer_slice(2);
  auto slice = buffer_slice.as_slice();
  if (slice[0] != '\x05') {
    return Status::Error(PSLICE() << "Unsupported socks protocol version " << int(slice[0]));
  }
  auto authentication_method = slice[1];
  if (authentication_method == '\0') {
    state_ = State::SendIpAddress;
    send_ip_address();
    return Status::OK();
  }
  if (authentication_method == '\x02') {
    return send_username_password();
  }
  return Status::Error("Unsupported authentication mode");
}

Status Socks5::send_username_password() {
  if (username_.size() >= 128) {
    return Status::Error("Username is too long");
  }
  if (password_.size() >= 128) {
    return Status::Error("Password is too long");
  }

  string request;
  request += '\x01';
  request += narrow_cast<char>(username_.size());
  request += username_;
  request += narrow_cast<char>(password_.size());
  request += password_;
  fd_.output_buffer().append(request);
  state_ = State::WaitPasswordResponse;

  return Status::OK();
}

Status Socks5::wait_password_response() {
  auto &buf = fd_.input_buffer();
  if (buf.size() < 2) {
    return Status::OK();
  }
  auto buffer_slice = buf.read_as_buffer_slice(2);
  auto slice = buffer_slice.as_slice();
  if (slice[0] != '\x01') {
    return Status::Error(PSLICE() << "Unsupported socks subnegotiation protocol version " << int(slice[0]));
  }
  if (slice[1] != '\x00') {
    return Status::Error("Wrong username or password");
  }

  state_ = State::SendIpAddress;
  send_ip_address();
  return Status::OK();
}

void Socks5::send_ip_address() {
  CHECK(state_ == State::SendIpAddress);
  callback_->on_connected();
  string request;
  request += '\x05';
  request += '\x01';
  request += '\x00';
  if (ip_address_.is_ipv4()) {
    request += '\x01';
    auto ipv4 = ip_address_.get_ipv4();
    request += static_cast<char>(ipv4 & 255);
    request += static_cast<char>((ipv4 >> 8) & 255);
    request += static_cast<char>((ipv4 >> 16) & 255);
    request += static_cast<char>((ipv4 >> 24) & 255);
  } else {
    request += '\x04';
    request += ip_address_.get_ipv6().str();
  }
  auto port = ip_address_.get_port();
  request += static_cast<char>((port >> 8) & 255);
  request += static_cast<char>(port & 255);
  fd_.output_buffer().append(request);
  state_ = State::WaitIpAddressResponse;
}

Status Socks5::wait_ip_address_response() {
  CHECK(state_ == State::WaitIpAddressResponse);
  auto it = fd_.input_buffer().clone();
  if (it.size() < 4) {
    return Status::OK();
  }
  char c;
  MutableSlice c_slice(&c, 1);
  it.advance(1, c_slice);
  if (c != '\x05') {
    return Status::Error("Invalid response");
  }
  it.advance(1, c_slice);
  if (c != '\0') {
    return Status::Error(PSLICE() << tag("code", c));
  }
  it.advance(1, c_slice);
  if (c != '\0') {
    return Status::Error("byte must be zero");
  }
  it.advance(1, c_slice);
  if (c == '\x01') {
    if (it.size() < 4) {
      return Status::OK();
    }
    it.advance(4);
  } else if (c == '\x04') {
    if (it.size() < 16) {
      return Status::OK();
    }
    it.advance(16);
  } else {
    return Status::Error("Invalid response");
  }
  if (it.size() < 2) {
    return Status::OK();
  }
  it.advance(2);
  stop();
  return Status::OK();
}

void Socks5::loop() {
  auto status = [&] {
    TRY_STATUS(fd_.flush_read());
    switch (state_) {
      case State::SendGreeting:
        send_greeting();
        break;
      case State::WaitGreetingResponse:
        TRY_STATUS(wait_greeting_response());
        break;
      case State::WaitPasswordResponse:
        TRY_STATUS(wait_password_response());
        break;
      case State::WaitIpAddressResponse:
        TRY_STATUS(wait_ip_address_response());
        break;
      case State::SendIpAddress:
      case State::Stop:
        UNREACHABLE();
    }
    TRY_STATUS(fd_.flush_write());
    return Status::OK();
  }();
  if (status.is_error()) {
    on_error(std::move(status));
  }
  if (can_close(fd_)) {
    on_error(Status::Error("Connection closed"));
  }
}
void Socks5::timeout_expired() {
  on_error(Status::Error("Timeout expired"));
}
}  // namespace td
