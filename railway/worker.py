#!/usr/bin/env python3
import ctypes
import json
import os
import signal
import sys
import time
from pathlib import Path

from supabase import create_client

API_ID = int(os.environ["TELEGRAM_API_ID"])
API_HASH = os.environ["TELEGRAM_API_HASH"]
SUPABASE_URL = os.environ.get("SUPABASE_URL")
SUPABASE_SECRET_KEY = os.environ.get("SUPABASE_SECRET_KEY")
DATA_DIR = Path(os.environ.get("TDLIB_DB_DIR", os.environ.get("TDLIB_DATA_DIR", "/data/telegram")))
FILES_DIR = Path(os.environ.get("TDLIB_FILES_DIR", str(DATA_DIR / "files")))
LOG_LEVEL = os.environ.get("LOG_LEVEL", "info").lower()

DATA_DIR.mkdir(parents=True, exist_ok=True)
FILES_DIR.mkdir(parents=True, exist_ok=True)

supabase = create_client(SUPABASE_URL, SUPABASE_SECRET_KEY) if SUPABASE_URL and SUPABASE_SECRET_KEY else None

lib = ctypes.CDLL("libtdjson.so")
lib.td_create_client_id.restype = ctypes.c_int
lib.td_send.argtypes = [ctypes.c_int, ctypes.c_char_p]
lib.td_receive.argtypes = [ctypes.c_double]
lib.td_receive.restype = ctypes.c_char_p
lib.td_execute.argtypes = [ctypes.c_char_p]
lib.td_execute.restype = ctypes.c_char_p

client_id = lib.td_create_client_id()
running = True

def log(msg, **fields):
    payload = {"msg": msg, **fields}
    print(json.dumps(payload, ensure_ascii=False), flush=True)

def send(query):
    lib.td_send(client_id, json.dumps(query).encode())

def receive(timeout=1.0):
    raw = lib.td_receive(timeout)
    return json.loads(raw.decode()) if raw else None

def stop(*_):
    global running
    running = False

signal.signal(signal.SIGTERM, stop)
signal.signal(signal.SIGINT, stop)

send({"@type": "setLogVerbosityLevel", "new_verbosity_level": 1 if LOG_LEVEL != "debug" else 3})
send({"@type": "getOption", "name": "version"})

def upsert_account(me):
    if not supabase:
        return
    row = {
        "telegram_user_id": str(me.get("id")),
        "username": (me.get("usernames") or {}).get("active_usernames", [None])[0],
        "display_name": " ".join(x for x in [me.get("first_name"), me.get("last_name")] if x).strip() or None,
        "authorization_status": "authorized",
        "sync_status": "live",
    }
    supabase.table("telegram_accounts").upsert(row, on_conflict="telegram_user_id").execute()

def handle_auth(state):
    t = state.get("@type")
    if t == "authorizationStateWaitTdlibParameters":
        send({
            "@type": "setTdlibParameters",
            "database_directory": str(DATA_DIR),
            "files_directory": str(FILES_DIR),
            "use_message_database": True,
            "use_secret_chats": True,
            "api_id": API_ID,
            "api_hash": API_HASH,
            "system_language_code": "en",
            "device_model": "Railway TDLib Worker",
            "application_version": "1.0.0",
        })
    elif t == "authorizationStateWaitPhoneNumber":
        phone = os.environ.get("TELEGRAM_PHONE_NUMBER")
        if phone:
            send({"@type": "setAuthenticationPhoneNumber", "phone_number": phone})
        else:
            log("authorization_required", state=t, hint="Set TELEGRAM_PHONE_NUMBER temporarily or use an interactive bootstrap session.")
    elif t == "authorizationStateWaitCode":
        code = os.environ.get("TELEGRAM_AUTH_CODE")
        if code:
            send({"@type": "checkAuthenticationCode", "code": code})
        else:
            log("authorization_required", state=t, hint="Set TELEGRAM_AUTH_CODE temporarily, redeploy, then remove it after login.")
    elif t == "authorizationStateWaitPassword":
        password = os.environ.get("TELEGRAM_2FA_PASSWORD")
        if password:
            send({"@type": "checkAuthenticationPassword", "password": password})
        else:
            log("authorization_required", state=t, hint="Set TELEGRAM_2FA_PASSWORD temporarily if 2FA is enabled.")
    elif t == "authorizationStateReady":
        log("telegram_authorized")
        send({"@type": "getMe", "@extra": "bootstrap:getMe"})
        send({"@type": "loadChats", "chat_list": {"@type": "chatListMain"}, "limit": 100})
        send({"@type": "loadChats", "chat_list": {"@type": "chatListArchive"}, "limit": 100})
    elif t in ("authorizationStateLoggingOut", "authorizationStateClosing", "authorizationStateClosed"):
        log("telegram_auth_state", state=t)

def persist_message(msg):
    if not supabase:
        return
    try:
        chat_id = str(msg.get("chat_id"))
        message_id = str(msg.get("id"))
        content = msg.get("content") or {}
        text = None
        if content.get("@type") == "messageText":
            text = (content.get("text") or {}).get("text")
        sender = msg.get("sender_id") or {}
        sender_id = sender.get("user_id") or sender.get("chat_id")
        row = {
            "telegram_chat_id": chat_id,
            "telegram_message_id": message_id,
            "sender_telegram_user_id": str(sender_id) if sender_id is not None else None,
            "direction": "outgoing" if msg.get("is_outgoing") else "incoming",
            "text": text,
            "telegram_timestamp": msg.get("date"),
            "has_media": content.get("@type") not in (None, "messageText"),
            "raw_event_type": content.get("@type"),
        }
        supabase.table("telegram_messages").upsert(row, on_conflict="telegram_chat_id,telegram_message_id").execute()
    except Exception as exc:
        log("supabase_message_upsert_failed", error=str(exc))

log("worker_started", data_dir=str(DATA_DIR), files_dir=str(FILES_DIR), supabase=bool(supabase))

while running:
    event = receive(1.0)
    if not event:
        continue
    et = event.get("@type")
    if et == "updateAuthorizationState":
        handle_auth(event.get("authorization_state") or {})
    elif et == "user" and event.get("@extra") == "bootstrap:getMe":
        upsert_account(event)
    elif et == "updateNewMessage":
        persist_message(event.get("message") or {})
    elif et == "error":
        log("tdlib_error", code=event.get("code"), message=event.get("message"))

log("worker_stopped")
