# Zeabur deployment — Open-TGate

Zeabur is the canonical always-on production runtime for the Open-TGate backend.
It runs two persistent services from immutable Docker Hub images:

| Service | Image | Public? | Persistence | Restart |
|---|---|---|---|---|
| `api` | `${DOCKERHUB_USERNAME}/open-tgate-api` | Yes (HTTP :8000) | stateless | on-failure |
| `worker` | `${DOCKERHUB_USERNAME}/open-tgate-worker` | No | volume at `/data/tdlib` | always |

`deploy/zeabur/template.yaml` is a scaffold of this topology. The dashboard steps
below are authoritative.

## Deployment flow

```
GitHub (main) → CI → versioned image → Docker Hub → Zeabur (this doc) → Supabase migrations → health checks → verified
```

Cloudflare provides DNS/TLS/WAF in front of the Zeabur API domain.

## One-time setup

1. **Create a Zeabur project** (single region close to the Supabase region,
   `ap-southeast-1`).
2. **API service** — deploy the prebuilt image
   `${DOCKERHUB_USERNAME}/open-tgate-api` pinned to an immutable tag or
   `@sha256` digest (never `latest` in production).
   - Expose port `8000` (HTTP).
   - Set the **health-check path to `/healthz`** (Service → Settings →
     Health Check). Readiness is additionally reported at `/readyz`.
   - Restart policy: on-failure.
3. **Worker service** — deploy the prebuilt image
   `${DOCKERHUB_USERNAME}/open-tgate-worker` pinned to an immutable tag/digest.
   - No public port.
   - Attach a **persistent volume mounted at `/data/tdlib`** — this holds the
     encrypted TDLib session/database state and MUST survive redeploys.
   - Restart policy: always.
4. **Bind a domain** to the API service and place it behind Cloudflare
   (proxied, WAF on). Point the dashboard Worker's `API_BASE_URL` at it.

## Required environment variables / secrets

Set these in each Zeabur service. Secrets go in the service **secret** store;
never commit them. Names only (see `open-tgate/.env.example` for the full
contract):

Both services:
- `SUPABASE_URL`
- `SUPABASE_SECRET_KEY` *(secret)*
- `SENTRY_DSN` *(secret; optional but recommended)*
- `SENTRY_ENVIRONMENT` (e.g. `production`)
- `EXTERNAL_SEND_ENABLED=false` *(keep false until approval controls pass)*

API only:
- `API_ADMIN_TOKEN` *(secret)*
- `DASHBOARD_ORIGIN`

Worker only:
- `TELEGRAM_API_ID`
- `TELEGRAM_API_HASH` *(secret)*
- `WORKER_ID`
- `TDLIB_DATABASE_DIRECTORY=/data/tdlib/db`
- `TDLIB_FILES_DIRECTORY=/data/tdlib/files`

Telegram user-session material lives ONLY on the worker's `/data/tdlib` volume —
never in GitHub Secrets, Zeabur variables, or the database.

## Verification (do not report DEPLOYED until these pass)

1. API health: `GET https://<api-domain>/healthz` → `200 {"status":"ok"}`.
2. API readiness: `GET /readyz` → `ready: true` once all secrets are set.
3. Worker liveness: a row for `WORKER_ID` appears in
   `public.open_tgate_worker_heartbeats` (Supabase) with a recent `last_seen_at`.
4. Redeploy the worker and confirm `/data/tdlib` state persists (no re-import).
5. Errors surface in the Sentry `open-tgate` project (org `hillstreet`).

## Rollback

Redeploy the previous immutable image tag/`@sha256` digest. Because worker state
lives on the persistent volume, rolling the image back does not lose TDLib
session state.

## Migration from Railway

The legacy `open-tgate/railway.*.toml` files describe the prior Railway runtime
and are retained only for reference during migration. Zeabur is the canonical
runtime; do not add new Railway configuration.
