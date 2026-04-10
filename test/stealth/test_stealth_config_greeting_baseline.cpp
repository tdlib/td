// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "test/stealth/MockRng.h"

#include "td/mtproto/ProxySecret.h"
#include "td/mtproto/stealth/StealthConfig.h"
#include "td/mtproto/stealth/StealthRecordSizeBaselines.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

namespace {

using td::mtproto::ProxySecret;
using td::mtproto::stealth::baselines::kGreetingRecord1;
using td::mtproto::stealth::baselines::kGreetingRecord2;
using td::mtproto::stealth::baselines::kGreetingRecord3;
using td::mtproto::stealth::baselines::kGreetingRecord4;
using td::mtproto::stealth::baselines::kGreetingRecord5;
using td::mtproto::stealth::DrsPhaseModel;
using td::mtproto::stealth::GreetingCamouflagePolicy;
using td::mtproto::stealth::RecordSizeBin;
using td::mtproto::stealth::StealthConfig;
using td::mtproto::test::MockRng;

td::string make_tls_secret() {
  return td::string("\xee") + "0123456789abcdefexample.com";
}

template <size_t N>
DrsPhaseModel baseline_model(const RecordSizeBin (&source)[N]) {
  DrsPhaseModel model;
  model.max_repeat_run = 4;
  model.local_jitter = 0;
  for (const auto &bin : source) {
    model.bins.push_back({bin.lo, bin.hi, bin.weight});
  }
  return model;
}

void assert_same_model(const DrsPhaseModel &lhs, const DrsPhaseModel &rhs) {
  ASSERT_EQ(lhs.max_repeat_run, rhs.max_repeat_run);
  ASSERT_EQ(lhs.local_jitter, rhs.local_jitter);
  ASSERT_EQ(lhs.bins.size(), rhs.bins.size());
  for (size_t i = 0; i < lhs.bins.size(); i++) {
    ASSERT_EQ(lhs.bins[i].lo, rhs.bins[i].lo);
    ASSERT_EQ(lhs.bins[i].hi, rhs.bins[i].hi);
    ASSERT_EQ(lhs.bins[i].weight, rhs.bins[i].weight);
  }
}

TEST(StealthConfigGreetingBaseline, TlsSecretsUseCaptureDerivedGreetingTemplates) {
  MockRng rng(17);
  auto config = StealthConfig::from_secret(ProxySecret::from_raw(make_tls_secret()), rng);

  ASSERT_TRUE(config.validate().is_ok());
  ASSERT_EQ(GreetingCamouflagePolicy::kMaxRecordModels, config.greeting_camouflage_policy.greeting_record_count);

  assert_same_model(baseline_model(kGreetingRecord1), config.greeting_camouflage_policy.record_models[0]);
  assert_same_model(baseline_model(kGreetingRecord2), config.greeting_camouflage_policy.record_models[1]);
  assert_same_model(baseline_model(kGreetingRecord3), config.greeting_camouflage_policy.record_models[2]);
  assert_same_model(baseline_model(kGreetingRecord4), config.greeting_camouflage_policy.record_models[3]);
  assert_same_model(baseline_model(kGreetingRecord5), config.greeting_camouflage_policy.record_models[4]);
}

}  // namespace