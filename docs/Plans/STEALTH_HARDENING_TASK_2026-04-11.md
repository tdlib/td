<!--
SPDX-FileCopyrightText: Copyright 2026 telemt community
SPDX-License-Identifier: MIT
telemt: https://github.com/telemt
telemt: https://t.me/telemtrs
-->

# Stealth Hardening Task — Post c7f013608 Refactor

**Дата создания:** 2026-04-11
**Контекст:** результат верификации в `docs/Researches/STEALTH_VERIFICATION_REPORT_2026-04-10.md` плюс upstream-коммит `c7f013608 BrowserProfile, Builder, TL-parser refactor`
**Deadline:** 2026-04-14 (ожидаемое усиление DPI у TSPU)
**Метод работы:** **TDD only** — сначала большой комплексный test suite, потом исправление кода под него

---

## 0. Контекст

Upstream-коммит `c7f013608` исправил критический wire-level баг (Apple TLS family `Safari26_3` / `IOS14` эмитировал PQ key_share через хардкоженный `Op::pq_key_share() + Op::ml_kem_768_key()`, что не соответствует реальной Apple TLS family). Это закрыло вчерашний CRIT-1 на wire-level.

Однако рефакторинг породил две регрессии и не затронул две независимые HIGH находки из вчерашнего отчёта.

## 1. Задачи

### REG-1 — Dead `has_pq` field cleanup (косметика, blocks lint/audit)

**Файлы:**
- `td/mtproto/stealth/TlsHelloProfileRegistry.cpp:74,76`

**Проблема:**
Старая таблица `PROFILE_SPECS` всё ещё содержит `has_pq=true, pq_group_id=0x11EC` для `Safari26_3` и `IOS14`. Эти поля стали dead code — никакой runtime-генератор в `td/mtproto/` их больше не читает (новый пайплайн через `BrowserProfile.cpp` → `ClientHelloOpMapper`). Но:
- `test/stealth/test_tls_profile_wire.cpp:207-231` утверждает `spec.has_pq=true` для `Safari26_3` и проходит на dead-поле, маскируя факт расхождения.
- Любой будущий ревьюер увидит `has_pq=true` в реестре и подумает, что Apple TLS должна иметь PQ.

**Definition of Done:**
- Поля для `Safari26_3` и `IOS14` установлены в `has_pq=false, pq_group_id=0`
- Тест переписан, чтобы утверждать `has_pq=false` для Apple TLS family
- Добавлена invariant-проверка: для всех профилей значение `ProfileSpec.has_pq` совпадает с фактическим присутствием PQ в `BrowserProfileSpec.supported_groups` (cross-table consistency)

### REG-2 — Test fixtures regression after Apple TLS PQ removal (HIGH, blocks CI)

**Файлы (вероятно failing):**
- `test/stealth/test_tls_corpus_ios_apple_tls_1k.cpp:115,122-127` — `IosAppleTlsCorpus1k.KeyShareMatchesCaptureFamilyStructure`, `IosAppleTlsCorpus1k.NonGreaseSupportedGroupsExactMatchCaptureFamily`
- `test/stealth/test_tls_corpus_safari26_3_invariance_1k.cpp:69,115-120` — `Safari26_3InvarianceCorpus1k.KeyShareHasGreasePlusPqPlusX25519`, `Safari26_3InvarianceCorpus1k.NonGreaseSupportedGroupsExactMatchCaptureFamily`
- `test/stealth/test_tls_capture_safari26_3_differential.cpp:104-106` — `KeyShareLengthsMatch`
- `test/stealth/ReviewedClientHelloFixtures.h` — generated константы:
  - `chrome147_0_7727_47_ios26_4_aNonGreaseSupportedGroups`
  - `safari26_3_1_ios26_3_1_aNonGreaseSupportedGroups`

**Проблема:**
Тесты и fixture-константы написаны против **старого (некорректного)** wire-format с PQ. После рефакторинга `BrowserProfile::IOS14` и `BrowserProfile::Safari26_3` корректно эмитят только X25519 (1 не-GREASE key_share entry, без `0x11EC` в supported_groups).

**Важное замечание:** fixtures были сняты с **реальных pcap captures**. Возможны два варианта:

1. **Captures действительно содержали PQ** — тогда они отражали `iOS Chromium family` (Chrome 26.1/26.3 на iOS), а не `Apple TLS family`. Тогда тесты должны проверять разные fixtures против разных профилей.
2. **Captures не содержали PQ, но был баг в fixture extractor** — тогда нужно перегенерить fixtures из тех же pcap.

В любом случае wire-image после рефакторинга **корректнее** старого, и тесты должны align'иться с ним.

**Definition of Done:**
- Все 5+ failing тестов проходят
- Fixture-константы для Apple TLS family обновлены так, чтобы отражать реальный Apple TLS (no PQ)
- Если old fixtures содержали PQ — они переименованы в `ios_chromium_family_*` и используются только в `iOS Chromium gap` тестах, а не в `Apple TLS` тестах
- Добавлен **regression guard**: тест, который ловит обратное добавление `0x11EC` в supported_groups для Apple TLS family

### HIGH-1 — Defensive CHECK on `proxies_[active_proxy_id_]`

**Файл:**
- `td/telegram/net/ConnectionCreator.cpp:383`

**Проблема:**
```cpp
Proxy active_proxy = active_proxy_id_ == 0 ? Proxy() : proxies_[active_proxy_id_];
```
Доступ через `operator[]` без проверки `count()`. В нормальной single-threaded actor model invariant держится (если `active_proxy_id_ != 0` после init, то прокси существует в map), но E13 рекомендовал defensive guard для defense-in-depth — особенно учитывая, что эта строка переживает рефакторинги уже несколько раз.

**Definition of Done:**
- Добавлен `CHECK(active_proxy_id_ == 0 || proxies_.count(active_proxy_id_) > 0)` перед чтением
- Аналогичные паттерны (если есть в `ConnectionCreator.cpp` или соседних) тоже защищены
- Добавлены adversarial тесты, которые имитируют race/state corruption и проверяют, что CHECK срабатывает (можно через test-only API)

### HIGH-2 — Missing `docs/Samples/utls-code/`

**Файл:**
- `docs/Samples/utls-code/` — отсутствует
- `docs/Plans/tdlib-obf-stealth-plan_v6.md` — ссылается на `u_parrots.go` как ground truth

**Проблема:**
План `v6` цитирует `docs/Samples/utls-code/u_parrots.go` для `HelloChrome_120/131/133` ground truth, но физически файлов в репо нет. Значения скопированы корректно (E4 zero drift), но при будущих рефакторингах нет локального источника правды.

**Definition of Done (опция A — commit snapshot):**
- Скачать актуальный uTLS snapshot с pinned версией (commit hash)
- Положить в `docs/Samples/utls-code/` под BSD-3-Clause license header
- Обновить `tdlib-obf-stealth-plan_v6.md` с конкретным commit hash

**Definition of Done (опция B — удалить ссылки):**
- Удалить упоминания `docs/Samples/utls-code/` из плана
- Заменить на ссылку на upstream uTLS репозиторий с pinned commit

Принять решение в процессе — если лицензия BSL допускает, опция A предпочтительнее.

---

## 2. TDD Workflow Mandate

> We use TDD approach only — we create comprehensive extended test suite and only after that we fix code.

### 2.1. Принципы

1. **Tests first.** Для каждой задачи выше — сначала пишутся тесты (red), потом фиксится код (green), потом refactor.
2. **Не релаксировать red тесты.** Если тест падает, он показывает subtle bug или vulnerability. Прежде чем менять assertion, **трижды подумать**, не находит ли тест реальную проблему.
3. **Architecturally correct fix.** Перед любым изменением кода — продумать, как это правильно по архитектуре. Не патчить симптом.
4. **Deterministic > comfortable.** Детерминированная структура важнее читаемости.
5. **Test taxonomy:** для каждой проблемы покрыть как минимум:
   - **Positive** — корректные входы, expected output
   - **Negative** — некорректные входы, expected error/rejection
   - **Edge cases** — boundary values, empty/max, off-by-one
   - **Adversarial / black hat** — попытки сломать invariants, race conditions, malformed input, timing attacks
   - **Integration** — тест cross-component поведения
   - **Light fuzz** — random inputs в bounded range, no crashes
   - **Stress** — high load, repeated invocations, no memory leaks
6. **OWASP ASVS L2 alignment.** Любая security-relevant логика проверяется на:
   - V5 (Validation) — все границы валидируются fail-closed
   - V7 (Errors/Logging) — нет sensitive info в логах, нет stack traces в error messages
   - V8 (Data Protection) — constant-time compare на крипто
   - V11 (Business Logic) — нет race conditions, нет TOCTOU
14. **Все тесты в отдельных файлах.** Никаких inline тестов.

### 2.2. Black Hat Mindset

При написании тестов представь себя злоумышленником, который хочет:

- **Wire-level distinguishing:** заставить wire-image отличаться от реального браузера хоть в одном бите
- **Invariant break:** заставить runtime в состояние, где `active_proxy_id_ != 0` но прокси нет в map
- **Cross-table inconsistency:** оставить расхождение между `ProfileSpec.has_pq` и `BrowserProfileSpec.supported_groups`
- **Profile leak:** заставить Apple TLS profile эмитнуть `0x11EC` или 3 key_share entries
- **CI bypass:** написать assertion, которое проходит при пустом результате (`ASSERT_EQ(0, vec.size())` где vec должен иметь определённый размер)
- **Fixture drift:** заменить `ReviewedClientHelloFixtures.h` константу на пустую — какие тесты не упадут?
- **Time skew:** манипулировать `unix_time` параметром, чтобы вызвать different sticky bucket selection

### 2.3. Test files (новые, обязательно создаются)

| Тест | Что покрывает | Тип |
|---|---|---|
| `test/stealth/test_profile_spec_pq_consistency_invariants.cpp` | REG-1: cross-table check `ProfileSpec.has_pq` ↔ `BrowserProfileSpec.supported_groups` для всех 8 профилей | invariant + adversarial |
| `test/stealth/test_apple_tls_no_pq_wire_invariants.cpp` | REG-2 regression guard: для IOS14 + Safari26_3 нигде в wire не появляется `0x11EC`, ни в supported_groups, ни в key_share | adversarial + 1024-iteration |
| `test/stealth/test_apple_tls_key_share_count_invariants.cpp` | REG-2: для Apple TLS family в key_share ровно 1 не-GREASE entry (X25519) | invariant + 1024-iteration |
| `test/stealth/test_apple_tls_supported_groups_exact_match.cpp` | REG-2: positive — supported_groups для Apple TLS family — это `{29, 23, 24, 25}` ровно (без 4588) | positive |
| `test/stealth/test_connection_creator_active_proxy_invariant.cpp` | HIGH-1: CHECK срабатывает при попытке state corruption | adversarial + integration |
| `test/stealth/test_connection_creator_active_proxy_invariant_fuzz.cpp` | HIGH-1: light fuzz random proxy_id values, no crash | fuzz |
| `test/stealth/test_browser_profile_supported_groups_no_drift.cpp` | REG-2: для каждого профиля supported_groups совпадают между `BrowserProfileSpec.supported_groups` (top-level) и SupportedGroups extension's `u16_list` | invariant |
| `test/stealth/test_browser_profile_key_share_consistency.cpp` | REG-2 + general: для каждого профиля key_share entries согласуются с supported_groups (no key_share for unsupported group) | invariant |

### 2.4. Существующие тесты под обновление

| Тест | Изменение | Cause |
|---|---|---|
| `test/stealth/test_tls_corpus_ios_apple_tls_1k.cpp` | Удалить assertion на 3 key_share entries; обновить supported_groups expected (без 0x11EC) | REG-2 |
| `test/stealth/test_tls_corpus_safari26_3_invariance_1k.cpp` | Аналогично | REG-2 |
| `test/stealth/test_tls_capture_safari26_3_differential.cpp:104-106` | `ASSERT_EQ(2u, ...)` → `ASSERT_EQ(1u, ...)` для key_share entries | REG-2 |
| `test/stealth/test_tls_profile_wire.cpp:207-231` | `has_pq=true` → `has_pq=false` для Safari26_3 | REG-1 |
| `test/stealth/ReviewedClientHelloFixtures.h` | Обновить `chrome147_*ios26_4*SupportedGroups` и `safari26_3_*SupportedGroups` константы | REG-2 |

**Внимание:** перед тем как менять `ReviewedClientHelloFixtures.h` вручную, проверить, не генерируется ли он скриптом `merge_client_hello_fixture_summary.py`. Если генерируется — менять не header, а исходный JSON в `test/analysis/fixtures/clienthello/{ios,macos}/` и регенерировать.

---

## 3. Workflow

1. **Build baseline.** Собрать проект на `c7f013608`, зафиксировать какие тесты падают (для подтверждения REG-2).
2. **Write tests (red).** Создать новые тесты из §2.3, обновить существующие из §2.4. Прогнать — ожидаемо красные.
3. **Fix code.** Минимально-достаточные изменения в `BrowserProfile.cpp`, `TlsHelloProfileRegistry.cpp`, `ConnectionCreator.cpp`. Архитектурно сначала продумать, потом писать.
4. **Run tests (green).** Все тесты должны проходить. Если что-то ещё падает — НЕ релаксировать, пойти в §3 и думать ещё.
5. **Stress + fuzz pass.** Прогнать stress/fuzz тесты — они длинные.
6. **Commit.** Conventional commit message с указанием TDD pattern и закрываемых задач.

---

## 4. Acceptance Criteria

| # | Critère | Verifiable |
|---|---|---|
| 1 | Все 8 новых тестов из §2.3 написаны и проходят | `run_all_tests --filter <pattern>` |
| 2 | Все 5 обновлённых тестов из §2.4 проходят | `run_all_tests --filter ios\|safari\|profile_wire` |
| 3 | `ReviewedClientHelloFixtures.h` синхронизирован с реальным wire | grep `0x11EC` в Apple TLS секциях не находит |
| 4 | `ProfileSpec.has_pq` и `BrowserProfileSpec` consistency для всех 8 профилей | invariant test green |
| 5 | `ConnectionCreator.cpp:383` имеет defensive CHECK | grep + visual review |
| 6 | Adversarial fuzz тесты HIGH-1 не падают на 1000+ random inputs | fuzz test green |
| 7 | Полный `run_all_tests` зелёный | exit 0 |
| 8 | OWASP ASVS L2 чеклист пройден для затронутых модулей | manual review |

---

## 5. Out of Scope

- **S22 (L7 mismatch)** — намеренно не закрывается, scope-guarded
- **PR-S0/S1 функциональные пробелы** (BucketEliminationNoBucketPeaksIn10000Packets и т.д.) — отложено после 14-го
- **Test name traceability gap** — отложено после 14-го
- **scrapy-impersonate corroboration** — отложено
- **PR-S5 chaff edge cases** — отложено
- **PR-S8 chunk-boundary rotation** — отложено

---

*Создано на базе `STEALTH_VERIFICATION_REPORT_2026-04-10.md` после анализа upstream-коммита `c7f013608`.*
