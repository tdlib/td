// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "test/invalid_file_id_handling_test_utils.h"

using td::invalid_file_id_handling_test::normalized_messages_manager_cpp;

TEST(InvalidFileIdHandlingAdversarial, rejects_legacy_unconditional_upload_id_allocation_pattern) {
  const auto normalized = normalized_messages_manager_cpp();

  ASSERT_EQ(td::string::npos,
            normalized.find("autofile_upload_ids=transform(file_ids,[](FileIdfile_id){returnFileUploadId(file_id,"
                            "FileManager::get_internal_upload_id());});"));
}

TEST(InvalidFileIdHandlingAdversarial, rejects_legacy_file_reference_repair_without_upload_id_guard) {
  const auto normalized = normalized_messages_manager_cpp();

  ASSERT_EQ(td::string::npos,
            normalized.find("if(pos<file_upload_ids_.size()&&pos<file_references_.size()&&!was_uploaded_){"));
}

TEST(InvalidFileIdHandlingAdversarial, paid_media_upload_loop_short_circuits_empty_file_views) {
  const auto normalized = normalized_messages_manager_cpp();

  ASSERT_NE(td::string::npos,
            normalized.find("if(file_view.empty()){on_upload_message_media_finished(m->media_album_id,dialog_id,"
                            "m->message_id,static_cast<int32>(i),Status::OK());continue;}"));
  ASSERT_NE(td::string::npos, normalized.find("CHECK(file_upload_id.is_valid());"));
}

TEST(InvalidFileIdHandlingAdversarial, paid_media_group_send_must_not_fatal_on_null_input_media) {
  const auto normalized = normalized_messages_manager_cpp();
  const auto segment = td::invalid_file_id_handling_test::extract_normalized_segment(
      normalized, "voidMessagesManager::do_send_paid_media_group(DialogIddialog_id,MessageIdmessage_id){",
      "voidMessagesManager::on_text_message_ready_to_send(DialogIddialog_id,MessageIdmessage_id){");

  ASSERT_FALSE(segment.empty());
  ASSERT_EQ(td::string::npos, segment.find("CHECK(input_media!=nullptr);"));

  const auto guard_pos = segment.find("if(input_media==nullptr){");
  const auto send_pos = segment.find("td_->create_handler<SendMediaQuery>()->send(");
  ASSERT_NE(td::string::npos, guard_pos);
  ASSERT_NE(td::string::npos, send_pos);
  ASSERT_TRUE(guard_pos < send_pos);
}
