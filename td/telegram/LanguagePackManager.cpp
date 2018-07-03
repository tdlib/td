//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/LanguagePackManager.h"

#include "td/telegram/Global.h"
#include "td/telegram/logevent/LogEvent.h"
#include "td/telegram/net/NetQueryDispatcher.h"

#include "td/utils/logging.h"
#include "td/utils/misc.h"

#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

namespace td {

void LanguagePackManager::get_languages(Promise<td_api::object_ptr<td_api::languagePack>> promise) {
  auto request_promise = PromiseCreator::lambda([promise = std::move(promise)](Result<NetQueryPtr> r_query) mutable {
    auto r_result = fetch_result<telegram_api::langpack_getLanguages>(std::move(r_query));
    if (r_result.is_error()) {
      return promise.set_error(r_result.move_as_error());
    }

    auto languages = r_result.move_as_ok();
    auto results = make_tl_object<td_api::languagePack>();
    results->languages_.reserve(languages.size());
    for (auto &language : languages) {
      results->languages_.push_back(
          make_tl_object<td_api::languageInfo>(language->lang_code_, language->name_, language->native_name_));
    }
    promise.set_value(std::move(results));
  });
  send_with_promise(G()->net_query_creator().create(create_storer(telegram_api::langpack_getLanguages())),
                    std::move(request_promise));
}

void LanguagePackManager::get_language_pack_strings(string language_code, vector<string> keys,
                                                    Promise<td_api::object_ptr<td_api::languagePackStrings>> promise) {
  bool is_all = keys.empty();
  auto result_promise =
      PromiseCreator::lambda([actor_id = actor_id(this), is_all, promise = std::move(promise)](
                                 Result<vector<tl_object_ptr<telegram_api::LangPackString>>> r_result) mutable {
        send_closure(actor_id, &LanguagePackManager::on_get_language_pack_strings, std::move(r_result), is_all,
                     std::move(promise));
      });

  if (is_all) {
    auto request_promise =
        PromiseCreator::lambda([promise = std::move(result_promise)](Result<NetQueryPtr> r_query) mutable {
          auto r_result = fetch_result<telegram_api::langpack_getLangPack>(std::move(r_query));
          if (r_result.is_error()) {
            return promise.set_error(r_result.move_as_error());
          }

          auto result = r_result.move_as_ok();
          LOG(INFO) << "Receive language pack for language " << result->lang_code_ << " from version "
                    << result->from_version_ << " with version " << result->version_ << " of size "
                    << result->strings_.size();
          promise.set_value(std::move(result->strings_));
        });
    send_with_promise(G()->net_query_creator().create(create_storer(telegram_api::langpack_getLangPack(language_code))),
                      std::move(request_promise));
  } else {
    auto request_promise =
        PromiseCreator::lambda([promise = std::move(result_promise)](Result<NetQueryPtr> r_query) mutable {
          auto r_result = fetch_result<telegram_api::langpack_getStrings>(std::move(r_query));
          if (r_result.is_error()) {
            return promise.set_error(r_result.move_as_error());
          }

          promise.set_value(r_result.move_as_ok());
        });
    send_with_promise(G()->net_query_creator().create(
                          create_storer(telegram_api::langpack_getStrings(language_code, std::move(keys)))),
                      std::move(request_promise));
  }
}

void LanguagePackManager::on_get_language_pack_strings(
    Result<vector<tl_object_ptr<telegram_api::LangPackString>>> r_result, bool ia_all,
    Promise<td_api::object_ptr<td_api::languagePackStrings>> promise) {
  if (r_result.is_error()) {
    return promise.set_error(r_result.move_as_error());
  }
  auto result =
      transform(r_result.move_as_ok(), [](const auto &string_ptr) -> tl_object_ptr<td_api::LanguagePackString> {
        CHECK(string_ptr != nullptr);
        switch (string_ptr->get_id()) {
          case telegram_api::langPackString::ID: {
            auto str = static_cast<const telegram_api::langPackString *>(string_ptr.get());
            return make_tl_object<td_api::languagePackStringValue>(str->key_, str->value_);
          }
          case telegram_api::langPackStringPluralized::ID: {
            auto str = static_cast<const telegram_api::langPackStringPluralized *>(string_ptr.get());
            return make_tl_object<td_api::languagePackStringPluralized>(str->key_, str->zero_value_, str->one_value_,
                                                                        str->two_value_, str->few_value_,
                                                                        str->many_value_, str->other_value_);
          }
          case telegram_api::langPackStringDeleted::ID: {
            auto str = static_cast<const telegram_api::langPackStringDeleted *>(string_ptr.get());
            return make_tl_object<td_api::languagePackStringDeleted>(str->key_);
          }
          default:
            UNREACHABLE();
            return nullptr;
        }
      });

  promise.set_value(make_tl_object<td_api::languagePackStrings>(std::move(result)));
}

void LanguagePackManager::on_result(NetQueryPtr query) {
  auto token = get_link_token();
  container_.extract(token).set_value(std::move(query));
}

void LanguagePackManager::send_with_promise(NetQueryPtr query, Promise<NetQueryPtr> promise) {
  auto id = container_.create(std::move(promise));
  G()->net_query_dispatcher().dispatch_with_callback(std::move(query), actor_shared(this, id));
}

void LanguagePackManager::hangup() {
  container_.for_each(
      [](auto id, Promise<NetQueryPtr> &promise) { promise.set_error(Status::Error(500, "Request aborted")); });
  stop();
}

}  // namespace td
