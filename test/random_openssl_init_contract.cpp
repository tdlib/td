// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/common.h"
#include "td/utils/tests.h"

#include "test/stealth/SourceContractFileReader.h"

#include <string_view>

namespace {

td::string normalize_for_contract(td::Slice source) {
  td::string normalized;
  normalized.reserve(source.size());
  for (auto c : source) {
    if (const auto byte = static_cast<unsigned char>(c); byte == ' ' || byte == '\t' || byte == '\r' || byte == '\n') {
      continue;
    }
    normalized.push_back(c);
  }
  return normalized;
}

td::string extract_region(std::string_view source, td::Slice begin_marker, td::Slice end_marker) {
  const auto begin = source.find(begin_marker.str());
  CHECK(begin != td::string::npos);
  const auto end = source.find(end_marker.str(), begin + begin_marker.size());
  CHECK(end != td::string::npos);
  CHECK(end > begin);
  return td::string(source.substr(begin, end - begin));
}

}  // namespace

TEST(RandomOpenSslInitContract, init_crypto_disables_ambient_openssl_config_autoload) {
  const auto source = td::mtproto::test::read_repo_text_file("tdutils/td/utils/crypto.cpp");
  const auto normalized = normalize_for_contract(source);

  ASSERT_NE(td::string::npos, normalized.find("OPENSSL_init_crypto(OPENSSL_INIT_NO_LOAD_CONFIG,nullptr)!=0"));
  ASSERT_EQ(td::string::npos, normalized.find("OPENSSL_init_crypto(0,nullptr)!=0"));
}

TEST(RandomOpenSslInitContract, init_openssl_threads_bootstraps_crypto_before_runtime_use) {
  const auto source = td::mtproto::test::read_repo_text_file("tdutils/td/utils/crypto.cpp");
  const auto region = normalize_for_contract(extract_region(source, "void init_openssl_threads() {",
                                                            "Status create_openssl_error(int code, Slice message) {"));

  ASSERT_NE(td::string::npos, region.find("voidinit_openssl_threads(){init_crypto();"));
}

TEST(RandomOpenSslInitContract, init_crypto_explicitly_pins_default_provider_and_drbg_before_rand_use) {
  const auto source = td::mtproto::test::read_repo_text_file("tdutils/td/utils/crypto.cpp");
  const auto normalized =
      normalize_for_contract(extract_region(source, "void init_crypto() {", "template <class FromT>"));

  ASSERT_NE(td::string::npos, normalized.find("OSSL_PROVIDER_load(nullptr,\"default\")"));
  ASSERT_NE(td::string::npos,
            normalized.find("RAND_set_DRBG_type(nullptr,\"CTR-DRBG\",nullptr,\"AES-256-CTR\",\"SHA256\")"));
}

TEST(RandomOpenSslInitContract, init_crypto_eagerly_warms_public_and_private_drbg_paths_after_setting_defaults) {
  const auto source = td::mtproto::test::read_repo_text_file("tdutils/td/utils/crypto.cpp");

  const auto init_region =
      normalize_for_contract(extract_region(source, "void init_crypto() {", "template <class FromT>"));
  ASSERT_NE(td::string::npos,
            init_region.find("RAND_set_DRBG_type(nullptr,\"CTR-DRBG\",nullptr,\"AES-256-CTR\",\"SHA256\")==1;"
                             "}if(result){result=warmup_openssl_random_generators();}"));

  const auto helper_region =
      normalize_for_contract(extract_region(source, "warmup_openssl_random_generators() {", "void init_crypto() {"));
  ASSERT_NE(td::string::npos, helper_region.find("unsignedcharpublic_warmup[1]={0};"));
  ASSERT_NE(td::string::npos, helper_region.find("unsignedcharprivate_warmup[1]={0};"));
  ASSERT_NE(td::string::npos, helper_region.find("__msan_scoped_disable_interceptor_checks();"));
  ASSERT_NE(td::string::npos, helper_region.find("__msan_scoped_enable_interceptor_checks();"));
  ASSERT_NE(td::string::npos, helper_region.find("RAND_bytes(public_warmup,sizeof(public_warmup))==1"));
  ASSERT_NE(td::string::npos, helper_region.find("RAND_priv_bytes(private_warmup,sizeof(private_warmup))==1"));
}

TEST(RandomOpenSslInitContract, evp_update_paths_initialize_output_length_before_openssl_calls) {
  const auto source = td::mtproto::test::read_repo_text_file("tdutils/td/utils/crypto.cpp");

  const auto encrypt_region =
      normalize_for_contract(extract_region(source, "void encrypt(const uint8 *src, uint8 *dst, int size) {",
                                            "void decrypt(const uint8 *src, uint8 *dst, int size) {"));
  ASSERT_NE(td::string::npos, encrypt_region.find("intlen=0;"));
  ASSERT_NE(td::string::npos, encrypt_region.find("intres=EVP_EncryptUpdate(ctx_,dst,&len,src,size);"));
  ASSERT_NE(td::string::npos, encrypt_region.find("__msan_unpoison(dst,len);"));

  const auto decrypt_region = normalize_for_contract(
      extract_region(source, "void decrypt(const uint8 *src, uint8 *dst, int size) {", "private:"));
  ASSERT_NE(td::string::npos, decrypt_region.find("intlen=0;"));
  ASSERT_NE(td::string::npos, decrypt_region.find("intres=EVP_DecryptUpdate(ctx_,dst,&len,src,size);"));
  ASSERT_NE(td::string::npos, decrypt_region.find("__msan_unpoison(dst,len);"));
}

TEST(RandomOpenSslInitContract, digest_paths_capture_finalized_length_and_unpoison_digest_output_for_msan) {
  const auto source = td::mtproto::test::read_repo_text_file("tdutils/td/utils/crypto.cpp");
  const auto normalized = normalize_for_contract(extract_region(
      source, "static void make_digest(Slice data, MutableSlice output, const EVP_MD_CTX *evp_md_ctx) {",
      "static void init_thread_local_evp_md_ctx(const EVP_MD_CTX *&evp_md_ctx, const char *algorithm) {"));

  ASSERT_NE(td::string::npos, normalized.find("unsignedintoutput_size=0;"));
  ASSERT_NE(td::string::npos, normalized.find("EVP_DigestFinal_ex(ctx,output.ubegin(),&output_size);"));
  ASSERT_NE(td::string::npos, normalized.find("CHECK(output_size<=output.size());"));
  ASSERT_NE(td::string::npos, normalized.find("__msan_unpoison(output.ubegin(),output_size);"));
}

TEST(RandomOpenSslInitContract, sha256_state_extract_captures_finalized_length_and_unpoisons_digest_output_for_msan) {
  const auto source = td::mtproto::test::read_repo_text_file("tdutils/td/utils/crypto.cpp");
  const auto normalized =
      normalize_for_contract(extract_region(source, "void Sha256State::extract(MutableSlice output, bool destroy) {",
                                            "void md5(Slice input, MutableSlice output) {"));

  ASSERT_NE(td::string::npos, normalized.find("unsignedintoutput_size=0;"));
  ASSERT_NE(td::string::npos, normalized.find("EVP_DigestFinal_ex(impl_->ctx_,output.ubegin(),&output_size);"));
  ASSERT_NE(td::string::npos, normalized.find("CHECK(output_size<=output.size());"));
  ASSERT_NE(td::string::npos, normalized.find("__msan_unpoison(output.ubegin(),output_size);"));
}

TEST(RandomOpenSslInitContract, provider_fetch_helpers_explicitly_initialize_crypto_before_openssl3_fetch_calls) {
  const auto source = td::mtproto::test::read_repo_text_file("tdutils/td/utils/crypto.cpp");

  const auto normalized = normalize_for_contract(source);
  ASSERT_NE(td::string::npos,
            normalized.find("staticvoidinit_thread_local_evp_cipher(constEVP_CIPHER*&evp_cipher,constchar*algorithm){"
                            "init_crypto();evp_cipher=EVP_CIPHER_fetch(nullptr,algorithm,nullptr);"));
  ASSERT_NE(td::string::npos,
            normalized.find("staticvoidinit_thread_local_evp_md_ctx(constEVP_MD_CTX*&evp_md_ctx,constchar*algorithm){"
                            "init_crypto();ScopedMsanInterceptorChecksscoped_msan_interceptor_checks;EVP_MD*evp_md=EVP_"
                            "MD_fetch(nullptr,algorithm,nullptr);"));
  ASSERT_NE(td::string::npos,
            normalized.find("staticvoidinit_thread_local_evp_mac_ctx(EVP_MAC_CTX*&evp_mac_ctx,constchar*digest){init_"
                            "crypto();ScopedMsanInterceptorChecksscoped_msan_interceptor_checks;EVP_MAC*hmac=EVP_MAC_"
                            "fetch(nullptr,\"HMAC\",nullptr);"));
}

TEST(RandomOpenSslInitContract, secure_random_paths_explicitly_initialize_crypto_before_rand_calls) {
  const auto source = td::mtproto::test::read_repo_text_file("tdutils/td/utils/Random.cpp");

  const auto secure_bytes_region = extract_region(
      source, "void Random::secure_bytes(unsigned char *ptr, size_t size) {", "int32 Random::secure_int32() {");
  const auto secure_bytes_init_pos = secure_bytes_region.find("init_crypto();");
  const auto secure_bytes_rand_pos = secure_bytes_region.find("call_rand_bytes_with_msan_interceptor_suppression(");
  ASSERT_NE(td::string::npos, secure_bytes_init_pos);
  ASSERT_NE(td::string::npos, secure_bytes_rand_pos);
  ASSERT_TRUE(secure_bytes_init_pos < secure_bytes_rand_pos);

  const auto add_seed_region = normalize_for_contract(extract_region(
      source, "void Random::add_seed(Slice bytes, double entropy) {", "void Random::secure_cleanup() {"));
  ASSERT_NE(td::string::npos, add_seed_region.find("voidRandom::add_seed(Slicebytes,doubleentropy){init_crypto();"));
}

TEST(RandomOpenSslInitContract, secure_random_paths_use_scoped_msan_interceptor_suppression_for_rand_bytes) {
  const auto source = td::mtproto::test::read_repo_text_file("tdutils/td/utils/Random.cpp");
  const auto normalized = normalize_for_contract(source);

  ASSERT_NE(td::string::npos,
            normalized.find("intcall_rand_bytes_with_msan_interceptor_suppression(unsignedchar*ptr,intsize){"));
  ASSERT_NE(td::string::npos, normalized.find("__msan_scoped_disable_interceptor_checks();"));
  ASSERT_NE(td::string::npos, normalized.find("__msan_scoped_enable_interceptor_checks();"));

  const auto secure_bytes_region = extract_region(
      source, "void Random::secure_bytes(unsigned char *ptr, size_t size) {", "int32 Random::secure_int32() {");
  ASSERT_NE(td::string::npos, secure_bytes_region.find("call_rand_bytes_with_msan_interceptor_suppression("));
}

TEST(RandomOpenSslInitContract, ssl_stream_read_scopes_msan_interceptor_checks_and_unpoisons_plaintext_output) {
  const auto source = td::mtproto::test::read_repo_text_file("tdnet/td/net/SslStream.cpp");
  const auto normalized = normalize_for_contract(source);

  ASSERT_NE(td::string::npos,
            normalized.find("classScopedMsanInterceptorChecksfinal{public:ScopedMsanInterceptorChecks(){"));
  ASSERT_NE(td::string::npos, normalized.find("__msan_scoped_disable_interceptor_checks();"));
  ASSERT_NE(td::string::npos, normalized.find("__msan_scoped_enable_interceptor_checks();"));

  const auto read_region = normalize_for_contract(extract_region(
      source, "Result<size_t> read(MutableSlice slice) {", "class SslReadByteFlow final : public ByteFlowBase {"));
  ASSERT_NE(td::string::npos, read_region.find("ScopedMsanInterceptorChecksscoped_msan_interceptor_checks;"));
  ASSERT_NE(td::string::npos,
            read_region.find("autosize=SSL_read(ssl_handle_.get(),slice.data(),static_cast<int>(slice.size()));"));
  ASSERT_NE(td::string::npos, read_region.find("__msan_unpoison(slice.data(),static_cast<size_t>(size));"));
}

TEST(RandomOpenSslInitContract, ssl_stream_custom_bio_write_unpoisons_ciphertext_before_buffering) {
  const auto source = td::mtproto::test::read_repo_text_file("tdnet/td/net/SslStream.cpp");
  const auto normalized = normalize_for_contract(
      extract_region(source, "int strm_write(BIO *b, const char *buf, int len) {", "}  // namespace"));

  ASSERT_NE(td::string::npos, normalized.find("if(len>0){"));
  ASSERT_NE(td::string::npos, normalized.find("__msan_unpoison(const_cast<char*>(buf),static_cast<size_t>(len));"));
  ASSERT_NE(td::string::npos, normalized.find("stream->flow_write(Slice(buf,len))"));
}
