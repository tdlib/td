//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/Global.h"
#include "td/telegram/net/AuthDataShared.h"
#include "td/telegram/net/DcId.h"
#include "td/telegram/net/NetQuery.h"

#include "td/actor/actor.h"

#include "td/utils/buffer.h"
#include "td/utils/common.h"

#include <memory>

namespace td {
class DcAuthManager : public NetQueryCallback {
 public:
  explicit DcAuthManager(ActorShared<> parent);

  void add_dc(std::shared_ptr<AuthDataShared> auth_data);
  void update_main_dc(DcId new_main_dc_id);

 private:
  struct DcInfo {
    DcId dc_id;
    std::shared_ptr<AuthDataShared> shared_auth_data;
    AuthState auth_state;

    enum class State : int32 { Waiting, Export, Import, BeforeOk, Ok };
    State state = State::Waiting;
    uint64 wait_id;
    int32 export_id;
    BufferSlice export_bytes;
  };

  ActorShared<> parent_;

  std::vector<DcInfo> dcs_;
  bool was_auth_ = false;
  DcId main_dc_id_;
  bool close_flag_ = false;

  DcInfo &get_dc(int32 dc_id);
  DcInfo *find_dc(int32 dc_id);

  void update_auth_state();

  void on_result(NetQueryPtr result) override;
  void dc_loop(DcInfo &dc);

  void loop() override;
};
}  // namespace td
