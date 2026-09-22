-- Open-TGate — Profile + sync-progress columns for comprehensive entity sync.
--
-- Extends the Telegram accounts table with:
--   * Profile fields populated by getMe after authorization (first_name, last_name, username)
--   * Sync progress tracking (sync_step, sync_started_at, sync_finished_at, entity_counts)
--
-- Extends the entities table with:
--   * last_message_date for chat ordering (mirrors TDLib's last_message.date)
--   * is_archived flag to distinguish archived chats
--
-- Idempotent: uses IF NOT EXISTS / ADD COLUMN IF NOT EXISTS.

-- ---------------------------------------------------------------------------
-- Accounts: profile + sync progress
-- ---------------------------------------------------------------------------
alter table public.open_tgate_tg_accounts
  add column if not exists tg_first_name text,
  add column if not exists tg_last_name text,
  add column if not exists tg_username text;

alter table public.open_tgate_tg_accounts
  add column if not exists sync_step text
    default null;

comment on column public.open_tgate_tg_accounts.sync_step is
  'Current sync phase: profile, chats, archived, contacts, complete, error. NULL when not syncing.';

alter table public.open_tgate_tg_accounts
  add column if not exists sync_started_at timestamptz,
  add column if not exists sync_finished_at timestamptz;

alter table public.open_tgate_tg_accounts
  add column if not exists entity_counts jsonb not null default '{}'::jsonb;

comment on column public.open_tgate_tg_accounts.entity_counts is
  'Per-kind entity counts after sync, e.g. {"contact":42,"group":5,"channel":12,"bot":3,"user":8}';

-- ---------------------------------------------------------------------------
-- Entities: last_message_date + archived flag
-- ---------------------------------------------------------------------------
alter table public.open_tgate_tg_entities
  add column if not exists last_message_date timestamptz;

alter table public.open_tgate_tg_entities
  add column if not exists is_archived boolean not null default false;

create index if not exists open_tgate_tg_entities_last_msg_idx
  on public.open_tgate_tg_entities (account_id, last_message_date desc nulls last)
  where kind in ('user', 'group', 'channel');

-- ---------------------------------------------------------------------------
-- Grant the new columns to authenticated (RLS still applies)
-- ---------------------------------------------------------------------------
-- The SELECT/INSERT/UPDATE grants from the base migration already cover new
-- columns on open_tgate_tg_accounts (column-level grants are not needed for
-- tables that have full-row grants). Same for entities SELECT.
