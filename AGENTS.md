# AGENTS.md

TDLib fork with MTProto-proxy-only stealth traffic-masking for DPI evasion.

## Build and test

- Use CMake; builds into `build/` directory
- Configure: `cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DTD_ENABLE_BENCHMARKS=OFF`
- Enable stealth seams: add `-DTDLIB_STEALTH_SHAPING=ON`
- Build tests: `cmake --build build --target run_all_tests --parallel 4`
- Run full test suite: `ctest --test-dir build --output-on-failure`
- Run stealth/TLS slice: `./build/test/run_all_tests --filter TlsHello`
- Run sanitizer lanes from repo root with `python3 tools/ci/run_sanitizer_matrix.py`; prefer this runner over ad hoc sanitizer configure/build/test chains.

## End-to-end harnesses

- The repository already has a real client/database end-to-end harness in `test/tdclient.cpp` and `test/online.cpp`.
- These tests bootstrap real `td::ClientActor` flows and real TDLib parameters such as `database_directory_`, `use_message_database_`, and `use_secret_chats_`.
- Use this harness when you need true actor + database + authorization/client lifecycle coverage.
- Do not treat it as a lightweight default fixture for narrow backport audits: it is heavier than seam/runtime tests and usually pulls in auth/network-oriented setup.
- For local-equivalent backport fixes in parser/repair/helper logic, prefer focused runtime/serialization tests first; escalate to `tdclient.cpp`/`online.cpp` only when the risk genuinely requires full-stack behavior.

## Common test filters

- `TlsHello` — stealth transport tests
- `EntryWindow` — entry window tests
- `AuxChannel` — aux channel tests
- `BlobStore` — blob store tests
- `WindowCount` — window count tests
- `EntryCount` — entry count tests
- `ReferenceTable` — reference table tests
- `SourceLayout` — source layout tests

## SocratiCode workflow

- MCP server is configured in `.vscode/mcp.json` as `socraticode`.
- First time in this repository, run index and keep polling status until complete:
	- `codebase_index`
	- `codebase_status` (repeat until 100%)
- Keep the watcher running for incremental updates:
	- `codebase_watch { action: "start" }`
- Use search-first exploration to minimize context usage:
	- `codebase_search` for conceptual discovery and unknown locations
	- `rg` for exact strings/identifiers when you already know the token
- Before refactors, rename, or delete operations, run blast-radius checks:
	- `codebase_impact` for symbol/file impact
	- `codebase_flow` for forward execution tracing from entry points
- Troubleshooting:
	- `codebase_health` for Docker/Qdrant/Ollama status
	- `codebase_status` if search returns no results

## context-mode — MANDATORY routing rules

context-mode MCP tools available. Rules protect context window from flooding. One unrouted command dumps 56 KB into context.

## Think in Code — MANDATORY

Analyze/count/filter/compare/search/parse/transform data: **write code** via `ctx_execute(language, code)`, `console.log()` only the answer. Do NOT read raw data into context. PROGRAM the analysis, not COMPUTE it. Pure JavaScript — Node.js built-ins only (`fs`, `path`, `child_process`). `try/catch`, handle `null`/`undefined`. One script replaces ten tool calls.

## BLOCKED — do NOT attempt

### curl / wget — BLOCKED
Terminal `curl`/`wget` intercepted and blocked. Do NOT retry.
Use: `ctx_fetch_and_index(url, source)` or `ctx_execute(language: "javascript", code: "const r = await fetch(...)")`

### Inline HTTP — BLOCKED
`fetch('http`, `requests.get(`, `requests.post(`, `http.get(`, `http.request(` — intercepted. Do NOT retry.
Use: `ctx_execute(language, code)` — only stdout enters context

### WebFetch / fetch — BLOCKED
Use: `ctx_fetch_and_index(url, source)` then `ctx_search(queries)`

## REDIRECTED — use sandbox

### Terminal / run_in_terminal (>20 lines output)
Terminal ONLY for: `git`, `mkdir`, `rm`, `mv`, `cd`, `ls`, `npm install`, `pip install`.
Otherwise: `ctx_batch_execute(commands, queries)` or `ctx_execute(language: "shell", code: "...")`

### read_file (for analysis)
Reading to **edit** → `read_file` correct. Reading to **analyze/explore/summarize** → `ctx_execute_file(path, language, code)`.

### grep / search (large results)
Use `ctx_execute(language: "shell", code: "grep ...")` in sandbox.

## Tool selection

0. **MEMORY**: `ctx_search(sort: "timeline")` — after resume, check prior context before asking user.
1. **GATHER**: `ctx_batch_execute(commands, queries)` — runs all commands, auto-indexes, returns search. ONE call replaces 30+. Each command: `{label: "header", command: "..."}`.
2. **FOLLOW-UP**: `ctx_search(queries: ["q1", "q2", ...])` — all questions as array, ONE call (default relevance mode).
3. **PROCESSING**: `ctx_execute(language, code)` | `ctx_execute_file(path, language, code)` — sandbox, only stdout enters context.
4. **WEB**: `ctx_fetch_and_index(url, source)` then `ctx_search(queries)` — raw HTML never enters context.
5. **INDEX**: `ctx_index(content, source)` — store in FTS5 for later search.

### Parallel I/O batches
Pass `concurrency: 4-8` to `ctx_batch_execute` and `ctx_fetch_and_index` for network/API batches. Keep `concurrency: 1` for CPU-bound work (test, build, lint). GitHub gh: cap at 4.

## Output

Write artifacts to FILES — never inline. Return: file path + 1-line description.
Descriptive source labels for `ctx_search(source: "label")`.

## Session Continuity

Skills, roles, and decisions persist for the entire session. Do not abandon them as the conversation grows.

## Memory

Session history is persistent and searchable. On resume, search BEFORE asking the user:

| Need | Command |
|------|---------|
| What were we working on? | `ctx_search(queries: ["summary"], source: "compaction", sort: "timeline")` |
| What did we decide? | `ctx_search(queries: ["decision"], source: "decision", sort: "timeline")` |
| What NOT to repeat? | `ctx_search(queries: ["rejected"], source: "rejected-approach")` |
| What constraints exist? | `ctx_search(queries: ["constraint"], source: "constraint")` |

Note: user-prompt history not available.

DO NOT ask "what were we working on?" — SEARCH FIRST.
If search returns 0 results, proceed as a fresh session.

## ctx commands

| Command | Action |
|---------|--------|
| `ctx stats` | Call `ctx_stats` MCP tool, display full output verbatim |
| `ctx doctor` | Call `ctx_doctor` MCP tool, run returned shell command, display as checklist |
| `ctx upgrade` | Call `ctx_upgrade` MCP tool, run returned shell command, display as checklist |
| `ctx purge` | Call `ctx_purge` MCP tool with confirm: true. Warns before wiping knowledge base. |

After /clear or /compact: knowledge base and session stats preserved. Use `ctx purge` to start fresh.

## Architecture & conventions

Use the instruction files below as the authoritative implementation and review rules for this repository:

- Treat `applyTo` as an activation hint: if you touch a listed file or the matching behavior category, read that instruction before editing.
- In particular, load the Doxygen reminder as soon as work becomes consumer-facing API work, and load the logging reminder as soon as work adds or changes a logging point or logging control path.

- **Architecture**: Layered design and structural constraints. See `.github/instructions/architecture.instructions.md`
- **C++ rules**: Modern C++ coding rules and Core Guidelines alignment. See `.github/instructions/c++_rules.instructions.md`
- **C++17 reference**: Language and library guidance for C++17 features. See `.github/instructions/CPP17.md`
- **C++20 reference**: Language and library guidance for C++20 features. See `.github/instructions/CPP20.md`
- **C++23 reference**: Language and library guidance for C++23 features. See `.github/instructions/CPP23.md`
- **Doxygen API docs**: Activation reminder for consumer-facing API work, request plumbing, and Doxygen input changes so public APIs are routed through the canonical documentation pipeline. See `.github/instructions/doxygen_api_documentation.instructions.md`
- **Logging subsystem**: Activation reminder for `LOG`/`VLOG` points, tags, sinks, callbacks, fatal logging paths, and public logging APIs so changes stay integrated with the existing subsystem. See `.github/instructions/logging_subsystem.instructions.md`
- **Sanitizer triage**: Activation reminder for ASan/UBSan/LSan/MSan/TSan investigations, exact reproducer workflow, and canonical matrix runner usage. See `.github/instructions/sanitizer_triage.instructions.md`
- **Security requirements**: OWASP ASVS L2 secure coding requirements. See `.github/instructions/Security_Requirements.instructions.md`
- **SonarQube MCP**: SonarQube MCP workflow and analysis guidance. See `.github/instructions/sonarqube_mcp.instructions.md`
- **TDD approach**: Adversarial testing required before implementation. See `.github/instructions/TDD_approach.instructions.md`

## License headers

- Modified existing Boost-derived files must use the dual SPDX header:
	- `SPDX-License-Identifier: BSL-1.0 AND MIT`
	- include both copyright lines: the original TDLib copyright line and `Copyright 2026 telemt community`
	- include the `telemt` project links already used in the repo header format
- New files created in this repository should use the telemt-only MIT header:
	- `SPDX-FileCopyrightText: Copyright 2026 telemt community`
	- `SPDX-License-Identifier: MIT`
	- include the same `telemt` project links
- Do not rewrite third-party or GPL-licensed file headers into the dual SPDX form. The dual header rule applies to Boost-derived repository files only.

Dual SPDX example for modified existing Boost-derived source files:

```cpp
// SPDX-FileCopyrightText: Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: BSL-1.0 AND MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
```

MIT-only example for newly created files:

```cpp
// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
```

## Reference material

- DPI context: `docs/Plans/STEALTH_IMPLEMENTATION_RU.md`
- Wave 2 implementation status: `docs/Plans/WAVE2_IMPLEMENTATION_STATUS_2026-04-17.md`
- Samples: `docs/Samples/GoodbyeDPI/README.md`, `docs/Samples/JA4/README.md`
- Standards: `docs/Standards/rfc8446.txt` (TLS), `docs/Standards/rfc7685.txt` (QUIC)

<!-- BEGIN BEADS INTEGRATION v:1 profile:minimal hash:7510c1e2 -->
## Beads Issue Tracker

This project uses **bd (beads)** for issue tracking. Run `bd prime` to see full workflow context and commands.

### Quick Reference

```bash
bd ready              # Find available work
bd show <id>          # View issue details
bd update <id> --claim  # Claim work
bd close <id>         # Complete work
```

### Rules

- Use `bd` for ALL task tracking — do NOT use TodoWrite, TaskCreate, or markdown TODO lists
- Run `bd prime` for detailed command reference and session close protocol
- Use `bd remember` for persistent knowledge — do NOT use MEMORY.md files

**Architecture in one line:** issues live in a local Dolt DB; sync uses `refs/dolt/data` on your git remote; `.beads/issues.jsonl` is a passive export. See https://github.com/gastownhall/beads/blob/main/docs/SYNC_CONCEPTS.md for details and anti-patterns.

## Session Completion

**When ending a work session**, you MUST complete ALL steps below. Work is NOT complete until `git push` succeeds.

**MANDATORY WORKFLOW:**

1. **File issues for remaining work** - Create issues for anything that needs follow-up
2. **Run quality gates** (if code changed) - Tests, linters, builds
3. **Update issue status** - Close finished work, update in-progress items
4. **PUSH TO REMOTE** - This is MANDATORY:
   ```bash
   git pull --rebase
   git push
   git status  # MUST show "up to date with origin"
   ```
5. **Clean up** - Clear stashes, prune remote branches
6. **Verify** - All changes committed AND pushed
7. **Hand off** - Provide context for next session

**CRITICAL RULES:**
- Work is NOT complete until `git push` succeeds
- NEVER stop before pushing - that leaves work stranded locally
- NEVER say "ready to push when you are" - YOU must push
- If push fails, resolve and retry until it succeeds
<!-- END BEADS INTEGRATION -->

<!-- BEGIN BEADS CODEX SETUP: generated by bd setup codex -->
## Beads Issue Tracker

Use Beads (`bd`) for durable task tracking in repositories that include it. Use the `beads` skill at `.agents/skills/beads/SKILL.md` (project install) or `~/.agents/skills/beads/SKILL.md` (global install) for Beads workflow guidance, then use the `bd` CLI for issue operations.

### Quick Reference

```bash
bd ready                # Find available work
bd show <id>            # View issue details
bd update <id> --claim  # Claim work
bd close <id>           # Complete work
bd prime                # Refresh Beads context
```

### Rules

- Use `bd` for all task tracking; do not create markdown TODO lists.
- Run `bd prime` when Beads context is missing or stale.
- Keep persistent project memory in Beads via `bd remember`; do not create ad hoc memory files.
<!-- END BEADS CODEX SETUP -->
