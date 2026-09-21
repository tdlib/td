# Open-TGate Application Layer

This directory contains the HillStreet application layer built around TDLib. Keep product-specific code here instead of modifying TDLib internals.

## Planned services

- `worker/` — persistent TDLib/tdjson synchronization worker
- `api/` — authenticated API for the dashboard and controlled integrations
- `dashboard/` — private operations UI
- `supabase/` — SQL migrations, RLS policies, database functions and storage policy
- `shared/` — schemas/contracts shared between services
- `tests/` — unit, integration, restart/recovery and authorization tests

## First production vertical slice

1. Load native `libtdjson`.
2. Complete Telegram authorization interactively through an authorized operator flow.
3. Persist TDLib state on a mounted volume.
4. Discover the connected account and chats.
5. Persist normalized account/chat records to Supabase.
6. Backfill one selected chat with a durable checkpoint.
7. Restart the worker and prove it resumes rather than re-importing everything.
8. Receive a live message and UPSERT it idempotently.
9. Emit an outbox event after the database transaction succeeds.
10. Keep outbound Telegram sending disabled.

Do not add AI/Notion dependencies to the Telegram synchronization process. Downstream intelligence consumes durable database/outbox records.

See `docs/OPEN_TGATE_ARCHITECTURE.md` for the complete target architecture.

## Implemented production slice

- FastAPI health, readiness and protected system endpoints in `app/`
- persistent TDLib worker bootstrap with Supabase heartbeat
- separate API and worker container definitions
- Cloudflare Worker landing page (`dashboard/`) with live `/healthz` status,
  strict CSP, and no data API surface at the edge
- service-only Supabase heartbeat migration (`public.open_tgate_worker_heartbeats`)
- optional Sentry error/performance reporting (DSN-gated; no-op when unset)
- unit, lint, container and Cloudflare dry-run checks in CI

### Deployment

Zeabur is the canonical always-on runtime. See
[`deploy/zeabur/README.md`](../deploy/zeabur/README.md) for the api + worker
service topology, required environment/secrets, health-check configuration, and
rollback. Images are published to Docker Hub by
`.github/workflows/release-dockerhub.yml` on semver tags.

### Local API

```bash
python -m venv .venv
. .venv/bin/activate
pip install -r requirements-dev.txt
pytest -q
uvicorn app.main:app --reload
```

### Safety state

`EXTERNAL_SEND_ENABLED=false` is the enforced default. The worker must pass
authorization, persistent-volume restart, checkpoint and idempotent ingestion
tests before sending can be enabled.
