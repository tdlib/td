"""Thin ctypes bridge to ``libtdjson``.

This is the only module that touches the native library. It is intentionally
minimal — create/send/receive/execute/destroy over the TDLib JSON interface —
so that the interesting logic (login handshake, entity sync) lives in the pure,
unit-tested modules and can run against a fake client in tests.

Importing this module never loads the native library; the library is loaded
lazily on first :func:`TdJsonClient.create`, so environments without
``libtdjson`` (CI unit tests, local dev) can import the package freely.
"""

from __future__ import annotations

import ctypes
import json
import logging
from pathlib import Path
from typing import Any

log = logging.getLogger("open-tgate.tdjson")

_lib: ctypes.CDLL | None = None


def _load(library_path: str) -> ctypes.CDLL:
    global _lib
    if _lib is not None:
        return _lib
    path = Path(library_path)
    if not path.is_file():
        raise RuntimeError(f"TDLib library not found: {path}")
    lib = ctypes.CDLL(str(path))

    lib.td_create_client_id.restype = ctypes.c_int
    lib.td_create_client_id.argtypes = []

    lib.td_send.restype = None
    lib.td_send.argtypes = [ctypes.c_int, ctypes.c_char_p]

    lib.td_receive.restype = ctypes.c_char_p
    lib.td_receive.argtypes = [ctypes.c_double]

    lib.td_execute.restype = ctypes.c_char_p
    lib.td_execute.argtypes = [ctypes.c_char_p]

    _lib = lib
    return lib


class TdJsonClient:
    """A single TDLib client (one Telegram account) over the JSON interface."""

    def __init__(self, library_path: str) -> None:
        self._lib = _load(library_path)
        self._client_id = self._lib.td_create_client_id()

    def send(self, request: dict[str, Any]) -> None:
        payload = json.dumps(request).encode("utf-8")
        self._lib.td_send(self._client_id, payload)

    def receive(self, timeout: float = 1.0) -> dict[str, Any] | None:
        raw = self._lib.td_receive(ctypes.c_double(timeout))
        if not raw:
            return None
        try:
            event = json.loads(raw.decode("utf-8"))
        except (ValueError, UnicodeDecodeError):
            log.warning("Dropping undecodable TDLib event")
            return None
        # Only deliver events addressed to this client id (shared receive queue).
        if event.get("@client_id") not in (None, self._client_id):
            return None
        return event

    def execute(self, request: dict[str, Any]) -> dict[str, Any] | None:
        """Synchronous, client-less TDLib call (e.g. ``setLogVerbosityLevel``)."""

        raw = self._lib.td_execute(json.dumps(request).encode("utf-8"))
        if not raw:
            return None
        return json.loads(raw.decode("utf-8"))

    @property
    def client_id(self) -> int:
        return self._client_id


def receive_any(library_path: str, timeout: float = 0.5) -> dict[str, Any] | None:
    """Read one event from TDLib's shared global receive queue (any client).

    TDLib multiplexes every client onto one queue, so a single pump must read
    it and route by ``@client_id``. Returns the decoded event or ``None`` on
    timeout / undecodable payload.
    """

    lib = _load(library_path)
    raw = lib.td_receive(ctypes.c_double(timeout))
    if not raw:
        return None
    try:
        return json.loads(raw.decode("utf-8"))
    except (ValueError, UnicodeDecodeError):
        log.warning("Dropping undecodable TDLib event")
        return None


def set_log_verbosity(library_path: str, level: int = 1) -> None:
    """Quiet TDLib's native logger (0=fatal … 1=errors). Best-effort."""

    lib = _load(library_path)
    request = json.dumps({"@type": "setLogVerbosityLevel", "new_verbosity_level": level})
    lib.td_execute(request.encode("utf-8"))
