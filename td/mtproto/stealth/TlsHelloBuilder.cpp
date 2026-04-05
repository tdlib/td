//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/mtproto/stealth/TlsHelloBuilder.h"

#include "td/mtproto/ProxySecret.h"

#include "td/utils/as.h"
#include "td/utils/BigNum.h"
#include "td/utils/common.h"
#include "td/utils/crypto.h"
#include "td/utils/logging.h"
#include "td/utils/Random.h"
#include "td/utils/Span.h"

#include <algorithm>
#include <cstring>

namespace td {
namespace mtproto {
namespace stealth {
namespace {

constexpr uint16 kPqHybridGroupId = 0x11EC;
constexpr uint16 kX25519GroupId = 0x001D;
constexpr uint16 kPqHybridKeyExchangeLength = 0x04C0;
constexpr uint16 kEchEncapsulatedKeyLength = 32;

void append_u16_be(string &out, uint16 value) {
  out.push_back(static_cast<char>((value >> 8) & 0xFF));
  out.push_back(static_cast<char>(value & 0xFF));
}

string make_supported_groups_profile_tail() {
  string result;
  result.reserve(8);
  append_u16_be(result, kPqHybridGroupId);
  append_u16_be(result, kX25519GroupId);
  append_u16_be(result, 0x0017);
  append_u16_be(result, 0x0018);
  return result;
}

string make_pq_key_share_prefix() {
  string result;
  result.reserve(7);
  result.append("\x00\x01\x00", 3);
  append_u16_be(result, kPqHybridGroupId);
  append_u16_be(result, kPqHybridKeyExchangeLength);
  return result;
}

string make_ech_enc_key_length_literal() {
  string result;
  result.reserve(2);
  append_u16_be(result, kEchEncapsulatedKeyLength);
  return result;
}

static_assert(kPqHybridKeyExchangeLength == 1184 + 32,
              "PQ key share length must match ML-KEM-768 (1184) + X25519 (32)");

void init_grease(MutableSlice res) {
  Random::secure_bytes(res);
  for (auto &c : res) {
    c = static_cast<char>((c & 0xF0) + 0x0A);
  }
  for (size_t i = 1; i < res.size(); i += 2) {
    if (res[i] == res[i - 1]) {
      res[i] ^= 0x10;
    }
  }
}

size_t get_padding_extension_content_len(size_t unpadded_len) {
  // RFC7685-style behavior: only pad when ClientHello falls into 256..511 range.
  if (unpadded_len <= 0xFF || unpadded_len >= 0x200) {
    return 0;
  }
  auto padding_len = 0x200 - unpadded_len;
  if (padding_len >= 5) {
    return padding_len - 4;
  }
  return 1;
}

int sample_ech_payload_length() {
  return 144 + static_cast<int>((Random::secure_uint32() % 4u) * 32u);
}

int sample_padding_entropy_length(bool enable_ech) {
  if (enable_ech) {
    return 0;
  }
  // Keep ECH-disabled lanes from collapsing to a single deterministic length.
  return static_cast<int>((Random::secure_uint32() & 0x7u) * 8u);
}

bool should_enable_ech(const NetworkRouteHints &route_hints) {
  // Fail closed: unknown and RU routes keep ECH disabled to avoid easy route-level blocking.
  return route_hints.is_known && !route_hints.is_ru;
}

void shuffle_fixture_bounded_windows(vector<string> &parts) {
  if (parts.size() < 6) {
    Random::shuffle(parts);
    return;
  }

  const size_t tail_anchor_index = parts.size() - 1;

  auto shuffle_window = [&](size_t begin, size_t end) {
    if (end <= begin + 1) {
      return;
    }
    vector<string> window;
    window.reserve(end - begin);
    for (size_t i = begin; i < end; i++) {
      window.push_back(std::move(parts[i]));
    }
    Random::shuffle(window);
    for (size_t i = begin; i < end; i++) {
      parts[i] = std::move(window[i - begin]);
    }
  };

  shuffle_window(0, std::min<size_t>(4, tail_anchor_index));
  shuffle_window(4, std::min<size_t>(8, tail_anchor_index));
  shuffle_window(8, std::min<size_t>(12, tail_anchor_index));

  if (tail_anchor_index > 12) {
    shuffle_window(12, tail_anchor_index);
  }
}

class TlsHello {
 public:
  struct Op {
    enum class Type {
      String,
      Random,
      Zero,
      Domain,
      Grease,
      Key,
      MlKem768Key,
      BeginScope,
      EndScope,
      Permutation,
      EchPayload,
      Padding
    };
    Type type;
    int length;
    int seed;
    string data;
    vector<vector<Op>> parts;

    static Op str(Slice str) {
      Op res;
      res.type = Type::String;
      res.data = str.str();
      return res;
    }
    static Op random(int length) {
      Op res;
      res.type = Type::Random;
      res.length = length;
      return res;
    }
    static Op zero(int length) {
      Op res;
      res.type = Type::Zero;
      res.length = length;
      return res;
    }
    static Op domain() {
      Op res;
      res.type = Type::Domain;
      return res;
    }
    static Op grease(int seed) {
      Op res;
      res.type = Type::Grease;
      res.seed = seed;
      return res;
    }
    static Op begin_scope() {
      Op res;
      res.type = Type::BeginScope;
      return res;
    }
    static Op end_scope() {
      Op res;
      res.type = Type::EndScope;
      return res;
    }
    static Op key() {
      Op res;
      res.type = Type::Key;
      return res;
    }
    static Op ml_kem_768_key() {
      Op res;
      res.type = Type::MlKem768Key;
      return res;
    }
    static Op permutation(vector<vector<Op>> parts) {
      Op res;
      res.type = Type::Permutation;
      res.parts = std::move(parts);
      return res;
    }
    static Op ech_payload() {
      Op res;
      res.type = Type::EchPayload;
      return res;
    }
    static Op padding() {
      Op res;
      res.type = Type::Padding;
      return res;
    }
  };

  static const TlsHello &get_default(bool enable_ech) {
    static TlsHello result_with_ech = [] {
      TlsHello res;
#if TD_DARWIN
      res.ops_ = {
          Op::str("\x16\x03\x01\x02\x00\x01\x00\x01\xfc\x03\x03"),
          Op::zero(32),
          Op::str("\x20"),
          Op::random(32),
          Op::str("\x00\x2a"),
          Op::grease(0),
          Op::str("\x13\x01\x13\x02\x13\x03\xc0\x2c\xc0\x2b\xcc\xa9\xc0\x30\xc0\x2f\xcc\xa8\xc0\x0a\xc0\x09\xc0\x14"
                  "\xc0\x13\x00\x9d\x00\x9c\x00\x35\x00\x2f\xc0\x08\xc0\x12\x00\x0a\x01\x00\x01\x89"),
          Op::grease(2),
          Op::str("\x00\x00\x00\x00"),
          Op::begin_scope(),
          Op::begin_scope(),
          Op::str("\x00"),
          Op::begin_scope(),
          Op::domain(),
          Op::end_scope(),
          Op::end_scope(),
          Op::end_scope(),
          Op::str("\x00\x17\x00\x00\xff\x01\x00\x01\x00\x00\x0a\x00\x0c\x00\x0a"),
          Op::grease(4),
          Op::str(
              "\x00\x1d\x00\x17\x00\x18\x00\x19\x00\x0b\x00\x02\x01\x00\x00\x10\x00\x0e\x00\x0c\x02\x68\x32\x08\x68\x74"
              "\x74\x70\x2f\x31\x2e\x31\x00\x05\x00\x05\x01\x00\x00\x00\x00\x00\x0d\x00\x16\x00\x14\x04\x03\x08\x04\x04"
              "\x01\x05\x03\x08\x05\x08\x05\x05\x01\x08\x06\x06\x01\x02\x01\x00\x12\x00\x00\x00\x33\x00\x2b\x00\x29"),
          Op::grease(4),
          Op::str("\x00\x01\x00\x00\x1d\x00\x20"),
          Op::key(),
          Op::str("\x00\x2d\x00\x02\x01\x01\x00\x2b\x00\x0b\x0a"),
          Op::grease(6),
          Op::str("\x03\x04\x03\x03\x03\x02\x03\x01\x00\x1b\x00\x03\x02\x00\x01"),
          Op::grease(3),
          Op::str("\x00\x01\x00"),
          Op::padding()};
#else
      res.ops_ = {
          Op::str("\x16\x03\x01"),
          Op::begin_scope(),
          Op::str("\x01\x00"),
          Op::begin_scope(),
          Op::str("\x03\x03"),
          Op::zero(32),
          Op::str("\x20"),
          Op::random(32),
          Op::str("\x00\x20"),
          Op::grease(0),
          Op::str("\x13\x01\x13\x02\x13\x03\xc0\x2b\xc0\x2f\xc0\x2c\xc0\x30\xcc\xa9\xcc\xa8\xc0\x13\xc0\x14\x00\x9c\x00"
                  "\x9d\x00\x2f\x00\x35\x01\x00"),
          Op::begin_scope(),
          Op::grease(2),
          Op::str("\x00\x00"),
          Op::permutation(
              {vector<Op>{Op::str("\x00\x00"), Op::begin_scope(), Op::begin_scope(), Op::str("\x00"), Op::begin_scope(),
                          Op::domain(), Op::end_scope(), Op::end_scope(), Op::end_scope()},
               vector<Op>{Op::str("\x00\x05\x00\x05\x01\x00\x00\x00\x00")},
               vector<Op>{Op::str("\x00\x0a\x00\x0c\x00\x0a"), Op::grease(4),
                          Op::str(make_supported_groups_profile_tail())},
               vector<Op>{Op::str("\x00\x0b\x00\x02\x01\x00")},
               vector<Op>{
                   Op::str("\x00\x0d\x00\x12\x00\x10\x04\x03\x08\x04\x04\x01\x05\x03\x08\x05\x05\x01\x08\x06\x06\x01")},
               vector<Op>{Op::str("\x00\x10\x00\x0e\x00\x0c\x02\x68\x32\x08\x68\x74\x74\x70\x2f\x31\x2e\x31")},
               vector<Op>{Op::str("\x00\x12\x00\x00")}, vector<Op>{Op::str("\x00\x17\x00\x00")},
               vector<Op>{Op::str("\x00\x1b\x00\x03\x02\x00\x02")}, vector<Op>{Op::str("\x00\x23\x00\x00")},
               vector<Op>{Op::str("\x00\x2b\x00\x07\x06"), Op::grease(6), Op::str("\x03\x04\x03\x03")},
               vector<Op>{Op::str("\x00\x2d\x00\x02\x01\x01")},
               vector<Op>{Op::str("\x00\x33\x04\xef\x04\xed"), Op::grease(4), Op::str(make_pq_key_share_prefix()),
                          Op::ml_kem_768_key(), Op::key(), Op::str("\x00\x1d\x00\x20"), Op::key()},
               vector<Op>{Op::str("\x44\xcd\x00\x05\x00\x03\x02\x68\x32")},
               vector<Op>{Op::str("\xfe\x0d"), Op::begin_scope(), Op::str("\x00\x00\x01\x00\x01"), Op::random(1),
                          Op::str(make_ech_enc_key_length_literal()), Op::random(32), Op::begin_scope(),
                          Op::ech_payload(), Op::end_scope(), Op::end_scope()},
               vector<Op>{Op::str("\xff\x01\x00\x01\x00")}}),
          Op::grease(3),
          Op::str("\x00\x01\x00"),
          Op::padding(),
          Op::end_scope(),
          Op::end_scope(),
          Op::end_scope()};
#endif
      return res;
    }();

#if TD_DARWIN
    static TlsHello result_without_ech = result_with_ech;
#else
    static TlsHello result_without_ech = [] {
      auto res = result_with_ech;
      for (auto &op : res.ops_) {
        if (op.type != Op::Type::Permutation) {
          continue;
        }
        op.parts.erase(std::remove_if(op.parts.begin(), op.parts.end(),
                                      [](const vector<Op> &part) {
                                        if (part.empty()) {
                                          return false;
                                        }
                                        const auto &first = part[0];
                                        if (first.type != Op::Type::String || first.data.size() < 2) {
                                          return false;
                                        }
                                        return static_cast<uint8>(first.data[0]) == 0xfe &&
                                               static_cast<uint8>(first.data[1]) == 0x0d;
                                      }),
                       op.parts.end());
      }
      return res;
    }();
#endif

    return enable_ech ? result_with_ech : result_without_ech;
  }

  Span<Op> get_ops() const {
    return ops_;
  }

  size_t get_grease_size() const {
    return grease_size_;
  }

 private:
  std::vector<Op> ops_;
  size_t grease_size_ = 7;
};

class TlsHelloContext {
 public:
  TlsHelloContext(size_t grease_size, string domain, bool enable_ech)
      : grease_(grease_size, '\0')
      , domain_(std::move(domain))
      , ech_payload_length_(sample_ech_payload_length())
      , padding_entropy_length_(sample_padding_entropy_length(enable_ech)) {
    init_grease(grease_);
  }

  char get_grease(size_t i) const {
    CHECK(i < grease_.size());
    return grease_[i];
  }
  size_t get_grease_size() const {
    return grease_.size();
  }
  Slice get_domain() const {
    return Slice(domain_).substr(0, ProxySecret::MAX_DOMAIN_LENGTH);
  }
  int get_ech_payload_length() const {
    return ech_payload_length_;
  }

  int get_padding_entropy_length() const {
    return padding_entropy_length_;
  }

 private:
  string grease_;
  string domain_;
  int ech_payload_length_{0};
  int padding_entropy_length_{0};
};

class TlsHelloCalcLength {
 public:
  void do_op(const TlsHello::Op &op, const TlsHelloContext *context) {
    if (status_.is_error()) {
      return;
    }
    using Type = TlsHello::Op::Type;
    switch (op.type) {
      case Type::String:
        size_ += op.data.size();
        break;
      case Type::Random:
        if (op.length <= 0 || op.length > 2048) {
          return on_error(Status::Error("Invalid random length"));
        }
        size_ += op.length;
        break;
      case Type::Zero:
        if (op.length <= 0 || op.length > 2048) {
          return on_error(Status::Error("Invalid zero length"));
        }
        size_ += op.length;
        break;
      case Type::Domain:
        CHECK(context);
        size_ += context->get_domain().size();
        break;
      case Type::Grease:
        CHECK(context);
        if (op.seed < 0 || static_cast<size_t>(op.seed) >= context->get_grease_size()) {
          return on_error(Status::Error("Invalid grease seed"));
        }
        size_ += 2;
        break;
      case Type::Key:
        size_ += 32;
        break;
      case Type::MlKem768Key:
        size_ += 1184;
        break;
      case Type::BeginScope:
        size_ += 2;
        scope_offset_.push_back(size_);
        break;
      case Type::EndScope: {
        if (scope_offset_.empty()) {
          return on_error(Status::Error("Unbalanced scopes"));
        }
        auto begin_offset = scope_offset_.back();
        scope_offset_.pop_back();
        auto end_offset = size_;
        auto size = end_offset - begin_offset;
        if (size >= (1 << 14)) {
          return on_error(Status::Error("Scope is too big"));
        }
        break;
      }
      case Type::Permutation: {
        for (const auto &part : op.parts) {
          for (auto &nested_op : part) {
            do_op(nested_op, context);
          }
        }
        break;
      }
      case Type::EchPayload:
        CHECK(context);
        size_ += context->get_ech_payload_length();
        break;
      case Type::Padding: {
        CHECK(context);
        auto padding_content_len = get_padding_extension_content_len(size_);
        auto padding_entropy_len = static_cast<size_t>(context->get_padding_entropy_length());
        if (padding_content_len > 0) {
          padding_content_len += padding_entropy_len;
          size_ += 4 + padding_content_len;
        } else if (padding_entropy_len > 0) {
          // ECH-disabled lanes may sit outside the RFC7685 window; keep a small
          // non-deterministic padding extension to avoid one-size fingerprints.
          size_ += 4 + 1 + padding_entropy_len;
        }
        break;
      }
      default:
        UNREACHABLE();
    }
  }

  size_t get_length() const {
    return size_;
  }

  Result<size_t> finish() {
    if (status_.is_error()) {
      return std::move(status_);
    }
    if (!scope_offset_.empty()) {
      return Status::Error("Unbalanced scopes");
    }
    if (size_ < 11 + 32) {
      return Status::Error("Too small for hash");
    }
    return size_;
  }

 private:
  size_t size_{0};
  Status status_;
  std::vector<size_t> scope_offset_;

  void on_error(Status error) {
    if (status_.is_ok()) {
      status_ = std::move(error);
    }
  }
};

class TlsHelloStore {
 public:
  explicit TlsHelloStore(MutableSlice dest) : data_(dest), dest_(dest) {
  }
  void do_op(const TlsHello::Op &op, const TlsHelloContext *context) {
    using Type = TlsHello::Op::Type;
    switch (op.type) {
      case Type::String:
        dest_.copy_from(op.data);
        dest_.remove_prefix(op.data.size());
        break;
      case Type::Random:
        Random::secure_bytes(dest_.substr(0, op.length));
        dest_.remove_prefix(op.length);
        break;
      case Type::Zero:
        std::memset(dest_.begin(), 0, op.length);
        dest_.remove_prefix(op.length);
        break;
      case Type::Domain: {
        CHECK(context);
        auto domain = context->get_domain();
        dest_.copy_from(domain);
        dest_.remove_prefix(domain.size());
        break;
      }
      case Type::Grease: {
        CHECK(context);
        auto grease = context->get_grease(op.seed);
        dest_[0] = grease;
        dest_[1] = grease;
        dest_.remove_prefix(2);
        break;
      }
      case Type::Key: {
        BigNum mod = BigNum::from_hex("7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffed").move_as_ok();
        BigNumContext big_num_context;
        auto key = dest_.substr(0, 32);
        while (true) {
          Random::secure_bytes(key);
          key[31] = static_cast<char>(key[31] & 127);

          BigNum x = BigNum::from_binary(key);
          BigNum y = get_y2(x, mod, big_num_context);
          if (is_quadratic_residue(y)) {
            for (int i = 0; i < 3; i++) {
              x = get_double_x(x, mod, big_num_context);
            }
            key.copy_from(x.to_le_binary(32));
            break;
          }
        }
        dest_.remove_prefix(32);
        break;
      }
      case Type::MlKem768Key:
        for (size_t i = 0; i < 384; i++) {
          auto a = Random::secure_uint32() % 3329;
          auto b = Random::secure_uint32() % 3329;
          dest_[0] = static_cast<char>(a & 255);
          dest_[1] = static_cast<char>((a >> 8) + ((b & 15) << 4));
          dest_[2] = static_cast<char>(b >> 4);
          dest_.remove_prefix(3);
        }
        do_op(TlsHello::Op::random(32), nullptr);
        break;
      case Type::BeginScope:
        scope_offset_.push_back(get_offset());
        dest_.remove_prefix(2);
        break;
      case Type::EndScope: {
        CHECK(!scope_offset_.empty());
        auto begin_offset = scope_offset_.back();
        scope_offset_.pop_back();
        auto end_offset = get_offset();
        size_t size = end_offset - begin_offset - 2;
        CHECK(size < (1 << 14));
        data_[begin_offset] = static_cast<char>((size >> 8) & 0xff);
        data_[begin_offset + 1] = static_cast<char>(size & 0xff);
        break;
      }
      case Type::Permutation: {
        vector<string> parts;
        for (const auto &part : op.parts) {
          TlsHelloCalcLength calc_length;
          for (auto &nested_op : part) {
            calc_length.do_op(nested_op, context);
          }
          auto length = calc_length.get_length();
          string data(length, '\0');
          TlsHelloStore storer(data);
          for (auto &nested_op : part) {
            storer.do_op(nested_op, context);
          }
          CHECK(storer.get_offset() == data.size());
          parts.push_back(std::move(data));
        }
        shuffle_fixture_bounded_windows(parts);
        for (auto &part : parts) {
          dest_.copy_from(part);
          dest_.remove_prefix(part.size());
        }
        break;
      }
      case Type::EchPayload:
        CHECK(context);
        do_op(TlsHello::Op::random(context->get_ech_payload_length()), nullptr);
        break;
      case Type::Padding: {
        CHECK(context);
        auto padding_content_len = get_padding_extension_content_len(get_offset());
        auto padding_entropy_len = static_cast<size_t>(context->get_padding_entropy_length());
        if (padding_content_len > 0) {
          padding_content_len += padding_entropy_len;
          do_op(TlsHello::Op::str("\x00\x15"), nullptr);
          do_op(TlsHello::Op::begin_scope(), nullptr);
          do_op(TlsHello::Op::zero(static_cast<int>(padding_content_len)), nullptr);
          do_op(TlsHello::Op::end_scope(), nullptr);
        } else if (padding_entropy_len > 0) {
          auto entropy_only_padding_len = 1 + padding_entropy_len;
          do_op(TlsHello::Op::str("\x00\x15"), nullptr);
          do_op(TlsHello::Op::begin_scope(), nullptr);
          do_op(TlsHello::Op::zero(static_cast<int>(entropy_only_padding_len)), nullptr);
          do_op(TlsHello::Op::end_scope(), nullptr);
        }
        break;
      }
      default:
        UNREACHABLE();
    }
  }

  void finish(Slice secret, int32 unix_time) {
    auto hash_dest = data_.substr(11, 32);
    hmac_sha256(secret, data_, hash_dest);
    int32 old = as<int32>(hash_dest.substr(28).data());
    as<int32>(hash_dest.substr(28).data()) = old ^ unix_time;
    CHECK(dest_.empty());
  }

 private:
  MutableSlice data_;
  MutableSlice dest_;
  vector<size_t> scope_offset_;

  static BigNum get_y2(BigNum &x, const BigNum &mod, BigNumContext &big_num_context) {
    // returns y = x^3 + 486662 * x^2 + x
    BigNum y = x.clone();
    BigNum coef = BigNum::from_decimal("486662").move_as_ok();
    BigNum::mod_add(y, y, coef, mod, big_num_context);
    BigNum::mod_mul(y, y, x, mod, big_num_context);
    BigNum one = BigNum::from_decimal("1").move_as_ok();
    BigNum::mod_add(y, y, one, mod, big_num_context);
    BigNum::mod_mul(y, y, x, mod, big_num_context);
    return y;
  }

  static BigNum get_double_x(BigNum &x, const BigNum &mod, BigNumContext &big_num_context) {
    // returns x_2 = (x^2 - 1)^2/(4*y^2)
    BigNum denominator = get_y2(x, mod, big_num_context);
    BigNum coef = BigNum::from_decimal("4").move_as_ok();
    BigNum::mod_mul(denominator, denominator, coef, mod, big_num_context);

    BigNum numerator;
    BigNum::mod_mul(numerator, x, x, mod, big_num_context);
    BigNum one = BigNum::from_decimal("1").move_as_ok();
    BigNum::mod_sub(numerator, numerator, one, mod, big_num_context);
    BigNum::mod_mul(numerator, numerator, numerator, mod, big_num_context);

    auto r_inverse = BigNum::mod_inverse(denominator, mod, big_num_context);
    if (r_inverse.is_error()) {
      LOG(ERROR) << r_inverse.error();
    } else {
      denominator = r_inverse.move_as_ok();
    }
    BigNum::mod_mul(numerator, numerator, denominator, mod, big_num_context);
    return numerator;
  }

  static bool is_quadratic_residue(const BigNum &a) {
    // 2^255 - 19
    BigNum mod = BigNum::from_hex("7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffed").move_as_ok();
    // (mod - 1) / 2 = 2^254 - 10
    BigNum pow = BigNum::from_hex("3ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff6").move_as_ok();

    BigNumContext context;
    BigNum r;
    BigNum::mod_exp(r, a, pow, mod, context);

    return r.to_decimal() == "1";
  }

  size_t get_offset() const {
    return data_.size() - dest_.size();
  }
};

}  // namespace

string build_default_tls_client_hello(string domain, Slice secret, int32 unix_time,
                                      const NetworkRouteHints &route_hints) {
  CHECK(!domain.empty());
  CHECK(secret.size() == 16);

  auto enable_ech = should_enable_ech(route_hints);
  auto &hello = TlsHello::get_default(enable_ech);
  TlsHelloContext context(hello.get_grease_size(), std::move(domain), enable_ech);
  TlsHelloCalcLength calc_length;
  for (auto &op : hello.get_ops()) {
    calc_length.do_op(op, &context);
  }
  auto length = calc_length.finish().move_as_ok();
  string data(length, '\0');
  TlsHelloStore storer(data);
  for (auto &op : hello.get_ops()) {
    storer.do_op(op, &context);
  }
  storer.finish(secret, unix_time);
  return data;
}

string build_default_tls_client_hello(string domain, Slice secret, int32 unix_time) {
  NetworkRouteHints default_route_hints;
  default_route_hints.is_known = false;
  default_route_hints.is_ru = false;
  return build_default_tls_client_hello(std::move(domain), secret, unix_time, default_route_hints);
}

}  // namespace stealth
}  // namespace mtproto
}  // namespace td
