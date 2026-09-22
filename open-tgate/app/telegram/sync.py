"""Pure normalisation of TDLib chat/user/file objects into entity rows.

The synchronisation path is deliberately dependency-free (no AI, no Notion, no
network in this module) and read-only: it turns the objects TDLib already holds
in its local database into flat rows that mirror what the official Telegram app
shows after login — contacts, groups, channels, bots and files.

Everything here is a pure function so the classification rules can be unit
tested against representative TDLib payloads.
"""

from __future__ import annotations

from typing import Any

EntityKind = str  # one of: user, contact, bot, group, channel, file


def mask_phone(phone: str | None) -> str | None:
    """Mask a phone number for storage/display, keeping only a country hint and
    the last two digits (e.g. ``+15551234567`` → ``+1•••••67``).

    Full phone numbers are personal data; we never persist them in the clear.
    """

    if not phone:
        return None
    digits = "".join(ch for ch in phone if ch.isdigit())
    if len(digits) < 4:
        return "•" * len(digits)
    lead = ("+" + digits[0]) if phone.strip().startswith("+") else digits[:1]
    return f"{lead}{'•' * max(3, len(digits) - 3)}{digits[-2:]}"


def classify_chat(chat: dict[str, Any]) -> EntityKind:
    """Classify a TDLib ``chat`` object into an Open-TGate entity kind."""

    chat_type = (chat.get("type") or {}).get("@type", "")
    if chat_type == "chatTypeBasicGroup":
        return "group"
    if chat_type == "chatTypeSupergroup":
        return "channel" if (chat.get("type") or {}).get("is_channel") else "group"
    if chat_type == "chatTypeSecret":
        return "user"
    # chatTypePrivate and anything unknown fall through to a 1:1 user chat.
    return "user"


def normalize_chat(chat: dict[str, Any]) -> dict[str, Any]:
    """Turn a TDLib ``chat`` object into a flat entity row."""

    return {
        "kind": classify_chat(chat),
        "tg_id": str(chat.get("id", "")),
        "title": chat.get("title") or "",
        "username": None,
        "meta": {
            "chat_type": (chat.get("type") or {}).get("@type", ""),
            "has_photo": bool(chat.get("photo")),
            "unread_count": chat.get("unread_count", 0),
        },
    }


def normalize_user(user: dict[str, Any]) -> dict[str, Any]:
    """Turn a TDLib ``user`` object into a flat entity row.

    A user is classified as ``bot`` when its type is ``userTypeBot``, otherwise
    ``contact`` when Telegram marks it a mutual/known contact, else ``user``.
    """

    user_type = (user.get("type") or {}).get("@type", "")
    if user_type == "userTypeBot":
        kind: EntityKind = "bot"
    elif user.get("is_contact") or user.get("is_mutual_contact"):
        kind = "contact"
    else:
        kind = "user"

    first = user.get("first_name") or ""
    last = user.get("last_name") or ""
    title = (first + " " + last).strip() or (user.get("username") or "")
    usernames = user.get("usernames") or {}
    active = usernames.get("active_usernames") if isinstance(usernames, dict) else None
    username = user.get("username") or (active[0] if active else None)

    return {
        "kind": kind,
        "tg_id": str(user.get("id", "")),
        "title": title,
        "username": username,
        "meta": {
            "is_bot": user_type == "userTypeBot",
            "is_contact": bool(user.get("is_contact")),
            "is_verified": bool(user.get("is_verified")),
            "phone_masked": mask_phone(user.get("phone_number")),
        },
    }


def normalize_file(document: dict[str, Any], *, chat_id: int | str | None = None) -> dict[str, Any]:
    """Turn a TDLib file-bearing object (``document``/``photo``/``audio`` …)
    into a flat ``file`` entity row. Only metadata is captured; file *bytes*
    are never uploaded off the worker by the sync path.
    """

    file = document.get("document") or document.get("file") or {}
    remote = (file.get("remote") or {}) if isinstance(file, dict) else {}
    return {
        "kind": "file",
        "tg_id": str(remote.get("unique_id") or file.get("id") or ""),
        "title": document.get("file_name") or document.get("caption", {}).get("text", "") or "",
        "username": None,
        "meta": {
            "mime_type": document.get("mime_type") or "",
            "size": (file.get("size") if isinstance(file, dict) else None) or 0,
            "chat_id": str(chat_id) if chat_id is not None else None,
        },
    }


def summarize_counts(entities: list[dict[str, Any]]) -> dict[str, int]:
    """Aggregate a list of entity rows into per-kind counts for the UI."""

    counts: dict[str, int] = {}
    for entity in entities:
        kind = entity.get("kind", "unknown")
        counts[kind] = counts.get(kind, 0) + 1
    return counts
