//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/FullMessageId.h"
#include "td/telegram/MessageLinkInfo.h"
#include "td/telegram/td_api.h"
#include "td/telegram/UserId.h"

#include "td/actor/actor.h"

#include "td/utils/common.h"
#include "td/utils/Promise.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"

#include <utility>

namespace td {

class Td;

class LinkManager final : public Actor {
 public:
  LinkManager(Td *td, ActorShared<> parent);

  LinkManager(const LinkManager &) = delete;
  LinkManager &operator=(const LinkManager &) = delete;
  LinkManager(LinkManager &&) = delete;
  LinkManager &operator=(LinkManager &&) = delete;
  ~LinkManager() final;

  class InternalLink {
   public:
    InternalLink() = default;
    InternalLink(const InternalLink &) = delete;
    InternalLink &operator=(const InternalLink &) = delete;
    InternalLink(InternalLink &&) = delete;
    InternalLink &operator=(InternalLink &&) = delete;
    virtual ~InternalLink() = default;

    virtual td_api::object_ptr<td_api::InternalLinkType> get_internal_link_type_object() const = 0;
  };

  // checks whether the link is a valid tg, ton or HTTP(S) URL and returns it in a canonical form
  static Result<string> check_link(CSlice link, bool http_only = false, bool https_only = false);

  // same as check_link, but returns an empty string instead of an error
  static string get_checked_link(Slice link, bool http_only = false, bool https_only = false);

  // checks whether the link is a supported tg or t.me link and parses it
  static unique_ptr<InternalLink> parse_internal_link(Slice link, bool is_trusted = false);

  void update_autologin_domains(string autologin_token, vector<string> autologin_domains,
                                vector<string> url_auth_domains);

  void get_deep_link_info(Slice link, Promise<td_api::object_ptr<td_api::deepLinkInfo>> &&promise);

  void get_external_link_info(string &&link, Promise<td_api::object_ptr<td_api::LoginUrlInfo>> &&promise);

  void get_login_url_info(FullMessageId full_message_id, int64 button_id,
                          Promise<td_api::object_ptr<td_api::LoginUrlInfo>> &&promise);

  void get_login_url(FullMessageId full_message_id, int64 button_id, bool allow_write_access,
                     Promise<td_api::object_ptr<td_api::httpUrl>> &&promise);

  void get_link_login_url(const string &url, bool allow_write_access,
                          Promise<td_api::object_ptr<td_api::httpUrl>> &&promise);

  static string get_dialog_invite_link_hash(Slice invite_link);

  static string get_dialog_invite_link(Slice hash, bool is_internal);

  static UserId get_link_user_id(Slice url);

  static Result<MessageLinkInfo> get_message_link_info(Slice url);

 private:
  void start_up() final;

  void tear_down() final;

  class InternalLinkActiveSessions;
  class InternalLinkAttachMenuBot;
  class InternalLinkAuthenticationCode;
  class InternalLinkBackground;
  class InternalLinkBotAddToChannel;
  class InternalLinkBotStart;
  class InternalLinkBotStartInGroup;
  class InternalLinkChangePhoneNumber;
  class InternalLinkConfirmPhone;
  class InternalLinkDialogInvite;
  class InternalLinkFilterSettings;
  class InternalLinkGame;
  class InternalLinkInvoice;
  class InternalLinkLanguage;
  class InternalLinkLanguageSettings;
  class InternalLinkMessage;
  class InternalLinkMessageDraft;
  class InternalLinkPassportDataRequest;
  class InternalLinkPremiumFeatures;
  class InternalLinkPrivacyAndSecuritySettings;
  class InternalLinkProxy;
  class InternalLinkPublicDialog;
  class InternalLinkQrCodeAuthentication;
  class InternalLinkSettings;
  class InternalLinkStickerSet;
  class InternalLinkTheme;
  class InternalLinkThemeSettings;
  class InternalLinkUnknownDeepLink;
  class InternalLinkUnsupportedProxy;
  class InternalLinkUserPhoneNumber;
  class InternalLinkVoiceChat;

  struct LinkInfo {
    bool is_internal_ = false;
    bool is_tg_ = false;
    string query_;
  };
  // returns information about the link
  static LinkInfo get_link_info(Slice link);

  static unique_ptr<InternalLink> parse_tg_link_query(Slice query, bool is_trusted);

  static unique_ptr<InternalLink> parse_t_me_link_query(Slice query, bool is_trusted);

  static unique_ptr<InternalLink> get_internal_link_passport(Slice query,
                                                             const vector<std::pair<string, string>> &args);

  static unique_ptr<InternalLink> get_internal_link_message_draft(Slice url, Slice text);

  static Result<string> check_link_impl(Slice link, bool http_only, bool https_only);

  Td *td_;
  ActorShared<> parent_;

  string autologin_token_;
  vector<string> autologin_domains_;
  double autologin_update_time_ = 0.0;
  vector<string> url_auth_domains_;
};

}  // namespace td
