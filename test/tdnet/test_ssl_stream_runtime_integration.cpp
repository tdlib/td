// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/net/SslCtx.h"
#include "td/net/SslStream.h"
#include "td/utils/buffer.h"
#include "td/utils/ByteFlow.h"
#include "td/utils/filesystem.h"
#include "td/utils/SliceBuilder.h"
#include "td/utils/tests.h"

#if defined(__has_feature)
#if __has_feature(memory_sanitizer)
#include <sanitizer/msan_interface.h>
#define TD_SSL_STREAM_RUNTIME_TEST_MSAN_ACTIVE 1
#endif
#endif
#if defined(__SANITIZE_MEMORY__)
#include <sanitizer/msan_interface.h>
#define TD_SSL_STREAM_RUNTIME_TEST_MSAN_ACTIVE 1
#endif
#ifndef TD_SSL_STREAM_RUNTIME_TEST_MSAN_ACTIVE
#define TD_SSL_STREAM_RUNTIME_TEST_MSAN_ACTIVE 0
#endif

#ifndef TELEMT_TEST_REPO_ROOT
#define TELEMT_TEST_REPO_ROOT ""
#endif

#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>

#include <array>
#include <memory>
#include <string>
#include <utility>

#if !TD_EMSCRIPTEN

namespace ssl_stream_runtime_test {

class ScopedMsanInterceptorChecks final {
 public:
  ScopedMsanInterceptorChecks() {
#if TD_SSL_STREAM_RUNTIME_TEST_MSAN_ACTIVE
    __msan_scoped_disable_interceptor_checks();
#endif
  }

  ~ScopedMsanInterceptorChecks() {
#if TD_SSL_STREAM_RUNTIME_TEST_MSAN_ACTIVE
    __msan_scoped_enable_interceptor_checks();
#endif
  }
};

struct FreeX509 {
  void operator()(X509 *x509) const {
    if (x509 != nullptr) {
      X509_free(x509);
    }
  }
};

struct FreeEvpPkey {
  void operator()(EVP_PKEY *pkey) const {
    if (pkey != nullptr) {
      EVP_PKEY_free(pkey);
    }
  }
};

struct FreeSslCtx {
  void operator()(SSL_CTX *ssl_ctx) const {
    if (ssl_ctx != nullptr) {
      SSL_CTX_free(ssl_ctx);
    }
  }
};

struct FreeSslHandle {
  void operator()(SSL *ssl) const {
    if (ssl != nullptr) {
      SSL_free(ssl);
    }
  }
};

using UniqueX509 = std::unique_ptr<X509, FreeX509>;
using UniqueEvpPkey = std::unique_ptr<EVP_PKEY, FreeEvpPkey>;
using UniqueSslCtx = std::unique_ptr<SSL_CTX, FreeSslCtx>;
using UniqueSslHandle = std::unique_ptr<SSL, FreeSslHandle>;

td::string repo_path(td::Slice relative_path) {
  auto path = td::string(TELEMT_TEST_REPO_ROOT);
  if (path.empty()) {
    return relative_path.str();
  }
  path += '/';
  path += relative_path.str();
  return path;
}

std::pair<UniqueEvpPkey, UniqueX509> make_test_cert(const char *) {
  ScopedMsanInterceptorChecks scoped_msan_interceptor_checks;
  auto cert_pem_result = td::read_file_str(repo_path("test/tdnet/fixtures/ssl_stream_runtime_cert.pem"));
  auto key_pem_result = td::read_file_str(repo_path("test/tdnet/fixtures/ssl_stream_runtime_key.pem"));
  if (cert_pem_result.is_error() || key_pem_result.is_error()) {
    return {};
  }
  auto cert_pem = cert_pem_result.move_as_ok();
  auto key_pem = key_pem_result.move_as_ok();
  BIO *cert_bio = BIO_new_mem_buf(cert_pem.data(), static_cast<int>(cert_pem.size()));
  BIO *key_bio = BIO_new_mem_buf(key_pem.data(), static_cast<int>(key_pem.size()));
  if (cert_bio == nullptr || key_bio == nullptr) {
    if (cert_bio != nullptr) {
      BIO_free(cert_bio);
    }
    if (key_bio != nullptr) {
      BIO_free(key_bio);
    }
    return {};
  }
  UniqueX509 cert(PEM_read_bio_X509(cert_bio, nullptr, nullptr, nullptr));
  UniqueEvpPkey pkey(PEM_read_bio_PrivateKey(key_bio, nullptr, nullptr, nullptr));
  BIO_free(cert_bio);
  BIO_free(key_bio);
  if (!cert || !pkey) {
    return {};
  }
  return {std::move(pkey), std::move(cert)};
}

UniqueSslCtx make_server_ssl_ctx(EVP_PKEY *key, X509 *cert) {
  ScopedMsanInterceptorChecks scoped_msan_interceptor_checks;
  UniqueSslCtx ssl_ctx(SSL_CTX_new(TLS_server_method()));
  if (!ssl_ctx) {
    return {};
  }
  if (SSL_CTX_set_min_proto_version(ssl_ctx.get(), TLS1_2_VERSION) != 1) {
    return {};
  }
  if (SSL_CTX_use_certificate(ssl_ctx.get(), cert) != 1) {
    return {};
  }
  if (SSL_CTX_use_PrivateKey(ssl_ctx.get(), key) != 1) {
    return {};
  }
  if (SSL_CTX_check_private_key(ssl_ctx.get()) != 1) {
    return {};
  }
  return ssl_ctx;
}

UniqueSslHandle make_server_ssl(SSL_CTX *ssl_ctx) {
  UniqueSslHandle ssl(SSL_new(ssl_ctx));
  if (!ssl) {
    return {};
  }
  BIO *rbio = BIO_new(BIO_s_mem());
  BIO *wbio = BIO_new(BIO_s_mem());
  if (rbio == nullptr || wbio == nullptr) {
    if (rbio != nullptr) {
      BIO_free(rbio);
    }
    if (wbio != nullptr) {
      BIO_free(wbio);
    }
    return {};
  }
  SSL_set_bio(ssl.get(), rbio, wbio);
  SSL_set_accept_state(ssl.get());
  return ssl;
}

std::string peek_openssl_error() {
  std::array<char, 256> buffer{};
  auto error = ERR_peek_error();
  if (error == 0) {
    return "none";
  }
  ERR_error_string_n(error, buffer.data(), buffer.size());
  return std::string(buffer.data());
}

bool is_retry_error(int ssl_error) {
  return ssl_error == SSL_ERROR_WANT_READ || ssl_error == SSL_ERROR_WANT_WRITE;
}

td::Slice fatal_handshake_failure_alert() {
  static const unsigned char kAlert[] = {0x15, 0x03, 0x03, 0x00, 0x02, 0x02, 0x28};
  return td::Slice(reinterpret_cast<const char *>(kAlert), sizeof(kAlert));
}

class ClientRuntimeHarness final {
 public:
  explicit ClientRuntimeHarness(td::SslStream stream)
      : stream_(std::move(stream))
      , outbound_plain_reader_(outbound_plain_writer_.extract_reader())
      , outbound_plain_source_(&outbound_plain_reader_)
      , outbound_wire_reader_(outbound_wire_writer_.extract_reader())
      , outbound_wire_sink_(&outbound_wire_writer_)
      , inbound_wire_reader_(inbound_wire_writer_.extract_reader())
      , inbound_wire_source_(&inbound_wire_reader_) {
    outbound_plain_source_ >> stream_.write_byte_flow() >> outbound_wire_sink_;
    inbound_wire_source_ >> stream_.read_byte_flow() >> inbound_plain_sink_;
  }

  void queue_outbound_plaintext(td::Slice plaintext) {
    outbound_plain_writer_.append(plaintext);
  }

  void pump_outbound() {
    outbound_plain_source_.wakeup();
  }

  size_t pending_outbound_plaintext() {
    outbound_plain_reader_.sync_with_writer();
    return outbound_plain_reader_.size();
  }

  td::ChainBufferReader &outbound_wire_reader() {
    return outbound_wire_reader_;
  }

  bool outbound_wire_is_ready() {
    return outbound_wire_sink_.is_ready();
  }

  const td::Status &outbound_wire_status() {
    return outbound_wire_sink_.status();
  }

  void close_outbound_plaintext() {
    outbound_plain_source_.close_input(td::Status::OK());
  }

  void accept_inbound_ciphertext(td::Slice ciphertext) {
    inbound_wire_writer_.append(ciphertext);
    inbound_wire_source_.wakeup();
  }

  bool inbound_plaintext_is_ready() {
    return inbound_plain_sink_.is_ready();
  }

  td::Result<std::string> take_finished_inbound_plaintext() {
    if (!inbound_plain_sink_.is_ready()) {
      return td::Status::Error("Client plaintext sink is not ready");
    }
    if (inbound_plain_sink_.status().is_error()) {
      return td::Status::Error(PSLICE() << "Client plaintext sink finished with error: "
                                        << inbound_plain_sink_.status());
    }
    return inbound_plain_sink_.result()->move_as_buffer_slice().as_slice().str();
  }

  td::Result<std::string> finish_inbound_plaintext() {
    inbound_wire_source_.close_input(td::Status::OK());
    if (!inbound_plain_sink_.is_ready()) {
      return td::Status::Error("Client plaintext sink did not finish after inbound close");
    }
    return take_finished_inbound_plaintext();
  }

 private:
  td::SslStream stream_;
  td::ChainBufferWriter outbound_plain_writer_;
  td::ChainBufferReader outbound_plain_reader_;
  td::ByteFlowSource outbound_plain_source_;
  td::ChainBufferWriter outbound_wire_writer_;
  td::ChainBufferReader outbound_wire_reader_;
  td::ByteFlowMoveSink outbound_wire_sink_;
  td::ChainBufferWriter inbound_wire_writer_;
  td::ChainBufferReader inbound_wire_reader_;
  td::ByteFlowSource inbound_wire_source_;
  td::ByteFlowSink inbound_plain_sink_;
};

td::Result<bool> transfer_reader_to_bio(td::ChainBufferReader &reader, BIO *target_bio, size_t max_fragment_size) {
  reader.sync_with_writer();
  size_t transferred = 0;
  while (!reader.empty()) {
    auto chunk = reader.prepare_read();
    chunk.truncate(max_fragment_size);
    auto written = BIO_write(target_bio, chunk.data(), static_cast<int>(chunk.size()));
    if (written <= 0) {
      return td::Status::Error("BIO_write failed while shuttling client ciphertext");
    }
    reader.confirm_read(static_cast<size_t>(written));
    transferred += static_cast<size_t>(written);
  }
  return transferred != 0;
}

td::Result<bool> transfer_bio_to_client(BIO *source_bio, ClientRuntimeHarness &client, size_t max_fragment_size) {
  std::array<char, 512> buffer{};
  size_t transferred = 0;
  for (;;) {
    auto pending = BIO_ctrl_pending(source_bio);
    if (pending == 0) {
      return transferred != 0;
    }
    auto to_read = td::min(buffer.size(), td::min(max_fragment_size, pending));
    auto read = BIO_read(source_bio, buffer.data(), static_cast<int>(to_read));
    if (read <= 0) {
      return td::Status::Error("BIO_read failed while shuttling server ciphertext");
    }
    client.accept_inbound_ciphertext(td::Slice(buffer.data(), static_cast<size_t>(read)));
    transferred += static_cast<size_t>(read);
  }
}

td::Result<bool> server_step_handshake(SSL *server_ssl) {
  ScopedMsanInterceptorChecks scoped_msan_interceptor_checks;
  auto rc = SSL_do_handshake(server_ssl);
  if (rc == 1) {
    return true;
  }
  auto ssl_error = SSL_get_error(server_ssl, rc);
  if (is_retry_error(ssl_error)) {
    return false;
  }
  return td::Status::Error(PSLICE() << "SSL_do_handshake failed with SSL error " << ssl_error << " and OpenSSL error "
                                    << peek_openssl_error());
}

td::Result<bool> server_step_read(SSL *server_ssl, std::string &received_plaintext) {
  std::array<char, 256> buffer{};
  ScopedMsanInterceptorChecks scoped_msan_interceptor_checks;
  auto rc = SSL_read(server_ssl, buffer.data(), static_cast<int>(buffer.size()));
  if (rc > 0) {
#if TD_SSL_STREAM_RUNTIME_TEST_MSAN_ACTIVE
    __msan_unpoison(buffer.data(), static_cast<size_t>(rc));
#endif
    received_plaintext.append(buffer.data(), static_cast<size_t>(rc));
    return true;
  }
  auto ssl_error = SSL_get_error(server_ssl, rc);
  if (is_retry_error(ssl_error) || ssl_error == SSL_ERROR_ZERO_RETURN) {
    return false;
  }
  return td::Status::Error(PSLICE() << "SSL_read failed with SSL error " << ssl_error << " and OpenSSL error "
                                    << peek_openssl_error());
}

td::Result<bool> server_step_write(SSL *server_ssl, td::Slice plaintext, size_t &written_offset) {
  auto remaining = plaintext.substr(written_offset);
  ScopedMsanInterceptorChecks scoped_msan_interceptor_checks;
  auto rc = SSL_write(server_ssl, remaining.data(), static_cast<int>(remaining.size()));
  if (rc > 0) {
    written_offset += static_cast<size_t>(rc);
    return true;
  }
  auto ssl_error = SSL_get_error(server_ssl, rc);
  if (is_retry_error(ssl_error)) {
    return false;
  }
  return td::Status::Error(PSLICE() << "SSL_write failed with SSL error " << ssl_error << " and OpenSSL error "
                                    << peek_openssl_error());
}

td::Result<bool> server_step_expect_peer_close(SSL *server_ssl) {
  std::array<char, 1> buffer{};
  ScopedMsanInterceptorChecks scoped_msan_interceptor_checks;
  auto rc = SSL_read(server_ssl, buffer.data(), static_cast<int>(buffer.size()));
  if (rc > 0) {
    return td::Status::Error("Server received unexpected application data while waiting for peer close_notify");
  }
  auto ssl_error = SSL_get_error(server_ssl, rc);
  if (ssl_error == SSL_ERROR_ZERO_RETURN) {
    if ((SSL_get_shutdown(server_ssl) & SSL_RECEIVED_SHUTDOWN) == 0) {
      return td::Status::Error("Server observed SSL_ERROR_ZERO_RETURN without the received shutdown bit");
    }
    return true;
  }
  if (is_retry_error(ssl_error)) {
    return false;
  }
  return td::Status::Error(PSLICE() << "SSL_read while waiting for peer close_notify failed with SSL error "
                                    << ssl_error << " and OpenSSL error " << peek_openssl_error());
}

td::Result<bool> server_step_send_close_notify(SSL *server_ssl) {
  ScopedMsanInterceptorChecks scoped_msan_interceptor_checks;
  auto rc = SSL_shutdown(server_ssl);
  if (rc == 1) {
    return true;
  }
  if (rc == 0) {
    return true;
  }
  auto ssl_error = SSL_get_error(server_ssl, rc);
  if (is_retry_error(ssl_error)) {
    return false;
  }
  return td::Status::Error(PSLICE() << "SSL_shutdown failed with SSL error " << ssl_error << " and OpenSSL error "
                                    << peek_openssl_error());
}

}  // namespace ssl_stream_runtime_test

TEST(SslStreamRuntimeIntegration, FragmentedTransportRoundTripsHandshakeAndApplicationData) {
  td::SslCtx::init_openssl();

  auto [server_key, server_cert] = ssl_stream_runtime_test::make_test_cert("example.com");
  ASSERT_TRUE(server_key != nullptr);
  ASSERT_TRUE(server_cert != nullptr);

  auto server_ctx = ssl_stream_runtime_test::make_server_ssl_ctx(server_key.get(), server_cert.get());
  ASSERT_TRUE(server_ctx != nullptr);

  auto server_ssl = ssl_stream_runtime_test::make_server_ssl(server_ctx.get());
  ASSERT_TRUE(server_ssl != nullptr);

  auto client_ctx = td::SslCtx::create(td::CSlice(), td::SslCtx::VerifyPeer::Off);
  ASSERT_TRUE(client_ctx.is_ok());
  auto client_stream = td::SslStream::create(td::CSlice("example.com"), client_ctx.move_as_ok());
  ASSERT_TRUE(client_stream.is_ok());

  ssl_stream_runtime_test::ClientRuntimeHarness client(client_stream.move_as_ok());

  const std::string request = "client payload across fragmented sslstream BIO";
  const std::string reply = "server reply across fragmented sslstream BIO";
  client.queue_outbound_plaintext(td::Slice(request));

  std::string server_received;
  size_t server_reply_offset = 0;

  constexpr size_t kClientToServerFragmentSize = 7;
  constexpr size_t kServerToClientFragmentSize = 5;
  constexpr int kMaxPumpIterations = 4096;

  int iteration = 0;
  while (iteration < kMaxPumpIterations &&
         (server_received.size() < request.size() || server_reply_offset < reply.size())) {
    bool progress = false;

    client.pump_outbound();
    auto client_to_server = ssl_stream_runtime_test::transfer_reader_to_bio(
        client.outbound_wire_reader(), SSL_get_rbio(server_ssl.get()), kClientToServerFragmentSize);
    ASSERT_TRUE(client_to_server.is_ok());
    progress |= client_to_server.move_as_ok();

    if (!SSL_is_init_finished(server_ssl.get())) {
      auto handshake_step = ssl_stream_runtime_test::server_step_handshake(server_ssl.get());
      ASSERT_TRUE(handshake_step.is_ok());
      progress |= handshake_step.move_as_ok();
    } else if (server_received.size() < request.size()) {
      auto read_step = ssl_stream_runtime_test::server_step_read(server_ssl.get(), server_received);
      ASSERT_TRUE(read_step.is_ok());
      progress |= read_step.move_as_ok();
    } else if (server_reply_offset < reply.size()) {
      auto write_step =
          ssl_stream_runtime_test::server_step_write(server_ssl.get(), td::Slice(reply), server_reply_offset);
      ASSERT_TRUE(write_step.is_ok());
      progress |= write_step.move_as_ok();
    }

    auto server_to_client = ssl_stream_runtime_test::transfer_bio_to_client(SSL_get_wbio(server_ssl.get()), client,
                                                                            kServerToClientFragmentSize);
    ASSERT_TRUE(server_to_client.is_ok());
    progress |= server_to_client.move_as_ok();

    if (!progress && (server_received.size() < request.size() || server_reply_offset < reply.size())) {
      client.outbound_wire_reader().sync_with_writer();
      LOG(FATAL) << "SslStream runtime pump stalled before completing fragmented request/reply exchange"
                 << td::tag("iteration", iteration)
                 << td::tag("server_handshake_done", SSL_is_init_finished(server_ssl.get()))
                 << td::tag("server_received_size", server_received.size())
                 << td::tag("server_reply_offset", server_reply_offset)
                 << td::tag("client_plain_pending", client.pending_outbound_plaintext())
                 << td::tag("client_wire_pending", client.outbound_wire_reader().size())
                 << td::tag("server_rbio_pending", BIO_ctrl_pending(SSL_get_rbio(server_ssl.get())))
                 << td::tag("server_wbio_pending", BIO_ctrl_pending(SSL_get_wbio(server_ssl.get())));
    }

    ++iteration;
  }

  ASSERT_EQ(request, server_received);
  ASSERT_EQ(reply.size(), server_reply_offset);
  ASSERT_TRUE(SSL_is_init_finished(server_ssl.get()));

  auto client_received = client.finish_inbound_plaintext();
  ASSERT_TRUE(client_received.is_ok());
  ASSERT_EQ(reply, client_received.move_as_ok());
}

TEST(SslStreamRuntimeIntegration, WriteInputCloseEmitsCloseNotifyToPeer) {
  td::SslCtx::init_openssl();

  auto [server_key, server_cert] = ssl_stream_runtime_test::make_test_cert("example.com");
  ASSERT_TRUE(server_key != nullptr);
  ASSERT_TRUE(server_cert != nullptr);

  auto server_ctx = ssl_stream_runtime_test::make_server_ssl_ctx(server_key.get(), server_cert.get());
  ASSERT_TRUE(server_ctx != nullptr);

  auto server_ssl = ssl_stream_runtime_test::make_server_ssl(server_ctx.get());
  ASSERT_TRUE(server_ssl != nullptr);

  auto client_ctx = td::SslCtx::create(td::CSlice(), td::SslCtx::VerifyPeer::Off);
  ASSERT_TRUE(client_ctx.is_ok());
  auto client_stream = td::SslStream::create(td::CSlice("example.com"), client_ctx.move_as_ok());
  ASSERT_TRUE(client_stream.is_ok());

  ssl_stream_runtime_test::ClientRuntimeHarness client(client_stream.move_as_ok());

  constexpr size_t kClientToServerFragmentSize = 7;
  constexpr size_t kServerToClientFragmentSize = 5;
  constexpr int kMaxPumpIterations = 1024;

  int handshake_iteration = 0;
  while (handshake_iteration < kMaxPumpIterations && !SSL_is_init_finished(server_ssl.get())) {
    bool progress = false;

    client.pump_outbound();
    auto client_to_server = ssl_stream_runtime_test::transfer_reader_to_bio(
        client.outbound_wire_reader(), SSL_get_rbio(server_ssl.get()), kClientToServerFragmentSize);
    ASSERT_TRUE(client_to_server.is_ok());
    progress |= client_to_server.move_as_ok();

    auto handshake_step = ssl_stream_runtime_test::server_step_handshake(server_ssl.get());
    ASSERT_TRUE(handshake_step.is_ok());
    progress |= handshake_step.move_as_ok();

    auto server_to_client = ssl_stream_runtime_test::transfer_bio_to_client(SSL_get_wbio(server_ssl.get()), client,
                                                                            kServerToClientFragmentSize);
    ASSERT_TRUE(server_to_client.is_ok());
    progress |= server_to_client.move_as_ok();

    if (!progress && !SSL_is_init_finished(server_ssl.get())) {
      client.outbound_wire_reader().sync_with_writer();
      LOG(FATAL) << "SslStream runtime pump stalled before completing TLS handshake for close_notify test"
                 << td::tag("iteration", handshake_iteration)
                 << td::tag("server_handshake_done", SSL_is_init_finished(server_ssl.get()))
                 << td::tag("client_wire_pending", client.outbound_wire_reader().size())
                 << td::tag("server_rbio_pending", BIO_ctrl_pending(SSL_get_rbio(server_ssl.get())))
                 << td::tag("server_wbio_pending", BIO_ctrl_pending(SSL_get_wbio(server_ssl.get())));
    }

    ++handshake_iteration;
  }

  ASSERT_TRUE(SSL_is_init_finished(server_ssl.get()));

  client.close_outbound_plaintext();

  bool server_saw_close_notify = false;
  int shutdown_iteration = 0;
  while (shutdown_iteration < kMaxPumpIterations && !server_saw_close_notify) {
    bool progress = false;

    auto client_to_server = ssl_stream_runtime_test::transfer_reader_to_bio(
        client.outbound_wire_reader(), SSL_get_rbio(server_ssl.get()), kClientToServerFragmentSize);
    ASSERT_TRUE(client_to_server.is_ok());
    progress |= client_to_server.move_as_ok();

    auto close_step = ssl_stream_runtime_test::server_step_expect_peer_close(server_ssl.get());
    ASSERT_TRUE(close_step.is_ok());
    server_saw_close_notify = close_step.move_as_ok();
    progress |= server_saw_close_notify;

    if (!progress && !server_saw_close_notify) {
      client.outbound_wire_reader().sync_with_writer();
      LOG(FATAL) << "SslStream runtime pump stalled before server observed client close_notify"
                 << td::tag("iteration", shutdown_iteration)
                 << td::tag("client_wire_pending", client.outbound_wire_reader().size())
                 << td::tag("server_rbio_pending", BIO_ctrl_pending(SSL_get_rbio(server_ssl.get())))
                 << td::tag("server_shutdown_state", SSL_get_shutdown(server_ssl.get()))
                 << td::tag("server_wbio_pending", BIO_ctrl_pending(SSL_get_wbio(server_ssl.get())));
    }

    ++shutdown_iteration;
  }

  ASSERT_TRUE(server_saw_close_notify);
  ASSERT_TRUE((SSL_get_shutdown(server_ssl.get()) & SSL_RECEIVED_SHUTDOWN) != 0);
}

TEST(SslStreamRuntimeIntegration, PeerCloseNotifyFinishesClientReadPathWithoutTransportEof) {
  td::SslCtx::init_openssl();

  auto [server_key, server_cert] = ssl_stream_runtime_test::make_test_cert("example.com");
  ASSERT_TRUE(server_key != nullptr);
  ASSERT_TRUE(server_cert != nullptr);

  auto server_ctx = ssl_stream_runtime_test::make_server_ssl_ctx(server_key.get(), server_cert.get());
  ASSERT_TRUE(server_ctx != nullptr);

  auto server_ssl = ssl_stream_runtime_test::make_server_ssl(server_ctx.get());
  ASSERT_TRUE(server_ssl != nullptr);

  auto client_ctx = td::SslCtx::create(td::CSlice(), td::SslCtx::VerifyPeer::Off);
  ASSERT_TRUE(client_ctx.is_ok());
  auto client_stream = td::SslStream::create(td::CSlice("example.com"), client_ctx.move_as_ok());
  ASSERT_TRUE(client_stream.is_ok());

  ssl_stream_runtime_test::ClientRuntimeHarness client(client_stream.move_as_ok());

  const std::string request = "request before server close_notify";
  const std::string reply = "reply before orderly server shutdown";
  client.queue_outbound_plaintext(td::Slice(request));

  std::string server_received;
  size_t server_reply_offset = 0;
  bool server_sent_close_notify = false;

  constexpr size_t kClientToServerFragmentSize = 7;
  constexpr size_t kServerToClientFragmentSize = 5;
  constexpr int kMaxPumpIterations = 4096;

  int iteration = 0;
  while (iteration < kMaxPumpIterations && !client.inbound_plaintext_is_ready()) {
    bool progress = false;

    client.pump_outbound();
    auto client_to_server = ssl_stream_runtime_test::transfer_reader_to_bio(
        client.outbound_wire_reader(), SSL_get_rbio(server_ssl.get()), kClientToServerFragmentSize);
    ASSERT_TRUE(client_to_server.is_ok());
    progress |= client_to_server.move_as_ok();

    if (!SSL_is_init_finished(server_ssl.get())) {
      auto handshake_step = ssl_stream_runtime_test::server_step_handshake(server_ssl.get());
      ASSERT_TRUE(handshake_step.is_ok());
      progress |= handshake_step.move_as_ok();
    } else if (server_received.size() < request.size()) {
      auto read_step = ssl_stream_runtime_test::server_step_read(server_ssl.get(), server_received);
      ASSERT_TRUE(read_step.is_ok());
      progress |= read_step.move_as_ok();
    } else if (server_reply_offset < reply.size()) {
      auto write_step =
          ssl_stream_runtime_test::server_step_write(server_ssl.get(), td::Slice(reply), server_reply_offset);
      ASSERT_TRUE(write_step.is_ok());
      progress |= write_step.move_as_ok();
    } else if (!server_sent_close_notify) {
      auto shutdown_step = ssl_stream_runtime_test::server_step_send_close_notify(server_ssl.get());
      ASSERT_TRUE(shutdown_step.is_ok());
      server_sent_close_notify = shutdown_step.move_as_ok();
      progress |= server_sent_close_notify;
    }

    auto server_to_client = ssl_stream_runtime_test::transfer_bio_to_client(SSL_get_wbio(server_ssl.get()), client,
                                                                            kServerToClientFragmentSize);
    ASSERT_TRUE(server_to_client.is_ok());
    progress |= server_to_client.move_as_ok();

    if (!progress && !client.inbound_plaintext_is_ready()) {
      client.outbound_wire_reader().sync_with_writer();
      LOG(FATAL) << "SslStream runtime pump stalled before client observed peer close_notify"
                 << td::tag("iteration", iteration)
                 << td::tag("server_handshake_done", SSL_is_init_finished(server_ssl.get()))
                 << td::tag("server_received_size", server_received.size())
                 << td::tag("server_reply_offset", server_reply_offset)
                 << td::tag("server_sent_close_notify", server_sent_close_notify)
                 << td::tag("client_plain_pending", client.pending_outbound_plaintext())
                 << td::tag("client_wire_pending", client.outbound_wire_reader().size())
                 << td::tag("server_rbio_pending", BIO_ctrl_pending(SSL_get_rbio(server_ssl.get())))
                 << td::tag("server_shutdown_state", SSL_get_shutdown(server_ssl.get()))
                 << td::tag("server_wbio_pending", BIO_ctrl_pending(SSL_get_wbio(server_ssl.get())));
    }

    ++iteration;
  }

  ASSERT_EQ(request, server_received);
  ASSERT_EQ(reply.size(), server_reply_offset);
  ASSERT_TRUE(server_sent_close_notify);
  ASSERT_TRUE(client.inbound_plaintext_is_ready());

  auto client_received = client.take_finished_inbound_plaintext();
  ASSERT_TRUE(client_received.is_ok());
  ASSERT_EQ(reply, client_received.move_as_ok());
}

TEST(SslStreamRuntimeIntegration, BidirectionalCloseNotifyCompletesAfterClientHalfClose) {
  td::SslCtx::init_openssl();

  auto [server_key, server_cert] = ssl_stream_runtime_test::make_test_cert("example.com");
  ASSERT_TRUE(server_key != nullptr);
  ASSERT_TRUE(server_cert != nullptr);

  auto server_ctx = ssl_stream_runtime_test::make_server_ssl_ctx(server_key.get(), server_cert.get());
  ASSERT_TRUE(server_ctx != nullptr);

  auto server_ssl = ssl_stream_runtime_test::make_server_ssl(server_ctx.get());
  ASSERT_TRUE(server_ssl != nullptr);

  auto client_ctx = td::SslCtx::create(td::CSlice(), td::SslCtx::VerifyPeer::Off);
  ASSERT_TRUE(client_ctx.is_ok());
  auto client_stream = td::SslStream::create(td::CSlice("example.com"), client_ctx.move_as_ok());
  ASSERT_TRUE(client_stream.is_ok());

  ssl_stream_runtime_test::ClientRuntimeHarness client(client_stream.move_as_ok());

  constexpr size_t kClientToServerFragmentSize = 7;
  constexpr size_t kServerToClientFragmentSize = 5;
  constexpr int kMaxPumpIterations = 2048;

  int handshake_iteration = 0;
  while (handshake_iteration < kMaxPumpIterations && !SSL_is_init_finished(server_ssl.get())) {
    bool progress = false;

    client.pump_outbound();
    auto client_to_server = ssl_stream_runtime_test::transfer_reader_to_bio(
        client.outbound_wire_reader(), SSL_get_rbio(server_ssl.get()), kClientToServerFragmentSize);
    ASSERT_TRUE(client_to_server.is_ok());
    progress |= client_to_server.move_as_ok();

    auto handshake_step = ssl_stream_runtime_test::server_step_handshake(server_ssl.get());
    ASSERT_TRUE(handshake_step.is_ok());
    progress |= handshake_step.move_as_ok();

    auto server_to_client = ssl_stream_runtime_test::transfer_bio_to_client(SSL_get_wbio(server_ssl.get()), client,
                                                                            kServerToClientFragmentSize);
    ASSERT_TRUE(server_to_client.is_ok());
    progress |= server_to_client.move_as_ok();

    if (!progress && !SSL_is_init_finished(server_ssl.get())) {
      client.outbound_wire_reader().sync_with_writer();
      LOG(FATAL) << "SslStream runtime pump stalled before completing TLS handshake for bidirectional close_notify test"
                 << td::tag("iteration", handshake_iteration)
                 << td::tag("client_wire_pending", client.outbound_wire_reader().size())
                 << td::tag("server_rbio_pending", BIO_ctrl_pending(SSL_get_rbio(server_ssl.get())))
                 << td::tag("server_wbio_pending", BIO_ctrl_pending(SSL_get_wbio(server_ssl.get())));
    }

    ++handshake_iteration;
  }

  ASSERT_TRUE(SSL_is_init_finished(server_ssl.get()));

  client.close_outbound_plaintext();

  bool server_saw_client_close = false;
  bool server_sent_close_notify = false;
  int iteration = 0;
  while (iteration < kMaxPumpIterations && !client.inbound_plaintext_is_ready()) {
    bool progress = false;

    auto client_to_server = ssl_stream_runtime_test::transfer_reader_to_bio(
        client.outbound_wire_reader(), SSL_get_rbio(server_ssl.get()), kClientToServerFragmentSize);
    ASSERT_TRUE(client_to_server.is_ok());
    progress |= client_to_server.move_as_ok();

    if (!server_saw_client_close) {
      auto close_step = ssl_stream_runtime_test::server_step_expect_peer_close(server_ssl.get());
      ASSERT_TRUE(close_step.is_ok());
      server_saw_client_close = close_step.move_as_ok();
      progress |= server_saw_client_close;
    } else if (!server_sent_close_notify) {
      auto shutdown_step = ssl_stream_runtime_test::server_step_send_close_notify(server_ssl.get());
      ASSERT_TRUE(shutdown_step.is_ok());
      server_sent_close_notify = shutdown_step.move_as_ok();
      progress |= server_sent_close_notify;
    }

    auto server_to_client = ssl_stream_runtime_test::transfer_bio_to_client(SSL_get_wbio(server_ssl.get()), client,
                                                                            kServerToClientFragmentSize);
    ASSERT_TRUE(server_to_client.is_ok());
    progress |= server_to_client.move_as_ok();

    if (!progress && !client.inbound_plaintext_is_ready()) {
      client.outbound_wire_reader().sync_with_writer();
      LOG(FATAL) << "SslStream runtime pump stalled before bidirectional close_notify completed"
                 << td::tag("iteration", iteration) << td::tag("server_saw_client_close", server_saw_client_close)
                 << td::tag("server_sent_close_notify", server_sent_close_notify)
                 << td::tag("client_wire_pending", client.outbound_wire_reader().size())
                 << td::tag("server_rbio_pending", BIO_ctrl_pending(SSL_get_rbio(server_ssl.get())))
                 << td::tag("server_shutdown_state", SSL_get_shutdown(server_ssl.get()))
                 << td::tag("server_wbio_pending", BIO_ctrl_pending(SSL_get_wbio(server_ssl.get())));
    }

    ++iteration;
  }

  ASSERT_TRUE(server_saw_client_close);
  ASSERT_TRUE(server_sent_close_notify);
  ASSERT_TRUE(client.inbound_plaintext_is_ready());

  auto client_received = client.take_finished_inbound_plaintext();
  ASSERT_TRUE(client_received.is_ok());
  ASSERT_EQ("", client_received.move_as_ok());
}

TEST(SslStreamRuntimeIntegration, FatalAlertDuringHandshakeSurfacesTerminalError) {
  td::SslCtx::init_openssl();

  auto client_ctx = td::SslCtx::create(td::CSlice(), td::SslCtx::VerifyPeer::Off);
  ASSERT_TRUE(client_ctx.is_ok());
  auto client_stream = td::SslStream::create(td::CSlice("example.com"), client_ctx.move_as_ok());
  ASSERT_TRUE(client_stream.is_ok());

  ssl_stream_runtime_test::ClientRuntimeHarness client(client_stream.move_as_ok());
  client.queue_outbound_plaintext(td::Slice("trigger handshake"));
  client.pump_outbound();

  client.outbound_wire_reader().sync_with_writer();
  ASSERT_TRUE(client.outbound_wire_reader().size() > 0);

  client.accept_inbound_ciphertext(ssl_stream_runtime_test::fatal_handshake_failure_alert());

  constexpr int kMaxPumpIterations = 32;
  int iteration = 0;
  while (iteration < kMaxPumpIterations && !client.outbound_wire_is_ready() && !client.inbound_plaintext_is_ready()) {
    client.outbound_wire_reader().sync_with_writer();
    auto wire_pending_before = client.outbound_wire_reader().size();

    client.pump_outbound();

    client.outbound_wire_reader().sync_with_writer();
    if (!client.outbound_wire_is_ready() && !client.inbound_plaintext_is_ready() &&
        client.outbound_wire_reader().size() == wire_pending_before) {
      LOG(FATAL) << "SslStream runtime pump stalled after fatal handshake alert" << td::tag("iteration", iteration)
                 << td::tag("client_wire_pending", client.outbound_wire_reader().size())
                 << td::tag("outbound_sink_ready", client.outbound_wire_is_ready())
                 << td::tag("inbound_sink_ready", client.inbound_plaintext_is_ready());
    }

    ++iteration;
  }

  ASSERT_TRUE(client.outbound_wire_is_ready() || client.inbound_plaintext_is_ready());
  if (client.outbound_wire_is_ready()) {
    ASSERT_TRUE(client.outbound_wire_status().is_error());
  }
  if (client.inbound_plaintext_is_ready()) {
    auto inbound_result = client.take_finished_inbound_plaintext();
    ASSERT_TRUE(inbound_result.is_error());
  }
}

#endif  // !TD_EMSCRIPTEN