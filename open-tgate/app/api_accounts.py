"""Admin API for Telegram account login (server-to-server automation).

The primary login UI in ``/app`` talks to Supabase directly under RLS (an
authenticated operator may create accounts and enqueue commands). These
endpoints provide the same actions for server-side automation, guarded by the
API admin token. Both paths write the same RLS-protected tables; TDLib session
material is never touched here.
"""

from __future__ import annotations

import json
import uuid

import httpx
from fastapi import APIRouter, Depends, HTTPException, status
from pydantic import BaseModel, Field

from .config import get_settings
from .security import require_admin
from .telegram.bus import build_command_row

router = APIRouter(prefix="/api/v1/telegram", dependencies=[Depends(require_admin)])


class CreateAccount(BaseModel):
    label: str = Field(min_length=1, max_length=80)


class Command(BaseModel):
    account_id: str = Field(min_length=1)
    action: str
    payload: dict | None = None


def _rest_headers() -> dict[str, str]:
    settings = get_settings()
    if not settings.supabase_url or not settings.supabase_secret_key:
        raise HTTPException(status_code=status.HTTP_503_SERVICE_UNAVAILABLE, detail="supabase_not_configured")
    return {
        "apikey": settings.supabase_secret_key,
        "authorization": f"Bearer {settings.supabase_secret_key}",
        "content-type": "application/json",
        "prefer": "return=representation",
    }


@router.post("/accounts", status_code=status.HTTP_201_CREATED)
def create_account(body: CreateAccount) -> dict[str, object]:
    settings = get_settings()
    headers = _rest_headers()
    account_id = str(uuid.uuid4())
    row = {"id": account_id, "label": body.label, "status": "pending", "created_via": "api"}
    url = f"{settings.supabase_url.rstrip('/')}/rest/v1/open_tgate_tg_accounts"
    try:
        resp = httpx.post(url, headers=headers, content=json.dumps(row), timeout=15)
        resp.raise_for_status()
    except httpx.HTTPError as exc:  # pragma: no cover - network error path
        raise HTTPException(status_code=status.HTTP_502_BAD_GATEWAY, detail=f"supabase_error: {exc}") from exc
    return {"account_id": account_id, "label": body.label}


@router.post("/commands", status_code=status.HTTP_202_ACCEPTED)
def enqueue_command(body: Command) -> dict[str, object]:
    try:
        row = build_command_row(body.account_id, body.action, body.payload)
    except ValueError as exc:
        raise HTTPException(status_code=status.HTTP_400_BAD_REQUEST, detail=str(exc)) from exc
    settings = get_settings()
    headers = _rest_headers()
    url = f"{settings.supabase_url.rstrip('/')}/rest/v1/open_tgate_login_commands"
    try:
        resp = httpx.post(url, headers=headers, content=json.dumps(row), timeout=15)
        resp.raise_for_status()
    except httpx.HTTPError as exc:  # pragma: no cover - network error path
        raise HTTPException(status_code=status.HTTP_502_BAD_GATEWAY, detail=f"supabase_error: {exc}") from exc
    return {"accepted": True, "action": body.action, "account_id": body.account_id}
