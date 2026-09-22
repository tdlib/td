"""Supabase-backed command/event bus between the API and the TDLib worker.

The public ``/app`` console and the admin API cannot talk to the worker
directly — the worker owns the TDLib session volume and has no public port. They
coordinate through a small set of RLS-protected tables in Supabase:

* ``public.open_tgate_tg_accounts`` — one row per connected account and its live
  login/sync status (operator-readable).
* ``public.open_tgate_login_commands`` — operator → worker instructions
  (start_phone / start_qr / submit_code / submit_password / logout). Secrets
  (code, password) ride here only transiently and are cleared by the worker the
  instant they are consumed.
* ``public.open_tgate_tg_entities`` — the synced contacts/groups/channels/bots/
  files (operator-readable).

This module maps :class:`~app.telegram.authflow.Decision` values to account-row
patches (a pure function, unit-tested) and provides a thin PostgREST client used
by the worker with the Supabase *secret* key. TDLib session material is NEVER
written here.
"""

from __future__ import annotations

import json
import logging
from typing import Any

import httpx

from .authflow import Decision, LoginStatus

log = logging.getLogger("open-tgate.bus")

# Login-command actions accepted from the API.
ACTION_START_PHONE = "start_phone"
ACTION_START_QR = "start_qr"
ACTION_SUBMIT_CODE = "submit_code"
ACTION_SUBMIT_PASSWORD = "submit_password"
ACTION_LOGOUT = "logout"

VALID_ACTIONS = frozenset(
    {
        ACTION_START_PHONE,
        ACTION_START_QR,
        ACTION_SUBMIT_CODE,
        ACTION_SUBMIT_PASSWORD,
        ACTION_LOGOUT,
    }
)


def account_patch_from_decision(decision: Decision) -> dict[str, Any]:
    """Map a login :class:`Decision` to the columns to patch on an account row.

    ``qr_link`` is only overwritten when the decision carries a fresh link, so a
    transient state that omits it does not wipe a link the operator is scanning.
    """

    patch: dict[str, Any] = {"status": decision.status.value}
    patch["needs"] = decision.needs
    patch["last_error"] = decision.error
    if decision.qr_link is not None:
        patch["qr_link"] = decision.qr_link
    if decision.status is LoginStatus.AUTHORIZED:
        # Login finished: clear the QR link and any pending prompt.
        patch["qr_link"] = None
        patch["needs"] = None
    return patch


def build_command_row(account_id: str, action: str, payload: dict[str, Any] | None) -> dict[str, Any]:
    """Validate and normalise an operator login command into a queue row.

    Raises :class:`ValueError` for an unknown action or a payload missing the
    field that action requires. The returned row is what gets inserted into
    ``open_tgate_login_commands`` with ``status='pending'``.
    """

    if action not in VALID_ACTIONS:
        raise ValueError(f"unknown action: {action!r}")
    payload = payload or {}
    normalized: dict[str, Any] = {}

    if action == ACTION_START_PHONE:
        phone = str(payload.get("phone_number", "")).strip()
        if not phone.startswith("+") or len(phone) < 8:
            raise ValueError("phone_number must be E.164, e.g. +15551234567")
        normalized["phone_number"] = phone
    elif action == ACTION_SUBMIT_CODE:
        code = str(payload.get("code", "")).strip()
        if not code.isdigit() or not (3 <= len(code) <= 8):
            raise ValueError("code must be the 3-8 digit login code")
        normalized["code"] = code
    elif action == ACTION_SUBMIT_PASSWORD:
        password = str(payload.get("password", ""))
        if not password:
            raise ValueError("password is required for two-step verification")
        normalized["password"] = password
    # start_qr and logout take no payload.

    return {
        "account_id": account_id,
        "action": action,
        "payload": normalized or None,
        "status": "pending",
    }


class SupabaseBus:
    """Minimal PostgREST client for the worker (uses the Supabase secret key)."""

    def __init__(self, base_url: str, secret_key: str, *, timeout: float = 15.0) -> None:
        self._rest = f"{base_url.rstrip('/')}/rest/v1"
        self._timeout = timeout
        self._headers = {
            "apikey": secret_key,
            "authorization": f"Bearer {secret_key}",
            "content-type": "application/json",
        }

    async def claim_pending_commands(self, limit: int = 20) -> list[dict[str, Any]]:
        """Fetch queued commands oldest-first for the worker to execute."""

        url = (
            f"{self._rest}/open_tgate_login_commands"
            f"?status=eq.pending&order=created_at.asc&limit={limit}"
        )
        async with httpx.AsyncClient(timeout=self._timeout) as client:
            resp = await client.get(url, headers=self._headers)
            resp.raise_for_status()
            return resp.json()

    async def mark_command(self, command_id: str, status: str, error: str | None = None) -> None:
        url = f"{self._rest}/open_tgate_login_commands?id=eq.{command_id}"
        body = {"status": status, "error": error, "payload": None, "consumed_at": "now()"}
        async with httpx.AsyncClient(timeout=self._timeout) as client:
            resp = await client.patch(url, headers=self._headers, content=json.dumps(body))
            resp.raise_for_status()

    async def update_account(self, account_id: str, patch: dict[str, Any]) -> None:
        url = f"{self._rest}/open_tgate_tg_accounts?id=eq.{account_id}"
        async with httpx.AsyncClient(timeout=self._timeout) as client:
            resp = await client.patch(url, headers=self._headers, content=json.dumps(patch))
            resp.raise_for_status()

    async def upsert_entities(self, rows: list[dict[str, Any]]) -> None:
        if not rows:
            return
        url = f"{self._rest}/open_tgate_tg_entities?on_conflict=account_id,kind,tg_id"
        headers = {**self._headers, "prefer": "resolution=merge-duplicates"}
        async with httpx.AsyncClient(timeout=self._timeout) as client:
            resp = await client.post(url, headers=headers, content=json.dumps(rows))
            resp.raise_for_status()
