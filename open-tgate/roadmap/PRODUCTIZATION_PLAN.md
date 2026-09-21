# Open-TGate — Productization Plan (TDLib sync, AI knowledge base, deploy)

Status legend (per project convention): **IMPLEMENTED** (code exists) ·
**CONFIGURED** (wired but not proven) · **DEPLOYED** (running somewhere) ·
**VERIFIED** (evidence of correct behavior) · **BLOCKED** (needs credentials
or a decision).

This document is the phased design for the larger request: synchronize all
connected Telegram accounts (contacts, groups, channels, bots, files) in
real time, avoid bans, feed an AI knowledge base, and go live on
Zeabur/Cloudflare/Supabase/Sentry. It is intentionally scoped in phases
because the full build is multi-week and parts of the naive request conflict
with the repository's own safety rules.

---

## 0. What already exists (verified by inspection)

- FastAPI `api` (`app/main.py`): `/healthz`, `/readyz`, admin-gated
  `/api/v1/system`. **IMPLEMENTED / VERIFIED (unit tests present).**
- Persistent `worker` (`app/worker.py`): loads `libtdjson`, ensures the
  TDLib volume dirs, publishes a Supabase heartbeat loop. **IMPLEMENTED.**
  The worker does **not** yet run a TDLib auth flow or ingest entities.
- Supabase migration `20260829000100_open_tgate_runtime.sql`:
  `public.open_tgate_worker_heartbeats`. **IMPLEMENTED.**
- Cloudflare Worker dashboard: now a full professional landing page that
  proxies only `/healthz`. **IMPLEMENTED / VERIFIED (wrangler dry-run).**
- Docker `api` + `worker` images; Zeabur template + README; Docker Hub
  release workflow on semver tags. **CONFIGURED.** No live deploy proven.

**Not yet built:** TDLib authorization, multi-account session management,
entity/message sync (contacts, chats, channels, bots, files), rate-limit /
ban-avoidance controls, the outbox consumer, and any AI knowledge base.

---

## 1. Design constraints from the repo (must be honored)

1. `EXTERNAL_SEND_ENABLED=false` stays the enforced default. Sync is
   **read-only** against Telegram until approval controls pass.
2. "Do not add AI/Notion dependencies to the Telegram synchronization
   process." → The sync worker writes durable records only. The AI
   knowledge base is a **separate downstream consumer** of Supabase / the
   outbox, never coupled into the sync loop.
3. Telegram session material lives only on the worker's encrypted volume —
   never in GitHub Secrets, Zeabur variables, or the database.
4. No new production platform without approval. **Databricks is not in the
   canonical stack** (GitHub, Docker Hub, Cloudflare, Supabase, Zeabur,
   Sentry). See §6 for the decision it needs.

---

## 2. Phase plan

### Phase A — TDLib authorization + single-account discovery (read-only)
- Replace the heartbeat-only worker loop with a real `tdjson` client loop
  (`td_json_client_send/receive/execute`).
- Implement the authorization state machine:
  `waitTdlibParameters → waitPhoneNumber → waitCode → (waitPassword) → ready`.
  Auth codes/passwords are supplied through an **operator-only** admin path,
  never stored.
- On `authorizationStateReady`, fetch `getMe`, `getChats`, and persist
  normalized `account` + `chat` rows to Supabase.
- Prove restart-resume: state on `/data/tdlib` means the second boot does
  **not** re-authorize or re-import.
- **Exit criteria (VERIFIED):** operator auth completes; account + chats in
  Supabase; worker restart resumes; heartbeat still green.
- **BLOCKED on:** `TELEGRAM_API_ID`, `TELEGRAM_API_HASH`, an authorized
  phone number, and a TDLib runtime with `libtdjson.so` (the image builds
  it; local verification needs the built `.so`).

### Phase B — Entity sync: contacts, groups, channels, bots, files
- Extend the normalizer to cover the full "like official Telegram on login"
  set: contacts (`getContacts`/`importContacts` read side), basic groups,
  supergroups/channels, bots, and file metadata (`file` objects; download
  on demand to the volume, record metadata + checkpoint in Supabase).
- Idempotent UPSERTs keyed by Telegram IDs; per-chat backfill with a
  durable `last_synced_message_id` checkpoint.
- New migrations: `accounts`, `chats`, `chat_members`, `messages`,
  `files`, `sync_checkpoints`, `outbox_events` — all with RLS and a
  service-only write role.
- **Exit criteria (VERIFIED):** a full account's entities appear; a restart
  resumes from checkpoints; re-run produces zero duplicates.

### Phase C — Realtime ingestion + transactional outbox
- Handle live `updateNewMessage`, `updateChat*`, `updateFile` events;
  UPSERT then append an `outbox_events` row in the same transaction.
- Emit outbox only after commit; downstream consumers are at-least-once.
- **Exit criteria (VERIFIED):** live message ingested idempotently; outbox
  event observed after commit.

### Phase D — Multi-account
- One TDLib client per account, isolated session dir under
  `/data/tdlib/<account_id>/`; a supervisor manages lifecycles, per-account
  auth state, and health.
- Concurrency is bounded and staggered (see §3) to protect accounts.
- **Exit criteria (VERIFIED):** two accounts sync concurrently without
  cross-contamination; one account's re-auth does not disrupt the other.

### Phase E — Ban-avoidance / rate governance (cross-cutting, lands with A–D)
See §3. This is not a separate feature; it is the discipline every phase
follows.

### Phase F — AI knowledge base (separate downstream service)
- A distinct service (`kb/`) reads committed Supabase records / the outbox,
  chunks + embeds message/entity content, and serves retrieval to AI agents
  with explicit authz, tool allowlists, rate limits, and audit logs.
- It never holds Telegram credentials and never calls TDLib.
- Embeddings/store: pgvector in the existing Supabase Postgres (no new
  platform). Agent access is least-privilege and human-approved for any
  write/destructive tool.
- **Exit criteria (VERIFIED):** a query returns grounded results from synced
  data with an audit trail; no coupling into the sync worker.

---

## 3. Ban-avoidance (how "avoiding ban" is actually done)

Telegram bans automation that behaves abusively. The safe posture:

- **Read-only first.** Keep `EXTERNAL_SEND_ENABLED=false`; sending is the
  main ban vector.
- **Respect FLOOD_WAIT.** Treat every `420/FLOOD_WAIT_x` and TDLib
  `Too Many Requests` as authoritative; back off for the returned seconds
  with jitter. Never tighten a retry loop around a flood wait.
- **Bounded concurrency + pacing.** One request in flight per account for
  sensitive calls; global token-bucket rate limits; randomized delays
  between backfill batches instead of hammering `getChatHistory`.
- **Prefer updates over polling.** Rely on TDLib's update stream for
  realtime; poll only for reconciliation, infrequently.
- **Stable device/session.** Persist the session on the volume; do not
  re-login repeatedly (frequent new logins look like takeovers).
- **Per-account isolation.** No shared IP hammering patterns; stagger
  account startup.
- **No mass/abusive actions.** No bulk membership scraping beyond the
  account's own visible chats, no unsolicited messaging.

These are guardrails, not a guarantee — Telegram's rules still apply and the
operator must use authorized accounts.

---

## 4. Deploy-to-live checklist (what "ready to use on Zeabur" needs)

The code and templates are deploy-ready. Going live is **BLOCKED on
credentials/access** I do not have. Required, by platform:

| Platform | Needed to go live | Where it goes |
|---|---|---|
| Docker Hub | `DOCKERHUB_USERNAME`, `DOCKERHUB_TOKEN`; push a semver tag | GitHub Actions Secrets; images published by `release-dockerhub.yml` |
| Zeabur | project + api/worker services; pin immutable image tag/digest; attach `/data/tdlib` volume; set health path `/healthz` | Zeabur dashboard + service secret store |
| Supabase | project URL + service key; run migrations | Zeabur service secrets; migrations applied via CI/CLI |
| Cloudflare | zone/domain; proxy the Zeabur API domain; set dashboard `API_BASE_URL`; deploy Worker | Cloudflare account + `wrangler deploy` |
| Sentry | `SENTRY_DSN` (org `hillstreet`, project `open-tgate`) | Zeabur service secrets (optional but recommended) |
| Telegram | `TELEGRAM_API_ID`, `TELEGRAM_API_HASH`, authorized phone | Worker secret store; session on volume only |

For each missing credential, the exact variable name, platform, and reason
are listed above. None are fabricated; none should be committed.

---

## 5. What I deliberately did NOT do

- **Did not merge "all branches."** ~35 of the ~40 branches are empty
  duplicates pointing at identical SHAs; merging them is destructive and
  adds nothing. Recommendation: delete the stale `backup/*`,
  `ci/open-tgate-v1..v16`, `agent/*`, `api/initial`, `app/*`, `archive/*`,
  `auth/*`, `automation/*`, `bootstrap/*`, `build/*`, `candidate/*`,
  `canary`, `chore/*` placeholders after you confirm — a one-line audit can
  be produced before any deletion.
- **Did not merge PR #23** (docs-only, another author, protected `master`).
  It should merge on its own required-checks flow.
- **Did not add Databricks** (see §6).

## 6. Databricks decision (needs your approval)

Databricks is **not** in the stated canonical stack and the preferences say
no new production platform "unless there is a technical requirement and I
approve it." The AI knowledge base (§2 Phase F) is fully served by
Supabase + pgvector at this scale, so Databricks is not technically
required today. If you want lakehouse-scale analytics/ML on synced data
later, that is the technical trigger — say the word and I'll design the
Supabase → Databricks export path as an additive, downstream-only
integration (never in the sync loop, never holding Telegram credentials).
