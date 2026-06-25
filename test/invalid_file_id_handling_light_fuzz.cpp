// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/Random.h"
#include "td/utils/tests.h"

#include "test/invalid_file_id_handling_test_utils.h"

#include <array>

namespace {

struct SnippetCase {
  int source_kind;
  td::string snippet;
  bool expected_present;
};

}  // namespace

TEST(InvalidFileIdHandlingLightFuzz, randomized_literal_sampling_preserves_invalid_file_id_guards) {
  const auto normalized_message_content = td::invalid_file_id_handling_test::normalized_message_content_cpp();
  const auto normalized_messages_manager = td::invalid_file_id_handling_test::normalized_messages_manager_cpp();
  const auto normalized_file_upload_id = td::invalid_file_id_handling_test::normalized_file_upload_id_cpp();

  const std::array<SnippetCase, 9> cases = {{
      {0, "if(!file_id.is_valid()){continue;}", true},
      {1, "autofile_upload_ids=FileUploadId::get_file_upload_ids(file_ids);", true},
      {2,
       "returnfile_id.is_valid()?FileUploadId(file_id,FileManager::get_internal_upload_id()):FileUploadId();",
       true},
      {1,
       "if(pos<file_upload_ids_.size()&&pos<file_references_.size()&&!was_uploaded_&&"
       "file_upload_ids_[pos].is_valid()){",
       true},
      {1,
       "if(file_view.empty()){on_upload_message_media_finished(m->media_album_id,dialog_id,m->message_id,"
       "static_cast<int32>(i),Status::OK());continue;}",
       true},
      {1, "CHECK(m->file_upload_ids[media_pos].is_valid());", true},
      {2, "returnFileUploadId(file_id,FileManager::get_internal_upload_id());", false},
      {1,
       "autofile_upload_ids=transform(file_ids,[](FileIdfile_id){returnfile_id.is_valid()"
       "?FileUploadId(file_id,FileManager::get_internal_upload_id()):FileUploadId();});",
       false},
      {1, "autofile_upload_ids=transform(file_ids,[](FileIdfile_id){returnFileUploadId(file_id,", false},
  }};

  constexpr int kIterations = 12000;
  for (int i = 0; i < kIterations; i++) {
    const auto &test_case = cases[static_cast<size_t>(td::Random::fast(0, static_cast<int>(cases.size()) - 1))];
    const auto &source = test_case.source_kind == 0
                             ? normalized_message_content
                             : (test_case.source_kind == 1 ? normalized_messages_manager : normalized_file_upload_id);
    ASSERT_EQ(test_case.expected_present, source.find(test_case.snippet) != td::string::npos);
  }
}
