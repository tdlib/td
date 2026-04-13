# SQLite Vendor Maintenance

This note covers the scripted refresh path for the vendored SQLCipher baseline under `sqlite/`.

## Prerequisites

- `python3`, `make`, and a working C toolchain must be available.
- `gperf` must be installed before the native CMake configure/build steps; on Debian/Ubuntu hosts use `sudo apt-get update && sudo apt-get install -y gperf`.
- A system `tclsh` is preferred.
- On Debian/Ubuntu hosts, `tools/sqlite/update_vendor.py` can bootstrap a local Tcl runtime automatically with `apt-get download` and `dpkg-deb` if `tclsh` is not installed.
- On other hosts, pass `--tclsh /path/to/tclsh`.

## Upgrade Sequence

Run the scripted refresh from the repository root:

```bash
python3 tools/sqlite/update_vendor.py --release-tag v4.14.0 --source-tarball-sha256 a86d78d5556a699059b47fc0899fc13bd8f7029b88f40a84831fdeb7c1fe5191
python3 tools/sqlite/audit_vendor.py --check-integrity
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DTD_ENABLE_BENCHMARKS=OFF
cmake --build build --target run_all_tests --parallel 2
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DTD_ENABLE_BENCHMARKS=OFF
ctest --test-dir build/test --output-on-failure -R '^Test_DB_sqlite_vendor_wrapper_surface_contract$'
```

The second configure pass is intentional: `test/CMakeLists.txt` registers exact `run_all_tests --exact ...` cases during configure by querying the already-built `run_all_tests` binary, so clean trees need a post-build refresh before the targeted CTest smoke can resolve the exact case name.

For the current pinned release, the updater can also reuse the digest already recorded in `sqlite/VENDOR.json`. Pass `--source-tarball-sha256` explicitly when refreshing to a new release or a different tarball URL.

The default GitHub API tarball flow redirects to `codeload.github.com`; the updater now treats that host as part of the pinned GitHub fetch path while still rejecting redirect targets outside the allowlist.

If the source tree is already extracted locally, reuse it instead of downloading a tarball:

```bash
python3 tools/sqlite/update_vendor.py \
	--release-tag v4.14.0 \
	--source-dir /path/to/sqlcipher-source \
	--source-tarball-sha256 a86d78d5556a699059b47fc0899fc13bd8f7029b88f40a84831fdeb7c1fe5191
```

## What The Script Refreshes

- `sqlite/upstream/` pristine SQLCipher-derived upstream files
- `sqlite/generated/tdsqlite_rename.h`
- thin wrapper headers in `sqlite/sqlite/`
- `sqlite/tdsqlite_amalgamation.c`
- `sqlite/VENDOR.json`

## Fast Guardrails

- `python3 tools/sqlite/audit_vendor.py --check-integrity` fails if managed file hashes drift or generated sidecars are stale.
- `python3 -m unittest discover -s test/analysis -p 'test_sqlite*.py'` rechecks the vendor scaffolding, updater guardrails, codegen, audit logic, and manifest contracts.

If a custom tarball mirror is required, keep it on HTTPS and explicitly allow the host:

```bash
python3 tools/sqlite/update_vendor.py \
	--release-tag v4.14.0 \
	--source-tarball-url https://mirror.example/sqlcipher-v4.14.0.tar.gz \
	--source-tarball-sha256 a86d78d5556a699059b47fc0899fc13bd8f7029b88f40a84831fdeb7c1fe5191 \
	--allow-source-host mirror.example
```

## VS Code CMake Tools

The workspace now ships a checked-in `CMakePresets.json` plus preset-backed VS Code tasks so CMake Tools can configure the repository without manual generator arguments.

For the native SQLite wrapper smoke in VS Code, run these workspace tasks in order:

```text
cmake configure preset
cmake build tests preset
cmake test sqlite vendor smoke
```

The smoke task targets the exact CTest case `Test_DB_sqlite_vendor_wrapper_surface_contract`, so it avoids broad `ctest` runs and does not touch any `1k` suites.