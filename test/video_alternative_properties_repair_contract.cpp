// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "test/stealth/SourceContractFileReader.h"

#include <string_view>

namespace {

td::string extract_region(std::string_view source, td::Slice begin_marker, td::Slice end_marker) {
  auto begin = source.find(begin_marker.str());
  CHECK(begin != td::string::npos);
  auto end = source.find(end_marker.str(), begin + begin_marker.size());
  CHECK(end != td::string::npos);
  CHECK(end > begin);
  return td::string(source.substr(begin, end - begin));
}

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

// Upstream tdlib 1a8d24176 ("Repair video properties from alternative videos if possible").
// Adapted faithfully; the new parameter carries a default ({}) in the header so the fork's other
// get_video_object callers are unaffected.
// Contract: when a video's own duration is 0 or its thumbnail is missing, get_video_object must derive
// those properties from the alternative (HLS) videos when available, instead of returning the broken
// zero/empty values.
TEST(VideoAlternativePropertiesRepairContract, MissingDurationOrThumbnailIsRepairedFromAlternatives) {
  auto header = td::mtproto::test::read_repo_text_file("td/telegram/VideosManager.h");
  auto header_norm = normalize_for_contract(header);
  ASSERT_TRUE(header_norm.find("get_video_object(FileIdfile_id,constvector<FileId>&alternative_file_ids={})const;") !=
              td::string::npos);

  auto source = td::mtproto::test::read_repo_text_file("td/telegram/VideosManager.cpp");
  auto region =
      extract_region(source, "VideosManager::get_video_object(FileId file_id,", "get_story_video_object");
  auto body = normalize_for_contract(region);
  ASSERT_TRUE(body.find("if((duration==0||thumbnail==nullptr)&&!alternative_file_ids.empty())") != td::string::npos);
  ASSERT_TRUE(body.find("duration=get_alternative_video_repair_plan(duration,has_primary_thumbnail,alternatives)."
                        "repaired_duration;") != td::string::npos);
  ASSERT_TRUE(body.find("make_object<td_api::video>(duration,") != td::string::npos);
}

}  // namespace
