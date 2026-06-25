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

TEST(VideoAlternativePropertiesRepairAdversarial, ConflictingAlternativeDurationsMustNotOverwritePrimaryDuration) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/VideosManager.cpp");
  auto region =
      extract_region(source, "VideosManager::get_video_object(FileId file_id,", "get_story_video_object");
  auto normalized = normalize_for_contract(region);

  ASSERT_NE(td::string::npos,
            normalized.find("alternatives.push_back({alternative_video->duration,alternative_video->animated_"
                            "thumbnail.file_id.is_valid()||alternative_video->thumbnail.file_id.is_valid()});"));
  ASSERT_NE(td::string::npos,
            normalized.find("duration=get_alternative_video_repair_plan(duration,has_primary_thumbnail,alternatives)."
                            "repaired_duration;"));
  ASSERT_EQ(td::string::npos, normalized.find("common_duration"));
}

TEST(VideoAlternativePropertiesRepairAdversarial, ThumbnailRepairMustStayGuardedByPrimaryThumbnailAbsence) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/VideosManager.cpp");
  auto region =
      extract_region(source, "VideosManager::get_video_object(FileId file_id,", "get_story_video_object");
  auto normalized = normalize_for_contract(region);

  auto thumbnail_assign_pos =
      normalized.find("thumbnail=get_thumbnail_object(td_->file_manager_.get(),alternative_video->thumbnail,"
                      "alternative_video->animated_thumbnail);");

  ASSERT_NE(td::string::npos, thumbnail_assign_pos);
  auto guard_pos = normalized.rfind("if(thumbnail==nullptr){", thumbnail_assign_pos);
  ASSERT_NE(td::string::npos, guard_pos);
  ASSERT_TRUE(guard_pos < thumbnail_assign_pos);
}

}  // namespace
