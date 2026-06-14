// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/common.h"
#include "td/utils/tests.h"

#include "test/stealth/SourceContractFileReader.h"

#include <vector>

namespace {

using td::mtproto::test::read_repo_text_file;

void assert_contains(td::Slice content, td::Slice needle, td::Slice path) {
  (void)path;
  ASSERT_TRUE(content.str().find(needle.str()) != td::string::npos);
}

void assert_dual_spdx(td::Slice path) {
  auto content = read_repo_text_file(path);
  assert_contains(content, "SPDX-FileCopyrightText: Copyright Aliaksei Levin", path);
  assert_contains(content, "SPDX-FileCopyrightText: Copyright 2026 telemt community", path);
  assert_contains(content, "SPDX-License-Identifier: BSL-1.0 AND MIT", path);
  assert_contains(content, "telemt: https://github.com/telemt", path);
  assert_contains(content, "telemt: https://t.me/telemtrs", path);
}

void assert_mit_spdx(td::Slice path) {
  auto content = read_repo_text_file(path);
  assert_contains(content, "SPDX-FileCopyrightText: Copyright 2026 telemt community", path);
  assert_contains(content, "SPDX-License-Identifier: MIT", path);
  assert_contains(content, "telemt: https://github.com/telemt", path);
  assert_contains(content, "telemt: https://t.me/telemtrs", path);
}

TEST(PR22SpdxHeaderContract, ModifiedBoostDerivedFilesUseRequiredDualHeader) {
  const std::vector<td::string> files = {
      "td/mtproto/ClientHelloExecutor.cpp",
      "td/mtproto/ClientHelloOpMapper.cpp",
      "td/mtproto/SessionEventBounds.cpp",
      "td/mtproto/SessionEventBounds.h",
      "td/mtproto/stealth/DrsEngine.cpp",
      "td/mtproto/stealth/StealthConfig.cpp",
      "td/mtproto/stealth/StealthParamsLoader.cpp",
      "td/mtproto/stealth/StealthParamsLoader.h",
      "td/mtproto/stealth/StealthRuntimeParams.cpp",
      "td/mtproto/stealth/StealthRuntimeParams.h",
      "td/mtproto/stealth/StealthTransportDecorator.cpp",
      "td/mtproto/stealth/StealthTransportDecorator.h",
      "td/mtproto/stealth/TlsHelloProfileRegistry.cpp",
      "td/mtproto/stealth/TlsHelloProfileRegistry.h",
      "td/telegram/net/ProxyChecker.cpp",
      "td/telegram/net/ProxyChecker.h",
  };

  for (const auto &path : files) {
    assert_dual_spdx(path);
  }
}

TEST(PR22SpdxHeaderContract, NewAndTestFilesUseTelemtMitHeader) {
  const std::vector<td::string> files = {
      "td/mtproto/stealth/SecureRngBounded.h",
      "test/sqlite_phase3_stress.cpp",
      "test/stealth/test_ipt_idle_sampler_adversarial.cpp",
  };

  for (const auto &path : files) {
    assert_mit_spdx(path);
  }
}

}  // namespace
