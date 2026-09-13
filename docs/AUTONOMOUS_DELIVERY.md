# Autonomous Delivery Contract

Repository: `hillstreet-ph/open-tgate`  
Production branch: `master`  
Control plane: GitHub Issues, pull requests, checks, tags, and releases.

## Goal

Keep development safe for scheduled AI workers (ChatGPT Work, Claude Cowork, Grok, and compatible agents) while producing traceable Semantic Version releases and Docker images where the repository has a validated container contract.

## Source of truth

1. Read this file, `README.md`, `AGENTS.md`/agent instructions, roadmap, open issues, open pull requests, and current workflow runs.
2. Search before creating. Reuse or update an existing issue, branch, pull request, workflow, version file, release, or Docker repository.
3. Never replace a working workflow with a second competing publisher.
4. `open-model` is the replacement for `open-box`; do not restore or create Open-Box references.
5. Record evidence in the pull request: commands/checks run, failures, root cause, risk, and rollback reference.

## Worker loop

`DISCOVER → SELECT ISSUE → SYNC BASE → BRANCH → IMPLEMENT → VALIDATE → PR → CHECKS/REVIEW → MERGE → RELEASE → VERIFY`

- One issue and one bounded objective per branch.
- Branches: `feature/*`, `fix/*`, `ai-fix/*`, `release/*`.
- Conventional commits: `feat:`, `fix:`, `docs:`, `refactor:`, `test:`, `build:`, `ci:`, `chore:`.
- Before editing, inspect overlapping open PRs and recent commits.
- Rebase or merge the current production branch before requesting final review.
- Never force-push a shared branch or bypass required checks.
- Merge only when required checks pass, review requirements are met, conversations are resolved, and the PR is conflict-free.

## Autonomous repair

An agent may diagnose logs, make a low-risk deterministic repair on `ai-fix/*`, rerun validation, and update the same PR. Maximum: three repair cycles. After that, preserve evidence and open/update a blocker issue.

Always stop for credentials, spending, destructive data changes, auth/authorization/RLS, secrets, DNS, production storage/networking, or a breaking migration. Autonomous does not mean bypassing production safety.

## Version and release contract

- Semantic Versioning: patch for compatible fixes, minor for compatible features, major for breaking changes.
- Release notes summarize features, fixes, breaking changes, security impact, migrations, deployment, and rollback.
- Git tag, GitHub Release, source SHA, Docker tag/digest (when applicable), and deployed revision must be traceable to each other.
- Docker tags: full semver, major.minor, major, `sha-<commit>`, and `latest` only as a convenience pointer.
- Production rollback uses an immutable Git SHA or image digest, never `latest` alone.

Missing: add release-please and Docker Hub tag workflow in this change.

## Docker contract

Dockerfile present; requires DOCKERHUB_USERNAME and DOCKERHUB_TOKEN.

Images must be built without embedded runtime secrets. Pull-request jobs must not receive registry credentials. A published image is not production-verified until startup, readiness/health, and critical paths have been checked.

## Scheduling and integrations

GitHub Actions is the canonical execution engine. Composio and Pipedream may trigger, observe, summarize, or synchronize GitHub state, but must not independently tag, release, merge, or deploy the same event. Scheduled agents must use GitHub idempotency keys: existing issue/PR URL, branch name, release tag, and workflow run ID.

## Definition of done

- Acceptance criteria met and linked to an issue.
- Required lint, typecheck, tests, build, security, and container checks pass when present.
- No unresolved conflicts or review conversations.
- Documentation, roadmap, changelog/release notes, and rollback guidance updated when affected.
- Release artifacts and production state are reported only when verified.
