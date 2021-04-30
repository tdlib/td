//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2021
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/GroupCallVideoPayload.h"

#include "td/telegram/misc.h"

#include "td/utils/algorithm.h"
#include "td/utils/JsonBuilder.h"
#include "td/utils/misc.h"

namespace td {

static bool operator==(const GroupCallVideoPayloadFeedbackType &lhs, const GroupCallVideoPayloadFeedbackType &rhs) {
  return lhs.type == rhs.type && lhs.subtype == rhs.subtype;
}

static td_api::object_ptr<td_api::groupCallVideoPayloadFeedbackType> get_group_call_video_payload_feedback_type_object(
    const GroupCallVideoPayloadFeedbackType &feedback_type) {
  return td_api::make_object<td_api::groupCallVideoPayloadFeedbackType>(feedback_type.type, feedback_type.subtype);
}

static bool operator==(const GroupCallVideoPayloadParameter &lhs, const GroupCallVideoPayloadParameter &rhs) {
  return lhs.name == rhs.name && lhs.value == rhs.value;
}

static td_api::object_ptr<td_api::groupCallVideoPayloadParameter> get_group_call_video_payload_parameter_object(
    const GroupCallVideoPayloadParameter &parameter) {
  return td_api::make_object<td_api::groupCallVideoPayloadParameter>(parameter.name, parameter.value);
}

static bool operator==(const GroupCallVideoPayloadType &lhs, const GroupCallVideoPayloadType &rhs) {
  return lhs.id == rhs.id && lhs.name == rhs.name && lhs.clock_rate == rhs.clock_rate &&
         lhs.channel_count == rhs.channel_count && lhs.feedback_types == rhs.feedback_types &&
         lhs.parameters == rhs.parameters;
}

static td_api::object_ptr<td_api::groupCallVideoPayloadType> get_group_call_video_payload_type_object(
    const GroupCallVideoPayloadType &payload_type) {
  return td_api::make_object<td_api::groupCallVideoPayloadType>(
      payload_type.id, payload_type.name, payload_type.clock_rate, payload_type.channel_count,
      transform(payload_type.feedback_types, get_group_call_video_payload_feedback_type_object),
      transform(payload_type.parameters, get_group_call_video_payload_parameter_object));
}

static bool operator==(const GroupCallVideoExtension &lhs, const GroupCallVideoExtension &rhs) {
  return lhs.id == rhs.id && lhs.name == rhs.name;
}

static td_api::object_ptr<td_api::groupCallVideoExtension> get_group_call_video_extension_object(
    const GroupCallVideoExtension &extension) {
  return td_api::make_object<td_api::groupCallVideoExtension>(extension.id, extension.name);
}

static bool operator==(const GroupCallVideoSourceGroup &lhs, const GroupCallVideoSourceGroup &rhs) {
  return lhs.sources == rhs.sources && lhs.semantics == rhs.semantics;
}

static td_api::object_ptr<td_api::groupCallVideoSourceGroup> get_group_call_video_source_group_object(
    const GroupCallVideoSourceGroup &source_group) {
  return td_api::make_object<td_api::groupCallVideoSourceGroup>(vector<int32>(source_group.sources),
                                                                source_group.semantics);
}

bool operator==(const GroupCallVideoPayload &lhs, const GroupCallVideoPayload &rhs) {
  return lhs.payload_types == rhs.payload_types && lhs.extensions == rhs.extensions &&
         lhs.source_groups == rhs.source_groups;
}

bool operator!=(const GroupCallVideoPayload &lhs, const GroupCallVideoPayload &rhs) {
  return !(lhs == rhs);
}

td_api::object_ptr<td_api::groupCallVideoPayload> get_group_call_video_payload_object(
    const GroupCallVideoPayload &payload) {
  if (payload.payload_types.empty() && payload.extensions.empty() && payload.source_groups.empty()) {
    return nullptr;
  }

  return td_api::make_object<td_api::groupCallVideoPayload>(
      transform(payload.payload_types, get_group_call_video_payload_type_object),
      transform(payload.extensions, get_group_call_video_extension_object),
      transform(payload.source_groups, get_group_call_video_source_group_object));
}

static Result<GroupCallVideoPayloadType> get_group_call_video_payload_type(JsonValue &&value) {
  if (value.type() != JsonValue::Type::Object) {
    return Status::Error("Expected JSON object as payload type");
  }

  GroupCallVideoPayloadType result;
  auto &value_object = value.get_object();
  TRY_RESULT_ASSIGN(result.id, get_json_object_int_field(value_object, "id", false));
  TRY_RESULT_ASSIGN(result.name, get_json_object_string_field(value_object, "name", false));
  TRY_RESULT_ASSIGN(result.clock_rate, get_json_object_int_field(value_object, "clockrate", false));
  TRY_RESULT_ASSIGN(result.channel_count, get_json_object_int_field(value_object, "channels"));
  TRY_RESULT(feedback_types, get_json_object_field(value_object, "rtcp-fbs", JsonValue::Type::Array));
  TRY_RESULT(parameters, get_json_object_field(value_object, "parameters", JsonValue::Type::Object));

  if (feedback_types.type() != JsonValue::Type::Null) {
    CHECK(feedback_types.type() == JsonValue::Type::Array);
    for (auto &feedback_type_value : feedback_types.get_array()) {
      if (feedback_type_value.type() != JsonValue::Type::Object) {
        return Status::Error("Expected JSON object as feedback type");
      }

      auto &feedback_type_object = feedback_type_value.get_object();
      GroupCallVideoPayloadFeedbackType feedback_type;
      TRY_RESULT_ASSIGN(feedback_type.type, get_json_object_string_field(feedback_type_object, "type", false));
      TRY_RESULT_ASSIGN(feedback_type.subtype, get_json_object_string_field(feedback_type_object, "subtype"));
      result.feedback_types.push_back(std::move(feedback_type));
    }
  }
  if (parameters.type() != JsonValue::Type::Null) {
    CHECK(parameters.type() == JsonValue::Type::Object);
    for (auto &parameter_value : parameters.get_object()) {
      GroupCallVideoPayloadParameter parameter;
      parameter.name = parameter_value.first.str();
      if (parameter_value.second.type() == JsonValue::Type::String) {
        parameter.value = parameter_value.second.get_string().str();
      } else if (parameter_value.second.type() == JsonValue::Type::Number) {
        parameter.value = parameter_value.second.get_number().str();
      } else {
        return Status::Error("Receive unexpected parameter type");
      }
      result.parameters.push_back(std::move(parameter));
    }
  }

  return result;
}

static Result<GroupCallVideoExtension> get_group_call_video_extension(JsonValue &&value) {
  if (value.type() != JsonValue::Type::Object) {
    return Status::Error("Expected JSON object as RTP header extension");
  }

  GroupCallVideoExtension result;
  auto &value_object = value.get_object();
  TRY_RESULT_ASSIGN(result.id, get_json_object_int_field(value_object, "id", false));
  TRY_RESULT_ASSIGN(result.name, get_json_object_string_field(value_object, "uri", false));
  return result;
}

static Result<GroupCallVideoSourceGroup> get_group_call_video_source_group(JsonValue &&value) {
  if (value.type() != JsonValue::Type::Object) {
    return Status::Error("Expected JSON object as synchronization source group");
  }

  GroupCallVideoSourceGroup result;
  auto &value_object = value.get_object();
  TRY_RESULT(sources, get_json_object_field(value_object, "sources", JsonValue::Type::Array, false));
  TRY_RESULT_ASSIGN(result.semantics, get_json_object_string_field(value_object, "semantics", false));

  for (auto &source : sources.get_array()) {
    Slice source_str;
    if (source.type() == JsonValue::Type::String) {
      source_str = source.get_string();
    } else if (source.type() == JsonValue::Type::Number) {
      source_str = source.get_number();
    }
    TRY_RESULT(source_id, to_integer_safe<int32>(source_str));
    result.sources.push_back(source_id);
  }

  return result;
}

Result<GroupCallVideoPayload> get_group_call_video_payload(string json, string &endpoint) {
  auto r_value = json_decode(json);
  if (r_value.is_error()) {
    return Status::Error("Can't parse JSON object");
  }

  auto value = r_value.move_as_ok();
  if (value.type() != JsonValue::Type::Object) {
    return Status::Error("Expected an Object");
  }

  auto &value_object = value.get_object();
  TRY_RESULT_ASSIGN(endpoint, get_json_object_string_field(value_object, "endpoint", false));
  TRY_RESULT(source_groups, get_json_object_field(value_object, "ssrc-groups", JsonValue::Type::Array, false));

  GroupCallVideoPayload result;
  for (auto &source_group_object : source_groups.get_array()) {
    TRY_RESULT(source_group, get_group_call_video_source_group(std::move(source_group_object)));
    result.source_groups.push_back(std::move(source_group));
  }
  return result;
}

Result<string> encode_join_group_call_payload(td_api::object_ptr<td_api::groupCallPayload> &&payload,
                                              int32 audio_source,
                                              td_api::object_ptr<td_api::groupCallVideoPayload> &&video_payload) {
  if (payload == nullptr) {
    return Status::Error(400, "Payload must be non-empty");
  }
  if (!clean_input_string(payload->ufrag_)) {
    return Status::Error(400, "Payload ufrag must be encoded in UTF-8");
  }
  if (!clean_input_string(payload->pwd_)) {
    return Status::Error(400, "Payload pwd must be encoded in UTF-8");
  }
  for (auto &fingerprint : payload->fingerprints_) {
    if (fingerprint == nullptr) {
      return Status::Error(400, "Payload fingerprint must be non-empty");
    }
    if (!clean_input_string(fingerprint->hash_)) {
      return Status::Error(400, "Fingerprint hash must be encoded in UTF-8");
    }
    if (!clean_input_string(fingerprint->setup_)) {
      return Status::Error(400, "Fingerprint setup must be encoded in UTF-8");
    }
    if (!clean_input_string(fingerprint->fingerprint_)) {
      return Status::Error(400, "Fingerprint must be encoded in UTF-8");
    }
  }

  if (audio_source == 0) {
    return Status::Error(400, "Audio synchronization source must be non-zero");
  }

  if (video_payload != nullptr) {
    for (auto &payload_type : video_payload->payload_types_) {
      if (!clean_input_string(payload_type->name_)) {
        return Status::Error(400, "Video payload type name must be encoded in UTF-8");
      }
      for (auto &feedback_type : payload_type->feedback_types_) {
        if (!clean_input_string(feedback_type->type_)) {
          return Status::Error(400, "Video feedback type must be encoded in UTF-8");
        }
        if (!clean_input_string(feedback_type->subtype_)) {
          return Status::Error(400, "Video feedback subtype must be encoded in UTF-8");
        }
      }
      for (auto &parameter : payload_type->parameters_) {
        if (!clean_input_string(parameter->name_)) {
          return Status::Error(400, "Video parameter name must be encoded in UTF-8");
        }
        if (!clean_input_string(parameter->value_)) {
          return Status::Error(400, "Video parameter value must be encoded in UTF-8");
        }
      }
    }
    for (auto &extension : video_payload->extensions_) {
      if (!clean_input_string(extension->name_)) {
        return Status::Error(400, "RTP header extension name must be encoded in UTF-8");
      }
    }
    for (auto &source_group : video_payload->source_groups_) {
      if (!clean_input_string(source_group->semantics_)) {
        return Status::Error(400, "Video source group semantics must be encoded in UTF-8");
      }
    }
  }

  return json_encode<string>(json_object([&payload, audio_source, &video_payload](auto &o) {
    o("ufrag", payload->ufrag_);
    o("pwd", payload->pwd_);
    o("fingerprints", json_array(payload->fingerprints_,
                                 [](const td_api::object_ptr<td_api::groupCallPayloadFingerprint> &fingerprint) {
                                   return json_object([&fingerprint](auto &o) {
                                     o("hash", fingerprint->hash_);
                                     o("setup", fingerprint->setup_);
                                     o("fingerprint", fingerprint->fingerprint_);
                                   });
                                 }));
    o("ssrc", audio_source);
    if (video_payload != nullptr) {
      o("payload-types",
        json_array(video_payload->payload_types_,
                   [](const td_api::object_ptr<td_api::groupCallVideoPayloadType> &payload_type) {
                     return json_object([&payload_type](auto &o) {
                       o("id", payload_type->id_);
                       o("name", payload_type->name_);
                       o("clockrate", payload_type->clock_rate_);
                       o("channels", payload_type->channel_count_);
                       if (!payload_type->feedback_types_.empty()) {
                         o("rtcp-fbs",
                           json_array(
                               payload_type->feedback_types_,
                               [](const td_api::object_ptr<td_api::groupCallVideoPayloadFeedbackType> &feedback_type) {
                                 return json_object([&feedback_type](auto &o) {
                                   o("type", feedback_type->type_);
                                   if (!feedback_type->subtype_.empty()) {
                                     o("subtype", feedback_type->subtype_);
                                   }
                                 });
                               }));
                       }
                       if (!payload_type->parameters_.empty()) {
                         o("parameters", json_object([parameters = &payload_type->parameters_](auto &o) {
                             for (auto &parameter : *parameters) {
                               o(parameter->name_, parameter->value_);
                             }
                           }));
                       }
                     });
                   }));
      o("rtp-hdrexts", json_array(video_payload->extensions_,
                                  [](const td_api::object_ptr<td_api::groupCallVideoExtension> &extension) {
                                    return json_object([&extension](auto &o) {
                                      o("id", extension->id_);
                                      o("uri", extension->name_);
                                    });
                                  }));
      o("ssrc-groups", json_array(video_payload->source_groups_,
                                  [](const td_api::object_ptr<td_api::groupCallVideoSourceGroup> &source_group) {
                                    return json_object([&source_group](auto &o) {
                                      o("sources",
                                        json_array(source_group->sources_, [](int32 source) { return source; }));
                                      o("semantics", source_group->semantics_);
                                    });
                                  }));
    }
  }));
}

Result<td_api::object_ptr<td_api::GroupCallJoinResponse>> get_group_call_join_response_object(string json) {
  auto r_value = json_decode(json);
  if (r_value.is_error()) {
    return Status::Error("Can't parse JSON object");
  }

  auto value = r_value.move_as_ok();
  if (value.type() != JsonValue::Type::Object) {
    return Status::Error("Expected an Object");
  }

  auto &value_object = value.get_object();
  auto r_stream = get_json_object_bool_field(value_object, "stream");
  if (r_stream.is_ok() && r_stream.ok() == true) {
    return td_api::make_object<td_api::groupCallJoinResponseStream>();
  }

  TRY_RESULT(transport, get_json_object_field(value_object, "transport", JsonValue::Type::Object, false));
  CHECK(transport.type() == JsonValue::Type::Object);
  auto &transport_object = transport.get_object();

  TRY_RESULT(candidates, get_json_object_field(transport_object, "candidates", JsonValue::Type::Array, false));
  TRY_RESULT(fingerprints, get_json_object_field(transport_object, "fingerprints", JsonValue::Type::Array, false));
  TRY_RESULT(ufrag, get_json_object_string_field(transport_object, "ufrag", false));
  TRY_RESULT(pwd, get_json_object_string_field(transport_object, "pwd", false));
  // skip "xmlns", "rtcp-mux"

  vector<td_api::object_ptr<td_api::groupCallPayloadFingerprint>> fingerprints_object;
  for (auto &fingerprint : fingerprints.get_array()) {
    if (fingerprint.type() != JsonValue::Type::Object) {
      return Status::Error("Expected JSON object as fingerprint");
    }
    auto &fingerprint_object = fingerprint.get_object();
    TRY_RESULT(hash, get_json_object_string_field(fingerprint_object, "hash", false));
    TRY_RESULT(setup, get_json_object_string_field(fingerprint_object, "setup", false));
    TRY_RESULT(fingerprint_value, get_json_object_string_field(fingerprint_object, "fingerprint", false));
    fingerprints_object.push_back(
        td_api::make_object<td_api::groupCallPayloadFingerprint>(hash, setup, fingerprint_value));
  }

  vector<td_api::object_ptr<td_api::groupCallJoinResponseCandidate>> candidates_object;
  for (auto &candidate : candidates.get_array()) {
    if (candidate.type() != JsonValue::Type::Object) {
      return Status::Error("Expected JSON object as candidate");
    }
    auto &candidate_object = candidate.get_object();
    TRY_RESULT(port, get_json_object_string_field(candidate_object, "port", false));
    TRY_RESULT(protocol, get_json_object_string_field(candidate_object, "protocol", false));
    TRY_RESULT(network, get_json_object_string_field(candidate_object, "network", false));
    TRY_RESULT(generation, get_json_object_string_field(candidate_object, "generation", false));
    TRY_RESULT(id, get_json_object_string_field(candidate_object, "id", false));
    TRY_RESULT(component, get_json_object_string_field(candidate_object, "component", false));
    TRY_RESULT(foundation, get_json_object_string_field(candidate_object, "foundation", false));
    TRY_RESULT(priority, get_json_object_string_field(candidate_object, "priority", false));
    TRY_RESULT(ip, get_json_object_string_field(candidate_object, "ip", false));
    TRY_RESULT(type, get_json_object_string_field(candidate_object, "type", false));
    TRY_RESULT(tcp_type, get_json_object_string_field(candidate_object, "tcptype"));
    TRY_RESULT(rel_addr, get_json_object_string_field(candidate_object, "rel-addr"));
    TRY_RESULT(rel_port, get_json_object_string_field(candidate_object, "rel-port"));
    candidates_object.push_back(td_api::make_object<td_api::groupCallJoinResponseCandidate>(
        port, protocol, network, generation, id, component, foundation, priority, ip, type, tcp_type, rel_addr,
        rel_port));
  }

  TRY_RESULT(video, get_json_object_field(value_object, "video", JsonValue::Type::Object, true));
  int32 server_video_bandwidth_probing_source = 0;
  if (video.type() == JsonValue::Type::Object) {
    auto &video_object = video.get_object();
    TRY_RESULT(server_sources, get_json_object_field(video_object, "server_sources", JsonValue::Type::Array, false));
    auto &server_sources_array = server_sources.get_array();
    if (server_sources_array.empty()) {
      return Status::Error("Expected at least one server source");
    }
    if (server_sources_array[0].type() != JsonValue::Type::Number) {
      return Status::Error("Expected Number as server source");
    }
    TRY_RESULT_ASSIGN(server_video_bandwidth_probing_source,
                      to_integer_safe<int32>(server_sources_array[0].get_number()));
  }

  auto payload = td_api::make_object<td_api::groupCallPayload>(ufrag, pwd, std::move(fingerprints_object));
  return td_api::make_object<td_api::groupCallJoinResponseWebrtc>(std::move(payload), std::move(candidates_object),
                                                                  server_video_bandwidth_probing_source);
}

}  // namespace td
