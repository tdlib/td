// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "test/invalid_file_id_handling_test_utils.h"

using td::invalid_file_id_handling_test::extract_normalized_segment;
using td::invalid_file_id_handling_test::normalized_message_content_cpp;
using td::invalid_file_id_handling_test::normalized_messages_manager_cpp;

TEST(InvalidFileIdHandlingIntegration, message_content_guard_executes_before_fail_closed_reference_return) {
  const auto normalized = normalized_message_content_cpp();
  const auto segment =
      extract_normalized_segment(normalized, "autofile_id=file_ids[i];", "LOG(ERROR)<<\"File\"<<file_id");

  ASSERT_FALSE(segment.empty());

  const auto guard_pos = segment.find("if(!file_id.is_valid()){continue;}");
  const auto force_check_pos = segment.find("if(!force){");
  ASSERT_NE(td::string::npos, guard_pos);
  ASSERT_NE(td::string::npos, force_check_pos);
  ASSERT_TRUE(guard_pos < force_check_pos);
}

TEST(InvalidFileIdHandlingIntegration, paid_media_upload_loop_marks_invalid_entries_finished_before_check) {
  const auto normalized = normalized_messages_manager_cpp();
  const auto segment = extract_normalized_segment(normalized, "autofile_upload_id=file_upload_ids[i];",
                                                  "LOG(INFO)<<\"Asktoupload\"<<file_upload_id");

  ASSERT_FALSE(segment.empty());
  ASSERT_NE(td::string::npos,
            segment.find("if(file_view.empty()){on_upload_message_media_finished(m->media_album_id,dialog_id,"
                         "m->message_id,static_cast<int32>(i),Status::OK());continue;}"));
  ASSERT_NE(td::string::npos, segment.find("CHECK(file_upload_id.is_valid());"));
}

TEST(InvalidFileIdHandlingIntegration, send_media_error_and_retry_paths_share_upload_id_validity_contract) {
  const auto normalized = normalized_messages_manager_cpp();

  ASSERT_NE(td::string::npos,
            normalized.find("if(pos<file_upload_ids_.size()&&pos<file_references_.size()&&!was_uploaded_&&"
                            "file_upload_ids_[pos].is_valid()){"));
  ASSERT_NE(td::string::npos, normalized.find("CHECK(m->file_upload_ids[media_pos].is_valid());"));
}

TEST(InvalidFileIdHandlingIntegration, paid_media_group_null_input_media_path_uses_fail_closed_send_error) {
  const auto normalized = normalized_messages_manager_cpp();
  const auto segment = extract_normalized_segment(
      normalized, "voidMessagesManager::do_send_paid_media_group(DialogIddialog_id,MessageIdmessage_id){",
      "voidMessagesManager::on_text_message_ready_to_send(DialogIddialog_id,MessageIdmessage_id){");

  ASSERT_FALSE(segment.empty());
  ASSERT_NE(td::string::npos,
            segment.find("if(input_media==nullptr){on_send_message_fail(random_id,Status::Error(400,"
                         "\"Groupsendfailed\"));CHECK(pending_paid_media_group_sends_.count({dialog_id,"
                         "message_id})==0);return;}"));
}
