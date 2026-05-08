<!--
SPDX-FileCopyrightText: Copyright 2026 telemt community
SPDX-License-Identifier: MIT
telemt: https://github.com/telemt
telemt: https://t.me/telemtrs
-->

# Logging Subsystem Guide

**Document Version:** 1.0  
**Date:** 2026-05-09  
**Scope:** Internal logging architecture, runtime controls, callback behavior, sink selection, concurrency guarantees, and maintenance rules for `tdlib-obf`

---

## Purpose

This document describes the current logging subsystem as implemented in the repository after the hardening work tracked in `docs/Plans/LOGGING_HARDENING_2026-05-07.md`.

It is intended to be the practical owner guide for maintainers and integrators who need to answer questions such as:

1. how `LOG(...)` and `VLOG(...)` actually reach stderr, file logs, or null logs;
2. where runtime verbosity and tag controls are stored;
3. how callbacks and fatal logging are delivered;
4. which public TDLib requests configure logging;
5. which contracts are enforced by the current contract, adversarial, fuzz, stress, and source-hygiene tests.

This is not a generic C++ logging tutorial. It is a repository-specific description of the real control path and the rules that future changes must preserve.

---

## Source Of Truth

These files are the primary owners of the subsystem:

1. `tdutils/td/utils/logging.h` and `tdutils/td/utils/logging.cpp` for macros, `Logger`, active sink state, truncation, callback state, and fatal handling.
2. `tdutils/td/utils/TsLog.h`, `tdutils/td/utils/TsLog.cpp`, `tdutils/td/utils/TsCerr.h`, and `tdutils/td/utils/TsCerr.cpp` for serialized emission on contended paths.
3. `tdutils/td/utils/FileLog.h`, `tdutils/td/utils/FileLog.cpp`, and `tdutils/td/utils/NullLog.h` for concrete sink implementations.
4. `td/telegram/Logging.h` and `td/telegram/Logging.cpp` for TDLib-facing runtime stream and tag controls.
5. `td/telegram/SynchronousRequests.cpp` for the synchronous TDLib request plumbing.
6. `td/telegram/Client.h`, `td/telegram/Client.cpp`, `td/telegram/td_json_client.h`, and `td/telegram/td_json_client.cpp` for the client and `tdjson` callback bridge.
7. `td/telegram/Log.h`, `td/telegram/Log.cpp`, and `td/telegram/td_log.h` for deprecated legacy wrappers that still exist.
8. `test/logging_*.cpp` and `test/stealth/test_*logging*.cpp` for the pinned behavior contracts.

If this document and the source disagree, the source and tests win.

---

## System Topology

At a high level, logging is split into five layers:

| Layer | Primary owners | Responsibility |
|---|---|---|
| Macro surface | `tdutils/td/utils/logging.h` | `LOG`, `LOG_IF`, `VLOG`, `VLOG_IF`, `LOG_CHECK`, `LOG_DCHECK`, compile-time stripping, runtime gating |
| Message construction | `tdutils/td/utils/logging.cpp` | prefix formatting, thread-local tag context, newline normalization, truncation marker insertion |
| Sink dispatch | `tdutils/td/utils/logging.cpp` | atomic active sink pointer, `LogInterface::append`, fatal processing, low-level callback dispatch |
| Concrete sinks | `DefaultLog`, `TsLog`, `TsCerr`, `FileLog`, `NullLog` | platform stderr, file output, serialization, rotation, empty sink |
| Public control plane | `td/telegram/Logging.cpp`, `td/telegram/SynchronousRequests.cpp`, legacy wrappers | `setLogStream`, `getLogStream`, global verbosity, tag verbosity, manual log injection, callback registration |

End-to-end flow for a normal log message:

```text
LOG(...) / VLOG(...)
  -> compile-time strip check
  -> runtime verbosity check
  -> temporary td::Logger
  -> Logger destructor flush
  -> load_active_log_interface()
  -> LogInterface::append()
  -> sink do_append()
  -> optional message callback delivery
```

For fatal logs the path is the same up to `LogInterface::append()`, but after the sink write the message goes through `process_fatal_error()` and the process aborts.

---

## Hardening Summary

The current codebase already incorporates the major hardening goals described in the May 2026 logging plan.

The notable implementation changes now reflected in source and tests are:

1. the active sink is no longer a raw mutable global pointer; it is an atomic `LogInterface *` accessed through `load_active_log_interface()` and `store_active_log_interface()`;
2. runtime tag verbosity is no longer modeled as plain mutable integers for registered tags; the registry now stores `std::atomic<int> *` entries;
3. low-level log callback configuration is stored as a single atomic snapshot object, so the callback pointer and its max verbosity travel together;
4. overflow is not silent anymore; `Logger::~Logger()` explicitly inserts `[truncated]` into the emitted slice when `StringBuilder` overflowed;
5. `TsLog` and `TsCerr` no longer busy-spin without relief; both use `atomic_flag` with periodic `std::this_thread::yield()` under contention;
6. the legacy fatal callback bridge in `td/telegram/Log.cpp` uses atomic load/store semantics instead of a plain shared function pointer.

Those are not design aspirations anymore. They are current repository behavior and are pinned by contract and adversarial tests.

---

## Verbosity Model

### Internal levels

The core levels in `tdutils/td/utils/logging.h` are:

| Constant | Value | Meaning |
|---|---:|---|
| `VERBOSITY_NAME(PLAIN)` | `-1` | internal plain output mode without the usual prefix |
| `VERBOSITY_NAME(FATAL)` | `0` | fatal error |
| `VERBOSITY_NAME(ERROR)` | `1` | error |
| `VERBOSITY_NAME(WARNING)` | `2` | warning |
| `VERBOSITY_NAME(INFO)` | `3` | informational |
| `VERBOSITY_NAME(DEBUG)` | `4` | debug |
| `VERBOSITY_NAME(NEVER)` | `1024` | disabled / upper clamp bound |

`log_options.level` defaults to `VERBOSITY_NAME(DEBUG) + 1`, which is `5`. That matches the long-standing TDLib default of verbosity level 5.

### Compile-time stripping

The macro layer supports compile-time stripping through `STRIP_LOG` and `LOG_IS_STRIPPED(strip_level)`.

Important detail: standard `LOG(level)` uses the same level for compile-time stripping and runtime gating, while `VLOG(tag)` uses `DEBUG` as its strip level and a runtime atomic variable as its actual gate. That means custom tag verbosity remains a runtime concern unless the build strips away `DEBUG` itself.

### Runtime gating

At runtime, logging is allowed only when the message level is less than or equal to the active `LogOptions::level`.

The runtime gate is intentionally cheap:

1. `LogOptions::get_level()` is a relaxed atomic load;
2. custom tag verbosity reads use `load_verbosity_level(const std::atomic<int> &)`, also cheap;
3. no `Logger` object is constructed when the gate fails.

### Tag-specific verbosity

`VLOG(tag)` expects `VERBOSITY_NAME(tag)` to exist. For runtime-mutable tags, the subsystem now expects an atomic declaration pattern like this:

```cpp
// header
extern std::atomic<int> VERBOSITY_NAME(example_tag);

// source file
std::atomic<int> VERBOSITY_NAME(example_tag){VERBOSITY_NAME(WARNING)};
```

If the tag should be externally adjustable through TDLib requests, it must also be registered in the `log_tags` map in `td/telegram/Logging.cpp`.

---

## Macro Surface

The main entry points are:

1. `LOG(level)`
2. `LOG_IF(level, condition)`
3. `VLOG(level)`
4. `VLOG_IF(level, condition)`
5. `LOG_CHECK(condition)`
6. `LOG_DCHECK(condition)`

Two thread-local context channels are also exposed:

1. `LOG_TAG`
2. `LOG_TAG2`

Both are `TD_THREAD_LOCAL const char *` fields owned by `td::Logger`. They are read during message construction, so callers must only point them at stable string storage whose lifetime covers the log expression.

The macro layer routes through:

```cpp
LOG_IMPL_FULL(*::td::load_active_log_interface(), ::td::log_options, ...)
```

That detail matters for maintenance because raw `log_interface` mutation is no longer valid. All internal code, including tests and benchmarks, is expected to go through the accessor helpers.

---

## Message Construction And Formatting

### `td::Logger`

`Logger` is the object behind every emitted message. The macros construct a temporary `Logger`, stream the message body into it, and rely on the destructor to flush.

This design has two consequences:

1. formatting and delivery happen at end-of-expression, not at the first `<<`;
2. any change to `Logger::~Logger()` is behaviorally significant across the whole system.

### Buffering model

`Logger` uses a fixed-size stack-backed buffer of 128 KiB.

That is intentionally simple and allocation-free on the hot path. If the `StringBuilder` overflows, `Logger` does not try to grow the buffer. Instead it marks the emitted slice with `[truncated]`.

### Prefix layout

When `LogOptions::add_info` is enabled, the prefix is assembled in this order:

1. log level in brackets, for example `[ 2]`;
2. thread identifier, for example `[t 7]`;
3. wall-clock timestamp as `unix_seconds.nanoseconds`;
4. basename of the source file and line number;
5. `LOG_TAG` context as `[#tag]` when present;
6. `LOG_TAG2` context as `[!tag2]` when present;
7. `LOG_IF` or similar comment text as `[&condition]` when present;
8. a tab separator before the message body.

Representative shape:

```text
[ 2][t 7][1746783123.123456789][Logging.cpp:84][#proxy][!dc2][&condition]	message text
```

Only the basename of the file is printed. Full source paths are trimmed.

### Plain mode

`LogOptions::plain()` disables prefix decoration and newline fixing. It exists for plain output scenarios where the subsystem should act more like a pass-through sink writer than a structured logger.

### Newline normalization

When `LogOptions::fix_newlines` is enabled, `Logger::~Logger()` ensures the emitted message ends with exactly one newline.

Specifically:

1. it appends a newline if needed;
2. it collapses repeated trailing newlines to one;
3. it applies truncation marker insertion after normalization if overflow occurred.

### Truncation semantics

Overflow is explicit and deterministic:

1. `[truncated]` is written directly into the emitted mutable slice;
2. insertion is bounded to the actual emitted slice size, not the full backing buffer size;
3. contract tests pin that the marker appears on overflow and appears exactly once.

This matters operationally because the system is expected to fail visible, not fail silent, when a log record exceeds the fixed in-memory budget.

---

## Dispatch Core

### Active sink pointer

The currently selected sink lives in `tdutils/td/utils/logging.cpp` as:

```cpp
static std::atomic<LogInterface *> active_log_interface{default_log_interface};
```

The contract is:

1. readers use `load_active_log_interface()` with acquire ordering;
2. writers use `store_active_log_interface()` with release ordering;
3. `nullptr` is rejected;
4. production sink objects have static lifetime, so the atomic change is about data-race elimination, not ownership transfer.

Any future code that writes the sink pointer directly is a regression.

### `LogInterface::append()`

All sinks inherit from `LogInterface`. The common `append()` method centralizes two cross-cutting behaviors:

1. first call the sink-specific `do_append()`;
2. then either process fatal behavior or, for non-fatal messages, deliver eligible callbacks.

That ordering is important:

1. the sink sees the message before the callback sees it;
2. fatal messages are written before the fatal callback and abort path runs.

### Callback snapshot state

Low-level callbacks are configured through:

```cpp
using OnLogMessageCallback = void (*)(int verbosity_level, CSlice message);
void set_log_message_callback(int max_verbosity_level, OnLogMessageCallback callback);
```

The implementation stores callback configuration as a single atomic snapshot object:

1. `max_verbosity_level` and `callback` live together in `LogMessageCallbackState`;
2. the active state is stored as `std::atomic<std::shared_ptr<const LogMessageCallbackState>>`;
3. reads use acquire ordering and writes use release ordering.

This avoids split configuration races where the threshold and the callback pointer could come from different updates.

### Reentrancy suppression

Callback delivery is guarded by `LogMessageCallbackScope`, which uses a thread-local flag to suppress recursive reentry.

The effect is fail-closed behavior:

1. if a callback emits another log on the same thread, the nested callback is suppressed;
2. the original log still reaches the sink;
3. the outer callback still runs exactly once.

This is important because callback recursion would otherwise create unbounded reentry or duplicate log delivery.

### Fatal processing

Fatal messages go through `process_fatal_error(CSlice message)`.

Behavioral contract:

1. the current callback snapshot is loaded once;
2. if a callback exists and its configured max verbosity is at least `0`, the fatal message is delivered to the callback;
3. after callback return, the process aborts with `std::abort()`;
4. calling back into TDLib from that fatal callback is forbidden.

---

## Concrete Sinks

### `DefaultLog`

`DefaultLog` is the built-in default sink.

Platform behavior:

1. Android uses `__android_log_write`;
2. Tizen uses `dlog_print`;
3. Emscripten uses `emscripten_log`, and fatal logging also throws into JS after writing;
4. non-Windows desktop builds write through `TsCerr` with ANSI colors for fatal, error, warning, and info;
5. Windows currently writes plain text through `TsCerr` without color.

`default_log_interface` points at this sink.

### `NullLog`

`NullLog` discards all output. It is the sink used by `logStreamEmpty`.

### `FileLog`

`FileLog` is the file-backed sink used behind `logStreamFile`.

Key behavior:

1. empty paths are rejected;
2. the default rotate threshold is 10 MiB;
3. the file is opened with create, write, and append semantics;
4. when possible, the configured path is canonicalized with `realpath`;
5. if `redirect_stderr` is enabled, the file descriptor is duplicated onto stderr as part of initialization and after rotation;
6. `get_file_paths()` returns the active log file path and the `.old` rotation path;
7. rotation occurs when the current size exceeds the threshold or `lazy_rotate()` has requested rotation;
8. rotation renames the current file to `<path>.old`, recreates the original path, and refreshes stderr redirection if configured;
9. if a single log write takes more than 0.1 seconds and the message level is at least error, an extra warning record is appended to the file.

Two important helper interactions exist here:

1. `ScopedDisableLog` is used during rotation to prevent the closed file from being used while the sink is reopening;
2. `has_log_guard()` is polled when stderr redirection is active so direct stderr critical sections can finish before file writes continue.

### `TsLog`

`TsLog` is a serializer wrapper around another `LogInterface`, most commonly `FileLog`.

It uses `std::atomic_flag` to protect:

1. `do_append()`;
2. `after_rotation()`;
3. `get_file_paths()`.

Its contention behavior is intentionally simple:

1. spin on `atomic_flag::test_and_set(memory_order_acquire)`;
2. bail out if `ExitGuard::is_exited()` becomes true;
3. call `std::this_thread::yield()` every 32 spins;
4. clear with `memory_order_release`.

### `TsCerr`

`TsCerr` is the serialized stderr writer used by `DefaultLog`.

Important details:

1. it uses a process-wide static `std::atomic_flag`;
2. it mirrors the `TsLog` contention model with exit-guard checks and periodic yields;
3. writes retry briefly on temporary failures;
4. writes stop early on `EPIPE` or when the short retry window expires;
5. lock release uses `memory_order_release`.

This means the default stderr path is serialized and contention-aware, but not a queueing logger. It is still synchronous output.

---

## Ancillary Guards And Control Helpers

### `LogGuard`

`LogGuard` is a small process-local guard implemented in `tdutils/td/utils/logging.cpp`.

It provides:

1. exclusive ownership of a binary guard flag;
2. `has_log_guard()` for code that needs to wait until the guarded stderr-critical region is over.

The file-backed sinks use this to avoid fighting direct stderr activity when stderr is redirected into the log file.

### `ScopedDisableLog`

`ScopedDisableLog` temporarily sets the global verbosity to the minimum integer value under a mutex-protected nesting counter.

This helper exists so maintenance operations such as file rotation can suppress incidental log traffic without permanently altering the configured verbosity.

The nesting behavior matters: nested disable scopes restore the original verbosity only when the outermost scope exits.

---

## Telegram Control Plane

The internal logging core is not configured directly by users. The supported runtime control path goes through `td/telegram/Logging.cpp` and synchronous TDLib requests.

### Supported synchronous requests

These TDLib functions are handled synchronously in `td/telegram/SynchronousRequests.cpp`:

| Request | Implementation target | Behavior |
|---|---|---|
| `setLogStream` | `Logging::set_current_stream` | switch to default, file, or empty sink |
| `getLogStream` | `Logging::get_current_stream` | return the current built-in stream description |
| `setLogVerbosityLevel` | `Logging::set_verbosity_level` | set global verbosity in the `0..1024` range |
| `getLogVerbosityLevel` | `Logging::get_verbosity_level` | return current global verbosity |
| `getLogTags` | `Logging::get_tags` | return registered externally controllable tags |
| `setLogTagVerbosityLevel` | `Logging::set_tag_verbosity_level` | set a tag gate after validation and clamping |
| `getLogTagVerbosityLevel` | `Logging::get_tag_verbosity_level` | read a single registered tag gate |
| `addLogMessage` | `Logging::add_message` | inject a manual message through the normal logging path |

These requests can be executed through `ClientManager::execute(...)` in the C++ API or `td_execute(...)` in the `tdjson` interface when the request is documented as synchronous.

### Stream types

`Logging::set_current_stream(...)` accepts exactly these stream families:

1. `logStreamDefault`
2. `logStreamFile`
3. `logStreamEmpty`

Validation rules:

1. `nullptr` streams are rejected;
2. file streams reject non-positive `max_file_size` values;
3. on success, the active sink pointer is updated through `store_active_log_interface(...)`.

`Logging::get_current_stream()` only knows how to serialize the built-in sink instances:

1. `default_log_interface`
2. `null_log`
3. `ts_log`

If a foreign sink is manually installed through the internal helper, `get_current_stream()` returns an error instead of inventing a public representation.

### Global verbosity

`Logging::set_verbosity_level(...)` accepts only `0 <= level <= 1024`.

Anything outside that range is rejected with an error instead of being silently clamped.

### Tag verbosity

`Logging::set_tag_verbosity_level(...)` is intentionally fail-closed:

1. empty tag names are rejected;
2. unknown tags are rejected;
3. accepted levels are clamped to the range `1..VERBOSITY_NAME(NEVER)`.

The lower clamp bound is intentionally `1`, not `0`, so per-tag controls are not used to represent fatal-only routing.

### Manual message injection

`Logging::add_message(...)` clamps the caller-supplied verbosity to `0..VERBOSITY_NAME(NEVER)`, creates a local `client` verbosity variable, and routes the message through `VLOG(client)`.

That means manually injected messages still obey the same logging path and sink selection as ordinary messages.

### Example `tdjson` requests

Switch to file logging:

```json
{
  "@type": "setLogStream",
  "log_stream": {
    "@type": "logStreamFile",
    "path": "tdlib.log",
    "max_file_size": 10485760,
    "redirect_stderr": true
  }
}
```

Set global verbosity to debug:

```json
{
  "@type": "setLogVerbosityLevel",
  "new_verbosity_level": 4
}
```

Raise the `proxy` tag:

```json
{
  "@type": "setLogTagVerbosityLevel",
  "tag": "proxy",
  "new_verbosity_level": 4
}
```

Inject a manual marker:

```json
{
  "@type": "addLogMessage",
  "verbosity_level": 2,
  "text": "manual operator marker"
}
```

---

## Registered Runtime Tags

`td/telegram/Logging.cpp` currently exposes the following externally controllable tag registry:

| Tag | Owning module |
|---|---|
| `td_init` | `td/telegram/Td.cpp` |
| `td_requests` | `td/telegram/Td.cpp` |
| `update_file` | `td/telegram/files/FileManager.cpp` |
| `connections` | `td/telegram/net/ConnectionCreator.cpp` |
| `binlog` | `tddb/td/db/binlog/Binlog.cpp` |
| `proxy` | `tdnet/td/net/TransparentProxy.cpp` |
| `net_query` | `td/telegram/net/NetQuery.cpp` |
| `dc` | `td/telegram/net/DcAuthManager.cpp` |
| `file_loader` | `td/telegram/files/FileLoaderUtils.cpp` |
| `mtproto` | `td/mtproto/SessionConnection.cpp` |
| `raw_mtproto` | `td/mtproto/Transport.cpp` |
| `fd` | `tdutils/td/utils/port/detail/NativeFd.cpp` |
| `actor` | `tdactor/td/actor/impl/Scheduler.cpp` |
| `sqlite` | `tddb/td/db/SqliteStatement.cpp` |
| `notifications` | `td/telegram/NotificationManager.cpp` |
| `get_difference` | `td/telegram/UpdatesManager.cpp` |
| `file_gc` | `td/telegram/files/FileGcWorker.cpp` |
| `config_recoverer` | `td/telegram/ConfigManager.cpp` |
| `dns_resolver` | `tdnet/td/net/GetHostByNameActor.cpp` |
| `file_references` | `td/telegram/FileReferenceManager.cpp` |

Maintenance rule: if you add a new externally controllable tag, update all three places together:

1. header declaration as `extern std::atomic<int> VERBOSITY_NAME(tag);`
2. source definition as `std::atomic<int> VERBOSITY_NAME(tag){...};`
3. `log_tags` registration in `td/telegram/Logging.cpp`

The contract tests deliberately pin the full registry so that partial migrations do not slip through.

---

## Callback And Client-Facing Behavior

### Low-level internal callback

`td::set_log_message_callback(...)` is the lowest-level callback registration entry point.

Signature:

```cpp
using OnLogMessageCallback = void (*)(int verbosity_level, CSlice message);
```

This is the callback used inside the logging core and by `ClientManager`.

### `ClientManager` callback bridge

The public C++ client callback surface is:

```cpp
using LogMessageCallbackPtr = void (*)(int verbosity_level, const char *message);
```

`ClientManager::set_log_message_callback(...)` stores the user callback in an atomic pointer and registers a wrapper with the low-level core.

The wrapper adds one important guarantee for user code: it always passes a null-terminated UTF-8 string.

If the underlying log message is not valid UTF-8:

1. the ASCII prefix is passed through unchanged up to the first invalid byte;
2. the remaining binary tail is percent-encoded with `url_encode(...)`;
3. a final newline is preserved when present.

This behavior is pinned by integration tests because public client callbacks must not receive raw invalid byte sequences.

### `tdjson` bridge

The C `tdjson` surface simply forwards:

```cpp
void td_set_log_message_callback(int max_verbosity_level, td_log_message_callback_ptr callback)
```

to `ClientManager::set_log_message_callback(...)`.

There is no separate logging core for `tdjson`. It is the same subsystem behind a thinner ABI bridge.

### Fatal callback compatibility wrapper

The deprecated `td::Log::set_fatal_error_callback(...)` path still exists for legacy users.

Behavior:

1. it stores the callback in an atomic function pointer;
2. it installs or removes a `ClientManager` log callback wrapper at verbosity level `0`;
3. on fatal messages, the wrapper invokes the callback if present;
4. once the callback returns, TDLib still aborts.

That means the fatal callback is a notification hook, not a recovery hook.

### Callback safety rules

The documented and tested rules remain strict:

1. do not call TDLib methods from a message callback;
2. do not assume recursion is allowed; nested callback delivery is suppressed on the same thread;
3. if the callback runs with verbosity level `0`, the process is going to abort after the callback returns;
4. removing the callback is fail-closed and stops delivery immediately.

---

## Legacy Wrapper Surfaces

Two legacy control surfaces remain for compatibility:

1. `td::Log` in `td/telegram/Log.h` and `td/telegram/Log.cpp`
2. the C API in `td/telegram/td_log.h`

They are deprecated in favor of synchronous TDLib requests such as `setLogStream` and `setLogVerbosityLevel`, but they still matter because downstream code may still call them.

Current behavior:

1. `Log::set_file_path(...)` routes to `Logging::set_current_stream(...)`;
2. `Log::set_max_file_size(...)` rebuilds the file stream configuration;
3. `Log::set_verbosity_level(...)` routes to `Logging::set_verbosity_level(...)`;
4. `Log::set_fatal_error_callback(...)` installs the legacy fatal callback bridge;
5. the C functions in `td_log.h` are the ABI-facing equivalents of those legacy controls.

When documenting or reviewing user-facing logging configuration, prefer the synchronous request interface, but do not forget these wrappers still exist and still affect runtime behavior.

---

## Concurrency Contracts

The logging subsystem is performance-sensitive but intentionally simple. The main contracts are:

1. active sink selection is data-race free through acquire/release atomic pointer access;
2. registered tag verbosity is data-race free through acquire/release atomic integer access;
3. low-level callback configuration is data-race free through a single atomic snapshot object;
4. the legacy fatal callback pointer is data-race free through atomic load/store;
5. `TsLog` and `TsCerr` serialize contended output with `atomic_flag` and release semantics;
6. both spin loops periodically yield, so they are not pure tight busy-waits anymore;
7. `ScopedDisableLog` uses a mutex and nesting count to preserve the original verbosity across nested scopes.

This subsystem is still mostly synchronous. It does not queue or batch log messages behind a background worker. Future changes must preserve the current lock ordering and callback ordering unless the whole contract is deliberately redesigned.

---

## Security And Stealth-Specific Rules

The repository has explicit source-contract coverage for logging hygiene in stealth and transport code.

The current rules are:

1. never log MTProto proxy secrets, raw secret bytes, keys, or similarly sensitive payloads;
2. keep transport and route logs structured enough to explain decisions without leaking secret material;
3. preserve the fixture-grounded traffic evidence pipeline under `test/analysis/fixtures/` and `docs/Samples/Traffic dumps/` when logging changes intersect DPI-evasion work;
4. prefer fail-closed validation for empty or invalid public logging inputs;
5. do not bypass the central logging path with ad hoc stderr printing except in carefully isolated low-level sink code.

The source-contract tests in `test/stealth/test_logging_secret_hygiene_source_contract.cpp` exist specifically to keep secret leakage and fixture drift from slipping in through logging changes.

---

## Test Matrix

The logging subsystem is backed by a dedicated multi-category test slice. Representative coverage includes:

| Category | Representative files | What is pinned |
|---|---|---|
| Contract | `test/logging_macro_contract.cpp`, `test/logging_stream_pointer_contract.cpp`, `test/logging_message_callback_contract.cpp`, `test/logging_truncation_contract.cpp`, `test/logging_tag_verbosity_contract.cpp`, `test/logging_fatal_callback_contract.cpp`, `test/logging_spin_contract.cpp`, `test/logging_tscerr_spin_contract.cpp` | API shape, atomic state, truncation policy, tag registry, spin-loop semantics, fatal callback ordering |
| Integration | `test/logging_stream_pointer_integration.cpp`, `test/logging_message_callback_integration.cpp`, `test/logging_tag_verbosity_integration.cpp` | runtime stream switching, `ClientManager` callback bridge, UTF-8 adaptation |
| Adversarial | `test/logging_message_callback_adversarial.cpp`, `test/logging_stream_pointer_adversarial.cpp`, `test/logging_tag_verbosity_adversarial.cpp`, `test/logging_fatal_callback_adversarial.cpp`, `test/logging_spin_adversarial.cpp`, `test/logging_tscerr_spin_adversarial.cpp` | split-state regressions, bad memory-order changes, recursion bugs, contention regressions |
| Light fuzz | `test/logging_macro_light_fuzz.cpp`, `test/logging_message_callback_light_fuzz.cpp`, `test/logging_stream_pointer_light_fuzz.cpp`, `test/logging_tag_verbosity_light_fuzz.cpp`, `test/logging_truncation_light_fuzz.cpp`, `test/logging_spin_light_fuzz.cpp` | malformed inputs and random operation sequences |
| Stress | `test/logging_macro_stress.cpp`, `test/logging_message_callback_stress.cpp`, `test/logging_stream_pointer_stress.cpp`, `test/logging_tag_verbosity_stress.cpp`, `test/logging_spin_stress.cpp`, `test/logging_tscerr_spin_stress.cpp` | sustained concurrency behavior |
| Source hygiene | `test/stealth/test_logging_secret_hygiene_source_contract.cpp` and related stealth source contracts | secret-safe logging and fixture-grounded observability |

If you change the subsystem and none of these tests need review, you probably have not found the real maintenance surface yet.

---

## Maintenance Playbook

### If you add a new log point

1. use `LOG`, `LOG_IF`, `VLOG`, or `VLOG_IF`, not ad hoc stderr printing;
2. choose the verbosity level deliberately;
3. do not include secrets or raw sensitive transport payloads;
4. if the message belongs to an externally controllable category, use an existing tag or add a new properly registered tag.

### If you add a new externally controllable tag

1. declare it as `extern std::atomic<int> VERBOSITY_NAME(tag);` in the owner header;
2. define it as `std::atomic<int> VERBOSITY_NAME(tag){...};` in the owner source file;
3. register it in `td/telegram/Logging.cpp`;
4. update the relevant contract test if the registry changed.

### If you change callback handling

1. preserve atomic snapshot semantics for the low-level callback state;
2. preserve the same-thread reentrancy suppression;
3. preserve UTF-8 guarantees on the `ClientManager` and `tdjson` surface;
4. preserve fatal ordering: sink write, optional callback, abort.

### If you change sink behavior

1. preserve the built-in stream mapping expected by `getLogStream()`;
2. preserve file-rotation correctness and stderr redirection semantics for file sinks;
3. preserve `TsLog` and `TsCerr` contention backoff behavior or replace it with a demonstrably better strategy and corresponding tests.

### If you need a focused validation set

The smallest high-signal checks are usually:

1. `LoggingMacroContract`
2. `LoggingStreamPointerContract`
3. `LoggingMessageCallbackContract`
4. `LoggingMessageCallbackIntegration`
5. `LoggingTagVerbosityContract`
6. `LoggingTruncationContract`
7. `LoggingSpinContract`
8. `LoggingTsCerrSpinContract`
9. `LoggingFatalCallbackContract`
10. `test/stealth/test_logging_secret_hygiene_source_contract.cpp`

---

## Practical Takeaways

For day-to-day maintenance, remember these rules first:

1. the logging core is centered on `tdutils/td/utils/logging.h` and `tdutils/td/utils/logging.cpp`;
2. runtime stream and tag policy is owned by `td/telegram/Logging.cpp`, not by individual callers;
3. all sink selection is atomic and must stay atomic;
4. all callback configuration is snapshot-based and must stay snapshot-based;
5. truncation must stay explicit;
6. fatal logging always aborts after notification;
7. secret hygiene is a hard requirement, not a style preference.

If a future patch breaks any of those points, it is changing the subsystem contract and must be treated as an architectural change, not as a minor cleanup.