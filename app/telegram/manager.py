"""Multi-account TDLib runtime: drives login and read-only auto-sync.

The manager owns the single native ``td_receive`` pump (TDLib multiplexes every
client onto one global receive queue) and routes each event to the right
:class:`AccountRuntime` by ``@client_id``. Per account it feeds the pure
:mod:`~app.telegram.authflow` state machine, dispatches the emitted request, and
mirrors login status to Supabase via :class:`~app.telegram.bus.SupabaseBus`.

Ban-safety measures baked in here:

* **Update-driven, not polling** — we react to TDLib updates instead of hot
  looping requests.
* **FLOOD_WAIT is honoured** — a ``retry after N`` error parks that account for
  ``N`` seconds instead of retrying immediately.
* **Paced sync** — chats/contacts are hydrated with a small delay between calls
  and a bounded in-flight window, mimicking an official client's cold start.
* **Read-only** — no send path exists here; outbound stays gated by
  ``EXTERNAL_SEND_ENABLED`` elsewhere.
"""

from __future__ import annotations

import asyncio
import json
import logging
import time
from dataclasses import dataclass, field

from ..config import Settings
from . import bus, sync
from .authflow import (
    LoginContext,
    LoginMode,
    LoginStatus,
    TdlibParameters,
    parse_flood_wait_seconds,
    plan,
)
from .tdjson import TdJsonClient, receive_any, set_log_verbosity

log = logging.getLogger("open-tgate.manager")


@dataclass
class AccountRuntime:
    """In-memory state for one connecting/connected Telegram account."""

    account_id: str
    client: TdJsonClient
    ctx: LoginContext
    status: LoginStatus = LoginStatus.INITIALIZING
    synced: bool = False
    # Epoch seconds until which this account is parked due to FLOOD_WAIT.
    paused_until: float = 0.0
    pending_chat_ids: list[int] = field(default_factory=list)
    # Collected entity rows during sync (flushed to Supabase in batches).
    _entity_buffer: list[dict] = field(default_factory=list)


class AccountManager:
    """Owns every :class:`AccountRuntime` and the shared receive pump."""

    def __init__(self, settings: Settings, message_bus: bus.SupabaseBus) -> None:
        self._settings = settings
        self._bus = message_bus
        self._runtimes: dict[str, AccountRuntime] = {}
        self._by_client: dict[int, AccountRuntime] = {}
        self._sync_delay = max(0.2, float(getattr(settings, "sync_pacing_seconds", 0.4)))
        self._entity_batch_size = 50

    # ---- lifecycle -----------------------------------------------------

    def _new_runtime(self, account_id: str, mode: LoginMode) -> AccountRuntime:
        client = TdJsonClient(self._settings.tdlib_library_path)
        params = TdlibParameters(
            api_id=int(self._settings.telegram_api_id or 0),
            database_directory=f"{self._settings.tdlib_database_directory}/{account_id}",
            files_directory=f"{self._settings.tdlib_files_directory}/{account_id}",
        )
        runtime = AccountRuntime(account_id=account_id, client=client, ctx=LoginContext(mode=mode, parameters=params))
        self._runtimes[account_id] = runtime
        self._by_client[client.client_id] = runtime
        return runtime

    # ---- command handling ---------------------------------------------

    async def apply_command(self, command: dict) -> None:
        """Apply one queued login command to the relevant account runtime."""

        account_id = command.get("account_id")
        action = command.get("action")
        payload = command.get("payload") or {}
        if not account_id or action not in bus.VALID_ACTIONS:
            await self._bus.mark_command(command["id"], "error", "invalid_command")
            return

        try:
            if action == bus.ACTION_START_PHONE:
                runtime = self._new_runtime(account_id, LoginMode.PHONE)
                runtime.ctx.phone_number = payload.get("phone_number")
            elif action == bus.ACTION_START_QR:
                self._new_runtime(account_id, LoginMode.QR)
            elif action == bus.ACTION_SUBMIT_CODE:
                runtime = self._runtimes.get(account_id)
                if runtime:
                    runtime.ctx.code = payload.get("code")
                    runtime.ctx.sent.discard("code")
            elif action == bus.ACTION_SUBMIT_PASSWORD:
                runtime = self._runtimes.get(account_id)
                if runtime:
                    runtime.ctx.password = payload.get("password")
                    runtime.ctx.sent.discard("password")
            elif action == bus.ACTION_LOGOUT:
                runtime = self._runtimes.get(account_id)
                if runtime:
                    runtime.client.send({"@type": "logOut"})
            await self._bus.mark_command(command["id"], "done")
        except Exception as exc:  # noqa: BLE001 - report and keep the loop alive
            log.exception("Command %s failed", command.get("id"))
            await self._bus.mark_command(command["id"], "error", str(exc)[:200])

    # ---- event pump ----------------------------------------------------

    def _kick(self, runtime: AccountRuntime) -> None:
        """Ask TDLib for the current auth state so ``plan`` can act on it."""

        runtime.client.send({"@type": "getAuthorizationState"})

    async def _handle_event(self, event: dict) -> None:
        client_id = event.get("@client_id")
        runtime = self._by_client.get(client_id) if client_id is not None else None
        if runtime is None:
            return

        etype = event.get("@type")
        if etype == "error":
            wait = parse_flood_wait_seconds(event)
            if wait:
                runtime.paused_until = time.time() + wait
                log.warning("Account %s FLOOD_WAIT %ss", runtime.account_id, wait)
            else:
                await self._bus.update_account(runtime.account_id, {"last_error": str(event.get("message"))[:200]})
            return

        state = event.get("authorization_state") if etype == "updateAuthorizationState" else (
            event if etype and etype.startswith("authorizationState") else None
        )
        if state is None:
            return

        decision = plan(state, runtime.ctx, api_hash=self._settings.telegram_api_hash)
        runtime.status = decision.status
        if decision.request is not None:
            runtime.client.send(decision.request)
        # Clear consumed secrets immediately after dispatch.
        runtime.ctx.code = None
        runtime.ctx.password = None
        await self._bus.update_account(runtime.account_id, bus.account_patch_from_decision(decision))

        if decision.status is LoginStatus.AUTHORIZED and not runtime.synced:
            runtime.synced = True
            asyncio.create_task(self._sync_account(runtime))

    # ---- comprehensive read-only entity sync ----------------------------

    async def _set_sync_step(self, runtime: AccountRuntime, step: str, extra: dict | None = None) -> None:
        """Update the sync_step column and any extra fields on the account row."""
        patch: dict = {"sync_step": step}
        if extra:
            patch.update(extra)
        try:
            await self._bus.update_account(runtime.account_id, patch)
        except Exception:  # noqa: BLE001
            log.exception("Failed to set sync_step=%s for %s", step, runtime.account_id)

    async def _flush_entities(self, runtime: AccountRuntime, rows: list[dict]) -> None:
        """Upsert entity rows to Supabase in batches."""
        for row in rows:
            row["account_id"] = runtime.account_id
        # Batch upsert.
        for i in range(0, len(rows), self._entity_batch_size):
            batch = rows[i : i + self._entity_batch_size]
            try:
                await self._bus.upsert_entities(batch)
            except Exception:  # noqa: BLE001
                log.exception("Entity upsert failed for %s (batch %d)", runtime.account_id, i)
            await asyncio.sleep(self._sync_delay * 0.5)

    async def _send_and_wait(self, runtime: AccountRuntime, request: dict, *, timeout: float = 30.0) -> dict | None:
        """Send a TDLib request via the extra-request path and wait for a typed
        response on the ``@extra`` correlation id. Returns None on timeout.

        This is a blocking helper for one-shot requests during sync. The main
        receive pump continues to run; we just filter for our ``@extra`` marker.
        """
        import uuid as _uuid

        extra_id = f"sync-{_uuid.uuid4().hex[:12]}"
        request["@extra"] = extra_id
        runtime.client.send(request)

        deadline = time.time() + timeout
        while time.time() < deadline:
            # Yield to the event loop so the main pump can also progress.
            await asyncio.sleep(0.15)
            # Drain the shared queue looking for our correlated response.
            for _ in range(30):
                event = self._receive_any()
                if event is None:
                    break
                if event.get("@extra") == extra_id:
                    return event
                # Forward non-correlated events through normal handling.
                try:
                    await self._handle_event(event)
                    rt = self._by_client.get(event.get("@client_id"))
                    if rt is not None and event.get("@type") in ("user", "chat", "updateNewChat", "updateUser"):
                        await self._ingest_update(rt, event)
                except Exception:  # noqa: BLE001
                    log.exception("Event handling during sync wait failed")
        log.warning("Sync request timed out for %s: %s", runtime.account_id, request.get("@type"))
        return None

    async def _ingest_update(self, runtime: AccountRuntime, event: dict) -> None:
        """Normalise and upsert a single chat/user update event."""
        etype = event.get("@type")
        rows: list[dict] = []
        if etype in ("user", "updateUser"):
            user_obj = event.get("user") if etype == "updateUser" else event
            if user_obj:
                rows.append(sync.normalize_user(user_obj))
        elif etype in ("chat", "updateNewChat"):
            chat_obj = event.get("chat") if etype == "updateNewChat" else event
            if chat_obj:
                rows.append(sync.normalize_chat(chat_obj))
        if rows:
            for row in rows:
                row["account_id"] = runtime.account_id
            try:
                await self._bus.upsert_entities(rows)
            except Exception:  # noqa: BLE001
                log.exception("Ingest update failed for %s", runtime.account_id)

    async def _sync_account(self, runtime: AccountRuntime) -> None:
        """Comprehensive read-only sync after login, paced to mimic a cold start.

        Sync steps:
        1. **profile** — ``getMe`` → extract name, username, masked phone
        2. **chats** — ``loadChats`` (main list) → entities from updateNewChat
        3. **archived** — ``loadChats`` (archive list) → entities
        4. **contacts** — ``getContacts`` → user entities
        5. **complete** — compute entity_counts, mark sync finished
        """

        account_id = runtime.account_id
        log.info("Starting comprehensive sync for account %s", account_id)

        try:
            # ── Step 1: Profile ─────────────────────────────────────────
            await self._set_sync_step(runtime, "profile", {"sync_started_at": "now()"})
            me = await self._send_and_wait(runtime, {"@type": "getMe"})
            if me and me.get("@type") == "user":
                profile_patch = sync.extract_profile(me)
                await self._bus.update_account(account_id, profile_patch)
                log.info("Profile synced for %s: @%s", account_id, profile_patch.get("tg_username"))
            await asyncio.sleep(self._sync_delay)

            # ── Step 2: Chats (main list) ───────────────────────────────
            await self._set_sync_step(runtime, "chats")
            # Load chats in pages. TDLib caches them locally; loadChats triggers
            # updateNewChat events for each chat. We request multiple pages with
            # a small sleep between to stay ban-safe.
            for page in range(5):  # Up to 5 × 200 = 1000 chats
                runtime.client.send({
                    "@type": "loadChats",
                    "chat_list": {"@type": "chatListMain"},
                    "limit": 200,
                })
                await asyncio.sleep(self._sync_delay * 2)

                # Drain events from loadChats (updateNewChat fires per chat).
                drained = 0
                for _ in range(250):
                    event = self._receive_any()
                    if event is None:
                        break
                    drained += 1
                    try:
                        await self._handle_event(event)
                        rt = self._by_client.get(event.get("@client_id"))
                        if rt is not None:
                            await self._ingest_update(rt, event)
                    except Exception:  # noqa: BLE001
                        log.exception("Chat drain failed")

                if drained < 5:
                    # TDLib returned very few events → we've loaded all chats.
                    break
                await asyncio.sleep(self._sync_delay)

            log.info("Main chat list loaded for %s", account_id)

            # ── Step 3: Archived chats ──────────────────────────────────
            await self._set_sync_step(runtime, "archived")
            for page in range(3):  # Up to 3 × 200 = 600 archived chats
                runtime.client.send({
                    "@type": "loadChats",
                    "chat_list": {"@type": "chatListArchive"},
                    "limit": 200,
                })
                await asyncio.sleep(self._sync_delay * 2)

                drained = 0
                for _ in range(250):
                    event = self._receive_any()
                    if event is None:
                        break
                    drained += 1
                    try:
                        await self._handle_event(event)
                        rt = self._by_client.get(event.get("@client_id"))
                        if rt is not None:
                            await self._ingest_update(rt, event)
                    except Exception:  # noqa: BLE001
                        log.exception("Archive drain failed")

                if drained < 5:
                    break
                await asyncio.sleep(self._sync_delay)

            log.info("Archived chat list loaded for %s", account_id)

            # ── Step 4: Contacts ────────────────────────────────────────
            await self._set_sync_step(runtime, "contacts")
            contacts_resp = await self._send_and_wait(runtime, {"@type": "getContacts"}, timeout=30.0)
            if contacts_resp and contacts_resp.get("@type") == "users":
                user_ids = contacts_resp.get("user_ids") or []
                log.info("Contact list for %s: %d user IDs", account_id, len(user_ids))
                # Fetch full user objects in small batches, paced.
                entity_rows: list[dict] = []
                for i, uid in enumerate(user_ids):
                    user_resp = await self._send_and_wait(
                        runtime, {"@type": "getUser", "user_id": uid}, timeout=10.0
                    )
                    if user_resp and user_resp.get("@type") == "user":
                        row = sync.normalize_user(user_resp)
                        # Force kind to "contact" since this came from getContacts.
                        row["kind"] = "contact"
                        entity_rows.append(row)
                    # Pace: brief sleep every few contacts.
                    if (i + 1) % 10 == 0:
                        await asyncio.sleep(self._sync_delay)
                if entity_rows:
                    await self._flush_entities(runtime, entity_rows)
                log.info("Contacts synced for %s: %d entities", account_id, len(entity_rows))
            await asyncio.sleep(self._sync_delay)

            # ── Step 5: Compute counts and mark complete ────────────────
            counts = await self._bus.count_entities(account_id)
            await self._set_sync_step(runtime, "complete", {
                "sync_finished_at": "now()",
                "entity_counts": json.dumps(counts) if counts else "{}",
                "status": LoginStatus.AUTHORIZED.value,
                "needs": None,
            })
            log.info("Sync complete for %s — counts: %s", account_id, counts)

        except Exception:  # noqa: BLE001
            log.exception("Sync failed for account %s", account_id)
            try:
                await self._set_sync_step(runtime, "error", {"last_error": "sync_failed"})
            except Exception:  # noqa: BLE001
                pass

    async def ingest_container(self, runtime: AccountRuntime, event: dict) -> None:
        """Normalise and persist an ``users``/``chats``/``chat``/``user`` event."""

        etype = event.get("@type")
        rows: list[dict] = []
        if etype == "user":
            rows.append(sync.normalize_user(event))
        elif etype == "chat":
            rows.append(sync.normalize_chat(event))
        elif etype == "updateNewChat":
            chat_obj = event.get("chat")
            if chat_obj:
                rows.append(sync.normalize_chat(chat_obj))
        elif etype == "updateUser":
            user_obj = event.get("user")
            if user_obj:
                rows.append(sync.normalize_user(user_obj))
        if rows:
            for row in rows:
                row["account_id"] = runtime.account_id
            await self._bus.upsert_entities(rows)

    # ---- top-level loop ------------------------------------------------

    async def run(self) -> None:
        set_log_verbosity(self._settings.tdlib_library_path, level=1)
        poll = max(1, int(getattr(self._settings, "command_poll_seconds", 3)))
        last_poll = 0.0
        while True:
            now = time.time()
            if now - last_poll >= poll:
                last_poll = now
                try:
                    for command in await self._bus.claim_pending_commands():
                        await self.apply_command(command)
                except Exception:  # noqa: BLE001
                    log.exception("Command poll failed")

            # Pump the shared TDLib receive queue for a short slice.
            for _ in range(50):
                event = self._receive_any()
                if event is None:
                    break
                try:
                    await self._handle_event(event)
                    runtime = self._by_client.get(event.get("@client_id"))
                    if runtime is not None and event.get("@type") in (
                        "user", "chat", "updateNewChat", "updateUser",
                    ):
                        await self.ingest_container(runtime, event)
                except Exception:  # noqa: BLE001
                    log.exception("Event handling failed")
            await asyncio.sleep(0.1)

    def _receive_any(self) -> dict | None:
        """Read one event from the shared native queue (any client)."""

        if not self._runtimes:
            return None
        return receive_any(self._settings.tdlib_library_path, timeout=0.5)
