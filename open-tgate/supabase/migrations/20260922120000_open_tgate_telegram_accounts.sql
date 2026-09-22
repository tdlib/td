-- Open-TGate — Telegram account login + read-only sync schema.
--
-- Adds the command/event bus and entity store that connect the /app operator
-- console and the admin API to the TDLib worker:
--
--   * open_tgate_tg_accounts   — one row per connected account + live status
--   * open_tgate_login_commands — operator → worker instructions (transient
--                                 secrets: code/password, cleared on consume)
--   * open_tgate_tg_entities    — synced contacts/groups/channels/bots/files
--
-- Security model:
--   * TDLib session material (keys/database) NEVER lives here — it stays on the
--     worker's encrypted volume. These tables hold only coordination state and
--     public entity metadata.
--   * RLS is on. Only allowlisted, active operators (public.open_tgate_operators
--     via public.open_tgate_is_operator()) may read status/entities and drive
--     login. The Supabase secret (service_role) key — used only by the worker —
--     bypasses RLS to process commands and write results.
--   * Login secrets ride a command row only until the worker consumes it, then
--     payload is nulled. Operators are granted INSERT (not SELECT) on commands
--     so codes/passwords cannot be read back through the API.

-- ---------------------------------------------------------------------------
-- updated_at helper
-- ---------------------------------------------------------------------------
create or replace function public.open_tgate_touch_updated_at()
returns trigger
language plpgsql
set search_path to ''
as $$
begin
  new.updated_at = now();
  return new;
end;
$$;

-- ---------------------------------------------------------------------------
-- Accounts
-- ---------------------------------------------------------------------------
create table if not exists public.open_tgate_tg_accounts (
  id uuid primary key default gen_random_uuid(),
  label text not null,
  status text not null default 'pending'
    check (status in (
      'pending', 'initializing', 'awaiting_phone', 'awaiting_qr_scan',
      'awaiting_code', 'awaiting_password', 'authorized', 'logged_out', 'error'
    )),
  needs text,
  qr_link text,
  last_error text,
  tg_user_id text,
  phone_masked text,
  created_via text not null default 'app',
  created_at timestamptz not null default now(),
  updated_at timestamptz not null default now()
);

drop trigger if exists open_tgate_tg_accounts_touch on public.open_tgate_tg_accounts;
create trigger open_tgate_tg_accounts_touch
  before update on public.open_tgate_tg_accounts
  for each row execute function public.open_tgate_touch_updated_at();

alter table public.open_tgate_tg_accounts enable row level security;
revoke all on public.open_tgate_tg_accounts from anon, authenticated;
grant select, insert, update on public.open_tgate_tg_accounts to authenticated;

drop policy if exists open_tgate_accounts_operator_select on public.open_tgate_tg_accounts;
create policy open_tgate_accounts_operator_select on public.open_tgate_tg_accounts
  for select to authenticated using (public.open_tgate_is_operator());

drop policy if exists open_tgate_accounts_operator_insert on public.open_tgate_tg_accounts;
create policy open_tgate_accounts_operator_insert on public.open_tgate_tg_accounts
  for insert to authenticated with check (public.open_tgate_is_operator());

drop policy if exists open_tgate_accounts_operator_update on public.open_tgate_tg_accounts;
create policy open_tgate_accounts_operator_update on public.open_tgate_tg_accounts
  for update to authenticated
  using (public.open_tgate_is_operator())
  with check (public.open_tgate_is_operator());

-- ---------------------------------------------------------------------------
-- Login commands (operator -> worker)
-- ---------------------------------------------------------------------------
create table if not exists public.open_tgate_login_commands (
  id uuid primary key default gen_random_uuid(),
  account_id uuid not null references public.open_tgate_tg_accounts(id) on delete cascade,
  action text not null
    check (action in ('start_phone', 'start_qr', 'submit_code', 'submit_password', 'logout')),
  payload jsonb,
  status text not null default 'pending'
    check (status in ('pending', 'processing', 'done', 'error')),
  error text,
  created_at timestamptz not null default now(),
  consumed_at timestamptz
);

create index if not exists open_tgate_login_commands_pending_idx
  on public.open_tgate_login_commands (created_at)
  where status = 'pending';

alter table public.open_tgate_login_commands enable row level security;
revoke all on public.open_tgate_login_commands from anon, authenticated;
-- Operators may enqueue commands but NOT read them back (protects transit of
-- one-time codes / 2FA passwords). Status is observed via the account row.
grant insert on public.open_tgate_login_commands to authenticated;

drop policy if exists open_tgate_commands_operator_insert on public.open_tgate_login_commands;
create policy open_tgate_commands_operator_insert on public.open_tgate_login_commands
  for insert to authenticated
  with check (
    public.open_tgate_is_operator()
    and status = 'pending'
    and exists (select 1 from public.open_tgate_tg_accounts a where a.id = account_id)
  );

-- ---------------------------------------------------------------------------
-- Synced entities (read-only mirror of the account's Telegram objects)
-- ---------------------------------------------------------------------------
create table if not exists public.open_tgate_tg_entities (
  account_id uuid not null references public.open_tgate_tg_accounts(id) on delete cascade,
  kind text not null check (kind in ('user', 'contact', 'bot', 'group', 'channel', 'file')),
  tg_id text not null,
  title text,
  username text,
  meta jsonb not null default '{}'::jsonb,
  synced_at timestamptz not null default now(),
  primary key (account_id, kind, tg_id)
);

create index if not exists open_tgate_tg_entities_account_kind_idx
  on public.open_tgate_tg_entities (account_id, kind);

alter table public.open_tgate_tg_entities enable row level security;
revoke all on public.open_tgate_tg_entities from anon, authenticated;
grant select on public.open_tgate_tg_entities to authenticated;

drop policy if exists open_tgate_entities_operator_select on public.open_tgate_tg_entities;
create policy open_tgate_entities_operator_select on public.open_tgate_tg_entities
  for select to authenticated using (public.open_tgate_is_operator());

comment on table public.open_tgate_tg_accounts is
  'Open-TGate connected Telegram accounts + live login/sync status. RLS: operators read/manage; service_role (worker) bypasses. No TDLib session material stored.';
comment on table public.open_tgate_login_commands is
  'Operator -> worker login instructions. Secrets ride payload transiently and are nulled on consume. Operators may INSERT only.';
comment on table public.open_tgate_tg_entities is
  'Read-only mirror of synced contacts/groups/channels/bots/files. RLS: operators read; service_role writes.';
