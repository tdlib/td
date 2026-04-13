// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/mtproto/ClientHelloExecutor.h"

#include "td/mtproto/ProxySecret.h"

#include "td/utils/BigNum.h"
#include "td/utils/common.h"
#include "td/utils/crypto.h"
#include "td/utils/Random.h"

#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/obj_mac.h>

#include <cstring>

namespace td {
namespace mtproto {

namespace {

using Op = ClientHelloOp;

void init_grease_values(MutableSlice res, stealth::IRng &rng) {
  rng.fill_secure_bytes(res);
  for (auto &c : res) {
    c = static_cast<char>((c & 0xF0) + 0x0A);
  }
  for (size_t i = 1; i < res.size(); i += 2) {
    if (res[i] == res[i - 1]) {
      res[i] ^= 0x10;
    }
  }
}

// Deterministic Fisher-Yates shuffle driven by `IRng`. Required because
// `td::Random::shuffle` uses a global Mersenne Twister seeded from
// /dev/urandom and is NOT reproducible from a test seed. Without this
// helper, any wire-equality test that depends on a permuted extension
// order would fail intermittently or always.
template <typename T>
void rng_shuffle(vector<T> &items, stealth::IRng &rng) {
  if (items.size() <= 1) {
    return;
  }
  for (size_t i = items.size() - 1; i > 0; i--) {
    auto j = static_cast<size_t>(rng.bounded(static_cast<uint32>(i + 1)));
    if (j != i) {
      using std::swap;
      swap(items[i], items[j]);
    }
  }
}

class ExecutionContext {
 public:
  ExecutionContext(const ExecutorConfig &config, Slice domain, stealth::IRng &rng)
      : grease_values_(config.grease_value_count, '\0'), domain_(domain.str()), config_(config), rng_(rng) {
    init_grease_values(MutableSlice(grease_values_), rng_);
  }

  char get_grease(size_t index) const {
    CHECK(index < grease_values_.size());
    return grease_values_[index];
  }

  size_t grease_size() const {
    return grease_values_.size();
  }

  Slice domain() const {
    return Slice(domain_).substr(0, ProxySecret::MAX_DOMAIN_LENGTH);
  }

  const ExecutorConfig &config() const {
    return config_;
  }

  stealth::IRng &rng() const {
    return rng_;
  }

 private:
  string grease_values_;
  string domain_;
  const ExecutorConfig &config_;
  stealth::IRng &rng_;
};

class LengthCalculator {
 public:
  void append(const Op &op, const ExecutionContext &context) {
    switch (op.type) {
      case Op::Type::Bytes:
        size_ += op.data.size();
        break;
      case Op::Type::RandomBytes:
      case Op::Type::ZeroBytes:
        size_ += op.length;
        break;
      case Op::Type::Domain:
        size_ += context.domain().size();
        break;
      case Op::Type::GreaseValue:
        size_ += 2;
        break;
      case Op::Type::X25519KeyShareEntry:
        size_ += 2 + 2 + 32;
        break;
      case Op::Type::Secp256r1KeyShareEntry:
        size_ += 2 + 2 + 65;
        break;
      case Op::Type::X25519MlKem768KeyShareEntry:
        // group(2) + length(2) + ML-KEM-768 public key(1184) +
        // X25519 public key(32). The hybrid IANA codepoint 0x11EC requires
        // the X25519 trailer; emitting only ML-KEM produces a wire-image
        // that no real Chrome / Firefox client ever produces and that
        // strict TLS 1.3 parsers reject as length-mismatched.
        size_ += 2 + 2 + 1184 + 32;
        break;
      case Op::Type::GreaseKeyShareEntry:
        // group(2 GREASE bytes) + length(2 = 0x0001) + body(1 = 0x00)
        size_ += 5;
        break;
      case Op::Type::X25519PublicKey:
        size_ += 32;
        break;
      case Op::Type::Scope16Begin:
        size_ += 2;
        scope_offsets_.push_back(size_);
        break;
      case Op::Type::Scope16End:
        CHECK(!scope_offsets_.empty());
        scope_offsets_.pop_back();
        break;
      case Op::Type::Permutation: {
        auto parts = op.permutation_parts;
        rng_shuffle(parts, context.rng());
        for (const auto &part : parts) {
          for (const auto &sub_op : part) {
            append(sub_op, context);
          }
        }
        break;
      }
      case Op::Type::PaddingToTarget: {
        // Padding only fires when ECH is disabled — ECH payload length
        // already provides per-build wire-size variability (144..240),
        // and emitting both ECH AND padding produces a wire-image that
        // no real Chrome client emits (verified against
        // chrome144/chrome146 captures, neither has 0x0015).
        //
        // Test lanes that need a deterministic body size set
        // `padding_extension_payload_length_override > 0`; in that case
        // a fixed-size padding extension is emitted unconditionally
        // (including under ECH), bypassing the target+entropy logic.
        const auto override_len = context.config().padding_extension_payload_length_override;
        if (override_len > 0) {
          size_ += 4 + override_len;
          break;
        }
        if (context.config().has_ech) {
          break;
        }
        auto effective_target =
            static_cast<size_t>(op.value) + static_cast<size_t>(context.config().padding_target_entropy);
        if (size_ < effective_target) {
          size_ = effective_target + 4;
        }
        break;
      }
      default:
        UNREACHABLE();
    }
  }

  Result<size_t> finish() {
    if (!scope_offsets_.empty()) {
      return Status::Error("Unbalanced scopes");
    }
    return size_;
  }

 private:
  size_t size_{0};
  vector<size_t> scope_offsets_;
};

class ByteWriter {
 public:
  explicit ByteWriter(MutableSlice dest) : all_(dest), remaining_(dest) {
  }

  void append(const Op &op, const ExecutionContext &context) {
    switch (op.type) {
      case Op::Type::Bytes:
        remaining_.copy_from(op.data);
        remaining_.remove_prefix(op.data.size());
        break;
      case Op::Type::RandomBytes:
        context.rng().fill_secure_bytes(remaining_.substr(0, op.length));
        remaining_.remove_prefix(op.length);
        break;
      case Op::Type::ZeroBytes:
        std::memset(remaining_.begin(), 0, op.length);
        remaining_.remove_prefix(op.length);
        break;
      case Op::Type::Domain: {
        auto domain = context.domain();
        remaining_.copy_from(domain);
        remaining_.remove_prefix(domain.size());
        break;
      }
      case Op::Type::GreaseValue: {
        auto grease = context.get_grease(op.value);
        remaining_[0] = grease;
        remaining_[1] = grease;
        remaining_.remove_prefix(2);
        break;
      }
      case Op::Type::X25519KeyShareEntry:
        append(Op::bytes("\x00\x1d\x00\x20"), context);
        store_x25519_key_share(context.rng());
        break;
      case Op::Type::Secp256r1KeyShareEntry:
        append(Op::bytes("\x00\x17\x00\x41"), context);
        store_secp256r1_key_share(context.rng());
        break;
      case Op::Type::X25519MlKem768KeyShareEntry: {
        // group(2 bytes, profile-specific PQ codepoint) +
        // length(0x04C0 = 1216 bytes) + ML-KEM-768 ek (1184) +
        // X25519 ek (32). Both halves of the hybrid public key MUST be
        // emitted in this exact order. The wire codepoint is sourced
        // from `config.pq_group_id_override` so test lanes that
        // exercise the legacy 0x6399 (X25519Kyber768Draft00) snapshot
        // can drive the executor without changing the production
        // 0x11EC default. See
        // `test/stealth/test_pq_hybrid_key_share_format_invariants.cpp`
        // and
        // `test/stealth/test_tls_context_entropy.cpp::ExplicitSerializerParametersDriveWireImage`
        // for the regression guards.
        auto pq_group = context.config().pq_group_id_override;
        char group_bytes[2] = {static_cast<char>((pq_group >> 8) & 0xFF), static_cast<char>(pq_group & 0xFF)};
        append(Op::bytes(Slice(group_bytes, 2)), context);
        append(Op::bytes("\x04\xc0"), context);
        store_mlkem_key_share(context.rng());
        store_x25519_key_share(context.rng());
        break;
      }
      case Op::Type::GreaseKeyShareEntry: {
        // GREASE key_share entry: 2 bytes group (paired GREASE byte) +
        // 2 bytes length (always 0x0001) + 1 byte body (always 0x00).
        // Real Chrome / Safari / iOS captures all carry exactly this
        // shape as the first key_share entry. Tests:
        // `test_grease_key_share_entry_invariants.cpp`.
        auto grease_byte = context.get_grease(static_cast<size_t>(op.value));
        remaining_[0] = grease_byte;
        remaining_[1] = grease_byte;
        remaining_[2] = '\x00';
        remaining_[3] = '\x01';
        remaining_[4] = '\x00';
        remaining_.remove_prefix(5);
        break;
      }
      case Op::Type::X25519PublicKey:
        store_x25519_key_share(context.rng());
        break;
      case Op::Type::Scope16Begin:
        scope_offsets_.push_back(offset());
        remaining_.remove_prefix(2);
        break;
      case Op::Type::Scope16End: {
        auto begin = scope_offsets_.back();
        scope_offsets_.pop_back();
        auto size = offset() - begin - 2;
        all_[begin] = static_cast<char>((size >> 8) & 0xff);
        all_[begin + 1] = static_cast<char>(size & 0xff);
        break;
      }
      case Op::Type::Permutation: {
        auto parts = op.permutation_parts;
        rng_shuffle(parts, context.rng());
        for (const auto &part : parts) {
          for (const auto &sub_op : part) {
            append(sub_op, context);
          }
        }
        break;
      }
      case Op::Type::PaddingToTarget: {
        // Padding only fires when ECH is disabled — see the matching
        // gate in `LengthCalculator::PaddingToTarget` for the rationale.
        // The `padding_extension_payload_length_override` test hook also
        // applies here in lockstep with the LengthCalculator pass so the
        // size accounting and the byte writes agree.
        const auto override_len = context.config().padding_extension_payload_length_override;
        if (override_len > 0) {
          append(Op::bytes("\x00\x15"), context);
          append(Op::scope16_begin(), context);
          append(Op::zero_bytes(static_cast<int>(override_len)), context);
          append(Op::scope16_end(), context);
          break;
        }
        if (context.config().has_ech) {
          break;
        }
        auto effective_target = op.value + context.config().padding_target_entropy;
        auto need = effective_target - static_cast<int>(offset());
        if (need > 0) {
          append(Op::bytes("\x00\x15"), context);
          append(Op::scope16_begin(), context);
          append(Op::zero_bytes(need), context);
          append(Op::scope16_end(), context);
        }
        break;
      }
      default:
        UNREACHABLE();
    }
  }

  void finalize(Slice secret, int32 unix_time) {
    auto hash_dest = all_.substr(11, 32);
    hmac_sha256(secret, all_, hash_dest);
    int32 old = static_cast<int32>(static_cast<uint8>(hash_dest[28])) |
                (static_cast<int32>(static_cast<uint8>(hash_dest[29])) << 8) |
                (static_cast<int32>(static_cast<uint8>(hash_dest[30])) << 16) |
                (static_cast<int32>(static_cast<uint8>(hash_dest[31])) << 24);
    int32 masked = old ^ unix_time;
    hash_dest[28] = static_cast<char>(masked & 0xff);
    hash_dest[29] = static_cast<char>((masked >> 8) & 0xff);
    hash_dest[30] = static_cast<char>((masked >> 16) & 0xff);
    hash_dest[31] = static_cast<char>((masked >> 24) & 0xff);
  }

 private:
  static BigNum get_y2(BigNum &x, const BigNum &mod, BigNumContext &ctx) {
    BigNum y = x.clone();
    BigNum coef = BigNum::from_decimal("486662").move_as_ok();
    BigNum::mod_add(y, y, coef, mod, ctx);
    BigNum::mod_mul(y, y, x, mod, ctx);
    BigNum one = BigNum::from_decimal("1").move_as_ok();
    BigNum::mod_add(y, y, one, mod, ctx);
    BigNum::mod_mul(y, y, x, mod, ctx);
    return y;
  }

  static BigNum get_double_x(BigNum &x, const BigNum &mod, BigNumContext &ctx) {
    BigNum denominator = get_y2(x, mod, ctx);
    BigNum coef = BigNum::from_decimal("4").move_as_ok();
    BigNum::mod_mul(denominator, denominator, coef, mod, ctx);

    BigNum numerator;
    BigNum::mod_mul(numerator, x, x, mod, ctx);
    BigNum one = BigNum::from_decimal("1").move_as_ok();
    BigNum::mod_sub(numerator, numerator, one, mod, ctx);
    BigNum::mod_mul(numerator, numerator, numerator, mod, ctx);

    auto inverse = BigNum::mod_inverse(denominator, mod, ctx);
    if (inverse.is_ok()) {
      denominator = inverse.move_as_ok();
    }
    BigNum::mod_mul(numerator, numerator, denominator, mod, ctx);
    return numerator;
  }

  static bool is_quadratic_residue(const BigNum &a) {
    BigNum mod = BigNum::from_hex("7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffed").move_as_ok();
    BigNum pow = BigNum::from_hex("3ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff6").move_as_ok();
    BigNumContext ctx;
    BigNum r;
    BigNum::mod_exp(r, a, pow, mod, ctx);
    return r.to_decimal() == "1";
  }

  static void derive_retry_candidate(MutableSlice dest, Slice seed_material, uint32 attempt) {
    CHECK(dest.size() == seed_material.size());
    if (attempt == 0) {
      dest.copy_from(seed_material);
      return;
    }

    char counter_bytes[4] = {
        static_cast<char>(attempt & 0xff),
        static_cast<char>((attempt >> 8) & 0xff),
        static_cast<char>((attempt >> 16) & 0xff),
        static_cast<char>((attempt >> 24) & 0xff),
    };
    string material;
    material.reserve(seed_material.size() + sizeof(counter_bytes));
    material.append(seed_material.begin(), seed_material.size());
    material.append(counter_bytes, sizeof(counter_bytes));

    string digest(dest.size(), '\0');
    sha256(material, digest);
    dest.copy_from(digest);
  }

  void store_x25519_key_share(stealth::IRng &rng) {
    constexpr uint32 kMaxRetryAttempts = 128;
    BigNum mod = BigNum::from_hex("7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffed").move_as_ok();
    BigNumContext ctx;
    auto key = remaining_.substr(0, 32);
    string seed_material(key.size(), '\0');
    rng.fill_secure_bytes(MutableSlice(seed_material));

    for (uint32 attempt = 0; attempt < kMaxRetryAttempts; attempt++) {
      // Pathological fixed-value RNGs used by adversarial tests can otherwise
      // feed the same invalid candidate forever and hang the builder.
      derive_retry_candidate(key, seed_material, attempt);
      key[31] = static_cast<char>(key[31] & 127);
      BigNum x = BigNum::from_binary(key);
      BigNum y = get_y2(x, mod, ctx);
      if (is_quadratic_residue(y)) {
        for (int i = 0; i < 3; i++) {
          x = get_double_x(x, mod, ctx);
        }
        key.copy_from(x.to_le_binary(32));
        remaining_.remove_prefix(32);
        return;
      }
    }

    UNREACHABLE();
  }

  void store_mlkem_key_share(stealth::IRng &rng) {
    for (size_t i = 0; i < 384; i++) {
      auto a = rng.secure_uint32() % 3329;
      auto b = rng.secure_uint32() % 3329;
      remaining_[0] = static_cast<char>(a & 255);
      remaining_[1] = static_cast<char>((a >> 8) + ((b & 15) << 4));
      remaining_[2] = static_cast<char>(b >> 4);
      remaining_.remove_prefix(3);
    }
    rng.fill_secure_bytes(remaining_.substr(0, 32));
    remaining_.remove_prefix(32);
  }

  void store_secp256r1_key_share(stealth::IRng &rng) {
    constexpr uint32 kMaxRetryAttempts = 128;
    EC_GROUP *group = EC_GROUP_new_by_curve_name(NID_X9_62_prime256v1);
    CHECK(group != nullptr);
    BN_CTX *ctx = BN_CTX_new();
    CHECK(ctx != nullptr);
    BIGNUM *order = BN_new();
    CHECK(order != nullptr);
    CHECK(EC_GROUP_get_order(group, order, ctx) == 1);
    BIGNUM *private_key = BN_new();
    CHECK(private_key != nullptr);

    char scalar_bytes[32];
    string seed_material(sizeof(scalar_bytes), '\0');
    rng.fill_secure_bytes(MutableSlice(seed_material));

    bool found_valid_scalar = false;
    for (uint32 attempt = 0; attempt < kMaxRetryAttempts; attempt++) {
      derive_retry_candidate(MutableSlice(scalar_bytes, sizeof(scalar_bytes)), seed_material, attempt);
      CHECK(BN_bin2bn(reinterpret_cast<const unsigned char *>(scalar_bytes), sizeof(scalar_bytes), private_key) !=
            nullptr);
      if (!BN_is_zero(private_key) && BN_cmp(private_key, order) < 0) {
        found_valid_scalar = true;
        break;
      }
    }
    CHECK(found_valid_scalar);

    EC_POINT *public_key = EC_POINT_new(group);
    CHECK(public_key != nullptr);
    CHECK(EC_POINT_mul(group, public_key, private_key, nullptr, nullptr, ctx) == 1);
    auto dest = remaining_.substr(0, 65);
    size_t written = EC_POINT_point2oct(group, public_key, POINT_CONVERSION_UNCOMPRESSED,
                                        reinterpret_cast<unsigned char *>(dest.begin()), 65, ctx);
    CHECK(written == 65);
    BN_free(private_key);
    BN_free(order);
    BN_CTX_free(ctx);
    EC_POINT_free(public_key);
    EC_GROUP_free(group);
    remaining_.remove_prefix(65);
  }

  size_t offset() const {
    return all_.size() - remaining_.size();
  }

  MutableSlice all_;
  MutableSlice remaining_;
  vector<size_t> scope_offsets_;
};

}  // namespace

Result<string> ClientHelloExecutor::execute(const vector<ClientHelloOp> &ops, Slice domain, Slice secret,
                                            int32 unix_time, const ExecutorConfig &config, stealth::IRng &rng) {
  CHECK(secret.size() == 16);
  CHECK(!domain.empty());
  ExecutionContext context(config, domain, rng);
  LengthCalculator calc;
  for (auto &op : ops) {
    calc.append(op, context);
  }
  TRY_RESULT(length, calc.finish());
  string result(length, '\0');
  ByteWriter writer(result);
  for (auto &op : ops) {
    writer.append(op, context);
  }
  writer.finalize(secret, unix_time);
  return result;
}

}  // namespace mtproto
}  // namespace td
