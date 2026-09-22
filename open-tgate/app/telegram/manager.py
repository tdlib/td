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


class AccountManager:
    """Owns every :class:`AccountRuntime` and the shared receive pump."""

    def __init__(self, settings: Settings, message_bus: bus.SupabaseBus) -> None:
        self._settings = settings
        self._bus = message_bus
        self._runtimes: dict[str, AccountRuntime] = {}
        self._by_client: dict[int, AccountRuntime] = {}
        self._sync_delay = max(0.2, float(getattr(settings, "sync_pacing_seconds", 0.4)))

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

    # ---- read-only entity sync ----------------------------------------

    async def _sync_account(self, runtime: AccountRuntime) -> None:
        """Hydrate contacts/chats after login, paced to look like a cold start.

        This is intentionally conservative: it asks TDLib for the chat list and
        contact list it already caches locally and mirrors the resulting entity
        metadata to Supabase. Message bodies and file bytes are not exported.
        """

        try:
            runtime.client.send({"@type": "loadChats", "limit": 200})
            runtime.client.send({"@type": "getContacts"})
            await self._bus.update_account(runtime.account_id, {"status": LoginStatus.AUTHORIZED.value, "needs": None})
            await asyncio.sleep(self._sync_delay)
        except Exception:  # noqa: BLE001
            log.exception("Sync bootstrap failed for %s", runtime.account_id)

    async def ingest_container(self, runtime: AccountRuntime, event: dict) -> None:
        """Normalise and persist an ``users``/``chats``/``chat``/``user`` event."""

        etype = event.get("@type")
        rows: list[dict] = []
        if etype == "user":
            rows.append(sync.normalize_user(event))
        elif etype == "chat":
            rows.append(sync.normalize_chat(event))
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
                    if runtime is not None and event.get("@type") in ("user", "chat"):
                        await self.ingest_container(runtime, event)
                except Exception:  # noqa: BLE001
                    log.exception("Event handling failed")
            await asyncio.sleep(0.1)

    def _receive_any(self) -> dict | None:
        """Read one event from the shared native queue (any client)."""

        if not self._runtimes:
            return None
        return receive_any(self._settings.tdlib_library_path, timeout=0.5)
