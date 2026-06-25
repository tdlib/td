// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "test/stealth/SourceContractFileReader.h"

#include <string_view>

namespace {

td::string normalize_for_contract(td::Slice source) {
  td::string normalized;
  normalized.reserve(source.size());
  for (auto c : source) {
    if (auto byte = static_cast<unsigned char>(c); byte == ' ' || byte == '\t' || byte == '\r' || byte == '\n') {
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

TEST(BusinessConnectedBotInfoContract, StoreSerializesTheLocationMemberRatherThanAShadow) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/BusinessConnectedBot.hpp");
  auto region = normalize_for_contract(
      extract_region(source, "template <class StorerT>", "template <class ParserT>"));

  ASSERT_TRUE(region.find("boolhas_location=!location_.empty();") != td::string::npos);
  ASSERT_TRUE(region.find("stringlocation_;") == td::string::npos);
  ASSERT_TRUE(region.find("if(has_location){td::store(location_,storer);}") != td::string::npos);
  ASSERT_TRUE(region.find("if(has_device){td::store(device_,storer);}") != td::string::npos);
  ASSERT_TRUE(region.find("if(has_date){td::store(date_,storer);}") != td::string::npos);
}

}  // namespace
