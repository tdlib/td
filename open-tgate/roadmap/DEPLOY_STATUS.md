# Open-TGate — Deploy Status & Credential Map

> Contains **names and public refs only** — never secret values. Secrets live
> in GitHub Actions secrets, Zeabur service secrets, or Cloudflare, never here.

## Verified facts (2026-09-21)

- **Supabase org:** `qbromdeoidotfakgtsvj` = **Kobeplay**.
- **open-tgate project:** **`hoseohvgoiarxluxqwqv` = "open-operations"** (region
  `ap-southeast-1`). ✅ VERIFIED — migration `open_tgate_runtime` applied and
  `public.open_tgate_worker_heartbeats` exists (RLS on, service-only, 0 rows =
  worker not yet running).
- **NOT open-tgate:** `huadtiuuoiriqrjpjxhr = "open-platform"` is a different
  project in the same org. Do not point the worker at it.

## Environment constraint

The agent session's egress policy denies `api.cloudflare.com`, `api.supabase.com`
(management), Docker Hub and Zeabur. So external deploys **must run from GitHub
Actions** (open egress) or the operator's own machine — not from the agent shell.
Supabase data-plane work is done via the Supabase MCP connector.

## Secret → platform → destination (names only)

| Secret name | Value kind | Set in | Used by |
|---|---|---|---|
| `CLOUDFLARE_API_TOKEN` | Cloudflare token (Workers Scripts: Edit) | GitHub Actions secrets | `deploy-dashboard.yml` |
| `CLOUDFLARE_ACCOUNT_ID` | Cloudflare account id | GitHub Actions secrets | `deploy-dashboard.yml` |
| `DOCKERHUB_USERNAME` | `hillstreet` | GitHub Actions secrets | `release-dockerhub.yml` |
| `DOCKERHUB_TOKEN` | Docker Hub PAT | GitHub Actions secrets | `release-dockerhub.yml` |
| `SUPABASE_URL` | `https://hoseohvgoiarxluxqwqv.supabase.co` | Zeabur api+worker | api, worker |
| `SUPABASE_SECRET_KEY` | Supabase secret (sb_secret_…) for open-operations | Zeabur api+worker (secret) | api, worker |
| `API_ADMIN_TOKEN` | random admin token | Zeabur api (secret) | api |
| `TELEGRAM_API_ID` / `TELEGRAM_API_HASH` | Telegram app creds | Zeabur worker (hash = secret) | worker |
| `SENTRY_DSN` | Sentry DSN (org hillstreet, project open-tgate) | Zeabur api+worker (secret) | api, worker |
| `ZEABUR` API key | Zeabur account token | operator/Zeabur only | provisioning |

Databricks / OpenAI / OpenRouter keys are **not** part of the open-tgate deploy
path and should not be added to these services (KB is Supabase + pgvector).

## Deploy steps (run outside the restricted agent shell)

1. **Website (Cloudflare):** add `CLOUDFLARE_API_TOKEN` + `CLOUDFLARE_ACCOUNT_ID`
   Actions secrets **and** set repo variable `DASHBOARD_DEPLOY_ENABLED=true`
   (the explicit opt-in gate) → `deploy-dashboard.yml` publishes the Worker on
   push / manual dispatch. The `validate` job always runs; the `deploy` job is
   skipped until the variable is set, and fails loudly if enabled without the
   secrets. Or locally: `cd open-tgate/dashboard && npx wrangler deploy`.
2. **Images (Docker Hub):** add `DOCKERHUB_USERNAME`/`DOCKERHUB_TOKEN` Actions
   secrets → push a semver tag → `release-dockerhub.yml` builds/publishes
   `open-tgate-api` + `open-tgate-worker`.
3. **Runtime (Zeabur):** create the project from `deploy/zeabur/`, pin an
   immutable image, attach the `/data/tdlib` volume, set the service secrets
   above; verify `/healthz`, `/readyz`, and a fresh heartbeat row.
4. **Repoint** the dashboard `API_BASE_URL` at the live Cloudflare-fronted API
   domain.
