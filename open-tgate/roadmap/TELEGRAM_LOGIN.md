# Telegram account login & auto-sync

In-platform login for **multiple** Telegram accounts from the `/app` operator
console — by **phone number** or **QR code** — followed by a **read-only**
mirror of each account's contacts, groups, channels, bots and files, the way the
official app populates after you sign in.

## How the pieces connect

```
/app console (Supabase Auth operator, RLS)
   │  insert account + login command rows (INSERT-only on commands)
   ▼
Supabase  ── open_tgate_tg_accounts / open_tgate_login_commands / open_tgate_tg_entities
   ▲  status, qr_link, needs, synced entities (operator-readable via RLS)
   │  service-role (worker only) processes commands, writes results
TDLib worker (Zeabur, persistent volume)
   • authflow state machine drives phone/QR handshake
   • entity sync mirrors chats/contacts read-only
```

The console and worker never talk directly. They coordinate through three
RLS-protected Supabase tables (see
`supabase/migrations/20260922120000_open_tgate_telegram_accounts.sql`). The
worker uses the Supabase **secret** key (bypasses RLS); operators use the
**publishable** key and are constrained by policies keyed on
`public.open_tgate_is_operator()`.

## Login flows

* **Phone:** operator enters an E.164 number → worker sends
  `setAuthenticationPhoneNumber` → operator enters the code Telegram delivers →
  (optional) two-step password → `authorized`.
* **QR:** worker calls `requestQrCodeAuthentication` → the emitted
  `tg://login?token=…` link is rendered as a QR code in the browser → operator
  scans it from an already-signed-in device → (optional) password → `authorized`.

## Ban-safety

* Update-driven, not polling; bounded command batches.
* `FLOOD_WAIT`/`retry after N` parked for exactly N seconds per account.
* Cold-start sync is paced (`SYNC_PACING_SECONDS`) to resemble an official
  client; only metadata TDLib already caches locally is mirrored.
* **Read-only** — no send path exists in sync; outbound stays gated by
  `EXTERNAL_SEND_ENABLED` (default `false`).

## Secrets & persistence

* TDLib session material (keys + database) lives **only** on the worker's
  `/data/tdlib/<account_id>` volume, never in Supabase or the repo.
* One-time login codes / 2-step passwords ride a command row **transiently** and
  are nulled the instant the worker consumes them; operators have INSERT (not
  SELECT) on the command table, so they cannot be read back.
* Phone numbers are stored masked (`+1•••••67`).

## Status

* **IMPLEMENTED / CONFIGURED:** state machine, entity normaliser, command bus,
  admin API (`/api/v1/telegram/*`), operator UI, Supabase schema (applied).
* **Verified here:** 32 unit tests (login handshake, QR, sync, RLS command
  contract, API gate) green; migration applied and security-advisor clean;
  dashboard Worker bundles.
* **BLOCKED on runtime (user-only):** end-to-end VERIFY needs `TELEGRAM_API_ID`
  / `TELEGRAM_API_HASH` set on the Zeabur **worker** service and the worker
  deployed; then connect a real account and confirm `authorized` + synced
  counts. See `roadmap/DEPLOY_STATUS.md` for the deploy gates.
