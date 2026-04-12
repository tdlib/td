# AGENTS.md

This directory implements the MTProto-proxy stealth runtime: browser-like TLS `ClientHello` generation, route-aware `ECH` policy, runtime parameter loading, traffic classification, and transport shaping.

- Keep this subtree scoped to MTProto proxy stealth mode. It is only applicable when `ProxySecret::emulate_tls()` is active; do not extend it to direct Telegram transport paths or QUIC/HTTP3 behavior.
- Keep policy changes conservative and route-aware. In the current design, `RU` and `unknown` routes default to `ECH` off, and stealth-specific runtime validation is expected to reject unsafe states instead of guessing.
- Treat wire-format changes as capture-driven work. Do not introduce new browser-mimicry assumptions without matching checked-in fixtures, reviewed profiles, or verification evidence.
- Tests for this subtree live in `test/stealth`. Any change here should add or update focused regressions there, especially for TLS wire format, route policy, timing, record sizing, and fail-closed behavior.

## Progressive disclosure

- Threat model and implemented behavior: `docs/Plans/STEALTH_IMPLEMENTATION_RU.md`
- Capture-driven verification notes: `docs/Researches/STEALTH_VERIFICATION_REPORT_2026-04-10.md`
- Structural guidance: `.github/instructions/architecture.instructions.md`
- C++ conventions: `.github/instructions/c++_rules.instructions.md`
- Security requirements: `.github/instructions/Security_Requirements.instructions.md`
- TDD and adversarial test expectations: `.github/instructions/TDD_approach.instructions.md`