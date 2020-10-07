//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/net/SslStream.h"

#if !TD_EMSCRIPTEN
#include "td/utils/common.h"
#include "td/utils/crypto.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/port/IPAddress.h"
#include "td/utils/port/wstring_convert.h"
#include "td/utils/Status.h"
#include "td/utils/Time.h"

#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include <cstring>
#include <map>
#include <memory>
#include <mutex>

#if TD_PORT_WINDOWS
#include <wincrypt.h>
#endif

namespace td {

namespace detail {
namespace {
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
      return 0;
    case BIO_CTRL_POP:
      return 0;
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

int verify_callback(int preverify_ok, X509_STORE_CTX *ctx) {
  if (!preverify_ok) {
    char buf[256];
    X509_NAME_oneline(X509_get_subject_name(X509_STORE_CTX_get_current_cert(ctx)), buf, 256);

    int err = X509_STORE_CTX_get_error(ctx);
    auto warning = PSTRING() << "verify error:num=" << err << ":" << X509_verify_cert_error_string(err)
                             << ":depth=" << X509_STORE_CTX_get_error_depth(ctx) << ":" << Slice(buf, std::strlen(buf));
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

using SslCtx = std::shared_ptr<SSL_CTX>;

struct SslHandleDeleter {
  void operator()(SSL *ssl_handle) {
    if (SSL_is_init_finished(ssl_handle)) {
      clear_openssl_errors("Before SSL_shutdown");
      SSL_set_quiet_shutdown(ssl_handle, 1);
      SSL_shutdown(ssl_handle);
      clear_openssl_errors("After SSL_shutdown");
    }
    SSL_free(ssl_handle);
  }
};

using SslHandle = std::unique_ptr<SSL, SslHandleDeleter>;

Result<SslCtx> do_create_ssl_ctx(CSlice cert_file, SslStream::VerifyPeer verify_peer) {
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
  if (!ssl_ctx) {
    return create_openssl_error(-7, "Failed to create an SSL context");
  }
  auto ssl_ctx_ptr = SslCtx(ssl_ctx, SSL_CTX_free);
  long options = 0;
#ifdef SSL_OP_NO_SSLv2
  options |= SSL_OP_NO_SSLv2;
#endif
#ifdef SSL_OP_NO_SSLv3
  options |= SSL_OP_NO_SSLv3;
#endif
  SSL_CTX_set_options(ssl_ctx, options);
#if OPENSSL_VERSION_NUMBER >= 0x10100000L
  SSL_CTX_set_min_proto_version(ssl_ctx, TLS1_VERSION);
#endif
  SSL_CTX_set_mode(ssl_ctx, SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER | SSL_MODE_ENABLE_PARTIAL_WRITE);

  if (cert_file.empty()) {
#if TD_PORT_WINDOWS
    LOG(DEBUG) << "Begin to load system store";
    auto flags = CERT_STORE_OPEN_EXISTING_FLAG | CERT_STORE_READONLY_FLAG | CERT_SYSTEM_STORE_CURRENT_USER;
    HCERTSTORE system_store =
        CertOpenStore(CERT_STORE_PROV_SYSTEM_W, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, HCRYPTPROV_LEGACY(), flags,
                      static_cast<const void *>(to_wstring("ROOT").ok().c_str()));

    if (system_store) {
      X509_STORE *store = X509_STORE_new();

      for (PCCERT_CONTEXT cert_context = CertEnumCertificatesInStore(system_store, nullptr); cert_context != nullptr;
           cert_context = CertEnumCertificatesInStore(system_store, cert_context)) {
        const unsigned char *in = cert_context->pbCertEncoded;
        X509 *x509 = d2i_X509(nullptr, &in, static_cast<long>(cert_context->cbCertEncoded));
        if (x509 != nullptr) {
          if (X509_STORE_add_cert(store, x509) != 1) {
            auto error_code = ERR_peek_error();
            auto error = create_openssl_error(-20, "Failed to add certificate");
            if (ERR_GET_REASON(error_code) != X509_R_CERT_ALREADY_IN_HASH_TABLE) {
              LOG(ERROR) << error;
            } else {
              LOG(INFO) << error;
            }
          }

          X509_free(x509);
        } else {
          LOG(ERROR) << create_openssl_error(-21, "Failed to load X509 certificate");
        }
      }

      CertCloseStore(system_store, 0);

      SSL_CTX_set_cert_store(ssl_ctx, store);
      LOG(DEBUG) << "End to load system store";
    } else {
      LOG(ERROR) << create_openssl_error(-22, "Failed to open system certificate store");
    }
#else
    if (SSL_CTX_set_default_verify_paths(ssl_ctx) == 0) {
      auto error = create_openssl_error(-8, "Failed to load default verify paths");
      if (verify_peer == SslStream::VerifyPeer::On) {
        return std::move(error);
      } else {
        LOG(ERROR) << error;
      }
    }
#endif
  } else {
    if (SSL_CTX_load_verify_locations(ssl_ctx, cert_file.c_str(), nullptr) == 0) {
      return create_openssl_error(-8, "Failed to set custom certificate file");
    }
  }

  if (verify_peer == SslStream::VerifyPeer::On) {
    SSL_CTX_set_verify(ssl_ctx, SSL_VERIFY_PEER, verify_callback);

    constexpr int DEFAULT_VERIFY_DEPTH = 10;
    SSL_CTX_set_verify_depth(ssl_ctx, DEFAULT_VERIFY_DEPTH);
  } else {
    SSL_CTX_set_verify(ssl_ctx, SSL_VERIFY_NONE, nullptr);
  }

  string cipher_list;
  if (SSL_CTX_set_cipher_list(ssl_ctx, cipher_list.empty() ? "DEFAULT" : cipher_list.c_str()) == 0) {
    return create_openssl_error(-9, PSLICE() << "Failed to set cipher list \"" << cipher_list << '"');
  }

  return std::move(ssl_ctx_ptr);
}

Result<SslCtx> get_default_ssl_ctx() {
  static auto ctx = do_create_ssl_ctx("", SslStream::VerifyPeer::On);
  if (ctx.is_error()) {
    return ctx.error().clone();
  }

  return ctx.ok();
}

Result<SslCtx> get_default_unverified_ssl_ctx() {
  static auto ctx = do_create_ssl_ctx("", SslStream::VerifyPeer::Off);
  if (ctx.is_error()) {
    return ctx.error().clone();
  }

  return ctx.ok();
}

Result<SslCtx> create_ssl_ctx(CSlice cert_file, SslStream::VerifyPeer verify_peer) {
  if (cert_file.empty()) {
    if (verify_peer == SslStream::VerifyPeer::On) {
      return get_default_ssl_ctx();
    } else {
      return get_default_unverified_ssl_ctx();
    }
  }
  return do_create_ssl_ctx(cert_file, verify_peer);
}

}  // namespace

class SslStreamImpl {
 public:
  Status init(CSlice host, CSlice cert_file, SslStream::VerifyPeer verify_peer, bool check_ip_address_as_host) {
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

    clear_openssl_errors("Before SslFd::init");

    TRY_RESULT(ssl_ctx, create_ssl_ctx(cert_file, verify_peer));

    auto ssl_handle = SslHandle(SSL_new(ssl_ctx.get()));
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
    if (r_ip_address.is_error()) {  // IP address must not be send as SNI
      LOG(DEBUG) << "Set SNI host name to " << host;
      auto host_str = host.str();
      SSL_set_tlsext_host_name(ssl_handle.get(), MutableCSlice(host_str).begin());
    }
#endif
    SSL_set_connect_state(ssl_handle.get());

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

  friend class SslReadByteFlow;
  friend class SslWriteByteFlow;

  Result<size_t> write(Slice slice) {
    clear_openssl_errors("Before SslFd::write");
    auto size = SSL_write(ssl_handle_.get(), slice.data(), static_cast<int>(slice.size()));
    if (size <= 0) {
      return process_ssl_error(size);
    }
    return size;
  }

  Result<size_t> read(MutableSlice slice) {
    clear_openssl_errors("Before SslFd::read");
    auto size = SSL_read(ssl_handle_.get(), slice.data(), static_cast<int>(slice.size()));
    if (size <= 0) {
      return process_ssl_error(size);
    }
    return size;
  }

  class SslReadByteFlow : public ByteFlowBase {
   public:
    explicit SslReadByteFlow(SslStreamImpl *stream) : stream_(stream) {
    }
    bool loop() override {
      auto to_read = output_.prepare_append();
      auto r_size = stream_->read(to_read);
      if (r_size.is_error()) {
        finish(r_size.move_as_error());
        return false;
      }
      auto size = r_size.move_as_ok();
      if (size == 0) {
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

  class SslWriteByteFlow : public ByteFlowBase {
   public:
    explicit SslWriteByteFlow(SslStreamImpl *stream) : stream_(stream) {
    }
    bool loop() override {
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
      return data.size();
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
        /* fall through */
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
  int res = narrow_cast<int>(stream->flow_read(MutableSlice(buf, len)));
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
  return narrow_cast<int>(stream->flow_write(Slice(buf, len)));
}
}  // namespace

}  // namespace detail

SslStream::SslStream() = default;
SslStream::SslStream(SslStream &&) = default;
SslStream &SslStream::operator=(SslStream &&) = default;
SslStream::~SslStream() = default;

Result<SslStream> SslStream::create(CSlice host, CSlice cert_file, VerifyPeer verify_peer,
                                    bool use_ip_address_as_host) {
  auto impl = make_unique<detail::SslStreamImpl>();
  TRY_STATUS(impl->init(host, cert_file, verify_peer, use_ip_address_as_host));
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
SslStream::SslStream(SslStream &&) = default;
SslStream &SslStream::operator=(SslStream &&) = default;
SslStream::~SslStream() = default;

Result<SslStream> SslStream::create(CSlice host, CSlice cert_file, VerifyPeer verify_peer,
                                    bool check_ip_address_as_host) {
  return Status::Error("Not supported in emscripten");
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
