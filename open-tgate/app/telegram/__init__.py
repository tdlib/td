"""Open-TGate Telegram engine.

This package owns everything that talks to Telegram through TDLib:

* :mod:`app.telegram.authflow` — a pure, side-effect-free state machine that
  turns TDLib ``updateAuthorizationState`` transitions (phone-number login and
  QR-code login) into the next request to send and a public login status.
* :mod:`app.telegram.sync` — pure normalisation helpers that turn TDLib chat /
  user objects into flat entity rows (contacts, groups, channels, bots, files).
* :mod:`app.telegram.tdjson` — the thin ctypes bridge to ``libtdjson``.
* :mod:`app.telegram.manager` — the multi-account runtime that drives login and
  auto-sync while respecting Telegram rate limits (ban-safe pacing).

Design rules enforced here:

* Telegram *session material* (TDLib database + keys) lives ONLY on the worker
  volume, never in Supabase or in any repository artefact.
* Sync is strictly read-only. Outbound sending stays gated behind
  ``EXTERNAL_SEND_ENABLED`` (default false) and no send path exists in sync.
* No AI / Notion dependency is imported by the synchronisation path.
"""
