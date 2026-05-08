---
description: "Activation reminder for public API work. Use this when touching a consumer-facing API surface, request plumbing for a new public API, or Doxygen inputs, so the change is routed through the canonical documentation pipeline instead of ad hoc edits."
applyTo: "td/generate/scheme/td_api.tl, td/generate/doxygen_tl_docs.py, td/telegram/Client.h, td/telegram/Log.h, td/telegram/TdCallback.h, td/telegram/td_json_client.h, td/telegram/td_log.h, td/telegram/Requests.cpp, td/telegram/Requests.h, td/telegram/cli.cpp, td/tl/*.h, tde2e/td/e2e/*.h, docs/api/*.md, docs/Documentation/API_DOCUMENTATION.md, Doxyfile.in, CMakeLists.txt, td/generate/CMakeLists.txt"
---

# Doxygen API Reminder

Use this reminder when you touch a public API surface or get blocked on how a new public API should be documented for integrators.

## When this reminder matters

1. You are adding or changing a public TDLib method, type, field, callback, or curated API page.
2. You are plumbing a new public API through `td_api.tl`, `Requests.cpp`, `Requests.h`, or CLI glue.
3. You need to decide whether a symbol belongs in the published Doxygen surface.

## What to do immediately

1. Work backward from the published Doxygen page an integrator should see.
2. Update the real source of truth, not generated output.
3. Keep the public API readable for integrators and keep internal-only details out of the canonical surface.

## Pick the correct source of truth

1. Generated TDLib API classes, functions, and fields: `td/generate/scheme/td_api.tl`.
2. Generated helper and banner docs injected into `td_api.h`, such as `object_ptr`, `make_object`, `move_object_as`, numeric aliases, and the file overview: `td/generate/doxygen_tl_docs.py`.
3. Handwritten public APIs: the public header or curated docs page already listed in `Doxyfile.in`.
4. Published input allowlist: `Doxyfile.in` and `docs/api/public_api_surfaces.md`.
5. Never hand-edit `td/generate/auto/td/telegram/td_api.h` or `td/generate/auto/td/telegram/td_api.hpp`.

## Public API checklist

1. If the symbol is public, make sure it is documented in the form Doxygen will publish.
2. If you add or remove a public input, update both `Doxyfile.in` and `docs/api/public_api_surfaces.md`.
3. Because `EXTRACT_ALL = NO`, undocumented public symbols are easy to lose from the published view.
4. In handwritten public headers, use `/** ... */` blocks and document what integrators actually need: ownership, lifetime, nullability, thread-safety, callback restrictions, synchronous-vs-asynchronous behavior, and fatal/crash semantics.
5. Keep markup simple. The generator escapes HTML, rewrites quoted URLs into anchors, and sanitizes comment terminators.

## Build reminders

1. `td/generate/CMakeLists.txt` runs `generate_common` and then `doxygen_tl_docs.py` inside `tl_generate_common` when Python 3 is available.
2. The root `td_generate_api_docs` target exists only when Python 3 and Doxygen were found at CMake configure time.
3. If that target is missing after installing tools, rerun CMake configure.
4. The configured file is `build/Doxyfile.api`, and the canonical HTML output is `build/docs/api/html/index.html`.

## Validation

1. Preferred check: `cmake --build build --target td_generate_api_docs`.
2. Review the rendered page an integrator would actually read.
3. If Doxygen cannot run locally, at least verify that the correct source-of-truth files were changed.