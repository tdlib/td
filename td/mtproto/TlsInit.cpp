// SPDX-FileCopyrightText: Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: BSL-1.0 AND MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/mtproto/TlsInit.h"

#include "td/mtproto/stealth/TlsHelloBuilder.h"

#include "td/utils/common.h"
#include "td/utils/crypto.h"
#include "td/utils/Random.h"
#include "td/utils/Time.h"

#include <algorithm>

namespace td {
namespace mtproto {

namespace {

constexpr size_t kMaxTlsRecordBodyLength = (1u << 14) + 256u;

Status consume_tls_hello_response_records(ChainBufferReader *it, bool *is_complete) {
  *is_complete = false;
  bool seen_handshake = false;

  while (true) {
    if (it->size() < 5) {
      return Status::OK();
    }

    uint8 header[5];
    it->advance(5, MutableSlice(header, 5));
    const auto record_type = header[0];
    const auto record_version_major = header[1];
    const auto record_version_minor = header[2];
    const size_t record_length = (static_cast<size_t>(header[3]) << 8) + static_cast<size_t>(header[4]);

    if (record_version_major != 0x03 || record_version_minor != 0x03) {
      return Status::Error("First part of response to hello is invalid");
    }

    if (record_length > kMaxTlsRecordBodyLength) {
      return Status::Error("First part of response to hello is invalid");
    }

    if (it->size() < record_length) {
      return Status::OK();
    }

    if (!seen_handshake) {
      if (record_type != 0x16) {
        return Status::Error("First part of response to hello is invalid");
      }
      if (record_length == 0) {
        return Status::Error("First part of response to hello is invalid");
      }
      seen_handshake = true;
      it->advance(record_length);
      continue;
    }

    if (record_type == 0x14) {
      if (record_length != 1) {
        return Status::Error("First part of response to hello is invalid");
      }
      uint8 ccs_payload = 0;
      it->advance(1, MutableSlice(&ccs_payload, 1));
      if (ccs_payload != 0x01) {
        return Status::Error("First part of response to hello is invalid");
      }
      continue;
    }

    if (record_type == 0x17) {
      if (record_length == 0) {
        return Status::Error("First part of response to hello is invalid");
      }
      it->advance(record_length);
      *is_complete = true;
      return Status::OK();
    }

    return Status::Error("First part of response to hello is invalid");
  }
}

}  // namespace

void Grease::init(MutableSlice res) {
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

void TlsInit::send_hello() {
  hello_unix_time_ = static_cast<int32>(Time::now() + server_time_difference_);
  auto decision = stealth::get_runtime_ech_decision(username_, hello_unix_time_, route_hints_);
#if TD_DARWIN
  hello_uses_ech_ = decision.ech_mode == stealth::EchMode::Rfc9180Outer;
  auto hello = stealth::build_proxy_tls_client_hello(username_, password_, hello_unix_time_, route_hints_);
#else
  auto platform = stealth::default_runtime_platform_hints();
  auto profile = stealth::pick_runtime_profile(username_, hello_unix_time_, platform);
  hello_uses_ech_ = profile_spec(profile).allows_ech && decision.ech_mode == stealth::EchMode::Rfc9180Outer;
  auto hello = stealth::build_proxy_tls_client_hello_for_profile(
      username_, password_, hello_unix_time_, profile,
      hello_uses_ech_ ? stealth::EchMode::Rfc9180Outer : stealth::EchMode::Disabled);
#endif
  stealth::note_runtime_ech_decision(decision, hello_uses_ech_);
  hello_rand_ = hello.substr(11, 32);
  fd_.output_buffer().append(hello);
  state_ = State::WaitHelloResponse;
}

Status TlsInit::wait_hello_response() {
  auto it = fd_.input_buffer().clone();
  bool is_complete = false;
  auto status = consume_tls_hello_response_records(&it, &is_complete);
  if (status.is_error()) {
    if (hello_uses_ech_) {
      stealth::note_runtime_ech_failure(username_, hello_unix_time_);
    }
    return status;
  }
  if (!is_complete) {
    return Status::OK();
  }

  auto response = fd_.input_buffer().cut_head(it.begin().clone()).move_as_buffer_slice();
  if (response.size() < 43) {
    if (hello_uses_ech_) {
      stealth::note_runtime_ech_failure(username_, hello_unix_time_);
    }
    return Status::Error("First part of response to hello is invalid");
  }
  auto response_rand_slice = response.as_mutable_slice().substr(11, 32);
  auto response_rand = response_rand_slice.str();
  std::fill(response_rand_slice.begin(), response_rand_slice.end(), '\0');
  string hash_dest(32, '\0');
  hmac_sha256(password_, PSLICE() << hello_rand_ << response.as_slice(), hash_dest);
  if (!constant_time_equals(hash_dest, response_rand)) {
    if (hello_uses_ech_) {
      stealth::note_runtime_ech_failure(username_, hello_unix_time_);
    }
    return Status::Error("Response hash mismatch");
  }

  if (hello_uses_ech_) {
    stealth::note_runtime_ech_success(username_, hello_unix_time_);
  }

  if (!empty()) {
    stop();
  }
  return Status::OK();
}

Status TlsInit::loop_impl() {
  switch (state_) {
    case State::SendHello:
      send_hello();
      break;
    case State::WaitHelloResponse:
      TRY_STATUS(wait_hello_response());
      break;
  }
  return Status::OK();
}

}  // namespace mtproto
}  // namespace td
