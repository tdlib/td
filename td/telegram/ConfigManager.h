//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/net/DcId.h"
#include "td/telegram/net/DcOptions.h"
#include "td/telegram/net/NetQuery.h"

#include "td/telegram/telegram_api.h"

#include "td/actor/actor.h"
#include "td/actor/PromiseFuture.h"

#include "td/utils/port/IPAddress.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"
#include "td/utils/Time.h"

namespace td {

class ConfigShared;

using SimpleConfig = tl_object_ptr<telegram_api::help_configSimple>;

Result<SimpleConfig> decode_config(Slice input);

ActorOwn<> get_simple_config_azure(Promise<SimpleConfig> promise, const ConfigShared *shared_config, bool is_test,
                                   int32 scheduler_id);

ActorOwn<> get_simple_config_google_dns(Promise<SimpleConfig> promise, const ConfigShared *shared_config, bool is_test,
                                        int32 scheduler_id);

using FullConfig = tl_object_ptr<telegram_api::config>;

ActorOwn<> get_full_config(DcId dc_id, IPAddress ip_address, Promise<FullConfig> promise);

class ConfigRecoverer;
class ConfigManager : public NetQueryCallback {
 public:
  explicit ConfigManager(ActorShared<> parent);

  void request_config();

  void on_dc_options_update(DcOptions dc_options);

 private:
  ActorShared<> parent_;
  int32 config_sent_cnt_{0};
  ActorOwn<ConfigRecoverer> config_recoverer_;
  int ref_cnt_{1};
  Timestamp expire_;

  void start_up() override;
  void hangup_shared() override;
  void hangup() override;
  void loop() override;
  void try_stop();

  void on_result(NetQueryPtr res) override;

  void request_config_from_dc_impl(DcId dc_id);
  void process_config(tl_object_ptr<telegram_api::config> config);

  Timestamp load_config_expire();
  void save_config_expire(Timestamp timestamp);
  void save_dc_options_update(DcOptions dc_options);
  DcOptions load_dc_options_update();
};
}  // namespace td
