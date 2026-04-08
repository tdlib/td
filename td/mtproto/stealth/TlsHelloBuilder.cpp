// SPDX-FileCopyrightText: Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: BSL-1.0 AND MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
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

constexpr uint16 kPqHybridKeyExchangeLength = detail::kCurrentSingleLanePqKeyShareLength;
constexpr uint16 kEchEncapsulatedKeyLength = detail::kCorrectEchEncKeyLen;
class SecureRng final : public IRng {
 public:
  void fill_secure_bytes(MutableSlice dest) final {
    Random::secure_bytes(dest);
  }

  uint32 secure_uint32() final {
    return Random::secure_uint32();
  }

  uint32 bounded(uint32 n) final {
    CHECK(n > 0);

    auto threshold = static_cast<uint32>(-n) % n;
    while (true) {
      auto value = secure_uint32();
      if (value >= threshold) {
        return value % n;
      }
    }
  }
};

static_assert(kPqHybridKeyExchangeLength == 1184 + 32,
              "PQ key share length must match ML-KEM-768 (1184) + X25519 (32)");

void init_grease(MutableSlice res, IRng &rng) {
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

int sample_ech_payload_length(IRng &rng) {
  return 144 + static_cast<int>(rng.bounded(4u) * 32u);
}

int sample_padding_entropy_length(bool enable_ech, IRng &rng) {
  if (enable_ech) {
    return 0;
  }
  // Keep ECH-disabled lanes from collapsing to a single deterministic length.
  return static_cast<int>(rng.bounded(8u) * 8u);
}

bool should_enable_ech(const NetworkRouteHints &route_hints) {
  // Fail closed: unknown and RU routes keep ECH disabled to avoid easy route-level blocking.
  return route_hints.is_known && !route_hints.is_ru;
}

enum class AlpnMode : uint8 {
  BrowserDefault = 0,
  Http11Only = 1,
};

Slice alpn_protocol_entries(AlpnMode alpn_mode) {
  static const char kBrowserDefault[] = "\x02\x68\x32\x08\x68\x74\x74\x70\x2f\x31\x2e\x31";
  static const char kHttp11Only[] = "\x08\x68\x74\x74\x70\x2f\x31\x2e\x31";

  switch (alpn_mode) {
    case AlpnMode::BrowserDefault:
      return Slice(kBrowserDefault, sizeof(kBrowserDefault) - 1);
    case AlpnMode::Http11Only:
      return Slice(kHttp11Only, sizeof(kHttp11Only) - 1);
    default:
      UNREACHABLE();
      return Slice();
  }
}

void shuffle_chrome_anchored_extensions(vector<string> &parts, IRng &rng) {
  // Match Chrome/uTLS ShuffleChromeTLSExtensions semantics for the subset of
  // extensions represented by this permutation: shuffle every non-anchor
  // extension as a single pool. GREASE and padding anchors stay outside of
  // the permutation block and retain their fixed positions.
  for (size_t i = parts.size(); i > 1; i--) {
    auto index = static_cast<uint32>(i);
    auto j = static_cast<size_t>(rng.bounded(index));
    std::swap(parts[i - 1], parts[j]);
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
      AlpnProtocols,
      EchPayload,
      EchEncKey,
      PqGroupId,
      PqKeyShare,
      AlpsType,
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
    static Op alpn_protocols() {
      Op res;
      res.type = Type::AlpnProtocols;
      return res;
    }
    static Op ech_payload() {
      Op res;
      res.type = Type::EchPayload;
      return res;
    }
    static Op ech_enc_key() {
      Op res;
      res.type = Type::EchEncKey;
      return res;
    }
    static Op pq_group_id() {
      Op res;
      res.type = Type::PqGroupId;
      return res;
    }
    static Op pq_key_share() {
      Op res;
      res.type = Type::PqKeyShare;
      return res;
    }
    static Op alps_type() {
      Op res;
      res.type = Type::AlpsType;
      return res;
    }
    static Op padding() {
      Op res;
      res.type = Type::Padding;
      return res;
    }
  };

  static TlsHello create_chromium_profile(bool include_ech, bool include_pq) {
    TlsHello res;
#if TD_DARWIN
    (void)include_ech;
    (void)include_pq;
    return get_default(true);
#else
    vector<vector<Op>> parts = {
        vector<Op>{Op::str("\x00\x00"), Op::begin_scope(), Op::begin_scope(), Op::str("\x00"), Op::begin_scope(),
                   Op::domain(), Op::end_scope(), Op::end_scope(), Op::end_scope()},
        vector<Op>{Op::str("\x00\x05\x00\x05\x01\x00\x00\x00\x00")},
        include_pq
            ? vector<Op>{Op::str("\x00\x0a\x00\x0c\x00\x0a"), Op::grease(4), Op::pq_group_id(),
                         Op::str("\x00\x1d\x00\x17\x00\x18")}
            : vector<Op>{Op::str("\x00\x0a\x00\x0a\x00\x08"), Op::grease(4), Op::str("\x00\x1d\x00\x17\x00\x18")},
        vector<Op>{Op::str("\x00\x0b\x00\x02\x01\x00")},
        vector<Op>{Op::str("\x00\x0d\x00\x12\x00\x10\x04\x03\x08\x04\x04\x01\x05\x03\x08\x05\x05\x01"
                           "\x08\x06\x06\x01")},
        vector<Op>{Op::str("\x00\x10"), Op::begin_scope(), Op::begin_scope(), Op::alpn_protocols(), Op::end_scope(),
                   Op::end_scope()},
        vector<Op>{Op::str("\x00\x12\x00\x00")},
        vector<Op>{Op::str("\x00\x17\x00\x00")},
        vector<Op>{Op::str("\x00\x1b\x00\x03\x02\x00\x02")},
        vector<Op>{Op::str("\x00\x23\x00\x00")},
        vector<Op>{Op::str("\x00\x2b\x00\x07\x06"), Op::grease(6), Op::str("\x03\x04\x03\x03")},
        vector<Op>{Op::str("\x00\x2d\x00\x02\x01\x01")},
        include_pq
            ? vector<Op>{Op::str("\x00\x33\x04\xef\x04\xed"), Op::grease(4), Op::str("\x00\x01\x00"),
                         Op::pq_key_share(), Op::ml_kem_768_key(), Op::key(), Op::str("\x00\x1d\x00\x20"), Op::key()}
            : vector<Op>{Op::str("\x00\x33\x00\x2b\x00\x29"), Op::grease(4),
                         Op::str("\x00\x01\x00"
                                 "\x00\x1d\x00\x20"),
                         Op::key()},
        vector<Op>{Op::alps_type(), Op::str("\x00\x05\x00\x03\x02\x68\x32")},
        vector<Op>{Op::str("\xff\x01\x00\x01\x00")},
    };

    if (include_ech) {
      parts.push_back(vector<Op>{Op::str("\xfe\x0d"), Op::begin_scope(), Op::str("\x00\x00\x01\x00\x01"), Op::random(1),
                                 Op::begin_scope(), Op::ech_enc_key(), Op::end_scope(), Op::begin_scope(),
                                 Op::ech_payload(), Op::end_scope(), Op::end_scope()});
    }

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
        Op::permutation(std::move(parts)),
        Op::grease(3),
        Op::str("\x00\x01\x00"),
        Op::padding(),
        Op::end_scope(),
        Op::end_scope(),
        Op::end_scope(),
    };
    return res;
#endif
  }

  static TlsHello create_fixed_profile_without_chromium_only_features() {
    TlsHello res;
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
        Op::begin_scope(),
        Op::begin_scope(),
        Op::str("\x00"),
        Op::begin_scope(),
        Op::domain(),
        Op::end_scope(),
        Op::end_scope(),
        Op::end_scope(),
        Op::str("\x00\x05\x00\x05\x01\x00\x00\x00\x00"),
        Op::str("\x00\x0a\x00\x0a\x00\x08"),
        Op::grease(4),
        Op::str("\x00\x1d\x00\x17\x00\x18"),
        Op::str("\x00\x0b\x00\x02\x01\x00"),
        Op::str("\x00\x0d\x00\x12\x00\x10\x04\x03\x08\x04\x04\x01\x05\x03\x08\x05\x05\x01"
                "\x08\x06\x06\x01"),
        Op::str("\x00\x10"),
        Op::begin_scope(),
        Op::begin_scope(),
        Op::alpn_protocols(),
        Op::end_scope(),
        Op::end_scope(),
        Op::str("\x00\x12\x00\x00"),
        Op::str("\x00\x17\x00\x00"),
        Op::str("\x00\x1b\x00\x03\x02\x00\x02"),
        Op::str("\x00\x23\x00\x00"),
        Op::str("\x00\x2b\x00\x07\x06"),
        Op::grease(6),
        Op::str("\x03\x04\x03\x03"),
        Op::str("\x00\x2d\x00\x02\x01\x01"),
        Op::str("\x00\x33\x00\x2b\x00\x29"),
        Op::grease(4),
        Op::str("\x00\x01\x00"
                "\x00\x1d\x00\x20"),
        Op::key(),
        Op::str("\xff\x01\x00\x01\x00"),
        Op::grease(3),
        Op::str("\x00\x01\x00"),
        Op::end_scope(),
        Op::end_scope(),
        Op::end_scope(),
    };
    return res;
  }

  static TlsHello create_firefox_148_profile(bool enable_ech) {
    TlsHello res;
    auto record_size_limit = profile_spec(BrowserProfile::Firefox148).record_size_limit;
    CHECK(record_size_limit != 0);
    string record_size_limit_extension("\x00\x1c\x00\x02", 4);
    record_size_limit_extension += static_cast<char>((record_size_limit >> 8) & 0xff);
    record_size_limit_extension += static_cast<char>(record_size_limit & 0xff);
    res.ops_ = {
        Op::str("\x16\x03\x01"),
        Op::begin_scope(),
        Op::str("\x01\x00"),
        Op::begin_scope(),
        Op::str("\x03\x03"),
        Op::zero(32),
        Op::str("\x20"),
        Op::random(32),
        Op::str("\x00\x22"
                "\x13\x01\x13\x03\x13\x02\xc0\x2b\xc0\x2f\xcc\xa9\xcc\xa8\xc0\x2c\xc0\x30\xc0\x0a\xc0\x09"
                "\xc0\x13\xc0\x14\x00\x9c\x00\x9d\x00\x2f\x00\x35"
                "\x01\x00"),
        Op::begin_scope(),
        Op::str("\x00\x00"),
        Op::begin_scope(),
        Op::begin_scope(),
        Op::str("\x00"),
        Op::begin_scope(),
        Op::domain(),
        Op::end_scope(),
        Op::end_scope(),
        Op::end_scope(),
        Op::str("\x00\x17\x00\x00"),
        Op::str("\xff\x01\x00\x01\x00"),
        Op::str("\x00\x0a\x00\x10\x00\x0e"),
        Op::pq_group_id(),
        Op::str("\x00\x1d\x00\x17\x00\x18\x00\x19\x01\x00\x01\x01"),
        Op::str("\x00\x0b\x00\x02\x01\x00"),
        Op::str("\x00\x23\x00\x00"),
        Op::str("\x00\x10"),
        Op::begin_scope(),
        Op::begin_scope(),
        Op::alpn_protocols(),
        Op::end_scope(),
        Op::end_scope(),
        Op::str("\x00\x05\x00\x05\x01\x00\x00\x00\x00"),
        Op::str("\x00\x22\x00\x0a\x00\x08\x04\x03\x05\x03\x06\x03\x02\x03"),
        Op::str("\x00\x12\x00\x00"),
        Op::str("\x00\x33"),
        Op::begin_scope(),
        Op::begin_scope(),
        Op::pq_key_share(),
        Op::ml_kem_768_key(),
        Op::key(),
        Op::str("\x00\x1d\x00\x20"),
        Op::key(),
        Op::str("\x00\x17\x00\x41"),
        Op::random(65),
        Op::end_scope(),
        Op::end_scope(),
        Op::str("\x00\x2b\x00\x05\x04\x03\x04\x03\x03"),
        Op::str("\x00\x0d\x00\x18\x00\x16\x04\x03\x05\x03\x06\x03\x08\x04\x08\x05\x08\x06\x04\x01\x05\x01\x06\x01\x02"
                "\x03\x02\x01"),
        Op::str("\x00\x2d\x00\x02\x01\x01"),
        Op::str(record_size_limit_extension),
        Op::str("\x00\x1b\x00\x07\x06\x00\x01\x00\x02\x00\x03"),
        enable_ech ? Op::str("\xfe\x0d") : Op::str(""),
        enable_ech ? Op::begin_scope() : Op::str(""),
        enable_ech ? Op::str("\x00\x00\x01\x00\x03") : Op::str(""),
        enable_ech ? Op::random(1) : Op::str(""),
        enable_ech ? Op::begin_scope() : Op::str(""),
        enable_ech ? Op::key() : Op::str(""),
        enable_ech ? Op::end_scope() : Op::str(""),
        enable_ech ? Op::begin_scope() : Op::str(""),
        enable_ech ? Op::random(239) : Op::str(""),
        enable_ech ? Op::end_scope() : Op::str(""),
        enable_ech ? Op::end_scope() : Op::str(""),
        Op::end_scope(),
        Op::end_scope(),
        Op::end_scope(),
    };
    if (!enable_ech) {
      res.ops_.erase(std::remove_if(res.ops_.begin(), res.ops_.end(),
                                    [](const Op &op) { return op.type == Op::Type::String && op.data.empty(); }),
                     res.ops_.end());
    }
    return res;
  }

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
          Op::str("\x00\x1d\x00\x17\x00\x18\x00\x19\x00\x0b\x00\x02\x01\x00"),
          Op::str("\x00\x10"),
          Op::begin_scope(),
          Op::begin_scope(),
          Op::alpn_protocols(),
          Op::end_scope(),
          Op::end_scope(),
          Op::str(
              "\x00\x05\x00\x05\x01\x00\x00\x00\x00\x00\x0d\x00\x16\x00\x14\x04\x03\x08\x04\x04"
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
               vector<Op>{Op::str("\x00\x0a\x00\x0c\x00\x0a"), Op::grease(4), Op::pq_group_id(),
                          Op::str("\x00\x1d\x00\x17\x00\x18")},
               vector<Op>{Op::str("\x00\x0b\x00\x02\x01\x00")},
               vector<Op>{
                   Op::str("\x00\x0d\x00\x12\x00\x10\x04\x03\x08\x04\x04\x01\x05\x03\x08\x05\x05\x01\x08\x06\x06\x01")},
               vector<Op>{Op::str("\x00\x10"), Op::begin_scope(), Op::begin_scope(), Op::alpn_protocols(),
                          Op::end_scope(), Op::end_scope()},
               vector<Op>{Op::str("\x00\x12\x00\x00")}, vector<Op>{Op::str("\x00\x17\x00\x00")},
               vector<Op>{Op::str("\x00\x1b\x00\x03\x02\x00\x02")}, vector<Op>{Op::str("\x00\x23\x00\x00")},
               vector<Op>{Op::str("\x00\x2b\x00\x07\x06"), Op::grease(6), Op::str("\x03\x04\x03\x03")},
               vector<Op>{Op::str("\x00\x2d\x00\x02\x01\x01")},
               vector<Op>{Op::str("\x00\x33\x04\xef\x04\xed"), Op::grease(4), Op::str("\x00\x01\x00"),
                          Op::pq_key_share(), Op::ml_kem_768_key(), Op::key(), Op::str("\x00\x1d\x00\x20"), Op::key()},
               vector<Op>{Op::str("\x44\xcd\x00\x05\x00\x03\x02\x68\x32")},
               vector<Op>{Op::str("\xfe\x0d"), Op::begin_scope(), Op::str("\x00\x00\x01\x00\x01"), Op::random(1),
                          Op::begin_scope(), Op::ech_enc_key(), Op::end_scope(), Op::begin_scope(), Op::ech_payload(),
                          Op::end_scope(), Op::end_scope()},
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

  static const TlsHello &get_profile(BrowserProfile profile, bool enable_ech) {
#if TD_DARWIN
    (void)profile;
    return get_default(enable_ech);
#else
    switch (profile) {
      case BrowserProfile::Chrome133:
      case BrowserProfile::Chrome131: {
        static const TlsHello hello_with_ech = create_chromium_profile(true, true);
        static const TlsHello hello_without_ech = create_chromium_profile(false, true);
        return enable_ech ? hello_with_ech : hello_without_ech;
      }
      case BrowserProfile::Chrome120: {
        static const TlsHello hello_with_ech = create_chromium_profile(true, false);
        static const TlsHello hello_without_ech = create_chromium_profile(false, false);
        return enable_ech ? hello_with_ech : hello_without_ech;
      }
      case BrowserProfile::Firefox148: {
        static const TlsHello hello_with_ech = create_firefox_148_profile(true);
        static const TlsHello hello_without_ech = create_firefox_148_profile(false);
        return enable_ech ? hello_with_ech : hello_without_ech;
      }
      case BrowserProfile::Safari26_3:
      case BrowserProfile::IOS14:
      case BrowserProfile::Android11_OkHttp: {
        static const TlsHello hello = create_fixed_profile_without_chromium_only_features();
        return hello;
      }
      default:
        UNREACHABLE();
        return get_default(enable_ech);
    }
#endif
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
  TlsHelloContext(size_t grease_size, string domain, bool enable_ech, AlpnMode alpn_mode, IRng &rng)
      : grease_(grease_size, '\0')
      , domain_(std::move(domain))
      , ech_payload_length_(sample_ech_payload_length(rng))
      , padding_entropy_length_(sample_padding_entropy_length(enable_ech, rng))
      , alpn_mode_(alpn_mode)
      , pq_group_id_(detail::kCurrentSingleLanePqGroupId)
      , alps_extension_type_(detail::kCurrentSingleLaneAlpsType)
      , ech_enc_key_length_(kEchEncapsulatedKeyLength) {
    init_grease(grease_, rng);
  }

  TlsHelloContext(size_t grease_size, string domain, const detail::TlsHelloBuildOptions &options, IRng &rng)
      : grease_(grease_size, '\0')
      , domain_(std::move(domain))
      , ech_payload_length_(options.ech_payload_length)
      , padding_entropy_length_(0)
      , padding_extension_payload_length_(options.padding_extension_payload_length)
      , alpn_mode_(AlpnMode::BrowserDefault)
      , pq_group_id_(options.pq_group_id)
      , alps_extension_type_(options.alps_extension_type)
      , ech_enc_key_length_(options.ech_enc_key_length) {
    init_grease(grease_, rng);
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

  Slice get_alpn_protocol_entries() const {
    return alpn_protocol_entries(alpn_mode_);
  }

  uint16 get_pq_group_id() const {
    return pq_group_id_;
  }

  void set_pq_group_id(uint16 pq_group_id) {
    pq_group_id_ = pq_group_id;
  }

  uint16 get_pq_key_share_length() const {
    return kPqHybridKeyExchangeLength;
  }

  uint16 get_ech_enc_key_length() const {
    return ech_enc_key_length_;
  }

  uint16 get_alps_extension_type() const {
    return alps_extension_type_;
  }

  void set_alps_extension_type(uint16 alps_extension_type) {
    alps_extension_type_ = alps_extension_type;
  }

  size_t get_padding_extension_payload_length() const {
    return padding_extension_payload_length_;
  }

  void set_padding_extension_payload_length(size_t padding_extension_payload_length) {
    padding_extension_payload_length_ = padding_extension_payload_length;
  }

 private:
  string grease_;
  string domain_;
  int ech_payload_length_{0};
  int padding_entropy_length_{0};
  size_t padding_extension_payload_length_{0};
  AlpnMode alpn_mode_{AlpnMode::BrowserDefault};
  uint16 pq_group_id_{0};
  uint16 alps_extension_type_{0};
  uint16 ech_enc_key_length_{0};
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
      case Type::AlpnProtocols:
        CHECK(context);
        size_ += context->get_alpn_protocol_entries().size();
        break;
      case Type::EchPayload:
        CHECK(context);
        size_ += context->get_ech_payload_length();
        break;
      case Type::EchEncKey:
        CHECK(context);
        size_ += context->get_ech_enc_key_length();
        break;
      case Type::PqGroupId:
        CHECK(context);
        size_ += 2;
        break;
      case Type::PqKeyShare:
        CHECK(context);
        size_ += 4;
        break;
      case Type::AlpsType:
        CHECK(context);
        size_ += 2;
        break;
      case Type::Padding: {
        CHECK(context);
        auto padding_extension_payload_length = context->get_padding_extension_payload_length();
        if (padding_extension_payload_length > 0) {
          size_ += 4 + padding_extension_payload_length;
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
  TlsHelloStore(MutableSlice dest, IRng &rng) : data_(dest), dest_(dest), rng_(rng) {
  }
  void do_op(const TlsHello::Op &op, const TlsHelloContext *context) {
    using Type = TlsHello::Op::Type;
    switch (op.type) {
      case Type::String:
        dest_.copy_from(op.data);
        dest_.remove_prefix(op.data.size());
        break;
      case Type::Random:
        rng_.fill_secure_bytes(dest_.substr(0, op.length));
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
          rng_.fill_secure_bytes(key);
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
          auto a = rng_.bounded(3329);
          auto b = rng_.bounded(3329);
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
          TlsHelloStore storer(data, rng_);
          for (auto &nested_op : part) {
            storer.do_op(nested_op, context);
          }
          CHECK(storer.get_offset() == data.size());
          parts.push_back(std::move(data));
        }
        shuffle_chrome_anchored_extensions(parts, rng_);
        for (auto &part : parts) {
          dest_.copy_from(part);
          dest_.remove_prefix(part.size());
        }
        break;
      }
      case Type::AlpnProtocols: {
        CHECK(context);
        auto entries = context->get_alpn_protocol_entries();
        dest_.copy_from(entries);
        dest_.remove_prefix(entries.size());
        break;
      }
      case Type::EchPayload:
        CHECK(context);
        do_op(TlsHello::Op::random(context->get_ech_payload_length()), nullptr);
        break;
      case Type::EchEncKey:
        CHECK(context);
        if (context->get_ech_enc_key_length() == kEchEncapsulatedKeyLength) {
          do_op(TlsHello::Op::key(), nullptr);
        } else {
          do_op(TlsHello::Op::random(context->get_ech_enc_key_length()), nullptr);
        }
        break;
      case Type::PqGroupId: {
        CHECK(context);
        auto group_id = context->get_pq_group_id();
        dest_[0] = static_cast<char>((group_id >> 8) & 0xFF);
        dest_[1] = static_cast<char>(group_id & 0xFF);
        dest_.remove_prefix(2);
        break;
      }
      case Type::PqKeyShare: {
        CHECK(context);
        auto group_id = context->get_pq_group_id();
        auto key_share_length = context->get_pq_key_share_length();
        dest_[0] = static_cast<char>((group_id >> 8) & 0xFF);
        dest_[1] = static_cast<char>(group_id & 0xFF);
        dest_[2] = static_cast<char>((key_share_length >> 8) & 0xFF);
        dest_[3] = static_cast<char>(key_share_length & 0xFF);
        dest_.remove_prefix(4);
        break;
      }
      case Type::AlpsType: {
        CHECK(context);
        auto alps_type = context->get_alps_extension_type();
        dest_[0] = static_cast<char>((alps_type >> 8) & 0xFF);
        dest_[1] = static_cast<char>(alps_type & 0xFF);
        dest_.remove_prefix(2);
        break;
      }
      case Type::Padding: {
        CHECK(context);
        auto padding_extension_payload_length = context->get_padding_extension_payload_length();
        if (padding_extension_payload_length > 0) {
          do_op(TlsHello::Op::str("\x00\x15"), nullptr);
          do_op(TlsHello::Op::begin_scope(), nullptr);
          do_op(TlsHello::Op::zero(static_cast<int>(padding_extension_payload_length)), nullptr);
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
  IRng &rng_;
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

namespace detail {

string build_default_tls_client_hello_with_options(string domain, Slice secret, int32 unix_time,
                                                   const NetworkRouteHints &route_hints, IRng &rng,
                                                   const TlsHelloBuildOptions &options) {
  CHECK(!domain.empty());
  CHECK(secret.size() == 16);

  auto enable_ech = should_enable_ech(route_hints);
  auto &hello = TlsHello::get_default(enable_ech);
  TlsHelloContext context(hello.get_grease_size(), std::move(domain), options, rng);

  TlsHelloCalcLength calc_length;
  for (auto &op : hello.get_ops()) {
    calc_length.do_op(op, &context);
  }
  auto length = calc_length.finish().move_as_ok();
  string data(length, '\0');
  TlsHelloStore storer(data, rng);
  for (auto &op : hello.get_ops()) {
    storer.do_op(op, &context);
  }
  storer.finish(secret, unix_time);
  return data;
}

}  // namespace detail

namespace {

string build_tls_client_hello_for_profile_with_alpn_mode(string domain, Slice secret, int32 unix_time,
                                                         BrowserProfile profile, EchMode ech_mode, AlpnMode alpn_mode,
                                                         IRng &rng) {
  CHECK(!domain.empty());
  CHECK(secret.size() == 16);

  auto &spec = profile_spec(profile);
  auto enable_ech = ech_mode == EchMode::Rfc9180Outer && spec.allows_ech;
  auto &hello = TlsHello::get_profile(profile, enable_ech);
  TlsHelloContext context(hello.get_grease_size(), std::move(domain), enable_ech, alpn_mode, rng);
  context.set_alps_extension_type(spec.alps_type);
  if (spec.has_pq) {
    context.set_pq_group_id(spec.pq_group_id);
  }

  auto padding_policy = spec.allows_padding ? PaddingPolicy{} : no_padding_policy();
  TlsHelloCalcLength unpadded_calc_length;
  for (auto &op : hello.get_ops()) {
    unpadded_calc_length.do_op(op, &context);
  }
  auto unpadded_length = unpadded_calc_length.finish().move_as_ok();
  context.set_padding_extension_payload_length(resolve_padding_extension_payload_len(
      padding_policy, unpadded_length, static_cast<size_t>(context.get_padding_entropy_length())));

  TlsHelloCalcLength calc_length;
  for (auto &op : hello.get_ops()) {
    calc_length.do_op(op, &context);
  }
  auto length = calc_length.finish().move_as_ok();
  string data(length, '\0');
  TlsHelloStore storer(data, rng);
  for (auto &op : hello.get_ops()) {
    storer.do_op(op, &context);
  }
  storer.finish(secret, unix_time);
  return data;
}

string build_default_tls_client_hello_with_alpn_mode(string domain, Slice secret, int32 unix_time,
                                                     const NetworkRouteHints &route_hints, AlpnMode alpn_mode,
                                                     IRng &rng) {
  CHECK(!domain.empty());
  CHECK(secret.size() == 16);

  auto enable_ech = should_enable_ech(route_hints);
  auto padding_policy = PaddingPolicy{};
  auto &hello = TlsHello::get_default(enable_ech);
  TlsHelloContext context(hello.get_grease_size(), std::move(domain), enable_ech, alpn_mode, rng);
  TlsHelloCalcLength unpadded_calc_length;
  for (auto &op : hello.get_ops()) {
    unpadded_calc_length.do_op(op, &context);
  }
  auto unpadded_length = unpadded_calc_length.finish().move_as_ok();
  context.set_padding_extension_payload_length(resolve_padding_extension_payload_len(
      padding_policy, unpadded_length, static_cast<size_t>(context.get_padding_entropy_length())));

  TlsHelloCalcLength calc_length;
  for (auto &op : hello.get_ops()) {
    calc_length.do_op(op, &context);
  }
  auto length = calc_length.finish().move_as_ok();
  string data(length, '\0');
  TlsHelloStore storer(data, rng);
  for (auto &op : hello.get_ops()) {
    storer.do_op(op, &context);
  }
  storer.finish(secret, unix_time);
  return data;
}

}  // namespace

string build_default_tls_client_hello(string domain, Slice secret, int32 unix_time,
                                      const NetworkRouteHints &route_hints, IRng &rng) {
  return build_default_tls_client_hello_with_alpn_mode(std::move(domain), secret, unix_time, route_hints,
                                                       AlpnMode::BrowserDefault, rng);
}

string build_proxy_tls_client_hello(string domain, Slice secret, int32 unix_time, const NetworkRouteHints &route_hints,
                                    IRng &rng) {
  return build_default_tls_client_hello_with_alpn_mode(std::move(domain), secret, unix_time, route_hints,
                                                       AlpnMode::Http11Only, rng);
}

string build_runtime_tls_client_hello(string domain, Slice secret, int32 unix_time,
                                      const NetworkRouteHints &route_hints, IRng &rng) {
  CHECK(!domain.empty());
  CHECK(secret.size() == 16);

#if TD_DARWIN
  return build_default_tls_client_hello(std::move(domain), secret, unix_time, route_hints, rng);
#else
  auto platform = default_runtime_platform_hints();
  auto profile = pick_runtime_profile(domain, unix_time, platform);
  auto ech_mode = get_runtime_ech_decision(domain, unix_time, route_hints).ech_mode;
  return build_tls_client_hello_for_profile(std::move(domain), secret, unix_time, profile, ech_mode, rng);
#endif
}

string build_runtime_tls_client_hello(string domain, Slice secret, int32 unix_time,
                                      const NetworkRouteHints &route_hints) {
  SecureRng rng;
  return build_runtime_tls_client_hello(std::move(domain), secret, unix_time, route_hints, rng);
}

string build_tls_client_hello_for_profile(string domain, Slice secret, int32 unix_time, BrowserProfile profile,
                                          EchMode ech_mode, IRng &rng) {
  return build_tls_client_hello_for_profile_with_alpn_mode(std::move(domain), secret, unix_time, profile, ech_mode,
                                                           AlpnMode::BrowserDefault, rng);
}

string build_proxy_tls_client_hello_for_profile(string domain, Slice secret, int32 unix_time, BrowserProfile profile,
                                                EchMode ech_mode, IRng &rng) {
  return build_tls_client_hello_for_profile_with_alpn_mode(std::move(domain), secret, unix_time, profile, ech_mode,
                                                           AlpnMode::Http11Only, rng);
}

string build_tls_client_hello_for_profile(string domain, Slice secret, int32 unix_time, BrowserProfile profile,
                                          EchMode ech_mode) {
  SecureRng rng;
  return build_tls_client_hello_for_profile(std::move(domain), secret, unix_time, profile, ech_mode, rng);
}

string build_proxy_tls_client_hello_for_profile(string domain, Slice secret, int32 unix_time, BrowserProfile profile,
                                                EchMode ech_mode) {
  SecureRng rng;
  return build_proxy_tls_client_hello_for_profile(std::move(domain), secret, unix_time, profile, ech_mode, rng);
}

string build_default_tls_client_hello(string domain, Slice secret, int32 unix_time,
                                      const NetworkRouteHints &route_hints) {
  SecureRng rng;
  return build_default_tls_client_hello(std::move(domain), secret, unix_time, route_hints, rng);
}

string build_proxy_tls_client_hello(string domain, Slice secret, int32 unix_time,
                                    const NetworkRouteHints &route_hints) {
  SecureRng rng;
  return build_proxy_tls_client_hello(std::move(domain), secret, unix_time, route_hints, rng);
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
