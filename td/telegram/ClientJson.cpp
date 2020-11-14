//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/ClientJson.h"

#include "td/telegram/td_api.h"
#include "td/telegram/td_api_json.h"

#include "td/utils/common.h"
#include "td/utils/JsonBuilder.h"
#include "td/utils/logging.h"
#include "td/utils/port/thread_local.h"
#include "td/utils/StackAllocator.h"
#include "td/utils/StringBuilder.h"

#include <utility>

namespace td {

static td_api::object_ptr<td_api::Function> get_return_error_function(Slice error_message) {
  auto error = td_api::make_object<td_api::error>(400, error_message.str());
  return td_api::make_object<td_api::testReturnError>(std::move(error));
}

static std::pair<td_api::object_ptr<td_api::Function>, string> to_request(Slice request) {
  auto request_str = request.str();
  auto r_json_value = json_decode(request_str);
  if (r_json_value.is_error()) {
    return {get_return_error_function(PSLICE()
                                      << "Failed to parse request as JSON object: " << r_json_value.error().message()),
            string()};
  }
  auto json_value = r_json_value.move_as_ok();
  if (json_value.type() != JsonValue::Type::Object) {
    return {get_return_error_function("Expected a JSON object"), string()};
  }

  string extra;
  if (has_json_object_field(json_value.get_object(), "@extra")) {
    extra = json_encode<string>(
        get_json_object_field(json_value.get_object(), "@extra", JsonValue::Type::Null).move_as_ok());
  }

  td_api::object_ptr<td_api::Function> func;
  auto status = from_json(func, std::move(json_value));
  if (status.is_error()) {
    return {get_return_error_function(PSLICE()
                                      << "Failed to parse JSON object as TDLib request: " << status.error().message()),
            std::move(extra)};
  }
  return std::make_pair(std::move(func), std::move(extra));
}

static string from_response(const td_api::Object &object, const string &extra, int client_id) {
  auto buf = StackAllocator::alloc(1 << 18);
  JsonBuilder jb(StringBuilder(buf.as_slice(), true), -1);
  jb.enter_value() << ToJson(object);
  auto slice = jb.string_builder().as_cslice();
  CHECK(!slice.empty() && slice.back() == '}');
  string str;
  str.reserve(slice.size() + (extra.empty() ? 0 : 10 + extra.size()) + (client_id == 0 ? 0 : 14 + 10));
  str.append(slice.begin(), slice.size() - 1);
  if (!extra.empty()) {
    str += ",\"@extra\":";
    str += extra;
  }
  if (client_id != 0) {
    str += ",\"@client_id\":";
    str += to_string(client_id);
  }
  str += '}';
  return str;
}

static TD_THREAD_LOCAL string *current_output;

static const char *store_string(string str) {
  init_thread_local<string>(current_output);
  *current_output = std::move(str);
  return current_output->c_str();
}

void ClientJson::send(Slice request) {
  auto parsed_request = to_request(request);
  std::uint64_t extra_id = extra_id_.fetch_add(1, std::memory_order_relaxed);
  if (!parsed_request.second.empty()) {
    std::lock_guard<std::mutex> guard(mutex_);
    extra_[extra_id] = std::move(parsed_request.second);
  }
  client_.send(Client::Request{extra_id, std::move(parsed_request.first)});
}

const char *ClientJson::receive(double timeout) {
  auto response = client_.receive(timeout);
  if (response.object == nullptr) {
    return nullptr;
  }

  string extra;
  if (response.id != 0) {
    std::lock_guard<std::mutex> guard(mutex_);
    auto it = extra_.find(response.id);
    if (it != extra_.end()) {
      extra = std::move(it->second);
      extra_.erase(it);
    }
  }
  return store_string(from_response(*response.object, extra, 0));
}

const char *ClientJson::execute(Slice request) {
  auto parsed_request = to_request(request);
  return store_string(from_response(*Client::execute(Client::Request{0, std::move(parsed_request.first)}).object,
                                    parsed_request.second, 0));
}

static ClientManager *get_manager() {
  return ClientManager::get_manager_singleton();
}

static std::mutex extra_mutex;
static std::unordered_map<int64, string> extra;
static std::atomic<uint64> extra_id{1};

int json_create_client_id() {
  return static_cast<int>(get_manager()->create_client_id());
}

void json_send(int client_id, Slice request) {
  auto parsed_request = to_request(request);
  auto request_id = extra_id.fetch_add(1, std::memory_order_relaxed);
  if (!parsed_request.second.empty()) {
    std::lock_guard<std::mutex> guard(extra_mutex);
    extra[request_id] = std::move(parsed_request.second);
  }
  get_manager()->send(client_id, request_id, std::move(parsed_request.first));
}

const char *json_receive(double timeout) {
  auto response = get_manager()->receive(timeout);
  if (!response.object) {
    return nullptr;
  }

  string extra_str;
  if (response.request_id != 0) {
    std::lock_guard<std::mutex> guard(extra_mutex);
    auto it = extra.find(response.request_id);
    if (it != extra.end()) {
      extra_str = std::move(it->second);
      extra.erase(it);
    }
  }
  return store_string(from_response(*response.object, extra_str, response.client_id));
}

const char *json_execute(Slice request) {
  auto parsed_request = to_request(request);
  return store_string(
      from_response(*ClientManager::execute(std::move(parsed_request.first)), parsed_request.second, 0));
}

}  // namespace td
