//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/TranslationManager.h"

#include "td/telegram/Global.h"
#include "td/telegram/Td.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/buffer.h"
#include "td/utils/Status.h"

namespace td {

class TranslateTextQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::text>> promise_;

 public:
  explicit TranslateTextQuery(Promise<td_api::object_ptr<td_api::text>> &&promise) : promise_(std::move(promise)) {
  }

  void send(const string &text, const string &from_language_code, const string &to_language_code) {
    int flags = telegram_api::messages_translateText::TEXT_MASK;
    vector<telegram_api::object_ptr<telegram_api::textWithEntities>> texts;
    texts.push_back(telegram_api::make_object<telegram_api::textWithEntities>(text, Auto()));
    send_query(G()->net_query_creator().create(
        telegram_api::messages_translateText(flags, nullptr, vector<int32>{}, std::move(texts), to_language_code)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::messages_translateText>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for TranslateTextQuery: " << to_string(ptr);
    if (ptr->result_.empty()) {
      return promise_.set_value(nullptr);
    }
    promise_.set_value(td_api::make_object<td_api::text>(ptr->result_[0]->text_));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

TranslationManager::TranslationManager(Td *td, ActorShared<> parent) : td_(td), parent_(std::move(parent)) {
}

void TranslationManager::tear_down() {
  parent_.reset();
}

void TranslationManager::translate_text(const string &text, const string &from_language_code,
                                        const string &to_language_code,
                                        Promise<td_api::object_ptr<td_api::text>> &&promise) {
  td_->create_handler<TranslateTextQuery>(std::move(promise))->send(text, from_language_code, to_language_code);
}

}  // namespace td
