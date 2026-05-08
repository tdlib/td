---
description: "Activation reminder for logging work. Use this when adding or changing a LOG/VLOG point, tag, sink, callback, fatal logging path, or public logging API so the change is integrated into the existing subsystem instead of ad hoc output."
applyTo: "td/**/*.cpp, td/**/*.h, tdactor/**/*.cpp, tdactor/**/*.h, tdnet/**/*.cpp, tdnet/**/*.h, tddb/**/*.cpp, tddb/**/*.h, tde2e/**/*.cpp, tde2e/**/*.h, tdutils/**/*.cpp, tdutils/**/*.h"
---

# Logging Subsystem Reminder

Use this reminder when you touch a logging point or get blocked on how a new log-related change should fit into the subsystem.

## When this reminder matters

1. You are adding or changing `LOG`, `LOG_IF`, `VLOG`, `VLOG_IF`, `LOG_TAG`, `LOG_TAG2`, or `VERBOSITY_NAME(tag)` usage.
2. You are changing sink selection, callback wiring, fatal logging behavior, or public logging controls.
3. You are tempted to print directly instead of routing through the existing logging path.

## What to do immediately

1. Route the change through the existing logging subsystem, not ad hoc `stderr`, direct callbacks, or parallel logger state.
2. Start from the nearest owner: `td/telegram/Logging.cpp` for stream or tag policy, `tdutils/td/utils/logging.cpp` for emit or callback behavior, `TsLog` and `TsCerr` for serialization.
3. Keep logging hot-path safe, concurrency-safe, and secret-safe.

## Integration checklist

1. The low-level emit path and active sink live in `tdutils/td/utils/logging.h` and `tdutils/td/utils/logging.cpp`.
2. File logging is `FileLog` wrapped by `TsLog`; default console or stderr logging goes through `DefaultLog`; `NullLog` backs the empty stream.
3. The active sink is an atomic `LogInterface *` and must go through `load_active_log_interface()` and `store_active_log_interface()`.
4. Global verbosity lives in `td::log_options.level` and is accessed through `GET_VERBOSITY_LEVEL()` and `SET_VERBOSITY_LEVEL()`.
5. Public stream and tag controls live in `td/telegram/Logging.cpp`; legacy public C++ wrappers live in `td/telegram/Log.h` and `td/telegram/Log.cpp`.

## If you add a new log tag or logging point

1. Use the existing `LOG` or `VLOG` path instead of direct `TsCerr` or custom output.
2. Choose verbosity intentionally; do not default everything to a noisy tag or level.
3. If you need a new externally controllable tag, define `std::atomic<int> VERBOSITY_NAME(tag)` in the owning translation unit and register it in the `log_tags` map in `td/telegram/Logging.cpp`.
4. If the change affects public logging behavior or callback contracts, align `td/telegram/Log.h` or `td/telegram/td_json_client.h`.

## Contracts to preserve

1. `Logger::~Logger()` normalizes trailing newlines and marks overflow with `[truncated]`.
2. Fatal logs flow through `process_fatal_error()`, may invoke the callback, and then abort.
3. Callback dispatch is reentrancy-guarded by `LogMessageCallbackScope`.
4. `TsLog` and `TsCerr` use `atomic_flag` spin locking with periodic `std::this_thread::yield()`; do not accidentally worsen contention behavior.

## Security and review reminder

1. Never log proxy secrets, key material, auth tokens, or sensitive transport payloads.
2. Prefer stable, redacted, operator-readable messages.
3. Prefer focused tests around the touched logging slice before broad builds.