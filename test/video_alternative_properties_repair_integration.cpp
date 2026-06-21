// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "test/stealth/SourceContractFileReader.h"

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

TEST(VideoAlternativePropertiesRepairIntegration, MessageVideoObjectPathThreadsAlternativeFileIdsIntoRepairSurface) {
  auto source = normalize_for_contract(td::mtproto::test::read_repo_text_file("td/telegram/MessageContent.cpp"));

  ASSERT_NE(td::string::npos,
            source.find("autovideo=td->videos_manager_->get_alternative_video_object(file_id,m->hls_file_ids);"));
  ASSERT_NE(td::string::npos,
            source.find("returnmake_tl_object<td_api::messageVideo>(td->videos_manager_->get_video_object("
                        "m->file_id,m->alternative_file_ids),std::move(alternative_videos),"));
}

}  // namespace
