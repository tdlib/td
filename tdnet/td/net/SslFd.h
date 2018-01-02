//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/port/Fd.h"
#include "td/utils/port/SocketFd.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"

#include <openssl/ssl.h>  // TODO can we remove it from header and make target_link_libraries dependence PRIVATE?

namespace td {

class SslFd {
 public:
  enum class VerifyPeer { On, Off };
  static Result<SslFd> init(SocketFd fd, CSlice host, CSlice cert_file = CSlice(),
                            VerifyPeer verify_peer = VerifyPeer::On) TD_WARN_UNUSED_RESULT;

  SslFd(const SslFd &other) = delete;
  SslFd &operator=(const SslFd &other) = delete;
  SslFd(SslFd &&other)
      : fd_(std::move(other.fd_))
      , write_mask_(other.write_mask_)
      , read_mask_(other.read_mask_)
      , ssl_handle_(other.ssl_handle_)
      , ssl_ctx_(other.ssl_ctx_) {
    other.ssl_handle_ = nullptr;
    other.ssl_ctx_ = nullptr;
  }
  SslFd &operator=(SslFd &&other) {
    close();

    fd_ = std::move(other.fd_);
    write_mask_ = other.write_mask_;
    read_mask_ = other.read_mask_;
    ssl_handle_ = other.ssl_handle_;
    ssl_ctx_ = other.ssl_ctx_;

    other.ssl_handle_ = nullptr;
    other.ssl_ctx_ = nullptr;
    return *this;
  }

  const Fd &get_fd() const {
    return fd_.get_fd();
  }

  Fd &get_fd() {
    return fd_.get_fd();
  }

  Status get_pending_error() TD_WARN_UNUSED_RESULT {
    return fd_.get_pending_error();
  }

  Result<size_t> write(Slice slice) TD_WARN_UNUSED_RESULT;
  Result<size_t> read(MutableSlice slice) TD_WARN_UNUSED_RESULT;

  void close();

  int32 get_flags() const {
    int32 res = 0;
    int32 fd_flags = fd_.get_flags();
    fd_flags &= ~Fd::Error;
    if (fd_flags & Fd::Close) {
      res |= Fd::Close;
    }
    write_mask_ &= ~fd_flags;
    read_mask_ &= ~fd_flags;
    if (write_mask_ == 0) {
      res |= Fd::Write;
    }
    if (read_mask_ == 0) {
      res |= Fd::Read;
    }
    return res;
  }

  bool empty() const {
    return fd_.empty();
  }

  ~SslFd() {
    close();
  }

 private:
  static constexpr bool VERIFY_PEER = true;
  static constexpr int VERIFY_DEPTH = 10;

  SocketFd fd_;
  mutable int write_mask_ = 0;
  mutable int read_mask_ = 0;

  // TODO unique_ptr
  SSL *ssl_handle_ = nullptr;
  SSL_CTX *ssl_ctx_ = nullptr;

  SslFd(SocketFd &&fd, SSL *ssl_handle_, SSL_CTX *ssl_ctx_);

  Result<size_t> process_ssl_error(int ret, int *mask) TD_WARN_UNUSED_RESULT;
};

}  // namespace td
