// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/mtproto/stealth/StealthParamsLoader.h"

#include "td/utils/JsonBuilder.h"
#if TD_PORT_POSIX
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#else
#include "td/utils/port/FileFd.h"
#endif

#include <unordered_set>

namespace td {
namespace mtproto {
namespace stealth {
namespace {

bool is_missing_config_path(const string &path) {
#if TD_PORT_POSIX
  struct ::stat st;
  if (::lstat(path.c_str(), &st) != 0) {
    return errno == ENOENT || errno == ENOTDIR;
  }
#else
  (void)path;
#endif
  return false;
}

Status validate_secure_parent_directory(const string &path) {
#if TD_PORT_POSIX
  auto pos = path.rfind('/');
  string parent = pos == string::npos ? string(".") : (pos == 0 ? string("/") : path.substr(0, pos));

  struct ::stat st;
  if (::lstat(parent.c_str(), &st) != 0) {
    return Status::PosixError(errno, "Failed to stat stealth params parent directory");
  }
  if (!S_ISDIR(st.st_mode)) {
    return Status::Error("Stealth params parent path must be a directory");
  }
  if (st.st_uid != ::geteuid()) {
    return Status::Error("Stealth params parent directory must be owned by the current user");
  }
  if ((st.st_mode & S_IWGRP) != 0 || (st.st_mode & S_IWOTH) != 0) {
    return Status::Error("Stealth params parent directory must not be writable by group or others");
  }
#else
  (void)path;
#endif
  return Status::OK();
}

Status ensure_exact_object_shape(Slice scope, const JsonObject &object, std::initializer_list<Slice> allowed_fields) {
  std::unordered_set<string> allowed;
  allowed.reserve(allowed_fields.size());
  for (auto field : allowed_fields) {
    allowed.emplace(field.str());
  }

  std::unordered_set<string> seen;
  seen.reserve(object.field_count());
  bool has_unknown = false;
  string unknown_field;
  string duplicate_field;
  object.foreach([&](Slice name, const JsonValue &) {
    auto name_str = name.str();
    if (!allowed.count(name_str)) {
      has_unknown = true;
      unknown_field = std::move(name_str);
      return;
    }
    if (!seen.emplace(name_str).second && duplicate_field.empty()) {
      duplicate_field = std::move(name_str);
    }
  });

  if (has_unknown) {
    return Status::Error(scope.str() + " has unknown field \"" + unknown_field + "\"");
  }
  if (!duplicate_field.empty()) {
    return Status::Error(scope.str() + " has duplicate field \"" + duplicate_field + "\"");
  }
  return Status::OK();
}

Result<uint8> parse_weight_field(const JsonObject &object, Slice field_name) {
  TRY_RESULT(value, object.get_required_int_field(field_name));
  if (value < 0 || value > 100) {
    return Status::Error("profile_weights." + field_name.str() + " must be within [0, 100]");
  }
  return static_cast<uint8>(value);
}

Result<DeviceClass> parse_device_class(Slice value) {
  if (value == "desktop") {
    return DeviceClass::Desktop;
  }
  if (value == "mobile") {
    return DeviceClass::Mobile;
  }
  return Status::Error("unsupported platform_hints.device_class \"" + value.str() + "\"");
}

Result<MobileOs> parse_mobile_os(Slice value) {
  if (value == "none") {
    return MobileOs::None;
  }
  if (value == "ios") {
    return MobileOs::IOS;
  }
  if (value == "android") {
    return MobileOs::Android;
  }
  return Status::Error("unsupported platform_hints.mobile_os \"" + value.str() + "\"");
}

Result<DesktopOs> parse_desktop_os(Slice value) {
  if (value == "unknown") {
    return DesktopOs::Unknown;
  }
  if (value == "darwin") {
    return DesktopOs::Darwin;
  }
  if (value == "windows") {
    return DesktopOs::Windows;
  }
  if (value == "linux") {
    return DesktopOs::Linux;
  }
  return Status::Error("unsupported platform_hints.desktop_os \"" + value.str() + "\"");
}

Result<EchMode> parse_ech_mode(Slice value) {
  if (value == "disabled") {
    return EchMode::Disabled;
  }
  if (value == "rfc9180_outer" || value == "grease_draft17") {
    return EchMode::Rfc9180Outer;
  }
  return Status::Error("unsupported ech_mode \"" + value.str() + "\"");
}

Result<RuntimeActivePolicy> parse_active_policy(Slice value) {
  if (value == "unknown") {
    return RuntimeActivePolicy::Unknown;
  }
  if (value == "ru_egress") {
    return RuntimeActivePolicy::RuEgress;
  }
  if (value == "non_ru_egress") {
    return RuntimeActivePolicy::NonRuEgress;
  }
  return Status::Error("unsupported active_policy \"" + value.str() + "\"");
}

Result<IptParams> parse_ipt_params(JsonValue value) {
  if (value.type() != JsonValue::Type::Object) {
    return Status::Error("ipt must be an object");
  }
  auto &object = value.get_object();
  TRY_STATUS(ensure_exact_object_shape(
      "ipt", object,
      {Slice("burst_mu_ms"), Slice("burst_sigma"), Slice("burst_max_ms"), Slice("idle_alpha"), Slice("idle_scale_ms"),
       Slice("idle_max_ms"), Slice("p_burst_stay"), Slice("p_idle_to_burst")}));

  IptParams params;
  TRY_RESULT(burst_mu_ms, object.get_required_double_field("burst_mu_ms"));
  TRY_RESULT(burst_sigma, object.get_required_double_field("burst_sigma"));
  TRY_RESULT(burst_max_ms, object.get_required_double_field("burst_max_ms"));
  TRY_RESULT(idle_alpha, object.get_required_double_field("idle_alpha"));
  TRY_RESULT(idle_scale_ms, object.get_required_double_field("idle_scale_ms"));
  TRY_RESULT(idle_max_ms, object.get_required_double_field("idle_max_ms"));
  TRY_RESULT(p_burst_stay, object.get_required_double_field("p_burst_stay"));
  TRY_RESULT(p_idle_to_burst, object.get_required_double_field("p_idle_to_burst"));
  params.burst_mu_ms = burst_mu_ms;
  params.burst_sigma = burst_sigma;
  params.burst_max_ms = burst_max_ms;
  params.idle_alpha = idle_alpha;
  params.idle_scale_ms = idle_scale_ms;
  params.idle_max_ms = idle_max_ms;
  params.p_burst_stay = p_burst_stay;
  params.p_idle_to_burst = p_idle_to_burst;
  return params;
}

Result<RecordSizeBin> parse_record_size_bin(JsonValue value) {
  if (value.type() != JsonValue::Type::Object) {
    return Status::Error("drs bins entries must be objects");
  }
  auto &object = value.get_object();
  TRY_STATUS(ensure_exact_object_shape("drs bins entry", object, {Slice("lo"), Slice("hi"), Slice("weight")}));

  RecordSizeBin bin;
  TRY_RESULT(lo, object.get_required_int_field("lo"));
  TRY_RESULT(hi, object.get_required_int_field("hi"));
  TRY_RESULT(weight, object.get_required_int_field("weight"));
  if (weight < 0 || weight > std::numeric_limits<uint16>::max()) {
    return Status::Error("drs bins entry weight must be within [0, 65535]");
  }
  bin.lo = lo;
  bin.hi = hi;
  bin.weight = static_cast<uint16>(weight);
  return bin;
}

Result<DrsPhaseModel> parse_drs_phase_model(Slice scope, JsonValue value) {
  if (value.type() != JsonValue::Type::Object) {
    return Status::Error(scope.str() + " must be an object");
  }
  auto &object = value.get_object();
  TRY_STATUS(ensure_exact_object_shape(scope, object, {Slice("bins"), Slice("max_repeat_run"), Slice("local_jitter")}));

  TRY_RESULT(bins_value, object.extract_required_field("bins", JsonValue::Type::Array));
  DrsPhaseModel model;
  for (auto &bin_value : bins_value.get_array()) {
    TRY_RESULT(bin, parse_record_size_bin(std::move(bin_value)));
    model.bins.push_back(bin);
  }
  TRY_RESULT(max_repeat_run, object.get_required_int_field("max_repeat_run"));
  TRY_RESULT(local_jitter, object.get_required_int_field("local_jitter"));
  model.max_repeat_run = max_repeat_run;
  model.local_jitter = local_jitter;
  return model;
}

Result<DrsPolicy> parse_drs_policy(JsonValue value) {
  if (value.type() != JsonValue::Type::Object) {
    return Status::Error("drs must be an object");
  }
  auto &object = value.get_object();
  TRY_STATUS(
      ensure_exact_object_shape("drs", object,
                                {Slice("slow_start"), Slice("congestion_open"), Slice("steady_state"),
                                 Slice("slow_start_records"), Slice("congestion_bytes"), Slice("idle_reset_ms_min"),
                                 Slice("idle_reset_ms_max"), Slice("min_payload_cap"), Slice("max_payload_cap")}));

  DrsPolicy policy;
  TRY_RESULT(slow_start_value, object.extract_required_field("slow_start", JsonValue::Type::Object));
  TRY_RESULT(congestion_open_value, object.extract_required_field("congestion_open", JsonValue::Type::Object));
  TRY_RESULT(steady_state_value, object.extract_required_field("steady_state", JsonValue::Type::Object));
  TRY_RESULT(slow_start, parse_drs_phase_model("drs.slow_start", std::move(slow_start_value)));
  TRY_RESULT(congestion_open, parse_drs_phase_model("drs.congestion_open", std::move(congestion_open_value)));
  TRY_RESULT(steady_state, parse_drs_phase_model("drs.steady_state", std::move(steady_state_value)));
  TRY_RESULT(slow_start_records, object.get_required_int_field("slow_start_records"));
  TRY_RESULT(congestion_bytes, object.get_required_int_field("congestion_bytes"));
  TRY_RESULT(idle_reset_ms_min, object.get_required_int_field("idle_reset_ms_min"));
  TRY_RESULT(idle_reset_ms_max, object.get_required_int_field("idle_reset_ms_max"));
  TRY_RESULT(min_payload_cap, object.get_required_int_field("min_payload_cap"));
  TRY_RESULT(max_payload_cap, object.get_required_int_field("max_payload_cap"));
  policy.slow_start = slow_start;
  policy.congestion_open = congestion_open;
  policy.steady_state = steady_state;
  policy.slow_start_records = slow_start_records;
  policy.congestion_bytes = congestion_bytes;
  policy.idle_reset_ms_min = idle_reset_ms_min;
  policy.idle_reset_ms_max = idle_reset_ms_max;
  policy.min_payload_cap = min_payload_cap;
  policy.max_payload_cap = max_payload_cap;
  return policy;
}

Result<RuntimeRoutePolicyEntry> parse_route_entry(Slice scope, JsonValue value) {
  if (value.type() != JsonValue::Type::Object) {
    return Status::Error(scope.str() + " must be an object");
  }
  auto &object = value.get_object();
  TRY_STATUS(ensure_exact_object_shape(scope, object, {Slice("ech_mode"), Slice("allow_quic")}));

  RuntimeRoutePolicyEntry entry;
  TRY_RESULT(ech_mode_name, object.get_required_string_field("ech_mode"));
  TRY_RESULT(ech_mode, parse_ech_mode(ech_mode_name));
  entry.ech_mode = ech_mode;
  TRY_RESULT(allow_quic, object.get_required_bool_field("allow_quic"));
  entry.allow_quic = allow_quic;
  return entry;
}

Result<RuntimeDesktopProfileWeights> parse_desktop_profile_weights(Slice scope, JsonValue value) {
  if (value.type() != JsonValue::Type::Object) {
    return Status::Error(scope.str() + " must be an object");
  }
  auto &object = value.get_object();
  TRY_STATUS(ensure_exact_object_shape(
      scope, object,
      {Slice("Chrome133"), Slice("Chrome131"), Slice("Chrome120"), Slice("Firefox148"), Slice("Safari26_3")}));

  RuntimeDesktopProfileWeights weights;
  TRY_RESULT(chrome133, parse_weight_field(object, "Chrome133"));
  TRY_RESULT(chrome131, parse_weight_field(object, "Chrome131"));
  TRY_RESULT(chrome120, parse_weight_field(object, "Chrome120"));
  TRY_RESULT(firefox148, parse_weight_field(object, "Firefox148"));
  TRY_RESULT(safari26_3, parse_weight_field(object, "Safari26_3"));
  weights.chrome133 = chrome133;
  weights.chrome131 = chrome131;
  weights.chrome120 = chrome120;
  weights.firefox148 = firefox148;
  weights.safari26_3 = safari26_3;
  return weights;
}

Result<RuntimeMobileProfileWeights> parse_mobile_profile_weights(Slice scope, JsonValue value) {
  if (value.type() != JsonValue::Type::Object) {
    return Status::Error(scope.str() + " must be an object");
  }
  auto &object = value.get_object();
  TRY_STATUS(ensure_exact_object_shape(scope, object, {Slice("IOS14"), Slice("Android11_OkHttp")}));

  RuntimeMobileProfileWeights weights;
  TRY_RESULT(ios14, parse_weight_field(object, "IOS14"));
  TRY_RESULT(android11_okhttp, parse_weight_field(object, "Android11_OkHttp"));
  weights.ios14 = ios14;
  weights.android11_okhttp = android11_okhttp;
  return weights;
}

ProfileWeights flatten_profile_selection(const RuntimeProfileSelectionPolicy &policy,
                                         const RuntimePlatformHints &platform) {
  ProfileWeights weights;
  const auto *desktop_weights = &policy.desktop_non_darwin;
  if (platform.device_class == DeviceClass::Desktop && platform.desktop_os == DesktopOs::Darwin) {
    desktop_weights = &policy.desktop_darwin;
  }
  weights.chrome133 = desktop_weights->chrome133;
  weights.chrome131 = desktop_weights->chrome131;
  weights.chrome120 = desktop_weights->chrome120;
  weights.firefox148 = desktop_weights->firefox148;
  weights.safari26_3 = desktop_weights->safari26_3;
  weights.ios14 = policy.mobile.ios14;
  weights.android11_okhttp = policy.mobile.android11_okhttp;
  return weights;
}

Result<ProfileWeights> parse_flat_profile_weights(JsonValue value) {
  if (value.type() != JsonValue::Type::Object) {
    return Status::Error("profile_weights must be an object");
  }
  auto &object = value.get_object();
  TRY_STATUS(ensure_exact_object_shape("profile_weights", object,
                                       {Slice("chrome133"), Slice("chrome131"), Slice("chrome120"), Slice("firefox148"),
                                        Slice("safari26_3"), Slice("ios14"), Slice("android11_okhttp")}));

  ProfileWeights weights;
  TRY_RESULT(chrome133, parse_weight_field(object, "chrome133"));
  TRY_RESULT(chrome131, parse_weight_field(object, "chrome131"));
  TRY_RESULT(chrome120, parse_weight_field(object, "chrome120"));
  TRY_RESULT(firefox148, parse_weight_field(object, "firefox148"));
  TRY_RESULT(safari26_3, parse_weight_field(object, "safari26_3"));
  TRY_RESULT(ios14, parse_weight_field(object, "ios14"));
  TRY_RESULT(android11_okhttp, parse_weight_field(object, "android11_okhttp"));
  weights.chrome133 = chrome133;
  weights.chrome131 = chrome131;
  weights.chrome120 = chrome120;
  weights.firefox148 = firefox148;
  weights.safari26_3 = safari26_3;
  weights.ios14 = ios14;
  weights.android11_okhttp = android11_okhttp;
  return weights;
}

Result<ProfileWeights> parse_profile_weights(JsonValue value, const RuntimePlatformHints &platform,
                                             RuntimeProfileSelectionPolicy *selection_policy = nullptr) {
  if (value.type() != JsonValue::Type::Object) {
    return Status::Error("profile_weights must be an object");
  }
  auto &object = value.get_object();

  if (object.has_field("allow_cross_class_rotation") || object.has_field("desktop_darwin") ||
      object.has_field("desktop_non_darwin") || object.has_field("mobile")) {
    TRY_STATUS(ensure_exact_object_shape(
        "profile_weights", object,
        {Slice("allow_cross_class_rotation"), Slice("desktop_darwin"), Slice("desktop_non_darwin"), Slice("mobile")}));

    RuntimeProfileSelectionPolicy parsed_policy;
    TRY_RESULT(allow_cross_class_rotation, object.get_required_bool_field("allow_cross_class_rotation"));
    parsed_policy.allow_cross_class_rotation = allow_cross_class_rotation;

    TRY_RESULT(desktop_darwin_value, object.extract_required_field("desktop_darwin", JsonValue::Type::Object));
    TRY_RESULT(desktop_non_darwin_value, object.extract_required_field("desktop_non_darwin", JsonValue::Type::Object));
    TRY_RESULT(mobile_value, object.extract_required_field("mobile", JsonValue::Type::Object));
    TRY_RESULT(desktop_darwin,
               parse_desktop_profile_weights("profile_weights.desktop_darwin", std::move(desktop_darwin_value)));
    TRY_RESULT(desktop_non_darwin, parse_desktop_profile_weights("profile_weights.desktop_non_darwin",
                                                                 std::move(desktop_non_darwin_value)));
    TRY_RESULT(mobile, parse_mobile_profile_weights("profile_weights.mobile", std::move(mobile_value)));
    parsed_policy.desktop_darwin = desktop_darwin;
    parsed_policy.desktop_non_darwin = desktop_non_darwin;
    parsed_policy.mobile = mobile;
    if (selection_policy != nullptr) {
      *selection_policy = parsed_policy;
    }
    return flatten_profile_selection(parsed_policy, platform);
  }

  return parse_flat_profile_weights(std::move(value));
}

Result<RuntimePlatformHints> parse_platform_hints(JsonValue value) {
  if (value.type() != JsonValue::Type::Object) {
    return Status::Error("platform_hints must be an object");
  }
  auto &object = value.get_object();
  TRY_STATUS(ensure_exact_object_shape("platform_hints", object,
                                       {Slice("device_class"), Slice("mobile_os"), Slice("desktop_os")}));

  RuntimePlatformHints platform;
  TRY_RESULT(device_class_name, object.get_required_string_field("device_class"));
  TRY_RESULT(mobile_os_name, object.get_required_string_field("mobile_os"));
  TRY_RESULT(desktop_os_name, object.get_required_string_field("desktop_os"));
  TRY_RESULT(device_class, parse_device_class(device_class_name));
  TRY_RESULT(mobile_os, parse_mobile_os(mobile_os_name));
  TRY_RESULT(desktop_os, parse_desktop_os(desktop_os_name));
  platform.device_class = device_class;
  platform.mobile_os = mobile_os;
  platform.desktop_os = desktop_os;
  return platform;
}

Result<RuntimeFlowBehaviorPolicy> parse_flow_behavior(JsonValue value) {
  if (value.type() != JsonValue::Type::Object) {
    return Status::Error("flow_behavior must be an object");
  }
  auto &object = value.get_object();
  TRY_STATUS(ensure_exact_object_shape(
      "flow_behavior", object,
      {Slice("max_connects_per_10s_per_destination"), Slice("min_reuse_ratio"), Slice("min_conn_lifetime_ms"),
       Slice("max_conn_lifetime_ms"), Slice("max_destination_share"), Slice("sticky_domain_rotation_window_sec"),
       Slice("anti_churn_min_reconnect_interval_ms")}));

  RuntimeFlowBehaviorPolicy flow_behavior;
  TRY_RESULT(max_connects_per_10s_per_destination,
             object.get_required_int_field("max_connects_per_10s_per_destination"));
  TRY_RESULT(min_reuse_ratio, object.get_required_double_field("min_reuse_ratio"));
  TRY_RESULT(min_conn_lifetime_ms, object.get_required_int_field("min_conn_lifetime_ms"));
  TRY_RESULT(max_conn_lifetime_ms, object.get_required_int_field("max_conn_lifetime_ms"));
  TRY_RESULT(max_destination_share, object.get_required_double_field("max_destination_share"));
  TRY_RESULT(sticky_domain_rotation_window_sec, object.get_required_int_field("sticky_domain_rotation_window_sec"));
  TRY_RESULT(anti_churn_min_reconnect_interval_ms,
             object.get_required_int_field("anti_churn_min_reconnect_interval_ms"));
  if (max_connects_per_10s_per_destination < 0 || min_conn_lifetime_ms < 0 || max_conn_lifetime_ms < 0 ||
      sticky_domain_rotation_window_sec < 0 || anti_churn_min_reconnect_interval_ms < 0) {
    return Status::Error("flow_behavior numeric fields must be non-negative");
  }
  flow_behavior.max_connects_per_10s_per_destination = static_cast<uint32>(max_connects_per_10s_per_destination);
  flow_behavior.min_reuse_ratio = min_reuse_ratio;
  flow_behavior.min_conn_lifetime_ms = static_cast<uint32>(min_conn_lifetime_ms);
  flow_behavior.max_conn_lifetime_ms = static_cast<uint32>(max_conn_lifetime_ms);
  flow_behavior.max_destination_share = max_destination_share;
  flow_behavior.sticky_domain_rotation_window_sec = static_cast<uint32>(sticky_domain_rotation_window_sec);
  flow_behavior.anti_churn_min_reconnect_interval_ms = static_cast<uint32>(anti_churn_min_reconnect_interval_ms);
  return flow_behavior;
}

Result<RuntimeRoutePolicy> parse_route_policy(JsonValue value) {
  if (value.type() != JsonValue::Type::Object) {
    return Status::Error("route_policy must be an object");
  }
  auto &object = value.get_object();
  TRY_STATUS(ensure_exact_object_shape(
      "route_policy", object,
      {Slice("unknown"), Slice("ru"), Slice("ru_egress"), Slice("non_ru"), Slice("non_ru_egress")}));

  if (object.has_field("ru") && object.has_field("ru_egress")) {
    return Status::Error("route_policy must not define both ru and ru_egress");
  }
  if (object.has_field("non_ru") && object.has_field("non_ru_egress")) {
    return Status::Error("route_policy must not define both non_ru and non_ru_egress");
  }

  RuntimeRoutePolicy route_policy;
  TRY_RESULT(unknown_value, object.extract_required_field("unknown", JsonValue::Type::Object));

  JsonValue ru_value;
  if (object.has_field("ru_egress")) {
    TRY_RESULT(parsed_ru_value, object.extract_required_field("ru_egress", JsonValue::Type::Object));
    ru_value = std::move(parsed_ru_value);
  } else {
    TRY_RESULT(parsed_ru_value, object.extract_required_field("ru", JsonValue::Type::Object));
    ru_value = std::move(parsed_ru_value);
  }

  JsonValue non_ru_value;
  if (object.has_field("non_ru_egress")) {
    TRY_RESULT(parsed_non_ru_value, object.extract_required_field("non_ru_egress", JsonValue::Type::Object));
    non_ru_value = std::move(parsed_non_ru_value);
  } else {
    TRY_RESULT(parsed_non_ru_value, object.extract_required_field("non_ru", JsonValue::Type::Object));
    non_ru_value = std::move(parsed_non_ru_value);
  }

  TRY_RESULT(unknown_entry, parse_route_entry("route_policy.unknown", std::move(unknown_value)));
  TRY_RESULT(ru_entry, parse_route_entry("route_policy.ru", std::move(ru_value)));
  TRY_RESULT(non_ru_entry, parse_route_entry("route_policy.non_ru", std::move(non_ru_value)));
  route_policy.unknown = unknown_entry;
  route_policy.ru = ru_entry;
  route_policy.non_ru = non_ru_entry;
  return route_policy;
}

Result<RuntimeRouteFailurePolicy> parse_route_failure(JsonValue value) {
  if (value.type() != JsonValue::Type::Object) {
    return Status::Error("route_failure must be an object");
  }
  auto &object = value.get_object();
  TRY_STATUS(ensure_exact_object_shape(
      "route_failure", object,
      {Slice("ech_failure_threshold"), Slice("ech_fail_open_threshold"), Slice("ech_disable_ttl_seconds"),
       Slice("ech_disable_ttl_sec"), Slice("failure_kinds"), Slice("persist_across_restart")}));

  RuntimeRouteFailurePolicy route_failure;
  if (object.has_field("ech_failure_threshold") && object.has_field("ech_fail_open_threshold")) {
    return Status::Error("route_failure must not define both ech_failure_threshold and ech_fail_open_threshold");
  }
  if (object.has_field("ech_disable_ttl_seconds") && object.has_field("ech_disable_ttl_sec")) {
    return Status::Error("route_failure must not define both ech_disable_ttl_seconds and ech_disable_ttl_sec");
  }

  int32 ech_failure_threshold = 0;
  if (object.has_field("ech_fail_open_threshold")) {
    TRY_RESULT(parsed_threshold, object.get_required_int_field("ech_fail_open_threshold"));
    ech_failure_threshold = parsed_threshold;
  } else {
    TRY_RESULT(parsed_threshold, object.get_required_int_field("ech_failure_threshold"));
    ech_failure_threshold = parsed_threshold;
  }
  if (ech_failure_threshold < 0) {
    return Status::Error("route_failure.ech_failure_threshold must be non-negative");
  }
  route_failure.ech_failure_threshold = static_cast<uint32>(ech_failure_threshold);

  double ech_disable_ttl_seconds = 0.0;
  if (object.has_field("ech_disable_ttl_sec")) {
    TRY_RESULT(parsed_ttl, object.get_required_double_field("ech_disable_ttl_sec"));
    ech_disable_ttl_seconds = parsed_ttl;
  } else {
    TRY_RESULT(parsed_ttl, object.get_required_double_field("ech_disable_ttl_seconds"));
    ech_disable_ttl_seconds = parsed_ttl;
  }

  if (object.has_field("failure_kinds")) {
    TRY_RESULT(failure_kinds_value, object.extract_required_field("failure_kinds", JsonValue::Type::Array));
    const auto &failure_kinds = failure_kinds_value.get_array();
    if (failure_kinds.empty()) {
      return Status::Error("route_failure.failure_kinds must not be empty");
    }
    for (const auto &failure_kind : failure_kinds) {
      if (failure_kind.type() != JsonValue::Type::String || failure_kind.get_string().empty()) {
        return Status::Error("route_failure.failure_kinds entries must be non-empty strings");
      }
    }
  }

  TRY_RESULT(persist_across_restart, object.get_required_bool_field("persist_across_restart"));
  route_failure.ech_disable_ttl_seconds = ech_disable_ttl_seconds;
  route_failure.persist_across_restart = persist_across_restart;
  return route_failure;
}

}  // namespace

StealthParamsLoader::StealthParamsLoader(string config_path)
    : config_path_(std::move(config_path))
    , current_(std::make_shared<const StealthRuntimeParams>(default_runtime_stealth_params())) {
}

Result<StealthRuntimeParams> StealthParamsLoader::try_load_strict(Slice config_path) noexcept {
  auto config_path_str = config_path.str();
#if TD_PORT_POSIX
  struct ::stat st;
  if (::lstat(config_path_str.c_str(), &st) != 0) {
    if (errno == ENOENT || errno == ENOTDIR) {
      return default_runtime_stealth_params();
    }
    return Status::PosixError(errno, "Failed to stat stealth params file");
  }
#endif

  TRY_RESULT(content, read_file_secure(config_path_str));
  return parse_and_validate(std::move(content));
}

bool StealthParamsLoader::try_reload() noexcept {
  auto lock = std::lock_guard<std::mutex>(reload_mu_);
  auto now = Timestamp::now();
  if (reload_cooldown_until_ && !reload_cooldown_until_.is_in_past(now)) {
    return false;
  }

  auto note_reload_failure = [&] {
    consecutive_reload_failures_++;
    if (consecutive_reload_failures_ >= kReloadErrorCooldownThreshold) {
      reload_cooldown_until_ = Timestamp::in(kReloadErrorCooldownSeconds, now);
    }
  };

  if (is_missing_config_path(config_path_)) {
    note_reload_failure();
    return false;
  }

  auto result = try_load_strict(config_path_);
  if (result.is_error()) {
    note_reload_failure();
    return false;
  }
  auto params = result.move_as_ok();
  if (set_runtime_stealth_params(params).is_error()) {
    note_reload_failure();
    return false;
  }
  consecutive_reload_failures_ = 0;
  reload_cooldown_until_ = Timestamp();
  std::atomic_store(&current_, std::make_shared<const StealthRuntimeParams>(params));
  return true;
}

StealthRuntimeParams StealthParamsLoader::get_snapshot() const noexcept {
  auto snapshot = std::atomic_load(&current_);
  CHECK(snapshot != nullptr);
  return *snapshot;
}

Result<string> StealthParamsLoader::read_file_secure(const string &path) noexcept {
#if TD_PORT_POSIX
  TRY_STATUS(validate_secure_parent_directory(path));

  int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
  if (fd < 0) {
    return Status::PosixError(errno, "Failed to open stealth params file");
  }

  struct FdCloser final {
    int fd_;
    ~FdCloser() {
      if (fd_ >= 0) {
        ::close(fd_);
      }
    }
  } closer{fd};

  struct ::stat st;
  if (::fstat(fd, &st) != 0) {
    return Status::PosixError(errno, "Failed to stat stealth params file");
  }
  if (!S_ISREG(st.st_mode)) {
    return Status::Error("Stealth params file must be a regular file");
  }
  if (st.st_uid != ::geteuid()) {
    return Status::Error("Stealth params file must be owned by the current user");
  }
  if ((st.st_mode & S_IWGRP) != 0 || (st.st_mode & S_IWOTH) != 0) {
    return Status::Error("Stealth params file must not be writable by group or others");
  }
  if (st.st_size < 0 || static_cast<uint64>(st.st_size) > kMaxConfigBytes) {
    return Status::Error("Stealth params file exceeds size limit");
  }

  string content;
  content.resize(static_cast<size_t>(st.st_size));
  size_t total_read = 0;
  while (total_read < content.size()) {
    auto read_size = ::read(fd, &content[total_read], content.size() - total_read);
    if (read_size < 0) {
      return Status::PosixError(errno, "Failed to read stealth params file");
    }
    if (read_size == 0) {
      break;
    }
    total_read += static_cast<size_t>(read_size);
  }
  content.resize(total_read);
  return content;
#else
  TRY_RESULT(file, FileFd::open(path, FileFd::Read));
  TRY_RESULT(stat, file.stat());
  if (!stat.is_reg_) {
    return Status::Error("Stealth params file must be a regular file");
  }
  if (stat.size_ < 0 || static_cast<uint64>(stat.size_) > kMaxConfigBytes) {
    return Status::Error("Stealth params file exceeds size limit");
  }
  string content;
  content.resize(static_cast<size_t>(stat.size_));
  TRY_RESULT(read_size, file.read(MutableSlice(content)));
  content.resize(read_size);
  return content;
#endif
}

Result<StealthRuntimeParams> StealthParamsLoader::parse_and_validate(string content) noexcept {
  if (content.empty()) {
    return Status::Error("Stealth params file must not be empty");
  }

  MutableSlice json(content);
  TRY_RESULT(json_value, json_decode(json));
  if (json_value.type() != JsonValue::Type::Object) {
    return Status::Error("Stealth params root must be an object");
  }
  auto &object = json_value.get_object();
  TRY_STATUS(ensure_exact_object_shape("root", object,
                                       {Slice("version"), Slice("active_policy"), Slice("ipt"), Slice("drs"),
                                        Slice("flow_behavior"), Slice("platform_hints"), Slice("profile_weights"),
                                        Slice("route_policy"), Slice("route_failure"), Slice("bulk_threshold_bytes")}));

  TRY_RESULT(version, object.get_required_int_field("version"));
  if (version != 1) {
    return Status::Error("Unsupported stealth params version");
  }

  StealthRuntimeParams params = default_runtime_stealth_params();
  if (object.has_field("active_policy")) {
    TRY_RESULT(active_policy_name, object.get_required_string_field("active_policy"));
    TRY_RESULT(active_policy, parse_active_policy(active_policy_name));
    params.active_policy = active_policy;
  }
  if (object.has_field("ipt")) {
    TRY_RESULT(ipt_value, object.extract_required_field("ipt", JsonValue::Type::Object));
    TRY_RESULT(ipt_params, parse_ipt_params(std::move(ipt_value)));
    params.ipt_params = ipt_params;
  }
  if (object.has_field("drs")) {
    TRY_RESULT(drs_value, object.extract_required_field("drs", JsonValue::Type::Object));
    TRY_RESULT(drs_policy, parse_drs_policy(std::move(drs_value)));
    params.drs_policy = drs_policy;
  }
  if (object.has_field("flow_behavior")) {
    TRY_RESULT(flow_behavior_value, object.extract_required_field("flow_behavior", JsonValue::Type::Object));
    TRY_RESULT(flow_behavior, parse_flow_behavior(std::move(flow_behavior_value)));
    params.flow_behavior = flow_behavior;
  }
  if (object.has_field("platform_hints")) {
    TRY_RESULT(platform_hints_value, object.extract_required_field("platform_hints", JsonValue::Type::Object));
    TRY_RESULT(platform_hints, parse_platform_hints(std::move(platform_hints_value)));
    params.platform_hints = platform_hints;
  }
  TRY_RESULT(profile_weights_value, object.extract_required_field("profile_weights", JsonValue::Type::Object));
  TRY_RESULT(route_policy_value, object.extract_required_field("route_policy", JsonValue::Type::Object));
  TRY_RESULT(route_failure_value, object.extract_required_field("route_failure", JsonValue::Type::Object));
  RuntimeProfileSelectionPolicy profile_selection = params.profile_selection;
  TRY_RESULT(profile_weights,
             parse_profile_weights(std::move(profile_weights_value), params.platform_hints, &profile_selection));
  TRY_RESULT(route_policy, parse_route_policy(std::move(route_policy_value)));
  TRY_RESULT(route_failure, parse_route_failure(std::move(route_failure_value)));
  params.profile_weights = profile_weights;
  params.profile_selection = profile_selection;
  params.route_policy = route_policy;
  params.route_failure = route_failure;

  TRY_RESULT(bulk_threshold_bytes, object.get_required_long_field("bulk_threshold_bytes"));
  if (bulk_threshold_bytes < 0) {
    return Status::Error("bulk_threshold_bytes must be non-negative");
  }
  params.bulk_threshold_bytes = static_cast<size_t>(bulk_threshold_bytes);

  TRY_STATUS(validate_runtime_stealth_params(params));
  return params;
}

}  // namespace stealth
}  // namespace mtproto
}  // namespace td