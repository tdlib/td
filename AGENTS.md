# AGENTS.md

This repository is a TDLib fork with an MTProto-proxy-only stealth traffic-masking subsystem intended to make Telegram traffic harder to classify under DPI-based censorship while preserving TDLib's core library structure.

## Build and test

- Use CMake for configure, build, and test work.
- Configure: `cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DTD_ENABLE_BENCHMARKS=OFF`
- Build focused tests: `cmake --build build --target run_all_tests --parallel 4`
- Run full tests: `ctest --test-dir build --output-on-failure`
- Run the stealth/TLS slice: `./build/test/run_all_tests --filter TlsHello`

## Progressive disclosure

- For current stealth implementation status and DPI threat-model context, read `docs/Plans/STEALTH_IMPLEMENTATION_RU.md`.
- For structural changes, use `.github/instructions/architecture.instructions.md`.
- For C++ conventions, use `.github/instructions/c++_rules.instructions.md`.
- Other reference material lives under `docs/Plans`, `docs/Researches`, `docs/Samples`, and `docs/Standards`.