// SPDX-FileCopyrightText: Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: BSL-1.0 AND MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
#include "td/net/SslStream.h"

#if !TD_EMSCRIPTEN
#include "td/net/HostLatchTable.h"

#include "td/utils/common.h"
#include "td/utils/crypto.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/port/IPAddress.h"
#include "td/utils/Status.h"
#include "td/utils/Time.h"

#if defined(__has_feature)
#if __has_feature(memory_sanitizer)
#include <sanitizer/msan_interface.h>
#define TD_SSL_STREAM_MSAN_ACTIVE 1
#endif
#endif
#if defined(__SANITIZE_MEMORY__)
#include <sanitizer/msan_interface.h>
#define TD_SSL_STREAM_MSAN_ACTIVE 1
#endif
#ifndef TD_SSL_STREAM_MSAN_ACTIVE
#define TD_SSL_STREAM_MSAN_ACTIVE 0
#endif

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/x509_vfy.h>

#include <cstring>
#include <memory>
#include <mutex>

namespace td {

namespace detail {
namespace {
class ScopedMsanInterceptorChecks final {
 public:
  ScopedMsanInterceptorChecks() {
#if TD_SSL_STREAM_MSAN_ACTIVE
    __msan_scoped_disable_interceptor_checks();
#endif
  }

  ~ScopedMsanInterceptorChecks() {
#if TD_SSL_STREAM_MSAN_ACTIVE
    __msan_scoped_enable_interceptor_checks();
#endif
  }
};

#if OPENSSL_VERSION_NUMBER < 0x10100000L
void *BIO_get_data(BIO *b) {
  return b->ptr;
}
void BIO_set_data(BIO *b, void *ptr) {
  b->ptr = ptr;
}
void BIO_set_init(BIO *b, int init) {
  b->init = init;
}

int BIO_get_new_index() {
  return 0;
}
BIO_METHOD *BIO_meth_new(int type, const char *name) {
  auto res = new BIO_METHOD();
  std::memset(res, 0, sizeof(*res));
  return res;
}

int BIO_meth_set_write(BIO_METHOD *biom, int (*bwrite)(BIO *, const char *, int)) {
  biom->bwrite = bwrite;
  return 1;
}
int BIO_meth_set_read(BIO_METHOD *biom, int (*bread)(BIO *, char *, int)) {
  biom->bread = bread;
  return 1;
}
int BIO_meth_set_ctrl(BIO_METHOD *biom, long (*ctrl)(BIO *, int, long, void *)) {
  biom->ctrl = ctrl;
  return 1;
}
int BIO_meth_set_create(BIO_METHOD *biom, int (*create)(BIO *)) {
  biom->create = create;
  return 1;
}
int BIO_meth_set_destroy(BIO_METHOD *biom, int (*destroy)(BIO *)) {
  biom->destroy = destroy;
  return 1;
}
#endif

int strm_create(BIO *b) {
  BIO_set_init(b, 1);
  return 1;
}

int strm_destroy(BIO *b) {
  return 1;
}

int strm_read(BIO *b, char *buf, int len);

int strm_write(BIO *b, const char *buf, int len);

long strm_ctrl(BIO *b, int cmd, long num, void *ptr) {
  switch (cmd) {
    case BIO_CTRL_FLUSH:
      return 1;
    case BIO_CTRL_PUSH:
    case BIO_CTRL_POP:
      return 0;
#if defined(BIO_CTRL_GET_KTLS_SEND)
    case BIO_CTRL_GET_KTLS_SEND:
      return 0;
#endif
#if defined(BIO_CTRL_GET_KTLS_RECV)
    case BIO_CTRL_GET_KTLS_RECV:
      return 0;
#endif
    default:
      LOG(FATAL) << b << " " << cmd << " " << num << " " << ptr;
  }
  return 1;
}

BIO_METHOD *BIO_s_sslstream() {
  static BIO_METHOD *result = [] {
    BIO_METHOD *res = BIO_meth_new(BIO_get_new_index(), "td::SslStream helper bio");
    BIO_meth_set_write(res, strm_write);
    BIO_meth_set_read(res, strm_read);
    BIO_meth_set_create(res, strm_create);
    BIO_meth_set_destroy(res, strm_destroy);
    BIO_meth_set_ctrl(res, strm_ctrl);
    return res;
  }();
  return result;
}

struct SslHandleDeleter {
  void operator()(SSL *ssl_handle) const {
    auto start_time = Time::now();
    if (SSL_is_init_finished(ssl_handle)) {
      clear_openssl_errors("Before SSL_shutdown");
      ScopedMsanInterceptorChecks scoped_msan_interceptor_checks;
      SSL_set_quiet_shutdown(ssl_handle, 1);
      SSL_shutdown(ssl_handle);
      clear_openssl_errors("After SSL_shutdown");
    }
    SSL_free(ssl_handle);
    auto elapsed_time = Time::now() - start_time;
    if (elapsed_time >= 0.1) {
      LOG(WARNING) << "SSL_free took " << elapsed_time << " seconds";
    }
  }
};

using SslHandle = std::unique_ptr<SSL, SslHandleDeleter>;

// ── SPKI anchor verify callback ──────────────────────────────────────────────
//
// This callback is installed per-connection (via SSL_set_verify) for any host
// that falls within the routing anchor window. It fires during certificate chain
// verification in the TLS handshake (before any application data is accepted).
//
// At depth 0 (leaf certificate), the SPKI SHA-256 is extracted and checked
// against the static anchor table. Mismatch → return 0 (handshake fails closed).
// At depth > 0 (intermediates/root), the standard chain validation result is
// forwarded as-is without modification.

static int g_spki_host_ex_idx = -1;
static std::once_flag g_spki_ex_once;

static int get_spki_host_ex_idx() {
  std::call_once(g_spki_ex_once,
                 [] { g_spki_host_ex_idx = SSL_get_ex_new_index(0, nullptr, nullptr, nullptr, nullptr); });
  return g_spki_host_ex_idx;
}

static int spki_anchor_verify_callback(int preverify_ok, X509_STORE_CTX *ctx) {
  // If the chain already failed standard validation, propagate the failure.
  if (!preverify_ok) {
    return 0;
  }

  int depth = X509_STORE_CTX_get_error_depth(ctx);
  if (depth != 0) {
    // Not the leaf certificate — nothing to check at this level.
    return 1;
  }

  // Retrieve the SSL handle to access per-connection ex_data.
  int ssl_idx = SSL_get_ex_data_X509_STORE_CTX_idx();
  SSL *ssl = static_cast<SSL *>(X509_STORE_CTX_get_ex_data(ctx, ssl_idx));
  if (ssl == nullptr) {
    // Cannot retrieve SSL handle — fail closed for safety.
    LOG(ERROR) << "SPKI anchor callback: failed to retrieve SSL handle from store ctx";
    return 0;
  }

  int host_idx = get_spki_host_ex_idx();
  if (host_idx < 0) {
    // ex_data index allocation failed during init — fail closed.
    LOG(ERROR) << "SPKI anchor callback: ex_data index not initialized";
    return 0;
  }

  const char *host_ptr = static_cast<const char *>(SSL_get_ex_data(ssl, host_idx));
  if (host_ptr == nullptr) {
    // No hostname stored — this connection was not set up with a latch host.
    // This should not happen if the callback was only installed for latched hosts.
    LOG(ERROR) << "SPKI anchor callback: no host stored in ex_data";
    return 0;
  }

  X509 *leaf_cert = X509_STORE_CTX_get_current_cert(ctx);
  td::Status status = td::verify_host_latch(td::CSlice(host_ptr), leaf_cert);
  if (status.is_error()) {
    LOG(WARNING) << "SPKI anchor mismatch for host " << host_ptr << ": " << status;
    X509_STORE_CTX_set_error(ctx, X509_V_ERR_CERT_REJECTED);
    return 0;
  }
  return 1;
}

}  // namespace

class SslStreamImpl {
 public:
  ~SslStreamImpl() {
    ssl_handle_.reset();
  }

  Status init(CSlice host, SslCtx ssl_ctx, bool check_ip_address_as_host) {
    if (!ssl_ctx) {
      return Status::Error("Invalid SSL context provided");
    }
    if (std::memchr(host.begin(), '\0', host.size()) != nullptr) {
      return Status::Error("TLS host contains embedded NUL byte");
    }

    clear_openssl_errors("Before SslFd::init");

    auto ssl_handle = SslHandle(SSL_new(static_cast<SSL_CTX *>(ssl_ctx.get_openssl_ctx())));
    if (!ssl_handle) {
      return create_openssl_error(-13, "Failed to create an SSL handle");
    }

    auto r_ip_address = IPAddress::get_ip_address(host);

#if OPENSSL_VERSION_NUMBER >= 0x10002000L
    X509_VERIFY_PARAM *param = SSL_get0_param(ssl_handle.get());
    X509_VERIFY_PARAM_set_hostflags(param, 0);
    if (r_ip_address.is_ok() && !check_ip_address_as_host) {
      LOG(DEBUG) << "Set verification IP address to " << r_ip_address.ok().get_ip_str();
      X509_VERIFY_PARAM_set1_ip_asc(param, r_ip_address.ok().get_ip_str().c_str());
    } else {
      LOG(DEBUG) << "Set verification host to " << host;
      X509_VERIFY_PARAM_set1_host(param, host.c_str(), 0);
    }
#else
#warning DANGEROUS! HTTPS HOST WILL NOT BE CHECKED. INSTALL OPENSSL >= 1.0.2 OR IMPLEMENT HTTPS HOST CHECK MANUALLY
#endif

    auto *bio = BIO_new(BIO_s_sslstream());
    BIO_set_data(bio, static_cast<void *>(this));
    SSL_set_bio(ssl_handle.get(), bio, bio);

#if OPENSSL_VERSION_NUMBER >= 0x0090806fL && !defined(OPENSSL_NO_TLSEXT)
    if (r_ip_address.is_error()) {  // IP address must not be sent as SNI
      LOG(DEBUG) << "Set SNI host name to " << host;
      auto host_str = host.str();
      SSL_set_tlsext_host_name(ssl_handle.get(), MutableCSlice(host_str).begin());
    }
#endif
    SSL_set_connect_state(ssl_handle.get());

    // ── SPKI anchor callback installation ──────────────────────────────────
    // For hosts that fall within a tracked routing anchor window, install the
    // SPKI anchor verify callback. The callback fires at depth 0 during the
    // TLS handshake, before any application data can flow. Mismatch → failure.
    host_ = host.str();
    if (is_latched_host(CSlice(host_))) {
      int host_idx = get_spki_host_ex_idx();
      if (host_idx < 0) {
        return Status::Error("Failed to initialize SPKI anchor slot");
      }
      // Store a pointer to the host string in per-connection ex_data.
      // The host_ string outlives the SSL handle (both owned by this SslStreamImpl).
      if (SSL_set_ex_data(ssl_handle.get(), host_idx, static_cast<void *>(const_cast<char *>(host_.c_str()))) != 1) {
        return Status::Error("Failed to attach host to SSL handle for SPKI anchor check");
      }
      // Force peer verification for pinned host families so SPKI checks are
      // always evaluated even if the base context was created as VerifyPeer::Off.
      SSL_set_verify(ssl_handle.get(), SSL_VERIFY_PEER, spki_anchor_verify_callback);
      LOG(DEBUG) << "Installed SPKI anchor callback for host " << host_;
    }

    ssl_handle_ = std::move(ssl_handle);

    return Status::OK();
  }

  ByteFlowInterface &read_byte_flow() {
    return read_flow_;
  }
  ByteFlowInterface &write_byte_flow() {
    return write_flow_;
  }
  size_t flow_read(MutableSlice slice) {
    return read_flow_.read(slice);
  }
  size_t flow_write(Slice slice) {
    return write_flow_.write(slice);
  }

 private:
  SslHandle ssl_handle_;
  string host_;  // stored for SPKI anchor callback lifetime

  friend class SslReadByteFlow;
  friend class SslWriteByteFlow;

  Result<size_t> write(Slice slice) {
    clear_openssl_errors("Before SslFd::write");
    auto start_time = Time::now();
    ScopedMsanInterceptorChecks scoped_msan_interceptor_checks;
    auto size = SSL_write(ssl_handle_.get(), slice.data(), static_cast<int>(slice.size()));
    auto elapsed_time = Time::now() - start_time;
    if (elapsed_time >= 0.1) {
      LOG(WARNING) << "SSL_write of size " << slice.size() << " took " << elapsed_time << " seconds and returned "
                   << size << ' ' << SSL_get_error(ssl_handle_.get(), size);
    }
    if (size <= 0) {
      return process_ssl_error(size);
    }
    return size;
  }

  Result<size_t> read(MutableSlice slice) {
    clear_openssl_errors("Before SslFd::read");
    auto start_time = Time::now();
    ScopedMsanInterceptorChecks scoped_msan_interceptor_checks;
    auto size = SSL_read(ssl_handle_.get(), slice.data(), static_cast<int>(slice.size()));
    auto elapsed_time = Time::now() - start_time;
    if (elapsed_time >= 0.1) {
      LOG(WARNING) << "SSL_read took " << elapsed_time << " seconds and returned " << size << ' '
                   << SSL_get_error(ssl_handle_.get(), size);
    }
    if (size <= 0) {
      return process_ssl_error(size);
    }
#if TD_SSL_STREAM_MSAN_ACTIVE
    // libssl is not instrumented under MSan, so mark the produced plaintext as initialized.
    __msan_unpoison(slice.data(), static_cast<size_t>(size));
#endif
    return size;
  }

  bool has_received_shutdown() const {
    return (SSL_get_shutdown(ssl_handle_.get()) & SSL_RECEIVED_SHUTDOWN) != 0;
  }

  Status shutdown_write() const {
    if (!SSL_is_init_finished(ssl_handle_.get())) {
      return Status::OK();
    }
    clear_openssl_errors("Before SslFd::shutdown_write");
    ScopedMsanInterceptorChecks scoped_msan_interceptor_checks;
    auto rc = SSL_shutdown(ssl_handle_.get());
    if (rc >= 0) {
      clear_openssl_errors("After SslFd::shutdown_write");
      return Status::OK();
    }
    if (auto ssl_error = SSL_get_error(ssl_handle_.get(), rc);
        ssl_error == SSL_ERROR_WANT_READ || ssl_error == SSL_ERROR_WANT_WRITE) {
      clear_openssl_errors("After SslFd::shutdown_write retry");
      return Status::OK();
    }
    return create_openssl_error(1, "SSL shutdown error ");
  }

  class SslReadByteFlow final : public ByteFlowBase {
   public:
    explicit SslReadByteFlow(SslStreamImpl *stream) : stream_(stream) {
    }
    bool loop() final {
      auto to_read = output_.prepare_append();
      auto r_size = stream_->read(to_read);
      if (r_size.is_error()) {
        finish(r_size.move_as_error());
        return false;
      }
      auto size = r_size.move_as_ok();
      stream_->write_flow_.retry_pending_plaintext();
      if (size == 0) {
        if (stream_->has_received_shutdown() || !is_input_active_) {
          finish(Status::OK());
        }
        return false;
      }
      output_.confirm_append(size);
      return true;
    }

    size_t read(MutableSlice data) {
      return input_->advance(min(data.size(), input_->size()), data);
    }

   private:
    SslStreamImpl *stream_;
  };

  class SslWriteByteFlow final : public ByteFlowBase {
   public:
    explicit SslWriteByteFlow(SslStreamImpl *stream) : stream_(stream) {
    }
    bool loop() final {
      input_->sync_with_writer();
      if (!is_input_active_ && input_->empty()) {
        if (auto status = stream_->shutdown_write(); status.is_error()) {
          finish(std::move(status));
          return false;
        }
        finish(Status::OK());
        return false;
      }
      auto to_write = input_->prepare_read();
      auto r_size = stream_->write(to_write);
      if (r_size.is_error()) {
        finish(r_size.move_as_error());
        return false;
      }
      auto size = r_size.move_as_ok();
      if (size == 0) {
        return false;
      }
      input_->confirm_read(size);
      return true;
    }

    size_t write(Slice data) {
      output_.append(data);
      on_output_updated();
      return data.size();
    }

    void retry_pending_plaintext() {
      if (input_ == nullptr) {
        return;
      }
      input_->sync_with_writer();
      if (input_->empty()) {
        return;
      }
      reset_need_size();
      wakeup();
    }

   private:
    SslStreamImpl *stream_;
  };

  SslReadByteFlow read_flow_{this};
  SslWriteByteFlow write_flow_{this};

  Result<size_t> process_ssl_error(int ret) {
    auto os_error = OS_ERROR("SSL_ERROR_SYSCALL");
    int error = SSL_get_error(ssl_handle_.get(), ret);
    switch (error) {
      case SSL_ERROR_NONE:
        LOG(ERROR) << "SSL_get_error returned no error";
        return 0;
      case SSL_ERROR_ZERO_RETURN:
        LOG(DEBUG) << "SSL_ZERO_RETURN";
        return 0;
      case SSL_ERROR_WANT_READ:
        LOG(DEBUG) << "SSL_WANT_READ";
        return 0;
      case SSL_ERROR_WANT_WRITE:
        LOG(DEBUG) << "SSL_WANT_WRITE";
        return 0;
      case SSL_ERROR_WANT_CONNECT:
      case SSL_ERROR_WANT_ACCEPT:
      case SSL_ERROR_WANT_X509_LOOKUP:
        LOG(DEBUG) << "SSL: CONNECT ACCEPT LOOKUP";
        return 0;
      case SSL_ERROR_SYSCALL:
        if (ERR_peek_error() == 0) {
          if (os_error.code() != 0) {
            LOG(DEBUG) << "SSL_ERROR_SYSCALL";
            return std::move(os_error);
          } else {
            LOG(DEBUG) << "SSL_SYSCALL";
            return 0;
          }
        }
        [[fallthrough]];
      default:
        LOG(DEBUG) << "SSL_ERROR Default";
        return create_openssl_error(1, "SSL error ");
    }
  }
};

namespace {
int strm_read(BIO *b, char *buf, int len) {
  auto *stream = static_cast<SslStreamImpl *>(BIO_get_data(b));
  CHECK(stream != nullptr);
  BIO_clear_retry_flags(b);
  CHECK(buf != nullptr);
  auto res = narrow_cast<int>(stream->flow_read(MutableSlice(buf, len)));
  if (res == 0) {
    BIO_set_retry_read(b);
    return -1;
  }
  return res;
}
int strm_write(BIO *b, const char *buf, int len) {
  auto *stream = static_cast<SslStreamImpl *>(BIO_get_data(b));
  CHECK(stream != nullptr);
  BIO_clear_retry_flags(b);
  CHECK(buf != nullptr);
#if TD_SSL_STREAM_MSAN_ACTIVE
  if (len > 0) {
    // libssl fills BIO write buffers outside MSan instrumentation, so mark the
    // ciphertext bytes as initialized before they enter the outgoing chain.
    __msan_unpoison(const_cast<char *>(buf), static_cast<size_t>(len));
  }
#endif
  return narrow_cast<int>(stream->flow_write(Slice(buf, len)));
}
}  // namespace

}  // namespace detail

SslStream::SslStream() = default;
SslStream::SslStream(SslStream &&) noexcept = default;
SslStream &SslStream::operator=(SslStream &&) noexcept = default;
SslStream::~SslStream() = default;

Result<SslStream> SslStream::create(CSlice host, SslCtx ssl_ctx, bool use_ip_address_as_host) {
  auto impl = make_unique<detail::SslStreamImpl>();
  TRY_STATUS(impl->init(host, ssl_ctx, use_ip_address_as_host));
  return SslStream(std::move(impl));
}
SslStream::SslStream(unique_ptr<detail::SslStreamImpl> impl) : impl_(std::move(impl)) {
}
ByteFlowInterface &SslStream::read_byte_flow() {
  return impl_->read_byte_flow();
}
ByteFlowInterface &SslStream::write_byte_flow() {
  return impl_->write_byte_flow();
}
size_t SslStream::flow_read(MutableSlice slice) {
  return impl_->flow_read(slice);
}
size_t SslStream::flow_write(Slice slice) {
  return impl_->flow_write(slice);
}

}  // namespace td

#else

namespace td {

namespace detail {
class SslStreamImpl {};
}  // namespace detail

SslStream::SslStream() = default;
SslStream::SslStream(SslStream &&) noexcept = default;
SslStream &SslStream::operator=(SslStream &&) noexcept = default;
SslStream::~SslStream() = default;

Result<SslStream> SslStream::create(CSlice host, SslCtx ssl_ctx, bool check_ip_address_as_host) {
  return Status::Error("Not supported in Emscripten");
}

SslStream::SslStream(unique_ptr<detail::SslStreamImpl> impl) : impl_(std::move(impl)) {
}

ByteFlowInterface &SslStream::read_byte_flow() {
  UNREACHABLE();
}

ByteFlowInterface &SslStream::write_byte_flow() {
  UNREACHABLE();
}

size_t SslStream::flow_read(MutableSlice slice) {
  UNREACHABLE();
}

size_t SslStream::flow_write(Slice slice) {
  UNREACHABLE();
}

}  // namespace td

#endif
