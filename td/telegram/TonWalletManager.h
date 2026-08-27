//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/actor/actor.h"

#include "td/utils/common.h"
#include "td/utils/Promise.h"

namespace td {

class Td;

class TonWalletManager final : public Actor {
 public:
  TonWalletManager(Td *td, ActorShared<> parent);

  void get_currency_rates(Promise<td_api::object_ptr<td_api::currencyExchangeRates>> &&promise);

  void get_on_ramp_providers(const string &cryptocurrency,
                             Promise<td_api::object_ptr<td_api::onRampProviders>> &&promise);

  void perform_ton_center_api_request(const string &endpoint,
                                      td_api::object_ptr<td_api::TonCenterApiRequestType> &&type,
                                      Promise<string> &&promise);

  void get_ton_center_streaming_api_url(Promise<td_api::object_ptr<td_api::tonCenterStreamingApiUrl>> &&promise);

 private:
  void tear_down() final;

  void on_get_currency_rates(Result<telegram_api::object_ptr<telegram_api::payments_currencyRates>> r_rates);

  td_api::object_ptr<td_api::currencyExchangeRates> get_currency_exchange_rates_object() const;

  void on_get_on_ramp_providers(Result<vector<telegram_api::object_ptr<telegram_api::onrampProviderInfo>>> r_providers,
                                Promise<td_api::object_ptr<td_api::onRampProviders>> &&promise);

  void on_get_ton_center_streaming_api_url(
      Result<telegram_api::object_ptr<telegram_api::toncenter_streamingUrl>> r_url);

  Td *td_;
  ActorShared<> parent_;

  struct CurrencyRate {
    string currency_;
    double rate_ = 0.0;
  };
  struct CurrencyRates {
    vector<CurrencyRate> rates_;
    double expires_at_ = 0.0;
  };
  CurrencyRates currency_rates_;
  vector<Promise<td_api::object_ptr<td_api::currencyExchangeRates>>> get_currency_rates_queries_;

  class OnRampProvider {
    string id_;
    string name_;
    vector<string> cryptocurrencies_;
    bool supports_base_currencies_ = false;
    bool supports_limits_ = false;
    bool supports_quote_ = false;

   public:
    OnRampProvider(telegram_api::object_ptr<telegram_api::onrampProviderInfo> &&info);

    td_api::object_ptr<td_api::onRampProvider> get_on_ramp_provider_object() const;
  };

  struct StreamingApiUrl {
    string url_;
    int32 expiration_date_ = 0;
  };
  StreamingApiUrl streaming_api_url_;
  vector<Promise<td_api::object_ptr<td_api::tonCenterStreamingApiUrl>>> get_streaming_api_url_queries_;
};

}  // namespace td
