//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DialogId.h"
#include "td/telegram/files/FileSourceId.h"
#include "td/telegram/StarAmount.h"
#include "td/telegram/StarGiftResalePrice.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/TonAmount.h"
#include "td/telegram/UserId.h"

#include "td/actor/actor.h"

#include "td/utils/common.h"
#include "td/utils/FlatHashMap.h"
#include "td/utils/Promise.h"
#include "td/utils/Status.h"

namespace td {

class Td;

class StarManager final : public Actor {
 public:
  StarManager(Td *td, ActorShared<> parent);

  void on_update_owned_star_amount(StarAmount star_amount);

  void add_pending_owned_star_count(int64 star_count, bool move_to_owned);

  bool has_owned_star_count(int64 star_count) const;

  void on_update_owned_ton_amount(TonAmount ton_amount);

  void add_pending_owned_ton_count(int64 ton_count, bool move_to_owned);

  bool has_owned_ton_count(int64 ton_count) const;

  void add_pending_owned_amount(const StarGiftResalePrice &amount, int32 multiplier, bool move_to_owned);

  bool has_owned_amount(const StarGiftResalePrice &amount) const;

  void get_star_payment_options(Promise<td_api::object_ptr<td_api::starPaymentOptions>> &&promise);

  void get_star_gift_payment_options(UserId user_id, Promise<td_api::object_ptr<td_api::starPaymentOptions>> &&promise);

  void get_star_giveaway_payment_options(Promise<td_api::object_ptr<td_api::starGiveawayPaymentOptions>> &&promise);

  void get_star_transactions(td_api::object_ptr<td_api::MessageSender> owner_id, const string &subscription_id,
                             const string &offset, int32 limit,
                             td_api::object_ptr<td_api::TransactionDirection> &&direction,
                             Promise<td_api::object_ptr<td_api::starTransactions>> &&promise);

  void get_ton_transactions(const string &offset, int32 limit,
                            td_api::object_ptr<td_api::TransactionDirection> &&direction,
                            Promise<td_api::object_ptr<td_api::tonTransactions>> &&promise);

  void get_star_subscriptions(bool only_expiring, const string &offset,
                              Promise<td_api::object_ptr<td_api::starSubscriptions>> &&promise);

  void edit_star_subscription(const string &subscription_id, bool is_canceled, Promise<Unit> &&promise);

  void edit_user_star_subscription(UserId user_id, const string &telegram_payment_charge_id, bool is_canceled,
                                   Promise<Unit> &&promise);

  void reuse_star_subscription(const string &subscription_id, Promise<Unit> &&promise);

  void refund_star_payment(UserId user_id, const string &telegram_payment_charge_id, Promise<Unit> &&promise);

  void get_star_revenue_statistics(const td_api::object_ptr<td_api::MessageSender> &owner_id, bool is_dark,
                                   Promise<td_api::object_ptr<td_api::starRevenueStatistics>> &&promise);

  void get_star_withdrawal_url(const td_api::object_ptr<td_api::MessageSender> &owner_id, int64 star_count,
                               const string &password, Promise<string> &&promise);

  void get_star_ad_account_url(const td_api::object_ptr<td_api::MessageSender> &owner_id, Promise<string> &&promise);

  void get_ton_revenue_statistics(bool is_dark, Promise<td_api::object_ptr<td_api::tonRevenueStatistics>> &&promise);

  void get_ton_withdrawal_url(const string &password, Promise<string> &&promise);

  void get_paid_message_revenue(UserId user_id, Promise<td_api::object_ptr<td_api::starCount>> &&promise);

  void reload_star_transaction(DialogId dialog_id, const string &transaction_id, bool is_refund,
                               Promise<Unit> &&promise);

  void reload_owned_star_count();

  void reload_owned_ton_count();

  void on_update_stars_revenue_status(telegram_api::object_ptr<telegram_api::updateStarsRevenueStatus> &&update);

  FileSourceId get_star_transaction_file_source_id(DialogId dialog_id, const string &transaction_id, bool is_refund);

  static int64 get_star_count(int64 amount, bool allow_negative = false);

  static int32 get_nanostar_count(int64 &star_count, int32 nanostar_count);

  static int32 get_months_by_star_count(int64 star_count);

  static string get_unused_star_transaction_field(
      const telegram_api::object_ptr<telegram_api::starsTransaction> &transaction,
      const td_api::object_ptr<td_api::productInfo> &product_info, const string &bot_payload,
      const td_api::object_ptr<td_api::affiliateInfo> &affiliate, int32 commission_per_mille);

  void get_current_state(vector<td_api::object_ptr<td_api::Update>> &updates) const;

 private:
  void start_up() final;

  void tear_down() final;

  Status can_manage_stars(DialogId dialog_id, bool allow_self = false) const;

  void do_get_star_transactions(DialogId dialog_id, const string &subscription_id, const string &offset, int32 limit,
                                td_api::object_ptr<td_api::TransactionDirection> &&direction,
                                Promise<td_api::object_ptr<td_api::starTransactions>> &&promise);

  void do_get_ton_transactions(const string &offset, int32 limit,
                               td_api::object_ptr<td_api::TransactionDirection> &&direction,
                               Promise<td_api::object_ptr<td_api::tonTransactions>> &&promise);

  void get_withdrawal_url(DialogId dialog_id, bool is_ton, int64 amount, const string &password,
                          Promise<string> &&promise);
  void send_get_withdrawal_url_query(DialogId dialog_id, bool is_ton, int64 amount,
                                     telegram_api::object_ptr<telegram_api::InputCheckPasswordSRP> input_check_password,
                                     Promise<string> &&promise);

  td_api::object_ptr<td_api::updateOwnedStarCount> get_update_owned_star_count_object() const;

  td_api::object_ptr<td_api::updateOwnedTonCount> get_update_owned_ton_count_object() const;

  Td *td_;
  ActorShared<> parent_;

  bool is_owned_star_count_inited_ = false;
  int64 owned_star_count_ = 0;
  int32 owned_nanostar_count_ = 0;
  int64 pending_owned_star_count_ = 0;
  int64 sent_star_count_ = 0;
  int32 sent_nanostar_count_ = 0;

  bool is_owned_ton_count_inited_ = false;
  int64 owned_ton_count_ = 0;
  int64 pending_owned_ton_count_ = 0;
  int64 sent_ton_count_ = 0;

  FlatHashMap<DialogId, FlatHashMap<string, FileSourceId>, DialogIdHash> star_transaction_file_source_ids_[2];
};

}  // namespace td
