<!--
SPDX-FileCopyrightText: Copyright 2026 telemt community
SPDX-License-Identifier: MIT
telemt: https://github.com/telemt
telemt: https://t.me/telemtrs
-->

# PR23 Upstream Backport Audit Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Audit the staged PR23 backport changes against the repo's architecture, security, C++23, and TDD principles, and only promote a fix if a real defect is proven with red-first tests.

**Architecture:** The audit runs with a preflight policy check followed by forty-four batches. Batches 1-8 cover the already-scoped PR23 seam, dependency, dialog, video, MTProto, and business/auth surfaces. Batches 9-44 partition every remaining non-cache `feat/upstream-backport-bulk` vs `master` delta into slices of at most ten files so the whole branch can be audited without scope drift. If any batch exposes a real bug, the workflow pauses, writes a red test in a separate file, makes the smallest fix, and reruns the same targeted filter until green.

**Tech Stack:** `git`, `rg`, SocratiCode `codebase_search`/`codebase_impact`, context-mode `ctx_batch_execute`/`ctx_execute_file`, CMake, `ctest`, `./build/test/run_all_tests`, C++23.

**Status:** PLAN ONLY. No production code changes yet.

---

### Preflight: Policy and audit framing

**Files:**
- Inspect: `AGENTS.md`
- Inspect: `.github/instructions/architecture.instructions.md`
- Inspect: `.github/instructions/c++_rules.instructions.md`
- Inspect: `.github/instructions/Security_Requirements.instructions.md`
- Inspect: `.github/instructions/TDD_approach.instructions.md`

- [ ] **Step 1: Load repository policy**

Re-read `AGENTS.md` and confirm the backport audit must stay aligned with the repo's own workflow, test, and security rules.

- [ ] **Step 2: Load architecture rules**

Re-read `.github/instructions/architecture.instructions.md` and confirm the audit will judge the backport against layered design and minimal-surface rules.

- [ ] **Step 3: Load C++ rules**

Re-read `.github/instructions/c++_rules.instructions.md` and confirm the audit will check ownership, const-correctness, and repository-specific C++ conventions.

- [ ] **Step 4: Load security and TDD rules**

Re-read `.github/instructions/Security_Requirements.instructions.md` and `.github/instructions/TDD_approach.instructions.md` and confirm the audit will stay deny-by-default, test-first, and red-first if a defect is real.

---

### Task 1: Batch 1 - Baseline, seam, and build-wiring audit

**Files:**
- Inspect: `td/telegram/BackportTestSeams.h`
- Inspect: `td/telegram/Dependencies.h`
- Inspect: `td/telegram/CallActor.cpp`
- Inspect: `td/telegram/MessageContent.cpp`
- Inspect: `td/telegram/MessagesManager.cpp`
- Inspect: `td/telegram/VideosManager.cpp`
- Inspect: `test/CMakeLists.txt`

- [ ] **Step 1: Confirm the exact staged surface**

Run:
```bash
git status --short --branch
git diff --cached --stat
git diff --cached --name-only
```
Expected: the staged set matches the PR23 backport surface and contains no surprise files.

- [ ] **Step 2: Inspect the new seam file**

Read `td/telegram/BackportTestSeams.h` and confirm every helper is pure, minimal, and exists only to support tests.

- [ ] **Step 3: Inspect the new dependency accessor**

Read `td/telegram/Dependencies.h` and confirm `get_user_ids()` is read-only and does not weaken dependency resolution or allow mutation through a test seam.

- [ ] **Step 4: Inspect the call notification refactor**

Read `td/telegram/CallActor.cpp` and confirm the `PendingCallNotificationAction` refactor preserves the old outgoing/incoming and pending/ready branching exactly.

- [ ] **Step 5: Inspect the message-repair helpers**

Read `td/telegram/MessageContent.cpp`, `td/telegram/MessagesManager.cpp`, and `td/telegram/VideosManager.cpp` for unintended semantic drift introduced by the new helper indirection.

- [ ] **Step 6: Inspect test registration**

Read `test/CMakeLists.txt` and confirm each new test file is registered exactly once under the expected suite name.

- [ ] **Step 7: Check the diff boundary**

Confirm the plan stays scoped to the staged PR23 backport surface and does not drift into unrelated repository cleanup.

- [ ] **Step 8: Check helper exposure**

Confirm the new test seam does not expose any mutable state that production code can now reach directly.

- [ ] **Step 9: Check for unintended API surface**

Confirm `Dependencies::get_user_ids()` is only a read-only inspection hook and not a new general-purpose accessor.

- [ ] **Step 10: Stop on ambiguity**

If any item in this batch is not provably safe, freeze the batch and write a red contract test in a separate file before changing code.

---

### Task 2: Batch 2 - Managed bot dependency and call-notification audit

**Files:**
- Inspect: `td/telegram/MessageContent.cpp`
- Inspect: `td/telegram/Dependencies.h`
- Inspect: `td/telegram/BackportTestSeams.h`
- Inspect: `td/telegram/CallActor.cpp`
- Inspect: `test/managed_bot_created_dependency_contract.cpp`
- Inspect: `test/managed_bot_created_dependency_adversarial.cpp`
- Inspect: `test/managed_bot_created_dependency_integration.cpp`
- Inspect: `test/managed_bot_created_dependency_runtime.cpp`
- Inspect: `test/call_notification_send_closure_later_contract.cpp`
- Inspect: `test/call_notification_send_closure_later_adversarial.cpp`

- [ ] **Step 1: Verify ManagedBotCreated dependency semantics**

Read the `MessageContent.cpp` ManagedBotCreated branch and confirm the new helper still resolves `bot_user_id` as a required dependency, not as a best-effort hint.

- [ ] **Step 2: Verify the contract test is real**

Read `test/managed_bot_created_dependency_contract.cpp` and confirm it checks the source contract itself, not just the helper name or a brittle formatting artifact.

- [ ] **Step 3: Verify adversarial and runtime coverage**

Read `test/managed_bot_created_dependency_adversarial.cpp`, `test/managed_bot_created_dependency_integration.cpp`, and `test/managed_bot_created_dependency_runtime.cpp` and confirm they cover missing bot-user resolution, malformed service-message shapes, and end-to-end dependency repair.

- [ ] **Step 4: Verify the notification action helper**

Read `td/telegram/CallActor.cpp` and the seam helpers in `td/telegram/BackportTestSeams.h`; confirm the notification action table is a pure translation of the old branching logic.

- [ ] **Step 5: Verify deferred add vs immediate remove**

Read `test/call_notification_send_closure_later_contract.cpp`, `test/call_notification_send_closure_later_adversarial.cpp`, and `test/call_notification_send_closure_later_runtime.cpp` and confirm they exercise the deferred add, immediate remove, and no-op cases separately.

- [ ] **Step 6: Check ordering and race windows**

Confirm the `send_closure_later` path does not introduce a new ordering regression or race window relative to the previous direct branch.

- [ ] **Step 7: Cross-check the blast radius before edits**

If a divergence appears, use `codebase_search` and `codebase_impact` on the affected symbol before touching the implementation.

- [ ] **Step 8: Prove the bug before fixing it**

If the change is wrong, write the smallest failing test first in a separate file instead of weakening the existing contracts.

- [ ] **Step 9: Run the targeted slice**

Run:
```bash
./build/test/run_all_tests --filter ManagedBotCreatedDependency
./build/test/run_all_tests --filter CallNotificationSendClosureLater
```
Expected: either green, or a single reproducible failure that maps to one concrete contract gap.

- [ ] **Step 10: Record the result**

Capture any confirmed issue with exact file/line references and a short severity note before moving to the next batch.

---

### Task 3: Batch 3 - Dialog repair, reply, and username audit

**Files:**
- Inspect: `td/telegram/MessagesManager.cpp`
- Inspect: `test/parse_dialog_repair_refetch_contract.cpp`
- Inspect: `test/parse_dialog_repair_refetch_adversarial.cpp`
- Inspect: `test/parse_dialog_repair_refetch_integration.cpp`
- Inspect: `test/parse_dialog_repair_refetch_runtime.cpp`
- Inspect: `test/parse_dialog_repair_refetch_stress.cpp`
- Inspect: `test/reply_and_username_contract.cpp`
- Inspect: `test/reply_and_username_adversarial.cpp`
- Inspect: `test/reply_and_username_integration.cpp`
- Inspect: `test/reply_and_username_runtime_contract.cpp`

- [ ] **Step 1: Audit the dialog repair path**

Read the `MessagesManager.cpp` parse-dialog repair branch and confirm dependency repairs are scheduled in a fail-closed order.

- [ ] **Step 2: Check the repair operation decomposition**

Confirm `make_dialog_dependency_repair_operations()` preserves every previous fetch/reload action and does not silently drop any unresolved message or dialog refresh.

- [ ] **Step 3: Verify the parse-dialog contract test**

Read `test/parse_dialog_repair_refetch_contract.cpp` and confirm it pins the real repair contract rather than only the helper names.

- [ ] **Step 4: Verify adversarial coverage**

Read `test/parse_dialog_repair_refetch_adversarial.cpp` and confirm it attacks missing dependencies, malformed database state, and retry behavior.

- [ ] **Step 5: Verify integration and stress coverage**

Read `test/parse_dialog_repair_refetch_integration.cpp`, `test/parse_dialog_repair_refetch_runtime.cpp`, and `test/parse_dialog_repair_refetch_stress.cpp` and confirm they cover the full repair loop under realistic load.

- [ ] **Step 6: Audit reply and username semantics**

Read `test/reply_and_username_contract.cpp` and `test/reply_and_username_runtime_contract.cpp` and confirm same-chat unsent replies, local-message handling, and username checks still follow the current repo policy.

- [ ] **Step 7: Verify fuzz and stress guardrails**

Read `test/reply_and_username_light_fuzz.cpp`, `test/reply_and_username_adversarial.cpp`, `test/reply_and_username_integration.cpp`, and `test/reply_and_username_stress.cpp` and confirm they pin the required guard patterns instead of overfitting to current formatting.

- [ ] **Step 8: Check for duplicate retries or loops**

Confirm the dialog repair flow does not accidentally duplicate network requests or introduce a retry loop when multiple dependencies fail at once.

- [ ] **Step 9: Run the targeted slice**

Run:
```bash
./build/test/run_all_tests --filter 'ParseDialogRepairRefetch|ReplyAndUsername'
```
Expected: either green, or one reproducible failure that can be isolated to a single dialog-repair or reply rule.

- [ ] **Step 10: Capture the outcome**

If anything is flaky, reproduce it with a minimal seed or fixture, keep the failing test separate, and record whether the issue is a true regression or a stale test assumption.

---

### Task 4: Batch 4 - Dialog action equality, video repair, and final regression sweep

**Files:**
- Inspect: `td/telegram/DialogAction.h`
- Inspect: `td/telegram/VideosManager.h`
- Inspect: `td/telegram/VideosManager.cpp`
- Inspect: `td/telegram/MessageContent.cpp`
- Inspect: `test/dialog_action_equality_fields_contract.cpp`
- Inspect: `test/dialog_action_equality_fields_runtime.cpp`
- Inspect: `test/dialog_action_equality_fields_light_fuzz.cpp`
- Inspect: `test/video_alternative_properties_repair_contract.cpp`
- Inspect: `test/video_alternative_properties_repair_adversarial.cpp`
- Inspect: `test/video_alternative_properties_repair_integration.cpp`

- [ ] **Step 1: Audit the dialog-action equality semantics**

Read `td/telegram/DialogAction.h` and the three `dialog_action_equality_fields_*` tests to verify `random_id_` and `text_` are compared wherever equality drives deduplication or update suppression.

- [ ] **Step 2: Check the fuzz target really hits the contract**

Read `test/dialog_action_equality_fields_light_fuzz.cpp` and confirm it mutates the fields that matter and fails when either field is ignored.

- [ ] **Step 3: Audit the video repair implementation**

Read `td/telegram/VideosManager.cpp`, `td/telegram/VideosManager.h`, and `td/telegram/MessageContent.cpp` and confirm the alternative-video repair plan only fills missing duration or thumbnail data.

- [ ] **Step 4: Verify the video repair contract**

Read `test/video_alternative_properties_repair_contract.cpp` and confirm it pins the expected repair behavior for missing duration and missing thumbnail cases.

- [ ] **Step 5: Verify adversarial coverage for conflicting alternatives**

Read `test/video_alternative_properties_repair_adversarial.cpp` and confirm it rejects conflicting alternative durations, bogus thumbnail promotion, and primary-data overwrite.

- [ ] **Step 6: Verify integration, fuzz, and runtime coverage**

Read `test/video_alternative_properties_repair_integration.cpp`, `test/video_alternative_properties_repair_light_fuzz.cpp`, and `test/video_alternative_properties_repair_runtime.cpp` and confirm they cover the full repair path without relying on a single happy-path fixture.

- [ ] **Step 7: Check the helper invariants**

Confirm `get_alternative_video_repair_plan()` is deterministic, explicit about its invariants, and does not mutate valid primary values.

- [ ] **Step 8: Run the targeted slice**

Run:
```bash
./build/test/run_all_tests --filter 'DialogActionEqualityFields|VideoAlternativePropertiesRepair'
```
Expected: green, or a single reproducible failure that maps to one concrete semantics bug.

- [ ] **Step 9: Escalate real defects with red-first tests**

If the slice fails for a real reason, add a new red test in a separate file first, then make the minimal code fix, then rerun the same targeted slice until green.

- [ ] **Step 10: Close this wave**

Record the Batch 4 result, but do not treat it as final closeout anymore; reserve the broader `ctest --test-dir build --output-on-failure` slice and final findings note for the end of the last scheduled batch.

---

### Task 5: Batch 5 - MTProto bootstrap and client-hello audit

**Files:**
- Inspect: `td/mtproto/BrowserProfile.cpp`
- Inspect: `td/mtproto/BrowserProfile.h`
- Inspect: `td/mtproto/ClientHelloExecutor.cpp`
- Inspect: `td/mtproto/ClientHelloOpMapper.cpp`
- Inspect: `td/mtproto/IStreamTransport.cpp`
- Inspect: `td/mtproto/SessionConnection.cpp`
- Inspect: `td/mtproto/SessionConnection.h`
- Inspect: `td/mtproto/SessionEventBounds.cpp`
- Inspect: `td/mtproto/SessionEventBounds.h`
- Inspect: `td/mtproto/TlsInit.cpp`

- [ ] **Step 1: Audit the browser profile inputs**

Read `td/mtproto/BrowserProfile.cpp` and `td/mtproto/BrowserProfile.h` and confirm the profile surface stays deterministic, immutable at handshake time, and free of transport-layer side effects.

- [ ] **Step 2: Audit client-hello execution**

Read `td/mtproto/ClientHelloExecutor.cpp` and `td/mtproto/ClientHelloOpMapper.cpp` and confirm the operation mapping remains a pure translation layer with no hidden fallback or state drift.

- [ ] **Step 3: Audit the stream transport boundary**

Read `td/mtproto/IStreamTransport.cpp`, `td/mtproto/SessionConnection.cpp`, and `td/mtproto/SessionConnection.h` and confirm connection setup, teardown, and state transitions remain fail-closed.

- [ ] **Step 4: Audit event bounds and init gates**

Read `td/mtproto/SessionEventBounds.cpp`, `td/mtproto/SessionEventBounds.h`, and `td/mtproto/TlsInit.cpp` and confirm event timing and TLS initialization do not accept out-of-range or partially initialized inputs.

- [ ] **Step 5: Verify bootstrap-facing tests**

Read `test/stealth/test_stream_transport_activation_fail_closed.cpp`, `test/stealth/test_tls_init_hello_generation_fail_closed.cpp`, `test/stealth/test_tls_init_profile_runtime.cpp`, `test/stealth/test_tls_runtime_builder_source_contract.cpp`, `test/stealth/test_tls_runtime_builder_equivalence.cpp`, and `test/stealth/test_tls_reader_byte_flow_adversarial.cpp` and confirm they cover the bootstrap path with positive, negative, adversarial, and regression checks.

- [ ] **Step 6: Check the blast radius before edits**

Use `codebase_search` and `codebase_impact` on any touched symbol before changing code, especially if profile selection or transport bootstrap behavior diverges from the tests.

- [ ] **Step 7: Prove any defect before fixing it**

If a bootstrap invariant is broken, write the smallest red test in a separate file first and keep the failure reproducible before touching production code.

- [ ] **Step 8: Run the targeted slice**

Run:
```bash
./build/test/run_all_tests --filter 'StreamTransportActivationFailClosed|TlsInitHelloGenerationFailClosed|TlsInitProfileRuntime|TlsRuntimeBuilder|TlsReaderByteFlow'
```
Expected: green, or one reproducible failure that maps to a single bootstrap invariant.

- [ ] **Step 9: Keep the fix minimal**

If the slice fails for a real reason, apply the smallest code change that satisfies the failing red test and rerun the same filter until green.

- [ ] **Step 10: Record the result**

Capture the outcome with exact file and line references so the next batch can inherit the verified boundary.

---

### Task 6: Batch 6 - Stealth parameter loading and DRS audit

**Files:**
- Inspect: `td/mtproto/TlsInit.h`
- Inspect: `td/mtproto/TlsReaderByteFlow.cpp`
- Inspect: `td/mtproto/TransportType.h`
- Inspect: `td/mtproto/stealth/DrsEngine.cpp`
- Inspect: `td/mtproto/stealth/SecureRngBounded.h`
- Inspect: `td/mtproto/stealth/StealthConfig.cpp`
- Inspect: `td/mtproto/stealth/StealthConfig.h`
- Inspect: `td/mtproto/stealth/StealthParamsLoader.cpp`
- Inspect: `td/mtproto/stealth/StealthParamsLoader.h`
- Inspect: `td/mtproto/stealth/StealthRuntimeParams.cpp`

- [ ] **Step 1: Audit the TLS-init header contract**

Read `td/mtproto/TlsInit.h` and `td/mtproto/TransportType.h` and confirm the transport API stays explicit, narrow, and closed over unsupported modes.

- [ ] **Step 2: Audit the reader byte flow**

Read `td/mtproto/TlsReaderByteFlow.cpp` and confirm parser state, length handling, and byte consumption do not permit ambiguous reads or silent truncation.

- [ ] **Step 3: Audit the DRS engine and RNG floor**

Read `td/mtproto/stealth/DrsEngine.cpp` and `td/mtproto/stealth/SecureRngBounded.h` and confirm record shaping remains bounded, deterministic under test seeds, and fail-closed on invalid ranges.

- [ ] **Step 4: Audit stealth config and loader behavior**

Read `td/mtproto/stealth/StealthConfig.cpp`, `td/mtproto/stealth/StealthConfig.h`, `td/mtproto/stealth/StealthParamsLoader.cpp`, and `td/mtproto/stealth/StealthParamsLoader.h` and confirm reload paths do not accept partially valid configuration.

- [ ] **Step 5: Audit runtime params**

Read `td/mtproto/stealth/StealthRuntimeParams.cpp` and confirm runtime parameter materialization preserves the validated loader invariants.

- [ ] **Step 6: Verify the DRS and loader tests**

Read `test/stealth/test_decorator_drs.cpp`, `test/stealth/test_drs_engine.cpp`, `test/stealth/test_stealth_params_loader.cpp`, `test/stealth/test_stealth_params_loader_runtime.cpp`, `test/stealth/test_stealth_params_loader_security.cpp`, `test/stealth/test_stealth_params_loader_reload_log_contract.cpp`, `test/stealth/test_stealth_runtime_defaults_contract.cpp`, and `test/stealth/test_stealth_logging_source_contract.cpp` and confirm they cover positive, negative, adversarial, and configuration-boundary cases.

- [ ] **Step 7: Cross-check the blast radius before edits**

Use `codebase_search` and `codebase_impact` if any DRS or config invariant appears unsafe.

- [ ] **Step 8: Run the targeted slice**

Run:
```bash
./build/test/run_all_tests --filter 'DecoratorDRS|DrsEngine|StealthParamsLoader|StealthRuntimeDefaults|StealthLogging|TlsRuntimeProfilePolicyFailClosed'
```
Expected: green, or one reproducible failure that isolates to config/DRS semantics.

- [ ] **Step 9: Escalate real defects with red-first tests**

If the slice exposes a real defect, add a separate failing test first, then apply the minimum fix, then rerun the same slice until green.

- [ ] **Step 10: Capture the outcome**

Record the result with exact files and test names so the next stealth batch inherits a verified baseline.

---

### Task 7: Batch 7 - Stealth transport and profile registry audit

**Files:**
- Inspect: `td/mtproto/stealth/StealthRuntimeParams.h`
- Inspect: `td/mtproto/stealth/StealthTransportDecorator.cpp`
- Inspect: `td/mtproto/stealth/StealthTransportDecorator.h`
- Inspect: `td/mtproto/stealth/TlsHelloBuilder.cpp`
- Inspect: `td/mtproto/stealth/TlsHelloBuilder.h`
- Inspect: `td/mtproto/stealth/TlsHelloProfileRegistry.cpp`
- Inspect: `td/mtproto/stealth/TlsHelloProfileRegistry.h`
- Inspect: `td/telegram/AccountManager.cpp`
- Inspect: `td/telegram/AccountManager.h`
- Inspect: `td/telegram/AnimationsManager.cpp`

- [ ] **Step 1: Audit the runtime-params boundary**

Read `td/mtproto/stealth/StealthRuntimeParams.h` and confirm the runtime view remains a narrow projection of validated loader output.

- [ ] **Step 2: Audit the transport decorator**

Read `td/mtproto/stealth/StealthTransportDecorator.cpp` and `td/mtproto/stealth/StealthTransportDecorator.h` and confirm the decorator does not leak policy decisions back into the core transport layer.

- [ ] **Step 3: Audit hello-building semantics**

Read `td/mtproto/stealth/TlsHelloBuilder.cpp` and `td/mtproto/stealth/TlsHelloBuilder.h` and confirm handshake construction remains deterministic under the selected profile inputs.

- [ ] **Step 4: Audit profile registry behavior**

Read `td/mtproto/stealth/TlsHelloProfileRegistry.cpp` and `td/mtproto/stealth/TlsHelloProfileRegistry.h` and confirm profile lookup, weighting, and fallback behavior stay explicit and fail-closed.

- [ ] **Step 5: Audit the adjacent Telegram managers**

Read `td/telegram/AccountManager.cpp`, `td/telegram/AccountManager.h`, and `td/telegram/AnimationsManager.cpp` and confirm the backport did not broaden their public surface or relax ownership and error handling.

- [ ] **Step 6: Verify the profile-selection tests**

Read `test/stealth/test_tls_profile_registry.cpp`, `test/stealth/test_profile_selection_weight_adversarial.cpp`, `test/stealth/test_tls_profile_selection_per_install_entropy.cpp`, `test/stealth/test_tls_profile_firefox_weight_independence.cpp`, `test/stealth/test_tls_mobile_release_grade_lane.cpp`, and `test/stealth/test_tls_multi_dump_windows_chrome_stats.cpp` and confirm they cover the profile registry and selection rules from multiple angles.

- [ ] **Step 7: Verify the fixture-alignment tests**

Read `test/stealth/test_tls_multi_dump_android_firefox_baseline.cpp`, `test/stealth/test_tls_runtime_real_fixture_alignment.cpp`, `test/stealth/test_tls_runtime_selection_source_contract.cpp`, and `test/stealth/test_tls_runtime_serverhello_fixture_contract.cpp` and confirm the selected profiles remain tied to reviewed real fixtures.

- [ ] **Step 8: Run the targeted slice**

Run:
```bash
./build/test/run_all_tests --filter 'TlsProfileRegistry|ProfileSelectionWeight|TlsProfileSelectionPerInstallEntropy|TlsProfileFirefoxWeightIndependence|TlsMobileReleaseGradeLane|TlsRuntimeRealFixtureAlignment'
```
Expected: green, or one reproducible failure that maps to a single profile-registry invariant.

- [ ] **Step 9: Keep fixes red-first**

If a real defect appears, add a separate red test first, fix the code minimally, and rerun the same filter until green.

- [ ] **Step 10: Capture the result**

Record the batch outcome with exact file and line references before moving to the remaining Telegram surfaces.

---

### Task 8: Batch 8 - Telegram business and auth audit

**Files:**
- Inspect: `td/telegram/AuthManager.cpp`
- Inspect: `td/telegram/AuthManager.h`
- Inspect: `td/telegram/BotAccessSettings.cpp`
- Inspect: `td/telegram/BotInfoManager.cpp`
- Inspect: `td/telegram/BusinessConnectedBot.cpp`
- Inspect: `td/telegram/BusinessConnectedBot.h`
- Inspect: `td/telegram/BusinessConnectedBot.hpp`
- Inspect: `td/telegram/BusinessConnectionManager.cpp`
- Inspect: `td/telegram/BusinessConnectionManager.h`
- Inspect: `td/telegram/BusinessManager.cpp`

- [ ] **Step 1: Audit auth-manager boundaries**

Read `td/telegram/AuthManager.cpp` and `td/telegram/AuthManager.h` and confirm the backport did not weaken session handling, authorization state, or fail-closed auth behavior.

- [ ] **Step 2: Audit bot access settings**

Read `td/telegram/BotAccessSettings.cpp` and `td/telegram/BotInfoManager.cpp` and confirm the access-control surface still denies by default and only promotes explicitly validated bot state.

- [ ] **Step 3: Audit business-connected bot wiring**

Read `td/telegram/BusinessConnectedBot.cpp`, `td/telegram/BusinessConnectedBot.h`, `td/telegram/BusinessConnectedBot.hpp`, `td/telegram/BusinessConnectionManager.cpp`, `td/telegram/BusinessConnectionManager.h`, and `td/telegram/BusinessManager.cpp` and confirm the new business paths stay layered and do not leak implementation details across boundaries.

- [ ] **Step 4: Check the adjacent chat and dialog ramifications**

Confirm no new business/auth path changes invalidate the existing dialog and chat invariants already audited in Tasks 1-4.

- [ ] **Step 5: Verify the auth and access tests**

Read `test/phase1_bot_access_settings_failclosed_contract.cpp`, `test/phase1_restricted_rights_failclosed_contract.cpp`, `test/phase2_chatjoin_guardbot_contract.cpp`, `test/phase2_instant_view_schema_contract.cpp`, `test/phase2_poll_media_failsafe_contract.cpp`, `test/phase2_search_type_filter_contract.cpp`, `test/phase2_web_token_auth_failclosed_contract.cpp`, and `test/phase2_webbrowser_failclosed_contract.cpp` and confirm they pin the deny-by-default and input-boundary behavior.

- [ ] **Step 6: Verify the business-related regression tests**

Read `test/managed_bot_created_dependency_contract.cpp`, `test/managed_bot_created_dependency_adversarial.cpp`, `test/managed_bot_created_dependency_integration.cpp`, `test/managed_bot_created_dependency_runtime.cpp`, `test/phase1_bot_access_settings_failclosed_contract.cpp`, `test/phase1_restricted_rights_failclosed_contract.cpp`, `test/phase2_chatjoin_guardbot_contract.cpp`, `test/phase2_instant_view_schema_contract.cpp`, `test/phase2_poll_media_failsafe_contract.cpp`, and `test/phase2_web_token_auth_failclosed_contract.cpp` and confirm they cover positive, negative, adversarial, integration, and load paths.

- [ ] **Step 7: Cross-check the blast radius before edits**

Use `codebase_search` and `codebase_impact` on any auth or business symbol before changing code.

- [ ] **Step 8: Run the targeted slice**

Run:
```bash
./build/test/run_all_tests --filter 'ManagedBotCreatedDependency|BotAccessSettings|RestrictedRights|ChatjoinGuardbot|InstantViewSchema|PollMediaFailsafe|WebTokenAuthFailclosed'
```
Expected: green, or one reproducible failure tied to a single access-control invariant.

- [ ] **Step 9: Keep red-first discipline**

If a real defect is found, add the separate failing test first, then apply the minimal fix, then rerun the same slice until green.

- [ ] **Step 10: Close this wave**

Write the batch result with exact references and separate verified defects from accepted design choices.

---

### Continuation Protocol: Batches 9-44

- [ ] **Step 1: Audit the listed deltas**

Read the listed files and confirm the delta stays aligned with the repo's architecture, C++ rules, and OWASP fail-closed expectations.

- [ ] **Step 2: Cross-check nearby contracts and callers**

Use `rg`, `codebase_search`, and `codebase_impact` as needed to find affected tests, schemas, call sites, or documentation consumers before deciding whether the delta is safe.

- [ ] **Step 3: Prove real defects red-first**

If any listed delta is unsafe, write or extend a separate failing test or contract check first, then make the minimal fix instead of relaxing the audit criteria.

- [ ] **Step 4: Run the narrowest valid verification**

For code and test batches, run the narrowest matching `./build/test/run_all_tests --filter ...` or `ctest` slice; for docs, metadata, and plan batches, verify link targets, naming, and cross-file consistency directly.

- [ ] **Step 5: Record the outcome**

Capture whether the batch is green, blocked, or needs follow-up, with exact file references and any newly added red-first tests.

---

### Task 9: Batch 9 - Repo Metadata And Workflow Audit

**Files:**
- Inspect: `.agents/skills/beads/SKILL.md`
- Inspect: `.agents/skills/beads/agents/openai.yaml`
- Inspect: `.beads/.gitignore`
- Inspect: `.beads/README.md`
- Inspect: `.beads/config.yaml`
- Inspect: `.beads/hooks/applypatch-msg`
- Inspect: `.beads/hooks/commit-msg`
- Inspect: `.beads/hooks/post-applypatch`
- Inspect: `.beads/hooks/post-checkout`
- Inspect: `.beads/hooks/post-commit`

Apply the continuation audit protocol above to this batch.

### Task 10: Batch 10 - Repo Metadata And Workflow Audit

**Files:**
- Inspect: `.beads/hooks/post-merge`
- Inspect: `.beads/hooks/post-receive`
- Inspect: `.beads/hooks/post-rewrite`
- Inspect: `.beads/hooks/post-update`
- Inspect: `.beads/hooks/pre-applypatch`
- Inspect: `.beads/hooks/pre-auto-gc`
- Inspect: `.beads/hooks/pre-commit`
- Inspect: `.beads/hooks/pre-push`
- Inspect: `.beads/hooks/pre-rebase`
- Inspect: `.beads/hooks/pre-receive`

Apply the continuation audit protocol above to this batch.

### Task 11: Batch 11 - Repo Metadata And Workflow Audit

**Files:**
- Inspect: `.beads/hooks/prepare-commit-msg`
- Inspect: `.beads/hooks/push-to-checkout`
- Inspect: `.beads/hooks/sendemail-validate`
- Inspect: `.beads/hooks/update`
- Inspect: `.beads/interactions.jsonl`
- Inspect: `.beads/issues.jsonl`
- Inspect: `.beads/metadata.json`
- Inspect: `.claude/settings.json`
- Inspect: `.gitignore`
- Inspect: `CHANGELOG.md`

Apply the continuation audit protocol above to this batch.

### Task 12: Batch 12 - Documentation And Audit Artifact Sweep

**Files:**
- Inspect: `CLAUDE.md`
- Inspect: `CMake/iOS.cmake`
- Inspect: `CMakeLists.txt`
- Inspect: `PR22_ADAPTIVE_RUNTIME_PROFILE_ROTATION_REVIEW_2026-06-13.md`
- Inspect: `SplitSource.php`
- Inspect: `artifacts/active_probing_nightly_observations.json`
- Inspect: `docs/Documentation/CUSTOM_CLIENT_INTEGRATION_GUIDE.md`
- Inspect: `docs/Documentation/FINGERPRINT_GENERATION_PIPELINE.md`
- Inspect: `docs/Documentation/Lessons_Learnt.md`
- Inspect: `docs/Generated/FINGERPRINT_TRANSPORT_COHERENCE_STATUS.generated.json`

Apply the continuation audit protocol above to this batch.

### Task 13: Batch 13 - Documentation And Audit Artifact Sweep

**Files:**
- Inspect: `docs/Plans/ADAPTIVE_RUNTIME_PROFILE_ROTATION_AUDIT_FINDINGS_2026-06-12.md`
- Inspect: `docs/Plans/ADAPTIVE_RUNTIME_PROFILE_ROTATION_DEFERRED_FIXES_PLAN_2026-06-12.md`
- Inspect: `docs/Plans/ADAPTIVE_RUNTIME_PROFILE_ROTATION_PLAN_2026-06-12.md`
- Inspect: `docs/Plans/Archived/ANDROID_CHROMIUM_RUNTIME_PROFILE_PROMOTION_PLAN_2026-06-12.md`
- Inspect: `docs/Plans/Archived/PR21_MOBILE_RELEASE_GRADE_ARCHITECTURAL_ISSUE_2026-06-12.md`
- Inspect: `docs/Plans/Archived/PR21_STEALTH_CORPUS_SIMILARITY_REVIEW_2026-06-11.md`
- Inspect: `docs/Plans/Archived/PR21_STEALTH_CORPUS_SIMILARITY_REVIEW_2026-06-12_CODEX.md`
- Inspect: `docs/Plans/Archived/RUNTIME_PROFILE_GAP_AUDIT_2026-06-12.md`
- Inspect: `docs/Plans/PR21_REVIEW_RESPONSE_2026-06-11.md`
- Inspect: `docs/Plans/UPSTREAM_BACKPORT_HARDEST_DEFERRED.md`

Apply the continuation audit protocol above to this batch.

### Task 14: Batch 14 - Documentation And Audit Artifact Sweep

**Files:**
- Inspect: `docs/Plans/UPSTREAM_BACKPORT_PHASE1_CLOSEOUT_2026-06-16.md`
- Inspect: `docs/Plans/UPSTREAM_BACKPORT_PHASE2_CLOSEOUT_2026-06-18.md`
- Inspect: `docs/Plans/UPSTREAM_BACKPORT_PHASE2_DEFERRED_2026-06-16.txt`
- Inspect: `docs/Plans/UPSTREAM_BACKPORT_PHASE2_RESUME_2026-06-16.txt`
- Inspect: `docs/Plans/UPSTREAM_BACKPORT_PLAN_2026-06-15.md`
- Inspect: `docs/Plans/UPSTREAM_BACKPORT_PLAN_2026-06-15_WAVE_A_ADDENDUM.md`
- Inspect: `docs/Plans/UPSTREAM_BACKPORT_PLAN_2026-06-15_WAVE_B_CLOSEOUT.md`
- Inspect: `docs/Plans/UPSTREAM_BACKPORT_PR23_AUDIT_PLAN_2026-06-21.md`
- Inspect: `docs/Plans/fingerprint-hardening-master-plan-2026-05-24/handoffs/stealth_corpus_similarity_claude_2026-06-08.json`
- Inspect: `td/generate/scheme/td_api.tl`

Apply the continuation audit protocol above to this batch.

### Task 15: Batch 15 - Telegram Runtime Sweep

**Files:**
- Inspect: `td/generate/scheme/telegram_api.tl`
- Inspect: `td/telegram/BusinessManager.h`
- Inspect: `td/telegram/BusinessRecipients.cpp`
- Inspect: `td/telegram/ChatManager.cpp`
- Inspect: `td/telegram/ChatManager.h`
- Inspect: `td/telegram/ConfigManager.cpp`
- Inspect: `td/telegram/ConfigManager.h`
- Inspect: `td/telegram/CountryInfoManager.cpp`
- Inspect: `td/telegram/CountryInfoManager.h`
- Inspect: `td/telegram/DialogAction.cpp`

Apply the continuation audit protocol above to this batch.

### Task 16: Batch 16 - Telegram Runtime Sweep

**Files:**
- Inspect: `td/telegram/DialogActionManager.cpp`
- Inspect: `td/telegram/DialogEventLog.cpp`
- Inspect: `td/telegram/DialogInviteLink.cpp`
- Inspect: `td/telegram/DialogInviteLinkManager.cpp`
- Inspect: `td/telegram/DialogInviteLinkManager.h`
- Inspect: `td/telegram/DialogManager.cpp`
- Inspect: `td/telegram/DialogManager.h`
- Inspect: `td/telegram/DialogParticipant.cpp`
- Inspect: `td/telegram/DialogParticipant.h`
- Inspect: `td/telegram/DialogParticipantManager.cpp`

Apply the continuation audit protocol above to this batch.

### Task 17: Batch 17 - Telegram Runtime Sweep

**Files:**
- Inspect: `td/telegram/DialogParticipantManager.h`
- Inspect: `td/telegram/DialogPhoto.cpp`
- Inspect: `td/telegram/DialogPhoto.h`
- Inspect: `td/telegram/Document.cpp`
- Inspect: `td/telegram/Document.h`
- Inspect: `td/telegram/DraftMessage.cpp`
- Inspect: `td/telegram/DraftMessage.h`
- Inspect: `td/telegram/DraftMessage.hpp`
- Inspect: `td/telegram/FileReferenceManager.cpp`
- Inspect: `td/telegram/FormattedDate.cpp`

Apply the continuation audit protocol above to this batch.

### Task 18: Batch 18 - Telegram Runtime Sweep

**Files:**
- Inspect: `td/telegram/FormattedDate.h`
- Inspect: `td/telegram/FormattedDate.hpp`
- Inspect: `td/telegram/Global.h`
- Inspect: `td/telegram/GroupCallManager.cpp`
- Inspect: `td/telegram/InlineMessageManager.cpp`
- Inspect: `td/telegram/InlineMessageManager.h`
- Inspect: `td/telegram/InlineQueriesManager.cpp`
- Inspect: `td/telegram/InlineQueriesManager.h`
- Inspect: `td/telegram/InputMessageText.cpp`
- Inspect: `td/telegram/InputMessageText.h`

Apply the continuation audit protocol above to this batch.

### Task 19: Batch 19 - Telegram Runtime Sweep

**Files:**
- Inspect: `td/telegram/JoinChatBotResult.cpp`
- Inspect: `td/telegram/JoinChatBotResult.h`
- Inspect: `td/telegram/LinkManager.cpp`
- Inspect: `td/telegram/LinkManager.h`
- Inspect: `td/telegram/Location.cpp`
- Inspect: `td/telegram/Location.h`
- Inspect: `td/telegram/MessageContent.h`
- Inspect: `td/telegram/MessageCover.cpp`
- Inspect: `td/telegram/MessageEntity.cpp`
- Inspect: `td/telegram/MessageEntity.h`

Apply the continuation audit protocol above to this batch.

### Task 20: Batch 20 - Telegram Runtime Sweep

**Files:**
- Inspect: `td/telegram/MessageEntity.hpp`
- Inspect: `td/telegram/MessageExtendedMedia.cpp`
- Inspect: `td/telegram/MessageQueryManager.cpp`
- Inspect: `td/telegram/MessageReplyInfo.cpp`
- Inspect: `td/telegram/MessageSender.cpp`
- Inspect: `td/telegram/MessageSender.h`
- Inspect: `td/telegram/MessagesManager.h`
- Inspect: `td/telegram/MissingInvitee.cpp`
- Inspect: `td/telegram/NotificationManager.cpp`
- Inspect: `td/telegram/NotificationType.cpp`

Apply the continuation audit protocol above to this batch.

### Task 21: Batch 21 - Telegram Runtime Sweep

**Files:**
- Inspect: `td/telegram/OptionManager.cpp`
- Inspect: `td/telegram/Payments.cpp`
- Inspect: `td/telegram/PhotoSize.cpp`
- Inspect: `td/telegram/PhotoSize.h`
- Inspect: `td/telegram/PollManager.cpp`
- Inspect: `td/telegram/PollOption.cpp`
- Inspect: `td/telegram/Premium.cpp`
- Inspect: `td/telegram/QuickReplyManager.cpp`
- Inspect: `td/telegram/Requests.cpp`
- Inspect: `td/telegram/Requests.h`

Apply the continuation audit protocol above to this batch.

### Task 22: Batch 22 - Telegram Runtime Sweep

**Files:**
- Inspect: `td/telegram/SavedMessagesManager.cpp`
- Inspect: `td/telegram/StickersManager.cpp`
- Inspect: `td/telegram/StoryAlbum.cpp`
- Inspect: `td/telegram/StoryContent.cpp`
- Inspect: `td/telegram/StoryManager.cpp`
- Inspect: `td/telegram/SuggestedActionManager.cpp`
- Inspect: `td/telegram/Td.cpp`
- Inspect: `td/telegram/Td.h`
- Inspect: `td/telegram/ToDoItem.cpp`
- Inspect: `td/telegram/ToDoItem.h`

Apply the continuation audit protocol above to this batch.

### Task 23: Batch 23 - Telegram Runtime Sweep

**Files:**
- Inspect: `td/telegram/ToDoItem.hpp`
- Inspect: `td/telegram/ToDoList.cpp`
- Inspect: `td/telegram/ToDoList.h`
- Inspect: `td/telegram/TopDialogManager.cpp`
- Inspect: `td/telegram/TranslationManager.cpp`
- Inspect: `td/telegram/UpdatesManager.cpp`
- Inspect: `td/telegram/UpdatesManager.h`
- Inspect: `td/telegram/UserId.cpp`
- Inspect: `td/telegram/UserId.h`
- Inspect: `td/telegram/UserManager.cpp`

Apply the continuation audit protocol above to this batch.

### Task 24: Batch 24 - Telegram Runtime Sweep

**Files:**
- Inspect: `td/telegram/UserManager.h`
- Inspect: `td/telegram/UserPrivacySettingRule.cpp`
- Inspect: `td/telegram/Version.h`
- Inspect: `td/telegram/WebAppManager.cpp`
- Inspect: `td/telegram/WebAppManager.h`
- Inspect: `td/telegram/WebBrowserManager.cpp`
- Inspect: `td/telegram/WebBrowserManager.h`
- Inspect: `td/telegram/WebBrowserSettings.cpp`
- Inspect: `td/telegram/WebBrowserSettings.h`
- Inspect: `td/telegram/WebBrowserSettings.hpp`

Apply the continuation audit protocol above to this batch.

### Task 25: Batch 25 - Telegram Runtime Sweep

**Files:**
- Inspect: `td/telegram/WebDomainException.cpp`
- Inspect: `td/telegram/WebDomainException.h`
- Inspect: `td/telegram/WebDomainException.hpp`
- Inspect: `td/telegram/WebPageBlock.cpp`
- Inspect: `td/telegram/WebPageBlock.h`
- Inspect: `td/telegram/WebPagesManager.cpp`
- Inspect: `td/telegram/cli.cpp`
- Inspect: `td/telegram/files/FileUploadId.cpp`
- Inspect: `td/telegram/files/FileUploadId.h`
- Inspect: `td/telegram/misc.cpp`

Apply the continuation audit protocol above to this batch.

### Task 26: Batch 26 - Telegram Runtime Sweep

**Files:**
- Inspect: `td/telegram/misc.h`
- Inspect: `td/telegram/net/ConnectionCreator.cpp`
- Inspect: `td/telegram/net/ConnectionCreator.h`
- Inspect: `td/telegram/net/ProxyChecker.cpp`
- Inspect: `td/telegram/net/ProxyChecker.h`
- Inspect: `test/analysis/build_family_lane_baselines.py`
- Inspect: `test/analysis/fixtures/clienthello/windows/brave1_89_132_windows10_21h1_00fe6ad2.clienthello.json`
- Inspect: `test/analysis/fixtures/clienthello/windows/chrome109_0_5414_120_windows7_pro_6_1_7601_356eca95.clienthello.json`
- Inspect: `test/analysis/fixtures/clienthello/windows/chrome109_0_54_windowsserver_2008_r2_standart_6_1_7601_5e7b5bf6.clienthello.json`
- Inspect: `test/analysis/fixtures/clienthello/windows/chrome109_0_54_windowsserver_2012_r2_standart_6_3_9600_e30794b5.clienthello.json`

Apply the continuation audit protocol above to this batch.

### Task 27: Batch 27 - Regression Test Suite Sweep

**Files:**
- Inspect: `test/analysis/fixtures/clienthello/windows/chrome146_0_7680_178_windows10_pro_22h2_19045_6456_359a8977.clienthello.json`
- Inspect: `test/analysis/fixtures/clienthello/windows/chrome146_0_7680_178_windows11_version_25h2_build_26200_8117_c2b58729.clienthello.json`
- Inspect: `test/analysis/fixtures/clienthello/windows/chrome146_0_7680_17_windows10_0_version_21h2_19044_7058_16d3ed6d.clienthello.json`
- Inspect: `test/analysis/fixtures/clienthello/windows/chrome146_0_7680_180_windows10_pro_22h2_19045_5608_bee16c61.clienthello.json`
- Inspect: `test/analysis/fixtures/clienthello/windows/chrome146_windowsserver_2019_standart_1809_17763_1457_b77c034d.clienthello.json`
- Inspect: `test/analysis/fixtures/clienthello/windows/chrome147_0_7727_55_windows10_0_8ce44de0.clienthello.json`
- Inspect: `test/analysis/fixtures/clienthello/windows/chrome147_0_7727_55_windows10_22h2_19045_7058_b9b21355.clienthello.json`
- Inspect: `test/analysis/fixtures/clienthello/windows/chrome147_0_7727_56_windows10_pro_22h2_19045_6456_837d9210.clienthello.json`
- Inspect: `test/analysis/fixtures/clienthello/windows/chromium142_windows11_23h2_2c4b2b22.clienthello.json`
- Inspect: `test/analysis/fixtures/clienthello/windows/chromiummaxthon_7_5_2_5400_chromium140_0_windows10_pro_22h2_19045_4780_456fea45.clienthello.json`

Apply the continuation audit protocol above to this batch.

### Task 28: Batch 28 - Regression Test Suite Sweep

**Files:**
- Inspect: `test/analysis/fixtures/clienthello/windows/edge146_0_0_0_windows10_0_4b75427d.clienthello.json`
- Inspect: `test/analysis/fixtures/clienthello/windows/edge146_0_3856_109_windows10_pro_22h2_19045_6456_866968a7.clienthello.json`
- Inspect: `test/analysis/fixtures/clienthello/windows/edge146_0_3856_109_windows11_version_25h2_build_26200_8037_53259acb.clienthello.json`
- Inspect: `test/analysis/fixtures/clienthello/windows/edge146_0_3856_10_windowsserver_2022_standard_21h2_20348_4648_15cdcd65.clienthello.json`
- Inspect: `test/analysis/fixtures/clienthello/windows/firefox115_0_windows6_1_ab9badf0.clienthello.json`
- Inspect: `test/analysis/fixtures/clienthello/windows/firefox115_9_1esr_windowsserver_2012_r2_standart_6_3_9600_7de8be3c.clienthello.json`
- Inspect: `test/analysis/fixtures/clienthello/windows/firefox149_0_2_windows10_pro_22h2_19045_6456_e32b3ddb.clienthello.json`
- Inspect: `test/analysis/fixtures/clienthello/windows/firefox149_0_2_windows11_0_25h2_26200_8117_082c503a.clienthello.json`
- Inspect: `test/analysis/fixtures/clienthello/windows/firefox149_0_2_windows11_0_65af0430.clienthello.json`
- Inspect: `test/analysis/fixtures/clienthello/windows/firefox149_0_2_windows11_25h2_os_build_26200_8117_e7543116.clienthello.json`

Apply the continuation audit protocol above to this batch.

### Task 29: Batch 29 - Regression Test Suite Sweep

**Files:**
- Inspect: `test/analysis/fixtures/clienthello/windows/firefox149_0_windows10_0_081f2d09.clienthello.json`
- Inspect: `test/analysis/fixtures/clienthello/windows/firefox149_0_windows11_25h2_26200_8117_cd5adfa6.clienthello.json`
- Inspect: `test/analysis/fixtures/clienthello/windows/firefox149_0_windows11_version_25h2_os_build_26200_8117_0377a59b.clienthello.json`
- Inspect: `test/analysis/fixtures/clienthello/windows/opera129_0_5823_28_windows11_25h2_26200_8037_9e0fa506.clienthello.json`
- Inspect: `test/analysis/fixtures/clienthello/windows/yandex26_3_3_862_64_bit_windows11_0_c10e8192.clienthello.json`
- Inspect: `test/analysis/fixtures/clienthello/windows/yandex26_3_3_869_64_bit_windows10_0_ef066851.clienthello.json`
- Inspect: `test/analysis/fixtures/clienthello/windows/yandex26_3_3_869_64_bit_windows10_pro_22h2_19045_6456_7762ae63.clienthello.json`
- Inspect: `test/analysis/test_corpus_iteration_tier_naming_contract.py`
- Inspect: `test/analysis/test_family_lane_oracle_generation.py`
- Inspect: `test/analysis/test_release_cohort_identity_contract.py`

Apply the continuation audit protocol above to this batch.

### Task 30: Batch 30 - Regression Test Suite Sweep

**Files:**
- Inspect: `test/analysis/test_similarity_release_gate_contract.py`
- Inspect: `test/analysis/test_sqlite_vendor_audit_adversarial.py`
- Inspect: `test/draft_local_reply_ignore_contract.cpp`
- Inspect: `test/message_content_null_guard_runtime.cpp`
- Inspect: `test/message_entities.cpp`
- Inspect: `test/phase1_bot_access_settings_failclosed_contract.cpp`
- Inspect: `test/phase1_poll_voter_visibility_contract.cpp`
- Inspect: `test/phase1_restricted_rights_failclosed_contract.cpp`
- Inspect: `test/phase2_chatjoin_guardbot_contract.cpp`
- Inspect: `test/phase2_instant_view_schema_contract.cpp`

Apply the continuation audit protocol above to this batch.

### Task 31: Batch 31 - Regression Test Suite Sweep

**Files:**
- Inspect: `test/phase2_poll_media_failsafe_contract.cpp`
- Inspect: `test/phase2_search_type_filter_contract.cpp`
- Inspect: `test/phase2_web_token_auth_failclosed_contract.cpp`
- Inspect: `test/phase2_webbrowser_failclosed_contract.cpp`
- Inspect: `test/sonar_blocker_source_contract.cpp`
- Inspect: `test/sqlite_phase3_stress.cpp`
- Inspect: `test/sqlite_phase3_stress_contract.cpp`
- Inspect: `test/stealth/FamilyLaneMatchers.cpp`
- Inspect: `test/stealth/FamilyLaneMatchers.h`
- Inspect: `test/stealth/ReviewedClientHelloFixtures.h`

Apply the continuation audit protocol above to this batch.

### Task 32: Batch 32 - Stealth Regression Suite Sweep

**Files:**
- Inspect: `test/stealth/ReviewedFamilyLaneBaselines.h`
- Inspect: `test/stealth/RuntimeProfileRotationTestSupport.h`
- Inspect: `test/stealth/RuntimeServerHelloPairingHelpers.h`
- Inspect: `test/stealth/ServerHelloFixtureLoader.h`
- Inspect: `test/stealth/SessionConnectionTestPeer.h`
- Inspect: `test/stealth/StealthParamsLoaderPlatformDriftMultiLoaderTestUtils.h`
- Inspect: `test/stealth/test_client_hello_executor_mlkem_rng_contract.cpp`
- Inspect: `test/stealth/test_connection_creator_proxy_route_source_contract.cpp`
- Inspect: `test/stealth/test_connection_creator_tls_init_source_contract.cpp`
- Inspect: `test/stealth/test_darwin_profile_hardcoding_bug.cpp`

Apply the continuation audit protocol above to this batch.

### Task 33: Batch 33 - Stealth Regression Suite Sweep

**Files:**
- Inspect: `test/stealth/test_decorator_drs.cpp`
- Inspect: `test/stealth/test_decorator_overflow_fail_closed.cpp`
- Inspect: `test/stealth/test_drs_engine.cpp`
- Inspect: `test/stealth/test_ech_circuit_breaker_reload_integration.cpp`
- Inspect: `test/stealth/test_ech_route_failure_legacy_migration_stress.cpp`
- Inspect: `test/stealth/test_ech_route_failure_lookup_budget_adversarial.cpp`
- Inspect: `test/stealth/test_ech_route_failure_parse_fail_ttl_race_adversarial.cpp`
- Inspect: `test/stealth/test_ipt_idle_sampler_adversarial.cpp`
- Inspect: `test/stealth/test_masking_ios_ech_disabled_fingerprint_adversarial.cpp`
- Inspect: `test/stealth/test_masking_profile_platform_isolation_adversarial.cpp`

Apply the continuation audit protocol above to this batch.

### Task 34: Batch 34 - Stealth Regression Suite Sweep

**Files:**
- Inspect: `test/stealth/test_post_handshake_greeting_policy_edges.cpp`
- Inspect: `test/stealth/test_pr22_spdx_header_contract.cpp`
- Inspect: `test/stealth/test_profile_selection_weight_adversarial.cpp`
- Inspect: `test/stealth/test_route_state_sink_source_contract.cpp`
- Inspect: `test/stealth/test_runtime_profile_rotation_adversarial.cpp`
- Inspect: `test/stealth/test_runtime_profile_rotation_contract.cpp`
- Inspect: `test/stealth/test_runtime_profile_rotation_edge.cpp`
- Inspect: `test/stealth/test_runtime_profile_rotation_fuzz.cpp`
- Inspect: `test/stealth/test_runtime_profile_rotation_handoff.cpp`
- Inspect: `test/stealth/test_runtime_profile_rotation_negative.cpp`

Apply the continuation audit protocol above to this batch.

### Task 35: Batch 35 - Stealth Regression Suite Sweep

**Files:**
- Inspect: `test/stealth/test_runtime_profile_rotation_positive.cpp`
- Inspect: `test/stealth/test_runtime_profile_rotation_stress.cpp`
- Inspect: `test/stealth/test_session_event_bounds_adversarial.cpp`
- Inspect: `test/stealth/test_session_event_bounds_contract.cpp`
- Inspect: `test/stealth/test_session_event_bounds_fuzz.cpp`
- Inspect: `test/stealth/test_session_service_query_cleanup_contract.cpp`
- Inspect: `test/stealth/test_stealth_config_darwin_profile_source_contract.cpp`
- Inspect: `test/stealth/test_stealth_config_tls_init_profile_temporal_divergence.cpp`
- Inspect: `test/stealth/test_stealth_logging_source_contract.cpp`
- Inspect: `test/stealth/test_stealth_params_loader.cpp`

Apply the continuation audit protocol above to this batch.

### Task 36: Batch 36 - Stealth Regression Suite Sweep

**Files:**
- Inspect: `test/stealth/test_stealth_params_loader_ancestor_security_adversarial.cpp`
- Inspect: `test/stealth/test_stealth_params_loader_cooldown.cpp`
- Inspect: `test/stealth/test_stealth_params_loader_ech_downstream_integration.cpp`
- Inspect: `test/stealth/test_stealth_params_loader_filesystem_fail_closed.cpp`
- Inspect: `test/stealth/test_stealth_params_loader_plan.cpp`
- Inspect: `test/stealth/test_stealth_params_loader_platform_drift_contract.cpp`
- Inspect: `test/stealth/test_stealth_params_loader_platform_drift_integration.cpp`
- Inspect: `test/stealth/test_stealth_params_loader_platform_drift_stress.cpp`
- Inspect: `test/stealth/test_stealth_params_loader_platform_runtime.cpp`
- Inspect: `test/stealth/test_stealth_params_loader_profile_weight_bridge_adversarial.cpp`

Apply the continuation audit protocol above to this batch.

### Task 37: Batch 37 - Stealth Regression Suite Sweep

**Files:**
- Inspect: `test/stealth/test_stealth_params_loader_profile_weight_bridge_contract.cpp`
- Inspect: `test/stealth/test_stealth_params_loader_profile_weight_bridge_integration.cpp`
- Inspect: `test/stealth/test_stealth_params_loader_profile_weight_bridge_light_fuzz.cpp`
- Inspect: `test/stealth/test_stealth_params_loader_relative_ancestor_security_adversarial.cpp`
- Inspect: `test/stealth/test_stealth_params_loader_reload_log_contract.cpp`
- Inspect: `test/stealth/test_stealth_params_loader_runtime.cpp`
- Inspect: `test/stealth/test_stealth_params_loader_security.cpp`
- Inspect: `test/stealth/test_stealth_rng_bounded_source_contract.cpp`
- Inspect: `test/stealth/test_stealth_runtime_defaults_contract.cpp`
- Inspect: `test/stealth/test_stream_transport_activation_fail_closed.cpp`

Apply the continuation audit protocol above to this batch.

### Task 38: Batch 38 - Stealth Regression Suite Sweep

**Files:**
- Inspect: `test/stealth/test_stream_transport_seam.cpp`
- Inspect: `test/stealth/test_tls_corpus_alps_type_consistency_1k.cpp`
- Inspect: `test/stealth/test_tls_corpus_android_chromium_alps_1k.cpp`
- Inspect: `test/stealth/test_tls_corpus_android_chromium_no_alps_1k.cpp`
- Inspect: `test/stealth/test_tls_corpus_chrome_extension_set_1k.cpp`
- Inspect: `test/stealth/test_tls_corpus_cross_platform_contamination.cpp`
- Inspect: `test/stealth/test_tls_corpus_cross_platform_contamination_extended_1k.cpp`
- Inspect: `test/stealth/test_tls_corpus_firefox_invariance_1k.cpp`
- Inspect: `test/stealth/test_tls_corpus_firefox_macos_1k.cpp`
- Inspect: `test/stealth/test_tls_corpus_fixed_mobile_profile_invariance_1k.cpp`

Apply the continuation audit protocol above to this batch.

### Task 39: Batch 39 - Stealth Regression Suite Sweep

**Files:**
- Inspect: `test/stealth/test_tls_corpus_hmac_timestamp_adversarial_1k.cpp`
- Inspect: `test/stealth/test_tls_corpus_ios_apple_tls_1k.cpp`
- Inspect: `test/stealth/test_tls_corpus_ios_chromium_gap_1k.cpp`
- Inspect: `test/stealth/test_tls_corpus_ja3_ja4_stability_1k.cpp`
- Inspect: `test/stealth/test_tls_corpus_safari26_3_invariance_1k.cpp`
- Inspect: `test/stealth/test_tls_corpus_statistical_sampling_1k.cpp`
- Inspect: `test/stealth/test_tls_corpus_structural_key_material_stress_1k.cpp`
- Inspect: `test/stealth/test_tls_fingerprint_classifier_blackhat.cpp`
- Inspect: `test/stealth/test_tls_generator_extension_count_similarity.cpp`
- Inspect: `test/stealth/test_tls_generator_fixture_exact_fields_gate.cpp`

Apply the continuation audit protocol above to this batch.

### Task 40: Batch 40 - Stealth Regression Suite Sweep

**Files:**
- Inspect: `test/stealth/test_tls_generator_shuffle_similarity.cpp`
- Inspect: `test/stealth/test_tls_generator_wire_length_fixture_gate.cpp`
- Inspect: `test/stealth/test_tls_init_hello_generation_fail_closed.cpp`
- Inspect: `test/stealth/test_tls_init_profile_runtime.cpp`
- Inspect: `test/stealth/test_tls_init_proxy_alpn_semantics.cpp`
- Inspect: `test/stealth/test_tls_mobile_release_grade_lane.cpp`
- Inspect: `test/stealth/test_tls_multi_dump_android_chromium_alps_baseline.cpp`
- Inspect: `test/stealth/test_tls_multi_dump_android_firefox_baseline.cpp`
- Inspect: `test/stealth/test_tls_multi_dump_macos_chromium_baseline.cpp`
- Inspect: `test/stealth/test_tls_multi_dump_windows_chrome_stats.cpp`

Apply the continuation audit protocol above to this batch.

### Task 41: Batch 41 - Stealth Regression Suite Sweep

**Files:**
- Inspect: `test/stealth/test_tls_nightly_wire_baseline_monte_carlo.cpp`
- Inspect: `test/stealth/test_tls_profile_firefox_weight_independence.cpp`
- Inspect: `test/stealth/test_tls_profile_platform_coherence.cpp`
- Inspect: `test/stealth/test_tls_profile_registry.cpp`
- Inspect: `test/stealth/test_tls_profile_selection_per_install_entropy.cpp`
- Inspect: `test/stealth/test_tls_reader_byte_flow_adversarial.cpp`
- Inspect: `test/stealth/test_tls_record_padding_budget_interactions.cpp`
- Inspect: `test/stealth/test_tls_release_similarity_unavailable_fail_closed.cpp`
- Inspect: `test/stealth/test_tls_route_failure_cache.cpp`
- Inspect: `test/stealth/test_tls_runtime_builder_equivalence.cpp`

Apply the continuation audit protocol above to this batch.

### Task 42: Batch 42 - Stealth Regression Suite Sweep

**Files:**
- Inspect: `test/stealth/test_tls_runtime_builder_source_contract.cpp`
- Inspect: `test/stealth/test_tls_runtime_case_alias_fixture_blackbox.cpp`
- Inspect: `test/stealth/test_tls_runtime_params_platform_fail_closed.cpp`
- Inspect: `test/stealth/test_tls_runtime_platform_lane_stress.cpp`
- Inspect: `test/stealth/test_tls_runtime_platform_weight_gate_adversarial.cpp`
- Inspect: `test/stealth/test_tls_runtime_profile_policy_fail_closed.cpp`
- Inspect: `test/stealth/test_tls_runtime_real_fixture_alignment.cpp`
- Inspect: `test/stealth/test_tls_runtime_release_profile_gating_contract.cpp`
- Inspect: `test/stealth/test_tls_runtime_selection_source_contract.cpp`
- Inspect: `test/stealth/test_tls_runtime_serverhello_fixture_contract.cpp`

Apply the continuation audit protocol above to this batch.

### Task 43: Batch 43 - Stealth Regression Suite Sweep

**Files:**
- Inspect: `test/stealth/test_tls_runtime_serverhello_pairing_adversarial.cpp`
- Inspect: `test/stealth/test_tls_runtime_serverhello_pairing_integration.cpp`
- Inspect: `test/stealth/test_tls_runtime_serverhello_pairing_light_fuzz.cpp`
- Inspect: `test/stealth/test_tls_runtime_transport_confidence_unknown_adversarial.cpp`
- Inspect: `test/stealth/test_tls_runtime_transport_confidence_unknown_contract.cpp`
- Inspect: `test/stealth/test_tls_runtime_transport_confidence_unknown_integration.cpp`
- Inspect: `test/stealth/test_tls_runtime_transport_confidence_unknown_light_fuzz.cpp`
- Inspect: `test/stealth/test_tls_runtime_transport_confidence_unknown_stress.cpp`
- Inspect: `test/stealth/test_tls_verified_profile_serverhello_pairing_adversarial.cpp`
- Inspect: `test/stealth/test_x25519_inverse_retry_adversarial.cpp`

Apply the continuation audit protocol above to this batch.

### Task 44: Batch 44 - Tooling And Spillover Sweep

**Files:**
- Inspect: `test/call_notification_send_closure_later_runtime.cpp`
- Inspect: `test/reply_and_username_light_fuzz.cpp`
- Inspect: `test/reply_and_username_stress.cpp`
- Inspect: `test/video_alternative_properties_repair_light_fuzz.cpp`
- Inspect: `test/video_alternative_properties_repair_runtime.cpp`
- Inspect: `tools/sqlite/audit_vendor.py`

Apply the continuation audit protocol above to this batch.

---

### Final Closeout

- [ ] **Step 1: Run the branch-end verification**

When the last scheduled batch is green, run:
```bash
ctest --test-dir build --output-on-failure
```
Expected: the broader branch slice is green, or any remaining failure is isolated to one already-documented batch defect.

- [ ] **Step 2: Write the final audit findings note**

Separate verified defects, accepted design choices, and still-unverified assumptions with exact file references.

- [ ] **Step 3: Confirm full branch coverage**

Before closing the session, confirm every non-cache `feat/upstream-backport-bulk` vs `master` delta is either audited green, tracked as a proven defect, or explicitly deferred with rationale.
