//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/mtproto/ProxySecret.h"

#include "td/utils/base64.h"
#include "td/utils/format.h"
#include "td/utils/misc.h"

namespace td {
namespace mtproto {

namespace {

constexpr size_t get_max_proxy_secret_size() {
  return 17 + ProxySecret::MAX_DOMAIN_LENGTH;
}

constexpr size_t get_max_encoded_proxy_secret_size(bool truncate_if_needed) {
  // Hex form is the longest accepted textual representation.
  auto max_raw_secret_size = get_max_proxy_secret_size() + static_cast<size_t>(truncate_if_needed);
  return 2 * max_raw_secret_size;
}

bool is_ascii_alnum(unsigned char c) {
  return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

const char *get_tls_emulation_domain_error(Slice domain) {
  if (domain.empty() || domain.size() > ProxySecret::MAX_DOMAIN_LENGTH) {
    return "length_out_of_bounds";
  }

  size_t label_size = 0;
  bool label_starts = true;
  bool label_ends_with_hyphen = false;
  for (auto c : domain) {
    auto byte = static_cast<unsigned char>(c);
    if (byte == '.') {
      if (label_size == 0 || label_ends_with_hyphen) {
        return "empty_or_hyphen_terminated_label";
      }
      label_size = 0;
      label_starts = true;
      label_ends_with_hyphen = false;
      continue;
    }

    if (!(is_ascii_alnum(byte) || byte == '-')) {
      return "non_ascii_alnum_or_hyphen";
    }
    if (label_starts && byte == '-') {
      return "label_starts_with_hyphen";
    }
    label_starts = false;
    label_ends_with_hyphen = (byte == '-');
    label_size++;
    if (label_size > 63) {
      return "label_too_long";
    }
  }

  if (label_size == 0 || label_ends_with_hyphen) {
    return "empty_or_hyphen_terminated_label";
  }

  return nullptr;
}

}  // namespace

Result<ProxySecret> ProxySecret::from_link(Slice encoded_secret, bool truncate_if_needed) {
  auto max_encoded_secret_size = get_max_encoded_proxy_secret_size(truncate_if_needed);
  if (encoded_secret.size() > max_encoded_secret_size) {
    return Status::Error(400, PSLICE() << "Wrong proxy secret: reason=encoded_length_out_of_bounds encoded_length="
                                       << encoded_secret.size() << " max_encoded_length=" << max_encoded_secret_size);
  }

  auto r_decoded = hex_decode(encoded_secret);
  if (r_decoded.is_error()) {
    r_decoded = base64url_decode(encoded_secret);
  }
  if (r_decoded.is_error()) {
    r_decoded = base64_decode(encoded_secret);
  }
  if (r_decoded.is_error()) {
    return Status::Error(400, PSLICE() << "Wrong proxy secret: decode=failed encoded_length=" << encoded_secret.size());
  }
  return from_binary(r_decoded.ok(), truncate_if_needed);
}

Result<ProxySecret> ProxySecret::from_binary(Slice raw_unchecked_secret, bool truncate_if_needed) {
  constexpr size_t kMaxSecretSize = get_max_proxy_secret_size();
  auto raw_length = raw_unchecked_secret.size();

  if (raw_length > kMaxSecretSize) {
    if (truncate_if_needed) {
      raw_unchecked_secret.truncate(kMaxSecretSize);
    } else {
      return Status::Error(
          400, PSLICE() << "Too long secret: raw_length=" << raw_length << " max_length=" << kMaxSecretSize);
    }
  }
  if (raw_unchecked_secret.size() == 16 ||
      (raw_unchecked_secret.size() == 17 && static_cast<unsigned char>(raw_unchecked_secret[0]) == 0xdd)) {
    return from_raw(raw_unchecked_secret);
  }
  if (raw_unchecked_secret.size() >= 18 && static_cast<unsigned char>(raw_unchecked_secret[0]) == 0xee) {
    auto tls_domain = raw_unchecked_secret.substr(17);
    auto *tls_domain_error = get_tls_emulation_domain_error(tls_domain);
    if (tls_domain_error != nullptr) {
      return Status::Error(400, PSLICE() << "Wrong proxy secret: tls_domain_error=" << tls_domain_error
                                         << " domain_length=" << tls_domain.size());
    }
    return from_raw(raw_unchecked_secret);
  }
  if (raw_unchecked_secret.size() < 16) {
    return Status::Error(400,
                         PSLICE() << "Wrong proxy secret: reason=too_short raw_length=" << raw_unchecked_secret.size());
  }
  auto marker = static_cast<unsigned int>(static_cast<unsigned char>(raw_unchecked_secret[0]));
  return Status::Error(400, PSLICE() << "Unsupported proxy secret: raw_length=" << raw_unchecked_secret.size()
                                     << " marker=0x" << format::as_hex(marker));
}

string ProxySecret::get_encoded_secret() const {
  if (emulate_tls()) {
    return base64url_encode(secret_);
  }
  return hex_encode(secret_);
}

}  // namespace mtproto
}  // namespace td
