//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/net/SessionMultiProxy.h"

#include "td/telegram/net/SessionProxy.h"

#include "td/utils/common.h"
#include "td/utils/format.h"
#include "td/utils/logging.h"
#include "td/utils/Slice.h"

#include <algorithm>

namespace td {

SessionMultiProxy::SessionMultiProxy() = default;
SessionMultiProxy::~SessionMultiProxy() = default;

SessionMultiProxy::SessionMultiProxy(int32 session_count, std::shared_ptr<AuthDataShared> shared_auth_data,
                                     bool is_main, bool use_pfs, bool allow_media_only, bool is_media, bool is_cdn,
                                     bool need_destroy_auth_key)
    : session_count_(session_count)
    , auth_data_(std::move(shared_auth_data))
    , is_main_(is_main)
    , use_pfs_(use_pfs)
    , allow_media_only_(allow_media_only)
    , is_media_(is_media)
    , is_cdn_(is_cdn)
    , need_destroy_auth_key_(need_destroy_auth_key) {
  if (allow_media_only_) {
    CHECK(is_media_);
  }
}

void SessionMultiProxy::send(NetQueryPtr query) {
  size_t pos = 0;
  // TODO temporary hack with total_timeout_limit
  if (query->auth_flag() == NetQuery::AuthFlag::On && query->total_timeout_limit_ > 7) {
    if (query->session_rand()) {
      pos = query->session_rand() % sessions_.size();
    } else {
      pos = std::min_element(sessions_.begin(), sessions_.end(),
                             [](const auto &a, const auto &b) { return a.queries_count < b.queries_count; }) -
            sessions_.begin();
    }
  }
  // query->debug(PSTRING() << get_name() << ": send to proxy #" << pos);
  sessions_[pos].queries_count++;
  send_closure(sessions_[pos].proxy, &SessionProxy::send, std::move(query));
}

void SessionMultiProxy::update_main_flag(bool is_main) {
  LOG(INFO) << "Update " << get_name() << " is_main to " << is_main;
  is_main_ = is_main;
  for (auto &session : sessions_) {
    send_closure(session.proxy, &SessionProxy::update_main_flag, is_main);
  }
}

void SessionMultiProxy::update_destroy_auth_key(bool need_destroy_auth_key) {
  need_destroy_auth_key_ = need_destroy_auth_key;
  send_closure(sessions_[0].proxy, &SessionProxy::update_destroy, need_destroy_auth_key_);
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
    send_closure_later(session.proxy, &SessionProxy::update_mtproto_header);
  }
}

void SessionMultiProxy::start_up() {
  init();
}

bool SessionMultiProxy::get_pfs_flag() const {
  return use_pfs_ && !is_cdn_;
}

void SessionMultiProxy::init() {
  sessions_generation_++;
  sessions_.clear();
  if (is_main_ && session_count_ > 1) {
    LOG(WARNING) << tag("session_count", session_count_);
  }
  for (int32 i = 0; i < session_count_; i++) {
    string name = PSTRING() << "Session" << get_name().substr(Slice("SessionMulti").size())
                            << format::cond(session_count_ > 1, format::concat("#", i));

    SessionInfo info;
    class Callback : public SessionProxy::Callback {
     public:
      Callback(ActorId<SessionMultiProxy> parent, uint32 generation, int32 session_id)
          : parent_(parent), generation_(generation), session_id_(session_id) {
      }
      void on_query_finished() override {
        send_closure(parent_, &SessionMultiProxy::on_query_finished, generation_, session_id_);
      }

     private:
      ActorId<SessionMultiProxy> parent_;
      uint32 generation_;
      int32 session_id_;
    };
    info.proxy = create_actor<SessionProxy>(name, make_unique<Callback>(actor_id(this), sessions_generation_, i),
                                            auth_data_, is_main_, allow_media_only_, is_media_, get_pfs_flag(), is_cdn_,
                                            need_destroy_auth_key_ && i == 0);
    sessions_.push_back(std::move(info));
  }
}

void SessionMultiProxy::on_query_finished(uint32 generation, int session_id) {
  if (generation != sessions_generation_) {
    return;
  }
  sessions_.at(session_id).queries_count--;
  CHECK(sessions_.at(session_id).queries_count >= 0);
}

}  // namespace td
