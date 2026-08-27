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

#include "td/utils/algorithm.h"
#include "td/utils/buffer.h"

namespace td {

class GetCurrencyRatesQuery final : public Td::ResultHandler {
  Promise<telegram_api::object_ptr<telegram_api::payments_currencyRates>> promise_;

 public:
  explicit GetCurrencyRatesQuery(Promise<telegram_api::object_ptr<telegram_api::payments_currencyRates>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send() {
    send_query(G()->net_query_creator().create(telegram_api::payments_getCurrencyRates()));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::payments_getCurrencyRates>(packet);
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

class GetOnRampProvidersQuery final : public Td::ResultHandler {
  Promise<vector<telegram_api::object_ptr<telegram_api::onrampProviderInfo>>> promise_;

 public:
  explicit GetOnRampProvidersQuery(
      Promise<vector<telegram_api::object_ptr<telegram_api::onrampProviderInfo>>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(const string &cryptocurrency) {
    int32 flags = 0;
    if (!cryptocurrency.empty()) {
      flags |= telegram_api::payments_getOnrampProviders::CRYPTO_CURRENCY_MASK;
    }
    send_query(G()->net_query_creator().create(telegram_api::payments_getOnrampProviders(flags, cryptocurrency)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::payments_getOnrampProviders>(packet);
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

class GetOnRampBaseCurrenciesQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::currencies>> promise_;

 public:
  explicit GetOnRampBaseCurrenciesQuery(Promise<td_api::object_ptr<td_api::currencies>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(const string &provider, const string &cryptocurrency) {
    send_query(
        G()->net_query_creator().create(telegram_api::payments_getOnrampBaseCurrencies(provider, cryptocurrency)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::payments_getOnrampBaseCurrencies>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto result = result_ptr.move_as_ok();
    promise_.set_value(td_api::make_object<td_api::currencies>(std::move(result)));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class GetOnRampAvailabilityQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::onRampPaymentAvailability>> promise_;

 public:
  explicit GetOnRampAvailabilityQuery(Promise<td_api::object_ptr<td_api::onRampPaymentAvailability>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(const string &provider, const string &cryptocurrency, const string &base_currency) {
    int32 flags = 0;
    if (!base_currency.empty()) {
      flags |= telegram_api::payments_getOnrampAvailability::BASE_CURRENCY_MASK;
    }
    send_query(G()->net_query_creator().create(
        telegram_api::payments_getOnrampAvailability(flags, provider, cryptocurrency, base_currency)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::payments_getOnrampAvailability>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto result = result_ptr.move_as_ok();
    vector<td_api::object_ptr<td_api::onRampPaymentMethod>> methods;
    for (auto &method : result->methods_) {
      methods.push_back(td_api::make_object<td_api::onRampPaymentMethod>(method->payment_method_, method->available_));
    }
    promise_.set_value(td_api::make_object<td_api::onRampPaymentAvailability>(
        result->allowed_, result->buy_allowed_, std::move(methods), result->country_code_, result->state_));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

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

TonWalletManager::OnRampProvider::OnRampProvider(telegram_api::object_ptr<telegram_api::onrampProviderInfo> &&info)
    : id_(std::move(info->id_))
    , name_(std::move(info->name_))
    , cryptocurrencies_(std::move(info->crypto_currencies_))
    , supports_base_currencies_(info->supports_base_currencies_)
    , supports_limits_(info->supports_limits_)
    , supports_quote_(info->supports_quote_) {
}

td_api::object_ptr<td_api::onRampProvider> TonWalletManager::OnRampProvider::get_on_ramp_provider_object() const {
  return td_api::make_object<td_api::onRampProvider>(id_, name_, vector<string>(cryptocurrencies_),
                                                     supports_base_currencies_, supports_limits_, supports_quote_);
}

TonWalletManager::TonWalletManager(Td *td, ActorShared<> parent) : td_(td), parent_(std::move(parent)) {
}

void TonWalletManager::tear_down() {
  parent_.reset();
}

td_api::object_ptr<td_api::currencyExchangeRates> TonWalletManager::get_currency_exchange_rates_object() const {
  return td_api::make_object<td_api::currencyExchangeRates>(
      transform(currency_rates_.rates_, [](const CurrencyRate &rate) {
        return td_api::make_object<td_api::currencyExchangeRate>(rate.currency_, rate.rate_);
      }));
}

void TonWalletManager::get_currency_rates(Promise<td_api::object_ptr<td_api::currencyExchangeRates>> &&promise) {
  if (Time::now() < currency_rates_.expires_at_) {
    return promise.set_value(get_currency_exchange_rates_object());
  }
  get_currency_rates_queries_.push_back(std::move(promise));
  if (get_currency_rates_queries_.size() == 1u) {
    auto query_promise = PromiseCreator::lambda(
        [actor_id = actor_id(this)](Result<telegram_api::object_ptr<telegram_api::payments_currencyRates>> r_rates) {
          send_closure(actor_id, &TonWalletManager::on_get_currency_rates, std::move(r_rates));
        });
    td_->create_handler<GetCurrencyRatesQuery>(std::move(query_promise))->send();
  }
}

void TonWalletManager::on_get_currency_rates(
    Result<telegram_api::object_ptr<telegram_api::payments_currencyRates>> r_rates) {
  G()->ignore_result_if_closing(r_rates);
  auto promises = std::move(get_currency_rates_queries_);
  CHECK(!promises.empty());
  get_currency_rates_queries_.clear();

  if (r_rates.is_error()) {
    return fail_promises(promises, r_rates.move_as_error());
  }
  auto rates = r_rates.move_as_ok();
  currency_rates_.rates_.clear();
  for (auto &rate : rates->rates_) {
    CurrencyRate currency_rate;
    currency_rate.currency_ = std::move(rate->currency_);
    currency_rate.rate_ = rate->rate_;
    currency_rates_.rates_.push_back(std::move(currency_rate));
  }
  currency_rates_.expires_at_ = Time::now() + 60;
  for (auto &promise : promises) {
    promise.set_value(get_currency_exchange_rates_object());
  }
}

void TonWalletManager::get_on_ramp_providers(const string &cryptocurrency,
                                             Promise<td_api::object_ptr<td_api::onRampProviders>> &&promise) {
  auto query_promise = PromiseCreator::lambda(
      [actor_id = actor_id(this), promise = std::move(promise)](
          Result<vector<telegram_api::object_ptr<telegram_api::onrampProviderInfo>>> r_providers) mutable {
        send_closure(actor_id, &TonWalletManager::on_get_on_ramp_providers, std::move(r_providers), std::move(promise));
      });
  td_->create_handler<GetOnRampProvidersQuery>(std::move(query_promise))->send(cryptocurrency);
}

void TonWalletManager::on_get_on_ramp_providers(
    Result<vector<telegram_api::object_ptr<telegram_api::onrampProviderInfo>>> r_providers,
    Promise<td_api::object_ptr<td_api::onRampProviders>> &&promise) {
  G()->ignore_result_if_closing(r_providers);
  if (r_providers.is_error()) {
    return promise.set_error(r_providers.move_as_error());
  }
  auto providers = r_providers.move_as_ok();
  vector<td_api::object_ptr<td_api::onRampProvider>> on_ramp_providers;
  for (auto &provider : providers) {
    auto on_ramp_provider = OnRampProvider(std::move(provider));
    on_ramp_providers.push_back(on_ramp_provider.get_on_ramp_provider_object());
  }
  promise.set_value(td_api::make_object<td_api::onRampProviders>(std::move(on_ramp_providers)));
}

void TonWalletManager::get_on_ramp_base_currencies(const string &provider, const string &cryptocurrency,
                                                   Promise<td_api::object_ptr<td_api::currencies>> &&promise) {
  td_->create_handler<GetOnRampBaseCurrenciesQuery>(std::move(promise))->send(provider, cryptocurrency);
}

void TonWalletManager::get_on_ramp_availability(
    const string &provider, const string &cryptocurrency, const string &base_currency,
    Promise<td_api::object_ptr<td_api::onRampPaymentAvailability>> &&promise) {
  td_->create_handler<GetOnRampAvailabilityQuery>(std::move(promise))->send(provider, cryptocurrency, base_currency);
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
  auto cache_expires_in = streaming_api_url_.expiration_date_ - G()->unix_time();
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
    return fail_promises(promises, r_url.move_as_error());
  }
  auto url = r_url.move_as_ok();

  auto expires_in = url->expires_ - G()->unix_time();
  if (expires_in <= 0 || url->url_.empty()) {
    LOG(ERROR) << "Receive " << to_string(url);
    fail_promises(promises, Status::Error(500, "Receive invalid response"));
    return;
  }
  streaming_api_url_.url_ = url->url_;
  streaming_api_url_.expiration_date_ = url->expires_;
  for (auto &promise : promises) {
    promise.set_value(td_api::make_object<td_api::tonCenterStreamingApiUrl>(url->url_, expires_in));
  }
}

}  // namespace td
