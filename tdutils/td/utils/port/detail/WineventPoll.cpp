//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/utils/port/detail/WineventPoll.h"

char disable_linker_warning_about_empty_file_wineventpoll_cpp TD_UNUSED;

#ifdef TD_POLL_WINEVENT

#include "td/utils/common.h"
#include "td/utils/logging.h"
#include "td/utils/port/PollBase.h"
#include "td/utils/Status.h"

#include <utility>

namespace td {
namespace detail {
IOCP::~IOCP() {
  clear();
}

void IOCP::loop() {
  IOCP::Guard guard(this);
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
      auto callback = reinterpret_cast<IOCP::Callback *>(key);
      if (callback == nullptr) {
        // LOG(ERROR) << "Interrupt IOCP loop";
        return;
      }
      callback->on_iocp(bytes, overlapped);
    } else {
      if (overlapped != nullptr) {
        auto error = OS_ERROR("Received from IOCP");
        auto callback = reinterpret_cast<IOCP::Callback *>(key);
        CHECK(callback != nullptr);
        callback->on_iocp(std::move(error), overlapped);
      }
    }
  }
}

void IOCP::interrupt_loop() {
  post(0, nullptr, nullptr);
}

void IOCP::init() {
  CHECK(!iocp_handle_);
  auto res = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
  if (res == nullptr) {
    auto error = OS_ERROR("IOCP creation failed");
    LOG(FATAL) << error;
  }
  iocp_handle_ = NativeFd(res);
}

void IOCP::clear() {
  iocp_handle_.close();
}

void IOCP::subscribe(const NativeFd &native_fd, Callback *callback) {
  CHECK(iocp_handle_);
  auto iocp_handle =
      CreateIoCompletionPort(native_fd.fd(), iocp_handle_.fd(), reinterpret_cast<ULONG_PTR>(callback), 0);
  if (iocp_handle == INVALID_HANDLE_VALUE) {
    auto error = OS_ERROR("CreateIoCompletionPort");
    LOG(FATAL) << error;
  }
  CHECK(iocp_handle == iocp_handle_.fd()) << iocp_handle << " " << iocp_handle_.fd();
}

void IOCP::post(size_t size, Callback *callback, WSAOVERLAPPED *overlapped) {
  PostQueuedCompletionStatus(iocp_handle_.fd(), DWORD(size), reinterpret_cast<ULONG_PTR>(callback),
                             reinterpret_cast<OVERLAPPED *>(overlapped));
}

void WineventPoll::init() {
}

void WineventPoll::clear() {
}

void WineventPoll::subscribe(PollableFd fd, PollFlags flags) {
  fd.release_as_list_node();
}

void WineventPoll::unsubscribe(PollableFdRef fd) {
  fd.lock();
}

void WineventPoll::unsubscribe_before_close(PollableFdRef fd) {
  unsubscribe(std::move(fd));
}

void WineventPoll::run(int timeout_ms) {
  UNREACHABLE();
}

}  // namespace detail
}  // namespace td

#endif
