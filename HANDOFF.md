<!-- SPDX-FileCopyrightText: Copyright 2026 telemt community -->
<!-- SPDX-License-Identifier: MIT -->
<!-- telemt: https://github.com/telemt -->
<!-- telemt: https://t.me/telemtrs -->

# Handoff — Sanitizer Triage (WIP)

Commit to audit: `b40170cceffb9b6bd812fc53cb523aab50dd26f5`

Repository: [AGENTS.md](AGENTS.md)
Instructions: [.github/instructions/](.github/instructions/)

Summary
- This handoff captures current state after focused RED→GREEN TDD work on OpenSSL init MSan findings.
- The commit above contains the staged changes that implement fixes and new focused tests. A push attempt was made but may require developer credentials in your environment.

What's already fixed (verified with focused tests)
- TL generator/tree unpoisoning fixes and tests
- Raw-storage lifetime/ownership fixes in `tdtl/td/tl/tl_config.cpp` (tests added)
- Logging startup global-init MSan fix in `td/telegram/Logging.cpp` (tests added)
- OpenSSL random-init hardening in `tdutils/td/utils/crypto.cpp` and `tdutils/td/utils/Random.cpp` (tests added)
- New tests added and passing locally (focused):
  - `test/random_openssl_init_contract.cpp`
  - `test/random_openssl_init_integration.cpp`

Active blocker(s)
- Canonical sanitizer runs still report issues (MSan was the frontier during triage). Full multi-lane sanitizer pass is pending and required.

Key artifacts / logs
- Last focused MSan artifact (investigation): `artifacts/sast/sanitizer_matrix/msan_findings_after_random_init_fix.json`
- Full CTest run (context-mode): large indexed output in CI run (see local `build/Testing/` or saved artifacts)

How to reproduce locally (recommended)
1. Rebuild and run focused tests (12 cores recommended):

```bash
cmake --build build --parallel 12
ctest --test-dir build --output-on-failure -j 12
```

2. Run the canonical sanitizer matrix (CI runner):

```bash
python3 tools/ci/run_sanitizer_matrix.py --lanes asan,lsan,tsan,ubsan,msan --jobs 10 --status-detail detailed --findings-report artifacts/sast/sanitizer_matrix/full_sanitizer_findings.json
```

Notes for the reviewer
- Please audit the commit `b40170cceffb9b6bd812fc53cb523aab50dd26f5` for:
  - Sanitizer regressions (ASan/LSan/TSan/UBSan/MSan)
  - SonarCloud / linter findings (no critical/high/medium allowed in new files)
  - OWASP ASVS L2 alignment for security-sensitive code paths (crypto, RNG, deserialization, networking)
  - C++23 idiomatic usage and Core Guidelines compliance
- The repository contains `.github/instructions/` with specific policies (TDD, sanitizer triage, PR rules). Reviewers must follow them.
- Important files that were changed:
  - `tdutils/td/utils/crypto.cpp`
  - `tdutils/td/utils/Random.cpp`
  - `tdtl/td/tl/*` (various TL generator files)
  - `td/telegram/Logging.cpp`
  - many test files under `test/` and `test/analysis/`

Requested reviewer actions (high priority)
- Run the full sanitizer matrix and triage any findings (see sanitizer triage instructions in `.github/instructions/sanitizer_triage.instructions.md`).
- Run SonarCloud / linters and fix or mark known acceptable findings. New/changed files must have 0 critical/high/medium issues.
- Audit new tests for adversarial strength and ensure they follow the TDD approach in `.github/instructions/TDD_approach.instructions.md`.
- If push to remote is needed, please push this branch after verifying CI and sanitize-run results.

Contact/Context
- Context: this change was produced during a focused MSan triage; a subset of fixes progressed the frontier, but full sanitizer-clean is not yet achieved. See commit for exact diff.

---
Handed off by local dev automation. Please attach review notes to the PR or commit review referencing the commit hash above.
