// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "td/utils/tests.h"

#include "test/stealth/SourceContractFileReader.h"

TEST(MtprotoDowncastConstructDispatchIntegration, session_connection_uses_constructor_dispatch_without_helper_object) {
  auto source = td::mtproto::test::read_repo_text_file("td/mtproto/SessionConnection.cpp");

  ASSERT_NE(td::string::npos, source.find("downcast_construct_call("));
  ASSERT_EQ(td::string::npos, source.find("TlDowncastHelper<mtproto_api::Object> helper("));
}

TEST(MtprotoDowncastConstructDispatchIntegration, tl_json_uses_constructor_dispatch_without_tldowncast_helper) {
  auto source = td::mtproto::test::read_repo_text_file("td/tl/tl_json.h");

  ASSERT_NE(td::string::npos, source.find("downcast_construct_call("));
  ASSERT_EQ(td::string::npos, source.find("TlDowncastHelper<T> helper("));
}

TEST(MtprotoDowncastConstructDispatchIntegration, generator_emits_tag_mapping_for_constructor_only_dispatch) {
  auto source = td::mtproto::test::read_repo_text_file("td/generate/tl_writer_hpp.cpp");

  ASSERT_NE(td::string::npos, source.find("struct downcast_call_tag"));
  ASSERT_NE(td::string::npos, source.find("downcast_call_target_t"));
  ASSERT_NE(td::string::npos, source.find("downcast_construct_call("));
}
