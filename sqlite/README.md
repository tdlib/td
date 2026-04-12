# SQLite Vendor Subtree

This directory vendors the SQLCipher-backed SQLite baseline used by TDLib's database layer while keeping upstream-derived inputs separate from Telegram-owned compatibility glue.

## Layout

- `sqlite/upstream/` holds the pristine upstream-derived SQLCipher files copied from the refresh source tree.
- `sqlite/generated/` holds generated compatibility artifacts, currently `tdsqlite_rename.h`.
- `sqlite/sqlite/` keeps the historical public include paths as thin wrapper headers.
- `sqlite/tdsqlite_amalgamation.c` is the only local translation unit that compiles the upstream amalgamation through the generated rename layer.
- `sqlite/VENDOR.json` records the pinned release metadata, source tarball digest, generator inputs, and managed-file hashes.

## Invariants

- Do not hand-edit files under `sqlite/upstream/`.
- Do not hand-edit files under `sqlite/generated/`, `sqlite/sqlite/`, or `sqlite/tdsqlite_amalgamation.c`; regenerate them.
- Do not reintroduce `sed`-based in-place renaming of upstream files.
- Keep SQLCipher-related build flags centralized in `sqlite/CMakeLists.txt` and keep wrapper policy in `tddb/td/db/`.

## Refresh Workflow

Run the refresh from the repository root:

```bash
python3 tools/sqlite/update_vendor.py --release-tag v4.14.0 --source-tarball-sha256 a86d78d5556a699059b47fc0899fc13bd8f7029b88f40a84831fdeb7c1fe5191
python3 tools/sqlite/audit_vendor.py --check-integrity
python3 -m unittest discover -s test/analysis -p 'test_sqlite*.py'
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DTD_ENABLE_BENCHMARKS=OFF
cmake --build build --target run_all_tests --parallel 2
ctest --test-dir build/test --output-on-failure -R '^Test_DB_sqlite_vendor_wrapper_surface_contract$'
```

Notes:

- Pass `--source-tarball-sha256` explicitly for any new release or custom tarball URL.
- If `tclsh` is unavailable, `tools/sqlite/update_vendor.py` can bootstrap a local Tcl runtime on Debian/Ubuntu hosts.
- The targeted CTest smoke stays on the SQLite wrapper surface and avoids any `1k` suites.

## Security And Integrity Guards

- `tools/sqlite/update_vendor.py` only accepts pinned tarball digests and HTTPS source URLs.
- The updater rejects unsafe managed-output paths such as symlinked vendor files or symlinked parent directories.
- `tools/sqlite/audit_vendor.py --check-integrity` fails when manifest hashes drift or generated sidecars are stale.
- `.github/workflows/sqlite-vendor-integrity.yml` reruns the Python guardrails and the targeted native DB smoke in CI.

## Where To Look Next

- Use `docs/Plans/SQLITE_VENDOR_MAINTENANCE_2026-04-13.md` for the dated maintenance note.
- Use `docs/Plans/SQLITE_UPGRADE_AND_VENDOR_ISOLATION_PLAN_2026-04-11.md` for the migration rationale and phase breakdown.