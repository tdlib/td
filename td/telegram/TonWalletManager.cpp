//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/TonWalletManager.h"

#include "td/telegram/Global.h"
#include "td/telegram/misc.h"
#include "td/telegram/Td.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/buffer.h"

namespace td {

class PerformTonCenterApiRequestQuery final : public Td::ResultHandler {
  Promise<string> promise_;

 public:
  explicit PerformTonCenterApiRequestQuery(Promise<string> &&promise) : promise_(std::move(promise)) {
  }

  void send(const string &endpoint, bool is_post, const string &query, const string &payload) {
    int32 flags = 0;
    if (!query.empty()) {
      flags |= telegram_api::toncenter_performApiRequest::QUERY_MASK;
    }
    if (!payload.empty()) {
      flags |= telegram_api::toncenter_performApiRequest::PAYLOAD_MASK;
    }
    send_query(G()->net_query_creator().create(
        telegram_api::toncenter_performApiRequest(flags, is_post, endpoint, query, payload), {},
        G()->get_webfile_dc_id()));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::toncenter_performApiRequest>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto result = result_ptr.move_as_ok();
    promise_.set_value(std::move(result->response_->data_));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class GetTonCenterStreamingApiUrlQuery final : public Td::ResultHandler {
  Promise<telegram_api::object_ptr<telegram_api::toncenter_streamingUrl>> promise_;

 public:
  explicit GetTonCenterStreamingApiUrlQuery(
      Promise<telegram_api::object_ptr<telegram_api::toncenter_streamingUrl>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send() {
    send_query(G()->net_query_creator().create(telegram_api::toncenter_getStreamingUrl()));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::toncenter_getStreamingUrl>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto result = result_ptr.move_as_ok();
    promise_.set_value(std::move(result));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

TonWalletManager::TonWalletManager(Td *td, ActorShared<> parent) : td_(td), parent_(std::move(parent)) {
}

void TonWalletManager::tear_down() {
  parent_.reset();
}

void TonWalletManager::perform_ton_center_api_request(const string &endpoint,
                                                      td_api::object_ptr<td_api::TonCenterApiRequestType> &&type,
                                                      Promise<string> &&promise) {
  if (type == nullptr) {
    return promise.set_error(400, "Item type must be non-empty");
  }
  switch (type->get_id()) {
    case td_api::tonCenterApiRequestTypeGet::ID: {
      auto get = td_api::move_object_as<td_api::tonCenterApiRequestTypeGet>(type);
      if (!clean_input_string(get->query_)) {
        return promise.set_error(400, "Query must be encoded in UTF-8");
      }
      td_->create_handler<PerformTonCenterApiRequestQuery>(std::move(promise))
          ->send(endpoint, false, get->query_, string());
      break;
    }
    case td_api::tonCenterApiRequestTypePost::ID: {
      auto post = td_api::move_object_as<td_api::tonCenterApiRequestTypePost>(type);
      if (!clean_input_string(post->payload_)) {
        return promise.set_error(400, "Phone number must be encoded in UTF-8");
      }
      td_->create_handler<PerformTonCenterApiRequestQuery>(std::move(promise))
          ->send(endpoint, true, string(), post->payload_);
      break;
    }
    default:
      UNREACHABLE();
  }
}

void TonWalletManager::get_ton_center_streaming_api_url(
    Promise<td_api::object_ptr<td_api::tonCenterStreamingApiUrl>> &&promise) {
  auto cache_expires_in = streaming_api_url_.expires_at_ - G()->unix_time();
  if (cache_expires_in >= 60) {
    return promise.set_value(
        td_api::make_object<td_api::tonCenterStreamingApiUrl>(streaming_api_url_.url_, cache_expires_in));
  }
  get_streaming_api_url_queries_.push_back(std::move(promise));
  if (get_streaming_api_url_queries_.size() == 1u) {
    auto query_promise = PromiseCreator::lambda(
        [actor_id = actor_id(this)](Result<telegram_api::object_ptr<telegram_api::toncenter_streamingUrl>> r_url) {
          send_closure(actor_id, &TonWalletManager::on_get_ton_center_streaming_api_url, std::move(r_url));
        });
    td_->create_handler<GetTonCenterStreamingApiUrlQuery>(std::move(query_promise))->send();
  }
}

void TonWalletManager::on_get_ton_center_streaming_api_url(
    Result<telegram_api::object_ptr<telegram_api::toncenter_streamingUrl>> r_url) {
  G()->ignore_result_if_closing(r_url);
  auto promises = std::move(get_streaming_api_url_queries_);
  CHECK(!promises.empty());
  get_streaming_api_url_queries_.clear();

  if (r_url.is_error()) {
    fail_promises(promises, r_url.move_as_error());
    return;
  }
  auto url = r_url.move_as_ok();

  auto expires_in = url->expires_ - G()->unix_time();
  if (expires_in <= 0 || url->url_.empty()) {
    LOG(ERROR) << "Receive " << to_string(url);
    fail_promises(promises, Status::Error(500, "Receive invalid response"));
    return;
  }
  streaming_api_url_.url_ = url->url_;
  streaming_api_url_.expires_at_ = url->expires_;
  for (auto &promise : promises) {
    promise.set_value(td_api::make_object<td_api::tonCenterStreamingApiUrl>(url->url_, expires_in));
  }
}

}  // namespace td
