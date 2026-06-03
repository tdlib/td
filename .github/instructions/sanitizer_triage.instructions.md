---
description: "Activation reminder for sanitizer triage. Use when investigating ASan/UBSan/LSan/MSan/TSan findings, memory corruption, leaks, or order-dependent test crashes. Prefer the repository sanitizer matrix runner and exact repros before editing."
name: sanitizer-triage
applyTo: "**"
---

# Sanitizer Triage Instructions

Use this instruction whenever the task mentions AddressSanitizer, LeakSanitizer, UndefinedBehaviorSanitizer, ThreadSanitizer, MemorySanitizer, `build-asan`, `build-ubsan`, `runtime error:`, heap-use-after-free, leaks, or a failing sanitizer matrix lane.

## Canonical Runner

- Run sanitizer lanes from the repository root with `python3 tools/ci/run_sanitizer_matrix.py`.
- Prefer the matrix runner over ad hoc configure/build/test chains because it selects the sanitizer presets from `CMakePresets.json`, stores per-lane logs under `artifacts/sast/sanitizer_matrix`, emits resumable run metadata, and preserves aggregated findings.
- Preferred commands:
  - ASan: `python3 tools/ci/run_sanitizer_matrix.py --lanes asan --jobs 14 --status-detail detailed --findings-report artifacts/sast/sanitizer_matrix/asan_findings.json`
  - UBSan: `python3 tools/ci/run_sanitizer_matrix.py --lanes ubsan --jobs 14 --status-detail detailed --findings-report artifacts/sast/sanitizer_matrix/ubsan_findings.json`
  - Full matrix with early stop: `python3 tools/ci/run_sanitizer_matrix.py --jobs 14 --status-detail detailed --stop-on-failure`
- Single-lane TSan and MSan investigations use the same command shape with `--lanes tsan` or `--lanes msan`.
- For long-running stress lanes, raise the timeout with `--ctest-timeout <seconds>` or pass `--ctest-timeout 0` for an effectively unbounded timeout.
- Resume and monitor commands:
  - `python3 tools/ci/run_sanitizer_matrix.py --resume-run-dir <run-dir>`
  - `python3 tools/ci/run_sanitizer_matrix.py --monitor --run-dir <run-dir> --follow`
- If compiler memory pressure becomes the blocker during sanitizer builds, reduce `--jobs`; do not replace the matrix runner with an unrelated manual workflow.

## First Triage Pass

- Start from the first failing lane and the first sanitizer finding in that lane. Later reports may be fallout.
- Read the first user-code frame before allocator, libc, or sanitizer runtime frames.
- Preserve the exact failing lane, test name, command, and run artifact path in your working notes or response.
- Do not add suppressions, disable leak detection, or relax sanitizer flags unless the user explicitly asks for that tradeoff.

## Focused Reproduction

- After the runner identifies a failing test, reproduce it inside the matching build dir:
  - ASan: `./build-asan/test/run_all_tests --exact <TestName>`
  - UBSan: `./build-ubsan/test/run_all_tests --exact <TestName>`
- If the failure depends on earlier tests, teardown state, or cumulative allocator state, reproduce with `--offset <TestName>` instead of `--exact`.
- Order-dependent ASan examples:
  - `./build-asan/test/run_all_tests --offset Test_HazardPointers_stress`
  - `./build-asan/test/run_all_tests --offset Test_Misc_to_double`
  - `./build-asan/test/run_all_tests --offset <TestName>`
- If the failing case already exists as a CTest test, prefer:
  - `ctest --test-dir build-asan --output-on-failure -j 14 -R <pattern>`
  - `ctest --test-dir build-ubsan --output-on-failure -j 14 -R <pattern>`
- When new `run_all_tests` cases were added or renamed, refresh CTest discovery with: configure, build `run_all_tests`, then configure again.
- Do not widen scope between the first fix and the first rerun of the exact reproducer.

## Fix Rules

- Fix the root cause in production code or test teardown; do not patch around the symptom.
- For LSan, inspect final owners after thread joins, thread-local shutdown, atomics, retire queues, and per-test teardown.
- For ASan and UBSan, validate object lifetime, bounds, integer conversions, ownership transfer, and teardown ordering before assuming flakiness.
- Keep the fix set narrow until the reproducer is green.

## Validation Ladder

- Rerun the exact reproducer first.
- If the original failure required order, rerun the `--offset` reproducer next.
- Then rerun the affected lane with `tools/ci/run_sanitizer_matrix.py`.
- Only after the lane is clean should you widen to the rest of the matrix, if the task requires it.

## Red Flags

- Treat `SUMMARY: AddressSanitizer`, `SUMMARY: LeakSanitizer`, `runtime error:`, and sanitizer aborts as real defects until disproved.
- A passing `--exact` test is not sufficient if the original failure required `--offset` or full-lane order.
- Do not trust editor-only diagnostics over a passing sanitizer lane unless the actual sanitizer build or test run fails.