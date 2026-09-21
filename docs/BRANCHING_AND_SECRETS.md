# Open-TGate Branching, Environments and Secrets

## Canonical branch model

- `main` — production only. Receives reviewed promotion PRs from `staging` or emergency `hotfix/*` branches.
- `staging` — pre-production integration and release verification.
- `develop` — active integration branch for completed feature work.
- `feature/*` — product features; branch from `develop`, PR back to `develop`.
- `fix/*` — non-emergency fixes; branch from `develop`, PR back to `develop`.
- `security/*` — security changes; branch from `develop`; expedited review allowed, never bypass validation.
- `infra/*` — infrastructure/IaC changes.
- `dependency/*` — dependency upgrades.
- `upstream-sync/*` — TDLib upstream imports. Never merge directly to production.
- `release/*` — optional stabilization branches cut from `staging`.
- `hotfix/*` — production emergency fixes cut from `main`, then back-merged to `staging` and `develop`.

`master` is the legacy/upstream TDLib baseline and must not be treated as the Open-TGate production deployment branch after migration to `main`.

## Promotion path

```text
feature/* | fix/* | security/* | infra/*
                    |
                    v
                 develop
                    |
               PR + CI gate
                    v
                 staging
                    |
         integration / smoke / security
                    |
               PR + approval
                    v
                   main
                    |
          immutable production release
```

## GitHub Environments

Create three GitHub Environments:

1. `development`
2. `staging`
3. `production`

Production should require manual approval and should only accept deployments from `main`.

## Repository/Environment secret contract

Store values in GitHub Actions Secrets or environment secrets. Never commit values.

### Telegram / TDLib
- `TELEGRAM_API_ID`
- `TELEGRAM_API_HASH`
- `TELEGRAM_BOT_TOKEN`

Telegram user-session database material must NOT be stored in GitHub Secrets. It belongs on the encrypted persistent runtime volume.

### Supabase
- `SUPABASE_PROJECT_REF`
- `SUPABASE_URL`
- `SUPABASE_PUBLISHABLE_KEY`
- `SUPABASE_SECRET_KEY`
- `SUPABASE_ACCESS_TOKEN`
- `DATABASE_URL`
- `DATABASE_DIRECT_URL`

### Cloudflare
Prefer a scoped API token over the Global API Key.
- `CLOUDFLARE_API_TOKEN`
- `CLOUDFLARE_ACCOUNT_ID`
- `CLOUDFLARE_ZONE_ID`
- `CLOUDFLARE_R2_ACCESS_KEY_ID`
- `CLOUDFLARE_R2_SECRET_ACCESS_KEY`
- `CLOUDFLARE_R2_ENDPOINT`

### Zeabur (canonical runtime)
- `ZEABUR_API_KEY` (deployment automation only)

### Sentry (observability)
- `SENTRY_DSN` (runtime reporting for api + worker)
- `SENTRY_AUTH_TOKEN` (release/source-map upload only)

### Railway (legacy — retained for migration reference only)
Do not add new Railway configuration; Zeabur is the canonical runtime.
- `RAILWAY_TOKEN`
- `RAILWAY_PROJECT_ID`
- `RAILWAY_ENVIRONMENT_ID`
- `RAILWAY_SERVICE_ID`

### FastAPI deployment
- `FASTAPI_CLOUD_TOKEN` (only when the selected FastAPI deployment provider supports token-based CI deployment)

### Notion
- `NOTION_TOKEN`
- `NOTION_PRIVATE_OPERATIONS_PAGE_ID`

### Pipedream
- `PIPEDREAM_CLIENT_ID`
- `PIPEDREAM_CLIENT_SECRET`
- `PIPEDREAM_PROJECT_ID`

### Container registry (optional)
- `DOCKERHUB_USERNAME`
- `DOCKERHUB_TOKEN`

## Public/non-secret configuration

Use GitHub Actions Variables for non-sensitive values:
- `APP_NAME=open-tgate`
- `SUPABASE_REGION=ap-northeast-1`
- `EXTERNAL_SEND_ENABLED=false`
- `TDLIB_DATABASE_DIRECTORY=/data/tdlib/db`
- `TDLIB_FILES_DIRECTORY=/data/tdlib/files`

## Required checks

Before `develop` merge:
- source formatting/lint
- TDLib build smoke test
- application unit tests
- secret scan
- dependency review

Before `staging` promotion:
- all develop checks
- container build
- migration validation
- API health/readiness tests
- TDLib worker startup test
- idempotency/restart test

Before `main` promotion:
- staging deployment healthy
- database migration reviewed
- security checks pass
- backup/recovery checkpoint recorded
- manual production approval

## Production rules

- Never use plaintext credentials in workflow YAML.
- Never print secrets or complete connection strings.
- Never expose Supabase secret/service credentials to browser code.
- Never use the GitHub PAT supplied by an operator as an application runtime credential.
- Prefer GitHub OIDC or narrowly scoped provider tokens where supported.
- `EXTERNAL_SEND_ENABLED` remains `false` until outbound Telegram approval controls pass acceptance tests.
