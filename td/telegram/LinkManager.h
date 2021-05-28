//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2021
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DialogId.h"
#include "td/telegram/MessageId.h"
#include "td/telegram/MessageLinkInfo.h"
#include "td/telegram/td_api.h"

#include "td/actor/actor.h"
#include "td/actor/PromiseFuture.h"

#include "td/utils/common.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"

#include <utility>

namespace td {

class Td;

class LinkManager : public Actor {
 public:
  LinkManager(Td *td, ActorShared<> parent);

  LinkManager(const LinkManager &) = delete;
  LinkManager &operator=(const LinkManager &) = delete;
  LinkManager(LinkManager &&) = delete;
  LinkManager &operator=(LinkManager &&) = delete;
  ~LinkManager() override;

  enum class InternalLinkType : int32 {
    AuthenticationCode,
    Background,
    DialogInvite,
    Message,
    MessageDraft,
    QrCodeAuthentication,
    StickerSet,
    UnknownDeepLink
  };

  class InternalLink {
   public:
    InternalLink() = default;
    InternalLink(const InternalLink &) = delete;
    InternalLink &operator=(const InternalLink &) = delete;
    InternalLink(InternalLink &&) = delete;
    InternalLink &operator=(InternalLink &&) = delete;
    virtual ~InternalLink() = default;

    virtual td_api::object_ptr<td_api::InternalLinkType> get_internal_link_type_object() const = 0;

    virtual InternalLinkType get_type() const = 0;
  };

  // checks whether the link is a valid tg, ton or HTTP(S) URL and returns it in a canonical form
  static Result<string> check_link(Slice link);

  // checks whether the link is a supported tg or t.me URL and parses it
  static unique_ptr<InternalLink> parse_internal_link(Slice link);

  void get_login_url_info(DialogId dialog_id, MessageId message_id, int32 button_id,
                          Promise<td_api::object_ptr<td_api::LoginUrlInfo>> &&promise);

  void get_login_url(DialogId dialog_id, MessageId message_id, int32 button_id, bool allow_write_access,
                     Promise<td_api::object_ptr<td_api::httpUrl>> &&promise);

  void get_link_login_url_info(const string &url, Promise<td_api::object_ptr<td_api::LoginUrlInfo>> &&promise);

  void get_link_login_url(const string &url, bool allow_write_access,
                          Promise<td_api::object_ptr<td_api::httpUrl>> &&promise);

  static string get_dialog_invite_link_hash(Slice invite_link);

  static Result<MessageLinkInfo> get_message_link_info(Slice url);

 private:
  void tear_down() override;

  class InternalLinkAuthenticationCode;
  class InternalLinkBackground;
  class InternalLinkDialogInvite;
  class InternalLinkMessage;
  class InternalLinkMessageDraft;
  class InternalLinkQrCodeAuthentication;
  class InternalLinkStickerSet;
  class InternalLinkUnknownDeepLink;

  struct LinkInfo {
    bool is_internal_ = false;
    bool is_tg_ = false;
    string query_;
  };
  // returns information about the link
  static LinkInfo get_link_info(Slice link);

  static unique_ptr<InternalLink> parse_tg_link_query(Slice query);

  static unique_ptr<InternalLink> parse_t_me_link_query(Slice query);

  static unique_ptr<InternalLink> get_internal_link_message_draft(Slice url, Slice text);

  Td *td_;
  ActorShared<> parent_;
};

}  // namespace td
