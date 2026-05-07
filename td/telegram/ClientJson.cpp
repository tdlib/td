//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/ClientJson.h"

#include "td/telegram/td_api.h"
#include "td/telegram/td_api_json.h"

#include "td/utils/common.h"
#include "td/utils/FlatHashMap.h"
#include "td/utils/JsonBuilder.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/port/thread_local.h"
#include "td/utils/SliceBuilder.h"
#include "td/utils/StackAllocator.h"
#include "td/utils/StringBuilder.h"

#include <cmath>
#include <utility>

namespace td {

namespace {

constexpr std::size_t kClientJsonPreviewBytes = 32;
constexpr std::size_t kClientJsonControlScanBytes = 64;

bool is_json_whitespace(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

string get_request_hex_preview(Slice request) {
  auto preview_size = std::min(request.size(), kClientJsonPreviewBytes);
  return hex_encode(request.substr(0, preview_size));
}

string get_request_boundary_context(Slice request) {
  return PSTRING() << "request_size=" << request.size() << " preview_hex=" << get_request_hex_preview(request);
}

std::size_t find_first_non_whitespace_byte(Slice request) {
  for (std::size_t i = 0; i < request.size(); i++) {
    if (!is_json_whitespace(request[i])) {
      return i;
    }
  }
  return request.size();
}

std::size_t find_first_disallowed_control_byte(Slice request) {
  auto scan_size = std::min(request.size(), kClientJsonControlScanBytes);
  for (std::size_t i = 0; i < scan_size; i++) {
    auto byte = static_cast<unsigned char>(request[i]);
    if (byte < 32 && !is_json_whitespace(request[i])) {
      return i;
    }
  }
  return request.size();
}

string byte_hex(unsigned char byte) {
  auto c = static_cast<char>(byte);
  return hex_encode(Slice(&c, 1));
}

double sanitize_timeout_seconds(double timeout, Slice callsite) {
  if (std::isfinite(timeout) && timeout >= 0.0) {
    return timeout;
  }
  LOG(WARNING) << callsite << ": invalid timeout value timeout=" << timeout << " fallback_timeout=0";
  return 0.0;
}

}  // namespace

static td_api::object_ptr<td_api::Function> get_return_error_function(Slice error_message) {
  auto error = td_api::make_object<td_api::error>(400, error_message.str());
  return td_api::make_object<td_api::testReturnError>(std::move(error));
}

static std::pair<td_api::object_ptr<td_api::Function>, string> to_request(Slice request) {
  auto boundary_context = get_request_boundary_context(request);
  LOG(DEBUG) << "ClientJson boundary: inbound request payload received " << boundary_context;

  if (request.empty()) {
    LOG(WARNING) << "ClientJson boundary reject: empty request payload " << boundary_context;
    return {get_return_error_function("Empty request payload"), string()};
  }

  if (auto control_byte_pos = find_first_disallowed_control_byte(request); control_byte_pos != request.size()) {
    auto bad_byte = static_cast<unsigned char>(request[control_byte_pos]);
    LOG(WARNING) << "ClientJson boundary reject: control byte detected in request prefix at offset " << control_byte_pos
                 << " byte=0x" << byte_hex(bad_byte) << ' ' << boundary_context;
    return {get_return_error_function(PSLICE() << "Invalid request payload: unexpected control byte at offset "
                                               << control_byte_pos),
            string()};
  }

  auto first_non_whitespace = find_first_non_whitespace_byte(request);
  if (first_non_whitespace == request.size()) {
    LOG(WARNING) << "ClientJson boundary reject: request payload has no JSON tokens " << boundary_context;
    return {get_return_error_function("Expected a JSON object request payload"), string()};
  }
  if (request[first_non_whitespace] != '{') {
    auto bad_byte = static_cast<unsigned char>(request[first_non_whitespace]);
    LOG(WARNING) << "ClientJson boundary reject: top-level JSON token is not object start offset="
                 << first_non_whitespace << " byte=0x" << byte_hex(bad_byte) << ' ' << boundary_context;
    return {get_return_error_function("Expected a JSON object request payload"), string()};
  }

  auto request_str = request.str();
  auto r_json_value = json_decode(request_str);
  if (r_json_value.is_error()) {
    LOG(WARNING) << "ClientJson boundary parse failure: json_decode failed parser_error=\""
                 << r_json_value.error().message() << "\" " << boundary_context;
    return {get_return_error_function(PSLICE()
                                      << "Failed to parse request as JSON object: " << r_json_value.error().message()),
            string()};
  }
  auto json_value = r_json_value.move_as_ok();
  if (json_value.type() != JsonValue::Type::Object) {
    LOG(WARNING) << "ClientJson boundary parse failure: top-level JSON type is not object " << boundary_context;
    return {get_return_error_function("Expected a JSON object request payload"), string()};
  }

  string extra;
  if (json_value.get_object().has_field("@extra")) {
    extra = json_encode<string>(json_value.get_object().extract_field("@extra"));
  }

  td_api::object_ptr<td_api::Function> func;
  if (auto status = from_json(func, std::move(json_value)); status.is_error()) {
    LOG(WARNING) << "ClientJson boundary parse failure: TDLib request conversion failed conversion_error=\""
                 << status.message() << "\" " << boundary_context;
    return {get_return_error_function(PSLICE() << "Failed to parse JSON object as TDLib request: " << status.message()),
            std::move(extra)};
  }
  LOG(DEBUG) << "ClientJson boundary: request parsed successfully function_id=" << func->get_id() << ' '
             << boundary_context;
  return std::make_pair(std::move(func), std::move(extra));
}

static string from_response(const td_api::Object &object, const string &extra, int client_id) {
  auto buf = StackAllocator::alloc(1 << 18);
  JsonBuilder jb(StringBuilder(buf.as_slice(), true), -1);
  jb.enter_value() << ToJson(object);
  auto &sb = jb.string_builder();
  auto slice = sb.as_cslice();
  CHECK(!slice.empty() && slice.back() == '}');
  sb.pop_back();
  if (!extra.empty()) {
    sb << ",\"@extra\":" << extra;
  }
  if (client_id != 0) {
    sb << ",\"@client_id\":" << client_id;
  }
  sb << '}';
  return sb.as_cslice().str();
}

static string &get_current_output() {
  static TD_THREAD_LOCAL string *current_output = nullptr;
  init_thread_local<string>(current_output);
  CHECK(current_output != nullptr);
  return *current_output;
}

static const char *store_string(string str) {
  auto &current_output = get_current_output();
  current_output = std::move(str);
  return current_output.c_str();
}

void ClientJson::send(Slice request) {
  LOG(DEBUG) << "ClientJson::send: begin request_size=" << request.size();
  auto [request_object, request_extra] = to_request(request);
  auto extra_id = extra_id_.fetch_add(1);
  auto has_extra = !request_extra.empty();
  if (has_extra) {
    std::scoped_lock guard(mutex_);
    extra_[extra_id] = std::move(request_extra);
  }
  LOG(DEBUG) << "ClientJson::send: enqueue request extra_id=" << extra_id << " has_extra=" << has_extra;
  client_.send(Client::Request{extra_id, std::move(request_object)});
}

const char *ClientJson::receive(double timeout) {
  auto effective_timeout = sanitize_timeout_seconds(timeout, "ClientJson::receive");
  LOG(DEBUG) << "ClientJson::receive: begin timeout=" << timeout << " effective_timeout=" << effective_timeout;
  auto response = client_.receive(effective_timeout);
  if (response.object == nullptr) {
    LOG(DEBUG) << "ClientJson::receive: no response effective_timeout=" << effective_timeout;
    return nullptr;
  }

  string extra;
  bool has_extra = false;
  if (response.id != 0) {
    std::scoped_lock guard(mutex_);
    auto it = extra_.find(response.id);
    if (it != extra_.end()) {
      extra = std::move(it->second);
      extra_.erase(it);
      has_extra = true;
    }
  }
  if (response.id != 0 && !has_extra) {
    LOG(WARNING) << "ClientJson::receive: missing @extra correlation for response_id=" << response.id;
  }
  LOG(DEBUG) << "ClientJson::receive: response ready response_id=" << response.id
             << " object_id=" << response.object->get_id() << " has_extra=" << has_extra;
  return store_string(from_response(*response.object, extra, 0));
}

const char *ClientJson::execute(Slice request) {
  LOG(DEBUG) << "ClientJson::execute: begin request_size=" << request.size();
  auto [request_object, request_extra] = to_request(request);
  LOG(DEBUG) << "ClientJson::execute: dispatch parsed request has_extra=" << !request_extra.empty();
  return store_string(
      from_response(*Client::execute(Client::Request{0, std::move(request_object)}).object, request_extra, 0));
}

static ClientManager *get_manager() {
  return ClientManager::get_manager_singleton();
}

static std::mutex &json_extra_mutex() {
  static std::mutex extra_mutex;
  return extra_mutex;
}

static FlatHashMap<int64, string> &json_extra() {
  static FlatHashMap<int64, string> extra;
  return extra;
}

static std::atomic<uint64> &json_extra_id() {
  static std::atomic<uint64> extra_id{1};
  return extra_id;
}

int json_create_client_id() {
  return static_cast<int>(get_manager()->create_client_id());
}

void json_send(int client_id, Slice request) {
  LOG(DEBUG) << "json_send: begin client_id=" << client_id << " request_size=" << request.size();
  auto [request_object, request_extra] = to_request(request);
  auto request_id = json_extra_id().fetch_add(1);
  auto has_extra = !request_extra.empty();
  if (has_extra) {
    std::scoped_lock guard(json_extra_mutex());
    json_extra()[request_id] = std::move(request_extra);
  }
  LOG(DEBUG) << "json_send: forward request client_id=" << client_id << " request_id=" << request_id
             << " has_extra=" << has_extra;
  get_manager()->send(client_id, request_id, std::move(request_object));
}

const char *json_receive(double timeout) {
  auto effective_timeout = sanitize_timeout_seconds(timeout, "json_receive");
  LOG(DEBUG) << "json_receive: begin timeout=" << timeout << " effective_timeout=" << effective_timeout;
  auto response = get_manager()->receive(effective_timeout);
  if (!response.object) {
    LOG(DEBUG) << "json_receive: no response effective_timeout=" << effective_timeout;
    return nullptr;
  }

  string extra_str;
  bool has_extra = false;
  if (response.request_id != 0) {
    std::scoped_lock guard(json_extra_mutex());
    auto it = json_extra().find(response.request_id);
    if (it != json_extra().end()) {
      extra_str = std::move(it->second);
      json_extra().erase(it);
      has_extra = true;
    }
  }
  if (response.request_id != 0 && !has_extra) {
    LOG(WARNING) << "json_receive: missing @extra correlation for request_id=" << response.request_id
                 << " client_id=" << response.client_id;
  }
  LOG(DEBUG) << "json_receive: response ready client_id=" << response.client_id << " request_id=" << response.request_id
             << " object_id=" << response.object->get_id() << " has_extra=" << has_extra;
  return store_string(from_response(*response.object, extra_str, response.client_id));
}

const char *json_execute(Slice request) {
  LOG(DEBUG) << "json_execute: begin request_size=" << request.size();
  auto [request_object, request_extra] = to_request(request);
  LOG(DEBUG) << "json_execute: dispatch parsed request has_extra=" << !request_extra.empty();
  return store_string(from_response(*ClientManager::execute(std::move(request_object)), request_extra, 0));
}

}  // namespace td
