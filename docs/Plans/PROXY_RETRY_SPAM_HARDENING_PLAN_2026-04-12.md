# Proxy Retry Spam Hardening Plan

## Problem

When a client is configured with the wrong MTProto proxy secret, the wrong proxy regime, or an incompatible stealth/TLS emulation mode, the server-side path usually rejects the connection quickly. The client then re-enters the connect path repeatedly while it is still considered online. That creates three risks:

1. repeated visible probes against the proxy endpoint;
2. unnecessary battery drain on mobile devices;
3. a recognizable failure pattern that can help DPI correlate client and proxy activity.

## Confirmed Control Path

The current retry behavior is the product of two layers:

1. `Session::on_session_failed` marks the session as failed and notifies the proxy wrapper.
2. `SessionProxy::on_failed` closes the current session and reopens it immediately.
3. `ConnectionCreator::client_loop` decides whether the new raw connection attempt is delayed.

Before this change, `ConnectionCreator::client_loop` applied the exponential `client.backoff` only when the runtime was offline. If the runtime was online, proxy-backed failures stayed on the fast retry path and were constrained only by the online flood controls. That is the retry seam that makes deterministic proxy rejection too chatty.

## Implemented Mitigation

This patch introduces `td/telegram/net/ConnectionRetryPolicy.{h,cpp}` and routes `ConnectionCreator::client_loop` through it.

Current policy:

1. direct connections keep the legacy fast online retry path;
2. offline connections keep the legacy exponential backoff path;
3. any proxy-backed connection attempt, including MTProto, MTProto-over-TLS emulation, SOCKS5, and HTTP TCP, now uses exponential backoff even while the client is online.

This is intentionally fail-closed. A misconfigured proxy is much safer when treated as a potentially hostile amplification surface than when treated as a transient online network wobble.

## Added Tests

`test/stealth/test_connection_retry_policy_security.cpp` now locks the retry policy for:

1. offline direct connections;
2. online direct connections;
3. online MTProto proxy connections;
4. online MTProto TLS-emulated proxy connections;
5. online SOCKS5 proxy connections;
6. online HTTP TCP proxy connections.

These tests make the retry contract explicit and keep future refactors from silently restoring the high-rate online retry behavior.

## Remaining Hardening Work

### 1. Classify deterministic proxy rejection reasons

Add a typed failure classification layer between `TransparentProxy` / `TlsInit` / handshake failures and `ConnectionCreator`.

Goal:

1. distinguish deterministic proxy misconfiguration (`wrong secret`, `wrong regime`, `ALPN mismatch`, `unexpected TLS response`, `EOF before handshake completion`) from transient transport failure;
2. allow stricter cooldowns for deterministic failures than for generic packet loss or route instability.

### 2. Introduce per-proxy cooldown state

Store a cooldown bucket keyed by active proxy identity and transport regime.

Requirements:

1. reset on proxy change;
2. reset on first successful `pong` or authenticated handshake;
3. keep bounded state only;
4. add jitter to avoid client herding.

ASVS L2 mapping:

1. rate limiting and abuse resistance;
2. fail-secure behavior under invalid inputs and protocol misuse.

### 3. Add a server-rejection soak harness

Build a dedicated integration harness that emulates a proxy endpoint which:

1. accepts TCP and closes immediately;
2. returns malformed TLS ServerHello fragments;
3. accepts SOCKS5 and stalls;
4. completes transport setup and then rejects MTProto packets deterministically.

Assertions:

1. retry intervals widen monotonically within configured jitter bounds;
2. no tight reconnect loop survives beyond the first few attempts;
3. query queues stay bounded;
4. connection counts do not exceed policy caps.

### 4. Add adversarial regression tests for policy bypasses

Add separate test files for:

1. rapid online/offline state flapping;
2. proxy changes during active backoff;
3. temporary success followed by immediate deterministic rejection;
4. multiple `SessionProxy` instances sharing one failing proxy;
5. stealth TLS route changes under RU egress restrictions.

### 5. Add observability for repeated proxy rejection

Emit structured counters for:

1. connection attempts per proxy transport;
2. handshake-stage failure bucket;
3. cooldown activations and suppressions;
4. time-to-first-success after proxy changes.

These counters should avoid secrets, raw domains, or user identifiers.

## Recommended Next Steps

1. implement failure classification first, because it is the missing seam needed for targeted cooldowns;
2. follow with per-proxy cooldown state and jitter;
3. then add the integration harness so the policy is stress-tested against realistic censor-side rejection patterns.