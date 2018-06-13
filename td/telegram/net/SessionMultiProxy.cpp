//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/net/SessionMultiProxy.h"

#include "td/telegram/net/SessionProxy.h"

#include "td/utils/format.h"
#include "td/utils/logging.h"
#include "td/utils/Slice.h"

namespace td {

SessionMultiProxy::SessionMultiProxy() = default;
SessionMultiProxy::~SessionMultiProxy() = default;

SessionMultiProxy::SessionMultiProxy(int32 session_count, std::shared_ptr<AuthDataShared> shared_auth_data,
                                     bool is_main, bool use_pfs, bool allow_media_only, bool is_media, bool is_cdn)
    : session_count_(session_count)
    , auth_data_(std::move(shared_auth_data))
    , is_main_(is_main)
    , use_pfs_(use_pfs)
    , allow_media_only_(allow_media_only)
    , is_media_(is_media)
    , is_cdn_(is_cdn) {
  if (allow_media_only_) {
    CHECK(is_media_);
  }
}

void SessionMultiProxy::send(NetQueryPtr query) {
  size_t pos = 0;
  // TODO temporary hack with total_timeout_limit
  if (query->auth_flag() == NetQuery::AuthFlag::On && query->total_timeout_limit > 50) {
    if (query->session_rand()) {
      pos = query->session_rand() % sessions_.size();
    } else {
      pos = pos_++ % sessions_.size();
    }
  }
  query->debug(PSTRING() << get_name() << ": send to proxy #" << pos);
  send_closure(sessions_[pos], &SessionProxy::send, std::move(query));
}

void SessionMultiProxy::update_main_flag(bool is_main) {
  LOG(INFO) << "Update " << get_name() << " is_main to " << is_main;
  is_main_ = is_main;
  for (auto &session : sessions_) {
    send_closure(session, &SessionProxy::update_main_flag, is_main);
  }
}
void SessionMultiProxy::update_session_count(int32 session_count) {
  update_options(session_count, use_pfs_);
}
void SessionMultiProxy::update_use_pfs(bool use_pfs) {
  update_options(session_count_, use_pfs);
}

void SessionMultiProxy::update_options(int32 session_count, bool use_pfs) {
  bool changed = false;

  if (session_count != session_count_) {
    session_count_ = session_count;
    if (session_count_ <= 0) {
      session_count_ = 1;
    }
    if (session_count_ > 100) {
      session_count_ = 100;
    }
    LOG(INFO) << "Update " << get_name() << " session_count to " << session_count_;
    changed = true;
  }

  if (use_pfs != use_pfs_) {
    bool old_pfs_flag = get_pfs_flag();
    use_pfs_ = use_pfs;
    if (old_pfs_flag != get_pfs_flag()) {
      LOG(INFO) << "Update " << get_name() << " use_pfs to " << use_pfs_;
      changed = true;
    }
  }
  if (changed) {
    init();
  }
}

void SessionMultiProxy::update_mtproto_header() {
  for (auto &session : sessions_) {
    send_closure_later(session, &SessionProxy::update_mtproto_header);
  }
}

void SessionMultiProxy::start_up() {
  init();
}

bool SessionMultiProxy::get_pfs_flag() const {
  return use_pfs_;
}

void SessionMultiProxy::init() {
  sessions_.clear();
  if (is_main_) {
    LOG(WARNING) << tag("session_count", session_count_);
  }
  for (int32 i = 0; i < session_count_; i++) {
    string name = PSTRING() << "Session" << get_name().substr(Slice("SessionMulti").size())
                            << format::cond(session_count_ > 1, format::concat("#", i));
    sessions_.push_back(create_actor<SessionProxy>(name, auth_data_, is_main_, allow_media_only_, is_media_,
                                                   get_pfs_flag(), is_main_ && i != 0, is_cdn_));
  }
}

}  // namespace td
