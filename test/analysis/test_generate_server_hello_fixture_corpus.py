# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

import pathlib
import tempfile
import unittest

from generate_server_hello_fixture_corpus import authoritative_family_for_artifact, output_path_for_artifact, prune_stale_outputs


class GenerateServerHelloFixtureCorpusTest(unittest.TestCase):
    def test_uses_registry_family_instead_of_profile_id(self) -> None:
        artifact = {
            "profile_id": "chrome146_177_linux_desktop",
            "samples": [{"fixture_id": "chrome146_177_linux_desktop:frame5"}],
        }
        registry = {
            "fixtures": {
                "chrome146_177_linux_desktop:frame5": {
                    "family": "chromium_44cd_mlkem_linux_desktop",
                }
            }
        }

        family = authoritative_family_for_artifact(artifact, registry)

        self.assertEqual("chromium_44cd_mlkem_linux_desktop", family)

    def test_mirrors_clienthello_tree_for_output_paths(self) -> None:
        input_root = pathlib.Path("test/analysis/fixtures/clienthello")
        output_root = pathlib.Path("test/analysis/fixtures/serverhello")
        artifact_path = input_root / "ios" / "safari26_4_ios26_4_a.clienthello.json"

        output_path = output_path_for_artifact(input_root, output_root, artifact_path)

        self.assertEqual(
            output_root / "ios" / "safari26_4_ios26_4_a.serverhello.json",
            output_path,
        )

    def test_prunes_stale_outputs_not_mirrored_from_clienthello_tree(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            output_root = pathlib.Path(temp_dir)
            keep_path = output_root / "ios" / "keep.serverhello.json"
            stale_path = output_root / "linux_desktop" / "stale.serverhello.json"
            keep_path.parent.mkdir(parents=True)
            stale_path.parent.mkdir(parents=True)
            keep_path.write_text("{}", encoding="utf-8")
            stale_path.write_text("{}", encoding="utf-8")

            prune_stale_outputs(output_root, {keep_path})

            self.assertTrue(keep_path.exists())
            self.assertFalse(stale_path.exists())


if __name__ == "__main__":
    unittest.main()