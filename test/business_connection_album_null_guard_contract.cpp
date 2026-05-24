// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/common.h"
#include "td/utils/tests.h"

#include "test/stealth/SourceContractFileReader.h"

#include <string_view>

namespace {

td::string normalize_for_contract(td::Slice source) {
  td::string normalized;
  normalized.reserve(source.size());
  for (auto c : source) {
    auto b = static_cast<unsigned char>(c);
    if (b == ' ' || b == '\t' || b == '\r' || b == '\n') {
      continue;
    }
    normalized.push_back(c);
  }
  return normalized;
}

td::string extract_region(std::string_view source, td::Slice begin_marker, td::Slice end_marker) {
  auto begin = source.find(begin_marker.str());
  CHECK(begin != td::string::npos);
  auto end = source.find(end_marker.str(), begin + begin_marker.size());
  CHECK(end != td::string::npos);
  CHECK(end > begin);
  return td::string(source.substr(begin, end - begin));
}

}  // namespace

TEST(BusinessConnectionAlbumNullGuardContract, AlbumProcessingRejectsNullBusinessMessageObjectsFailClosed) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/BusinessConnectionManager.cpp");
  auto region = normalize_for_contract(extract_region(
      source, "void BusinessConnectionManager::process_sent_business_message_album(",
      "void BusinessConnectionManager::on_upload_message_paid_media(int64 request_id, size_t media_pos,"));

  ASSERT_TRUE(region.find("automessage=td_->messages_manager_->get_business_message_object(std::move(update->message_),"
                          "std::move(update->reply_to_message_));") != td::string::npos);
  ASSERT_TRUE(region.find("if(message==nullptr){") != td::string::npos);
  ASSERT_TRUE(region.find("returnpromise.set_error(500,\"Receiveinvalidbusinessconnectionmessages\");") !=
              td::string::npos);
  ASSERT_TRUE(region.find("messages->messages_.push_back(std::move(message));") != td::string::npos);
}
