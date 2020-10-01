//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/utils/benchmark.h"

#include "td/utils/common.h"
#include "td/utils/crypto.h"
#include "td/utils/logging.h"
#include "td/utils/port/thread.h"
#include "td/utils/Random.h"
#include "td/utils/Slice.h"
#include "td/utils/UInt.h"

#include <openssl/evp.h>
#include <openssl/sha.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

static constexpr int DATA_SIZE = 8 << 10;
static constexpr int SHORT_DATA_SIZE = 64;

class SHA1Bench : public td::Benchmark {
 public:
  alignas(64) unsigned char data[DATA_SIZE];

  std::string get_description() const override {
    return PSTRING() << "SHA1 OpenSSL [" << (DATA_SIZE >> 10) << "KB]";
  }

  void start_up() override {
    for (int i = 0; i < DATA_SIZE; i++) {
      data[i] = 123;
      data[i] = 0;
    }
  }

  void run(int n) override {
    for (int i = 0; i < n; i++) {
      unsigned char md[20];
      SHA1(data, DATA_SIZE, md);
    }
  }
};

class AesEcbBench : public td::Benchmark {
 public:
  alignas(64) unsigned char data[DATA_SIZE];
  td::UInt256 key;
  td::UInt256 iv;

  std::string get_description() const override {
    return PSTRING() << "AES ECB OpenSSL [" << (DATA_SIZE >> 10) << "KB]";
  }

  void start_up() override {
    for (int i = 0; i < DATA_SIZE; i++) {
      data[i] = 123;
    }
    td::Random::secure_bytes(key.raw, sizeof(key));
    td::Random::secure_bytes(iv.raw, sizeof(iv));
  }

  void run(int n) override {
    td::AesState state;
    state.init(td::as_slice(key), true);
    td::MutableSlice data_slice(data, DATA_SIZE);
    for (int i = 0; i <= n; i++) {
      size_t step = 16;
      for (size_t offset = 0; offset + step <= data_slice.size(); offset += step) {
        state.encrypt(data_slice.ubegin() + offset, data_slice.ubegin() + offset, static_cast<int>(step));
      }
    }
  }
};

class AesIgeEncryptBench : public td::Benchmark {
 public:
  alignas(64) unsigned char data[DATA_SIZE];
  td::UInt256 key;
  td::UInt256 iv;

  std::string get_description() const override {
    return PSTRING() << "AES IGE OpenSSL encrypt [" << (DATA_SIZE >> 10) << "KB]";
  }

  void start_up() override {
    for (int i = 0; i < DATA_SIZE; i++) {
      data[i] = 123;
    }
    td::Random::secure_bytes(key.raw, sizeof(key));
    td::Random::secure_bytes(iv.raw, sizeof(iv));
  }

  void run(int n) override {
    td::MutableSlice data_slice(data, DATA_SIZE);
    td::AesIgeState state;
    state.init(as_slice(key), as_slice(iv), true);
    for (int i = 0; i < n; i++) {
      state.encrypt(data_slice, data_slice);
    }
  }
};

class AesIgeDecryptBench : public td::Benchmark {
 public:
  alignas(64) unsigned char data[DATA_SIZE];
  td::UInt256 key;
  td::UInt256 iv;

  std::string get_description() const override {
    return PSTRING() << "AES IGE OpenSSL decrypt [" << (DATA_SIZE >> 10) << "KB]";
  }

  void start_up() override {
    for (int i = 0; i < DATA_SIZE; i++) {
      data[i] = 123;
    }
    td::Random::secure_bytes(key.raw, sizeof(key));
    td::Random::secure_bytes(iv.raw, sizeof(iv));
  }

  void run(int n) override {
    td::MutableSlice data_slice(data, DATA_SIZE);
    td::AesIgeState state;
    state.init(as_slice(key), as_slice(iv), false);
    for (int i = 0; i < n; i++) {
      state.decrypt(data_slice, data_slice);
    }
  }
};

class AesCtrBench : public td::Benchmark {
 public:
  alignas(64) unsigned char data[DATA_SIZE];
  td::UInt256 key;
  td::UInt128 iv;

  std::string get_description() const override {
    return PSTRING() << "AES CTR OpenSSL [" << (DATA_SIZE >> 10) << "KB]";
  }

  void start_up() override {
    for (int i = 0; i < DATA_SIZE; i++) {
      data[i] = 123;
    }
    td::Random::secure_bytes(key.raw, sizeof(key));
    td::Random::secure_bytes(iv.raw, sizeof(iv));
  }

  void run(int n) override {
    td::MutableSlice data_slice(data, DATA_SIZE);
    td::AesCtrState state;
    state.init(as_slice(key), as_slice(iv));
    for (int i = 0; i < n; i++) {
      state.encrypt(data_slice, data_slice);
    }
  }
};

class AesCtrOpenSSLBench : public td::Benchmark {
 public:
  alignas(64) unsigned char data[DATA_SIZE];
  td::UInt256 key;
  td::UInt128 iv;

  std::string get_description() const override {
    return PSTRING() << "AES CTR RAW OpenSSL [" << (DATA_SIZE >> 10) << "KB]";
  }

  void start_up() override {
    for (int i = 0; i < DATA_SIZE; i++) {
      data[i] = 123;
    }
    td::Random::secure_bytes(key.raw, sizeof(key));
    td::Random::secure_bytes(iv.raw, sizeof(iv));
  }

  void run(int n) override {
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    EVP_EncryptInit_ex(ctx, EVP_aes_256_ctr(), nullptr, key.raw, iv.raw);

    td::MutableSlice data_slice(data, DATA_SIZE);
    td::AesCtrState state;
    state.init(as_slice(key), as_slice(iv));
    for (int i = 0; i < n; i++) {
      int len = 0;
      EVP_EncryptUpdate(ctx, data_slice.ubegin(), &len, data_slice.ubegin(), DATA_SIZE);
      CHECK(len == DATA_SIZE);
    }

    EVP_CIPHER_CTX_free(ctx);
  }
};

class AesCbcDecryptBench : public td::Benchmark {
 public:
  alignas(64) unsigned char data[DATA_SIZE];
  td::UInt256 key;
  td::UInt128 iv;

  std::string get_description() const override {
    return PSTRING() << "AES CBC Decrypt OpenSSL [" << (DATA_SIZE >> 10) << "KB]";
  }

  void start_up() override {
    for (int i = 0; i < DATA_SIZE; i++) {
      data[i] = 123;
    }
    td::Random::secure_bytes(as_slice(key));
    td::Random::secure_bytes(as_slice(iv));
  }

  void run(int n) override {
    td::MutableSlice data_slice(data, DATA_SIZE);
    for (int i = 0; i < n; i++) {
      td::aes_cbc_decrypt(as_slice(key), as_slice(iv), data_slice, data_slice);
    }
  }
};

class AesCbcEncryptBench : public td::Benchmark {
 public:
  alignas(64) unsigned char data[DATA_SIZE];
  td::UInt256 key;
  td::UInt128 iv;

  std::string get_description() const override {
    return PSTRING() << "AES CBC Encrypt OpenSSL [" << (DATA_SIZE >> 10) << "KB]";
  }

  void start_up() override {
    for (int i = 0; i < DATA_SIZE; i++) {
      data[i] = 123;
    }
    td::Random::secure_bytes(as_slice(key));
    td::Random::secure_bytes(as_slice(iv));
  }

  void run(int n) override {
    td::MutableSlice data_slice(data, DATA_SIZE);
    for (int i = 0; i < n; i++) {
      td::aes_cbc_encrypt(as_slice(key), as_slice(iv), data_slice, data_slice);
    }
  }
};

template <bool use_state>
class AesIgeShortBench : public td::Benchmark {
 public:
  alignas(64) unsigned char data[SHORT_DATA_SIZE];
  td::UInt256 key;
  td::UInt256 iv;

  std::string get_description() const override {
    return PSTRING() << "AES IGE OpenSSL " << (use_state ? "EVP" : "C  ") << "[" << SHORT_DATA_SIZE << "B]";
  }

  void start_up() override {
    for (int i = 0; i < SHORT_DATA_SIZE; i++) {
      data[i] = 123;
    }
    td::Random::secure_bytes(as_slice(key));
    td::Random::secure_bytes(as_slice(iv));
  }

  void run(int n) override {
    td::MutableSlice data_slice(data, SHORT_DATA_SIZE);
    for (int i = 0; i < n; i++) {
      if (use_state) {
        td::AesIgeState ige;
        ige.init(as_slice(key), as_slice(iv), false);
        ige.decrypt(data_slice, data_slice);
      } else {
        td::aes_ige_decrypt(as_slice(key), as_slice(iv), data_slice, data_slice);
      }
    }
  }
};

BENCH(Rand, "std_rand") {
  int res = 0;
  for (int i = 0; i < n; i++) {
    res ^= std::rand();
  }
  td::do_not_optimize_away(res);
}

BENCH(CppRand, "mt19937_rand") {
  std::uint_fast32_t res = 0;
  std::mt19937 g(123);
  for (int i = 0; i < n; i++) {
    res ^= g();
  }
  td::do_not_optimize_away(res);
}

BENCH(TdRand32, "td_rand_fast32") {
  td::uint32 res = 0;
  for (int i = 0; i < n; i++) {
    res ^= td::Random::fast_uint32();
  }
  td::do_not_optimize_away(res);
}

BENCH(TdRandFast, "td_rand_fast") {
  int res = 0;
  for (int i = 0; i < n; i++) {
    res ^= td::Random::fast(0, RAND_MAX);
  }
  td::do_not_optimize_away(res);
}

#if !TD_THREAD_UNSUPPORTED
BENCH(SslRand, "ssl_rand_int32") {
  std::vector<td::thread> v;
  std::atomic<td::uint32> sum{0};
  for (int i = 0; i < 3; i++) {
    v.emplace_back([&sum, n] {
      td::int32 res = 0;
      for (int j = 0; j < n; j++) {
        res ^= td::Random::secure_int32();
      }
      sum += res;
    });
  }
  for (auto &x : v) {
    x.join();
  }
  v.clear();
  td::do_not_optimize_away(sum.load());
}
#endif

BENCH(SslRandBuf, "ssl_rand_bytes") {
  td::int32 res = 0;
  std::array<td::int32, 1000> buf;
  for (int i = 0; i < n; i += static_cast<int>(buf.size())) {
    td::Random::secure_bytes(reinterpret_cast<td::uint8 *>(buf.data()), sizeof(buf[0]) * buf.size());
    for (auto x : buf) {
      res ^= x;
    }
  }
  td::do_not_optimize_away(res);
}

BENCH(Pbkdf2, "pbkdf2") {
  std::string password = "cucumber";
  std::string salt = "abcdefghijklmnopqrstuvw";
  std::string key(32, ' ');
  td::pbkdf2_sha256(password, salt, n, key);
}

class Crc32Bench : public td::Benchmark {
 public:
  alignas(64) unsigned char data[DATA_SIZE];

  std::string get_description() const override {
    return PSTRING() << "Crc32 zlib [" << (DATA_SIZE >> 10) << "KB]";
  }

  void start_up() override {
    for (int i = 0; i < DATA_SIZE; i++) {
      data[i] = 123;
      data[i] = 0;
    }
  }

  void run(int n) override {
    td::uint64 res = 0;
    for (int i = 0; i < n; i++) {
      res += td::crc32(td::Slice(data, DATA_SIZE));
    }
    td::do_not_optimize_away(res);
  }
};

class Crc64Bench : public td::Benchmark {
 public:
  alignas(64) unsigned char data[DATA_SIZE];

  std::string get_description() const override {
    return PSTRING() << "Crc64 Anton [" << (DATA_SIZE >> 10) << "KB]";
  }

  void start_up() override {
    for (int i = 0; i < DATA_SIZE; i++) {
      data[i] = 123;
      data[i] = 0;
    }
  }

  void run(int n) override {
    td::uint64 res = 0;
    for (int i = 0; i < n; i++) {
      res += td::crc64(td::Slice(data, DATA_SIZE));
    }
    td::do_not_optimize_away(res);
  }
};

int main() {
  td::init_openssl_threads();
  td::bench(AesCtrBench());
  td::bench(AesCtrOpenSSLBench());

  td::bench(AesCbcDecryptBench());
  td::bench(AesCbcEncryptBench());
  td::bench(AesIgeShortBench<true>());
  td::bench(AesIgeShortBench<false>());
  td::bench(AesIgeEncryptBench());
  td::bench(AesIgeDecryptBench());
  td::bench(AesEcbBench());

  td::bench(Pbkdf2Bench());
  td::bench(RandBench());
  td::bench(CppRandBench());
  td::bench(TdRand32Bench());
  td::bench(TdRandFastBench());
#if !TD_THREAD_UNSUPPORTED
  td::bench(SslRandBench());
#endif
  td::bench(SslRandBufBench());
  td::bench(SHA1Bench());
  td::bench(Crc32Bench());
  td::bench(Crc64Bench());
}
