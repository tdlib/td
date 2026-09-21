# Platform backend assignment

This repository is assigned to the shared **open-operations** Supabase project.

## Non-secret configuration

| Setting | Value |
|---|---|
| Supabase project | `open-operations` |
| Supabase project reference | `hoseohvgoiarxluxqwqv` |
| Supabase URL | `https://hoseohvgoiarxluxqwqv.supabase.co` |
| PostgreSQL schema | `open_tgate` |
| Sentry organization | `hillstreet` |
| Sentry project | `open-tgate` |
| GitHub repository | `hillstreet-ph/open-tgate` |
| Container image | `${DOCKERHUB_USERNAME}/open-tgate` |

## Required deployment secrets

Configure these only in GitHub environment secrets and the Zeabur service secret store:

- `SUPABASE_SERVICE_ROLE_KEY`
- `SUPABASE_DB_URL` when server-side migrations or direct PostgreSQL access are required
- `DOCKERHUB_USERNAME`
- `DOCKERHUB_TOKEN`
- `ZEABUR_API_KEY` only for deployment automation that requires it
- `SENTRY_DSN` runtime error/performance reporting for the api and worker services
- `SENTRY_AUTH_TOKEN` only for release/source-map upload
- Application-specific secrets

## Runtime database objects

The `20260829000100_open_tgate_runtime` migration is applied to `open-operations`.
It is additive and isolated from the other apps sharing the project:

- schema `open_tgate` — reserved canonical home for the Open-TGate domain model
- table `public.open_tgate_worker_heartbeats` — service-only runtime liveness,
  RLS enabled, written only with the Supabase secret (service_role) key

## Scope

The canonical stack is GitHub, Docker Hub, Cloudflare, Supabase, Zeabur, and
Sentry. **Databricks is not part of this stack** and is not provisioned here; it
would require an explicit approval and a stated technical requirement before any
integration is added.

Public client builds may use the Supabase URL and publishable key. Never expose the service-role key, database password, Docker Hub token, Zeabur key, or Sentry auth token in source code, build logs, Google Drive, or client bundles.

## Deployment boundary

1. GitHub is the source of truth.
2. CI validates the project and builds a versioned container.
3. Docker Hub stores the approved image.
4. Zeabur deploys the approved immutable image.
5. Cloudflare provides DNS, TLS, WAF, and public routing.
6. Sentry records releases and runtime errors.
7. Supabase migrations must target schema `open_tgate` and must be reviewed before production execution.

## Current credential status

The previously shared Supabase, Docker Hub, and Zeabur management tokens were exposed in chat and must be rotated before write-enabled CI/CD or deployment is activated.
