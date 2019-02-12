//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2019
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/ClientJson.h"

#include "td/telegram/td_api.h"
#include "td/telegram/td_api_json.h"

#include "td/tl/tl_json.h"

#include "td/utils/common.h"
#include "td/utils/format.h"
#include "td/utils/JsonBuilder.h"
#include "td/utils/logging.h"
#include "td/utils/port/thread_local.h"
#include "td/utils/Status.h"

#include <utility>

namespace td {

static Result<std::pair<td_api::object_ptr<td_api::Function>, string>> to_request(Slice request) {
  auto request_str = request.str();
  TRY_RESULT(json_value, json_decode(request_str));
  if (json_value.type() != JsonValue::Type::Object) {
    return Status::Error("Expected an Object");
  }

  string extra;
  if (has_json_object_field(json_value.get_object(), "@extra")) {
    extra = json_encode<string>(
        get_json_object_field(json_value.get_object(), "@extra", JsonValue::Type::Null).move_as_ok());
  }

  td_api::object_ptr<td_api::Function> func;
  TRY_STATUS(from_json(func, json_value));
  return std::make_pair(std::move(func), extra);
}

static std::string from_response(const td_api::Object &object, const string &extra) {
  auto str = json_encode<string>(ToJson(object));
  CHECK(!str.empty() && str.back() == '}');
  if (!extra.empty()) {
    str.pop_back();
    str.reserve(str.size() + 11 + extra.size());
    str += ",\"@extra\":";
    str += extra;
    str += '}';
  }
  return str;
}

static TD_THREAD_LOCAL std::string *current_output;

static CSlice store_string(std::string str) {
  init_thread_local<std::string>(current_output);
  *current_output = std::move(str);
  return *current_output;
}

void ClientJson::send(Slice request) {
  auto r_request = to_request(request);
  if (r_request.is_error()) {
    LOG(ERROR) << "Failed to parse " << tag("request", format::escaped(request)) << " " << r_request.error();
    return;
  }

  std::uint64_t extra_id = extra_id_.fetch_add(1, std::memory_order_relaxed);
  if (!r_request.ok_ref().second.empty()) {
    std::lock_guard<std::mutex> guard(mutex_);
    extra_[extra_id] = std::move(r_request.ok_ref().second);
  }
  client_.send(Client::Request{extra_id, std::move(r_request.ok_ref().first)});
}

CSlice ClientJson::receive(double timeout) {
  auto response = client_.receive(timeout);
  if (!response.object) {
    return {};
  }

  std::string extra;
  if (response.id != 0) {
    std::lock_guard<std::mutex> guard(mutex_);
    auto it = extra_.find(response.id);
    if (it != extra_.end()) {
      extra = std::move(it->second);
      extra_.erase(it);
    }
  }
  return store_string(from_response(*response.object, extra));
}

CSlice ClientJson::execute(Slice request) {
  auto r_request = to_request(request);
  if (r_request.is_error()) {
    LOG(ERROR) << "Failed to parse " << tag("request", format::escaped(request)) << " " << r_request.error();
    return {};
  }

  return store_string(from_response(*Client::execute(Client::Request{0, std::move(r_request.ok_ref().first)}).object,
                                    r_request.ok().second));
}

}  // namespace td
