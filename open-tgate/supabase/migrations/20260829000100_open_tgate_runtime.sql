-- Open-TGate runtime baseline for the shared "open-operations" Supabase project.
--
-- This migration is additive, idempotent, and isolated from the other apps that
-- share this project:
--   * A dedicated "open_tgate" schema is reserved as the canonical home for the
--     Open-TGate domain model (accessed via direct SQL / server-side Postgres).
--   * The runtime liveness table is kept in "public" with an "open_tgate_" name
--     prefix so the sync worker can UPSERT it over PostgREST WITHOUT changing the
--     shared API gateway's exposed-schemas configuration.
--
-- Row Level Security is enabled with no anon/authenticated policies; only the
-- Supabase secret (service_role) key may read or write it.

-- Canonical, isolated application schema (reserved for the Open-TGate domain).
create schema if not exists open_tgate;
grant usage on schema open_tgate to service_role;
alter default privileges in schema open_tgate grant all on tables to service_role;
alter default privileges in schema open_tgate grant all on sequences to service_role;

-- Service-only runtime liveness. Written with the Supabase secret key.
create table if not exists public.open_tgate_worker_heartbeats (
  worker_id text primary key,
  service text not null,
  status text not null check (status in ('starting', 'running', 'degraded', 'stopped')),
  last_seen_at timestamptz not null default now(),
  metadata jsonb not null default '{}'::jsonb
);

alter table public.open_tgate_worker_heartbeats enable row level security;
revoke all on public.open_tgate_worker_heartbeats from anon, authenticated;

create index if not exists open_tgate_worker_heartbeats_last_seen_idx
  on public.open_tgate_worker_heartbeats (last_seen_at desc);

comment on table public.open_tgate_worker_heartbeats is
  'Open-TGate service-only runtime liveness. Written with the Supabase secret key; RLS on, no anon/authenticated access.';
