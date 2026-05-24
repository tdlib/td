// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "test/invalid_file_id_handling_test_utils.h"

using td::invalid_file_id_handling_test::normalized_message_content_cpp;
using td::invalid_file_id_handling_test::normalized_messages_manager_cpp;

TEST(InvalidFileIdHandlingContract, message_content_skips_invalid_file_ids_before_reference_repair) {
  const auto normalized = normalized_message_content_cpp();

  ASSERT_NE(td::string::npos, normalized.find("if(!file_id.is_valid()){continue;}"));
}

TEST(InvalidFileIdHandlingContract, messages_manager_uses_empty_upload_id_for_invalid_file_ids) {
  const auto normalized = normalized_messages_manager_cpp();

  ASSERT_NE(td::string::npos,
            normalized.find("autofile_upload_ids=transform(file_ids,[](FileIdfile_id){returnfile_id.is_valid()"
                            "?FileUploadId(file_id,FileManager::get_internal_upload_id()):FileUploadId();});"));
}

TEST(InvalidFileIdHandlingContract, send_media_file_reference_error_requires_valid_upload_id) {
  const auto normalized = normalized_messages_manager_cpp();

  ASSERT_NE(td::string::npos,
            normalized.find("if(pos<file_upload_ids_.size()&&pos<file_references_.size()&&!was_uploaded_&&"
                            "file_upload_ids_[pos].is_valid()){"));
}

TEST(InvalidFileIdHandlingContract, paid_media_retry_path_checks_upload_id_validity) {
  const auto normalized = normalized_messages_manager_cpp();

  ASSERT_NE(td::string::npos, normalized.find("CHECK(m->file_upload_ids[media_pos].is_valid());"));
}

TEST(InvalidFileIdHandlingContract, paid_media_group_send_fails_closed_when_input_media_builder_returns_null) {
  const auto normalized = normalized_messages_manager_cpp();
  const auto segment = td::invalid_file_id_handling_test::extract_normalized_segment(
      normalized, "voidMessagesManager::do_send_paid_media_group(DialogIddialog_id,MessageIdmessage_id){",
      "voidMessagesManager::on_text_message_ready_to_send(DialogIddialog_id,MessageIdmessage_id){");

  ASSERT_FALSE(segment.empty());
  ASSERT_EQ(td::string::npos, segment.find("CHECK(input_media!=nullptr);"));
  ASSERT_NE(td::string::npos,
            segment.find("if(input_media==nullptr){on_send_message_fail(random_id,Status::Error(400,"
                         "\"Groupsendfailed\"));CHECK(pending_paid_media_group_sends_.count({dialog_id,"
                         "message_id})==0);return;}"));
}
