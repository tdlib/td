//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2019
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/utils/port/detail/Iocp.h"

char disable_linker_warning_about_empty_file_iocp_cpp TD_UNUSED;

#ifdef TD_PORT_WINDOWS

#include "td/utils/logging.h"

namespace td {
namespace detail {
Iocp::~Iocp() {
  clear();
}

void Iocp::loop() {
  Iocp::Guard guard(this);
  while (true) {
    DWORD bytes = 0;
    ULONG_PTR key = 0;
    WSAOVERLAPPED *overlapped = nullptr;
    BOOL ok =
        GetQueuedCompletionStatus(iocp_handle_.fd(), &bytes, &key, reinterpret_cast<OVERLAPPED **>(&overlapped), 1000);
    if (bytes || key || overlapped) {
      // LOG(ERROR) << "Got IOCP " << bytes << " " << key << " " << overlapped;
    }
    if (ok) {
      auto callback = reinterpret_cast<Iocp::Callback *>(key);
      if (callback == nullptr) {
        // LOG(ERROR) << "Interrupt IOCP loop";
        return;
      }
      callback->on_iocp(bytes, overlapped);
    } else {
      if (overlapped != nullptr) {
        auto error = OS_ERROR("Received from IOCP");
        auto callback = reinterpret_cast<Iocp::Callback *>(key);
        CHECK(callback != nullptr);
        callback->on_iocp(std::move(error), overlapped);
      }
    }
  }
}

void Iocp::interrupt_loop() {
  post(0, nullptr, nullptr);
}

void Iocp::init() {
  CHECK(!iocp_handle_);
  auto res = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
  if (res == nullptr) {
    auto error = OS_ERROR("IOCP creation failed");
    LOG(FATAL) << error;
  }
  iocp_handle_ = NativeFd(res);
}

void Iocp::clear() {
  iocp_handle_.close();
}

void Iocp::subscribe(const NativeFd &native_fd, Callback *callback) {
  CHECK(iocp_handle_);
  auto iocp_handle =
      CreateIoCompletionPort(native_fd.fd(), iocp_handle_.fd(), reinterpret_cast<ULONG_PTR>(callback), 0);
  if (iocp_handle == nullptr) {
    auto error = OS_ERROR("CreateIoCompletionPort");
    LOG(FATAL) << error;
  }
  LOG_CHECK(iocp_handle == iocp_handle_.fd()) << iocp_handle << " " << iocp_handle_.fd();
}

void Iocp::post(size_t size, Callback *callback, WSAOVERLAPPED *overlapped) {
  if (PostQueuedCompletionStatus(iocp_handle_.fd(), DWORD(size), reinterpret_cast<ULONG_PTR>(callback),
                                 reinterpret_cast<OVERLAPPED *>(overlapped)) == 0) {
    auto error = OS_ERROR("IOCP post failed");
    LOG(FATAL) << error;
  }
}

}  // namespace detail
}  // namespace td

#endif
