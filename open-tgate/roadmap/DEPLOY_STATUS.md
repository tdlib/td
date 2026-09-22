# Open-TGate — Deploy Guide & Credential Map

> Contains **names and public refs only** — never secret values. Secrets live
> in GitHub Actions secrets, Zeabur service secrets, or Cloudflare, never here.

## Provider configuration

- **Supabase org:** `qbromdeoidotfakgtsvj` = **Kobeplay**.
- **open-tgate project:** **`hoseohvgoiarxluxqwqv` = "open-operations"** (region
  `ap-southeast-1`). Migration `open_tgate_runtime` is applied and
  `public.open_tgate_worker_heartbeats` exists (RLS on, service-only). Point the
  api and worker at this project.

## Secret → platform → destination (names only)

| Secret name | Value kind | Set in | Used by |
|---|---|---|---|
| `CLOUDFLARE_API_TOKEN` | Cloudflare token (Workers Scripts: Edit) | GitHub Actions secrets | `deploy-dashboard.yml` |
| `CLOUDFLARE_ACCOUNT_ID` | Cloudflare account id | GitHub Actions secrets | `deploy-dashboard.yml` |
| `DOCKERHUB_USERNAME` | Docker Hub username | GitHub Actions secrets | `release-dockerhub.yml` |
| `DOCKERHUB_TOKEN` | Docker Hub PAT | GitHub Actions secrets | `release-dockerhub.yml` |
| `SUPABASE_URL` | `https://hoseohvgoiarxluxqwqv.supabase.co` | Zeabur api+worker | api, worker |
| `SUPABASE_SECRET_KEY` | Supabase secret key (open-operations) | Zeabur api+worker (secret) | api, worker |
| `API_ADMIN_TOKEN` | random admin token | Zeabur api (secret) | api |
| `TELEGRAM_API_ID` / `TELEGRAM_API_HASH` | Telegram app creds | Zeabur worker (hash = secret) | worker |
| `SENTRY_DSN` | Sentry DSN (org hillstreet, project open-tgate) | Zeabur api+worker (secret) | api, worker |
| Zeabur API key | Zeabur account token | operator/Zeabur only | provisioning |

The knowledge base is served by Supabase + pgvector; no additional data or model
platform is part of the open-tgate deploy path.

## Operator console (login)

- URL: `/app` on the dashboard Worker. Login is Supabase Auth **email magic
  link** (no password) using the project's publishable key + URL (public, in
  `wrangler.toml` `[vars]`). Access is gated by `public.open_tgate_operators`
  (email allowlist) and RLS; a signed-in non-operator can read nothing.
- Live worker status is read directly from Supabase (RLS-restricted to
  operators), so the console works as soon as the Worker is served — it does
  not require the API to be deployed first.
- Manage operators: insert/disable rows in `public.open_tgate_operators`
  (service role). The initial admin is seeded.
- Supabase requirement: the dashboard's public origin must be listed under
  Auth → URL Configuration → Redirect URLs (e.g. `https://<worker-domain>/app`)
  so magic links return to the console.

## Deploy steps

1. **Website (Cloudflare):** add `CLOUDFLARE_API_TOKEN` + `CLOUDFLARE_ACCOUNT_ID`
   Actions secrets **and** set repo variable `DASHBOARD_DEPLOY_ENABLED=true`
   (the explicit opt-in gate). `deploy-dashboard.yml` then publishes the Worker
   on push / manual dispatch: the `validate` job always builds the Worker; the
   `deploy` job is skipped until the variable is set and fails loudly if enabled
   without the secrets. Local equivalent:
   `cd open-tgate/dashboard && npx wrangler deploy`.
2. **Images (Docker Hub):** add `DOCKERHUB_USERNAME` / `DOCKERHUB_TOKEN` Actions
   secrets → push a semver tag → `release-dockerhub.yml` builds and publishes
   `open-tgate-api` + `open-tgate-worker`.
3. **Runtime (Zeabur):** create the project from `deploy/zeabur/`, pin an
   immutable image, attach the `/data/tdlib` volume, set the service secrets
   above; verify `/healthz`, `/readyz`, and a fresh heartbeat row.
4. **Repoint** the dashboard `API_BASE_URL` at the live Cloudflare-fronted API
   domain.
