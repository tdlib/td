# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: MIT

import pathlib
import shutil
import subprocess
import tempfile
import textwrap
import unittest

THIS_DIR = pathlib.Path(__file__).resolve().parent
REPO_ROOT = THIS_DIR.parent.parent
CMAKE_MODULE_DIR = REPO_ROOT / "CMake"
ADD_FLAG_MODULE = CMAKE_MODULE_DIR / "AddCXXCompilerFlag.cmake"
FIND_ATOMICS_MODULE = CMAKE_MODULE_DIR / "FindAtomics.cmake"
TD_SETUP_COMPILER_MODULE = CMAKE_MODULE_DIR / "TdSetUpCompiler.cmake"


def normalize_ws(text: str) -> str:
    return " ".join(text.split())


class CMakeProbeGuardrailsTest(unittest.TestCase):
    def test_lld_enablement_uses_link_capability_probe(self) -> None:
        module_text = TD_SETUP_COMPILER_MODULE.read_text(encoding="utf-8")

        self.assertIn("include(CheckCXXSourceCompiles)", module_text)
        self.assertIn(
            'check_cxx_source_compiles("int main() { return 0; }" TD_HAVE_FUSE_LD_LLD)',
            module_text,
        )

    def test_lld_enablement_does_not_use_compile_only_probe(self) -> None:
        module_text = TD_SETUP_COMPILER_MODULE.read_text(encoding="utf-8")

        self.assertNotIn(
            'check_cxx_compiler_flag("-fuse-ld=lld" TD_HAVE_FUSE_LD_LLD)', module_text
        )

    def test_lld_icf_enablement_is_guarded_off_for_sanitizer_builds(self) -> None:
        module_text = normalize_ws(TD_SETUP_COMPILER_MODULE.read_text(encoding="utf-8"))

        icf_pos = module_text.find(
            'set(TD_LINKER_FLAGS "${TD_LINKER_FLAGS} -Wl,--icf=safe")'
        )
        self.assertNotEqual(
            -1,
            icf_pos,
            msg="expected TdSetUpCompiler to configure lld ICF safe mode when supported",
        )

        sanitizer_guard = "if(NOT TD_SANITIZER_FLAGS_ACTIVE)"
        guard_pos = module_text.rfind(sanitizer_guard, 0, icf_pos)
        self.assertNotEqual(
            -1,
            guard_pos,
            msg=(
                "lld ICF must be disabled in sanitizer builds because sanitizer runtimes rely on precise symbol and "
                "frame identity during thread/interceptor setup"
            ),
        )

    def test_exclude_libs_all_is_guarded_off_for_sanitizer_builds(self) -> None:
        module_text = normalize_ws(TD_SETUP_COMPILER_MODULE.read_text(encoding="utf-8"))

        self.assertIn("set(TD_SANITIZER_FLAGS_ACTIVE OFF)", module_text)
        self.assertIn(
            'if((CMAKE_CXX_FLAGS MATCHES "sanitize") OR (CMAKE_C_FLAGS MATCHES "sanitize")',
            module_text,
        )
        self.assertIn('set(TD_LINKER_FLAGS "-Wl,--gc-sections")', module_text)
        self.assertIn(
            'set(TD_LINKER_FLAGS "-Wl,--gc-sections -Wl,--exclude-libs,ALL")',
            module_text,
        )

    def test_add_cxx_compiler_flag_probe_isolated_from_linker_flags(self) -> None:
        module_text = ADD_FLAG_MODULE.read_text(encoding="utf-8")

        self.assertIn("set(_SAVED_CMAKE_EXE_LINKER_FLAGS", module_text)
        self.assertIn("set(_SAVED_CMAKE_SHARED_LINKER_FLAGS", module_text)
        self.assertIn("set(_SAVED_CMAKE_MODULE_LINKER_FLAGS", module_text)
        self.assertIn('set(CMAKE_EXE_LINKER_FLAGS "")', module_text)
        self.assertIn('set(CMAKE_SHARED_LINKER_FLAGS "")', module_text)
        self.assertIn('set(CMAKE_MODULE_LINKER_FLAGS "")', module_text)

    def test_find_atomics_probe_isolated_from_linker_flags(self) -> None:
        module_text = FIND_ATOMICS_MODULE.read_text(encoding="utf-8")

        self.assertIn("set(_SAVED_CMAKE_EXE_LINKER_FLAGS", module_text)
        self.assertIn("set(_SAVED_CMAKE_SHARED_LINKER_FLAGS", module_text)
        self.assertIn("set(_SAVED_CMAKE_MODULE_LINKER_FLAGS", module_text)
        self.assertIn('set(CMAKE_EXE_LINKER_FLAGS "")', module_text)
        self.assertIn('set(CMAKE_SHARED_LINKER_FLAGS "")', module_text)
        self.assertIn('set(CMAKE_MODULE_LINKER_FLAGS "")', module_text)

    def setUp(self) -> None:
        self._tmp = tempfile.TemporaryDirectory(prefix="cmake-probe-guardrails-")
        self.tmp_path = pathlib.Path(self._tmp.name)

    def tearDown(self) -> None:
        self._tmp.cleanup()

    def _configure(self, project_cmake: str) -> subprocess.CompletedProcess[str]:
        source_dir = self.tmp_path / "src"
        build_dir = self.tmp_path / "build"
        source_dir.mkdir(parents=True, exist_ok=True)
        (source_dir / "CMakeLists.txt").write_text(project_cmake, encoding="utf-8")

        cmake_cmd = ["cmake", "-S", str(source_dir), "-B", str(build_dir)]
        if shutil.which("ninja") is not None:
            cmake_cmd.extend(["-G", "Ninja"])

        return subprocess.run(cmake_cmd, check=False, capture_output=True, text=True)

    def test_add_cxx_compiler_flag_does_not_mutate_cmake_required_flags(self) -> None:
        project = textwrap.dedent(f"""
            cmake_minimum_required(VERSION 3.16)
            project(GuardAddFlag CXX)
            set(CMAKE_MODULE_PATH "{CMAKE_MODULE_DIR.as_posix()}")

            include(AddCXXCompilerFlag)

            set(_expected_required_flags "-Werror=return-type -Werror=format")
            set(CMAKE_REQUIRED_FLAGS "${{_expected_required_flags}}")
            add_cxx_compiler_flag("-Wall")

            if(NOT CMAKE_REQUIRED_FLAGS STREQUAL _expected_required_flags)
              message(FATAL_ERROR "CMAKE_REQUIRED_FLAGS was unexpectedly mutated")
            endif()
            """)

        result = self._configure(project)

        self.assertEqual(
            0,
            result.returncode,
            msg=(
                "add_cxx_compiler_flag must preserve CMAKE_REQUIRED_FLAGS after probe execution.\n"
                f"stdout:\n{result.stdout}\n"
                f"stderr:\n{result.stderr}"
            ),
        )

    def test_find_atomics_succeeds_even_when_required_flags_include_werror(
        self,
    ) -> None:
        project = textwrap.dedent(f"""
            cmake_minimum_required(VERSION 3.16)
            project(GuardAtomics CXX)
            set(CMAKE_MODULE_PATH "{CMAKE_MODULE_DIR.as_posix()}")

            # This is the strict CI shape that previously made atomics probes fail-closed.
            set(CMAKE_REQUIRED_FLAGS "-Werror=return-type -Werror=deprecated -Werror=format")
            find_package(Atomics REQUIRED)
            """)

        result = self._configure(project)

        self.assertEqual(
            0,
            result.returncode,
            msg=(
                "FindAtomics must succeed when strict warning flags are present.\n"
                f"stdout:\n{result.stdout}\n"
                f"stderr:\n{result.stderr}"
            ),
        )


if __name__ == "__main__":
    unittest.main()
