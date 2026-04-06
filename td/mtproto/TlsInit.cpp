//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
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
  auto hello = stealth::build_default_tls_client_hello(username_, password_, hello_unix_time_, route_hints_);
#else
  auto platform = stealth::default_runtime_platform_hints();
  auto profile = stealth::pick_runtime_profile(username_, hello_unix_time_, platform);
  hello_uses_ech_ = profile_spec(profile).allows_ech && decision.ech_mode == stealth::EchMode::Rfc9180Outer;
  auto hello = stealth::build_tls_client_hello_for_profile(
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
  for (auto prefix : {Slice("\x16\x03\x03"), Slice("\x14\x03\x03\x00\x01\x01\x17\x03\x03")}) {
    if (it.size() < prefix.size() + 2) {
      return Status::OK();
    }

    string response_prefix(prefix.size(), '\0');
    it.advance(prefix.size(), response_prefix);
    if (prefix != response_prefix) {
      if (hello_uses_ech_) {
        stealth::note_runtime_ech_failure(username_, hello_unix_time_);
      }
      return Status::Error("First part of response to hello is invalid");
    }

    uint8 tmp[2];
    it.advance(2, MutableSlice(tmp, 2));
    size_t skip_size = (tmp[0] << 8) + tmp[1];
    if (it.size() < skip_size) {
      return Status::OK();
    }
    it.advance(skip_size);
  }

  auto response = fd_.input_buffer().cut_head(it.begin().clone()).move_as_buffer_slice();
  auto response_rand_slice = response.as_mutable_slice().substr(11, 32);
  auto response_rand = response_rand_slice.str();
  std::fill(response_rand_slice.begin(), response_rand_slice.end(), '\0');
  string hash_dest(32, '\0');
  hmac_sha256(password_, PSLICE() << hello_rand_ << response.as_slice(), hash_dest);
  if (hash_dest != response_rand) {
    if (hello_uses_ech_) {
      stealth::note_runtime_ech_failure(username_, hello_unix_time_);
    }
    return Status::Error("Response hash mismatch");
  }

  if (hello_uses_ech_) {
    stealth::note_runtime_ech_success(username_, hello_unix_time_);
  }

  stop();
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
