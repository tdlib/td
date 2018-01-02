//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/net/SslFd.h"

#include "td/utils/logging.h"
#include "td/utils/StackAllocator.h"
#include "td/utils/StringBuilder.h"
#include "td/utils/Time.h"

#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>

#include <map>
#include <mutex>

namespace td {

#if !TD_WINDOWS
static int verify_callback(int preverify_ok, X509_STORE_CTX *ctx) {
  if (!preverify_ok) {
    char buf[256];
    X509_NAME_oneline(X509_get_subject_name(X509_STORE_CTX_get_current_cert(ctx)), buf, 256);

    int err = X509_STORE_CTX_get_error(ctx);
    auto warning = PSTRING() << "verify error:num=" << err << ":" << X509_verify_cert_error_string(err)
                             << ":depth=" << X509_STORE_CTX_get_error_depth(ctx) << ":" << buf;
    double now = Time::now();

    static std::mutex warning_mutex;
    {
      std::lock_guard<std::mutex> lock(warning_mutex);
      static std::map<std::string, double> next_warning_time;
      double &next = next_warning_time[warning];
      if (next <= now) {
        next = now + 300;  // one warning per 5 minutes
        LOG(WARNING) << warning;
      }
    }
  }

  return preverify_ok;
}
#endif

namespace {

Status create_openssl_error(int code, Slice message) {
  const int buf_size = 1 << 12;
  auto buf = StackAllocator::alloc(buf_size);
  StringBuilder sb(buf.as_slice());

  sb << message;
  while (unsigned long error_code = ERR_get_error()) {
    sb << "{" << error_code << ", " << ERR_error_string(error_code, nullptr) << "}";
  }
  LOG_IF(ERROR, sb.is_error()) << "OPENSSL error buffer overflow";
  return Status::Error(code, sb.as_cslice());
}

void openssl_clear_errors(Slice from) {
  if (ERR_peek_error() != 0) {
    LOG(ERROR) << from << ": " << create_openssl_error(0, "Unprocessed OPENSSL_ERROR");
  }
  errno = 0;
}

void do_ssl_shutdown(SSL *ssl_handle) {
  if (!SSL_is_init_finished(ssl_handle)) {
    return;
  }
  openssl_clear_errors("Before SSL_shutdown");
  SSL_set_quiet_shutdown(ssl_handle, 1);
  SSL_shutdown(ssl_handle);
  openssl_clear_errors("After SSL_shutdown");
}

}  // namespace

SslFd::SslFd(SocketFd &&fd, SSL *ssl_handle_, SSL_CTX *ssl_ctx_)
    : fd_(std::move(fd)), ssl_handle_(ssl_handle_), ssl_ctx_(ssl_ctx_) {
}

Result<SslFd> SslFd::init(SocketFd fd, CSlice host, CSlice cert_file, VerifyPeer verify_peer) {
#if TD_WINDOWS
  return Status::Error("TODO");
#else
  static bool init_openssl = [] {
#if OPENSSL_VERSION_NUMBER >= 0x10100000L
    return OPENSSL_init_ssl(0, nullptr) != 0;
#else
    OpenSSL_add_all_algorithms();
    SSL_load_error_strings();
    return OpenSSL_add_ssl_algorithms() != 0;
#endif
  }();
  CHECK(init_openssl);

  openssl_clear_errors("Before SslFd::init");
  CHECK(!fd.empty());

  auto ssl_method =
#if OPENSSL_VERSION_NUMBER >= 0x10100000L
      TLS_client_method();
#else
      SSLv23_client_method();
#endif
  if (ssl_method == nullptr) {
    return create_openssl_error(-6, "Failed to create an SSL client method");
  }

  auto ssl_ctx = SSL_CTX_new(ssl_method);
  if (ssl_ctx == nullptr) {
    return create_openssl_error(-7, "Failed to create an SSL context");
  }
  auto ssl_ctx_guard = ScopeExit() + [&]() { SSL_CTX_free(ssl_ctx); };
  long options = 0;
#ifdef SSL_OP_NO_SSLv2
  options |= SSL_OP_NO_SSLv2;
#endif
#ifdef SSL_OP_NO_SSLv3
  options |= SSL_OP_NO_SSLv3;
#endif
  SSL_CTX_set_options(ssl_ctx, options);
  SSL_CTX_set_mode(ssl_ctx, SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER | SSL_MODE_ENABLE_PARTIAL_WRITE);

  if (cert_file.empty()) {
    SSL_CTX_set_default_verify_paths(ssl_ctx);
  } else {
    if (SSL_CTX_load_verify_locations(ssl_ctx, cert_file.c_str(), nullptr) == 0) {
      return create_openssl_error(-8, "Failed to set custom cert file");
    }
  }
  if (VERIFY_PEER && verify_peer == VerifyPeer::On) {
    SSL_CTX_set_verify(ssl_ctx, SSL_VERIFY_PEER, verify_callback);

    if (VERIFY_DEPTH != -1) {
      SSL_CTX_set_verify_depth(ssl_ctx, VERIFY_DEPTH);
    }
  } else {
    SSL_CTX_set_verify(ssl_ctx, SSL_VERIFY_NONE, nullptr);
  }

  // TODO(now): cipher list
  string cipher_list;
  if (SSL_CTX_set_cipher_list(ssl_ctx, cipher_list.empty() ? "DEFAULT" : cipher_list.c_str()) == 0) {
    return create_openssl_error(-9, PSLICE("Failed to set cipher list \"%s\"", cipher_list.c_str()));
  }

  auto ssl_handle = SSL_new(ssl_ctx);
  if (ssl_handle == nullptr) {
    return create_openssl_error(-13, "Failed to create an SSL handle");
  }
  auto ssl_handle_guard = ScopeExit() + [&]() {
    do_ssl_shutdown(ssl_handle);
    SSL_free(ssl_handle);
  };

#if OPENSSL_VERSION_NUMBER >= 0x10002000L
  X509_VERIFY_PARAM *param = SSL_get0_param(ssl_handle);
  /* Enable automatic hostname checks */
  // TODO: X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS
  X509_VERIFY_PARAM_set_hostflags(param, 0);
  X509_VERIFY_PARAM_set1_host(param, host.c_str(), 0);
#else
#warning DANGEROUS! HTTPS HOST WILL NOT BE CHECKED. INSTALL OPENSSL >= 1.0.2 OR IMPLEMENT HTTPS HOST CHECK MANUALLY
#endif

  if (!SSL_set_fd(ssl_handle, fd.get_fd().get_native_fd())) {
    return create_openssl_error(-14, "Failed to set fd");
  }

#if OPENSSL_VERSION_NUMBER >= 0x0090806fL && !defined(OPENSSL_NO_TLSEXT)
  auto host_str = host.str();
  SSL_set_tlsext_host_name(ssl_handle, MutableCSlice(host_str).begin());
#endif
  SSL_set_connect_state(ssl_handle);

  ssl_ctx_guard.dismiss();
  ssl_handle_guard.dismiss();
  return SslFd(std::move(fd), ssl_handle, ssl_ctx);
#endif
}

Result<size_t> SslFd::process_ssl_error(int ret, int *mask) {
#if TD_WINDOWS
  return Status::Error("TODO");
#else
  auto openssl_errno = errno;
  int error = SSL_get_error(ssl_handle_, ret);
  LOG(INFO) << "SSL ERROR: " << ret << " " << error;
  switch (error) {
    case SSL_ERROR_NONE:
      LOG(ERROR) << "SSL_get_error returned no error";
      return 0;
    case SSL_ERROR_ZERO_RETURN:
      LOG(DEBUG) << "SSL_ERROR_ZERO_RETURN";
      fd_.get_fd().update_flags(Fd::Close);
      write_mask_ |= Fd::Error;
      *mask |= Fd::Error;
      return 0;
    case SSL_ERROR_WANT_READ:
      LOG(DEBUG) << "SSL_ERROR_WANT_READ";
      fd_.get_fd().clear_flags(Fd::Read);
      *mask |= Fd::Read;
      return 0;
    case SSL_ERROR_WANT_WRITE:
      LOG(DEBUG) << "SSL_ERROR_WANT_WRITE";
      fd_.get_fd().clear_flags(Fd::Write);
      *mask |= Fd::Write;
      return 0;
    case SSL_ERROR_WANT_CONNECT:
    case SSL_ERROR_WANT_ACCEPT:
    case SSL_ERROR_WANT_X509_LOOKUP:
      LOG(DEBUG) << "SSL_ERROR: CONNECT ACCEPT LOOKUP";
      fd_.get_fd().clear_flags(Fd::Write);
      *mask |= Fd::Write;
      return 0;
    case SSL_ERROR_SYSCALL:
      LOG(DEBUG) << "SSL_ERROR_SYSCALL";
      if (ERR_peek_error() == 0) {
        if (openssl_errno != 0) {
          CHECK(openssl_errno != EAGAIN);
          return Status::PosixError(openssl_errno, "SSL_ERROR_SYSCALL");
        } else {
          // Socket was closed from the other side, probably. Not an error
          fd_.get_fd().update_flags(Fd::Close);
          write_mask_ |= Fd::Error;
          *mask |= Fd::Error;
          return 0;
        }
      }
    /* fall through */
    default:
      LOG(DEBUG) << "SSL_ERROR Default";
      fd_.get_fd().update_flags(Fd::Close);
      write_mask_ |= Fd::Error;
      read_mask_ |= Fd::Error;
      return create_openssl_error(1, "SSL error ");
  }
#endif
}

Result<size_t> SslFd::write(Slice slice) {
  openssl_clear_errors("Before SslFd::write");
  auto size = SSL_write(ssl_handle_, slice.data(), static_cast<int>(slice.size()));
  if (size <= 0) {
    return process_ssl_error(size, &write_mask_);
  }
  return size;
}
Result<size_t> SslFd::read(MutableSlice slice) {
  openssl_clear_errors("Before SslFd::read");
  auto size = SSL_read(ssl_handle_, slice.data(), static_cast<int>(slice.size()));
  if (size <= 0) {
    return process_ssl_error(size, &read_mask_);
  }
  return size;
}

void SslFd::close() {
  if (fd_.empty()) {
    CHECK(!ssl_handle_ && !ssl_ctx_);
    return;
  }
  CHECK(ssl_handle_ && ssl_ctx_);
  do_ssl_shutdown(ssl_handle_);
  SSL_free(ssl_handle_);
  ssl_handle_ = nullptr;
  SSL_CTX_free(ssl_ctx_);
  ssl_ctx_ = nullptr;
  fd_.close();
}

}  // namespace td
