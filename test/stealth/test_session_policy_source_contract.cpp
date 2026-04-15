// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/common.h"
#include "td/utils/tests.h"

#include <fstream>
#include <iterator>

namespace {

td::string read_text_file(td::Slice path) {
  std::ifstream input(path.str(), std::ios::binary);
  CHECK(input.is_open());
  return td::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

TEST(SessionPolicySourceContract, SessionConstructorUsesPolicySetterPath) {
  // Verify the Session constructor routes through the policy-level session mode setter,
  // not the runtime-enforced setter that records coerce attempts.
  auto session_cpp = read_text_file("td/telegram/net/Session.cpp");

  ASSERT_TRUE(session_cpp.find("auth_data_.set_session_mode_from_policy(session_keyed);") != td::string::npos);
  ASSERT_TRUE(session_cpp.find("auth_data_.set_session_mode(session_keyed);") == td::string::npos);
  ASSERT_TRUE(session_cpp.find("auth_data_.set_session_mode(false)") == td::string::npos);
}

}  // namespace