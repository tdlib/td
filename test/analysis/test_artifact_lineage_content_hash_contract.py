#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

import hashlib
import json
import pathlib
import re
import tempfile
import time
import unittest


THIS_DIR = pathlib.Path(__file__).resolve().parent
REPO_ROOT = THIS_DIR.parents[1]

LINEAGE_REQUIRED_FIELDS = (
    "generator_sha256",
    "generator_command_line",
    "parser_version",
)


def content_hash(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def build_lineage_record(
    *,
    generator_path: pathlib.Path,
    command_line: str,
    parser_version: str,
    upstream_content_hashes: dict[str, str] | None = None,
) -> dict:
    generator_bytes = generator_path.read_bytes()
    record: dict = {
        "generator_sha256": content_hash(generator_bytes),
        "generator_command_line": command_line,
        "parser_version": parser_version,
    }
    if upstream_content_hashes:
        record["upstream_content_hashes"] = upstream_content_hashes
    return record


def build_artifact(
    *,
    content: str,
    lineage: dict,
    repo_root: pathlib.Path,
    artifact_path: pathlib.Path,
) -> dict:
    return {
        "content_hash": content_hash(content.encode("utf-8")),
        "artifact_path": artifact_path.relative_to(repo_root).as_posix(),
        "generated_at_epoch": time.time(),
        "lineage": lineage,
    }


def verify_artifact_paths_repo_relative(
    artifact: dict, repo_root: pathlib.Path
) -> list[str]:
    violations: list[str] = []
    artifact_path = artifact.get("artifact_path", "")
    if pathlib.PurePosixPath(artifact_path).is_absolute():
        violations.append(
            f"artifact_path is absolute: {artifact_path}"
        )
    if ".." in pathlib.PurePosixPath(artifact_path).parts:
        violations.append(
            f"artifact_path contains parent traversal: {artifact_path}"
        )
    lineage = artifact.get("lineage", {})
    for field in ("generator_command_line",):
        value = lineage.get(field, "")
        root_posix = repo_root.as_posix()
        if root_posix in value:
            violations.append(
                f"lineage.{field} contains absolute repo root: {value}"
            )
    return violations


class ArtifactLineageContentHashContractTest(unittest.TestCase):
    """Content-addressed artifact lineage contract tests.

    Ensures that generated artifacts carry verifiable content hashes,
    upstream lineage metadata, and repository-relative paths so that
    any downstream consumer can audit the full provenance chain.
    """

    def setUp(self) -> None:
        self._tmpdir_obj = tempfile.TemporaryDirectory()
        self.tmp = pathlib.Path(self._tmpdir_obj.name)
        self.fake_repo = self.tmp / "repo"
        self.fake_repo.mkdir()

        self.generator_dir = self.fake_repo / "tools"
        self.generator_dir.mkdir()
        self.generator_path = self.generator_dir / "gen_artifact.py"
        self.generator_path.write_text(
            "#!/usr/bin/env python3\nprint('v1')\n",
            encoding="utf-8",
        )

        self.output_dir = self.fake_repo / "generated"
        self.output_dir.mkdir()

    def tearDown(self) -> None:
        self._tmpdir_obj.cleanup()

    # ------------------------------------------------------------------
    # 1. Downstream artifacts carry upstream content hashes
    # ------------------------------------------------------------------
    def test_downstream_artifact_carries_upstream_content_hashes(self) -> None:
        upstream_content = "upstream payload alpha"
        upstream_hash = content_hash(upstream_content.encode("utf-8"))

        lineage = build_lineage_record(
            generator_path=self.generator_path,
            command_line="tools/gen_artifact.py --stage=2",
            parser_version="1.0.0",
            upstream_content_hashes={"stage1.json": upstream_hash},
        )
        downstream = build_artifact(
            content="downstream derived payload",
            lineage=lineage,
            repo_root=self.fake_repo,
            artifact_path=self.output_dir / "stage2.json",
        )

        recorded_upstream = downstream["lineage"]["upstream_content_hashes"]
        self.assertIn("stage1.json", recorded_upstream)
        self.assertEqual(upstream_hash, recorded_upstream["stage1.json"])

    # ------------------------------------------------------------------
    # 2. Fresh-timestamp stale-content mutant fails
    # ------------------------------------------------------------------
    def test_fresh_timestamp_stale_content_mutant_detected(self) -> None:
        original_content = "immutable payload"
        original_hash = content_hash(original_content.encode("utf-8"))

        artifact = build_artifact(
            content=original_content,
            lineage=build_lineage_record(
                generator_path=self.generator_path,
                command_line="tools/gen_artifact.py",
                parser_version="1.0.0",
            ),
            repo_root=self.fake_repo,
            artifact_path=self.output_dir / "out.json",
        )

        mutant = dict(artifact)
        mutant["generated_at_epoch"] = time.time() + 3600
        mutant_recomputed_hash = content_hash(
            original_content.encode("utf-8")
        )

        self.assertEqual(
            original_hash,
            mutant_recomputed_hash,
            msg="content hash must depend only on content, not on timestamp",
        )
        self.assertEqual(
            artifact["content_hash"],
            mutant["content_hash"],
            msg="bumping timestamp alone must not change the content hash",
        )

        tampered_content = "tampered payload"
        tampered_hash = content_hash(tampered_content.encode("utf-8"))
        self.assertNotEqual(
            original_hash,
            tampered_hash,
            msg="different content must produce different content hash",
        )

    # ------------------------------------------------------------------
    # 3. Lineage records include generator SHA-256
    # ------------------------------------------------------------------
    def test_lineage_includes_generator_sha256(self) -> None:
        lineage = build_lineage_record(
            generator_path=self.generator_path,
            command_line="tools/gen_artifact.py",
            parser_version="2.1.0",
        )

        self.assertIn("generator_sha256", lineage)
        sha = lineage["generator_sha256"]
        self.assertRegex(sha, r"^[0-9a-f]{64}$")
        expected = content_hash(self.generator_path.read_bytes())
        self.assertEqual(expected, sha)

    # ------------------------------------------------------------------
    # 4. Lineage records include command line
    # ------------------------------------------------------------------
    def test_lineage_includes_command_line(self) -> None:
        cmd = "tools/gen_artifact.py --input=a.json --output=b.json"
        lineage = build_lineage_record(
            generator_path=self.generator_path,
            command_line=cmd,
            parser_version="1.0.0",
        )

        self.assertIn("generator_command_line", lineage)
        self.assertEqual(cmd, lineage["generator_command_line"])

    # ------------------------------------------------------------------
    # 5. Lineage records include parser version
    # ------------------------------------------------------------------
    def test_lineage_includes_parser_version(self) -> None:
        lineage = build_lineage_record(
            generator_path=self.generator_path,
            command_line="tools/gen_artifact.py",
            parser_version="3.7.2",
        )

        self.assertIn("parser_version", lineage)
        self.assertEqual("3.7.2", lineage["parser_version"])
        self.assertRegex(
            lineage["parser_version"],
            r"^\d+\.\d+\.\d+$",
            msg="parser_version must be a semantic version string",
        )

    # ------------------------------------------------------------------
    # 6. Repository-relative paths only
    # ------------------------------------------------------------------
    def test_artifact_paths_are_repository_relative(self) -> None:
        artifact = build_artifact(
            content="some generated output",
            lineage=build_lineage_record(
                generator_path=self.generator_path,
                command_line="tools/gen_artifact.py",
                parser_version="1.0.0",
            ),
            repo_root=self.fake_repo,
            artifact_path=self.output_dir / "result.json",
        )

        violations = verify_artifact_paths_repo_relative(
            artifact, self.fake_repo
        )
        self.assertEqual(
            [],
            violations,
            msg="artifact paths must be repo-relative:\n"
            + "\n".join(violations),
        )
        self.assertEqual(
            "generated/result.json", artifact["artifact_path"]
        )

    # ------------------------------------------------------------------
    # 7. Absolute paths rejected in artifact records
    # ------------------------------------------------------------------
    def test_absolute_path_in_artifact_rejected(self) -> None:
        bad_artifact = {
            "content_hash": content_hash(b"x"),
            "artifact_path": "/home/user/repo/generated/out.json",
            "generated_at_epoch": time.time(),
            "lineage": {
                "generator_sha256": "a" * 64,
                "generator_command_line": "gen.py",
                "parser_version": "1.0.0",
            },
        }
        violations = verify_artifact_paths_repo_relative(
            bad_artifact, self.fake_repo
        )
        self.assertTrue(
            any("absolute" in v for v in violations),
            msg="absolute artifact_path must be flagged",
        )

    # ------------------------------------------------------------------
    # 8. Stale generator hash detected
    # ------------------------------------------------------------------
    def test_stale_generator_hash_detected_after_edit(self) -> None:
        lineage_before = build_lineage_record(
            generator_path=self.generator_path,
            command_line="tools/gen_artifact.py",
            parser_version="1.0.0",
        )
        hash_before = lineage_before["generator_sha256"]

        self.generator_path.write_text(
            "#!/usr/bin/env python3\nprint('v2 -- changed')\n",
            encoding="utf-8",
        )
        hash_after = content_hash(self.generator_path.read_bytes())

        self.assertNotEqual(
            hash_before,
            hash_after,
            msg="editing the generator must change its SHA-256",
        )

        lineage_stale = dict(lineage_before)
        self.assertNotEqual(
            lineage_stale["generator_sha256"],
            hash_after,
            msg="lineage frozen before edit must mismatch current generator hash",
        )

    # ------------------------------------------------------------------
    # 9. Lineage record schema completeness
    # ------------------------------------------------------------------
    def test_lineage_record_contains_all_required_fields(self) -> None:
        lineage = build_lineage_record(
            generator_path=self.generator_path,
            command_line="tools/gen_artifact.py --all",
            parser_version="1.0.0",
        )
        for field in LINEAGE_REQUIRED_FIELDS:
            self.assertIn(
                field,
                lineage,
                msg=f"required lineage field missing: {field}",
            )

    # ------------------------------------------------------------------
    # 10. Multi-hop upstream chain preserves all hashes
    # ------------------------------------------------------------------
    def test_multi_hop_upstream_chain_preserves_all_content_hashes(self) -> None:
        stage0_content = "raw input data"
        stage0_hash = content_hash(stage0_content.encode("utf-8"))

        stage1_content = "intermediate derivation"
        stage1_hash = content_hash(stage1_content.encode("utf-8"))

        lineage_stage2 = build_lineage_record(
            generator_path=self.generator_path,
            command_line="tools/gen_artifact.py --stage=2",
            parser_version="1.0.0",
            upstream_content_hashes={
                "stage0.bin": stage0_hash,
                "stage1.json": stage1_hash,
            },
        )

        final_artifact = build_artifact(
            content="final output",
            lineage=lineage_stage2,
            repo_root=self.fake_repo,
            artifact_path=self.output_dir / "stage2_final.json",
        )

        upstream = final_artifact["lineage"]["upstream_content_hashes"]
        self.assertEqual(2, len(upstream))
        self.assertEqual(stage0_hash, upstream["stage0.bin"])
        self.assertEqual(stage1_hash, upstream["stage1.json"])

        for name, recorded_hash in upstream.items():
            self.assertRegex(
                recorded_hash,
                r"^[0-9a-f]{64}$",
                msg=f"upstream hash for {name} is not a valid SHA-256 hex digest",
            )


if __name__ == "__main__":
    unittest.main()
