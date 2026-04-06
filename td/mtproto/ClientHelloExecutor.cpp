#include "td/mtproto/ClientHelloExecutor.h"

#include "td/mtproto/ProxySecret.h"
#include "td/mtproto/TlsInit.h"

#include "td/utils/as.h"
#include "td/utils/BigNum.h"
#include "td/utils/crypto.h"
#include "td/utils/logging.h"
#include "td/utils/Random.h"

#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/obj_mac.h>

#include <cstring>

namespace td {
namespace mtproto {

namespace {

using Op = ClientHelloOp;

class ExecutionContext {
 public:
  ExecutionContext(size_t grease_value_count, Slice domain) : grease_values_(grease_value_count, '\0'), domain_(domain.str()) {
    Grease::init(grease_values_);
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

 private:
  string grease_values_;
  string domain_;
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
        size_ += 2 + 2 + 1184;
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
        Random::shuffle(parts);
        for (const auto &part : parts) {
          for (const auto &sub_op : part) {
            append(sub_op, context);
          }
        }
        break;
      }
      case Op::Type::PaddingToTarget:
        if (size_ < static_cast<size_t>(op.value)) {
          size_ = static_cast<size_t>(op.value + 4);
        }
        break;
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
        Random::secure_bytes(remaining_.substr(0, op.length));
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
        store_x25519_key_share();
        break;
      case Op::Type::Secp256r1KeyShareEntry:
        append(Op::bytes("\x00\x17\x00\x41"), context);
        store_secp256r1_key_share();
        break;
      case Op::Type::X25519MlKem768KeyShareEntry:
        append(Op::bytes("\x11\xec\x04\xa0"), context);
        store_mlkem_key_share();
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
        Random::shuffle(parts);
        for (const auto &part : parts) {
          for (const auto &sub_op : part) {
            append(sub_op, context);
          }
        }
        break;
      }
      case Op::Type::PaddingToTarget: {
        auto need = op.value - static_cast<int>(offset());
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
    int32 old = as<int32>(hash_dest.substr(28).data());
    as<int32>(hash_dest.substr(28).data()) = old ^ unix_time;
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

  void store_x25519_key_share() {
    BigNum mod = BigNum::from_hex("7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffed").move_as_ok();
    BigNumContext ctx;
    auto key = remaining_.substr(0, 32);
    while (true) {
      Random::secure_bytes(key);
      key[31] = static_cast<char>(key[31] & 127);
      BigNum x = BigNum::from_binary(key);
      BigNum y = get_y2(x, mod, ctx);
      if (is_quadratic_residue(y)) {
        for (int i = 0; i < 3; i++) {
          x = get_double_x(x, mod, ctx);
        }
        key.copy_from(x.to_le_binary(32));
        break;
      }
    }
    remaining_.remove_prefix(32);
  }

  void store_mlkem_key_share() {
    for (size_t i = 0; i < 384; i++) {
      auto a = Random::secure_uint32() % 3329;
      auto b = Random::secure_uint32() % 3329;
      remaining_[0] = static_cast<char>(a & 255);
      remaining_[1] = static_cast<char>((a >> 8) + ((b & 15) << 4));
      remaining_[2] = static_cast<char>(b >> 4);
      remaining_.remove_prefix(3);
    }
    Random::secure_bytes(remaining_.substr(0, 32));
    remaining_.remove_prefix(32);
  }

  void store_secp256r1_key_share() {
    EC_KEY *ec_key = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
    CHECK(ec_key != nullptr);
    CHECK(EC_KEY_generate_key(ec_key) == 1);
    const EC_GROUP *group = EC_KEY_get0_group(ec_key);
    const EC_POINT *public_key = EC_KEY_get0_public_key(ec_key);
    CHECK(group != nullptr);
    CHECK(public_key != nullptr);
    auto dest = remaining_.substr(0, 65);
    size_t written =
        EC_POINT_point2oct(group, public_key, POINT_CONVERSION_UNCOMPRESSED, reinterpret_cast<unsigned char *>(dest.begin()),
                           65, nullptr);
    CHECK(written == 65);
    EC_KEY_free(ec_key);
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

Result<string> ClientHelloExecutor::execute(const vector<ClientHelloOp> &ops, Slice domain, Slice secret, int32 unix_time,
                                            size_t grease_value_count) {
  CHECK(secret.size() == 16);
  CHECK(!domain.empty());
  ExecutionContext context(grease_value_count, domain);
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
