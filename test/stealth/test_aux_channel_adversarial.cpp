// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "td/telegram/ConfigManager.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/crypto.h"
#include "td/utils/tests.h"

namespace {

void store_int32_le(td::MutableSlice slice, td::int32 value) {
  auto unsigned_value = static_cast<td::uint32>(value);
  for (td::int32 i = 0; i < 4; i++) {
    slice[i] = static_cast<char>((unsigned_value >> (i * 8)) & 0xff);
  }
}

td::string make_simple_config_payload(td::int32 length, td::int32 constructor_id) {
  td::string payload(224, '\0');
  store_int32_le(td::MutableSlice(payload).substr(0, 4), length);
  store_int32_le(td::MutableSlice(payload).substr(4, 4), constructor_id);

  td::string hash(32, '\0');
  td::sha256(td::Slice(payload).substr(0, 208), td::MutableSlice(hash));
  td::MutableSlice(payload).substr(208, 16).copy_from(td::Slice(hash).substr(0, 16));
  return payload;
}

td::string make_payload_with_broken_hash(td::int32 length, td::int32 constructor_id) {
  auto payload = make_simple_config_payload(length, constructor_id);
  payload[208] ^= 0x01;
  return payload;
}

td::string mutate_fixture(td::string fixture) {
  for (auto &ch : fixture) {
    if (ch >= 'A' && ch <= 'Z') {
      ch = ch == 'A' ? 'B' : 'A';
      return fixture;
    }
    if (ch >= 'a' && ch <= 'z') {
      ch = ch == 'a' ? 'b' : 'a';
      return fixture;
    }
    if (ch >= '0' && ch <= '9') {
      ch = ch == '0' ? '1' : '0';
      return fixture;
    }
  }
  UNREACHABLE();
}

TEST(AuxChannelAdversarial, CiphertextMutationFailsClosed) {
  td::string data =
      "   hO//tt \b\n\tiwPVovorKtIYtQ8y2ik7CqfJiJ4pJOCLRa4fBmNPixuRPXnBFF/3mTAAZoSyHq4SNylGHz0Cv1/"
      "FnWWdEV+BPJeOTk+ARHcNkuJBt0CqnfcVCoDOpKqGyq0U31s2MOpQvHgAG+Tlpg02syuH0E4dCGRw5CbJPARiynteb9y5fT5x/"
      "kmdp6BMR5tWQSQF0liH16zLh8BDSIdiMsikdcwnAvBwdNhRqQBqGx9MTh62MDmlebjtczE9Gz0z5cscUO2yhzGdphgIy6SP+"
      "bwaqLWYF0XdPGjKLMUEJW+rou6fbL1t/EUXPtU0XmQAnO0Fh86h+AqDMOe30N4qKrPQ==   ";

  ASSERT_TRUE(td::decode_config(mutate_fixture(std::move(data))).is_error());
}

TEST(AuxChannelAdversarial, MalformedPayloadLengthFailsClosed) {
  auto status =
      td::decode_simple_config_payload(make_simple_config_payload(7, td::telegram_api::help_configSimple::ID));
  ASSERT_TRUE(status.is_error());
}

TEST(AuxChannelAdversarial, HashMismatchFailsClosedBeforeParsing) {
  auto status =
      td::decode_simple_config_payload(make_payload_with_broken_hash(8, td::telegram_api::help_configSimple::ID));
  ASSERT_TRUE(status.is_error());
}

TEST(AuxChannelAdversarial, ConstructorMismatchFailsClosed) {
  auto status = td::decode_simple_config_payload(make_simple_config_payload(8, 0x10293847));
  ASSERT_TRUE(status.is_error());
}

}  // namespace