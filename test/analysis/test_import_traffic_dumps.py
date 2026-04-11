# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

import pathlib
import tempfile
import unittest

from import_traffic_dumps import CapturePlan, derive_capture_plan, move_capture


class ImportTrafficDumpsTest(unittest.TestCase):
    @staticmethod
    def make_plan(source_path: pathlib.Path, target_path: pathlib.Path) -> CapturePlan:
        return CapturePlan(
            source_path=str(source_path),
            target_capture_path=str(target_path),
            platform_key="android",
            captures_dir="Android",
            fixtures_dir="android",
            os_family="android",
            device_class="mobile",
            browser_alias="chrome",
            browser_version_slug="146",
            os_version_slug="10",
            profile_id="chrome146_android10_test",
            clienthello_out=str(target_path.parent / "artifacts" / "chrome146.clienthello.json"),
            serverhello_out=str(target_path.parent / "artifacts" / "chrome146.serverhello.json"),
            clienthello_scenario_id="imported_chrome146_clienthello",
            serverhello_scenario_id="imported_chrome146_serverhello",
            user_os_token="Android_10",
            user_browser_token="Google_Chrome_146",
            auto_os_token="auto_Android_10",
            auto_browser_token="auto_Google_Chrome_146",
            selected_os_token="Android_10",
            selected_browser_token="Google_Chrome_146",
            selected_os_source="user",
            selected_browser_source="user",
        )

    def test_prefers_user_tokens_when_present(self) -> None:
        plan = derive_capture_plan(pathlib.Path("iOS 26.5, Safari 26.5, auto iOS 18.7, auto Safari 26.5.pcap"))

        self.assertEqual("ios", plan.platform_key)
        self.assertEqual("safari", plan.browser_alias)
        self.assertEqual("user", plan.selected_os_source)
        self.assertEqual("user", plan.selected_browser_source)
        self.assertTrue(plan.profile_id.startswith("safari26_5_ios26_5_"))

    def test_falls_back_to_auto_browser_when_user_browser_is_null(self) -> None:
        plan = derive_capture_plan(pathlib.Path("iOS 18.7, null, auto iOS 18.7, auto Safari 26.4.pcap"))

        self.assertEqual("ios", plan.platform_key)
        self.assertEqual("safari", plan.browser_alias)
        self.assertEqual("auto", plan.selected_browser_source)
        self.assertTrue(plan.profile_id.startswith("safari26_4_ios18_7_"))

    def test_uses_auto_browser_family_when_user_token_only_contains_version(self) -> None:
        plan = derive_capture_plan(
            pathlib.Path("Windows_10_0,_26_3_3_869_64_bit,_auto_Windows_10_0,_auto_YaBrows.pcap")
        )

        self.assertEqual("windows", plan.platform_key)
        self.assertEqual("yandex", plan.browser_alias)
        self.assertEqual("auto", plan.selected_browser_source)
        self.assertTrue(plan.profile_id.startswith("yandex26_3_3_869_64_bit_windows10_0_"))

    def test_handles_cyrillic_android_browser_names(self) -> None:
        plan = derive_capture_plan(
            pathlib.Path("Андроид_14,_Adblock_browser_3_11_1,_auto_Android_10,_auto_Chromi.pcap")
        )

        self.assertEqual("android", plan.platform_key)
        self.assertEqual("adblock_browser", plan.browser_alias)
        self.assertEqual("mobile", plan.device_class)
        self.assertTrue(plan.profile_id.startswith("adblock_browser3_11_1_android14_"))

    def test_maps_linux_desktop_variants_to_linux_platform(self) -> None:
        plan = derive_capture_plan(
            pathlib.Path("Arch_Linux_6_19_6,_LibreWolf_149_0_2_1,_auto_Linux,_auto_Firefox.pcap")
        )

        self.assertEqual("linux_desktop", plan.platform_key)
        self.assertEqual("librewolf", plan.browser_alias)
        self.assertEqual("desktop", plan.device_class)
        self.assertTrue(plan.profile_id.startswith("librewolf149_0_2_1_linux6_19_6_"))

    def test_rejects_unknown_browser_capture_names(self) -> None:
        with self.assertRaisesRegex(ValueError, "unable to classify browser"):
            derive_capture_plan(
                pathlib.Path("Windows_11_Version_25H2_Build_26200_8037,_126_0_0_HEAD_3134+g196.pcap")
            )

    def test_move_capture_disambiguates_existing_target_name(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            base_dir = pathlib.Path(temp_dir)
            source_path = base_dir / "source.pcap"
            target_path = base_dir / "captures" / "collision.pcap"
            source_path.write_bytes(b"new-capture")
            target_path.parent.mkdir(parents=True)
            target_path.write_bytes(b"existing-capture")

            plan = self.make_plan(source_path, target_path)

            moved_path, _ = move_capture(plan, dry_run=False)

            self.assertNotEqual(target_path, moved_path)
            self.assertTrue(moved_path.name.startswith("collision__"))
            self.assertTrue(moved_path.exists())
            self.assertEqual(b"new-capture", moved_path.read_bytes())
            self.assertFalse(source_path.exists())
            self.assertEqual(b"existing-capture", target_path.read_bytes())

    def test_disambiguated_filename_produces_distinct_profile_id(self) -> None:
        original = derive_capture_plan(
            pathlib.Path("Android_10,_Google_Chrome_146,_auto_Android_10,_auto_Google_Chro.pcap")
        )
        disambiguated = derive_capture_plan(
            pathlib.Path("Android_10,_Google_Chrome_146,_auto_Android_10,_auto_Google_Chro__deadbeef.pcap")
        )

        self.assertNotEqual(original.profile_id, disambiguated.profile_id)
        self.assertEqual(original.browser_alias, disambiguated.browser_alias)
        self.assertEqual(original.platform_key, disambiguated.platform_key)


if __name__ == "__main__":
    unittest.main()