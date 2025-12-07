//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2025
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/BusinessConnectionId.h"
#include "td/telegram/DialogId.h"
#include "td/telegram/MessageFullId.h"
#include "td/telegram/StarGift.h"
#include "td/telegram/StarGiftAuctionState.h"
#include "td/telegram/StarGiftAuctionUserState.h"
#include "td/telegram/StarGiftCollectionId.h"
#include "td/telegram/StarGiftId.h"
#include "td/telegram/StarGiftResalePrice.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/UserId.h"

#include "td/actor/actor.h"
#include "td/actor/MultiTimeout.h"
#include "td/actor/Timeout.h"

#include "td/utils/common.h"
#include "td/utils/FlatHashMap.h"
#include "td/utils/FlatHashSet.h"
#include "td/utils/Promise.h"
#include "td/utils/Status.h"
#include "td/utils/WaitFreeHashMap.h"

#include <utility>

namespace td {

class StarGift;
class Td;

class StarGiftManager final : public Actor {
 public:
  StarGiftManager(Td *td, ActorShared<> parent);
  StarGiftManager(const StarGiftManager &) = delete;
  StarGiftManager &operator=(const StarGiftManager &) = delete;
  StarGiftManager(StarGiftManager &&) = delete;
  StarGiftManager &operator=(StarGiftManager &&) = delete;
  ~StarGiftManager() final;

  void get_gift_payment_options(Promise<td_api::object_ptr<td_api::availableGifts>> &&promise);

  void on_get_star_gift(const StarGift &star_gift, bool from_server);

  void can_send_gift(int64 gift_id, Promise<td_api::object_ptr<td_api::CanSendGiftResult>> &&promise);

  void send_gift(int64 gift_id, DialogId dialog_id, td_api::object_ptr<td_api::formattedText> text, bool is_private,
                 bool pay_for_upgrade, Promise<Unit> &&promise);

  void get_gift_auction_state(const string &auction_id,
                              Promise<td_api::object_ptr<td_api::giftAuctionState>> &&promise);

  void on_update_gift_auction_state(int64 gift_id,
                                    telegram_api::object_ptr<telegram_api::StarGiftAuctionState> &&state);

  void on_update_gift_auction_user_state(int64 gift_id,
                                         telegram_api::object_ptr<telegram_api::starGiftAuctionUserState> &&user_state);

  void get_gift_auction_acquired_gifts(int64 gift_id,
                                       Promise<td_api::object_ptr<td_api::giftAuctionAcquiredGifts>> &&promise);

  void open_gift_auction(int64 gift_id, bool is_recursive, Promise<Unit> &&promise);

  void close_gift_auction(int64 gift_id, Promise<Unit> &&promise);

  void place_gift_auction_bid(int64 gift_id, int64 star_count, UserId user_id,
                              td_api::object_ptr<td_api::formattedText> text, bool is_private, Promise<Unit> &&promise);

  void update_gift_auction_bid(int64 gift_id, int64 star_count, Promise<Unit> &&promise);

  void convert_gift(BusinessConnectionId business_connection_id, StarGiftId star_gift_id, Promise<Unit> &&promise);

  void save_gift(StarGiftId star_gift_id, bool is_saved, Promise<Unit> &&promise);

  void set_dialog_pinned_gifts(DialogId dialog_id, const vector<StarGiftId> &star_gift_ids, Promise<Unit> &&promise);

  void toggle_chat_star_gift_notifications(DialogId dialog_id, bool are_enabled, Promise<Unit> &&promise);

  void get_gift_upgrade_preview(int64 gift_id, Promise<td_api::object_ptr<td_api::giftUpgradePreview>> &&promise);

  void upgrade_gift(BusinessConnectionId business_connection_id, StarGiftId star_gift_id, bool keep_original_details,
                    int64 star_count, Promise<td_api::object_ptr<td_api::upgradeGiftResult>> &&promise);

  void buy_gift_upgrade(DialogId dialog_id, const string &prepaid_upgrade_hash, int64 star_count,
                        Promise<Unit> &&promise);

  void transfer_gift(BusinessConnectionId business_connection_id, StarGiftId star_gift_id, DialogId receiver_dialog_id,
                     int64 star_count, Promise<Unit> &&promise);

  void drop_gift_original_details(StarGiftId star_gift_id, int64 star_count, Promise<Unit> &&promise);

  void send_resold_gift(const string &gift_name, DialogId receiver_dialog_id, StarGiftResalePrice price,
                        Promise<td_api::object_ptr<td_api::GiftResaleResult>> &&promise);

  void get_saved_star_gifts(BusinessConnectionId business_connection_id, DialogId dialog_id,
                            StarGiftCollectionId collection_id, bool exclude_unsaved, bool exclude_saved,
                            bool exclude_unlimited, bool exclude_upgradable, bool exclude_non_upgradable,
                            bool exclude_unique, bool peer_color_available, bool exclude_hosted, bool sort_by_value,
                            const string &offset, int32 limit,
                            Promise<td_api::object_ptr<td_api::receivedGifts>> &&promise);

  void get_saved_star_gift(StarGiftId star_gift_id, Promise<td_api::object_ptr<td_api::receivedGift>> &&promise);

  void get_upgraded_gift(const string &name, Promise<td_api::object_ptr<td_api::upgradedGift>> &&promise);

  void get_upgraded_gift_value_info(const string &name,
                                    Promise<td_api::object_ptr<td_api::upgradedGiftValueInfo>> &&promise);

  void get_star_gift_withdrawal_url(StarGiftId star_gift_id, const string &password, Promise<string> &&promise);

  void set_star_gift_price(StarGiftId star_gift_id, StarGiftResalePrice price, Promise<Unit> &&promise);

  void get_resale_star_gifts(int64 gift_id, const td_api::object_ptr<td_api::GiftForResaleOrder> &order,
                             const vector<td_api::object_ptr<td_api::UpgradedGiftAttributeId>> &attributes,
                             const string &offset, int32 limit,
                             Promise<td_api::object_ptr<td_api::giftsForResale>> &&promise);

  void get_gift_collections(DialogId dialog_id, Promise<td_api::object_ptr<td_api::giftCollections>> &&promise);

  void create_gift_collection(DialogId dialog_id, const string &title, const vector<StarGiftId> &star_gift_ids,
                              Promise<td_api::object_ptr<td_api::giftCollection>> &&promise);

  void reorder_gift_collections(DialogId dialog_id, const vector<StarGiftCollectionId> &collection_ids,
                                Promise<Unit> &&promise);

  void delete_gift_collection(DialogId dialog_id, StarGiftCollectionId collection_id, Promise<Unit> &&promise);

  void set_gift_collection_title(DialogId dialog_id, StarGiftCollectionId collection_id, const string &title,
                                 Promise<td_api::object_ptr<td_api::giftCollection>> &&promise);

  void add_gift_collection_gifts(DialogId dialog_id, StarGiftCollectionId collection_id,
                                 const vector<StarGiftId> &star_gift_ids,
                                 Promise<td_api::object_ptr<td_api::giftCollection>> &&promise);

  void remove_gift_collection_gifts(DialogId dialog_id, StarGiftCollectionId collection_id,
                                    const vector<StarGiftId> &star_gift_ids,
                                    Promise<td_api::object_ptr<td_api::giftCollection>> &&promise);

  void reorder_gift_collection_gifts(DialogId dialog_id, StarGiftCollectionId collection_id,
                                     const vector<StarGiftId> &star_gift_ids,
                                     Promise<td_api::object_ptr<td_api::giftCollection>> &&promise);

  void register_gift(MessageFullId message_full_id, const char *source);

  void unregister_gift(MessageFullId message_full_id, const char *source);

 private:
  struct AuctionInfo {
    StarGift gift_;
    StarGiftAuctionState state_;
    StarGiftAuctionUserState user_state_;

    bool operator==(const AuctionInfo &other) const {
      return gift_ == other.gift_ && state_ == other.state_ && user_state_ == other.user_state_;
    }
  };

  void start_up() final;

  void tear_down() final;

  Status check_star_gift_id(const StarGiftId &star_gift_id, DialogId dialog_id) const;

  Status check_star_gift_ids(const vector<StarGiftId> &star_gift_ids, DialogId dialog_id) const;

  void send_get_star_gift_withdrawal_url_query(
      StarGiftId star_gift_id, telegram_api::object_ptr<telegram_api::InputCheckPasswordSRP> input_check_password,
      Promise<string> &&promise);

  double get_gift_message_polling_timeout() const;

  static void on_update_gift_message_timeout_callback(void *star_gift_manager_ptr, int64 message_number);

  void on_update_gift_message_timeout(int64 message_number);

  void on_update_gift_message(MessageFullId message_full_id);

  static void on_reload_gift_auction_timeout_callback(void *star_gift_manager_ptr, int64 gift_id);

  void on_reload_gift_auction_timeout(int64 gift_id);

  void on_online();

  void on_dialog_gift_transferred(DialogId from_dialog_id, DialogId to_dialog_id, Promise<Unit> &&promise);

  const AuctionInfo *get_auction_info(telegram_api::object_ptr<telegram_api::StarGift> &&star_gift,
                                      telegram_api::object_ptr<telegram_api::StarGiftAuctionState> &&state,
                                      telegram_api::object_ptr<telegram_api::starGiftAuctionUserState> &&user_state);

  void reload_gift_auction_state(telegram_api::object_ptr<telegram_api::InputStarGiftAuction> &&input_auction,
                                 int32 version, Promise<td_api::object_ptr<td_api::giftAuctionState>> &&promise);

  void on_get_auction_state(Result<telegram_api::object_ptr<telegram_api::payments_starGiftAuctionState>> r_state,
                            Promise<td_api::object_ptr<td_api::giftAuctionState>> &&promise);

  void schedule_active_gift_auctions_reload();

  static void reload_active_gift_auctions_static(void *td);

  void reload_active_gift_auctions();

  void on_get_active_gift_auctions(
      Result<telegram_api::object_ptr<telegram_api::payments_StarGiftActiveAuctions>> r_auctions);

  td_api::object_ptr<td_api::giftAuctionState> get_gift_auction_state_object(const AuctionInfo &info) const;

  void send_update_gift_auction_state(const AuctionInfo &info);

  void send_update_active_gift_auctions();

  void do_place_gift_auction_bid(int64 gift_id, int64 star_count, UserId user_id,
                                 td_api::object_ptr<td_api::formattedText> text, bool is_private,
                                 Promise<Unit> &&promise);

  void do_update_gift_auction_bid(int64 gift_id, int64 star_count, Promise<Unit> &&promise);

  Td *td_;
  ActorShared<> parent_;

  FlatHashMap<int64, std::pair<int64, int64>> gift_prices_;

  int64 gift_message_count_ = 0;
  WaitFreeHashMap<MessageFullId, int64, MessageFullIdHash> gift_message_full_ids_;
  WaitFreeHashMap<int64, MessageFullId> gift_message_full_ids_by_id_;
  FlatHashSet<MessageFullId, MessageFullIdHash> being_reloaded_gift_messages_;
  MultiTimeout update_gift_message_timeout_{"UpdateGiftMessageTimeout"};

  FlatHashMap<int64, AuctionInfo> gift_auction_infos_;
  vector<AuctionInfo> active_gift_auctions_;
  FlatHashMap<int64, int32> gift_auction_open_counts_;
  MultiTimeout reload_gift_auction_timeout_{"ReloadGiftAuctionTimeout"};

  Timeout active_gift_auctions_reload_timeout_;
};

}  // namespace td
