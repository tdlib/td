<!--
SPDX-FileCopyrightText: Copyright 2026 telemt community
SPDX-License-Identifier: MIT
telemt: https://github.com/telemt
telemt: https://t.me/telemtrs
-->

# Fork Maintenance Policy

**Document Version:** 1.0  
**Date:** 2026-05-24  
**Scope:** Maintainer workflow for exact upstream baselines and selective downstream intake

`tdlib-obf` is maintained as a vendor/security fork of TDLib. This note turns the repository-level
policy into a repeatable maintainer workflow.

---

## Upstream Baseline Tag Policy (2026-05-24)

1. Before any new selective upstream intake cycle, fetch `upstream` and create an immutable
   annotated tag pointing to the exact upstream commit that will serve as the intake baseline.
2. Use the naming convention `upstream-baseline-YYYY-MM-DD-<12sha>`.
3. The baseline tag must point to an unmodified upstream commit. Never retarget, reuse, or
   force-move an existing baseline tag.
4. If a review branch is useful, create a companion reference branch from the same commit named
   `upstream-reference/YYYY-MM-DD-<12sha>`. Do not land downstream commits on that branch.
5. Record the baseline tag name and upstream commit in the active changelog or backport plan for
   the intake cycle.
6. `master` remains the downstream integration branch. Baseline tags document provenance; they do
   not imply semantic parity and they must not be used to justify ancestry rewrites.

## Maintainer Workflow

From the repository root:

```bash
git fetch upstream --tags
git tag -a upstream-baseline-2026-05-24-e0943d068 e0943d068 \
  -m "Exact upstream baseline for selective intake started 2026-05-24"
git branch upstream-reference/2026-05-24-e0943d068 e0943d068
```

Minimum required artifact: the annotated tag.

Optional convenience artifact: the read-only reference branch.

## Release Checklist Note

Before cutting a downstream release:

1. Verify that the release notes or changelog name the most recent upstream baseline tag used for
   the intake cycle.
2. Verify that the backport manifest or gating plan records whether each reviewed upstream delta was
   cherry-picked, locally adapted, deferred, rejected, or consumed elsewhere.
3. Verify that no downstream-only commits were added to the exact upstream reference branch and that
   no baseline tag was moved after publication.