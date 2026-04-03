---
description: Companion to `Agents.md`. These are **activation directives**, not tutorials. You already know these patterns — apply them. When making any structural or design decision, run the relevant section below as a checklist.

---

## 1. Active Principles (always on)

Apply these on every non-trivial change. No exceptions.

- **SRP** — one reason to change per component. If you can't name the responsibility in one noun phrase, split it.
- **OCP** — extend by adding, not by modifying. New variants/impls over patching existing logic.
- **ISP** — traits stay minimal. More than ~5 methods is a split signal.
- **DIP** — high-level modules depend on traits, not concrete types. Infrastructure implements domain traits; it does not own domain logic.
- **DRY** — one authoritative source per piece of knowledge. Copies are bugs that haven't diverged yet.
- **YAGNI** — generic parameters, extension hooks, and pluggable strategies require an *existing* concrete use case, not a hypothetical one.
- **KISS** — two equivalent designs: choose the one with fewer concepts. Justify complexity; never assume it.

---

## 2. Layered Architecture

Dependencies point **inward only**: `Presentation → Application → Domain ← Infrastructure`.

- Domain layer: zero I/O. No network, no filesystem, no async runtime imports.
- Infrastructure: implements domain traits at the boundary. Never leaks SDK/wire types inward.
- Anti-Corruption Layer (ACL): all third-party and external-protocol types are translated here. If the external format changes, only the ACL changes.
- Presentation: translates wire/HTTP representations to domain types and back. Nothing else.

---

## 3. Design Pattern Selection

Apply the right pattern. Do not invent a new abstraction when a named pattern fits.

| Situation | Pattern to apply |
|---|---|
| Struct with 3+ optional/dependent fields | **Builder** — `build()` returns `Result`, never panics |
| Cross-cutting behavior (logging, retry, metrics) on a trait impl | **Decorator** — implements same trait, delegates all calls |
| Subsystem with multiple internal components | **Façade** — single public entry point, internals are `pub(crate)` |
| Swappable algorithm or policy | **Strategy** — trait injection; generics for compile-time, `dyn` for runtime |
| Component notifying decoupled consumers | **Observer** — typed channels (`broadcast`, `watch`), not callback `Vec<Box<dyn Fn>>` |
| Exclusive mutable state serving concurrent callers | **Actor** — `mpsc` command channel + `oneshot` reply; no lock needed on state |
| Finite state with invalid transition prevention | **Typestate** — distinct types per state; invalid ops are compile errors |
| Fixed process skeleton with overridable steps | **Template Method** — defaulted trait method calls required hooks |
| Request pipeline with independent handlers | **Chain/Middleware** — generic compile-time chain for hot paths, `dyn` for runtime assembly |
| Hiding a concrete type behind a trait | **Factory Function** — returns `Box<dyn Trait>` or `impl Trait` |

---

## 4. Data Modeling Rules

- **Make illegal states unrepresentable.** Type system enforces invariants; runtime validation is a second line, not the first.
- **Newtype every primitive** that carries domain meaning. `SessionId(u64)` ≠ `UserId(u64)` — the compiler enforces it.
- **Enums over booleans** for any parameter or field with two or more named states.
- **Typed error enums** with named variants carrying full diagnostic context. `anyhow` is application-layer only; never in library code.
- **Domain types carry no I/O concerns.** No `serde`, no codec, no DB derives on domain structs. Conversions via `From`/`TryFrom` at layer boundaries.

---

## 5. Concurrency Rules

- Prefer message-passing over shared memory. Shared state is a fallback.
- All channels must be **bounded**. Document the bound's rationale inline.
- Never hold a lock across an `await` unless atomicity explicitly requires it — document why.
- Document lock acquisition order wherever two locks are taken together.
- Every `async fn` is cancellation-safe unless explicitly documented otherwise. Mutate shared state *after* the `await` that may be cancelled, not before.
- High-read/low-write state: use `arc-swap` or `watch` for lock-free reads.

---

## 6. Error Handling Rules

- Errors translated at every layer boundary — low-level errors never surface unmodified.
- Add context at the propagation site: what operation failed and where.
- No `unwrap()`/`expect()` in production paths without a comment proving `None`/`Err` is impossible.
- Panics are only permitted in: tests, startup/init unrecoverable failure, and `unreachable!()` with an invariant comment.

---

## 7. API Design Rules

- **CQS**: functions that return data must not mutate; functions that mutate return only `Result`.
- **Least surprise**: a function does exactly what its name implies. Side effects are documented.
- **Idempotency**: `close()`, `shutdown()`, `unregister()` called twice must not panic or error.
- **Fallibility at the type level**: failure → `Result<T, E>`. No sentinel values.
- **Minimal public surface**: default to `pub(crate)`. Mark `pub` only deliberate API. Re-export through a single surface in `mod.rs`.

---

## 8. Performance Rules (hot paths)

- Annotate hot-path functions with `// HOT PATH: <throughput requirement>`.
- Zero allocations per operation in hot paths after initialization. Preallocate in constructors, reuse buffers.
- Pass `&[u8]` / `Bytes` slices — not `Vec<u8>`. Use `BytesMut` for reusable mutable buffers.
- No `String` formatting in hot paths. No logging without a rate-limit or sampling gate.
- Any allocation in a hot path gets a comment: `// ALLOC: <reason and size>`.

---

## 9. Testing Rules

- Bug fixes require a regression test that is **red before the fix, green after**. Name it after the bug.
- Property tests for: codec round-trips, state machine invariants, cryptographic protocol correctness.
- No shared mutable state between tests. Each test constructs its own environment.
- Test doubles hierarchy (simplest first): Fake → Stub → Spy → Mock. Mocks couple to implementation, not behavior — use sparingly.

---

## 10. Pre-Change Checklist

Run this before proposing or implementing any structural decision:

- [ ] Responsibility nameable in one noun phrase?
- [ ] Layer dependencies point inward only?
- [ ] Invalid states unrepresentable in the type system?
- [ ] State transitions gated through a single interface?
- [ ] All channels bounded?
- [ ] No locks held across `await` (or documented)?
- [ ] Errors typed and translated at layer boundaries?
- [ ] No panics in production paths without invariant proof?
- [ ] Hot paths annotated and allocation-free?
- [ ] Public surface minimal — only deliberate API marked `pub`?
- [ ] Correct pattern chosen from Section 3 table?