//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/ClientJson.h"

#include "td/telegram/td_api.h"
#include "td/telegram/td_api_json.h"

#include "td/tl/tl_json.h"

#include "td/utils/format.h"
#include "td/utils/JsonBuilder.h"
#include "td/utils/logging.h"
#include "td/utils/Status.h"

#include <algorithm>

namespace td {

Result<Client::Request> ClientJson::to_request(Slice request) {
  auto request_str = request.str();
  TRY_RESULT(json_value, json_decode(request_str));
  if (json_value.type() != JsonValue::Type::Object) {
    return Status::Error("Expected an object");
  }
  TRY_RESULT(extra_field, get_json_object_field(json_value.get_object(), "@extra", JsonValue::Type::Null, true));
  std::uint64_t extra_id = extra_id_.fetch_add(1, std::memory_order_relaxed);
  auto extra_str = json_encode<string>(extra_field);
  if (!extra_str.empty()) {
    std::lock_guard<std::mutex> guard(mutex_);
    extra_[extra_id] = std::move(extra_str);
  }

  td_api::object_ptr<td_api::Function> func;
  TRY_STATUS(from_json(func, json_value));
  return Client::Request{extra_id, std::move(func)};
}

std::string ClientJson::from_response(Client::Response response) {
  auto str = json_encode<string>(ToJson(static_cast<td_api::Object &>(*response.object)));
  CHECK(!str.empty() && str.back() == '}');
  std::string extra;
  if (response.id != 0) {
    std::lock_guard<std::mutex> guard(mutex_);
    auto it = extra_.find(response.id);
    if (it != extra_.end()) {
      extra = std::move(it->second);
      extra_.erase(it);
    }
  }
  if (!extra.empty()) {
    str.pop_back();
    str.reserve(str.size() + 10 + extra.size());
    str += ",\"@extra\":";
    str += extra;
    str += "}";
  }
  return str;
}

TD_THREAD_LOCAL std::string *ClientJson::current_output_;
CSlice ClientJson::store_string(std::string str) {
  init_thread_local<std::string>(ClientJson::current_output_);
  *current_output_ = std::move(str);
  return *current_output_;
}

void ClientJson::send(Slice request) {
  auto status = [&] {
    TRY_RESULT(client_request, to_request(request));
    client_.send(std::move(client_request));
    return Status::OK();
  }();

  LOG_IF(ERROR, status.is_error()) << "Failed to parse " << tag("request", format::escaped(request)) << " " << status;
}

CSlice ClientJson::receive(double timeout) {
  auto response = client_.receive(timeout);
  if (!response.object) {
    return {};
  }
  return store_string(from_response(std::move(response)));
}

CSlice ClientJson::execute(Slice request) {
  auto r_request = to_request(request);
  if (r_request.is_error()) {
    LOG(ERROR) << "Failed to parse " << tag("request", format::escaped(request)) << " " << r_request.error();
    return {};
  }

  return store_string(from_response(Client::execute(r_request.move_as_ok())));
}

}  // namespace td
