<!--
SPDX-FileCopyrightText: Copyright 2026 telemt community
SPDX-License-Identifier: MIT
telemt: https://github.com/telemt
telemt: https://t.me/telemtrs
-->

# SQLite Upgrade And Vendor Isolation Plan

**Date:** 2026-04-11
**Goal:** update the vendored SQLite stack to a current upstream release and stop hand-editing upstream amalgamation files.
**Primary constraint:** current runtime behavior must keep working, including the `tdsqlite3_*` API surface used by `tddb` and the SQLCipher-backed encryption flow.

---

## 0. Extracted Current Customizations From This Tree

This section is the concise inventory of what is actually custom in the repository today.

### 0.1 Telegram-owned customizations

1. **Global symbol namespace rewrite from `sqlite3*` to `tdsqlite3*`.**

   Proven by:
   - `sqlite/CMakeLists.txt` comment: `all SQLite functions are moved to namespace tdsqlite3 by sed -Ebi 's/sqlite3([^.]|$)/td&/g' *`
   - `sqlite/sqlite/sqlite3.h` exports `tdsqlite3_libversion`, `tdsqlite3_sourceid`, `tdsqlite3_open_v2`, and `typedef struct tdsqlite3 tdsqlite3;`
   - `sqlite/sqlite/sqlite3ext.h` exposes `struct tdsqlite3_api_routines`
   - `sqlite/sqlite/sqlite3session.h` exposes `tdsqlite3session_*`

   Consequence:
   - the vendored source is not pristine upstream source;
   - every future upgrade currently requires rewriting upstream files in place.

2. **A stable C++ wrapper layer depends on the renamed API, not on raw upstream names.**

   Proven by:
   - `tddb/td/db/detail/RawSqliteDb.h`
   - `tddb/td/db/SqliteDb.cpp`
   - `tddb/td/db/SqliteStatement.h`
   - `tddb/td/db/SqliteStatement.cpp`
   - `tddb/td/db/SqliteKeyValue.cpp`

   Current wrapper responsibilities:
   - connection open/close;
   - statement prepare/bind/step/reset;
   - key-value helpers;
   - tracing/logging;
   - DB-file destruction policy on corruption.

3. **Telegram database policy is implemented outside SQLite source.**

   Proven by:
   - `tddb/td/db/detail/RawSqliteDb.cpp`: on `SQLITE_CORRUPT`, the wrapper destroys the DB files;
   - `tddb/td/db/SqliteDb.cpp`: tracing, statement error handling, transaction helpers, and wrapper-specific logging.

   This is good news for the migration: these behaviors do not need to stay inside vendored SQLite.

4. **Current SQLite feature profile is defined in build settings.**

   Proven by `sqlite/CMakeLists.txt`.

   Current compile-time choices that must be reviewed and intentionally preserved or dropped:
   - `SQLITE_DEFAULT_MEMSTATUS=0`
   - `SQLITE_DEFAULT_RECURSIVE_TRIGGERS=1`
   - `SQLITE_DEFAULT_SYNCHRONOUS=1`
   - `SQLITE_DISABLE_LFS`
   - `SQLITE_ENABLE_FTS5`
   - `SQLITE_HAS_CODEC`
   - `SQLITE_OMIT_DECLTYPE`
   - `SQLITE_OMIT_DEPRECATED`
   - `SQLITE_OMIT_DESERIALIZE`
   - `SQLITE_OMIT_LOAD_EXTENSION`
   - `SQLITE_OMIT_PROGRESS_CALLBACK`
   - `SQLITE_TEMP_STORE=2`
   - `OMIT_MEMLOCK`

5. **`sqlite3session.h` is vendored and renamed, but it appears to be unused outside the vendored tree and `sqlite/CMakeLists.txt`.**

   Evidence from repo search:
   - direct hits are in `sqlite/sqlite/sqlite3session.h` itself and `sqlite/CMakeLists.txt`;
   - there are no obvious consumers in `tddb`, `td`, or tests.

   This should be treated as a keep-or-drop decision during the migration.

### 0.2 Required dependency delta currently embedded in vendor source

These are not Telegram business rules, but they are part of the current vendored source and cannot be ignored during an upgrade.

1. **The current amalgamation is not plain upstream SQLite. It embeds SQLCipher code.**

   Proven by:
   - `sqlite/sqlite/sqlite3.h`: `BEGIN SQLCIPHER`
   - `sqlite/sqlite/sqlite3.c`: `BEGIN SQLCIPHER`, `sqlcipher.h`, `sqlcipher_codec_pragma`, `tdsqlite3_key`, `tdsqlite3_key_v2`, `sqlcipher_exportFunc`
   - `sqlite/CMakeLists.txt`: links OpenSSL and defines `SQLITE_HAS_CODEC`
   - `tddb/td/db/SqliteDb.cpp`: uses `PRAGMA key`, `PRAGMA cipher_compatibility`, and `SELECT sqlcipher_export(...)`

   Consequence:
   - a plain drop-in upgrade from SQLite `3.31.0` to the latest SQLite release is not enough;
   - the target must either remain SQLCipher-based, or the encryption design must be changed deliberately as a separate project.

2. **The current vendor base is old and locally edited.**

   Proven by:
   - `sqlite/sqlite/sqlite3.h`: `SQLITE_VERSION "3.31.0"`
   - `sqlite/sqlite/sqlite3.h`: `SQLITE_SOURCE_ID ... b86alt1`
   - `sqlite/sqlite/sqlite3.c`: same source ID marker

   Consequence:
   - current PVS noise is dominated by old upstream code and imported SQLCipher code, not only by Telegram logic;
   - before rebasing to the latest upstream, we need a clean inventory of what is Telegram-owned versus imported.

3. **The wrapper layer depends on SQLCipher behavior, not only on SQLite behavior.**

   Proven by `tddb/td/db/SqliteDb.cpp`:
   - `db_key_to_sqlcipher_key(...)`
   - `PRAGMA key = ...`
   - `PRAGMA cipher_compatibility = ...`
   - `PRAGMA rekey = ...`
   - `SELECT sqlcipher_export('encrypted')`
   - `SELECT sqlcipher_export('decrypted')`

   Consequence:
   - the upgrade must preserve SQLCipher API semantics or explicitly replace them.

---

## 1. Conclusions

The current stack is not simply “upstream SQLite plus a Telegram rename”. It is:

1. an old vendored base (`3.31.0`),
2. with embedded SQLCipher code,
3. mechanically rewritten to the `tdsqlite3_*` namespace,
4. plus Telegram wrapper/policy code in `tddb/td/db`.

That means the right end-state is not “copy the latest `sqlite3.c` over the old one”. The right end-state is:

1. keep upstream source pristine,
2. keep SQLCipher as an explicit upstream dependency choice,
3. express Telegram-specific changes as generated wrappers and local build logic,
4. keep Telegram DB policies in wrapper code only.

---

## 2. Target State

### 2.1 Functional target

The project should continue to support:

1. the `tdsqlite3_*` API surface consumed by `tddb`;
2. SQLCipher-backed encrypted databases and key migration;
3. current wrapper policies such as corruption cleanup and tracing;
4. the existing include contract used by consumers (`#include "sqlite/sqlite3.h"`).

### 2.2 Vendor-management target

The project should move to this model:

1. **Pristine upstream vendor subtree**
   - contains unmodified upstream amalgamation files;
   - no manual edits allowed.

2. **Generated Telegram compatibility layer**
   - generated rename header(s) and wrapper translation unit(s);
   - no in-place `sed` on upstream files.

3. **Single update script**
   - imports upstream source,
   - regenerates wrapper files,
   - updates metadata,
   - runs validation checks.

4. **CI/review guards**
   - fail if vendored upstream files were edited manually;
   - fail if generated compatibility files are stale.

---

## 3. Recommended Mechanism

### 3.1 Keep pristine upstream files in a dedicated subtree

Recommended layout:

```text
sqlite/
  CMakeLists.txt
  upstream/
    sqlite3.c
    sqlite3.h
    sqlite3ext.h
    sqlite3session.h
  generated/
    tdsqlite_rename.h
  sqlite/
    sqlite3.h
    sqlite3ext.h
    sqlite3session.h
  tdsqlite_amalgamation.c
  VENDOR.json
```

Notes:

1. `upstream/` is pristine vendor input.
2. `generated/tdsqlite_rename.h` is generated, never edited manually.
3. `sqlite/sqlite3.h`, `sqlite/sqlite3ext.h`, and `sqlite/sqlite3session.h` stay at the current include path, but become thin wrappers instead of mutated upstream files.
4. `tdsqlite_amalgamation.c` becomes the only local translation unit that includes upstream source.

### 3.2 Replace source mutation with a generated rename layer

Preferred mechanism:

1. generate a macro-rename header that maps `sqlite3` API identifiers to `tdsqlite3` identifiers;
2. include that rename header before including upstream headers and the upstream amalgamation.

Example shape:

```c
/* generated/tdsqlite_rename.h */
#define sqlite3 tdsqlite3
#define sqlite3_stmt tdsqlite3_stmt
#define sqlite3_value tdsqlite3_value
#define sqlite3_open_v2 tdsqlite3_open_v2
...
```

```c
/* sqlite/sqlite3.h */
#pragma once
#include "../generated/tdsqlite_rename.h"
#include "../upstream/sqlite3.h"
```

```c
/* tdsqlite_amalgamation.c */
#include "generated/tdsqlite_rename.h"
#include "upstream/sqlite3.c"
```

Why this mechanism is preferred:

1. it keeps upstream files pristine;
2. it is cross-platform and does not depend on linker-specific symbol rewriting;
3. it renames types, functions, and internal identifiers consistently at preprocessing time;
4. it eliminates the current brittle `sed` workflow.

### 3.3 Keep build-time configuration explicit and centralized

Current build configuration in `sqlite/CMakeLists.txt` should be preserved in one authoritative place.

Minimum requirement:

1. keep a single list of compile definitions for the SQLite target;
2. document which ones are required for compatibility and which are optional;
3. keep platform link requirements (`OpenSSL`, `ZLIB`, `dl`, Windows socket libs) in the local build system, not in vendor source.

Optional improvement:

1. emit these settings into `VENDOR.json` or a small generated manifest so an upgrade script can verify that the target feature profile did not drift.

### 3.4 Keep Telegram DB policy outside vendor code

The following must remain outside vendor source and should not be reintroduced into the SQLite tree:

1. corruption cleanup behavior from `RawSqliteDb`;
2. tracing/logging behavior from `SqliteDb` and `SqliteStatement`;
3. key migration flow and wrapper ergonomics in `SqliteDb`;
4. key-value helper abstractions in `SqliteKeyValue`.

### 3.5 Treat SQLCipher as an explicit base dependency decision

This is the most important architectural fork.

Because the current tree depends on SQLCipher behavior, Phase 1 must decide between these options:

1. **Preferred:** vendor a pristine upstream SQLCipher amalgamation as the base and apply only Telegram’s rename/build/wrapper layer locally.
2. **Higher-risk alternative:** vendor pristine upstream SQLite plus a scripted SQLCipher patch application step.
3. **Not in scope for this task:** remove SQLCipher dependence entirely and redesign encrypted DB support.

Given the current wrapper behavior, option 1 is the pragmatic path.

---

## 4. Execution Plan

### Phase 1. Baseline audit and dependency decision

Objective:
separate Telegram-owned changes from imported SQLCipher/upstream history.

Tasks:

1. obtain a pristine upstream base matching the current behavior baseline:
   - upstream SQLite `3.31.0` for comparison,
   - and the matching SQLCipher amalgamation/release that explains the `BEGIN SQLCIPHER` blocks.
2. diff current vendored files against that pristine base.
3. classify every delta into one of:
   - imported SQLCipher code,
   - mechanical `tdsqlite3` rename,
   - Telegram build configuration,
   - Telegram wrapper policy,
   - unexpected semantic local patch.
4. decide whether `sqlite3session.h` stays or is removed from the supported surface.

Deliverable:

1. a short audit note that answers: “what is truly Telegram-owned?”

Definition of done:

1. there are no unexplained local semantic patches left inside the vendored source tree.

### Phase 2. Introduce the non-invasive vendoring scaffold without upgrading versions yet

Objective:
prove the mechanism on the current baseline before changing upstream versions.

Tasks:

1. add the pristine vendor subtree;
2. add `generated/tdsqlite_rename.h` generation logic;
3. replace current mutated vendored headers with thin wrapper headers at the same include paths;
4. add `tdsqlite_amalgamation.c` that includes the pristine upstream source through the rename layer;
5. switch `sqlite/CMakeLists.txt` to build from the wrapper TU and upstream subtree;
6. keep `tddb` code unchanged initially.

Deliverable:

1. a build that behaves the same as today while using pristine upstream files plus generated compatibility wrappers.

Definition of done:

1. no file under `sqlite/upstream/` is locally edited;
2. `tddb` still compiles against `tdsqlite3_*` names;
3. runtime behavior for existing DB paths and keying flow is unchanged.

### Phase 3. Add focused validation for the DB stack

Objective:
stop relying only on broad application tests for a risky vendor migration.

Tasks:

1. add focused tests for:
   - open/read/write on an unencrypted DB;
   - open/read/write on an encrypted DB;
   - compatibility fallback via `cipher_compatibility`;
   - key change flows: encrypt, decrypt, rekey;
   - corruption cleanup behavior in `RawSqliteDb`;
   - simple statement prepare/bind/step/reset behavior.
2. wire those tests into the normal native test target.

Hardening extension:

1. add concurrent transaction stress with multiple readers and writers using separate SQLite connections in WAL mode;
2. expand corruption coverage beyond malformed schema with a small corpus of page-level on-disk corruption patterns.

Rationale:

1. current repo search does not show an obvious dedicated SQLite test slice;
2. vendor refresh without narrow DB tests is too risky.

Definition of done:

1. the migration can be validated without relying only on a full `run_all_tests` run.

### Phase 4. Rebase to the current upstream release

Objective:
perform the actual upgrade once the mechanism is proven.

Tasks:

1. replace the pristine upstream vendor subtree with the target latest release;
2. regenerate rename wrappers;
3. review compile definitions for compatibility with the new upstream base;
4. fix only local wrapper/build code as needed;
5. keep vendor source pristine.

Definition of done:

1. the new vendor baseline builds;
2. focused DB tests pass;
3. application tests that cover DB startup and normal operation pass;
4. no manual edits were made inside pristine upstream files.

### Phase 5. Add automation and guardrails

Objective:
make the next upgrade cheap.

Tasks:

1. add a script, for example `tools/sqlite/update_vendor.py`, that:
   - imports upstream source,
   - updates `VENDOR.json`,
   - regenerates rename wrappers,
   - runs a small verification suite.
2. add a CI check that fails when:
   - pristine vendor files were edited manually;
   - generated files are out of date.
3. add a short maintenance doc with one upgrade command sequence.

Definition of done:

1. the upgrade workflow is scripted and reproducible;
2. manual `sed`-based vendor rewriting is removed from the project process.

---

## 5. Concrete Deliverables

The migration should produce these artifacts:

1. `sqlite/upstream/` with pristine upstream files;
2. `sqlite/generated/tdsqlite_rename.h`;
3. thin wrapper headers at the current include paths;
4. `sqlite/tdsqlite_amalgamation.c`;
5. `sqlite/VENDOR.json` with version/source metadata and hashes;
6. `tools/sqlite/update_vendor.py` or equivalent;
7. focused DB tests covering encryption and wrapper behavior;
8. CI checks for pristine-vendor and generated-file integrity.

---

## 6. Validation Plan

At minimum, each migration phase should validate:

1. **Build validation**
   - build the `tdsqlite` and `tddb` slices through the normal CMake build.

2. **Focused runtime validation**
   - run dedicated DB tests added in Phase 3.

3. **Broader integration validation**
   - build `run_all_tests` and run at least the DB-adjacent subset plus a final full-suite pass before merge.

4. **Vendor integrity validation**
   - verify that pristine upstream files exactly match recorded hashes;
   - verify that generated wrapper files match generator output.

---

## 7. Risks And Open Questions

1. **Exact upstream base selection is unresolved.**
   - The current tree clearly includes SQLCipher code, so “latest SQLite” is underspecified.
   - The practical target is likely “latest SQLCipher-compatible upstream base”, not plain SQLite.

2. **Symbol renaming coverage must be exhaustive.**
   - The rename layer must cover public headers, extension APIs, session APIs, globals, and any required internal identifiers touched by the amalgamation.

3. **SQLCipher compatibility may change across upstream versions.**
   - `PRAGMA key`, `PRAGMA cipher_compatibility`, and `sqlcipher_export()` behavior must be revalidated explicitly.

4. **`sqlite3session.h` may be dead weight.**
   - If it is truly unused, dropping it reduces future maintenance.

5. **Current wrapper assumptions may depend on old SQLite behavior.**
   - Statement tracing, busy timeout behavior, WAL behavior, and omitted-feature flags should all be rechecked after the upgrade.

---

## 8. Recommended Order Of Work

1. complete Phase 1 first;
2. implement Phase 2 on the current version before touching upstream versions;
3. add Phase 3 focused tests before the actual rebase;
4. perform the real upstream upgrade in Phase 4;
5. finish with Phase 5 automation and CI guards.

This order minimizes risk because it proves the “no source edits” mechanism before the project also absorbs upstream code churn.
