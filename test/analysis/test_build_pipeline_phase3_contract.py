# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT

import pathlib
import unittest


THIS_DIR = pathlib.Path(__file__).resolve().parent
REPO_ROOT = THIS_DIR.parent.parent
ROOT_CMAKE_PATH = REPO_ROOT / "CMakeLists.txt"
MESSAGES_MANAGER_CPP_PATH = REPO_ROOT / "td" / "telegram" / "MessagesManager.cpp"
MESSAGES_SPLIT_CPP_PATH = REPO_ROOT / "td" / "telegram" / "MessagesManagerLifecycle.cpp"
STORY_MANAGER_CPP_PATH = REPO_ROOT / "td" / "telegram" / "StoryManager.cpp"
STORY_SPLIT_CPP_PATH = REPO_ROOT / "td" / "telegram" / "StoryManagerLifecycle.cpp"


class BuildPipelinePhase3ContractTest(unittest.TestCase):
    def test_phase3_split_translation_unit_is_registered_in_root_cmake(self) -> None:
        root_cmake = ROOT_CMAKE_PATH.read_text(encoding="utf-8")

        self.assertIn(
            "td/telegram/MessagesManagerLifecycle.cpp",
            root_cmake,
            msg="phase 3 must add a dedicated MessagesManager split translation unit to the tdcore source list",
        )

    def test_phase3_split_translation_unit_file_exists(self) -> None:
        self.assertTrue(
            MESSAGES_SPLIT_CPP_PATH.exists(),
            msg="phase 3 must create the split MessagesManager lifecycle translation unit",
        )

    def test_phase3_story_split_translation_unit_is_registered_in_root_cmake(self) -> None:
        root_cmake = ROOT_CMAKE_PATH.read_text(encoding="utf-8")

        self.assertIn(
            "td/telegram/StoryManagerLifecycle.cpp",
            root_cmake,
            msg="phase 3 must add a dedicated StoryManager split translation unit to the tdcore source list",
        )

    def test_phase3_story_split_translation_unit_file_exists(self) -> None:
        self.assertTrue(
            STORY_SPLIT_CPP_PATH.exists(),
            msg="phase 3 must create the split StoryManager lifecycle translation unit",
        )

    def test_moved_lifecycle_methods_are_not_defined_in_messages_manager_cpp(self) -> None:
        messages_cpp = MESSAGES_MANAGER_CPP_PATH.read_text(encoding="utf-8")

        self.assertNotIn(
            "void MessagesManager::on_channel_get_difference_timeout_callback(void *messages_manager_ptr, int64 dialog_id_int)",
            messages_cpp,
            msg="lifecycle callback definitions must be moved out of the hotspot translation unit",
        )
        self.assertNotIn(
            "int32 MessagesManager::get_message_index_mask(DialogId dialog_id, const Message *m) const",
            messages_cpp,
            msg="message index mask hot helper must be moved out of the hotspot translation unit",
        )

    def test_moved_lifecycle_methods_are_not_defined_in_story_manager_cpp(self) -> None:
        story_cpp = STORY_MANAGER_CPP_PATH.read_text(encoding="utf-8")

        self.assertNotIn(
            "void StoryManager::on_story_reload_timeout_callback(void *story_manager_ptr, int64 story_global_id)",
            story_cpp,
            msg="story lifecycle callback definitions must be moved out of the hotspot translation unit",
        )
        self.assertNotIn(
            "void StoryManager::on_load_expired_database_stories(vector<StoryDbStory> stories)",
            story_cpp,
            msg="expired-story load handlers must be moved out of the hotspot translation unit",
        )


if __name__ == "__main__":
    unittest.main()