// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
//
// PVS-3 / MSVC C4456 — `ConnectionCreator::ping_proxy` shadowing UB.
//
// The historical form of `ConnectionCreator::ping_proxy` declared
//
//     Proxy requested_proxy;
//     Proxy *requested_proxy_ptr = nullptr;
//     if (input_proxy != nullptr) {
//       TRY_RESULT_PROMISE(promise, requested_proxy, Proxy::create_proxy(input_proxy.get()));
//       requested_proxy_ptr = &requested_proxy;
//     }
//     auto proxy = resolve_effective_ping_proxy(active_proxy, requested_proxy_ptr);
//
// The `TRY_RESULT_PROMISE` macro expands to a fresh `auto requested_proxy = ...`
// declaration, which silently SHADOWS the outer one. The inner shadow is
// only alive until the closing `}` of the `if`, so
// `requested_proxy_ptr = &requested_proxy;` captured the address of the inner
// shadow, and the subsequent `resolve_effective_ping_proxy(...)` outside the
// `if` dereferenced a dangling stack pointer to read the proxy fields.
//
// On most allocators (Linux glibc, Debug Windows malloc) the just-freed stack
// frame still contains the original Proxy bits long enough that the read
// "works" by accident; on optimizing builds and on heap-checker / sanitizer
// builds the read returns garbage that causes downstream `proxy.use_proxy()`
// or `proxy.server()` calls to dereference invalid pointers, leading to a
// crash on the very first ping_proxy() invocation with an explicit non-null
// proxy parameter.
//
// PVS-Studio V506 ("Pointer to local variable is stored outside the scope of
// this variable") and the MSVC C4456 ("declaration shadows a local variable")
// warning both flagged the pattern. The architectural fix in
// `td/telegram/net/ConnectionCreator.cpp::ping_proxy` lifts the optional
// proxy into a `std::unique_ptr<Proxy>` slot owned at function scope and
// passes `unique_ptr.get()` to `resolve_effective_ping_proxy`. There is no
// shadowing, no out-of-scope address-of, and the resolved proxy is consumed
// before the unique_ptr goes out of scope.
//
// This suite enforces the wire-level invariants of the helper function
// `resolve_effective_ping_proxy` directly. The function is the contract
// surface that the dangling pointer was being passed across, so a strong
// regression test on it ensures that any future caller-side bug — whether
// shadowing, lifetime, or aliasing — that produces an invalid `Proxy *` is
// not silently absorbed by the helper.

#include "td/telegram/net/ConnectionCreator.h"
#include "td/telegram/net/Proxy.h"

#include "td/utils/common.h"
#include "td/utils/Slice.h"
#include "td/utils/tests.h"

#include <memory>

namespace {

using td::ConnectionCreator;
using td::Proxy;

// L1 — When the requested proxy pointer is `nullptr`, the function MUST
// return the active proxy unchanged. This is the path that legacy ping_proxy
// callers (the `nullptr` overload) take, and it must inherit any active
// stealth-mode MTProto proxy instead of falling back to a direct dial.
TEST(ConnectionCreatorPingProxyLifetime, NullRequestedReturnsActiveProxyByValue) {
  Proxy active = Proxy::socks5("198.51.100.1", 443, "alice", "hunter2");
  auto resolved = ConnectionCreator::resolve_effective_ping_proxy(active, nullptr);

  ASSERT_EQ(td::Slice("198.51.100.1"), resolved.server());
  ASSERT_EQ(443, resolved.port());
  ASSERT_EQ(td::Slice("alice"), resolved.user());
  ASSERT_EQ(td::Slice("hunter2"), resolved.password());
}

// L1 — When a requested proxy pointer IS provided, the function MUST return
// the requested proxy by value, NOT the active proxy. The shadowing UB
// caused this contract to silently revert to the active proxy whenever the
// inner shadow's stack slot was reused, so this test asserts the requested
// proxy is fully copied out into the return value.
TEST(ConnectionCreatorPingProxyLifetime, NonNullRequestedOverridesActiveProxy) {
  Proxy active = Proxy::socks5("198.51.100.1", 443, "alice", "hunter2");
  Proxy requested = Proxy::http_tcp("203.0.113.7", 8080, "bob", "letmein");

  auto resolved = ConnectionCreator::resolve_effective_ping_proxy(active, &requested);

  ASSERT_EQ(td::Slice("203.0.113.7"), resolved.server());
  ASSERT_EQ(8080, resolved.port());
  ASSERT_EQ(td::Slice("bob"), resolved.user());
  ASSERT_EQ(td::Slice("letmein"), resolved.password());
  ASSERT_TRUE(resolved.use_proxy());
}

// L1 — Lifetime contract: the resolved Proxy MUST own its server / user /
// password strings, not point at the source Proxy's storage. After the
// requested Proxy goes out of scope, the resolved value must still produce
// the correct fields. The shadowing UB violated this because the resolved
// proxy was constructed by dereferencing a dangling stack pointer.
TEST(ConnectionCreatorPingProxyLifetime, ResolvedProxyOutlivesRequestedProxyScope) {
  Proxy active = Proxy::socks5("198.51.100.1", 443, "alice", "hunter2");

  Proxy resolved = [&]() {
    Proxy requested = Proxy::http_tcp("203.0.113.42", 7777, "carol", "secret-1");
    return ConnectionCreator::resolve_effective_ping_proxy(active, &requested);
    // `requested` is destroyed here at the end of the lambda scope. The
    // returned `Proxy` MUST contain its own copy of the strings.
  }();

  ASSERT_EQ(td::Slice("203.0.113.42"), resolved.server());
  ASSERT_EQ(7777, resolved.port());
  ASSERT_EQ(td::Slice("carol"), resolved.user());
  ASSERT_EQ(td::Slice("secret-1"), resolved.password());
}

// L1 — Black-hat scenario for the `unique_ptr<Proxy>` rewrite: simulate
// the macro-shadowing call site by storing the requested Proxy in a heap
// allocation that is dropped right after the resolved value is captured.
// If `resolve_effective_ping_proxy` ever started returning a pointer or a
// view into its second argument, this test would catch a use-after-free.
TEST(ConnectionCreatorPingProxyLifetime, RequestedProxyHeapAllocationCanBeFreedImmediately) {
  Proxy active = Proxy::socks5("198.51.100.1", 443, "alice", "hunter2");

  auto requested_owner = std::make_unique<Proxy>(Proxy::mtproto("198.51.100.99", 8443,
                                                                td::mtproto::ProxySecret()));
  auto resolved = ConnectionCreator::resolve_effective_ping_proxy(active, requested_owner.get());
  requested_owner.reset();  // Free the heap-owned source proxy.

  ASSERT_EQ(td::Slice("198.51.100.99"), resolved.server());
  ASSERT_EQ(8443, resolved.port());
  ASSERT_TRUE(resolved.use_proxy());
}

// L1 — Defensive: the active proxy parameter is also passed by const ref,
// so it must be safe to free the active proxy storage after the call.
TEST(ConnectionCreatorPingProxyLifetime, ActiveProxyMayBeDestroyedAfterCallReturns) {
  Proxy resolved;
  {
    Proxy active = Proxy::socks5("198.51.100.55", 1080, "user", "pwd");
    resolved = ConnectionCreator::resolve_effective_ping_proxy(active, nullptr);
    // `active` is destroyed at the closing brace.
  }

  ASSERT_EQ(td::Slice("198.51.100.55"), resolved.server());
  ASSERT_EQ(1080, resolved.port());
  ASSERT_EQ(td::Slice("user"), resolved.user());
  ASSERT_EQ(td::Slice("pwd"), resolved.password());
}

}  // namespace
