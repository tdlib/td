// SPDX-FileCopyrightText: Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: BSL-1.0 AND MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/mtproto/TlsInit.h"

#include "td/mtproto/stealth/StealthRuntimeParams.h"
#include "td/mtproto/stealth/TlsHelloBuilder.h"

#include "td/net/ProxySetupError.h"

#include "td/utils/common.h"
#include "td/utils/crypto.h"
#include "td/utils/format.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/Random.h"
#include "td/utils/Time.h"

#include <algorithm>

namespace td {
namespace mtproto {

namespace {

constexpr size_t kMaxTlsRecordBodyLength = (1u << 14) + 256u;
constexpr size_t kTlsHelloMinEnvelopeLength = 43;
constexpr size_t kTlsHelloResponseRandomOffset = 11;
constexpr size_t kTlsHelloResponseRandomSize = 32;

Slice ech_mode_name(stealth::EchMode mode) {
  switch (mode) {
    case stealth::EchMode::Disabled:
      return Slice("disabled");
    case stealth::EchMode::Rfc9180Outer:
      return Slice("rfc9180_outer");
    default:
      return Slice("unknown");
  }
}

bool looks_like_http_response_prefix(const uint8 header[5]) {
  return header[0] == 'H' && header[1] == 'T' && header[2] == 'T' && header[3] == 'P' && header[4] == '/';
}

bool is_valid_tls_record_type(uint8 record_type) {
  return record_type == 0x14 || record_type == 0x15 || record_type == 0x16 || record_type == 0x17;
}

Slice tls_record_type_name(uint8 record_type) {
  switch (record_type) {
    case 0x14:
      return Slice("change_cipher_spec");
    case 0x15:
      return Slice("alert");
    case 0x16:
      return Slice("handshake");
    case 0x17:
      return Slice("application_data");
    default:
      return Slice("unknown");
  }
}

string tls_record_header_context(uint8 record_type, uint8 record_version_major, uint8 record_version_minor,
                                 size_t record_length) {
  char version_bytes[2] = {static_cast<char>(record_version_major), static_cast<char>(record_version_minor)};
  return PSTRING() << "record_type=" << tls_record_type_name(record_type)
                   << " record_type_code=" << format::as_hex(record_type) << " record_version=0x"
                   << hex_encode(Slice(version_bytes, 2)) << " record_length=" << record_length;
}

string tls_header_prefix_context(const uint8 header[5]) {
  char ascii_header[5];
  for (size_t i = 0; i < 5; i++) {
    auto byte = header[i];
    ascii_header[i] = byte >= 0x20 && byte <= 0x7e ? static_cast<char>(byte) : '.';
  }
  return PSTRING() << "header_ascii=" << Slice(ascii_header, 5)
                   << " header_hex=" << hex_encode(Slice(reinterpret_cast<const char *>(header), 5));
}

Status tls_hello_wrong_regime_error(Slice details) {
  return make_proxy_setup_error(ProxySetupErrorCode::TlsHelloWrongRegime,
                                PSLICE() << "TLS hello response is from a different protocol regime: " << details);
}

Status tls_hello_malformed_response_error(Slice reason) {
  return make_proxy_setup_error(ProxySetupErrorCode::TlsHelloMalformedResponse,
                                PSLICE() << "TLS hello response malformed: " << reason);
}

Status tls_hello_hash_mismatch_error(size_t response_bytes) {
  return make_proxy_setup_error(ProxySetupErrorCode::TlsHelloResponseHashMismatch,
                                PSLICE() << "Response hash mismatch: response_bytes=" << response_bytes
                                         << " random_offset=" << kTlsHelloResponseRandomOffset
                                         << " random_size=" << kTlsHelloResponseRandomSize);
}

// Maps a typed TLS-hello ProxySetupError into the conservative profile-quarantine
// vocabulary, mirroring the ConnectionRetryPolicy taxonomy without depending on
// the higher td/telegram layer. Only a malformed hello response is a wire-shape
// rejection; wrong-regime and response-hash-mismatch indicate a wrong protocol
// regime or proxy secret that no fingerprint change can repair, so they never
// quarantine. Anything else is treated as None (not eligible).
stealth::RuntimeProfileFailureSignal profile_failure_signal_for_status(const Status &status) {
  switch (static_cast<ProxySetupErrorCode>(status.code())) {
    case ProxySetupErrorCode::TlsHelloMalformedResponse:
      return stealth::RuntimeProfileFailureSignal::MalformedHelloResponse;
    case ProxySetupErrorCode::TlsHelloWrongRegime:
      return stealth::RuntimeProfileFailureSignal::WrongRegime;
    case ProxySetupErrorCode::TlsHelloResponseHashMismatch:
      return stealth::RuntimeProfileFailureSignal::ResponseHashMismatch;
    default:
      return stealth::RuntimeProfileFailureSignal::None;
  }
}

Status consume_tls_hello_response_records(ChainBufferReader *it, bool *is_complete) {
  *is_complete = false;
  bool seen_handshake = false;
  bool seen_change_cipher_spec = false;

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

    if (looks_like_http_response_prefix(header) || record_type == 0x05 || !is_valid_tls_record_type(record_type)) {
      return tls_hello_wrong_regime_error(tls_header_prefix_context(header));
    }

    if (record_version_major != 0x03 || record_version_minor != 0x03) {
      return tls_hello_malformed_response_error(
          PSLICE() << "record version must be 0x0303 ["
                   << tls_record_header_context(record_type, record_version_major, record_version_minor, record_length)
                   << ']');
    }

    if (record_length > kMaxTlsRecordBodyLength) {
      return tls_hello_malformed_response_error(
          PSLICE() << "record length exceeds TLS hello limit: record_length=" << record_length
                   << " max_allowed=" << kMaxTlsRecordBodyLength << " ["
                   << tls_record_header_context(record_type, record_version_major, record_version_minor, record_length)
                   << ']');
    }

    if (it->size() < record_length) {
      return Status::OK();
    }

    if (!seen_handshake) {
      if (record_type != 0x16) {
        return tls_hello_malformed_response_error(PSLICE()
                                                  << "first record is not handshake ["
                                                  << tls_record_header_context(record_type, record_version_major,
                                                                               record_version_minor, record_length)
                                                  << ']');
      }
      if (record_length == 0) {
        return tls_hello_malformed_response_error(PSLICE()
                                                  << "handshake record has zero length ["
                                                  << tls_record_header_context(record_type, record_version_major,
                                                                               record_version_minor, record_length)
                                                  << ']');
      }
      seen_handshake = true;
      it->advance(record_length);
      continue;
    }

    if (record_type == 0x14) {
      if (seen_change_cipher_spec) {
        return tls_hello_malformed_response_error(PSLICE()
                                                  << "duplicate change_cipher_spec record ["
                                                  << tls_record_header_context(record_type, record_version_major,
                                                                               record_version_minor, record_length)
                                                  << ']');
      }
      if (record_length != 1) {
        return tls_hello_malformed_response_error(PSLICE()
                                                  << "change_cipher_spec record length must be 1 ["
                                                  << tls_record_header_context(record_type, record_version_major,
                                                                               record_version_minor, record_length)
                                                  << ']');
      }
      uint8 ccs_payload = 0;
      it->advance(1, MutableSlice(&ccs_payload, 1));
      if (ccs_payload != 0x01) {
        return tls_hello_malformed_response_error(PSLICE()
                                                  << "change_cipher_spec payload must be 0x01 ["
                                                  << tls_record_header_context(record_type, record_version_major,
                                                                               record_version_minor, record_length)
                                                  << " payload=" << format::as_hex(ccs_payload) << ']');
      }
      seen_change_cipher_spec = true;
      continue;
    }

    if (record_type == 0x17) {
      if (record_length == 0) {
        return tls_hello_malformed_response_error(PSLICE()
                                                  << "application_data record has zero length ["
                                                  << tls_record_header_context(record_type, record_version_major,
                                                                               record_version_minor, record_length)
                                                  << ']');
      }
      it->advance(record_length);
      *is_complete = true;
      return Status::OK();
    }

    return tls_hello_malformed_response_error(
        PSLICE() << "unexpected TLS record type after handshake ["
                 << tls_record_header_context(record_type, record_version_major, record_version_minor, record_length)
                 << ']');
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

bool TlsInit::record_ech_failure_once() {
  if (!hello_uses_ech_ || hello_failure_recorded_) {
    return false;
  }
  stealth::note_runtime_ech_failure(username_, hello_unix_time_);
  hello_failure_recorded_ = true;
  return true;
}

void TlsInit::record_profile_failure_once(stealth::RuntimeProfileFailureSignal signal) {
  if (hello_profile_failure_recorded_) {
    return;
  }
  // Only burn the once-guard on a quarantine-eligible signal, so an earlier
  // ineligible rejection (e.g. wrong regime) does not suppress a later genuine
  // wire-shape failure in the same attempt.
  if (!stealth::runtime_profile_failure_signal_is_quarantine_eligible(signal)) {
    return;
  }
  stealth::note_runtime_profile_failure(username_, {hello_profile_, hello_uses_ech_}, signal);
  hello_profile_failure_recorded_ = true;
}

void TlsInit::send_hello() {
  hello_unix_time_ = static_cast<int32>(Time::now() + server_time_difference_);

  // One profile for this whole connection attempt. If the connection path already
  // selected a full wire-variant snapshot (the single-selection handoff), use it
  // verbatim so the emitted ClientHello matches the transport-shaping config and
  // one attempt carries one immutable wire variant. Otherwise self-select now
  // (tests / legacy callers). Either way the chosen (profile + hello_uses_ech) is
  // the exact unit failure/success accounting and quarantine key on.
  auto platform = stealth::default_runtime_platform_hints();
  stealth::RuntimeProfileSelectionDecision selection;
  if (preselected_runtime_profile_) {
    selection = preselected_runtime_profile_.value();
  } else {
    selection = stealth::select_runtime_profile_for_attempt(username_, hello_unix_time_, platform, route_hints_);
  }
  stealth::RuntimeEchDecision decision;
  decision.ech_mode = selection.ech_mode;
  decision.disabled_by_route = selection.ech_disabled_by_route;
  decision.disabled_by_circuit_breaker = selection.ech_disabled_by_circuit_breaker;
  decision.reenabled_after_ttl = selection.ech_reenabled_after_ttl;

  hello_ech_mode_name_ = ech_mode_name(decision.ech_mode).str();
  hello_ech_disabled_by_route_ = decision.disabled_by_route;
  hello_ech_disabled_by_circuit_breaker_ = decision.disabled_by_circuit_breaker;
  hello_ech_reenabled_after_ttl_ = decision.reenabled_after_ttl;
  hello_profile_ = selection.profile;
  hello_profile_name_ = stealth::profile_spec(selection.profile).name.str();
  hello_profile_allows_ech_ = stealth::profile_spec(selection.profile).allows_ech;
  hello_uses_ech_ = selection.hello_uses_ech;
  auto expected_hello_uses_ech = hello_profile_allows_ech_ && decision.ech_mode == stealth::EchMode::Rfc9180Outer;
  if (hello_uses_ech_ != expected_hello_uses_ech) {
    LOG(ERROR) << "TlsInit runtime profile snapshot inconsistent " << tag("destination", username_)
               << tag("profile", hello_profile_name_) << tag("ech_mode", hello_ech_mode_name_)
               << tag("hello_uses_ech", hello_uses_ech_) << tag("expected_hello_uses_ech", expected_hello_uses_ech);
    on_error(make_proxy_setup_error(
        ProxySetupErrorCode::TlsHelloMalformedResponse,
        PSLICE() << "runtime profile snapshot inconsistent with final ECH gate: profile=" << hello_profile_name_
                 << " ech_mode=" << hello_ech_mode_name_ << " hello_uses_ech=" << hello_uses_ech_
                 << " expected_hello_uses_ech=" << expected_hello_uses_ech));
    return;
  }
  hello_profile_rotation_avoided_quarantined_ = selection.avoided_quarantined_profile;
  hello_profile_rotation_quarantined_candidates_ = selection.quarantined_candidate_count;
  hello_profile_rotation_enabled_ = stealth::get_runtime_stealth_params_snapshot().profile_rotation.enabled;
  hello_failure_recorded_ = false;
  hello_profile_failure_recorded_ = false;
  auto hello_result = stealth::try_build_proxy_tls_client_hello_for_profile(
      username_, password_, hello_unix_time_, selection.profile,
      hello_uses_ech_ ? stealth::EchMode::Rfc9180Outer : stealth::EchMode::Disabled);
  if (hello_result.is_error()) {
    auto error = hello_result.move_as_error();
    LOG(ERROR) << "TlsInit hello generation failed " << tag("destination", username_)
               << tag("profile", hello_profile_name_) << tag("status_code", error.code())
               << tag("status_message", error.public_message());
    on_error(make_proxy_setup_error(ProxySetupErrorCode::TlsHelloMalformedResponse,
                                    PSLICE() << "TLS hello generation failed: " << error.public_message()));
    return;
  }

  auto hello = hello_result.move_as_ok();
  if (hello.size() < kTlsHelloResponseRandomOffset + kTlsHelloResponseRandomSize) {
    LOG(ERROR) << "TlsInit hello generation failed " << tag("destination", username_)
               << tag("profile", hello_profile_name_) << tag("hello_bytes", hello.size())
               << tag("min_expected", kTlsHelloResponseRandomOffset + kTlsHelloResponseRandomSize);
    on_error(make_proxy_setup_error(
        ProxySetupErrorCode::TlsHelloMalformedResponse,
        PSLICE() << "generated TLS hello is shorter than random extraction envelope: hello_bytes=" << hello.size()
                 << " min_expected=" << (kTlsHelloResponseRandomOffset + kTlsHelloResponseRandomSize)));
    return;
  }

  LOG(DEBUG) << "TlsInit hello prepared " << tag("destination", username_) << tag("route_known", route_hints_.is_known)
             << tag("route_ru", route_hints_.is_ru) << tag("ech_mode", hello_ech_mode_name_)
             << tag("ech_enabled", hello_uses_ech_) << tag("profile", hello_profile_name_)
             << tag("profile_allows_ech", hello_profile_allows_ech_)
             << tag("ech_disabled_by_route", hello_ech_disabled_by_route_)
             << tag("ech_disabled_by_circuit_breaker", hello_ech_disabled_by_circuit_breaker_)
             << tag("ech_reenabled_after_ttl", hello_ech_reenabled_after_ttl_)
             << tag("hello_unix_time", hello_unix_time_)
             << tag("profile_rotation_enabled", hello_profile_rotation_enabled_)
             << tag("profile_rotation_avoided_quarantined", hello_profile_rotation_avoided_quarantined_)
             << tag("profile_rotation_quarantined_candidates", hello_profile_rotation_quarantined_candidates_);

  stealth::note_runtime_ech_decision(decision, hello_uses_ech_);
  hello_rand_ = hello.substr(kTlsHelloResponseRandomOffset, kTlsHelloResponseRandomSize);
  fd_.output_buffer().append(hello);
  state_ = State::WaitHelloResponse;
}

Status TlsInit::wait_hello_response() {
  auto log_hello_rejection = [&](Slice failure_stage, const Status &status, bool recorded_ech_failure,
                                 size_t buffered_bytes, size_t parsed_bytes, bool parse_complete) {
    LOG(WARNING) << "TlsInit hello response rejected " << tag("destination", username_)
                 << tag("route_known", route_hints_.is_known) << tag("route_ru", route_hints_.is_ru)
                 << tag("ech_mode", hello_ech_mode_name_) << tag("ech_enabled", hello_uses_ech_)
                 << tag("profile", hello_profile_name_) << tag("profile_allows_ech", hello_profile_allows_ech_)
                 << tag("ech_disabled_by_route", hello_ech_disabled_by_route_)
                 << tag("ech_disabled_by_circuit_breaker", hello_ech_disabled_by_circuit_breaker_)
                 << tag("ech_reenabled_after_ttl", hello_ech_reenabled_after_ttl_)
                 << tag("hello_unix_time", hello_unix_time_) << tag("failure_stage", failure_stage)
                 << tag("status_code", status.code()) << tag("status_message", status.public_message())
                 << tag("recorded_ech_failure", recorded_ech_failure)
                 << tag("profile_rotation_enabled", hello_profile_rotation_enabled_)
                 << tag("profile_rotation_avoided_quarantined", hello_profile_rotation_avoided_quarantined_)
                 << tag("profile_rotation_quarantined_candidates", hello_profile_rotation_quarantined_candidates_)
                 << tag("profile_rotation_failure_recorded", hello_profile_failure_recorded_)
                 << tag("buffered_bytes", buffered_bytes) << tag("parsed_bytes", parsed_bytes)
                 << tag("parse_complete", parse_complete);
  };

  auto buffered_bytes = fd_.input_buffer().size();
  auto it = fd_.input_buffer().clone();
  bool is_complete = false;
  auto status = consume_tls_hello_response_records(&it, &is_complete);
  auto parsed_bytes = buffered_bytes - it.size();
  if (status.is_error()) {
    bool recorded_ech_failure = record_ech_failure_once();
    // A wrong-regime parse rejection classifies as not-eligible and is a no-op for
    // quarantine; only a malformed-hello parse rejection counts as a wire-shape
    // rejection against this profile variant.
    record_profile_failure_once(profile_failure_signal_for_status(status));
    log_hello_rejection("record_parse", status, recorded_ech_failure, buffered_bytes, parsed_bytes, false);
    return status;
  }
  if (!is_complete) {
    return Status::OK();
  }

  auto response = fd_.input_buffer().cut_head(it.begin().clone()).move_as_buffer_slice();
  if (response.size() < kTlsHelloMinEnvelopeLength) {
    bool recorded_ech_failure = record_ech_failure_once();
    auto error = tls_hello_malformed_response_error(PSLICE() << "response is shorter than minimal TLS hello envelope: "
                                                             << "response_bytes=" << response.size()
                                                             << " min_expected=" << kTlsHelloMinEnvelopeLength);
    record_profile_failure_once(profile_failure_signal_for_status(error));
    log_hello_rejection("response_too_short", error, recorded_ech_failure, response.size(), response.size(), true);
    return error;
  }
  auto response_rand_slice =
      response.as_mutable_slice().substr(kTlsHelloResponseRandomOffset, kTlsHelloResponseRandomSize);
  auto response_rand = response_rand_slice.str();
  std::fill(response_rand_slice.begin(), response_rand_slice.end(), '\0');
  string hash_dest(32, '\0');
  hmac_sha256(password_, PSLICE() << hello_rand_ << response.as_slice(), hash_dest);
  if (!constant_time_equals(hash_dest, response_rand)) {
    bool recorded_ech_failure = record_ech_failure_once();
    auto error = tls_hello_hash_mismatch_error(response.size());
    // Response-hash mismatch points at a wrong proxy secret / TLS-init contract,
    // not a blocked fingerprint: classified not-eligible, so it never quarantines
    // the profile (false-positive resistance).
    record_profile_failure_once(profile_failure_signal_for_status(error));
    log_hello_rejection("response_hash", error, recorded_ech_failure, response.size(), response.size(), true);
    return error;
  }

  if (hello_uses_ech_) {
    stealth::note_runtime_ech_success(username_, hello_unix_time_);
  }
  // A verified hello response clears any quarantine for this exact wire variant.
  stealth::note_runtime_profile_success(username_, {hello_profile_, hello_uses_ech_});

  LOG(DEBUG) << "TlsInit hello response accepted " << tag("destination", username_)
             << tag("route_known", route_hints_.is_known) << tag("route_ru", route_hints_.is_ru)
             << tag("ech_mode", hello_ech_mode_name_) << tag("ech_enabled", hello_uses_ech_)
             << tag("profile", hello_profile_name_) << tag("profile_allows_ech", hello_profile_allows_ech_)
             << tag("ech_disabled_by_route", hello_ech_disabled_by_route_)
             << tag("ech_disabled_by_circuit_breaker", hello_ech_disabled_by_circuit_breaker_)
             << tag("ech_reenabled_after_ttl", hello_ech_reenabled_after_ttl_)
             << tag("hello_unix_time", hello_unix_time_) << tag("response_bytes", response.size())
             << tag("profile_rotation_enabled", hello_profile_rotation_enabled_)
             << tag("profile_rotation_avoided_quarantined", hello_profile_rotation_avoided_quarantined_)
             << tag("profile_rotation_quarantined_candidates", hello_profile_rotation_quarantined_candidates_);

  if (!empty()) {
    stop();
  }
  return Status::OK();
}

void TlsInit::on_proxy_setup_error(const Status &status) {
  if (!status.is_error() || state_ != State::WaitHelloResponse) {
    return;
  }

  // Transport/setup rejection after the hello was sent and while waiting for the
  // response is the classic post-ClientHello block signal, so it is a
  // quarantine-eligible wire-shape rejection for this profile variant (recorded
  // at most once per attempt). This is independent of the ECH circuit breaker.
  record_profile_failure_once(stealth::RuntimeProfileFailureSignal::TransportRejectionAfterHello);

  auto recorded_ech_failure = record_ech_failure_once();
  if (!recorded_ech_failure) {
    return;
  }

  LOG(WARNING) << "TlsInit hello transport/setup rejection recorded for ECH circuit breaker "
               << tag("destination", username_) << tag("route_known", route_hints_.is_known)
               << tag("route_ru", route_hints_.is_ru) << tag("ech_mode", hello_ech_mode_name_)
               << tag("ech_enabled", hello_uses_ech_) << tag("profile", hello_profile_name_)
               << tag("profile_allows_ech", hello_profile_allows_ech_)
               << tag("ech_disabled_by_route", hello_ech_disabled_by_route_)
               << tag("ech_disabled_by_circuit_breaker", hello_ech_disabled_by_circuit_breaker_)
               << tag("ech_reenabled_after_ttl", hello_ech_reenabled_after_ttl_)
               << tag("hello_unix_time", hello_unix_time_) << tag("status_code", status.code())
               << tag("status_message", status.public_message());
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
