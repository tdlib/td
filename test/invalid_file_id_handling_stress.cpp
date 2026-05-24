// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "test/invalid_file_id_handling_test_utils.h"

TEST(InvalidFileIdHandlingStress, repeated_source_reads_preserve_invalid_file_id_contract) {
  constexpr int kIterations = 2400;

  for (int i = 0; i < kIterations; i++) {
    const auto normalized_message_content = td::invalid_file_id_handling_test::normalized_message_content_cpp();
    const auto normalized_messages_manager = td::invalid_file_id_handling_test::normalized_messages_manager_cpp();

    ASSERT_EQ(1u, td::invalid_file_id_handling_test::count_occurrences(normalized_message_content,
                                                                          "if(!file_id.is_valid()){continue;}"));
    ASSERT_EQ(1u, td::invalid_file_id_handling_test::count_occurrences(
                      normalized_messages_manager,
                      "if(pos<file_upload_ids_.size()&&pos<file_references_.size()&&!was_uploaded_&&"
                      "file_upload_ids_[pos].is_valid()){"));
    ASSERT_EQ(1u, td::invalid_file_id_handling_test::count_occurrences(
                      normalized_messages_manager, "CHECK(m->file_upload_ids[media_pos].is_valid());"));
  }
}
