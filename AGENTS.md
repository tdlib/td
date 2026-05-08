# AGENTS.md

TDLib fork with MTProto-proxy-only stealth traffic-masking for DPI evasion.

## Build and test

- Use CMake; builds into `build/` directory
- Configure: `cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DTD_ENABLE_BENCHMARKS=OFF`
- Enable stealth seams: add `-DTDLIB_STEALTH_SHAPING=ON`
- Build tests: `cmake --build build --target run_all_tests --parallel 4`
- Run full test suite: `ctest --test-dir build --output-on-failure`
- Run stealth/TLS slice: `./build/test/run_all_tests --filter TlsHello`

## Common test filters

- `TlsHello` — stealth transport tests
- `EntryWindow` — entry window tests
- `AuxChannel` — aux channel tests
- `BlobStore` — blob store tests
- `WindowCount` — window count tests
- `EntryCount` — entry count tests
- `ReferenceTable` — reference table tests
- `SourceLayout` — source layout tests

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