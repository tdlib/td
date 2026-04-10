<!--
SPDX-FileCopyrightText: Copyright 2026 telemt community
SPDX-License-Identifier: MIT
telemt: https://github.com/telemt
telemt: https://t.me/telemtrs
-->

# tdlib-obf Stealth Plan — Полностью самостоятельный документ

**Дата:** 2026-04-03  
**Репозиторий:** `tdlib-obf` (форк TDLib, директория `td/mtproto/`)  
**Компаньон:** telemt (Rust MTProxy сервер, `IMPLEMENTATION_PLAN.md`)  
**Статус угрозы:** ТСПУ бюджет ₽84 млрд, ML-классификация TLS-фингерпринтов активна.  
**Цель:** максимально снизить детектируемость и стоимость детекта для DPI, без обещания «абсолютной неотличимости» (100% undetectable недостижимо в реальных сетях).  
**Главное правило:** вся маскировка активна **строго только** при
`ProxySecret::emulate_tls() == true` (секрет с префиксом `ee`).

### Аудит V7: Критические исправления (2026-04-05, scrapy-impersonate + uTLS deep-dive)

> V7 добавляет исправления на основе детального аудита против `docs/Samples/scrapy-impersonate/`, `docs/Samples/utls-code/u_parrots.go` (Chrome 120/131/133), `docs/Standards/rfc7685.txt` и реального кода в `td/mtproto/stealth/TlsHelloBuilder.cpp`.
>
> **S6/S24 (КРИТИЧЕСКОЕ — ИСПРАВЛЕНИЕ V7):** `ShuffleChromeTLSExtensions` в bundled uTLS — это **полный случайный shuffle** всех расширений, кроме трёх фиксированных якорей: `UtlsGREASEExtension`, `UtlsPaddingExtension` и `PreSharedKeyExtension`. Это поведение Chrome с версии 106 (Chrome 106+). Описание «fixture-bounded order windows» является НЕВЕРНЫМ для Chrome: правильный термин — **ChromeShuffleAnchored** (полный shuffle с якорями). Предыдущая реализация `shuffle_fixture_bounded_windows` (окна по 4 элемента) была **менее случайна и менее реалистична**, чем Chrome. На текущей ветке это уже исправлено: builder использует anchored shuffle, а тесты `TlsExtensionOrderPolicy_*` проверяют anchor layout и вариативность позиции RenegotiationInfo. Для Firefox/Safari/Mobile — `FixedFromFixture` (без shuffle).
>
> **RenegotiationInfo tail anchor (HIGH — НОВОЕ V7):** В предыдущем состоянии `TlsHelloBuilder.cpp` часть `\xff\x01` (RenegotiationInfo) была зафиксирована как «хвостовой якорь» permutation — она всегда оказывалась последней в перемешиваемом блоке. Но `ShuffleChromeTLSExtensions` перемешивает RenegotiationInfo наравне с другими расширениями (это не anchor). Детектор, который ожидает RenegotiationInfo в конце блока, может использовать это как сигнатуру. На текущей ветке это исправлено: RenegotiationInfo входит в общий shuffled pool.
>
> **scrapy-impersonate / curl_cffi (НОВОЕ V7 — источник эталонных ClientHello):** `docs/Samples/scrapy-impersonate/` использует библиотеку `curl_cffi`, которая реализует TLS-фингерпринты браузеров через BoringSSL с активными Chrome-профилями. `BrowserType` из curl_cffi включает `chrome131`, `chrome133`, `firefox135`, `safari18_0`, `edge131` и другие версии. Это **сильный программный источник** эталонных ClientHello и хороший способ автоматизировать fixture refresh, но не единственный source of truth: точность зависит от pinned версии `curl_cffi`/BoringSSL и корректности локального capture workflow. Поэтому policy для PR-2: использовать curl_cffi **вместе** с живыми pcap и bundled uTLS snapshot, а не вместо них. На текущей ветке это уже оформлено как reproducible workflow: parser-driven extractor `test/analysis/extract_client_hello_fixtures.py`, provenance-diff `test/analysis/diff_client_hello_fixtures.py`, platform corpus `test/analysis/fixtures/clienthello/<platform>/*.json` и merge-step `test/analysis/merge_client_hello_fixture_summary.py`, который генерирует `test/stealth/ReviewedClientHelloFixtures.h` для capture-driven PR-2 тестов.
>
> **Chrome 120 PQ-variant использует 0x6399, не 0x11EC (НОВОЕ V7):** `HelloChrome_120_PQ` в bundled uTLS использует `X25519Kyber768Draft00 = 0x6399` (старый Kyber draft, не финальный ML-KEM стандарт). Только начиная с Chrome 131+ перешли на `X25519MLKEM768 = 0x11EC` (4588, финальный RFC 9180/IANA). Соответственно: `BrowserProfile::Chrome120` → `HelloChrome_120` (non-PQ, без PQ key share совсем) — это корректная цель. Если когда-либо будет добавлен `Chrome120_PQ`, он **обязан** использовать `0x6399`, а не `0x11EC`.
>
> **ExtensionOrderPolicy enum renaming (HIGH — V7):** `ChromeShuffleBounded = 1` переименовать в `ChromeShuffleAnchored = 1`, чтобы точно отражать семантику: якоря (не окна).
>
> **Главный оставшийся разрыв по надёжности ClientHello (HIGH — НОВОЕ V7):** текущий `td/mtproto/stealth/TlsHelloBuilder.cpp` уже исправил route-aware ECH on/off, anchored shuffle и RFC7685-style padding decision, но всё ещё выдаёт **один Chrome-like шаблон**, а не snapshot-backed browser registry. На текущей ветке это один lane с жёстко заданными `ALPS=0x44CD`, `PQ=0x11EC`, `ALPN=h2/http/1.1` и optional `ECH=0xFE0D` по route policy. Это лучше старого synthetic hello, но **менее надёжно**, чем fixture-driven профили из uTLS/curl_cffi/pcap. Следствие: приоритет PR-2 выше, чем новые transport-shaping эвристики; пока нет profile registry, нельзя считать ClientHello «браузерно-надёжным».

### Аудит V6: Критические исправления

> V6 документ был повторно проверен против реального исходного кода TDLib, bundled uTLS (`docs/Samples/utls-code`) и TLS wire-format. Внесены следующие исправления:
>
> 1. **S3 (КРИТИЧЕСКОЕ):** `0x11EC` — это валидный IANA codepoint X25519MLKEM768, и в bundled uTLS `HelloChrome_131` сейчас тоже используется `0x11EC`. Жесткая привязка "Chrome131 == 0x6399" признана недостоверной. Решение: codepoint должен быть profile/snapshot-driven и проверяться по capture/утвержденному реестру структурных семейств.
> 2. **S4 (КРИТИЧЕСКОЕ):** по реальному коду `td/mtproto/TlsInit.cpp` ECH type уже `0xFE0D` (не `0xFE02`). В плане это фиксируется как regression-guard и route-aware policy (RU default-off), а не как «новая фича».
> 3. **S8 (КРИТИЧЕСКОЕ):** 5-байтный ECH префикс `\x00\x00\x01\x00\x01` (outer + kdf + aead) корректен. По текущему коду declared encapsulated key length `\x00\x20` совпадает с фактически записываемыми 32 байтами; риск переносится в обязательные wire-regression тесты, чтобы исключить откат к mismatched длинам.
> 4. **S20 (КРИТИЧЕСКОЕ):** для RU egress ECH практически блокируется. Без route-aware режима "ECH off in RU" соединения будут массово отрезаться.
> 5. **S21 (HIGH):** QUIC/HTTP3 (RU->non-RU) блокируется. Стратегия должна явно оставаться в TCP+TLS и не имитировать QUIC-поведение.
> 6. **S7 (HIGH):** ALPS codepoint нельзя задавать глобально. По bundled uTLS snapshot есть оба валидных варианта: `HelloChrome_131` использует `0x4469`, а `HelloChrome_133` — `0x44CD`. Ошибка — не конкретный codepoint, а hardcode без profile/snapshot привязки.
> 7. **S16/S18 (MEDIUM):** аппликационная TCP-фрагментация ClientHello и принудительное сведение трафика «в один сокет» признаны плохими default-стратегиями для этого проекта; переводятся в «не внедрять по умолчанию».
> 8. **S22 (КРИТИЧЕСКОЕ, НОВОЕ):** ALPN=`h2/http/1.1` при передаче сырого MTProto после handshake создаёт L7 несоответствие. Это отдельный остаточный риск, который нельзя закрыть только JA3/JA4/IPT/DRS.
> 9. Обновлены smoke-tests и чеклист: разделение Global/RU режимов для ECH, profile-registry для PQ codepoint, проверка ECH wire-структуры и явный L7 scope-guard.
> 10. **Уточнение по pcap-аудиту (2026-04-05):** в `test_logs.pcapng` редкие `0xFE02` — это не parser/noise-«мусор», а валидно декодируемые ClientHello extension-блоки (6 TCP CH, единая JA4-family, SNI `telemost.yandex.ru`). Политика: учитывать как отдельное legacy fixture-family в анализе, но не переносить в runtime policy (в production остаётся запрет `0xFE02`).
> 11. **S1 (КРИТИЧЕСКОЕ):** риск fixed `517` остаётся обязательным guardrail после любых рефакторов. Проверка должна валидировать не только «не 517», но и browser-like распределение длин по profile policy (RFC 7685 style windowing), без нового узкого статического коридора.
> 12. **S6/S24 (HIGH):** прежний `Op::permutation + shuffle_fixture_bounded_windows` в non-Darwin использовал окна по 4 элемента, что **менее случайно**, чем Chrome. Правильная цель — `ChromeShuffleAnchored`: полный random shuffle всех расширений кроме якорей GREASE/padding/PSK (аналог `ShuffleChromeTLSExtensions`). Прежний «tail anchor» на RenegotiationInfo (`\xff\x01`) не соответствовал Chrome — Chrome перемешивает RenegotiationInfo произвольно. На текущей ветке это уже исправлено в `TlsHelloBuilder.cpp`; оставшийся риск — регрессия при будущих профильных рефакторах.

Примечание по внешним артефактам: в текущем workspace отсутствует файл `0001-TLS-test.patch.txt`, поэтому ссылки на него трактуются как внешняя справка (out-of-repo), а не как единственный source of truth для implementation-пунктов.

### Аудит внешнего ревью (4 спорных тезиса)

Ниже — верификация 4 тезисов из внешнего security-ревью по реальному коду репозитория и материалам в `docs/Samples`.

| Тезис внешнего ревью | Вердикт | Что исправить в плане |
|---|---|---|
| «Нужно полностью выкинуть Op-DSL и перейти только на template patching» | 🟡 Частично верно (проблема качества есть), но рецепт неверный как default | Оставить декларативный builder (Op-DSL), усилить wire-валидаторы и differential-тесты против capture/uTLS. Template replay допустим как опциональный fallback-профиль, но не как единственный режим. |
| «Sync overflow write разрушает маскировку по таймингам» | 🔴 Верно | Убрать sync overflow write полностью. Ввести hard backpressure через `can_write()` и watermark-политику. |
| «TrafficHint имеет data race между акторами» | ❌ Неверно для текущей архитектуры TDLib | Явно зафиксировать actor-confinement: один `Session` владеет своим `SessionConnection`/`RawConnection`/`transport_`, конкурентного доступа к одному transport нет. Оставить consume-once семантику, но без ложной модели race. |
| «Нужно снижать энтропию TLS record нулями/ASCII-паддингом» | ❌ Неверно и опасно для этого протокола | Запретить payload-level tampering в fake-TLS record. Для MTProto over AES-CTR это ломает протокол и не даёт корректной маскировки. Работать через timing/size/policy, а не через искусственную де-энтропизацию ciphertext. |

Ключевая поправка: модуль `sudoku` в xray-core существует, но это pre-TLS/finalmask слой для потока, а не «внутри-TLS record zero padding». По реальному коду `finalmask`/`sudoku` он оборачивает `net.Conn`/`net.PacketConn` (`WrapConnClient/WrapConnServer`, `WrapPacketConnClient/WrapPacketConnServer`) как appearance-transform wrapper. Переносить этот приём в текущий TDLib transport 1:1 нельзя.

### Аудит доп. тезисов из чата (Spark и команда)

| Тезис из чата | Вердикт | Решение в плане |
|---|---|---|
| S16: «Фрагментация ClientHello в TDLib поможет обойти DPI» | 🔴 Не принимать как default | Для ТСПУ/NGFW с TCP reassembly польза низкая, а сложность/регрессии высокие. App-level fragmentation в этом плане не внедряется; максимум — отдельный research трек вне production-пути. |
| S18: «Свести всё в одно долгоживущее TCP соединение» | 🔴 Не принимать | Принудительный single-socket даёт поведенческую аномалию. В плане запрещён forced single-socket; допускается только capture-driven pool policy без жёсткого схлопывания. |
| «xhttp/xtls: maxConnections=1 + H2 CONNECT лечит блокировки» | 🟡 Частично как внешняя гипотеза | В текущем репозитории нет текстовых артефактов xhttp/xtls/trustrunnel, а текущий transport не реализует H2 CONNECT payload framing. Прямой перенос недопустим; берём только идею как измеряемую гипотезу про flow-cadence и проверяем pcap/smoke-метриками. |
| «Нужен жёсткий ECH circuit breaker» | ✅ Принято | RU/unknown: ECH disabled. non-RU: ECH только с circuit breaker + TTL-state между соединениями. |
| «Главный риск — L7 несоответствие ALPN и payload» | ✅ Принято | Добавлен S22 и отдельный scope-guard: текущий план не обещает неотличимость от реального h2/h1 без полноценной L7-обёртки. |

### Аудит PR #30528 (TLS: randomize padding, ALPS, optional ECH)

PR полезен как источник анти-паттернов для этого плана. Зафиксированы правила, чтобы не повторять ошибки:

Scope-disambiguation (обязательно при ревью внешних PR/диффов):

1. Этот документ привязан к `tdlib-obf/td/mtproto/*`. Утверждения из `tdesktop` PR считаются advisory до сверки с кодом этого репозитория.
2. На текущем состоянии `tdlib-obf` ECH type в `TlsInit.cpp` уже `0xFE0D` (legacy `0xFE02` в этом файле отсутствует). Поэтому для этого репозитория это regression-guard, а не «новый фикс в этом PR».
3. Если внешний ревью-комментарий ссылается на другой файл/ветку (например, `Telegram/SourceFiles/...`), он должен маркироваться как cross-repo finding и не может автоматически менять приоритеты `tdlib-obf` roadmap.

1. `S1` padding: диапазонный jitter вида `496 + RandomIndex(41)` снимает фикс `517`, но создаёт новый узкий «коридор» длин. Для production нужна profile-driven padding-политика (Boring-style window/none-by-profile), а не всегда-on случайный диапазон.
2. `S7` ALPS: нельзя трактовать `0x4469` как «всегда новое/правильное», а `0x44CD` как «всегда legacy». Проверка должна быть fixture-driven по конкретному browser snapshot/capture.
3. `S20` ECH: compile-time/env тумблеры для transport-политики слишком хрупкие. Требуется runtime route-aware policy + circuit breaker, чтобы RU/non-RU поведение не зависело от способа сборки.
4. Scope hygiene: wire-format изменения и крупные refactor-выносы должны идти раздельно; stealth PR должен оставаться минимальным и проверяемым по diff.
5. Внешние iOS-патчи с literal-правками должны проходить wire-level sanity-check: при удалении signature-scheme нельзя оставлять старые `signature_algorithms` длины (`extension_data_len` и `list_len`). В runtime импортировать только структурно валидные изменения (например, удаление 3DES suites, актуализация `supported_versions`).
6. По итогам текущего цикла внешние iOS literal-правки фиксируются как plan-only input: в `td/mtproto/TlsInit.cpp` не вносятся точечные ad-hoc изменения вне профильного PR-трека (`PR-2` registry + fixture-driven tests).

---

## Содержание

1. Модель угрозы и полная карта сигнатур
2. Архитектура
3. Граф зависимостей PR
4. PR-A: Test Infrastructure (TDD foundation)
5. PR-1: TLS ClientHello — Context + Per-Connection Entropy
6. PR-2: Browser Profile Registry (Chrome 131/120, Firefox 148, Safari 26.3 snapshot)
7. PR-3: IStreamTransport extensions + Activation Gate
8. PR-4: StealthTransportDecorator (скелет + activation)
9. PR-5: IPT — Inter-Packet Timing (Log-normal + Markov + Keepalive bypass)
10. PR-6: DRS — Capture-Driven Dynamic Record Sizing
11. PR-7: TrafficClassifier + Correct Wiring (Session/Raw/Handshake)
12. PR-8: Runtime Params Loader (hot-reload)
13. PR-9: Integration Smoke Tests + PR-10 ServerHello Realism track
14. Таблица изменяемых файлов
15. OWASP ASVS L2 матрица
16. Синхронизация с telemt
17. Риск-регистр
18. Критерии готовности к релизу

---

# 1. Модель угрозы и полная карта сигнатур

## 1.1 Что видит ТСПУ

| Слой | Что наблюдается | Метод детекции |
|---|---|---|
| TCP | SYN timing, window size | p0f, пассивный фингерпринт |
| TLS Handshake | ClientHello: cipher suites, extensions, groups | JA3, JA4, ML-classifier |
| TLS Application Data | Размер записей, timing между записями | Statistical analysis |
| Connection | Продолжительность, объём данных | Behavioural signature |

TCP-слой контролируется ОС, не приложением. Атакуем только TLS и Application Data слои.

## 1.2 Полная карта детектируемых сигнатур

| ID | Серьёзность | Где | Описание | Статус |
|---|---|---|---|---|
| S1 | 🔴 CRITICAL | `TlsHelloStore` | Fixed `517` на текущей ветке уже устранён, но padding policy всё ещё не fixture-driven: builder использует RFC7685 window decision и добавляет entropy-only padding на ECH-disabled lane, что снижает single-length signature, но ещё не привязано к конкретному browser snapshot. | Частично исправлено |
| S2 | 🔴 CRITICAL | `TlsHelloContext` / ECH lane | Per-connection sampling длины ECH payload на текущей ветке уже есть, но ECH остаётся частью одного Chrome-like lane без profile registry и без полноценного runtime circuit-breaker/TTL state. | Частично исправлено |
| S3 | 🔴 CRITICAL | Supported Groups + Key Share | PQ group codepoint захардкожен без profile/snapshot registry. Для устойчивой маскировки codepoint должен выбираться из валидированного structural-family профиля и совпадать в **двух** местах: supported_groups и key_share. | Исправить |
| S4 | 🔴 CRITICAL | ECH extension | В текущем коде ECH type уже `0xFE0D`; критичен риск регрессии и отсутствие route-aware ECH policy (RU/unknown должны быть default-off). | Закрепить тестами |
| S5 | 🟠 HIGH | `TcpTransport` | `MAX_TLS_PACKET_LENGTH = 2878` — статическая константа, известная сигнатура | Исправить |
| S6 | 🟠 HIGH | TlsHello | Статический op-template по-прежнему задаёт один Chrome-like ClientHello family для non-Darwin. Энтропийные поля уже per-connection, но сам выбор профиля ещё не registry-driven. | Исправить |
| S7 | 🟠 HIGH | Chrome профиль | ALPS codepoint в текущем коде захардкожен как `0x44CD` без profile/snapshot привязки. Для части профилей/captures нужен `0x4469` (пример: Chrome131), для части — `0x44CD` (пример: Chrome133+). Критичен hardcode, а не «один правильный» codepoint. | Исправить |
| S8 | 🔴 CRITICAL | ECH inner | Для `enc` обязателен wire-инвариант: declared length == фактическая длина байт. В текущем коде 32/32; нужно зафиксировать regression-тестами и smoke-парсером. | Закрепить тестами |
| S9 | 🟠 HIGH | Darwin (`#if TD_DARWIN`) профиль | 3DES suite (0xC012, 0xC008, 0x000A) — удалены Apple в iOS 15 / macOS Monterey (2021). Присутствуют **только** в Darwin-ветке кода, не в основном Chrome-профиле. | Исправить |
| S10 | 🟠 HIGH | Firefox профиль (будущий) | При создании Firefox-профиля: НЕ включать 3DES (0x000A) — Firefox удалил в 2021. Текущий код не имеет Firefox-профиля вообще. | Исправить |
| S11 | 🟠 HIGH | Darwin | `#if TD_DARWIN` использует специальный TLS-профиль эпохи 1.2 — тривиально детектируем | Исправить |
| S12 | 🟠 HIGH | IPT | Равномерное распределение межпакетных интервалов (нет jitter) | Исправить |
| S13 | 🟡 MEDIUM | DRS | Фиксированные record size (1380/4096/16384) без ±jitter — механистично | Исправить |
| S14 | 🟡 MEDIUM | Keepalive | Keepalive/PING не должен проходить через искусственные IPT-задержки: для online main-сессии `ping_disconnect_delay() = rtt()*2.5` (обычно десятки-сотни ms), для offline/non-main — `135 + random_delay_` секунд. | Исправить |
| S15 | 🟡 MEDIUM | Session | Hint для первых auth-пакетов не выставляется → лишние задержки при handshake | Исправить |
| S16 | 🟡 MEDIUM | ClientHello | App-level фрагментация ClientHello в TDLib даёт слабый эффект против DPI с reassembly и повышает сложность/риски регрессий. | Не внедрять по умолчанию |
| S17 | 🔴 CRITICAL | TLS Response | ServerHello/response path в emulate_tls остаётся синтетическим и слишком однотипным (`\x16\x03\x03` + CCS + Application Data + HMAC-check path), что расходится с наблюдаемым разнообразием ServerHello в реальных дампах. | Исправить (tdlib+telemt) |
| S18 | 🟠 HIGH | Connection | Flow-level аномалия: детектируется не только single-socket, но и чрезмерный churn короткоживущих TCP к одному destination. В коде есть multi-session pools (`SessionMultiProxy`), поэтому нужен контроль cadence/reuse по capture, а не жёсткий one-size-fits-all. | Исправить |
| S19 | 🟠 HIGH | SNI | При `ee`-секрете SNI domain берётся из `ProxySecret::get_domain()` и обычно стабилен в рамках активного proxy-профиля; длительная концентрация на одном домене повышает поведенческую заметность. | Исправить |
| S20 | 🔴 CRITICAL | Deployment policy | ECH в RU egress блокируется на части маршрутов. На текущей ветке RU/unknown уже fail-closed (`ECH off`), но ещё нет полноценного runtime circuit-breaker/TTL state между соединениями. | Частично исправлено |
| S21 | 🟠 HIGH | Protocol strategy | QUIC/HTTP3 (RU->non-RU) блокируется. Попытки имитации QUIC создают дополнительную поверхность детекта/блока. | Исправить |
| S22 | 🔴 CRITICAL | L7 semantics | ALPN декларирует `h2/http/1.1`, но после handshake текущий transport несёт сырой MTProto (без HTTP/2 или HTTP/1.1 фрейминга). Для продвинутого DPI это отдельная поведенческая сигнатура. | Открытый риск (отдельный трек) |
| S23 | 🔴 CRITICAL | Profile policy | OS/profile incoherence: desktop Linux/Windows не должны внезапно выглядеть как Safari/iOS-профили. Иначе возникает кросс-слойное противоречие (TCP/OS family signature vs TLS-family). | Исправить |
| S24 | 🟠 HIGH | Capture baseline | Capture-набор гетерогенный: несколько реальных TLS1.3 семейств (в т.ч. ECH+PQ), классические TLS1.3 без PQ, TLS1.2 fallback на части серверов и ALPN-вариативность (`h2/http/1.1` vs `h3`). В имеющихся дампах `h3` наблюдается в QUIC/UDP, не в TCP ClientHello; без transport/provenance-tagging появляются ложные profile assertions (ALPS/ECH/PQ/ALPN). | Исправить |
| S25 | 🔴 CRITICAL | Extension shuffle | `ShuffleChromeTLSExtensions` в bundled uTLS — **полный shuffle** всех non-GREASE/non-padding/non-PSK расширений (см. u_parrots.go:ShuffleChromeTLSExtensions). На текущей ветке default non-Darwin builder уже переведён на `ChromeShuffleAnchored`; остаточный риск — регрессия при будущем profile-registry refactor или ошибочное применение Chrome-shuffle к не-Chrome профилям. | Закрепить тестами |
| S26 | 🟠 HIGH | Chrome 120 PQ variant | `HelloChrome_120_PQ` в uTLS использует `X25519Kyber768Draft00 = 0x6399` (Kyber draft), не `0x11EC` (ML-KEM финальный). Если когда-либо будет добавлен профиль Chrome120_PQ, обязателен PQ codepoint `0x6399`, не `0x11EC`. `BrowserProfile::Chrome120` → `HelloChrome_120` (non-PQ, без PQ key share). | Закрепить комментарием в ProfileFixtures.h |
| S27 | 🟡 MEDIUM | curl_cffi + browser capture fixture workflow | Базовый capture workflow больше не полностью ручной: в репо уже есть parser-driven extractor, provenance diff, frozen batch-1 corpus и merge-step в reviewed summary header. Остаточный риск: расширить тот же pipeline на будущие `curl_cffi_capture` artifacts и новые corpus batches без drift между JSON corpus и test-facing header. | Частично закрыто; поддерживать через corpus+merge flow |

## 1.2.1 Проверка по реальным дампам (ClientHello + ServerHello)

Наблюдения из `docs/Samples/Traffic dumps` (tshark разбор):

- `gosuslugi.pcap`: одновременно есть modern CH (`ECH=0xFE0D`, `ALPS=0x44CD`, `PQ=0x11EC/4588`) и отдельное legacy-семейство (`JA3=5aac...`) без modern extension-блока.
- `test_logs.pcapng`: выраженно многосемейный набор. В текущем sample-наборе наблюдается крупный bucket `JA3=424f6d...` с ECH+PQ, без ALPS; присутствуют также семейства с `ALPS=0x44CD`. Parser-level разбор подтверждает **6 TCP ClientHello** с реальным legacy `0xFE02` extension family (в этом capture они попадают в один наблюдаемый JA4 bucket `t13d1516h2_8daaf6152771_d1e053f3eb1c`, SNI `telemost.yandex.ru`). Важно: эти JA3/JA4 значения трактуются только как observed capture artifacts, а не как стабильные browser constants: Chrome-family randomization может раскалывать одну profile-family на несколько hashes/tokens между прогонами. Наивный byte-scan вида `frame contains fe:02` даёт **7** совпадений, потому что как минимум один `0xFE0D` ECH payload содержит такой байтовый паттерн внутри тела. Политика для аудита: учитывать `0xFE02` только через parser-level extraction, а не через raw byte grep.
- `Fire.pcapng` / `Yabr.pcapng` / `Дамп firefox.pcap`: реальные modern TLS1.3 семейства с `ECH=0xFE0D`, `PQ=0x11EC/4588`; в capture встречаются оба ALPS codepoint (`0x4469` и `0x44CD`) в зависимости от семейства. По TCP-срезу смешивания codepoint внутри одной JA4-family не наблюдается.
- `test_logs.pcapng` и `Дамп firefox.pcap`: ALPN `h3` встречается только в `udp:quic:tls` кадрах; в TCP ClientHello `h3` не наблюдается. Для TCP+TLS masking это обязательное правило фильтрации baseline.
- `beget.com.pcap` / `ur66.ru.pcap` / `web_max.ru_.pcap`: «классический» TLS1.3 ClientHello без ECH/PQ/ALPS (JA3 семейства без hybrid-групп), при этом по ServerHello виден как TLS1.3, так и TLS1.2-like fallback (`0xC030`) в отдельных трассах.
- ServerHello в целом по набору сильно вариативен (extension-матрицы `43,51`, `51,43`, `43,51,41`, а также TLS1.2-like наборы типа `65281,16,23`). Следовательно, smoke-stage обязан проверять правдоподобие ServerHello, а не только ClientHello.
- **Extension shuffle observation (V7):** во всех Chrome-семействах из pcap расширения появляются в случайном порядке — ни одно поле между соединениями не фиксировано (кроме GREASE на первом/последнем местах и padding в конце). Это подтверждает, что Chrome использует полный shuffle (не windowed). RenegotiationInfo (`0xFF01`) наблюдается в произвольных позициях внутри блока — не всегда последней.
- **curl_cffi как дополнительный capture источник (V7):** `docs/Samples/scrapy-impersonate/middleware.py` использует `curl_cffi.BrowserType`, который включает `chrome131`, `chrome133`, `firefox135`, `safari18_0`, `edge131`. Захват ClientHello через существующий `capture_chrome131.py` полезен как автоматизируемый BoringSSL-based baseline, но должен храниться с provenance/version pinning и проверяться против pcap/uTLS snapshot. Это дополнительный capture source, а не замена живым дампам.

## 1.3 Что уже хорошо (база есть, но нужна калибровка)

- `MlKem768Key` (1184 байт) — правильная структура ML-KEM-768 публичного ключа (384 пары NTT-коэффициентов ∈ [0, 3329) + 32 random bytes)
- `Permutation` + GREASE primitives уже есть и полезны как база. На текущей ветке non-Darwin default исправлен на Chrome-совместимый anchored shuffle для Chrome-like hello. Дальнейшая задача — не откатиться назад и распространить profile-driven policy дальше: для `Chrome*` профилей — `ChromeShuffleAnchored`, то есть полный shuffle всех non-GREASE/non-padding/non-PSK расширений по модели `ShuffleChromeTLSExtensions`; для `Firefox/Safari/Mobile` — `FixedFromFixture`. Fail-closed проверка against allowed-order set остаётся обязательной.
- GREASE в cipher suites и extensions (через `Grease::init()`) — уже рандомный, формат `(byte & 0xF0) | 0x0A` корректен по RFC 8701
- HMAC-SHA256 в ClientHello.random — взаимная аутентификация с сервером
- ALPN: `h2` + `http/1.1` выставляется в соответствии с browser-like профилем на уровне ClientHello
- `Random::secure_bytes` для ключей
- Curve25519 key generation с проверкой квадратичного вычета — правильно

## 1.4 Предупреждение: TLS-in-TLS не использовать

Реальный TLS-стек (BoringSSL) поверх синтетического — запрещён. Причины:
- Валидный сертификат для SNI недоступен клиенту
- 2 RTT вместо 1 RTT → новый поведенческий фингерпринт
- Избыточное шифрование без выгоды

Синтетический TLS — **транспортная маскировка**, а не защита данных.

## 1.5 Операционные ограничения RU

- Для RU egress режим по умолчанию: ECH disabled (расширение не отправлять).
- Для non-RU egress: ECH можно включать только в controlled/validated профиле.
- QUIC/HTTP3 в этой стратегии не использовать; целевой транспорт — TCP + TLS only.

## 1.6 Критическое ограничение L7 (ALPN vs payload)

На текущем коде после TLS handshake передаётся сырой MTProto внутри TLS-like records (`RawConnection::send_crypto` -> `transport_->write` -> `ObfuscatedTransport::do_write_tls`), без HTTP/2 frame layer и без HTTP/1.1 message grammar.

Следствие:

- Handshake-маскировка (JA3/JA4, ECH/ALPS, GREASE) снижает стоимость базового детекта, но не закрывает L7-поведенческий анализ.
- Заявление «полностью неотличим от браузерного h2/h1» в рамках PR-A..PR-9 недопустимо.

Правило для V6:

- PR-A..PR-9 считаются фазой handshake+timing+record-shaping hardening.
- Полноценная L7-мимикрия (реальные HTTP/2/HTTP/1.1 фреймы поверх transport) — отдельный тяжёлый трек, не входящий в текущий релизный scope.

## 1.7 Flow-Реализм: Cadence И Destination Concentration

Проверенные факты по текущему коду:

- Сетевой слой использует multi-session модель (`SessionMultiProxy`) и отдельные пулы для main/upload/download, поэтому модель «всегда один TCP на DC» технически неточна.
- Параметр `session_count` конфигурируемый (`max(option("session_count"), 1)`), что может сдвигать профиль как в сторону churn, так и в сторону reuse.
- TLS domain в emulate_tls пути приходит из `ProxySecret::get_domain()`, т.е. для данного proxy-профиля обычно стабилен.

Следствие:

- Для DPI-риска критичен не один частный паттерн, а целый класс flow-аномалий: слишком быстрые reconnect-бёрсты к одному IP/SNI, слишком длинная монодоменная концентрация, либо искусственное схлопывание в один сокет.
- S18/S19 в этом плане трактуются как HIGH и закрываются через cadence/reuse/pool-policy + smoke-метрики, а не через «магический» single-socket или агрессивный churn.
- Явный owner S18/S19: PR-8 внедряет policy-параметры и runtime enforcement hooks (anti-churn + sticky-rotation guardrails), PR-9 только валидирует их через smoke и не заменяет runtime-фикс.

---

# 2. Архитектура

## 2.1 Принципы

| Принцип | Применение |
|---|---|
| **Strict Activation Gate** | Единственный `if (secret.emulate_tls())` в `create_transport()`. Нигде больше нет `if (is_stealth)` в горячем коде. |
| **Decorator** | `StealthTransportDecorator` реализует `IStreamTransport`, держит inner. Вся логика маскировки здесь. |
| **Factory** | `create_transport()` — единственная точка принятия решения о враппинге. |
| **Pre-sampled Context** | Все случайные длины (padding, ECH, record jitter) вычисляются **один раз** в `TlsHelloContext` при его создании. CalcLength и Store только читают. |
| **Consume-once Hint** | `TrafficHint` потребляется один раз в `write()`, авто-сбрасывается в `Unknown`; до PR-7 действует нормализация `Unknown -> Interactive`. Это защита от hint-drift; data race между акторами здесь не ожидается из-за actor-confinement. |
| **Bounded Ring + Hard Backpressure** | `write()` никогда не пишет в `inner_` в обход IPT/DRS. При достижении high watermark `can_write()` возвращает `false`, отправка откладывается до drain ring. |
| **Hot Path: Zero Alloc** | После init нет аллокаций на пакет. Нет `dynamic_cast`. Все через virtual. |
| **TDD: Red First** | Каждый PR начинается с красных тестов, которые падают на текущем коде по правильной причине. |
| **TDLIB_STEALTH_SHAPING=OFF** | Compile-time feature flag. При OFF — все upstream тесты проходят bit-for-bit. |
| **No Payload Tampering** | Не модифицировать содержимое шифротекста для «понижения энтропии». Маскировка только через handshake/profile, timing и record sizing. |

## 2.2 Слои обработки пакета

```
SessionConnection::flush_packet()
    │
    ├─ set_traffic_hint(Keepalive | Interactive | AuthHandshake)
    │
    ▼
StealthTransportDecorator::write(message, quick_ack)
    │
    ├─ [1] Consume hint (auto-reset to Unknown)
    ├─ [2] Unknown fallback (PR-5): Unknown -> Interactive
    │       (capture-driven bytes->hint classifier появляется в PR-7)
    ├─ [3] IptController::next_delay_us(has_pending, hint)
    │       ├─ Keepalive / BulkData / AuthHandshake → delay = 0
    │       └─ Interactive → log-normal sample + Markov transition
    ├─ [4] Enqueue to ShaperRingBuffer
    │       └─ if ring full: backpressure (no direct write to inner)
    │
StealthTransportDecorator::pre_flush_write(now)
    │
    ├─ [5] Detect idle gap → DrsEngine::notify_idle() if gap > sampled threshold
    ├─ [6] DrsEngine::next_payload_cap(hint) from profile/capture bins
    ├─ [7] inner_->set_max_tls_record_size(jittered_size)
    ├─ [8] ring_.drain_ready(now, write_to_inner)
    └─ [9] if pending < low watermark: снять backpressure

RawConnection::flush_write()
    └─ calls pre_flush_write() on transport
```

---

# 3. Граф зависимостей PR

```
PR-A  (Test Foundation: wire/parser tests + narrow seams)
  │
  ├─► PR-1  (TlsHelloContext pre-sampling + ECH per-connection + GREASE fix)
  │     └─► PR-2  (Browser Profile Registry: Chromium-first verified lanes + advisory others)
  │           └─► PR-3  (IStreamTransport extensions + Activation Gate + StealthConfig)
  │                 └─► PR-4  (StealthTransportDecorator: skeleton + consume-once hint)
  │                       ├─► PR-5  (IPT: log-normal + Markov + Keepalive bypass)
  │                       ├─► PR-6  (DRS: capture-driven bins + coalescing + idle-reset)
  │                       └─► PR-7  (TrafficClassifier + Correct Wiring)
  │                             └─► PR-8  (Runtime Params Loader, JSON hot-reload)
  │
  └─► PR-9  (Integration Smoke Tests: Python scripts vs local telemt)
        └─► PR-10 (ServerHello realism: tdlib parser hardening + telemt response profiles)
```

**Параллелизация:**
- PR-A и PR-1 можно начинать параллельно только в части discovery/красных тестов; merge-order остаётся `PR-A -> PR-1`, потому что PR-A вводит минимальные test seams для deterministic/capture-driven проверок
- PR-5 и PR-6 можно разрабатывать параллельно после PR-4; PR-7 можно готовить параллельно, но merge-order для bypass-логики должен быть `PR-5 -> PR-7`, потому что до PR-7 действует контракт `Unknown -> Interactive`
- PR-8 независим от PR-5/6/7, зависит от PR-3 и PR-2 (типы профилей/ECH mode)
- PR-10 запускается только после PR-9 smoke-infra: без уже работающего `check_server_hello_matrix` и fixture-tagging невозможно fail-closed проверить реализм ответа.

---

# 4. PR-A: Test Infrastructure

## 4.0 Реальный статус на текущей ветке (2026-04-06)

PR-A по факту **реализован существенно больше, чем исходный минимальный план**. В репозитории уже присутствуют production seams и parser/test helpers, на которые опираются PR-1/PR-2 runtime и regression tests:

- `td/mtproto/stealth/Interfaces.h` уже содержит `IRng`, `IClock`, `PaddingPolicy`, `NetworkRouteHints` и factory helpers `make_connection_rng()` / `make_clock()`.
- `td/mtproto/stealth/TlsHelloBuilder.h/.cpp` уже являются production serializer/facade, а `td/mtproto/TlsInit.cpp` использует этот путь как orchestration layer для ClientHello runtime generation.
- `test/stealth/MockRng.h`, `test/stealth/MockClock.h`, `test/stealth/TlsHelloParsers.h`, `test/stealth/FingerprintFixtures.h` уже существуют и используются в `run_all_tests`.
- `test/CMakeLists.txt` уже подключает большой stealth test surface в `run_all_tests`; отдельного `tdmtproto_tests` target в репозитории по-прежнему нет.

Следствие: PR-A надо считать **завершённым как foundation layer**, а не как только «черновой план». Дальнейшие правки в этой зоне должны трактоваться как regression/infrastructure maintenance, а не как незакрытая базовая работа.

**Цель:** построить детерминированный и capture-driven test foundation для ClientHello/profile work, не вводя фиктивных интерфейсов раньше времени.  
**Реальные поправки по итогам аудита кода:**

1. В репозитории **нет** target `tdmtproto_tests`. Сейчас mtproto-код собирается в `tdmtproto`, а интеграция тестов идёт через `test/CMakeLists.txt` и target `run_all_tests`.
2. В репозитории **нет** `td/mtproto/CMakeLists.txt`. Новые production sources нужно подключать в корневом `CMakeLists.txt`, а тестовые файлы — в `test/CMakeLists.txt`.
3. `RecordingTransport` и transport-seam fakes не относятся к исходному scope PR-A; это корректно считать PR-3 work, потому что они опираются на уже расширенный `IStreamTransport`.
4. `TlsHelloBuilder` уже существует в `td/mtproto/stealth/TlsHelloBuilder.cpp`, а `td/mtproto/TlsInit.cpp` сейчас играет роль orchestration wrapper. Значит PR-A больше не должен планировать «вынести builder из TlsInit.cpp»; он должен использовать уже существующий seam, расширять тесты вокруг него и не дублировать serializer в test helper'ах.

**Gate:** `cmake --build . --target run_all_tests && ctest --output-on-failure -R run_all_tests`  
**Отдельный smoke-stage:** offline Python/pcap-проверки из раздела 13. Это не merge-gate для PR-A, а дополнительный дифференциальный контроль против `docs/Samples`.

## 4.1 Что именно входит в PR-A

PR-A не должен пытаться тестировать будущий shaper/decorator раньше появления транспортных seam'ов. Его задача уже на старте закрыть две вещи:

- **wire-структурную корректность** synthetic ClientHello/ECH/extension blocks;
- **differential-проверку против референсов**: bundled uTLS, `docs/Samples/Traffic dumps/*.pcap*`, RFC 8446.

Минимально допустимые изменения в production для этого этапа:

- использовать уже существующий serializer/builder ClientHello из `td/mtproto/stealth/TlsHelloBuilder.cpp`, чтобы тесты потребляли **тот же** production-код сериализации, а не копию;
- ввести узкие абстракции `IRng`/`IClock` и policy-типы, которые реально понадобятся уже в PR-1/PR-5, но **не** расширять `IStreamTransport` раньше PR-3;
- не дублировать wire-логику в test helper'ах: тест должен проверять production serializer, а не вторую независимую реализацию.

## 4.2 Файловая структура

```
td/mtproto/stealth/
  Interfaces.h             IRng, IClock, PaddingPolicy (без transport virtuals)   PR-A
  TlsHelloBuilder.h        внутренний builder/test seam                            PR-A
  TlsHelloBuilder.cpp      вынесенная из TlsInit.cpp сериализация                  PR-A

test/stealth/
  MockRng.h                детерминированный xoshiro256** для unit-тестов         PR-A
  MockClock.h              ручное время для будущих controller tests               PR-A
  TlsHelloParsers.h        parse helpers: extensions, groups, key_share, ECH       PR-A
  FingerprintFixtures.h    approved structural families / expected invariants       PR-A
  RecordingTransport.h     fake IStreamTransport (заглушка)                        PR-3 (не PR-A)
  test_tls_hello_wire.cpp  RFC8446/ECH structural checks                           PR-A
  test_tls_profiles.cpp    differential checks vs uTLS/pcap baselines              PR-A
```

Подключение в сборку:

- `CMakeLists.txt` — добавить production sources `td/mtproto/stealth/*.cpp` в `TD_MTPROTO_SOURCE`;
- `test/CMakeLists.txt` — добавить `test/stealth/*.cpp` в `TD_TEST_SOURCE`/`run_all_tests`.

## 4.3 Что PR-A обязан проверять

### A. Structural / parser-level invariants

- синтезированный ClientHello парсится как валидный TLS 1.3 handshake по RFC 8446;
- все scope lengths совпадают с реально записанными длинами;
- для ECH-block declared lengths совпадают с фактическим количеством байт;
- `supported_groups` и `key_share` согласованы по group id;
- padding policy не создаёт фиксированный target по умолчанию.

### B. Differential checks against real references

- сравнение extension set/order и codepoint policy с bundled uTLS (`docs/Samples/utls-code/u_parrots.go`, `u_tls_extensions.go`, `u_ech.go`);
- проверка против approved capture fixtures из `docs/Samples/Traffic dumps/*.pcap*`;
- rule: «JA3 не совпадает с известным Telegram» необходима, но **недостаточна**. Нужны также проверки ALPS/ECH/PQ-policy, иначе можно получить другое, но столь же synthetic structural family.

### C. Red/Regression tests (что должно быть зафиксировано в CI)

- fixed padding target `513 -> ClientHello 517`;
- per-process ECH payload length из static `ech_payload()`;
- ALPS codepoint не совпадает с выбранным profile fixture (например, `0x44CD` вместо `0x4469` для Chrome131, или наоборот для Chrome133+);
- отсутствие route-aware ECH policy (`RU/unknown = disabled`, `non-RU = validated profile only`);
- regression-check: ECH type остаётся `0xFE0D` и declared `enc` length совпадает с фактической длиной байт.

## 4.4 Что явно НЕ входит в PR-A

- `RecordingTransport` с `set_traffic_hint` / `set_max_tls_record_size`: переносится в PR-3 вместе с реальным расширением `IStreamTransport`;
- IPT/DRS timing assertions: до появления decorator/shaper это будет тестирование заглушек, а не поведения;
- live network smoke как обязательный gate: pcap/python инструменты остаются отдельным этапом в разделе 13;
- отдельная директория `td/mtproto/test/` и отдельный mtproto-only test target: текущая структура репозитория этого не имеет, и план не должен выдумывать несуществующий build path.

## 4.6 Что реально уже закрыто тестами

На текущей ветке PR-A инфраструктура уже подтверждена конкретными тестами:

- `test/stealth/test_tls_hello_wire.cpp`: structural parser checks, declared-vs-actual length checks, anti-fixed-length regressions.
- `test/stealth/test_tls_hello_builder_seam.cpp`: deterministic seam with injected RNG, same-seed stability, anti-static-output regression.
- `test/stealth/test_tls_context_entropy.cpp`: explicit serializer option seam для padding/ECH/PQ/ALPS invariants.
- `test/stealth/test_tls_hello_parser_security.cpp` и `test/stealth/test_tls_hello_parser_fuzz.cpp`: parser hardening against malformed wire images.
- `test/stealth/test_tls_hello_differential.cpp`, `test/stealth/test_tls_profiles.cpp`: differential/profile-policy checks against known structural families.

Итог: PR-A больше не является «планируемым» этапом; это уже рабочая test foundation, на которую опираются runtime changes следующих PR.

## 4.5 Эталонные примитивы для unit-тестов

```cpp
// test/stealth/MockRng.h
// Deterministic xoshiro256** PRNG implementing stealth::IRng.
// Used only in tests and differential fixtures.
class MockRng final : public stealth::IRng {
 public:
  explicit MockRng(uint64_t seed);

  uint32_t next_u32() override;
  uint32_t bounded(uint32_t n) override;

 private:
  uint64_t state_[4]{};
};
```

```cpp
// test/stealth/MockClock.h
class MockClock final : public stealth::IClock {
 public:
  double now() const override { return time_; }
  void advance(double seconds) { time_ += seconds; }

 private:
  double time_{1000.0};
};
```

```cpp
// test/stealth/TlsHelloParsers.h
// Parse helpers only. No duplicated serializer logic.
// Tests must consume bytes produced by the production TlsHello builder.
struct ParsedExtension {
  uint16_t type;
  Slice body;
};

vector<ParsedExtension> parse_extensions(Slice client_hello);
uint16_t find_supported_group(Slice client_hello, size_t index);
uint16_t find_key_share_group(Slice client_hello, size_t index);
bool ech_declared_lengths_match(Slice client_hello);

// test/stealth/TestHelpers.h
// Helper must use binary TLS parsing (RFC 8446), not regex/string scanning.
string generate_header_test(BrowserProfile profile,
                            EchMode ech_mode = EchMode::Disabled,
                            IRng &rng = default_test_rng());
vector<uint16_t> extract_supported_groups(Slice client_hello);
vector<uint16_t> extract_key_share_groups(Slice client_hello);
vector<uint16_t> extract_cipher_suites(Slice client_hello);
bool has_extension(Slice client_hello, uint16_t type);
size_t find_extension_position(Slice client_hello, uint16_t type);
Slice extract_extension_body(Slice client_hello, uint16_t type);
string compute_ja3(Slice client_hello);
bool check_pq_group_consistency(Slice client_hello);
bool contains_any_pq_group(const vector<uint16_t> &groups);
size_t extract_session_id_length(Slice client_hello);
```

---

# 5. PR-1: TLS ClientHello — Context + Per-Connection Entropy

**Зависит от:** PR-A (тестовая инфра)  
**Исправляет:** S1 (static padding), S2 (ECH singleton), S8 (ECH declared-vs-actual key length mismatch), структурную часть S3 (убрать hardcoded dual-use literals из serializer; profile registry как источник значения остаётся в PR-2)

## 5.0 Реальный статус на текущей ветке (2026-04-06)

PR-1 по сути **реализован в production и закреплён regression-тестами**. На текущем `HEAD` уже есть:

- `TlsHelloContext` с per-connection sampled state вместо process-wide singleton semantics.
- context-driven ECH payload length sampling (`144/176/208/240`) через injected `IRng`.
- fail-closed route-aware `ECH on/off` в default builder path: unknown/RU route disables ECH, known non-RU keeps it enabled.
- RFC7685-style padding decision через `PaddingPolicy::compute_padding_content_len()` и `resolve_padding_extension_payload_len()`.
- Chrome-like anchored extension shuffle вместо bounded-window permutation.
- explicit serializer seam `detail::TlsHelloBuildOptions` / `build_default_tls_client_hello_with_options()` для direct wire regression tests.

Остаток PR-1 сейчас не в том, чтобы «дописать механику», а в том, чтобы не допустить регрессию при дальнейших profile/runtime refactor'ах.

**Актуализация на текущей ветке (2026-04-05):** значимая часть механики PR-1 уже приземлена в `td/mtproto/stealth/TlsHelloBuilder.cpp`: per-connection sampling длины ECH payload, route-aware `ECH on/off`, RFC7685-style padding decision и anchored extension shuffle. Поэтому red-case snippets ниже нужно читать как **исторические регрессии, которые нельзя вернуть**, а не как описание текущего `HEAD`. Оставшийся scope PR-1: зафиксировать эти инварианты тестами и не дать profile-registry refactor сломать уже исправленные свойства.

## 5.1 Проблемы (детально)

### S2: ECH singleton — фиксированная длина на весь процесс

```cpp
// ИСТОРИЧЕСКИЙ red-case (уже устранён на текущей ветке):
static Op ech_payload() {
  Op res;
  res.type = Type::Random;
  res.length = Random::fast(0, 3) * 32 + 144;  // ← ОДИН РАЗ при init
  return res;
}
// Вызывается внутри: static TlsHello result = []{ ... }()
// Результат: длина ECH = {144, 176, 208, 240} — фиксирована для всего процесса.
// ТСПУ записывает первое соединение → блокирует все с той же длиной.
```

Критическое уточнение по итогам ревизии кода (`td/mtproto/stealth/TlsHelloBuilder.cpp`):

- на текущей ветке `sample_ech_payload_length()` вызывается из `TlsHelloContext`, а `Type::EchPayload` читает уже сэмплированную длину из context; static `TlsHello` хранит только op-template, а не process-wide ECH length;
- оставшийся риск — не текущий singleton, а **будущий регресс**, если refactor снова перенесёт sampling в static template или введёт межсоединенческий cache ECH payload;
- smoke-gate по-прежнему обязан проверять variance в рамках **одного процесса и одного profile lane**, иначе смешивание разных процессов/профилей может скрыть возврат бага.

Минимальный red-test контракт для S2 (обязателен в PR-1/PR-9):

1. Один процесс, один профиль, `non_ru_egress`, ECH enabled, минимум 64 последовательных соединения.
2. Наблюдаются только допустимые длины ECH payload: `{144, 176, 208, 240}`.
3. FAIL если в выборке ровно одна длина (признак singleton entropy path).
4. FAIL если обнаружена длина вне policy/snapshot-driven allowset.

### S3: PQ group должен быть profile/snapshot-driven

```cpp
// ТЕКУЩИЙ КОД:
// В supported_groups:
Op::str("\x11\xec\x00\x1d\x00\x17\x00\x18")
// В key_share:
Op::str("\x00\x01\x00\x11\xec\x04\xc0"), Op::ml_kem_768_key()
//
// 0x11EC = 4588 = X25519MLKEM768 (IANA финальный стандарт, RFC 9580)
// Bundled uTLS HelloChrome_131 в docs/Samples/utls-code также использует 0x11EC.
// Поэтому фиксированное правило "Chrome131 == 0x6399" ненадёжно и устаревающее.
//
// ⚠ 0x11EC — валидная именованная группа, не GREASE.
// Реальная проблема: отсутствие capture-driven profile registry.
// При этом текущий код СТРУКТУРНО согласован: 0x11EC уже присутствует и в
// supported_groups, и в key_share. Ошибка здесь не в текущем mismatch, а в том,
// что значение зашито literal'ом и будущая смена snapshot/profile должна будет
// менять ОБА места синхронно.
//
// GREASE в supported_groups обрабатывается отдельно через grease(4) — он КОРРЕКТЕН.
//
// Значение присутствует в ДВУХ местах и должно меняться СИНХРОННО:
//   1. supported_groups extension: \x11\xec
//   2. key_share extension: \x11\xec\x04\xc0 (group_id + key_exchange_length)
```

### S8: ECH wire-format regression guard для encapsulated key

```cpp
// ТЕКУЩИЙ КОД (актуально):
Op::str("\xfe\x0d"), Op::begin_scope(),
Op::str("\x00\x00\x01\x00\x01"), Op::random(1),
Op::str("\x00\x20"), Op::random(32), ...
//
// 5-байтный префикс \x00\x00\x01\x00\x01 корректен:
// [OuterClientHello=0x00][KDF=0x0001][AEAD=0x0001].
// На текущем коде длины совпадают (32/32).
// Требование плана: держать это под обязательным regression-тестом,
// чтобы исключить повторный drift declared-vs-actual в будущем.
```

### S1: Static padding target

```cpp
// ИСТОРИЧЕСКИЙ red-case (уже устранён на текущей ветке):
void TlsHelloStore::do_op(Type::Padding) {
  if (length < 513) {
    write_zero(513 - length);  // ← ровно 513 байт padding → ClientHello = 517
  }
}
// ClientHello всегда 517 байт → тривиальная сигнатура.
```

Уточнение по RFC 7685 (`docs/Standards/rfc7685.txt`, секция Example Usage):

- идея padding-extension — условно вытолкнуть длину из проблемного окна `256..511`, а не закреплять один постоянный размер handshake;
- на текущей ветке builder уже реализует именно window-based decision (`BoringPaddingStyle` semantics) и дополнительно может добавить небольшой entropy-only padding на ECH-disabled lane, чтобы избежать single-length signature;
- этот entropy-only fallback полезен как временная anti-collapse мера для fail-closed ECH-disabled маршрутов, но он ещё **не fixture-driven**, поэтому должен остаться явно временным до PR-2 profile registry;
- поэтому regression-guard для S1 должен проверять именно policy-совместимость (window-based decision + profile allowances), а не «любая случайность приемлема».

Критичный audit-check после любого рефактора `TlsInit.cpp`:

1. Не допускается возврат к forced target (`513/517`) на default lane.
2. Не допускается замена forced target на узкий always-on corridor (новая статическая сигнатура).
3. Для profile lane без padding по fixture extension `padding(21)` должен отсутствовать.

## 5.2 Решение: TlsHelloContext с pre-sampled полями

### Важная корректировка padding-стратегии

Padding не должен быть «произвольным jitter в диапазоне». Для браузерной мимикрии нужен profile-driven режим:

- Chrome-like профили: BoringPaddingStyle-семантика (padding extension добавляется только если unpadded ClientHello в окне `(0xFF, 0x200)`).
- Профили, где hello стабильно > 512 байт (например, Chrome 131 с PQ key_share): padding extension, как правило, отсутствует.
- Safari-профиль: padding extension отсутствует.

Это устраняет старую сигнатуру fixed 517, но не создаёт новую искусственную сигнатуру «рандомный padding всегда есть».

Ключевая правка по дизайну: в PR-1 нельзя хранить в context «padding_target» как финальную длину ClientHello. Это двусмысленно и ломает расчёт, потому что BoringPaddingStyle возвращает **длину содержимого padding extension**, а не финальную длину всего hello. В `TlsHelloContext` должен храниться либо `padding_content_len`, либо `std::optional<size_t>` с длиной содержимого extension.

```cpp
// td/mtproto/stealth/TlsHelloBuilder.h — внутренний builder seam

class TlsHelloContext {
 public:
  // Expanded constructor: all random lengths pre-sampled exactly once.
  // CalcLength and Store must read from context, never sample independently.
  TlsHelloContext(size_t grease_size,
                  string domain,
                  size_t padding_content_len,  // BoringPaddingStyle content length; 0 => no padding extension
                  size_t ech_payload_len,      // pre-sampled: 144 + n*32, n in [0,3]
                  uint16_t pq_group_id,        // explicit serializer input; profile registry arrives in PR-2
                  uint16_t ech_enc_key_len)    // explicit length-prefixed ECH encapsulated key length
      : grease_(grease_size, '\0'),
        domain_(std::move(domain)),
        padding_content_len_(padding_content_len),
        ech_payload_len_(ech_payload_len),
        pq_group_id_(pq_group_id),
        ech_enc_key_len_(ech_enc_key_len) {
    Grease::init(grease_);
  }

  // Backward-compatible constructor for non-stealth call sites.
  // Keeps old behavior where optional stealth fields are disabled.
  TlsHelloContext(size_t grease_size, string domain)
      : TlsHelloContext(grease_size, std::move(domain),
                        /*padding_content_len=*/0,
                        /*ech_payload_len=*/0,
                        /*pq_group_id=*/0,
                        /*ech_enc_key_len=*/0) {
  }

  // Existing accessors preserved:
  char get_grease(size_t i) const;
  size_t get_grease_size() const;
  Slice get_domain() const;

  // New accessors:
  size_t get_padding_content_len() const noexcept { return padding_content_len_; }
  size_t get_ech_payload_len() const noexcept { return ech_payload_len_; }

  // Returns the PQ hybrid named group codepoint for this serializer instance.
  // Used in both supported_groups and key_share extensions — MUST match.
  uint16_t get_pq_group_id() const noexcept { return pq_group_id_; }

  // Returns the key_exchange_length for the PQ key share.
  // Current implementation uses 1184 ML-KEM bytes + 32 X25519 bytes = 1216.
  uint16_t get_pq_key_share_length() const noexcept { return 0x04C0; }

  // Declared ECH encapsulated-key length must equal the number of bytes written.
  uint16_t get_ech_enc_key_len() const noexcept { return ech_enc_key_len_; }

 private:
  string grease_;
  string domain_;
  size_t padding_content_len_;
  size_t ech_payload_len_;
  uint16_t pq_group_id_;
  uint16_t ech_enc_key_len_;
};
```

## 5.3 Новые Op::Type для ECH/PQ (EchPayload, EchEncKey, PqGroupId, PqKeyShare)

```cpp
// В TlsHello::Op::Type enum добавить:
EchPayload,   // per-connection ECH length из context
EchEncKey,    // length-prefixed ECH encapsulated key из context
PqGroupId,    // per-connection PQ named group codepoint из context (0x6399 или 0x11EC)
PqKeyShare,   // PQ key share header: group_id (2 bytes) + key_exchange_length (2 bytes)

// Новые factory методы (заменяют static ech_payload()):
static Op ech_payload_dynamic() {
  Op res;
  res.type = Type::EchPayload;
  return res;
}

static Op ech_enc_key() {
  Op res;
  res.type = Type::EchEncKey;
  return res;
}

static Op pq_group_id() {
  Op res;
  res.type = Type::PqGroupId;
  return res;
}

static Op pq_key_share() {
  Op res;
  res.type = Type::PqKeyShare;
  return res;
}

// УДАЛИТЬ:
// static Op ech_payload() { ... Random::fast(0,3)*32+144 ... }
```

ECH wire-инварианты для serializer (обязательны):

1. Внешний type-byte `outer=0x00` является частью wire-формата и не должен теряться при рефакторинге (`\x00\x00\x01\x00\x01` = outer + kdf + aead).
2. Поле `enc` должно быть length-prefixed через scope, а не через ручной literal `\x00\x20`, чтобы declared length всегда совпадала с фактическим числом байт.

```cpp
// ECH block concept: preserve outer+suite prefix and scope variable fields.
vector<Op>{
    Op::str("\xfe\x0d"),
    Op::begin_scope(),
    Op::str("\x00\x00\x01\x00\x01"),  // outer + kdf + aead
    Op::random(1),                         // config_id
    Op::begin_scope(),
      Op::ech_enc_key(),
    Op::end_scope(),
    Op::begin_scope(),
      Op::ech_payload_dynamic(),
    Op::end_scope(),
    Op::end_scope(),
};
```

## 5.4 Граница PR-1: ещё без profile registry

```cpp
// td/mtproto/stealth/Interfaces.h

struct NetworkRouteHints {
  // True when egress path is routed via RU ISP where ECH is actively blocked.
  bool is_ru_egress = false;
};

struct PaddingPolicy {
  // Boring-style behavior: add padding only in (255, 512) unpadded window.
  bool enabled = true;

  size_t compute_padding_content_len(size_t unpadded_len) const noexcept {
    if (!enabled) {
      return 0;
    }
    if (unpadded_len > 0xFF && unpadded_len < 0x200) {
      // Match BoringPaddingStyle: target 0x200 before extension framing.
      auto padding_len = 0x200 - unpadded_len;
      if (padding_len >= 5) {
        return padding_len - 4;  // ext type + ext len
      }
      return 1;
    }
    return 0;
  }
};

inline PaddingPolicy no_padding_policy() {
  PaddingPolicy p;
  p.enabled = false;
  return p;
}

// Per-connection CSPRNG factory used by production paths.
// Нельзя использовать process-wide singleton RNG для stealth shape decisions.
unique_ptr<IRng> make_connection_rng();

// PR-1 only removes hardcoded literals from the serializer.
// BrowserProfile -> PQ codepoint mapping is introduced in PR-2.
// Current single-lane Chromium-like builder still uses 0x11EC today.
// This constant exists only as a PR-1 transitional serializer input and MUST
// be replaced by profile-driven mapping in PR-2.
constexpr uint16_t kCurrentSingleLanePqGroupId = 0x11EC;
constexpr uint16_t kCorrectEchEncKeyLen = 32;  // Regression guard: declared enc length must stay equal to written X25519 bytes (32).

// Current serializer writes 1184 ML-KEM bytes + 32 X25519 bytes.
static_assert(1184 + 32 == 0x04C0, "PQ key share size must remain 1216 bytes");
```

Здесь важно разделить обязанности:

- PR-1 делает serializer parameter-driven и убирает singleton/length mismatch.
- PR-2 вводит `BrowserProfile`, snapshot-backed profile registry, `EchMode` и route-aware политику.
- GREASE test helper типа `sample_grease_value(IRng&)` должен жить в `test/stealth/TestHelpers.h`, а не в production `Interfaces.h`.

## 5.5 Изменения в CalcLength и Store

```cpp
// TlsHelloCalcLength::do_op — новые case:

case Type::EchPayload:
  CHECK(context);
  size_ += context->get_ech_payload_len();
  break;

case Type::EchEncKey:
  CHECK(context);
  size_ += context->get_ech_enc_key_len();
  break;

case Type::PqGroupId:
  CHECK(context);
  size_ += 2;  // uint16_t named group codepoint
  break;

case Type::PqKeyShare:
  CHECK(context);
  size_ += 4;  // group_id (2 bytes) + key_exchange_length (2 bytes)
  break;

case Type::Padding: {
  CHECK(context);
  auto pad_content_len = context->get_padding_content_len();
  if (pad_content_len == 0) break;
  if (pad_content_len > 0) {
    size_ += 4 + pad_content_len;  // ext type + ext len + zero-filled content
  }
  break;
}

// TlsHelloStore::do_op — новые case:

case Type::EchPayload:
  CHECK(context);
  Random::secure_bytes(dest_.substr(0, context->get_ech_payload_len()));
  dest_.remove_prefix(context->get_ech_payload_len());
  break;

case Type::EchEncKey:
  CHECK(context);
  if (context->get_ech_enc_key_len() == 32) {
    // Reuse X25519-style key generation path instead of raw random bytes.
    do_op(TlsHello::Op::key(), context);
  } else {
    Random::secure_bytes(dest_.substr(0, context->get_ech_enc_key_len()));
    dest_.remove_prefix(context->get_ech_enc_key_len());
  }
  break;

case Type::PqGroupId: {
  CHECK(context);
  uint16_t gv = context->get_pq_group_id();
  dest_[0] = static_cast<char>((gv >> 8) & 0xFF);
  dest_[1] = static_cast<char>(gv & 0xFF);
  dest_.remove_prefix(2);
  break;
}

case Type::PqKeyShare: {
  CHECK(context);
  uint16_t gid = context->get_pq_group_id();
  uint16_t klen = context->get_pq_key_share_length();
  dest_[0] = static_cast<char>((gid >> 8) & 0xFF);
  dest_[1] = static_cast<char>(gid & 0xFF);
  dest_[2] = static_cast<char>((klen >> 8) & 0xFF);
  dest_[3] = static_cast<char>(klen & 0xFF);
  dest_.remove_prefix(4);
  break;
}

case Type::Padding: {
  CHECK(context);
  auto pad_content_len = context->get_padding_content_len();
  if (pad_content_len == 0) break;
  if (pad_content_len > 0) {
    do_op(TlsHello::Op::str("\x00\x15"), nullptr);
    do_op(TlsHello::Op::begin_scope(), nullptr);
    do_op(TlsHello::Op::zero(pad_content_len), nullptr);
    do_op(TlsHello::Op::end_scope(), nullptr);
  }
  break;
}
```

## 5.6 Обновлённый generate_header

```cpp
// td/mtproto/stealth/TlsHelloBuilder.h
// PR-1 keeps the public API narrow and profile-agnostic.
string generate_header_with_context(string domain, Slice secret, int32 unix_time,
                                   const TlsHelloContext &context);

// Existing production call path stays compatible and builds a context internally
// from the current single template:
// - padding_content_len = BoringPaddingStyle(unpadded_len) or 0
// - ech_payload_len = 144 + n*32 sampled per connection
// - pq_group_id = kCurrentSingleLanePqGroupId (0x11EC) until PR-2 registry lands
// - ech_enc_key_len = kCorrectEchEncKeyLen (32)
string generate_header(string domain, Slice secret, int32 unix_time, stealth::IRng &rng);

// BrowserProfile-aware overload is introduced in PR-2, not here.
// TlsHello::get_default() remains only for non-stealth compatibility path
// and should be marked [[deprecated("use get_hello_for_profile for stealth")]].
```

## 5.7 TDD (красные тесты ДО кода)

```cpp
// test/stealth/test_context_entropy.cpp

TEST(ContextEntropy, PaddingAndEchSampledOnce) {
  MockRng rng(42);
  auto p = boring_padding_content_len(/*unpadded_len=*/300);
  size_t e = rng.bounded(4) * 32 + 144;
  TlsHelloContext ctx(7, "google.com", p, e, kCurrentSingleLanePqGroupId,
                      kCorrectEchEncKeyLen);
  EXPECT_EQ(ctx.get_padding_content_len(), p);
  EXPECT_EQ(ctx.get_ech_payload_len(), e);
  EXPECT_EQ(ctx.get_pq_group_id(), kCurrentSingleLanePqGroupId);
  EXPECT_EQ(ctx.get_ech_enc_key_len(), kCorrectEchEncKeyLen);
}

TEST(ContextEntropy, EchLengthVariesPerConnection) {
  MockRng rng(12345);
  std::set<size_t> lengths;
  for (int i = 0; i < 100; i++) {
    auto e = rng.bounded(4) * 32 + 144;
    lengths.insert(e);
  }
  // 4 possible values {144,176,208,240}: must see ≥2 in 100 draws.
  EXPECT_GE(lengths.size(), 2u);
}

TEST(ContextEntropy, CalcLengthAndStoreAreConsistent) {
  MockRng rng(1);
  for (int i = 0; i < 200; i++) {
    TlsHelloContext ctx(7, "google.com",
                        boring_padding_content_len(/*unpadded_len=*/300),
                        rng.bounded(4) * 32 + 144,
                        kCurrentSingleLanePqGroupId,
                        kCorrectEchEncKeyLen);
    TlsHelloCalcLength calc;
    for (auto &op : get_current_hello_template().get_ops())
      calc.do_op(op, &ctx);
    auto length = calc.finish().move_as_ok();

    string buf(length, '\0');
    TlsHelloStore store(buf);
    for (auto &op : get_current_hello_template().get_ops())
      store.do_op(op, &ctx);

    // Buffer must be exactly filled — no overflow, no underrun.
    EXPECT_EQ(store.get_offset(), length) << "Mismatch at i=" << i;
  }
}

TEST(ContextEntropy, AllOpTypesHandledWithoutUnreachable) {
  MockRng rng(77);
  TlsHelloContext ctx(7, "google.com",
                      /*padding_content_len=*/0,
                      /*ech_payload_len=*/176,
                      kCurrentSingleLanePqGroupId,
                      kCorrectEchEncKeyLen);

  // Build an op list that touches newly introduced op types.
  vector<TlsHello::Op> ops = {
      TlsHello::Op::pq_group_id(),
      TlsHello::Op::pq_key_share(),
      TlsHello::Op::ech_enc_key(),
      TlsHello::Op::ech_payload_dynamic(),
      TlsHello::Op::padding()};

  TlsHelloCalcLength calc;
  for (auto &op : ops) {
    calc.do_op(op, &ctx);
  }
  auto len = calc.finish().move_as_ok();

  string buf(len, '\0');
  TlsHelloStore store(buf);
  for (auto &op : ops) {
    store.do_op(op, &ctx);
  }
  EXPECT_EQ(store.get_offset(), len);
}

TEST(ContextEntropy, PqGroupAppearsInBothGroupsAndKeyShare) {
  // Regression: explicit pq_group_id must land in BOTH supported_groups and key_share.
  MockRng rng(42);
  TlsHelloContext ctx(7, "google.com", /*padding_content_len=*/0,
                      /*ech_payload_len=*/176, kCurrentSingleLanePqGroupId,
                      kCorrectEchEncKeyLen);
  auto h = generate_header_with_context("google.com", test_secret, unix_now, ctx);
  auto groups = extract_supported_groups(h);
  auto key_shares = extract_key_share_groups(h);
  uint16_t pq_codepoint = kCurrentSingleLanePqGroupId;
  bool in_groups = std::find(groups.begin(), groups.end(), pq_codepoint) != groups.end();
  bool in_keyshare = std::find(key_shares.begin(), key_shares.end(), pq_codepoint) != key_shares.end();
  EXPECT_EQ(in_groups, in_keyshare) << "PQ group must be in both or neither";
}

TEST(ContextEntropy, GreaseValuesStillValidRfc8701) {
  // GREASE values (from Grease::init) are separate from PQ group codepoints.
  // Verify existing GREASE generation is still correct.
  MockRng rng(99);
  for (int i = 0; i < 1000; i++) {
    uint16_t gv = sample_grease_value(rng);
    uint8_t lo = gv & 0xFF;
    uint8_t hi = (gv >> 8) & 0xFF;
    EXPECT_EQ(lo, hi) << "GREASE byte mismatch at i=" << i;
    EXPECT_EQ((lo - 0x0A) % 0x10, 0u) << "Not a valid GREASE byte at i=" << i;
  }
}

TEST(ContextEntropy, NoForcedPadding517Regression) {
  MockRng rng(7);
  size_t count_517 = 0;
  for (int i = 0; i < 200; i++) {
    auto h = generate_header("google.com", test_secret, unix_now, rng);
    if (h.size() == 517u) {
      count_517++;
    }
  }
  // Old behavior: always 517. New behavior: never hard-forced to 517.
  EXPECT_LT(count_517, 200u);
}

TEST(ContextEntropy, CurrentTemplatePaddingExtensionAbsentOutsideBoringWindow) {
  MockRng rng(11);
  auto h = generate_header("google.com", test_secret, unix_now, rng);
  EXPECT_FALSE(has_extension(h, 0x0015))
      << "Current template should not force padding when unpadded ClientHello is outside Boring window";
}

TEST(ContextEntropy, LightweightProfileAddsPaddingInsideBoringWindow) {
  // Positive case for RFC 7685-style windowing: lightweight profile without PQ
  // should carry extension 0x0015 when unpadded ClientHello falls into (255, 512).
  MockRng rng(13);
  auto h = generate_header_for_profile_test(BrowserProfile::Chrome120, test_secret, unix_now, rng);
  ASSERT_TRUE(unpadded_client_hello_len_in_window(h, 0xFF, 0x200));
  EXPECT_TRUE(has_extension(h, 0x0015));
  EXPECT_TRUE(padding_extension_payload_is_zero(h));
}
```

Профильно-зависимые тесты (`BrowserProfile`, `0xFE0D`, `0x4469/0x44CD` по fixture, RU/non-RU policy, Safari/Firefox-specific assertions) должны оставаться в PR-2. PR-1 тестирует только serializer/context invariants и red-regressions текущего шаблона.

## 5.5 Что реально уже закрыто кодом и тестами

На текущей ветке PR-1 фактически закрыт следующими файлами:

- `td/mtproto/stealth/TlsHelloBuilder.cpp`: `sample_ech_payload_length()`, `sample_padding_entropy_length()`, `should_enable_ech()`, `TlsHelloContext`, context-driven `Type::EchPayload` / `Type::Padding` storage path.
- `test/stealth/test_tls_hello_wire.cpp`: structural wire invariants и anti-fixed-length regressions.
- `test/stealth/test_tls_route_policy.cpp` и `test/stealth/test_tls_route_policy_stress.cpp`: route-aware ECH fail-closed behavior и anti-collapse distribution checks.
- `test/stealth/test_tls_builder_route_seam.cpp`: explicit options cannot force ECH on fail-closed routes.
- `test/stealth/test_tls_context_entropy.cpp`: explicit PQ/ECH/padding serializer invariants.
- `test/stealth/test_tls_extension_order_policy.cpp`: anchored shuffle regression checks and non-deterministic RenegotiationInfo placement.

Что всё ещё не закрыто на уровне PR-1:

- default single-lane builder path больше не является финальной browser-mimicry целью; verified multi-profile runtime теперь живёт в PR-2.
- padding entropy fallback для ECH-disabled lane остаётся временной anti-collapse мерой, а не fixture-proven browser baseline.

---

# 6. PR-2: Browser Profile Registry

**Зависит от:** PR-1  
**Исправляет:** S3, S4, S7, S9, S10, S11, S20, S21

## 6.0 Реальный статус на текущей ветке (2026-04-06)

PR-2 **реализован в основном production scope**, но не полностью завершён как release-grade capture program. В коде уже есть:

- `BrowserProfile`, `ProfileSpec`, `ProfileFixtureMetadata`, `RuntimePlatformHints`, `EchMode`, `ExtensionOrderPolicy`.
- snapshot-backed profile registry в `TlsHelloProfileRegistry.cpp` с verified Chromium lanes (`Chrome133`, `Chrome131`, `Chrome120`) и verified `Firefox148`, plus advisory `Safari26_3` / `IOS14` / `Android11_OkHttp_Advisory`.
- sticky runtime profile selection with platform filtering (`allowed_profiles_for_platform`, `pick_profile_sticky`, `pick_runtime_profile`).
- RU/unknown default-off ECH route policy plus runtime circuit-breaker state and counters.
- runtime wiring in `TlsInit.cpp`: actual production ClientHello path now uses selected runtime profile plus route-aware ECH decision.

Ограничение остаётся тем же, что и в предыдущих аудиторских заметках: Safari/mobile profiles уже присутствуют в registry, но пока сохраняют advisory status из-за отсутствия independent network-derived fixture closure в самом repo state.

## 6.1 Аудит PR-2 (что было не так)

Критические проблемы исходного текста PR-2:

1. Профили были partially hand-crafted, а не snapshot-driven. Это создаёт drift между ciphers/extensions/order и резко повышает детектируемость.
2. Были внутренние противоречия: в Safari-блоке одновременно заявлено "без padding", но указан `Op::padding()`.
3. Версии профилей (`Firefox128`, `Safari iOS 17`) не подтверждаются bundled uTLS snapshot в `docs/Samples/utls-code` (там есть `HelloFirefox_148`, `HelloSafari_26_3`).
4. `pick_random_profile()` не имел sticky-семантики и мог менять "браузер" на каждом новом TCP-соединении, что аномально для реального клиента.
5. Route-policy для ECH была слишком грубой (`is_ru_egress` bool) без circuit breaker и без fallback-поведения при ошибочной классификации маршрута.
6. В примере `build_chrome131_hello()` использовался `ech_mode`, но параметр функции не был объявлен (псевдокод несогласован).
7. Тесты содержали потенциально ложные hard-assert инварианты (например, blanket "SafariHasNo3Des"), которые должны быть fixture-driven, а не предположениями.

## 6.2 Принцип PR-2: только snapshot-driven registry

PR-2 должен строить ClientHello не из "набора вручную написанных кусочков", а из **реестра профилей** с source-of-truth:

- bundled uTLS snapshots: `docs/Samples/utls-code/u_parrots.go`, `u_ech.go`, `u_tls_extensions.go`;
- capture fixtures из `docs/Samples/Traffic dumps/*.pcap*`;
- **curl_cffi programmatic captures (V7 — НОВЫЙ источник):** `docs/Samples/scrapy-impersonate/` использует `curl_cffi` с реальными BoringSSL Chrome-профилями. Текущий эталонный инструмент в repo — `docs/Samples/scrapy-impersonate/capture_chrome131.py`; несмотря на историческое имя файла, он уже принимает `--profile` и умеет снимать `chrome131`, `chrome133`, `firefox135`, `safari18_0`, `edge131` через один и тот же локальный intercept workflow. Это хороший автоматизируемый source, но он должен входить в fixture update workflow **только вместе** с pinned version/provenance и cross-check против pcap/uTLS snapshot;
- текущий production wire-формат в `td/mtproto/TlsInit.cpp` (для regressions).

Rule: если конкретный инвариант не подтверждён snapshot/capture/curl_cffi, он не фиксируется как обязательный.

V7 release-priority note по надёжности ClientHello:

- verified rollout должен идти **Chromium-first**: сначала `chrome133_like`, затем `chrome131_like`, затем `chrome120_like`;
- причина не в «любви к Chrome», а в качестве локальной доказательной базы: bundled uTLS уже содержит `HelloChrome_133/131/120`, `capture_chrome131.py --profile` покрывает `chrome133/chrome131/edge131`, а текущий runtime single-lane builder структурно ближе именно к Chromium-family (`ChromeShuffleAnchored`, `PQ=0x11EC`, `ALPN=h2/http/1.1`, текущий literal `ALPS=0x44CD` ближе к `Chrome133` family, чем к Firefox/Safari);
- `Firefox148`, `Safari26_3`, `IOS14`, `Android11_OkHttp_Advisory` могут жить в registry, но до появления реальных network-derived fixture ids остаются advisory и не должны конкурировать с verified Chromium lane за release verdict;
- `edge131` из `curl_cffi` полезен как corroborating Chromium-family capture, но не должен автоматически становиться отдельным runtime-profile без независимого browser-capture/snapshot triage.

### 6.2.1 Source Precedence И Provenance Rules

Ниже — жёсткая иерархия доверия для PR-2, чтобы registry не смешивал независимые источники как будто они эквивалентны:

1. `browser_capture`: живой сетевой capture из `pcap/pcapng` с parser-level extraction ClientHello. Это основной network-derived baseline.
2. `curl_cffi_capture`: программный network-derived capture через `curl_cffi`/BoringSSL профиль и локальный intercept workflow. Это допустимый baseline только при pinned version/provenance и cross-check против как минимум одного независимого источника.
3. `utls_snapshot`: code snapshot bundled uTLS (`HelloChrome_*`, `HelloFirefox_*`, `ApplicationSettingsExtension*`, `BoringPaddingStyle`, `ShuffleChromeTLSExtensions`). Это authoritative source для **структуры профиля и policy semantics**, но не самодостаточный network baseline.
4. `advisory_code_sample`: `docs/Samples/xray-core-code/*`, synthetic code snippets и прочие non-wire reference материалы. Они допустимы только как design/review input и **никогда** не входят в release-gating fixture set для ClientHello.

Обязательные правила:

1. Каждый release-gating profile обязан иметь минимум **один network-derived fixture** (`browser_capture` или `curl_cffi_capture`) и минимум **один независимый corroborating source** (`utls_snapshot` или второй network-derived fixture из другого capture path).
2. Profile, построенный только на `utls_snapshot`, считается advisory: он может жить в registry, но не участвует в release verdict.
3. Profile, построенный только на `curl_cffi_capture` без pcap или uTLS corroboration, тоже advisory: этого недостаточно для fail-closed policy.
4. `frame contains`, hex grep и иные byte-scan эвристики не считаются provenance-grade extraction. Для network-derived fixtures обязателен parser-level разбор с pinned `parser_version`.
5. `docs/Samples/xray-core-code/*` и любые transport appearance wrappers (`finalmask`, `sudoku`) запрещено использовать как источник ClientHello fixture IDs, JA3/JA4 allow-sets или ALPS/ECH/PQ expectations.

Обязательные provenance-поля по source_kind:

1. `browser_capture`: `source_path`, `source_sha256`, `parser_version`, `capture_date_utc`, `frame_ref` или `display_filter`, `transport`, `tls_handshake_type`.
2. `curl_cffi_capture`: все поля из `browser_capture` плюс `capture_tool`, `capture_tool_version`, `curl_cffi_version`, `browser_type`, `target_host`, `intercept_method`.
3. `utls_snapshot`: `source_path`, `source_sha256`, `snapshot_date`, `hello_id`, `extension_variant` (например `ApplicationSettingsExtension` vs `ApplicationSettingsExtensionNew`), `pq_variant`.
4. `advisory_code_sample`: `source_path`, `source_sha256`, `advisory_reason`; trust tier не может быть выше `advisory`.

### 6.2.2 Reproducible ClientHello Extraction And Merge Workflow

Чтобы PR-2 fixture refresh не зависел от ручного copy-paste из `tshark` и не дрейфовал между reviewer machines, текущая ветка использует отдельный reproducible pipeline:

1. `test/analysis/extract_client_hello_fixtures.py`
  Парсит `pcap/pcapng` через `tshark` + repo-local ClientHello parser и создаёт immutable JSON artifact с pinned `parser_version`, `source_sha256`, `capture_date_utc`, `display_filter`, `transport`, `tls_handshake_type` и parser-level derived fields (`cipher_suites`, `supported_groups`, `key_share_entries`, `extension_types`, `compress_certificate_algorithms`, `ech`).
2. `test/analysis/diff_client_hello_fixtures.py`
  Сравнивает regenerated artifact с frozen corpus и выводит provenance-delta report: changed top-level metadata, added/removed fixture ids, changed payload fields и `sha256` drift.
3. `test/analysis/fixtures/clienthello/linux_desktop/*.json`
  Reviewed Linux desktop browser-capture corpus (`chrome144`, `chrome146.75`, `chrome146.177`, `tdesktop6.7.3`, `firefox148`, `firefox149`). Каждый JSON = один immutable input capture.
4. `test/analysis/merge_client_hello_fixture_summary.py`
  Сводит frozen JSON corpus в единый reviewed summary header `test/stealth/ReviewedClientHelloFixtures.h`.
5. PR-2 capture-driven tests
  `test/stealth/test_tls_capture_chrome144_differential.cpp` и `test/stealth/test_tls_capture_firefox148_differential.cpp` больше не должны хранить capture-derived literals вручную; они читают reviewed constants из `ReviewedClientHelloFixtures.h`.

Норматив refresh workflow:

```bash
python3 test/analysis/extract_client_hello_fixtures.py \
  --pcap "docs/Samples/Traffic dumps/Linux, desktop/clienthello-chrome144.0.7559.109-ubuntu24.04.pcapng" \
  --profile-id chrome144_linux_desktop \
  --source-kind browser_capture \
  --scenario-id linux_desktop_chrome144_clienthello \
  --device-class desktop \
  --os-family linux \
  --route-mode non_ru_egress \
  --out /tmp/chrome144_linux_desktop.clienthello.json

python3 test/analysis/diff_client_hello_fixtures.py \
  --old test/analysis/fixtures/clienthello/linux_desktop/chrome144_linux_desktop.clienthello.json \
  --new /tmp/chrome144_linux_desktop.clienthello.json

python3 test/analysis/merge_client_hello_fixture_summary.py \
  --input-dir test/analysis/fixtures/clienthello/linux_desktop \
  --out-header test/stealth/ReviewedClientHelloFixtures.h
```

Fail-closed правила для этого pipeline:

1. Frozen corpus JSON не обновляется без provenance-diff review.
2. `ReviewedClientHelloFixtures.h` считается derived artifact и обязан быть regenerated после любого изменения frozen corpus.
3. Ручное редактирование capture-derived literals в PR-2 tests запрещено; source of truth = frozen JSON corpus + merge step.
4. Если `parser_version` меняется, reviewer обязан видеть provenance-delta note, потому что меняется contract extraction semantics, а не просто sample content.

## 6.3 Файлы

```
td/mtproto/stealth/
  TlsHelloProfile.h             enum BrowserProfile + ProfileSpec API
  TlsHelloProfileRegistry.cpp   snapshot-backed registry + sticky selection
  TlsHelloProfileRegistry.h     lookup/profile selection contracts

test/stealth/
  test_browser_profiles.cpp     fixture-driven profile tests
  ProfileFixtures.h             source snapshot metadata + expected invariants
```

## 6.4 BrowserProfile (исправленный)

```cpp
// td/mtproto/stealth/TlsHelloProfile.h
namespace td::mtproto::stealth {

// IMPORTANT: enum values must map to verified snapshot fixtures.
enum class BrowserProfile : uint8_t {
  Chrome133,
  Chrome131,
  Chrome120,
  Firefox148,
  Safari26_3,
  IOS14,
  Android11_OkHttp_Advisory,
};

enum class DeviceClass : uint8_t {
  Desktop,
  Mobile,
};

enum class MobileOs : uint8_t {
  None,
  IOS,
  Android,
};

enum class DesktopOs : uint8_t {
  Unknown,
  Darwin,
  Windows,
  Linux,
};

struct RuntimePlatformHints {
  DeviceClass device_class{DeviceClass::Desktop};
  MobileOs mobile_os{MobileOs::None};
  DesktopOs desktop_os{DesktopOs::Unknown};
  bool is_ios{false};
  bool is_android{false};
};

enum class EchMode : uint8_t {
  Disabled = 0,      // default for RU egress and for blocked routes
  Rfc9180Outer = 1   // 0xFE0D per RFC 9180 (finalized ECH outer extension type)
};

// Backward-compatibility note for config parser:
// JSON string value "grease_draft17" is accepted as an alias of EchMode::Rfc9180Outer.

enum class ExtensionOrderPolicy : uint8_t {
  FixedFromFixture = 0,
  // Full random shuffle of all non-GREASE/non-padding/non-PSK extensions.
  // Matches Chrome's ShuffleChromeTLSExtensions (since Chrome 106).
  // NO positional windows — RenegotiationInfo is included in the shuffled pool.
  ChromeShuffleAnchored = 1,
};

struct ProfileSpec {
  BrowserProfile id;
  Slice name;
  uint16_t alps_type;
  bool allows_ech;
  bool allows_padding;
  bool has_session_id;
  ExtensionOrderPolicy extension_order_policy;
};

const ProfileSpec &profile_spec(BrowserProfile profile);

// Sticky profile selection: stable for (secret, destination, day-bucket),
// not randomized independently for every TCP connection.
BrowserProfile pick_profile_sticky(const ProfileWeights &weights,
                                   const SelectionKey &key,
                                   const RuntimePlatformHints &platform,
                                   Span<const BrowserProfile> allowed_profiles,
                                   IRng &rng);

EchMode ech_mode_for_route(const NetworkRouteHints &route,
                           const RouteFailureState &state) noexcept;

}  // namespace td::mtproto::stealth
```

Явное соответствие snapshot IDs (обязательно для drift-free реализации):

- `BrowserProfile::Chrome133` -> `HelloChrome_133`
- `BrowserProfile::Chrome131` -> `HelloChrome_131`
- `BrowserProfile::Chrome120` -> `HelloChrome_120`
- `BrowserProfile::Firefox148` -> `HelloFirefox_148`
- `BrowserProfile::Safari26_3` -> `HelloSafari_26_3`
- `BrowserProfile::IOS14` -> `HelloIOS_14`
- `BrowserProfile::Android11_OkHttp_Advisory` -> `HelloAndroid_11_OkHttp`

ALPS codepoint задаётся fixture-значением профиля, а не глобальным правилом. Для текущего runtime-set в V6:

- Chrome133: fixture-driven (в текущем bundled uTLS snapshot — `0x44CD`, `ApplicationSettingsExtensionNew`)
- Chrome131/Chrome120: fixture-driven (в текущем bundled uTLS snapshot — `0x4469`, `ApplicationSettingsExtension`)
- Firefox148/Safari26_3/IOS14/Android11_OkHttp_Advisory: fixture-driven (может отсутствовать)
- В одном ClientHello запрещено одновременно иметь и `0x4469`, и `0x44CD`.

Требование к provenance для ALPS policy:

1. Значение `alps_type` может попасть в release-gating registry только если оно подтверждено network-derived fixture и согласовано с `utls_snapshot` для той же profile family.
2. Если `curl_cffi_capture` и `browser_capture` расходятся по ALPS type, профиль переводится в advisory до ручного triage; автоматический выбор одного из значений запрещён.
3. Старый и новый ALPS codepoint (`0x4469` / `0x44CD`) трактуются как разные fixture families, пока provenance явно не докажет обратное.

**Важное замечание по PQ codepoint для Chrome 120 (V7):**
- `BrowserProfile::Chrome120` → `HelloChrome_120` (non-PQ): **NO PQ key share**, только X25519.
- Если когда-либо будет добавлен вариант Chrome120 с PQ, он обязан использовать `X25519Kyber768Draft00 = 0x6399` (Kyber draft), **НЕ** `0x11EC` (ML-KEM финальный). Chrome 131 — первый профиль, где PQ group переключился на `0x11EC`.
- Эта граница должна быть зафиксирована в `ProfileFixtures.h` с явным комментарием.

Extension-order policy (audit-fix для пунктов S6/S24/S25 — V7 исправление):

- Историческая проблема, уже исправленная на текущей ветке: non-Darwin ветка использовала `Op::permutation` с `shuffle_fixture_bounded_windows` (окна по 4 элемента, хвостовой якорь на RenegotiationInfo `\xff\x01`).
- **Это НЕПРАВИЛЬНО для Chrome-профилей.** `ShuffleChromeTLSExtensions` из bundled uTLS — это **полный случайный shuffle** (не windows): перемешиваются все расширения, кроме строго трёх якорей: `UtlsGREASEExtension` (первый и последний), `UtlsPaddingExtension`, `PreSharedKeyExtension`. RenegotiationInfo (`\xff\x01`) **НЕ является якорем** и должен перемешиваться в пул.
- Правильная policy для `Chrome*` профилей — `ChromeShuffleAnchored`: полный random shuffle всех расширений кроме GREASE/padding/PSK якорей. Реализация должна воспроизводить `ShuffleChromeTLSExtensions`, а не использовать 4-элементные окна.
- Для `Firefox*`, `Safari*`, `IOS*`, `Android*` и legacy capture-families: `FixedFromFixture` (без shuffle, порядок из fixture).
- PR-9 smoke обязан fail-closed: extension-order hash каждого сэмпла должен входить в allow-set конкретного профиля/fixture-family. Для Chrome-профилей allow-set = все перестановки non-anchor расширений.

## 6.5 Выбор профиля: sticky + weighted + platform-coherent

```cpp
// Weights must always sum to 100. No implicit remainder routing.
// Default mix is conservative and can be overridden in PR-8 runtime params.
// Selection must be class-stable: desktop profiles never rotate into mobile profiles
// and vice versa within one runtime installation bucket.
// Selection must also be OS-coherent inside desktop class:
// Safari26_3 is allowed only on Darwin desktop hints.

// Desktop baseline examples:
// Darwin: Chrome133=35, Chrome131=25, Chrome120=10, Safari26_3=20, Firefox148=10.
// Non-Darwin: Chrome133=50, Chrome131=20, Chrome120=15, Safari26_3=0, Firefox148=15.
// Mobile baseline example:
// IOS14=70, Android11_OkHttp_Advisory=30.

BrowserProfile pick_profile_sticky(const ProfileWeights &w,
                                   const SelectionKey &key,
                                   const RuntimePlatformHints &platform,
                                   Span<const BrowserProfile> allowed,
                                   IRng &rng) {
  // selection hash must be stable for key to avoid profile flapping
  uint32_t h = stable_hash32(key);
  uint32_t roll = h % 100;

  if (platform.device_class == DeviceClass::Mobile) {
    // Class guard: mobile runtime uses only mobile snapshot set.
    if (roll < w.ios14) return BrowserProfile::IOS14;
    return BrowserProfile::Android11_OkHttp_Advisory;
  }

  // Class guard: desktop runtime uses only desktop snapshot set,
  // plus OS guard to avoid impossible profile/platform combinations.
  if (platform.desktop_os == DesktopOs::Darwin) {
    if (roll < w.darwin.chrome133) return BrowserProfile::Chrome133;
    if (roll < w.darwin.chrome133 + w.darwin.chrome131) return BrowserProfile::Chrome131;
    if (roll < w.darwin.chrome133 + w.darwin.chrome131 + w.darwin.chrome120) {
      return BrowserProfile::Chrome120;
    }
    if (roll < w.darwin.chrome133 + w.darwin.chrome131 + w.darwin.chrome120 + w.darwin.safari26_3) {
      return BrowserProfile::Safari26_3;
    }
    return BrowserProfile::Firefox148;
  }

  // Non-Darwin desktop: Safari is forbidden by policy.
  if (roll < w.non_darwin.chrome133) return BrowserProfile::Chrome133;
  if (roll < w.non_darwin.chrome133 + w.non_darwin.chrome131) return BrowserProfile::Chrome131;
  if (roll < w.non_darwin.chrome133 + w.non_darwin.chrome131 + w.non_darwin.chrome120) {
    return BrowserProfile::Chrome120;
  }
  return BrowserProfile::Firefox148;
}
```

Требования к sticky-key:

- ключ включает destination/SNI, device_class, os-family и time-bucket (например, сутки), чтобы не было "прыжков" профиля между соседними соединениями;
- ключ не содержит сырых секретов в логах и метриках;
- одинаковый ключ => одинаковый профиль в пределах bucket.
- cross-class rotation запрещён: desktop<->mobile переключение внутри одной установки допустимо только при явной смене platform hints.
- OS-guard обязателен: Safari26_3 разрешён только при `desktop_os=Darwin`; mobile профили разрешены только при `device_class=Mobile`.

## 6.6 Route-aware ECH policy с circuit breaker

```cpp
EchMode ech_mode_for_route(const NetworkRouteHints &route,
                           const RouteFailureState &state) noexcept {
  // Hard rule: RU egress -> ECH disabled by default.
  if (route.is_ru_egress) {
    return EchMode::Disabled;
  }

  // Circuit breaker: if recent failures indicate ECH-related breakage,
  // temporarily disable ECH even on non-RU routes.
  if (state.ech_block_suspected || state.recent_ech_failures >= 3) {
    return EchMode::Disabled;
  }

  return EchMode::Rfc9180Outer;
}
```

Источник `NetworkRouteHints` в V6 должен быть зафиксирован явно:

- базовый источник: `active_policy` из PR-8 (`unknown|ru_egress|non_ru_egress`), управляется оператором;
- опциональный источник: внешний trusted route-hint провайдер (out-of-process), если подключён;
- при отсутствии trusted hint по умолчанию используется `unknown` (fail-safe, ECH disabled).

`RouteFailureState` должен быть персистентен между соединениями (TTL-based cache), а не жить только в рамках одного соединения. Минимальный ключ: `(destination/SNI, policy_bucket)`. Без этого circuit breaker не работает в реальной эксплуатации.

Операционные требования:

- QUIC/HTTP3 в PR-2 не имитируется и не включается (TCP+TLS only);
- ECH-политика должна иметь telemetry counters (enabled/disabled/fallback), без логирования секретов.

### 6.6.1 Статус реализации по реальному коду (2026-04-05)

На текущем `td/**` route-aware ECH policy всё ещё **policy-level**, не runtime-ready:

- в `td/mtproto/TlsInit.cpp` ECH extension (`0xFE0D`) зашит в шаблон default ClientHello и не зависит от route hints;
- в `td/mtproto/TlsInit.cpp` отсутствуют сущности вида `EchMode/RouteHints/active_policy`; в `wait_hello_response()` нет route-aware fallback/circuit-breaker логики;
- в `td/telegram/net/ConnectionCreator.cpp` при `transport_type.secret.emulate_tls()` в `TlsInit` передаются только domain+secret, без route metadata;
- в `td/mtproto/ProxySecret.h` есть только `emulate_tls()` и `get_domain()`, но нет route-policy state.

Следствие: формулировка «RU/unknown -> ECH off, non-RU -> controlled ECH» пока является требованием плана, а не уже внедрённым runtime-поведение.

### 6.6.2 Обязательная runtime-верификация (до закрытия PR-8/PR-9)

Минимальный acceptance-контракт для «реального runtime», а не только policy текста:

1. Для одной и той же установки/ключа маршрута прогоняется последовательность `unknown -> non_ru_egress -> ru_egress` с сохранением TTL-state между соединениями.
2. В `unknown` и `ru_egress` во всех сэмплах отсутствуют и `0xFE0D`, и `0xFE02`.
3. В `non_ru_egress` допускается только `0xFE0D` (никогда `0xFE02`) и только для profile lane, где ECH разрешён registry.
4. При инъекции ECH-related ошибок (>=3 подряд) circuit breaker переводит non-RU lane в `ECH disabled` на TTL-интервал; после TTL допускается controlled re-enable.
5. Любой запуск без route metadata (`unknown route origin`) считается fail-safe: ECH disabled.
6. Smoke verdict недействителен, если не приложены counters: `ech_enabled_total`, `ech_disabled_route_total`, `ech_disabled_cb_total`, `ech_reenabled_total`.

### 6.6.3 Нормативная семантика circuit-breaker (обязательная)

Чтобы не оставлять поведение на «додумывание реализации», вводится фиксированный контракт:

1. Событие `ech_failure` инкрементируется только для ECH-related ошибок текущего lane:
  - TCP reset/close после отправки ClientHello и до валидации ответа.
  - Timeout ожидания ответа в `wait_hello_response()`.
  - TLS alert/fatal в ответе, связанный с handshake rejection.
  - Parser reject в PR-10 (`unknown server-hello layout` для текущего profile family).
2. Не считаются `ech_failure`: DNS ошибки до установления сокета, локальная отмена запроса, и общесистемные сетевые ошибки до фактической отправки ClientHello.
3. Threshold и TTL берутся только из runtime params (`ech_fail_open_threshold`, `ech_disable_ttl_sec`) и применяются по ключу `(destination/SNI, policy_bucket)`.
4. TTL-state хранится в персистентном кеше между соединениями и перезапусками процесса; volatile in-memory cache допускается только как ускоряющий слой поверх persistent snapshot.
5. По истечении TTL выполняется controlled probe: первая попытка re-enable ограничена одним соединением; при повторном `ech_failure` lane немедленно возвращается в disabled на новый TTL.

## 6.7 Build API (исправленный, без псевдо-багов)

```cpp
// Function signature explicitly carries mode/spec/context.
static TlsHello build_hello_for_profile(const ProfileSpec &spec,
                                        EchMode ech_mode,
                                        const TlsHelloContext &ctx) {
  // 1) start from snapshot-backed extension/cipher ordering
  // 2) patch only dynamic fields: GREASE, keys, domain, ECH payload lengths
  // 3) if ech_mode == Disabled: remove ECH extension entirely
  // 4) padding only if spec.allows_padding and profile policy says so
}
```

Обязательные инварианты при сборке:

- `supported_groups` и `key_share` используют один и тот же PQ group из registry;
- ECH type только `0xFE0D` когда ECH включён;
- ECH encapsulated-key declared length == фактически записанные байты;
- ALPS type берётся из profile fixture (`0x4469`/`0x44CD` строго по snapshot), не из глобального if-правила;
- отсутствие "магических" literal-ов, дублирующих один и тот же codepoint в нескольких местах.

## 6.8 Safari/Firefox/Mobile policy (исправленная)

1. Не фиксировать blanket-утверждения вроде "Safari всегда без PQ" или "Safari всегда без 3DES" без capture-подтверждения.
2. Для профилей Safari/Firefox/Mobile инварианты должны быть fixture-driven и версионно-явными:
   - `Safari26_3` (из bundled uTLS snapshot) проверяется по его fixture;
  - `IOS14` и `Android11_OkHttp_Advisory` используются только в mobile class и только при валидных capture-fixtures;
   - если нужен отдельный `SafariIos17`, сначала добавить pcap fixture и только потом включать в weighted selection.
3. Исключить "legacy" профиль из runtime enum (никаких `FirefoxLegacy`), чтобы нельзя было случайно активировать его на проде.

## 6.9 TDD для PR-2 (обновлённый)

```cpp
// test/stealth/test_browser_profiles.cpp

TEST(ProfileRegistry, SourceOfTruthIsSnapshotBacked) {
  // Registry must expose metadata with source id (utls/capture), not ad-hoc structs.
  for (auto p : all_profiles()) {
    auto m = fixture_metadata(p);
    EXPECT_FALSE(m.source_id.empty());
    EXPECT_TRUE(m.is_verified);
  }
}

TEST(ProfileRegistry, ProfileSelectionIsSticky) {
  SelectionKey k{/*same destination + same day bucket*/};
  MockRng rng(42);
  RuntimePlatformHints platform{/*desktop*/};
  auto p1 = pick_profile_sticky(default_weights(), k, platform, allowed_desktop_profiles(), rng);
  auto p2 = pick_profile_sticky(default_weights(), k, platform, allowed_desktop_profiles(), rng);
  EXPECT_EQ(p1, p2);
}

TEST(ProfileSelection, MobileClassUsesOnlyMobileProfiles) {
  SelectionKey k{/*same destination + same day bucket*/};
  MockRng rng(7);
  RuntimePlatformHints platform;
  platform.device_class = DeviceClass::Mobile;
  for (int i = 0; i < 100; i++) {
    auto p = pick_profile_sticky(default_weights(), k, platform, allowed_mobile_profiles(), rng);
    EXPECT_TRUE(p == BrowserProfile::IOS14 || p == BrowserProfile::Android11_OkHttp_Advisory);
  }
}

TEST(ProfileSelection, DesktopClassNeverUsesMobileProfiles) {
  SelectionKey k{/*same destination + same day bucket*/};
  MockRng rng(9);
  RuntimePlatformHints platform;
  platform.device_class = DeviceClass::Desktop;
  platform.desktop_os = DesktopOs::Linux;
  for (int i = 0; i < 100; i++) {
    auto p = pick_profile_sticky(default_weights(), k, platform, allowed_desktop_profiles(), rng);
    EXPECT_FALSE(p == BrowserProfile::IOS14 || p == BrowserProfile::Android11_OkHttp_Advisory);
  }
}

TEST(ProfileSelection, NonDarwinDesktopNeverUsesSafari) {
  SelectionKey k{/*same destination + same day bucket*/};
  MockRng rng(11);
  RuntimePlatformHints platform;
  platform.device_class = DeviceClass::Desktop;
  platform.desktop_os = DesktopOs::Windows;
  for (int i = 0; i < 200; i++) {
    auto p = pick_profile_sticky(default_weights(), k, platform, allowed_desktop_profiles(), rng);
    EXPECT_FALSE(p == BrowserProfile::Safari26_3);
  }
}

TEST(ProfilePolicy, RuRouteDisablesEch) {
  NetworkRouteHints route;
  route.is_ru_egress = true;
  RouteFailureState st;
  EXPECT_EQ(ech_mode_for_route(route, st), EchMode::Disabled);
}

TEST(ProfilePolicy, CircuitBreakerDisablesEchAfterFailures) {
  NetworkRouteHints route;
  route.is_ru_egress = false;
  RouteFailureState st;
  st.recent_ech_failures = 3;
  EXPECT_EQ(ech_mode_for_route(route, st), EchMode::Disabled);
}

TEST(ProfileWire, ChromiumEchProfilesUseFe0dNotFe02) {
  for (auto p : {BrowserProfile::Chrome133, BrowserProfile::Chrome131}) {
    auto h = generate_header_test(p, EchMode::Rfc9180Outer);
    EXPECT_TRUE(has_extension(h, 0xFE0D));
    EXPECT_FALSE(has_extension(h, 0xFE02));
  }
}

TEST(ProfileWire, PqGroupIsConsistentBetweenGroupsAndKeyShare) {
  for (auto p : all_profiles()) {
    auto h = generate_header_test(p);
    EXPECT_TRUE(check_pq_group_consistency(h));
  }
}

TEST(ProfileWire, ExtensionOrderMatchesPolicyWindow) {
  // For ChromeShuffleAnchored profiles: any permutation of non-anchor extensions
  // is allowed — each shuffle produces a different order. The test validates:
  //   1. GREASE is at fixed anchor positions (first and last among extensions).
  //   2. Padding (0x0015), if present, is last.
  //   3. For FixedFromFixture profiles: order is exactly as in the fixture.
  for (auto p : all_profiles()) {
    auto h = generate_header_test(p);
    auto spec = profile_spec(p);
    auto order = extract_extension_order(h);
    EXPECT_TRUE(is_order_allowed_for_profile(spec, order));
  }
}

TEST(ProfileWire, FixedProfilesDoNotUseGlobalShuffle) {
  for (auto p : all_profiles()) {
    auto spec = profile_spec(p);
    if (spec.extension_order_policy != ExtensionOrderPolicy::FixedFromFixture) {
      continue;
    }
    auto h1 = generate_header_test(p);
    auto h2 = generate_header_test(p);
    EXPECT_EQ(extract_extension_order(h1), extract_extension_order(h2));
  }
}

TEST(ProfileWire, ChromeAnchoredProfilesGREASEAndPaddingArePositionallyStable) {
  // GREASE extensions must be the first and last in extension list.
  // Padding (0x0015) must be last or absent.
  // All other extensions (including RenegotiationInfo 0xff01) shuffle freely.
  for (auto p : all_profiles()) {
    auto spec = profile_spec(p);
    if (spec.extension_order_policy != ExtensionOrderPolicy::ChromeShuffleAnchored) {
      continue;
    }
    auto h = generate_header_test(p);
    auto order = extract_extension_order(h);
    ASSERT_GE(order.size(), 3u);
    EXPECT_TRUE(is_grease_extension(order.front())) << "First extension must be GREASE";
    EXPECT_TRUE(is_grease_extension(order.back()) || order.back() == 0x0015u)
        << "Last extension must be GREASE or padding";
    // RenegotiationInfo is NOT expected at a fixed position:
    auto ri_pos = std::find(order.begin(), order.end(), 0xff01u);
    if (ri_pos != order.end()) {
      EXPECT_FALSE(ri_pos == order.end() - 1 && !is_grease_extension(order.back()))
          << "RenegotiationInfo should vary position, not be stuck at tail";
    }
  }
}

TEST(ProfileWire, ChromeAnchoredProfilesRenegotiationInfoPositionVaries) {
  // RenegotiationInfo must shuffle freely in Chrome profile.
  for (auto p : {BrowserProfile::Chrome133, BrowserProfile::Chrome131}) {
    std::set<size_t> ri_positions;
    for (int i = 0; i < 500; i++) {
      auto h = generate_header_test(p, EchMode::Disabled);
      auto order = extract_extension_order(h);
      auto ri_it = std::find(order.begin(), order.end(), uint16_t{0xff01});
      if (ri_it != order.end()) {
        ri_positions.insert(static_cast<size_t>(ri_it - order.begin()));
      }
    }
    // Must appear at 3 or more distinct positions in 500 samples.
    EXPECT_GE(ri_positions.size(), 3u)
        << "RenegotiationInfo must not be stuck at one position (Chrome shuffle)";
  }
}

TEST(ProfileWire, FixtureDrivenAssertionsNoGuesswork) {
  for (auto p : all_profiles()) {
    auto h = generate_header_test(p);
    auto f = fixture_for_profile(p);
    EXPECT_EQ(canonical_fingerprint_struct(h), f.expected_struct);
  }
}

TEST(ProfileWire, Ja3IsNotKnownTelegramFingerprint) {
  const string known_tg_ja3 = "e0e58235789a753608b12649376e91ec";
  for (auto p : all_profiles()) {
    auto h = generate_header_test(p);
    EXPECT_NE(compute_ja3(h), known_tg_ja3);
  }
}
```

## 6.10 Definition of Done для PR-2

PR-2 считается готовым только если:

1. Все profile-структуры строятся из snapshot-backed registry, не из ad-hoc block assembly.
2. Profile selection sticky, platform-coherent и class-stable (без desktop/mobile drift).
3. ECH route policy имеет RU-default-off + circuit breaker для non-RU.
4. Все PR-2 тесты зелёные, включая fixture-driven regression checks.
5. Нет ложных hard-assert инвариантов, не подтверждённых fixtures/captures.
6. **Chrome-профили используют `ChromeShuffleAnchored`** (полный shuffle, не окна): зелёные fixture-driven проверки из `test/stealth/test_tls_extension_order_policy.cpp`, минимум `TlsExtensionOrderPolicy.EchEnabledExtensionsMatchChromeShuffleModel`, `TlsExtensionOrderPolicy.EchDisabledExtensionsMatchChromeShuffleModel` и `TlsExtensionOrderPolicy.ChromeShuffleEntropyMustRemainNonDeterministic`.
7. **В fixture базе зафиксировано**, что `BrowserProfile::Chrome133` маппится на `HelloChrome_133` и использует `0x44CD`/`ApplicationSettingsExtensionNew`; это отдельная fixture-family, не смешиваемая автоматически с `Chrome131`.
8. **В fixture базе зафиксировано**, что `BrowserProfile::Chrome120` = non-PQ (без PQ key share); `Chrome120_PQ` с `0x6399` остаётся reserved/commented в ProfileFixtures.h.
9. **curl_cffi capture workflow** `docs/Samples/scrapy-impersonate/capture_chrome131.py` pinned и встроен в provenance-checked fixture refresh process: capture без version/provenance metadata не может менять release registry; имя файла не трактуется как ограничение на один `chrome131`-lane, потому что сам инструмент уже parameterized через `--profile`.
10. **Frozen corpus -> reviewed summary path обязателен:** network-derived ClientHello fixtures сначала фиксируются как immutable JSON artifacts, затем проходят provenance diff, и только после этого попадают в `test/stealth/ReviewedClientHelloFixtures.h`, откуда их читают PR-2 capture-driven tests.

### 6.10.1 Текущий статус rollout (2026-04-06)

- Дополнительный capture-сбор для `Safari26_3`, `IOS14` и `Android11_OkHttp_Advisory` **идёт параллельно**; raw dumps и provenance notes собираются отдельно и ещё не считаются завершённой частью PR-2 release verdict.
- На текущей ветке PR-2 уже достаточно продвинут, чтобы **начинать PR-3**: registry contracts, sticky profile selection, route-aware `EchMode`, verified Chromium lanes и capture-backed `Firefox148` lane уже существуют как production-facing зависимости для transport seam work.
- Для batch-1 browser captures reproducible fixture-refresh loop уже materialized в коде: extractor, diff, frozen corpus и reviewed summary header существуют и используются PR-2 differential tests. Это закрывает не всю PR-2 capture story, но снимает основной риск ручного drift между pcap triage и test literals.
- Ограничение: старт PR-3 **не означает**, что PR-2 полностью закрыт по Safari/mobile. Эти lane'ы остаются advisory до прихода parser-grade network-derived fixtures.
- Следствие для sequencing: PR-3 можно вести дальше по activation gate, `IStreamTransport` seam, wakeup plumbing и `StealthConfig::validate()`, но нельзя объявлять весь stealth rollout release-ready, пока capture-сбор для advisory lane'ов не завершён.
- Отдельно: capture-driven defaults для PR-6/PR-8 не должны «выдумываться» из отсутствующих Safari/mobile baseline'ов; до их прихода используем только уже подтверждённые profile families и fail-closed policy.

## 6.10.2 Что реально уже закрыто кодом и тестами

На текущей ветке production и tests уже подтверждают следующие части PR-2:

- `test/stealth/test_tls_profile_registry.cpp`: sticky selection, platform coherence, advisory vs verified fixture metadata, RU/unknown ECH disable, circuit-breaker route policy.
- `test/stealth/test_tls_profile_wire.cpp`: profile-specific ALPS/PQ/ECH/padding behavior for Chromium, Firefox148 and fixed advisory profiles.
- `test/stealth/test_tls_init_profile_runtime.cpp`: production `TlsInit` runtime path uses selected profile and respects route-aware ECH suppression.
- `test/stealth/test_tls_init_circuit_breaker.cpp`: repeated ECH failures disable ECH for subsequent connections and counters/TTL re-enable path work.
- `test/stealth/test_tls_extension_order_policy.cpp`: Chrome shuffle model is enforced for Chromium lanes.

Что остаётся частичным в PR-2:

- provenance-checked curl_cffi refresh workflow описан в плане, но в production registry state не существует как enforced loader/process.
- Safari26_3, IOS14 и Android11_OkHttp_Advisory остаются advisory fixture families; release verdict по ним ещё нельзя считать закрытым.
- registry уже snapshot-backed, но не hot-reloadable: runtime params loader остаётся будущим PR-8 scope.

---

# 7. PR-3: Transport Seam + Activation Gate + StealthConfig (аудит-фикс)

**Зависит от:** PR-2  
**Исправляет:** Activation Gate + минимальные transport-seams, без которых PR-4/5/6 будут некорректны в runtime

## 7.0 Реальный статус на текущей ветке (2026-04-06)

PR-3 **реализован и уже включает часть decorator skeleton, который в первоначальном плане относился к PR-4**. На текущей ветке уже есть:

- расширение `IStreamTransport` новыми virtual seams (`pre_flush_write`, `get_shaping_wakeup`, `set_traffic_hint`, `set_max_tls_record_size`, `supports_tls_record_sizing`);
- activation gate inside existing `create_transport(TransportType)` with fail-closed config/decorator construction;
- runtime TLS record cap in `tcp::ObfuscatedTransport` plus capability guard;
- wakeup plumbing from `RawConnection::flush_write()` and `RawConnection::shaping_wakeup_at()` into `SessionConnection::flush()`;
- validated `StealthConfig`, per-connection initial record-size sampling and test-only config factory override;
- already-landed `ShaperRingBuffer` and `StealthTransportDecorator` skeleton with bounded queue, hard backpressure, consume-once hint semantics, runtime record-size propagation, and zero-delay stub scheduling.

Это означает, что PR-3 в реальном коде уже перешёл границу «только seam + activation gate» и partially absorbed the safe skeleton from planned PR-4. При документировании дальнейших PR нужно считать этот skeleton уже существующей базой, а не будущей работой.

## 7.1 Что исправлено по аудиту PR-3

1. Нельзя менять сигнатуру `create_transport` на `(int16, ProxySecret)`: в реальном коде используется `create_transport(TransportType)` и это часть текущего wiring через `RawConnection`.
2. Нельзя «упростить» `create_transport` до always-`ObfuscatedTransport`: это сломает ветки `Tcp`/`Http`.
3. Фиксированный `kInitialRecordSize = 1380` в PR-3 создаёт новую статическую сигнатуру; initial size должен быть sampled per-connection из policy.
4. `StealthConfig::from_secret(..., global_rng())` — нежелательно: глобальный RNG вносит cross-connection coupling и lock contention на hot-path.
5. `pre_flush_write/get_shaping_wakeup` как prerequisite нельзя откладывать до PR-7: без этого delayed writes могут «зависать» до внешнего socket event.
6. Конфиг PR-3 обязан иметь валидацию границ (ring/watermarks/IPT/DRS), иначе runtime override может превратиться в DoS на клиенте.

## 7.2 IStreamTransport — минимальное расширение интерфейса

```cpp
// td/mtproto/IStreamTransport.h — добавить:
class IStreamTransport {
 public:
  // Called by RawConnection before each flush cycle.
  virtual void pre_flush_write(double now) {}

  // Next wakeup timestamp for delayed shaping writes. 0.0 = no wakeup.
  virtual double get_shaping_wakeup() const { return 0.0; }

  // Consume-once hint for the next write() call.
  virtual void set_traffic_hint(stealth::TrafficHint hint) {}

  // Runtime control of TLS record cap.
  // Implemented by ObfuscatedTransport; default no-op for Tcp/Http.
  virtual void set_max_tls_record_size(int32 size) {}

  // Capability guard to avoid silent no-op misuse in decorator.
  virtual bool supports_tls_record_sizing() const { return false; }
};
```

## 7.3 ObfuscatedTransport — без изменения legacy-поведения по умолчанию

```cpp
// td/mtproto/TcpTransport.h
class ObfuscatedTransport final : public IStreamTransport {
 public:
  ObfuscatedTransport(int16 dc_id, ProxySecret secret)
      : dc_id_(dc_id), secret_(std::move(secret)), impl_(secret_.use_random_padding()) {
  }

  void set_max_tls_record_size(int32 size) override {
    // HOT PATH: no allocation.
    // Upper bound is TLS payload limit; lower bound avoids pathological tiny records.
    max_tls_packet_length_ = td::clamp(size, 256, 16384);
  }

  bool supports_tls_record_sizing() const override {
    return secret_.emulate_tls();
  }

 private:
  // Keep legacy default to avoid behavior regression when shaping is off.
  static constexpr int32 MAX_TLS_PACKET_LENGTH = 2878;
  int32 max_tls_packet_length_{MAX_TLS_PACKET_LENGTH};
};

// td/mtproto/TcpTransport.cpp
// Обязательное условие: в do_write_tls() все обращения к MAX_TLS_PACKET_LENGTH
// должны быть заменены на max_tls_packet_length_, иначе runtime setter будет no-op.
// Пример: CHECK(header_.size() <= max_tls_packet_length_);
//         slice.substr(0, max_tls_packet_length_ - header_.size());
```

Критично: PR-3 не должен менять дефолтную длину TLS record на фиксированный `1380` для всех соединений.

## 7.4 Activation Gate — строго в существующем create_transport(TransportType)

```cpp
// td/mtproto/IStreamTransport.cpp

unique_ptr<IStreamTransport> create_transport(TransportType type) {
  switch (type.type) {
    case TransportType::ObfuscatedTcp: {
      auto secret_copy = type.secret;  // keep gate decision independent from move
      auto inner = td::make_unique<tcp::ObfuscatedTransport>(type.dc_id, std::move(type.secret));

#if TDLIB_STEALTH_SHAPING
      if (secret_copy.emulate_tls()) {
        auto rng = stealth::make_connection_rng();  // per-connection RNG, not global singleton
        auto config = stealth::StealthConfig::from_secret(secret_copy, *rng);
        return td::make_unique<stealth::StealthTransportDecorator>(
            std::move(inner), config, std::move(rng), stealth::make_clock());
      }
#endif

      return inner;
    }
    case TransportType::Tcp:
      return td::make_unique<tcp::OldTransport>();
    case TransportType::Http:
      return td::make_unique<http::Transport>(type.secret.get_raw_secret().str());
  }
  UNREACHABLE();
}
```

Итог: «единственный activation if» остаётся, но только внутри `ObfuscatedTcp` ветки, без регрессии остальных transport type.

## 7.5 PR-3 prerequisite wiring: RawConnection + SessionConnection wakeup merge

Это нужно подтянуть из будущего PR-7 в PR-3 как prerequisite для PR-4/5/6.

```cpp
// td/mtproto/RawConnection.cpp
Status flush_write() {
  transport_->pre_flush_write(Time::now_cached());
  TRY_RESULT(size, socket_fd_.flush_write());
  // ...
}

double shaping_wakeup_at() const {
  return transport_->get_shaping_wakeup();
}

// td/mtproto/SessionConnection.cpp
double SessionConnection::flush(Callback *callback) {
  // ...
  relax_timeout_at(&wakeup_at, raw_connection_->shaping_wakeup_at());
  return wakeup_at;
}
```

Без этого `get_shaping_wakeup()` остаётся «висячим API», а отложенные записи зависят от случайных внешних wakeup-событий.

## 7.6 StealthConfig — policy-driven initial size + обязательная валидация

```cpp
// td/mtproto/stealth/StealthConfig.h
struct RecordSizePolicy {
  int32 slow_start_min{1200};
  int32 slow_start_max{1460};
};

struct StealthConfig {
  BrowserProfile profile;
  PaddingPolicy padding_policy;
  IptParams ipt_params;
  DrsPolicy drs_policy;
  RecordSizePolicy record_size_policy;
  size_t ring_capacity{32};
  size_t high_watermark{24};
  size_t low_watermark{8};

  static StealthConfig from_secret(const ProxySecret &secret, IRng &rng);
  static StealthConfig default_config(IRng &rng);
  StealthConfig with_overrides(const StealthParamsOverride &overrides) const;

  // Must clamp/validate every runtime-controlled field.
  Status validate() const;

  // Sampled once per connection (not constant, not per-packet).
  int32 sample_initial_record_size(IRng &rng) const;
};
```

Требование по маскировке: initial record size должен быть sampled per-connection из policy/capture-backed диапазона, а не фиксирован literal-значением.

## 7.7 PR-3 TDD (обязательный минимум)

1. `create_transport(TransportType::Tcp|Http)` даёт те же типы транспорта, что и до PR-3.
2. `ObfuscatedTcp + secret.emulate_tls()==false` не оборачивается в decorator.
3. `ObfuscatedTcp + secret.emulate_tls()==true` оборачивается в decorator только при `TDLIB_STEALTH_SHAPING=ON`.
4. `RawConnection::flush_write()` вызывает `pre_flush_write()` перед `socket_fd_.flush_write()`.
5. `SessionConnection::flush()` включает `raw_connection_->shaping_wakeup_at()` в `wakeup_at`.
6. `StealthConfig::validate()` отклоняет невалидные watermarks/ring bounds/DRS-IPT ranges.

## 7.8 Что реально уже закрыто кодом и тестами

На текущей ветке PR-3 подтверждён следующими файлами:

- `test/stealth/test_stream_transport_seam.cpp`: activation gate, legacy transport preservation, fail-closed invalid config path, no-op default seam methods.
- `test/stealth/test_raw_connection_flush_order.cpp`: `pre_flush_write()` вызывается перед socket flush и делает queued bytes visible.
- `test/stealth/test_session_wakeup_seam.cpp`: `SessionConnection::flush()` действительно включает `raw_connection_->shaping_wakeup_at()`.
- `test/stealth/test_stealth_config.cpp` и `test/stealth/test_stealth_config_fail_closed.cpp`: validation bounds, fail-closed oversized ring, constructor dependency validation, per-connection initial record-size entropy.
- `test/stealth/test_tls_record_sizing.cpp`: runtime TLS record-size setter works and clamps hostile extremes.
- `test/stealth/test_decorator.cpp` и `test/stealth/test_decorator_hint_adversarial.cpp`: bounded queue contract, backpressure latching, wakeup monotonicity, consume-once hints, hint overwrite/bleed-through regression coverage, capability-safe record-size forwarding.

Что остаётся вне PR-3 даже при наличии decorator skeleton:

- реальный non-zero IPT sampler и keepalive/bulk bypass policy всё ещё не реализованы; `next_delay_us_stub()` возвращает `0` для всех hints.
- capture-driven DRS phases, anti-repeat sizing model и flush-batch coalescing ещё не реализованы.
- metadata-driven classifier wiring в `RawConnection::send_*` / `SessionConnection` остаётся будущим PR-7 scope.

---

# 8. PR-4: StealthTransportDecorator

**Зависит от:** PR-3  
**Реализует:** bounded ring queue, hard backpressure, consume-once hint, wakeup plumbing  
**Не реализует:** PR-5 (реальный IPT sampler), PR-6 (DRS phases/jitter), PR-7 (TrafficClassifier wiring)

## 8.0 Что исправлено в PR-4 по аудиту

1. PR-4 не должен повторно «реализовывать Activation Gate»: gate уже закреплён в PR-3 внутри `create_transport(TransportType)`.
2. Декоратор обязан полностью реализовать pure-virtual контракт `IStreamTransport` и прозрачно делегировать read-path в `inner_`.
3. PR-4 не должен тянуть логику будущих фаз: `IptController` (PR-5) и `DrsEngine` (PR-6) заменяются на deterministic stubs в этом PR.
4. Переполнение ring нельзя оставлять как «тихий drop». Это invariant violation: fail-closed + метрика, и НИКОГДА без обходного sync-write в `inner_`.
5. `get_shaping_wakeup()` обязан возвращать earliest deadline очереди, иначе actor-loop не проснётся для delayed send.

## 8.1 Заголовочный файл

> **⚠ Capacity и watermark policy:** `ring_capacity/high_watermark/low_watermark` задаются из `StealthConfig` и валидируются в PR-3 (`low <= high < capacity`). Числа должны быть capture-driven (см. `docs/Samples/Traffic dumps`), а не «магией» в коде.

```cpp
// td/mtproto/stealth/StealthTransportDecorator.h
namespace td::mtproto::stealth {

class StealthTransportDecorator final : public IStreamTransport {
 public:
  StealthTransportDecorator(unique_ptr<IStreamTransport> inner,
                            const StealthConfig &config,
                            unique_ptr<IRng> rng,
                            unique_ptr<IClock> clock,
                            size_t ring_capacity = ShaperRingBuffer::kDefaultCapacity);
  ~StealthTransportDecorator() override;

  // Full IStreamTransport delegation contract:
  Result<size_t> read_next(BufferSlice *message, uint32 *quick_ack) override;
  bool support_quick_ack() const override;
  bool can_read() const override;
  void init(ChainBufferReader *input, ChainBufferWriter *output) override;
  size_t max_prepend_size() const override;
  size_t max_append_size() const override;
  TransportType get_type() const override;
  bool use_random_padding() const override;

  // Shaping hooks introduced in PR-3:
  bool can_write() const override;
  void write(BufferWriter &&message, bool quick_ack) override;
  void pre_flush_write(double now) override;
  double get_shaping_wakeup() const override;
  void set_traffic_hint(TrafficHint hint) override;
  void set_max_tls_record_size(int32 size) override;
  bool supports_tls_record_sizing() const override;

 private:
  struct PendingWrite {
    BufferWriter message;
    bool quick_ack{false};
    double send_at{0.0};
    TrafficHint hint{TrafficHint::Unknown};
  };

  unique_ptr<IStreamTransport> inner_;
  unique_ptr<IRng> rng_;
  unique_ptr<IClock> clock_;
  ShaperRingBuffer ring_;

  // PR-4 stub: constant per-connection delay = 0.
  // Replaced by IptController in PR-5.
  uint64 next_delay_us_stub(TrafficHint hint) const;

  size_t high_watermark_;
  size_t low_watermark_;
  bool backpressure_latched_{false};
  uint64 overflow_invariant_hits_{0};

  // Sampled once per connection in PR-3 config path; no DRS phases yet.
  int32 initial_record_size_;

  // Consume-once semantics: reset after every write().
  TrafficHint pending_hint_{TrafficHint::Unknown};
};

}  // namespace td::mtproto::stealth
```

## 8.2 Реализация

```cpp
// td/mtproto/stealth/ShaperRingBuffer.h
class ShaperRingBuffer {
 public:
  static constexpr size_t kDefaultCapacity = 32;

  explicit ShaperRingBuffer(size_t capacity = kDefaultCapacity);
  bool try_enqueue(StealthTransportDecorator::PendingWrite &&item);

  // Callback returns true => continue, false => stop immediately.
  // All not-yet-drained items stay queued in original order.
  template <class F>
  void drain_ready(double now, F &&cb);

  size_t size() const noexcept;
  bool empty() const noexcept;
  double earliest_deadline() const noexcept;
};
```

```cpp
// td/mtproto/stealth/StealthTransportDecorator.cpp

Result<size_t> StealthTransportDecorator::read_next(BufferSlice *message, uint32 *quick_ack) {
  return inner_->read_next(message, quick_ack);
}

bool StealthTransportDecorator::support_quick_ack() const {
  return inner_->support_quick_ack();
}

bool StealthTransportDecorator::can_read() const {
  return inner_->can_read();
}

void StealthTransportDecorator::init(ChainBufferReader *input, ChainBufferWriter *output) {
  inner_->init(input, output);
}

size_t StealthTransportDecorator::max_prepend_size() const {
  return inner_->max_prepend_size();
}

size_t StealthTransportDecorator::max_append_size() const {
  return inner_->max_append_size();
}

TransportType StealthTransportDecorator::get_type() const {
  return inner_->get_type();
}

bool StealthTransportDecorator::use_random_padding() const {
  return inner_->use_random_padding();
}

void StealthTransportDecorator::set_traffic_hint(TrafficHint hint) {
  pending_hint_ = hint;
}

bool StealthTransportDecorator::can_write() const {
  if (!inner_->can_write()) {
    return false;
  }
  return !backpressure_latched_;
}

uint64 StealthTransportDecorator::next_delay_us_stub(TrafficHint hint) const {
  // PR-4 phase scope: do not implement IPT yet.
  // Keepalive/Bulk/Auth are still explicit to lock the future contract.
  switch (hint) {
    case TrafficHint::Keepalive:
    case TrafficHint::BulkData:
    case TrafficHint::AuthHandshake:
    case TrafficHint::Interactive:
    case TrafficHint::Unknown:
    default:
      return 0;
  }
}

void StealthTransportDecorator::write(BufferWriter &&message, bool quick_ack) {
  auto hint = pending_hint_;
  pending_hint_ = TrafficHint::Unknown;

  const auto delay_us = next_delay_us_stub(hint);
  const auto send_at = clock_->now() + static_cast<double>(delay_us) / 1e6;
  PendingWrite pw{std::move(message), quick_ack, send_at, hint};

  if (!ring_.try_enqueue(std::move(pw))) {
    // Invariant violation: caller ignored can_write() contract or config is invalid.
    // Never bypass queue with direct inner_->write().
    overflow_invariant_hits_++;
    LOG(FATAL) << "Stealth ring overflow invariant broken";
  }

  if (ring_.size() >= high_watermark_) {
    backpressure_latched_ = true;
  }
}

double StealthTransportDecorator::get_shaping_wakeup() const {
  if (ring_.empty()) {
    return 0.0;
  }
  return ring_.earliest_deadline();
}

void StealthTransportDecorator::pre_flush_write(double now) {
  ring_.drain_ready(now, [this](PendingWrite &&pw) -> bool {
    if (!inner_->can_write()) {
      return false;
    }

    if (inner_->supports_tls_record_sizing()) {
      inner_->set_max_tls_record_size(initial_record_size_);
    }
    inner_->write(std::move(pw.message), pw.quick_ack);
    return true;
  });

  if (backpressure_latched_ && ring_.size() <= low_watermark_) {
    backpressure_latched_ = false;
  }
}
```

## 8.3 TDD (обновлено)

```cpp
// test/stealth/test_decorator.cpp

TEST(DecoratorContract, DelegatesReadPathToInnerTransport) {
  auto [dec, inner] = make_test_decorator();
  EXPECT_EQ(dec->support_quick_ack(), inner->support_quick_ack());
  EXPECT_EQ(dec->max_prepend_size(), inner->max_prepend_size());
}

TEST(DecoratorHint, ConsumeOnceHintResetsToUnknown) {
  auto [dec, inner] = make_test_decorator();
  dec->set_traffic_hint(TrafficHint::Keepalive);
  dec->write(make_test_buffer(64), false);
  dec->write(make_test_buffer(64), false);

  ASSERT_EQ(inner->queued_hints.size(), 2u);
  EXPECT_EQ(inner->queued_hints[0], TrafficHint::Keepalive);
  EXPECT_EQ(inner->queued_hints[1], TrafficHint::Unknown);
}

TEST(DecoratorBackpressure, LatchesAtHighAndReleasesAtLow) {
  auto [dec, _] = make_test_decorator(/*capacity=*/8, /*high=*/6, /*low=*/2);
  enqueue_n(*dec, 6);
  EXPECT_FALSE(dec->can_write());

  drain_until_size(*dec, 2);
  EXPECT_TRUE(dec->can_write());
}

TEST(DecoratorWakeup, ReturnsEarliestDeadlineFromRing) {
  auto [dec, _] = make_test_decorator_with_clock();
  dec->set_traffic_hint(TrafficHint::Interactive);
  dec->write(make_test_buffer(256), false);
  EXPECT_GT(dec->get_shaping_wakeup(), 0.0);
}

TEST(DecoratorSafety, OverflowNeverBypassesInnerWrite) {
  auto [dec, inner] = make_test_decorator(/*capacity=*/2, /*high=*/2, /*low=*/1);
  enqueue_n(*dec, 2);
  EXPECT_FALSE(dec->can_write());
  EXPECT_TRUE(inner->sync_bypass_write_happened == false);
}
```

## 8.4 Scope guard между PR-4/5/6/7

1. Activation tests остаются в PR-3 (там же проверяется реальный `create_transport(TransportType)` и `ProxySecret::emulate_tls()` gate).
2. IPT-distribution тесты (`log-normal`, `Markov`, keepalive bypass по delay) остаются в PR-5.
3. DRS phase/reset/jitter тесты остаются в PR-6.
4. Полноценный classifier (`bytes -> hint`) переносится в PR-7 и должен быть capture-driven, а не фиксированными порогами из головы. В PR-5 fallback ограничен правилом `Unknown -> Interactive`.
5. В PR-4 допускаются только deterministic stubs, чтобы собрать безопасный skeleton без phase leakage.

---

# 9. PR-5: IPT — Inter-Packet Timing

**Зависит от:** PR-4  
**Исправляет:** S12 (равномерный IPT), S14 (keepalive delay)

## 9.0 Границы PR-5 (важно для anti-detection surface)

- PR-5 реализует только sampling/scheduling inter-packet delay для уже готового decorator-path.
- PR-5 не меняет TLS handshake-профили (ECH/ALPS/PQ) и не вводит QUIC/UDP поведение.
- PR-5 не должен блокировать event-loop: никаких `sleep`/busy-wait в hot path, только расчёт deadline и wakeup через существующий plumbing.
- До PR-7 действует консервативное правило: `TrafficHint::Unknown -> Interactive`.

## 9.1 TrafficHint

```cpp
// td/mtproto/stealth/TrafficHint.h
enum class TrafficHint : uint8_t {
  Unknown      = 0,
  Interactive  = 1,  // Chat messages, API calls — log-normal delay
  BulkData     = 2,  // File/media transfer — drain immediately
  Keepalive    = 3,  // MTProto PING — bypass IPT (must not be delayed by shaper)
  AuthHandshake= 4,  // First ~3 packets after connect — drain immediately
};
```

## 9.2 IptParams (shared submodule)

```cpp
// telemt-stealth-params/params.h (синхронизирован с telemt)
struct IptParams {
  // Log-normal parameters for Burst state: μ=3.5 -> median ~33ms.
  // Значения обязаны быть capture-driven и переопределяемыми через PR-8.
  double burst_mu_ms    = 3.5;
  double burst_sigma    = 0.8;
  double burst_max_ms   = 200.0;  // safety cap for Interactive traffic only

  // Truncated Pareto parameters for Idle state inter-request delays.
  // Важно: используем именно Truncated Pareto sampling, а не clamp после sample,
  // чтобы не создавать искусственный пик на idle_max_ms.
  double idle_alpha     = 1.5;
  double idle_scale_ms  = 500.0;   // xm (minimum of Pareto support)
  double idle_max_ms    = 3000.0;  // upper bound of truncated support

  // Markov transition probabilities.
  double p_burst_stay    = 0.95;
  double p_idle_to_burst = 0.30;
};
```

## 9.3 IptController с Keepalive bypass

```cpp
// td/mtproto/stealth/ShaperState.h

class IptController {
 public:
  explicit IptController(const IptParams &params, IRng &rng);

  // Returns delay in microseconds for the next packet.
  // hint=Keepalive/BulkData/AuthHandshake → returns 0 (bypass).
  // hint=Interactive → log-normal sample with Markov state transition.
  uint64_t next_delay_us(bool has_pending_data, TrafficHint hint);

 private:
  using Burst = std::monostate;
  using Idle  = struct {};
  using MarkovState = std::variant<Burst, Idle>;

  IptParams params_;
  IRng &rng_;
  MarkovState state_{Idle{}};

  // Samplers are deterministic under IRng in tests.
  double sample_lognormal(double mu, double sigma);
  double sample_truncated_pareto(double u, double alpha, double xm, double xmax);
  static bool is_bypass_hint(TrafficHint hint);
  static TrafficHint normalize_hint(TrafficHint hint);
  MarkovState transition(bool has_pending);
};

uint64_t IptController::next_delay_us(bool has_pending_data, TrafficHint hint) {
  // Bypass path must run first.
  hint = normalize_hint(hint);
  if (is_bypass_hint(hint)) {
    return 0;
  }

  state_ = transition(has_pending_data);

  return std::visit([this, has_pending_data](auto &&s) -> uint64_t {
    using T = std::decay_t<decltype(s)>;
    if constexpr (std::is_same_v<T, Burst>) {
      double delay_ms = sample_lognormal(params_.burst_mu_ms, params_.burst_sigma);
      delay_ms = std::min(delay_ms, params_.burst_max_ms);
      return static_cast<uint64_t>(delay_ms * 1000.0);
    } else {
      if (!has_pending_data) return 0;
      // Truncated Pareto via inverse CDF on bounded support [xm, xmax].
      // This avoids a detector-visible spike at xmax (unlike post-sample clamp).
      double u = static_cast<double>(rng_.next_u32()) / 4294967296.0;
      u = td::clamp(u, 1e-9, 1.0 - 1e-9);
      double delay_ms = sample_truncated_pareto(u,
                                                params_.idle_alpha,
                                                params_.idle_scale_ms,
                                                params_.idle_max_ms);
      return static_cast<uint64_t>(delay_ms * 1000.0);
    }
  }, state_);
}

bool IptController::is_bypass_hint(TrafficHint hint) {
  return hint == TrafficHint::Keepalive ||
         hint == TrafficHint::BulkData ||
         hint == TrafficHint::AuthHandshake;
}

TrafficHint IptController::normalize_hint(TrafficHint hint) {
  // До появления classifier wiring (PR-7) не выделяем Unknown в отдельный класс.
  return hint == TrafficHint::Unknown ? TrafficHint::Interactive : hint;
}

double IptController::sample_truncated_pareto(double u, double alpha, double xm, double xmax) {
  // Inverse CDF on bounded support [xm, xmax], without post-sample hard clamp.
  const double f_xmax = 1.0 - std::pow(xm / xmax, alpha);
  const double u_scaled = u * f_xmax;
  const double u_safe = td::clamp(u_scaled, 0.0, 1.0 - 1e-9);
  return xm / std::pow(1.0 - u_safe, 1.0 / alpha);
}
```

## 9.4 TDD для IPT

```cpp
TEST(IptController, KeepaliveBypassesDelayInBurstState) {
  MockRng rng(42);
  IptController ipt(IptParams{}, rng);
  // Drive to Burst state:
  for (int i = 0; i < 10; i++) ipt.next_delay_us(true, TrafficHint::Interactive);
  // Keepalive must be 0 even in Burst:
  EXPECT_EQ(ipt.next_delay_us(true, TrafficHint::Keepalive), 0u);
}

TEST(IptController, UnknownFallsBackToInteractiveBeforePr7Classifier) {
  MockRng rng(123);
  IptController ipt(IptParams{}, rng);
  // Unknown не должен автоматически bypass-иться.
  auto d = ipt.next_delay_us(true, TrafficHint::Unknown);
  EXPECT_GT(d, 0u);
}

TEST(IptController, InteractiveDelayAlwaysRespectsConfiguredUpperBounds) {
  MockRng rng(0);
  IptParams p;
  IptController ipt(IptParams{}, rng);
  for (int i = 0; i < 100000; i++) {
    auto delay = ipt.next_delay_us(true, TrafficHint::Interactive);
    const auto upper_us = static_cast<uint64_t>(std::max(p.burst_max_ms, p.idle_max_ms) * 1000.0);
    EXPECT_LE(delay, upper_us) << "Configured delay upper bound violated at i=" << i;
  }
}

TEST(IptController, DelayDistributionIsLogNormal) {
  MockRng rng(1);
  IptController ipt(IptParams{}, rng);
  std::vector<double> samples;
  for (int i = 0; i < 10000; i++) {
    // Force Burst state:
    rng = MockRng(i);
    IptController fresh(IptParams{}, rng);
    for (int j = 0; j < 5; j++) fresh.next_delay_us(true, TrafficHint::Interactive);
    auto d = fresh.next_delay_us(true, TrafficHint::Interactive);
    if (d > 0) samples.push_back(static_cast<double>(d) / 1000.0);
  }
  // K-S test against log-normal: p-value must be > 0.01.
  EXPECT_GT(kolmogorov_smirnov_lognormal_pvalue(samples, 3.5, 0.8), 0.01);
}

TEST(IptController, TruncatedParetoHasNoHardCapSpike) {
  MockRng rng(9);
  IptParams p;
  IptController ipt(p, rng);

  std::vector<uint64_t> idle;
  for (int i = 0; i < 20000; i++) {
    auto d = ipt.next_delay_us(true, TrafficHint::Interactive);
    if (d > 0) {
      idle.push_back(d);
    }
  }

  // Quality gate: no detector-visible pile-up exactly at idle_max.
  const uint64_t hard_cap_us = static_cast<uint64_t>(p.idle_max_ms * 1000.0);
  const size_t at_cap = std::count(idle.begin(), idle.end(), hard_cap_us);
  EXPECT_LT(at_cap, idle.size() / 100);
}
```

---

# 10. PR-6: DRS — Capture-Driven Dynamic Record Sizing

**Зависит от:** PR-3 (runtime record cap seam), PR-4 (decorator/ring), совместим с PR-5 hint-контрактом  
**Исправляет:** S5 (static 2878), S13 (механистичный sizing), плюс снижает риск новых DRS-сигнатур

## 10.1 Аудит PR-6 V6: что было рискованно

1. Модель `3 фиксированных target + uniform ±10%` сама становится ML-сигнатурой.
2. DRS через один только `set_max_tls_record_size()` не может «вырастить» записи, если очереди не коалесцируются в более крупные write-batch.
3. Не учитывался current-wire overhead первого TLS write (`header_` + first TLS marker), поэтому target/реальный on-wire size расходятся.
4. Жёсткий инвариант «2878 никогда» слишком хрупкий: детект идёт по доминанте/сериям, а не по одному значению.
5. Fixed idle-reset threshold `500ms` даёт sawtooth-паттерн; threshold должен быть profile/capture-driven.

## 10.2 Принцип PR-6: profile-driven distribution, а не uniform jitter

Источник параметров для DRS:

- baseline captures из `docs/Samples/Traffic dumps/*.pcap*`;
- совместимые snapshot-профили из `docs/Samples/utls-code`;
- ограничения wire-слоя текущего `ObfuscatedTransport` (`MAX_TLS_PACKET_LENGTH`, first-record behavior).

DRS policy хранит не «один target на фазу», а эмпирические бины размеров с весами.

```cpp
// telemt-stealth-params/params.h
struct RecordSizeBin {
  int32 lo;      // inclusive
  int32 hi;      // inclusive
  uint16 weight; // >0
};

struct DrsPhaseModel {
  vector<RecordSizeBin> bins;   // capture-driven histogram buckets
  int32 max_repeat_run = 4;     // anti-mechanical run cap
  int32 local_jitter = 24;      // fine jitter inside bin (bytes)
};

struct DrsPolicy {
  DrsPhaseModel slow_start;
  DrsPhaseModel congestion_open;
  DrsPhaseModel steady_state;

  int32 slow_start_records = 4;
  int32 congestion_bytes = 32768;

  // Idle reset is sampled per connection from this range (not fixed 500ms).
  int32 idle_reset_ms_min = 250;
  int32 idle_reset_ms_max = 1200;

  // Hard bounds for payload cap in current transport implementation.
  int32 min_payload_cap = 900;
  int32 max_payload_cap = 16384;
};
```

Критичные правила:

- выбор размера идёт из weighted bins + локальный jitter, но с anti-repeat guard;
- `BulkData` может форсировать steady-state фазу, но не bypass-ит bounds/validation;
- если профиль в handshake объявляет `record_size_limit`, DRS обязан уважать этот верхний предел;
- policy валидируется в PR-3 `StealthConfig::validate()` (границы, суммы весов, пустые bins, min<=max).

## 10.3 DrsEngine (исправленный контракт)

```cpp
// td/mtproto/stealth/DrsEngine.h
class DrsEngine {
 public:
  enum class Phase { SlowStart, CongestionOpen, SteadyState };

  DrsEngine(const DrsPolicy &policy, IRng &rng);

  // Returns payload cap for next flush-batch.
  int32 next_payload_cap(TrafficHint hint);

  // Called with ACTUAL bytes written to inner transport in this flush batch.
  void notify_bytes_written(size_t bytes);

  // Called when elapsed idle exceeds sampled threshold for this connection.
  void notify_idle();

  Phase current_phase() const noexcept { return phase_; }

 private:
  const DrsPolicy policy_;
  IRng &rng_;

  Phase phase_{Phase::SlowStart};
  size_t records_in_phase_{0};
  size_t bytes_in_phase_{0};

  int32 sampled_idle_reset_ms_{0};
  int32 last_cap_{-1};
  int32 last_cap_run_{0};

  int32 sample_from_phase(const DrsPhaseModel &m) noexcept;
  void maybe_advance_phase();
};
```

## 10.4 Интеграция в decorator: обязательно с flush-batch coalescing

Без batching DRS почти не влияет на короткие MTProto сообщения. Поэтому PR-6 обязан расширить drain-путь:

```cpp
void StealthTransportDecorator::pre_flush_write(double now) {
  maybe_reset_drs_on_idle(now);  // uses sampled per-connection threshold

  ring_.drain_ready(now, [this](PendingWrite &&first) -> bool {
    if (!inner_->can_write()) {
      return false;
    }

    const auto hint = normalize_hint(first.hint);
    const int32 payload_cap = drs_.next_payload_cap(hint);

    // Account for current transport overhead on the first TLS write.
    const int32 effective_cap = adjust_cap_for_first_tls_write(payload_cap);
    inner_->set_max_tls_record_size(effective_cap);

    // Build one flush batch up to effective_cap or short deadline.
    BatchBuilder batch(effective_cap);
    batch.push(std::move(first));
    ring_.pop_ready_while(now, [&batch](const PendingWrite &pw) {
      return batch.can_append(pw);
    }, [&batch](PendingWrite &&pw) {
      batch.append(std::move(pw));
    });

    size_t written = 0;
    if (batch.can_coalesce()) {
      auto merged = batch.take_coalesced_message();
      written = merged.size();
      inner_->write(std::move(merged), /*quick_ack=*/false);
    } else {
      // quick_ack or incompatible items must be flushed individually
      // to preserve packet-level semantics.
      for (auto &item : batch.items()) {
        written += item.message.size();
        inner_->write(std::move(item.message), item.quick_ack);
      }
    }
    drs_.notify_bytes_written(written);
    return true;
  });

  if (backpressure_latched_ && ring_.size() <= low_watermark_) {
    backpressure_latched_ = false;
  }
}
```

Важно: `BatchBuilder` не должен нарушать порядок пакетов и quick-ack semantics.
Коалесцирование допустимо только для безопасного подмножества (`quick_ack=false`, совместимые дедлайны/hint), иначе запись должна остаться packet-by-packet.

Минимальный контракт `BatchBuilder` (чтобы не было несовместимых ad-hoc реализаций):

```cpp
class BatchBuilder {
 public:
  explicit BatchBuilder(int32 cap);
  void push(StealthTransportDecorator::PendingWrite &&first);
  bool can_append(const StealthTransportDecorator::PendingWrite &pw) const;
  void append(StealthTransportDecorator::PendingWrite &&pw);
  bool can_coalesce() const;
  BufferWriter take_coalesced_message();
  vector<StealthTransportDecorator::PendingWrite> &items();

 private:
  int32 remaining_cap_;
  vector<StealthTransportDecorator::PendingWrite> items_;
};
```

## 10.4.1 Актуализация progress на текущей ветке (2026-04-07)

Что уже приземлено в repo state по PR-6:

- В `StealthTransportDecorator` появился реальный `DrsEngine` path с phase-aware sampling, idle reset, flush-batch coalescing и packet-order/quick-ack guardrails; DRS больше не остаётся purely setter-only фасадом.
- Учтён current-wire overhead первого TLS flush: decorator теперь кеширует `tls_record_sizing_payload_overhead()` **до** inner write и тем же значением кормит `notify_bytes_written(...)`, чтобы one-shot overhead не терялся после мутации состояния транспорта.
- `ObfuscatedTransport::tls_record_sizing_payload_overhead()` теперь отражает фактический first-flush overhead для TLS lane: `64-byte obfuscation header + 6-byte TLS primer = 70`, затем `0` после первого TLS packet.
- Добавлены focused adversarial/wire tests для PR-6 seam'ов: phase progression по real written bytes, anti-repeat, idle reset, coalescing visibility, hostile overhead saturation, zero-write behavior и first-write overhead regression.
- Profile metadata больше не оставляет `record_size_limit` orphaned только в ClientHello builder: `ProfileSpec` несёт `record_size_limit`, Firefox148 wired как `0x4001`, а runtime `StealthConfig::from_secret(secret, rng, unix_time, platform)` на non-Darwin выбирает runtime profile по TLS secret domain/time/platform и clamp'ит `drs_policy.max_payload_cap` из profile metadata.
- Firefox `0x001c` extension body в builder теперь собирается из `ProfileSpec.record_size_limit`, а не из отдельного hardcoded literal, чтобы handshake metadata и runtime DRS ceiling расходились не могли.

Что этим уже закрыто по тексту PR-6:

- пункт про flush-batch coalescing в `10.4` больше не теоретический: seam реализован и покрыт regression tests;
- пункт про first TLS write overhead из `10.1` и `10.4` закрыт на runtime accounting path;
- правило из `10.2`, что profile-declared `record_size_limit` обязан ограничивать DRS, теперь wired в runtime config path, а не только описано в плане.

Что ещё остаётся открытым внутри PR-6 despite this progress:

- текущий shipped profile set не даёт observable behavioral delta от `record_size_limit` clamp, потому что Firefox148 advertises maximum TLS 1.3 limit `0x4001 -> 16384 payload ceiling`; важен сам wired seam, но для более жёстких профилей нужен отдельный fixture/runtime case;
- нужны более широкие smoke/diff gates на длинные constant runs и dominance checks against real capture families, а не только focused unit/wire regressions;
- финальная release-grade оценка PR-6 должна проверять совместимость с profile families и будущими runtime params refresh'ами, чтобы новый DRS policy не дрейфовал обратно к synthetic mode.

## 10.5 TDD для PR-6 (исправленный)

```cpp
TEST(DrsEngine, AdvancesPhaseByRealWrittenBytes) {
  MockRng rng(1);
  DrsPolicy p = make_test_policy();
  DrsEngine drs(p, rng);

  for (int i = 0; i < p.slow_start_records; i++) {
    drs.next_payload_cap(TrafficHint::Interactive);
    drs.notify_bytes_written(1300);
  }
  EXPECT_EQ(drs.current_phase(), DrsEngine::Phase::CongestionOpen);

  drs.notify_bytes_written(p.congestion_bytes);
  EXPECT_EQ(drs.current_phase(), DrsEngine::Phase::SteadyState);
}

TEST(DrsEngine, DistributionNotUniformAndNotSingleValue) {
  MockRng rng(42);
  DrsPolicy p = make_test_policy();
  DrsEngine drs(p, rng);

  auto series = sample_caps(drs, 2000);
  EXPECT_TRUE(has_multiple_modes(series));
  EXPECT_LT(max_repeat_run(series), 6);
}

TEST(DrsEngine, IdleResetThresholdIsSampledPerConnection) {
  MockRng rngA(7), rngB(8);
  DrsPolicy p = make_test_policy();
  DrsEngine a(p, rngA), b(p, rngB);

  EXPECT_NE(a.debug_idle_reset_ms(), b.debug_idle_reset_ms());
}

TEST(DecoratorDrs, CoalescingMakesCapObservableOnWire) {
  auto [dec, inner] = make_test_decorator_with_drs();
  enqueue_small_burst(*dec, /*count=*/8, /*payload=*/300);
  dec->pre_flush_write(mock_now());
  EXPECT_TRUE(inner->saw_coalesced_write_call());
}

TEST(BatchBuilder, DoesNotCoalesceQuickAckPackets) {
  BatchBuilder bb(/*cap=*/12000);
  bb.push(make_pending_write(/*bytes=*/700, /*quick_ack=*/true, TrafficHint::Interactive));
  auto non_quick = make_pending_write(/*bytes=*/600, /*quick_ack=*/false, TrafficHint::Interactive);
  EXPECT_FALSE(bb.can_append(non_quick));
}

TEST(BatchBuilder, PreservesPacketOrderWhenCoalescing) {
  BatchBuilder bb(/*cap=*/12000);
  bb.push(make_pending_write_with_seq(/*seq=*/1, /*bytes=*/400, /*quick_ack=*/false));
  bb.append(make_pending_write_with_seq(/*seq=*/2, /*bytes=*/500, /*quick_ack=*/false));
  bb.append(make_pending_write_with_seq(/*seq=*/3, /*bytes=*/450, /*quick_ack=*/false));
  auto merged = bb.take_coalesced_message();
  EXPECT_TRUE(merged_contains_seq_in_order(merged, {1, 2, 3}));
}

TEST(DecoratorDrs, EveryProducedChunkHasTlsAppDataRecordHeader) {
  auto [dec, inner] = make_test_decorator_with_drs();
  enqueue_large_burst(*dec, /*payload=*/20000);
  dec->pre_flush_write(mock_now());
  EXPECT_TRUE(inner->all_tls_chunks_prefixed_with_170303());
}

TEST(DecoratorDrs, FirstTlsWriteOverheadCompensationApplied) {
  auto [dec, inner] = make_test_decorator_with_drs();
  dec->write(make_test_buffer(1200), false);
  dec->pre_flush_write(mock_now());
  EXPECT_TRUE(inner->last_cap_was_adjusted_for_tls_preamble());
}

TEST(DrsRegression, Legacy2878IsNotDominantMode) {
  MockRng rng(9);
  DrsPolicy p = make_test_policy();
  DrsEngine drs(p, rng);
  auto series = sample_caps(drs, 5000);

  // Detector-relevant invariant: 2878 must not dominate distribution.
  EXPECT_LT(mode_share(series, 2878), 0.05);
}
```

## 10.6 Definition of Done для PR-6

PR-6 готов только если:

1. DRS policy capture-driven и profile-aware, без fixed uniform-jitter-only модели.
2. Decorator реализует batching/coalescing, иначе DRS не является наблюдаемым на wire.
3. Учтён first TLS write overhead при вычислении effective payload cap.
4. Idle reset threshold sampled per-connection (в пределах policy), не fixed 500ms.
5. Smoke/diff проверки подтверждают отсутствие доминирующей сигнатуры 2878 и отсутствие длинных константных run.
6. Конфиг проходит валидацию границ/весов и не позволяет DoS-параметры.
7. `BatchBuilder` строго сохраняет quick-ack и packet-order инварианты (покрыто отдельными тестами).

Операционная заметка: для RU-направлений стратегия остаётся TCP+TLS only; PR-6 не должен добавлять QUIC-имитацию.

---

# 11. PR-7: TrafficClassifier + Correct Wiring (Session/Raw/Handshake)

**Зависит от:** PR-3, PR-5  
**Исправляет:** S14 (keepalive), S15 (auth handshake delay)

## 11.0 Аудит PR-7 (исправления после сверки с реальным кодом)

Текущий текст PR-7 в V6 имел несколько архитектурно неверных предпосылок:

1. В `SessionConnection` нет `start_auth()`, `do_loop()` и поля `connection_`; это невалидные точки интеграции для hint wiring.
2. `flush_packet()` в реальном коде не использует `flush_msg_id_` и не имеет `is_keepalive_message(...)`; keepalive-сигнал формируется через локальный `ping_id`.
3. Начальный MTProto auth-key handshake (`req_pq -> req_DH_params -> set_client_DH_params`) идёт через `HandshakeConnection::send_no_crypto()`, а не через `SessionConnection`.
4. Правило "первые 3 packet в SessionConnection = AuthHandshake" некорректно: это смешивает pre-auth и post-auth этапы.

Следствие: PR-7 обязан делать wiring на реальных write-точках (`RawConnection::send_no_crypto` и `RawConnection::send_crypto`), а `SessionConnection` должен только классифицировать свой `flush_packet()` контекст.

Отдельная guardrail-поправка из `docs/Samples/xray-core-code`: не переносить sudoku/finalmask byte-markers в MTProto ciphertext. Для TDLib-маскировки классификатор должен управлять **только timing/record policy**, без payload tampering.

## 11.1 Корректные точки интеграции

```cpp
// td/mtproto/RawConnection.h
// PR-7: добавить hint в write-path API.
virtual size_t send_crypto(const Storer &storer,
                           uint64 session_id,
                           int64 salt,
                           const AuthKey &auth_key,
                           uint64 quick_ack_token,
                           stealth::TrafficHint hint = stealth::TrafficHint::Interactive) = 0;

virtual void send_no_crypto(const Storer &storer,
                            stealth::TrafficHint hint = stealth::TrafficHint::AuthHandshake) = 0;
```

```cpp
// td/mtproto/RawConnection.cpp (RawConnectionDefault)
size_t send_crypto(..., uint64 quick_ack_token, stealth::TrafficHint hint) final {
  // ... packet build ...
  transport_->set_traffic_hint(hint);
  transport_->write(std::move(packet), use_quick_ack);
  return packet_size;
}

void send_no_crypto(const Storer &storer,
                    stealth::TrafficHint hint = stealth::TrafficHint::AuthHandshake) final {
  // ... packet build ...
  transport_->set_traffic_hint(hint);
  transport_->write(std::move(packet), false);
}
```

Комментарий:
- Для `HandshakeConnection` и `PingConnectionReqPQ` дополнительных вызовов не нужно: они уже идут через `send_no_crypto(...)` и автоматически получают `AuthHandshake` hint.
- Это убирает ложную зависимость от несуществующих методов `SessionConnection::start_auth/do_loop`.

## 11.2 TrafficClassifier в SessionConnection (metadata-only)

Классификатор должен работать только на локальной мета-информации `flush_packet()` и не читать payload:

```cpp
// td/mtproto/SessionConnection.cpp
static stealth::TrafficHint classify_hint(bool has_salt,
                                          int64 ping_id,
                                          size_t query_count,
                                          size_t query_bytes,
                                          size_t ack_count,
                                          bool has_service_queries,
                                          bool destroy_auth_key,
                                          size_t bulk_threshold_bytes) noexcept {
  // Bootstrap/control path before stable session state.
  if (!has_salt) {
    return stealth::TrafficHint::AuthHandshake;
  }

  // Keepalive only for pure control packets.
  const bool has_user_queries = query_count > 0;
  const bool pure_control = !has_user_queries && !has_service_queries && !destroy_auth_key;
  if (pure_control && (ping_id != 0 || ack_count > 0)) {
    return stealth::TrafficHint::Keepalive;
  }

  // Prevent false bypass: mixed packet with ping + user data is not Keepalive.
  if (has_user_queries && query_bytes >= bulk_threshold_bytes) {
    return stealth::TrafficHint::BulkData;
  }

  return stealth::TrafficHint::Interactive;
}
```

Обязательные инварианты:

1. `Unknown` не используется в явном wiring-path из `SessionConnection`; fallback `Unknown -> Interactive` остаётся только для внешних/неразмеченных callers.
2. Пакет с `ping_id != 0` и пользовательскими query не помечается как `Keepalive`.
3. Никаких фиксированных "магических" size-threshold из головы: `bulk_threshold_bytes` берётся из capture-driven defaults (PR-6/PR-8), валидируется диапазоном.
4. Никакого parsing/decryption payload в classifier (security/perf guardrail).
5. Наличие `future_salt_n > 0` само по себе не считается признаком bootstrap-handshake.

## 11.3 Изменения в SessionConnection::flush_packet

Внутри существующего `flush_packet()` hint вычисляется один раз перед отправкой:

```cpp
auto hint = classify_hint(/* has_salt, ping_id, queries.size(), send_size,
                            to_ack.size(), has_service_queries,
                            destroy_auth_key, bulk_threshold */);

send_crypto(storer, quick_ack_token, hint);
```

Где `has_service_queries = !to_resend_answer.empty() || !to_cancel_answer.empty() || !to_get_state_info.empty()`.

Важно: wakeup plumbing (`pre_flush_write/get_shaping_wakeup`) остаётся в PR-3 и здесь не дублируется.

## 11.4 TDD (переписано под реальные API)

```cpp
TEST(TrafficClassifier, NoSaltIsAuthHandshake) {
  EXPECT_EQ(classify_hint(/*has_salt=*/false, /*ping_id=*/0,
                          /*query_count=*/0, /*query_bytes=*/0,
                          /*ack_count=*/0, /*has_service=*/false,
                          /*destroy=*/false, /*bulk_threshold=*/8192),
            TrafficHint::AuthHandshake);
}

TEST(TrafficClassifier, HasSaltAloneIsNotAuthHandshake) {
  EXPECT_NE(classify_hint(/*has_salt=*/true, /*ping_id=*/0,
                          /*query_count=*/0, /*query_bytes=*/0,
                          /*ack_count=*/0, /*has_service=*/false,
                          /*destroy=*/false, /*bulk_threshold=*/8192),
            TrafficHint::AuthHandshake);
}

TEST(TrafficClassifier, PurePingOrAckIsKeepalive) {
  EXPECT_EQ(classify_hint(true, /*ping_id=*/1, 0, 0, 0, false, false, 8192),
            TrafficHint::Keepalive);
  EXPECT_EQ(classify_hint(true, /*ping_id=*/0, 0, 0, 1, false, false, 8192),
            TrafficHint::Keepalive);
}

TEST(TrafficClassifier, MixedPingWithUserDataIsNotKeepalive) {
  EXPECT_NE(classify_hint(true, /*ping_id=*/1, /*query_count=*/2,
                          /*query_bytes=*/1200, /*ack_count=*/0,
                          /*has_service=*/false, /*destroy=*/false, /*bulk_threshold=*/8192),
            TrafficHint::Keepalive);
}

TEST(RawConnectionHints, SendNoCryptoDefaultsToAuthHandshake) {
  // Fake transport captures last set_traffic_hint() and write() calls.
  // send_no_crypto() must set AuthHandshake before write.
}

TEST(SessionFlushWiring, FlushPacketPassesClassifierHintToSendCrypto) {
  // Fake RawConnection captures hint argument from send_crypto(..., hint).
  // Drive SessionConnection via public API: send_query() + flush().
}
```

## 11.5 Definition of Done для PR-7

PR-7 готов только если:

1. Весь hint wiring проходит через реальные write-точки (`RawConnection::send_no_crypto/send_crypto`), без обращения к несуществующим API.
2. Pre-auth handshake packets гарантированно получают `AuthHandshake` hint через default-path `send_no_crypto`.
3. Keepalive bypass применяется только к pure-control packet и не срабатывает на mixed packet с user data.
4. Classifier metadata-only, без payload tampering/parsing.
5. Тесты покрывают false-bypass regression и hook correctness (`SessionConnection`, `HandshakeConnection`, `PingConnectionReqPQ`).
6. В режиме `TDLIB_STEALTH_SHAPING=OFF` hint plumbing не меняет поведение upstream path.

---

# 12. PR-8: Runtime Params Loader

**Зависит от:** PR-3, PR-2 (типы профилей/ECH mode)  
**Исправляет:** ТСПУ адаптируется быстрее чем возможен rebuild

## 12.1 Формат файла `~/.config/tdlib-obf/stealth-params.json`

```json
{
  "version": 1,
  "active_policy": "ru_egress",
  "ipt": {
    "burst_mu_ms": 3.5,
    "burst_sigma": 0.8,
    "burst_max_ms": 200.0,
    "idle_alpha": 1.5,
    "idle_scale_ms": 500.0,
    "idle_max_ms": 3000.0,
    "p_burst_stay": 0.95,
    "p_idle_to_burst": 0.30
  },
  "drs": {
    "slow_start": {
      "bins": [{"lo": 1200, "hi": 1460, "weight": 50}, {"lo": 1461, "hi": 2200, "weight": 50}],
      "max_repeat_run": 4,
      "local_jitter": 24
    },
    "congestion_open": {
      "bins": [{"lo": 2200, "hi": 4800, "weight": 60}, {"lo": 4801, "hi": 7600, "weight": 40}],
      "max_repeat_run": 5,
      "local_jitter": 32
    },
    "steady_state": {
      "bins": [{"lo": 4096, "hi": 8192, "weight": 45}, {"lo": 8193, "hi": 16384, "weight": 55}],
      "max_repeat_run": 6,
      "local_jitter": 48
    },
    "slow_start_records": 4,
    "congestion_bytes": 32768,
    "idle_reset_ms_min": 250,
    "idle_reset_ms_max": 1200,
    "min_payload_cap": 900,
    "max_payload_cap": 16384
  },
  "route_failure": {
    "ech_fail_open_threshold": 3,
    "ech_disable_ttl_sec": 1800,
    "failure_kinds": ["tcp_reset_after_ch", "hello_timeout", "tls_alert_fatal", "server_hello_parser_reject"],
    "persist_across_restart": true
  },
  "flow_behavior": {
    "max_connects_per_10s_per_destination": 6,
    "min_reuse_ratio": 0.55,
    "min_conn_lifetime_ms": 1500,
    "max_conn_lifetime_ms": 180000,
    "max_destination_share": 0.70,
    "sticky_domain_rotation_window_sec": 900,
    "anti_churn_min_reconnect_interval_ms": 300
  },
  "platform_hints": {
    "device_class": "desktop",
    "mobile_os": "none",
    "desktop_os": "linux"
  },
  "profile_weights": {
    "allow_cross_class_rotation": false,
    "desktop_darwin": {
      "Chrome133": 35,
      "Chrome131": 25,
      "Chrome120": 10,
      "Safari26_3": 20,
      "Firefox148": 10
    },
    "desktop_non_darwin": {
      "Chrome133": 50,
      "Chrome131": 20,
      "Chrome120": 15,
      "Safari26_3": 0,
      "Firefox148": 15
    },
    "mobile": {
      "IOS14": 70,
      "Android11_OkHttp_Advisory": 30
    }
  },
  "route_policy": {
    "unknown": {
      "ech_mode": "disabled",
      "allow_quic": false
    },
    "ru_egress": {
      "ech_mode": "disabled",
      "allow_quic": false
    },
    "non_ru_egress": {
      "ech_mode": "grease_draft17",
      "allow_quic": false
    }
  }
}
```

Критическое уточнение по модели применения:

- `active_policy` выбирается оператором/оркестратором (процесс-level), а не «магически» вычисляется внутри PR-8.
- В PR-8 нет автоматического гео-route detection. Если источник route-hints отсутствует, используется `unknown` (fail-safe), где ECH выключен.
- Для RU-deployment безопасный дефолт: `active_policy = ru_egress`.
- QUIC/HTTP3 не часть этого transport-дизайна; `allow_quic` оставлен как policy guard и обязан оставаться `false`.

## 12.2 StealthParamsLoader

```cpp
// td/mtproto/stealth/StealthParamsLoader.h

class StealthParamsLoader {
 public:
  // One-shot strict load. Missing file is allowed (defaults), malformed file is rejected.
  // Fail-closed: parse/validation/security error never produces partial params.
  static Result<StealthParamsOverride> try_load_strict(Slice config_path) noexcept;

  // Periodic reload (e.g., every 60s with jitter from scheduler).
  // On failure keeps last-known-good snapshot; never swaps to partially valid config.
  bool try_reload() noexcept;

  // Lock-free read path for shaper/profile selection.
  shared_ptr<const StealthParamsOverride> get_snapshot() const noexcept;

 private:
  string config_path_;
  std::atomic<int64> last_mtime_ns_{0};
  static constexpr size_t kMaxConfigBytes = 64 * 1024;
  shared_ptr<const StealthParamsOverride> current_;
  mutable std::mutex reload_mu_;

  // Schema + bounds + cross-field invariants.
  // Unknown JSON keys are rejected (typo-safe config).
  static bool validate(const StealthParamsOverride &params) noexcept;

  // Secure open/read sequence (TOCTOU-safe):
  // - open(path, O_RDONLY|O_CLOEXEC|O_NOFOLLOW)
  // - fstat(fd): regular file, owner == current uid, no world-writable bit
  // - read exactly <= kMaxConfigBytes from fd (not by reopening path)
  // - UTF-8 + strict JSON parse
  static Result<string> read_file_secure(const string &path) noexcept;
};
```

Hot-reload swap rule (обязательно):

- parse -> validate -> build immutable snapshot -> atomic publish;
- при любой ошибке reload: сохранить предыдущий snapshot, вернуть `false`, увеличить failure counter;
- после N подряд ошибок (например, 5) включать cooldown, чтобы не создавать parser-flood при битом файле.

## 12.3 Валидация (OWASP ASVS V5)

```cpp
static bool StealthParamsLoader::validate(const StealthParamsOverride &p) noexcept {
  // IPT validation: prevent absurd delays that would break connections.
  if (p.ipt.burst_mu_ms < 0.1 || p.ipt.burst_mu_ms > 10.0)   return false;
  if (p.ipt.burst_sigma < 0.1 || p.ipt.burst_sigma > 3.0)     return false;
  if (p.ipt.burst_max_ms < 10.0 || p.ipt.burst_max_ms > 1000.0) return false;
  if (p.ipt.idle_max_ms > 5000.0)                              return false;
  if (p.ipt.p_burst_stay < 0.0 || p.ipt.p_burst_stay > 1.0)  return false;
  if (p.ipt.p_idle_to_burst < 0.0 || p.ipt.p_idle_to_burst > 1.0) return false;

  // DRS validation: phase models + bounds, no empty bins.
  if (p.drs.min_payload_cap < 256 || p.drs.max_payload_cap > 16384) return false;
  if (p.drs.min_payload_cap > p.drs.max_payload_cap) return false;
  if (p.drs.idle_reset_ms_min < 50 || p.drs.idle_reset_ms_max > 5000) return false;
  if (p.drs.idle_reset_ms_min > p.drs.idle_reset_ms_max) return false;
  if (p.drs.slow_start_records < 1 || p.drs.slow_start_records > 16) return false;
  if (p.drs.congestion_bytes < 1024) return false;

  auto validate_phase = [&](const DrsPhaseModelOverride &m) {
    if (m.bins.empty()) return false;
    if (m.max_repeat_run < 1 || m.max_repeat_run > 32) return false;
    if (m.local_jitter < 0 || m.local_jitter > 256) return false;
    uint32_t sum = 0;
    for (const auto &b : m.bins) {
      if (b.lo > b.hi) return false;
      if (b.lo < p.drs.min_payload_cap || b.hi > p.drs.max_payload_cap) return false;
      if (b.weight == 0) return false;
      sum += b.weight;
    }
    return sum > 0;
  };
  if (!validate_phase(p.drs.slow_start)) return false;
  if (!validate_phase(p.drs.congestion_open)) return false;
  if (!validate_phase(p.drs.steady_state)) return false;

        // Profile weights: class-specific weights must sum to exactly 100.
        int desktop_darwin_sum = p.weights.desktop_darwin.chrome133 + p.weights.desktop_darwin.chrome131 +
          p.weights.desktop_darwin.chrome120 +
          p.weights.desktop_darwin.safari26_3 + p.weights.desktop_darwin.firefox148;
        if (desktop_darwin_sum != 100) return false;

        int desktop_non_darwin_sum = p.weights.desktop_non_darwin.chrome133 + p.weights.desktop_non_darwin.chrome131 +
          p.weights.desktop_non_darwin.chrome120 +
          p.weights.desktop_non_darwin.safari26_3 + p.weights.desktop_non_darwin.firefox148;
        if (desktop_non_darwin_sum != 100) return false;
        if (p.weights.desktop_non_darwin.safari26_3 != 0) return false;

    int mobile_sum = p.weights.mobile.ios14 + p.weights.mobile.android11_okhttp_advisory;
    if (mobile_sum != 100) return false;

    // Cross-class drift must stay disabled by default.
    if (p.weights.allow_cross_class_rotation) return false;

    if (p.platform_hints.device_class != DeviceClass::Desktop &&
      p.platform_hints.device_class != DeviceClass::Mobile) return false;
    if (p.platform_hints.device_class == DeviceClass::Desktop &&
      p.platform_hints.mobile_os != MobileOs::None) return false;
    if (p.platform_hints.device_class == DeviceClass::Mobile &&
      p.platform_hints.mobile_os == MobileOs::None) return false;
    if (p.platform_hints.device_class == DeviceClass::Mobile &&
      p.platform_hints.desktop_os != DesktopOs::Unknown) return false;

  // Route policy validation.
  if (p.active_policy != PolicyName::Unknown &&
      p.active_policy != PolicyName::RuEgress &&
      p.active_policy != PolicyName::NonRuEgress) return false;

  if (p.route_policy.unknown.allow_quic) return false;
  if (p.route_policy.unknown.ech_mode != EchMode::Disabled) return false;

  if (p.route_policy.ru_egress.allow_quic) return false;  // TCP+TLS only in this design.
  if (p.route_policy.ru_egress.ech_mode != EchMode::Disabled) return false;

  if (p.route_policy.non_ru_egress.ech_mode != EchMode::Disabled &&
      p.route_policy.non_ru_egress.ech_mode != EchMode::Rfc9180Outer) return false;
  if (p.route_policy.non_ru_egress.allow_quic) return false;

  if (p.route_failure.ech_fail_open_threshold < 1 || p.route_failure.ech_fail_open_threshold > 10) return false;
  if (p.route_failure.ech_disable_ttl_sec < 60 || p.route_failure.ech_disable_ttl_sec > 86400) return false;
    if (p.route_failure.failure_kinds.empty()) return false;
    if (!p.route_failure.persist_across_restart) return false;

    // Flow-behavior validation (S18/S19 guardrails).
    if (p.flow_behavior.max_connects_per_10s_per_destination < 1 ||
      p.flow_behavior.max_connects_per_10s_per_destination > 30) return false;
    if (p.flow_behavior.min_reuse_ratio < 0.0 || p.flow_behavior.min_reuse_ratio > 1.0) return false;
    if (p.flow_behavior.min_conn_lifetime_ms < 200 || p.flow_behavior.min_conn_lifetime_ms > 600000) return false;
    if (p.flow_behavior.max_conn_lifetime_ms < p.flow_behavior.min_conn_lifetime_ms ||
      p.flow_behavior.max_conn_lifetime_ms > 3600000) return false;
    if (p.flow_behavior.max_destination_share <= 0.0 || p.flow_behavior.max_destination_share > 1.0) return false;
    if (p.flow_behavior.sticky_domain_rotation_window_sec < 60 ||
      p.flow_behavior.sticky_domain_rotation_window_sec > 86400) return false;
    if (p.flow_behavior.anti_churn_min_reconnect_interval_ms < 50 ||
      p.flow_behavior.anti_churn_min_reconnect_interval_ms > 60000) return false;

  // PR-8 has no built-in geo classifier: "auto" route selection is forbidden here.
  // Runtime route-aware switching can be added later via explicit route hints input.

  return true;
}
```

## 12.4 Критические ограничения PR-8 (честно, без иллюзий)

1. PR-8 не делает автоматическое определение `ru_egress/non_ru_egress`.
2. До появления отдельного route-hint источника выбор policy делается только через `active_policy`.
3. Если route-hint недоступен или сомнителен, применяется `unknown` (ECH disabled).
4. Конфигурация не должна пытаться включить QUIC/HTTP3: это вне текущей архитектуры TCP+TLS и только увеличивает поверхность блока.
5. Ошибочный JSON не должен приводить к переключению на «частично применённые» параметры; всегда last-known-good.
6. Состояние `RouteFailureState` (ECH circuit breaker) хранится в TTL-кеше между соединениями; in-memory состояние одного соединения недостаточно.
7. Состояние `RouteFailureState` обязательно переживает restart процесса (persistent TTL cache); иначе ECH может флапать при каждом перезапуске.
8. Owner S18/S19 зафиксирован в этом плане: PR-8 реализует policy hooks (`max_connects_per_10s_per_destination`, `max_destination_share`, `sticky_domain_rotation_window_sec`, `anti_churn_min_reconnect_interval_ms`), PR-9 только проверяет их соблюдение.

---

# 13. PR-9: Integration Smoke Tests

**Зависит от:** PR-8 (runtime params + route policy + profile registry уже в production пути)

PR-9 — это не «один скрипт на JA3», а fail-closed smoke-stage из 4 проверок: structural-family, IPT, DRS, flow-behavior.
Цель PR-9: поймать регрессии, которые сразу поднимают стоимость детекта вниз (и блок вверх) для RU DPI.

## 13.0 Обязательная матрица сценариев

Проверки проводятся отдельно по 3 route-policy сценариям (не смешивать в одну выборку):

| Сценарий | `active_policy` | Ожидание по ECH | Ожидание по QUIC |
|---|---|---|---|
| Unknown fallback | `unknown` | ECH отсутствует (`0xFE0D` и `0xFE02` отсутствуют) | disabled |
| RU egress | `ru_egress` | ECH отсутствует (`0xFE0D` и `0xFE02` отсутствуют) | disabled |
| non-RU egress | `non_ru_egress` | только `0xFE0D` по registry policy, `0xFE02` запрещен | disabled |

Любой запуск, где режим маршрута не подтвержден метаданными, считается невалидным и должен завершаться FAIL.

## 13.1 `check_fingerprint.py` — snapshot-driven structural-family checks + advisory JA3/JA4 telemetry

Ключевой принцип: `profiles_validation.json` — единственный source-of-truth для profile-инвариантов.
Никаких hardcoded «магических» значений в коде smoke-скрипта (включая `0x6399`, фиксированные Safari/Firefox предположения и т.д.).

Политика использования JA3/JA4 (обязательно):

- Алгоритм JA3 используется как совместимый индикатор (GREASE-stripped canonical string + MD5), но не как единственный детектор.
- Для Chrome-family профилей с intentional randomization exact JA3 hash не считается стабильным profile ID: shuffle, GREASE, ECH lane и соседние wire-вариации могут менять hash между соединениями.
- Exact JA4 token тоже не должен считаться «вечной константой профиля»: он полезен как auxiliary observed feature, но release verdict не может требовать один фиксированный JA4 для randomized Chrome-family lane.
- Репозиторий `salesforce/ja3` архивирован (read-only), поэтому его списки (`*.csv`) не могут быть source-of-truth для allow/block решений.
- Любой verdict в PR-9 опирается прежде всего на структурные инварианты ClientHello (extensions/order/pq/alps/ech policy); JA3/JA4 используются только как auxiliary telemetry, anti-Telegram denylist и anti-collapse signals.
- Если используются данные из legacy JA3-списков, они проходят локальную валидацию на capture-наборе и маркируются как advisory IOC, не как authoritative mapping.
- При вендоринге материалов из JA3-репозитория должна сохраняться атрибуция и license-notice (BSD-3-Clause).

## 13.1.0 Strict Fixture-Tagging Format (anti-contamination)

Чтобы mixed-corpus contamination был невозможен технически, `profiles_validation.json` должен быть fail-closed и fixture-addressable:

1. Каждый fixture имеет immutable `fixture_id` и обязательные теги происхождения (`source_kind`, `family`, `platform_class`, `tls_gen`).
2. Каждый profile ссылается только на явный список `include_fixture_ids` (никаких wildcard/source-path фильтров внутри profile).
3. Агрегация observed JA3/JA4 telemetry и структурных инвариантов разрешена только по fixture'ам, прошедшим `allowed_tags`.
4. Любой unknown tag, missing tag, unknown `fixture_id`, hash mismatch или mixed family/source вне allowlist -> немедленный FAIL.
5. Перекрёстное смешивание `legacy_tls12` и `modern_browser_tls13` в одном profile запрещено, кроме явно выделенного compatibility-profile.
6. `source_path` и `source_sha256` обязательны для каждого fixture; registry без reproducible provenance невалиден.
7. Теги `family` и `tls_gen` назначаются только parser-пайплайном из wire-фактов (extensions/versions/ALPN/transport), а не вручную; ручной override без signed-обоснования -> FAIL.
8. Любые synthetic/mask-only источники (например, xray `finalmask/sudoku`) маркируются отдельным `source_kind` и не могут входить в release-gating browser profiles.
9. Для каждого fixture обязателен tag `transport` (`tcp_tls` или `udp_quic_tls`); при `tcp_only=true` любые `udp_quic_tls` fixture автоматически исключаются из gating.
10. При каждом расширении corpus обязателен provenance-delta отчёт (новые fixture_id, changed tags, parser_version drift); без него registry update не проходит.

## 13.1.1 Fixture provenance strictness (continuous QA)

Чтобы ошибка маркировки (`family/tls_gen/source_kind/transport`) не «тихо отравляла» profile policy при росте corpus, вводится постоянный QA-контур:

1. Double-pass label check: первичная разметка parser'ом + независимая проверка consistency-валидатором; расхождение -> FAIL.
2. Label-stability gate: если после обновления parser version более 1% fixture меняют `family` или `tls_gen`, релизный gating блокируется до ручного triage.
3. Canary contamination tests: тестовый набор с намеренно неверными tag'ами (`family` mismatch, `tls_gen` mismatch, `transport` mismatch) обязан падать в CI.
4. Source separation: browser-capture и synthetic/xray/QUIC advisory корпуса агрегируются в разные family lane; cross-lane aggregation запрещена.
5. Любой profile, который опирается на fixture с `trust_tier != verified`, может быть только advisory и не участвует в release verdict.
6. Для каждого release хранится frozen snapshot `profiles_validation.json + source_sha256` для воспроизводимого аудита.

Минимальный schema-контур (strict fixture tagging, fail-closed):

```json
{
  "version": "2026-04-05",
  "parser_version": "tls-parser-v3",
  "contamination_guard": {
    "fail_on_unknown_fixture_id": true,
    "fail_on_unknown_tag": true,
    "fail_on_missing_required_tag": true,
    "fail_on_source_hash_mismatch": true,
    "allow_mixed_source_kind_per_profile": false,
    "allow_mixed_family_per_profile": false,
    "allow_legacy_tls12_in_modern_profiles": false,
    "allow_advisory_code_sample_per_profile": false,
    "require_network_derived_fixture_for_release_profile": true,
    "require_independent_corroborating_source": true
  },
  "source": [
    "docs/Samples/Traffic dumps/*.pcap*",
    "docs/Samples/utls-code/*",
    "docs/Samples/scrapy-impersonate/capture_chrome131.py"
  ],
  "fixtures": {
    "fx_ch131_0001": {
      "source_path": "docs/Samples/Traffic dumps/test_logs.pcapng",
      "source_sha256": "<sha256>",
      "source_kind": "browser_capture",
      "parser_version": "tls-parser-v3",
      "capture_date_utc": "<utc-timestamp>",
      "frame_ref": "tcp.stream==... && tls.handshake.type==1",
      "transport": "tcp",
      "family": "chrome131_like",
      "platform_class": "desktop",
      "os_family": "linux",
      "tls_gen": "tls13",
      "trust_tier": "verified"
    },
    "fx_ch131_cffi_0001": {
      "source_path": "docs/Samples/scrapy-impersonate/capture_chrome131.py",
      "source_sha256": "<sha256>",
      "source_kind": "curl_cffi_capture",
      "parser_version": "tls-parser-v3",
      "capture_tool": "curl_cffi",
      "capture_tool_version": "<version>",
      "curl_cffi_version": "<version>",
      "browser_type": "chrome131",
      "target_host": "cloudflare.com",
      "intercept_method": "localhost_connect_mitm",
      "capture_date_utc": "<utc-timestamp>",
      "transport": "tcp",
      "family": "chrome131_like",
      "platform_class": "desktop",
      "os_family": "linux",
      "tls_gen": "tls13",
      "trust_tier": "corroborated"
    },
    "fx_ch131_utls_0001": {
      "source_path": "docs/Samples/utls-code/u_parrots.go",
      "source_sha256": "<sha256>",
      "source_kind": "utls_snapshot",
      "snapshot_date": "<utc-date>",
      "hello_id": "HelloChrome_131",
      "extension_variant": "ApplicationSettingsExtension",
      "pq_variant": "X25519MLKEM768",
      "family": "chrome131_like",
      "platform_class": "desktop",
      "tls_gen": "tls13",
      "trust_tier": "corroborating"
    },
    "fx_ch_classic_0007": {
      "source_path": "docs/Samples/Traffic dumps/ur66.ru.pcap",
      "source_sha256": "<sha256>",
      "source_kind": "browser_capture",
      "parser_version": "tls-parser-v3",
      "capture_date_utc": "<utc-timestamp>",
      "frame_ref": "tcp.stream==... && tls.handshake.type==1",
      "transport": "tcp",
      "family": "chrome_classic_no_pq_like",
      "platform_class": "desktop",
      "os_family": "unknown",
      "tls_gen": "tls13",
      "trust_tier": "verified"
    }
  },
  "profiles": {
    "Chrome133": {
      "include_fixture_ids": ["<explicit-fixture-id>"],
      "allowed_tags": {
        "source_kind": ["browser_capture", "curl_cffi_capture", "utls_snapshot"],
        "family": ["chrome133_like"],
        "platform_class": ["desktop"],
        "tls_gen": ["tls13"]
      },
      "ja3_observed_hashes": ["<capture-derived-advisory>"],
      "ja4_observed_hashes": ["<capture-derived-advisory>"],
      "fingerprint_policy": {
        "allow_exact_ja3_pin": false,
        "allow_exact_ja4_pin": false,
        "require_structural_match": true,
        "require_anti_telegram_ja3": true,
        "require_noncollapsed_randomized_hashes": true,
        "min_unique_ja3_per_64": "<capture-derived-or-null>",
        "min_unique_ja4_per_64": "<capture-derived-or-null>"
      },
      "pq_group": "<fixture-derived-or-null>",
      "alps_type": "<fixture-derived:0x44CD|null>",
      "ech_type": "<fixture-derived:0xFE0D|null>",
      "supported_versions_requires_grease": true,
      "edge_grease": "fixture-driven"
    },
    "Chrome131": {
      "include_fixture_ids": ["fx_ch131_0001", "fx_ch131_cffi_0001", "fx_ch131_utls_0001"],
      "allowed_tags": {
        "source_kind": ["browser_capture", "curl_cffi_capture", "utls_snapshot"],
        "family": ["chrome131_like"],
        "platform_class": ["desktop"],
        "tls_gen": ["tls13"]
      },
      "ja3_observed_hashes": ["<capture-derived-advisory>"],
      "ja4_observed_hashes": ["<capture-derived-advisory>"],
      "fingerprint_policy": {
        "allow_exact_ja3_pin": false,
        "allow_exact_ja4_pin": false,
        "require_structural_match": true,
        "require_anti_telegram_ja3": true,
        "require_noncollapsed_randomized_hashes": true,
        "min_unique_ja3_per_64": "<capture-derived-or-null>",
        "min_unique_ja4_per_64": "<capture-derived-or-null>"
      },
      "pq_group": "<fixture-derived-or-null>",
      "alps_type": "<fixture-derived:0x4469|0x44CD|null>",
      "ech_type": "<fixture-derived:0xFE0D|null>",
      "supported_versions_requires_grease": true,
      "edge_grease": "fixture-driven"
    },
    "Chrome120": {
      "include_fixture_ids": ["<explicit-fixture-id>"],
      "allowed_tags": {
        "source_kind": ["browser_capture", "curl_cffi_capture", "utls_snapshot"],
        "family": ["chrome120_like"],
        "platform_class": ["desktop"],
        "tls_gen": ["tls13"]
      },
      "ja3_observed_hashes": ["<capture-derived-advisory>"],
      "ja4_observed_hashes": ["<capture-derived-advisory>"],
      "fingerprint_policy": {
        "allow_exact_ja3_pin": false,
        "allow_exact_ja4_pin": false,
        "require_structural_match": true,
        "require_anti_telegram_ja3": true,
        "require_noncollapsed_randomized_hashes": true,
        "min_unique_ja3_per_64": "<capture-derived-or-null>",
        "min_unique_ja4_per_64": "<capture-derived-or-null>"
      },
      "pq_group": "<fixture-derived-or-null>",
      "alps_type": "<fixture-derived:0x4469|0x44CD|null>",
      "ech_type": "<fixture-derived:0xFE0D|null>",
      "supported_versions_requires_grease": true,
      "edge_grease": "fixture-driven"
    },
    "Firefox148": {
      "include_fixture_ids": ["<explicit-fixture-id>"],
      "allowed_tags": {
        "source_kind": ["browser_capture", "curl_cffi_capture", "utls_snapshot"],
        "family": ["firefox148_like"],
        "platform_class": ["desktop"],
        "tls_gen": ["tls13"]
      },
      "ja3_observed_hashes": ["<capture-derived-advisory>"],
      "ja4_observed_hashes": ["<capture-derived-advisory>"],
      "fingerprint_policy": {
        "allow_exact_ja3_pin": false,
        "allow_exact_ja4_pin": false,
        "require_structural_match": true,
        "require_anti_telegram_ja3": true,
        "require_noncollapsed_randomized_hashes": false
      }
    },
    "Safari26_3": {
      "include_fixture_ids": ["<explicit-fixture-id>"],
      "allowed_tags": {
        "source_kind": ["browser_capture", "curl_cffi_capture", "utls_snapshot"],
        "family": ["safari26_3_like"],
        "platform_class": ["desktop"],
        "os_family": ["darwin"],
        "tls_gen": ["tls13"]
      },
      "ja3_observed_hashes": ["<capture-derived-advisory>"],
      "ja4_observed_hashes": ["<capture-derived-advisory>"],
      "fingerprint_policy": {
        "allow_exact_ja3_pin": false,
        "allow_exact_ja4_pin": false,
        "require_structural_match": true,
        "require_anti_telegram_ja3": true,
        "require_noncollapsed_randomized_hashes": false
      }
    },
    "IOS14": {
      "include_fixture_ids": ["<explicit-fixture-id>"],
      "allowed_tags": {
        "source_kind": ["browser_capture", "curl_cffi_capture", "utls_snapshot"],
        "family": ["ios14_like"],
        "platform_class": ["mobile"],
        "os_family": ["ios"],
        "tls_gen": ["tls13"]
      },
      "ja3_observed_hashes": ["<capture-derived-advisory>"],
      "ja4_observed_hashes": ["<capture-derived-advisory>"],
      "fingerprint_policy": {
        "allow_exact_ja3_pin": false,
        "allow_exact_ja4_pin": false,
        "require_structural_match": true,
        "require_anti_telegram_ja3": true,
        "require_noncollapsed_randomized_hashes": false
      },
      "device_class": "mobile"
    },
    "Android11_OkHttp_Advisory": {
      "include_fixture_ids": ["<explicit-fixture-id>"],
      "allowed_tags": {
        "source_kind": ["browser_capture", "curl_cffi_capture", "utls_snapshot"],
        "family": ["android11_okhttp_like"],
        "platform_class": ["mobile"],
        "os_family": ["android"],
        "tls_gen": ["tls13"]
      },
      "ja3_observed_hashes": ["<capture-derived-advisory>"],
      "ja4_observed_hashes": ["<capture-derived-advisory>"],
      "fingerprint_policy": {
        "allow_exact_ja3_pin": false,
        "allow_exact_ja4_pin": false,
        "require_structural_match": true,
        "require_anti_telegram_ja3": true,
        "require_noncollapsed_randomized_hashes": false
      },
      "device_class": "mobile"
    }
  }
}
```

Минимальный валидационный контракт для loader/checker:

- перед вычислением profile structural policy строится `resolved_fixture_set = include_fixture_ids`;
- для каждого fixture проверяется: id существует, required tags присутствуют, `source_sha256` совпадает с файлом;
- каждый fixture обязан пройти `allowed_tags` текущего profile;
- если во множестве profile более одного `family` или `source_kind` и это не разрешено guard-политикой -> FAIL;
- если среди `include_fixture_ids` release-gating profile нет ни одного `browser_capture` или `curl_cffi_capture` -> FAIL;
- если среди `include_fixture_ids` нет независимого corroborating source помимо primary network-derived fixture -> FAIL;
- если в `include_fixture_ids` попадает `advisory_code_sample` (например, `docs/Samples/xray-core-code/*`) -> FAIL;
- если fixture-набор profile содержит конфликтующие ALPS/ECH ожидания между family lane (например, `0x4469` и `0x44CD` без явного family split) -> FAIL для release-gating;
- если profile помечен как randomized Chrome-family, exact `ja3_observed_hashes`/`ja4_observed_hashes` не могут использоваться как equality-gate; разрешены только anti-Telegram denylist, dispersion floor и structural checks;
- profile с пустым `include_fixture_ids` или с `<advisory>`-only fixture-набором не может быть release-gating profile.
- при `transport_policy.tcp_only=true` в gating набор попадают только `tcp && tls.handshake.type==1`; QUIC (`udp:quic:tls`) ведётся отдельным advisory-треком и не влияет на TCP ALPN policy verdict.

### 13.1.2 Fixture completion gate (release-blocking)

Текущий status в этом файле:

- `Chrome131` использует реальный fixture id (`fx_ch131_0001`);
- `Chrome133`, `Chrome120`, `Firefox148`, `Safari26_3`, `IOS14`, `Android11_OkHttp_Advisory` пока содержат `"<explicit-fixture-id>"` placeholder.
- следующий verified lane должен быть `chrome133_like`, а не произвольный non-Chromium placeholder: локальная доказательная база для него сильнее всего (`HelloChrome_133` в bundled uTLS, `capture_chrome131.py --profile chrome133`, текущий runtime lane уже ближе к `ALPS=0x44CD` Chromium-family).

Нормативный release-контракт:

1. До release-gating PR-9 все placeholder значения `"<explicit-fixture-id>"` должны быть заменены на реальные fixture ids.
2. Для каждого такого fixture обязателен `source_sha256` и валидный provenance-tag набор.
3. CI smoke обязан падать fail-closed, если в registry остаётся хотя бы один placeholder fixture id.
4. Каждый release-gating profile обязан иметь хотя бы один network-derived fixture и хотя бы один независимый corroborating source; профиль без этого не может перейти из advisory в verified.

```python
#!/usr/bin/env python3
"""
Collect ClientHello samples from tdlib->telemt local path and validate:
1) anti-Telegram signature/family guardrails,
2) route-policy/ECH correctness,
3) profile-registry invariants (ALPS/PQ/GREASE/cipher/ALPN policy),
4) ECH wire-structure consistency.

Usage:
  python test/analysis/check_fingerprint.py --pcap out.pcapng --registry profiles_validation.json
"""

KNOWN_TELEGRAM_JA3 = {
    "e0e58235789a753608b12649376e91ec",
}

ROUTE_MODES = {"unknown", "ru_egress", "non_ru_egress"}
REGISTRY = load_profile_registry("profiles_validation.json")

def check_ech_policy(ch: ClientHello) -> bool:
    mode = ch.metadata.route_mode
    has_old = any(ext.type == 0xFE02 for ext in ch.extensions)
    has_new = any(ext.type == 0xFE0D for ext in ch.extensions)
    if has_old:
        return False
    if mode in ("unknown", "ru_egress"):
        return not has_new
    if mode == "non_ru_egress":
        return profile_requires_ech(ch.profile, REGISTRY) == has_new
    return False

def check_pq_group_policy(ch: ClientHello) -> bool:
    expected = expected_pq_group(ch.profile, REGISTRY)  # int | None
    if expected is None:
        return not has_known_pq_groups(ch.supported_groups + ch.key_share_groups)
    return expected in ch.supported_groups and expected in ch.key_share_groups

def check_alps_policy(ch: ClientHello) -> bool:
    expected = expected_alps_type(ch.profile, ch.metadata.fixture_family_id, REGISTRY)  # int | None
    if expected is None:
        return True
    if expected == 0x4469:
        return has_extension(ch, 0x4469) and not has_extension(ch, 0x44CD)
    if expected == 0x44CD:
        return has_extension(ch, 0x44CD) and not has_extension(ch, 0x4469)
    return has_extension(ch, expected)

def check_alpn_policy(ch: ClientHello) -> bool:
    advertised = set(ch.alpn_protocols)
    expected = expected_alpn_set(ch.profile, REGISTRY)  # set[str] | None
    if expected is not None and advertised != set(expected):
        return False
    tcp_only = REGISTRY.get("transport_policy", {}).get("tcp_only", True)
    if tcp_only and ch.metadata.transport != "tcp":
        return True  # out-of-scope for TCP masking verdict
    # In this plan QUIC is disabled; h3 must not leak into TCP-only masking profiles.
    if tcp_only and "h3" in advertised:
        return False
    return True

def check_ech_outer_lengths(ch: ClientHello) -> bool:
    ext = extract_extension(ch, 0xFE0D)
    if ext is None:
        return True
    parsed = parse_ech_outer(ext)
    return parsed.enc_key_len == parsed.actual_enc_key_len

def check_not_forced_padding_517(samples: list[ClientHello]) -> bool:
    # Evaluate per-profile to avoid false signal from mixed-profile runs.
    for profile, group in group_by_profile(samples).items():
        lengths = [len(ch.raw) for ch in group]
        if len(lengths) >= 20 and all(x == 517 for x in lengths):
            return False
    return True

def check_ja3_canonicalization(ch: ClientHello) -> bool:
    # JA3 must be computed from GREASE-stripped decimal tuple according to algorithm.
    # We keep both raw tuple string and md5 hash in report for auditability.
    raw_tuple = canonical_ja3_tuple(ch, ignore_grease=True)
    md5_hash = md5_hex(raw_tuple)
    return raw_tuple != "" and len(md5_hash) == 32

def check_randomized_fingerprint_dispersion(samples: list[ClientHello]) -> bool:
  for profile, group in group_by_profile(samples).items():
    policy = REGISTRY["profiles"][profile].get("fingerprint_policy", {})
    if not policy.get("require_noncollapsed_randomized_hashes", False):
      continue
    if len(group) < 64:
      return False
    ja3_count = len({compute_ja3(ch) for ch in group})
    ja4_count = len({compute_ja4(ch) for ch in group})
    min_ja3 = policy.get("min_unique_ja3_per_64")
    min_ja4 = policy.get("min_unique_ja4_per_64")
    if min_ja3 is not None and ja3_count < min_ja3:
      return False
    if min_ja4 is not None and ja4_count < min_ja4:
      return False
  return True

SAMPLE_CHECKS = [
    ("JA3 not Telegram", lambda ch: compute_ja3(ch) not in KNOWN_TELEGRAM_JA3),
    ("JA3 canonicalization valid", check_ja3_canonicalization),
    ("Route mode known", lambda ch: ch.metadata.route_mode in ROUTE_MODES),
    ("Profile class matches device class", lambda ch: profile_matches_device_class(ch.profile, ch.metadata.device_class, REGISTRY)),
    ("Profile matches platform hints", lambda ch: profile_matches_platform(ch.profile, ch.metadata.device_class, ch.metadata.os_family, REGISTRY)),
    ("ECH route policy", check_ech_policy),
    ("No legacy ECH 0xFE02", lambda ch: not has_extension(ch, 0xFE02)),
    ("ECH outer lengths consistent", check_ech_outer_lengths),
    ("PQ group policy", check_pq_group_policy),
    ("ALPS policy", check_alps_policy),
    ("ALPN policy (TCP mode)", check_alpn_policy),
    ("GREASE policy", lambda ch: check_grease_policy(ch, REGISTRY)),
    ("Cipher policy", lambda ch: check_cipher_policy(ch, REGISTRY)),
]

BATCH_CHECKS = [
    ("Min samples per scenario", check_min_samples_per_scenario),
    ("ECH payload variance (non-RU, ECH-enabled profiles)", check_ech_payload_variance),
    ("Padding not forced to 517", check_not_forced_padding_517),
    ("Randomized Chrome JA3/JA4 do not collapse", check_randomized_fingerprint_dispersion),
  # check_server_hello_matrix uses parsed ServerHello artifacts from the same scenario.
    ("ServerHello matrix valid", check_server_hello_matrix),
    ("No JA3-only or JA4-only decision path", check_no_hash_only_verdict_path),
]
```

### 13.1.1 Обязательные helper-контракты

Минимальный набор структур/функций должен жить в `test/analysis/common_tls.py` и использоваться из `test/analysis/check_fingerprint.py`.
Требование: full TLS parse. Regex/substring-parser запрещен.

```python
from dataclasses import dataclass

@dataclass(frozen=True)
class ParsedExtension:
  type: int
  body: bytes

@dataclass(frozen=True)
class SampleMeta:
  route_mode: str      # unknown | ru_egress | non_ru_egress
  device_class: str    # desktop | mobile
  os_family: str       # darwin | windows | linux | ios | android | unknown
  transport: str       # tcp | quic | other
  fixture_family_id: str
  scenario_id: str     # unique run/scenario identifier
  ts_us: int

@dataclass(frozen=True)
class ClientHello:
  raw: bytes
  profile: str
  extensions: list[ParsedExtension]
  cipher_suites: list[int]
  supported_groups: list[int]
  key_share_groups: list[int]
  metadata: SampleMeta

@dataclass(frozen=True)
class ServerHello:
  raw: bytes
  selected_version: int | None
  cipher_suite: int
  selected_group: int | None
  extensions: list[ParsedExtension]
  metadata: SampleMeta

@dataclass(frozen=True)
class FingerprintStruct:
  cipher_suites: tuple[int, ...]      # without GREASE placeholders
  extensions_order: tuple[int, ...]   # extension IDs in wire order
  supported_groups: tuple[int, ...]   # without GREASE
  key_share_groups: tuple[int, ...]   # without GREASE
  alps_type: int | None               # 0x4469 / 0x44CD / None
  ech_type: int | None                # 0xFE0D / None
  pq_group: int | None                # e.g. 0x11EC / None

def load_profile_registry(path: str) -> dict: ...
def parse_client_hello(raw: bytes, meta: SampleMeta) -> ClientHello: ...
def parse_server_hello(raw: bytes, meta: SampleMeta) -> ServerHello: ...
def parse_ech_outer(ext: ParsedExtension) -> ParsedEchOuter: ...
def canonical_fingerprint_struct(ch: ClientHello) -> FingerprintStruct: ...
def compute_ja3(ch: ClientHello) -> str: ...
def compute_ja4(ch: ClientHello) -> str: ...

# IMPORTANT:
# - exact JA3/JA4 values are audit/report fields, not stable profile primary keys
#   for randomized Chrome-family lanes;
# - release verdicts for Chrome-family profiles are based on structural invariants,
#   anti-Telegram denylist checks, and non-collapsed observed hash/token sets.
```

### 13.1.2 Порядок выполнения

```python
def run_all_checks(samples: list[ClientHello]) -> tuple[bool, list[str]]:
  failures: list[str] = []

  for idx, ch in enumerate(samples):
    for name, fn in SAMPLE_CHECKS:
      if not bool(fn(ch)):
        failures.append(f"sample[{idx}]: {name}")

  for name, fn in BATCH_CHECKS:
    if not bool(fn(samples)):
      failures.append(f"batch: {name}")

  return (len(failures) == 0), failures
```

### 13.1.3 Fail-closed условия запуска

```python
# Hard requirements:
# - >=50 ClientHello samples for each scenario (unknown, ru_egress, non_ru_egress)
# - every sample parsed by full TLS parser (0 parse-fallbacks)
# - registry schema is valid and version is pinned for this smoke run
# - route_mode and scenario_id are present for every sample
# - output report JSON + failing sample indices are written to artifacts/
#
# Any violation => immediate FAIL (exit code != 0)
```

### 13.1.4 Строгий контракт `check_ech_payload_variance` (anti-singleton)

```python
def check_ech_payload_variance(samples: list[ClientHello]) -> bool:
  # Analyze only lanes where ECH is expected by route/profile policy.
  scoped = [
    ch for ch in samples
    if ch.metadata.route_mode == "non_ru_egress"
    and profile_requires_ech(ch.profile, REGISTRY)
  ]
  if len(scoped) < 64:
    return False

  allowed = {144, 176, 208, 240}
  lengths = [extract_ech_payload_len(ch) for ch in scoped]
  if any(x not in allowed for x in lengths):
    return False

  # Core regression guard: process-wide singleton should be impossible.
  return len(set(lengths)) >= 2
```

Норматив: этот check запускается per-scenario/per-profile внутри одного runtime процесса. Aggregation между profile-family запрещена.

### 13.1.5 Строгий контракт `check_server_hello_matrix` (anti-synthetic)

```python
def check_server_hello_matrix(samples: list[ClientHello], sh_samples: list[ServerHello]) -> bool:
  # 1) Full parse only; no regex/substring parser fallback.
  if any(sh is None for sh in sh_samples):
    return False
  if any(not sh.metadata.fixture_id or not sh.metadata.parser_version for sh in sh_samples):
    return False
  if any(sh.metadata.parser_version != REGISTRY.parser_version for sh in sh_samples):
    return False

  # 2) Compare against fixture-approved server matrices for profile family.
  #    Matrix key: (selected_version, cipher_suite, tuple(server_extensions_order)).
  for sh in sh_samples:
    key = (sh.selected_version, sh.cipher_suite, tuple(ext.type for ext in sh.extensions))
    if not is_allowed_serverhello_key(sh.metadata.fixture_family_id, key, REGISTRY):
      return False

  # 3) Explicit synthetic-layout guard derived from current tdlib wait_hello_response path.
  #    If all handshakes collapse to one fixed record-layout signature, fail.
  layouts = [extract_record_layout_signature(sh.raw) for sh in sh_samples]
  if len(layouts) >= 50 and len(set(layouts)) == 1:
    return False

  return True
```

Базовый capture reference (ревизия 2026-04-05, `docs/Samples/Traffic dumps`):

- `test_logs.pcapng`: минимум 10 server extension-matrices; доминируют `43,51` и `51,43`, присутствуют `43,51,41` и TLS1.2-like `65281,16,23`;
- `Fire.pcapng`/`Yabr.pcapng`: одновременно встречаются TLS1.3-паттерны и TLS1.2-like ветви;
- во всём наборе наблюдается несколько record-layout семейств (`22`, `22,20`, `22,20,22`), поэтому fixed single-layout policy недопустим.

Правило источников для `check_server_hello_matrix`:

1. Для ServerHello allowlist authoritative source = только `browser_capture` / `pcap` с parser-level extraction response tuples.
2. `utls_snapshot` и `curl_cffi_capture` здесь не являются source-of-truth: это клиентские источники и они не должны формировать server tuple allowlist.
3. Любой server tuple, полученный не из network capture, маркируется `advisory` и не участвует в release verdict.
4. `xray-core`, `finalmask/sudoku` и прочие appearance-transform samples не используются ни для ServerHello tuple list, ни для record-layout allowlist.

### 13.1.6 Reproducible ServerHello Extraction Workflow

Чтобы `check_server_hello_matrix` не зависел от ручного triage по pcap и не дрейфовал между машинами, PR-10 обязан использовать отдельный parser-driven extraction step:

```bash
python test/analysis/extract_server_hello_fixtures.py \
  --pcap "docs/Samples/Traffic dumps/test_logs.pcapng" \
  --scenario non_ru_egress \
  --parser-version tls-serverhello-parser-v1 \
  --out artifacts/serverhello_test_logs.json
```

Норматив для extractor output:

1. Один входной `pcap/pcapng` -> один immutable JSON artifact с `source_path`, `source_sha256`, `parser_version`, `capture_date_utc`, `scenario_id`, `route_mode`.
2. Каждый tuple обязан содержать `frame_ref` или `tcp.stream`, `selected_version`, `cipher_suite`, `extensions`, `record_layout_signature`, `server_hello_random_offset`, `family_hint`.
3. Любой parser fallback, unknown extension-length path или truncated record помечается как ошибка extraction, а не silently-ignored sample.
4. `response_profiles.json` обновляется только merge-скриптом из этих artifacts; ручное добавление `allowed_serverhello` literal'ами запрещено.
5. При смене `parser_version` обязателен provenance-delta отчёт: какие fixture ids изменились, какие tuple/layout entries добавились или исчезли.

## 13.2 `check_ipt.py` — межпакетные интервалы (реально исполнимый контракт)

```python
def check_ipt(pcap_file: str, baseline_files: list[str]) -> bool:
    """
    Scope:
      - analyze only outbound TLS application records after handshake;
      - drop pure ACKs/retransmits/out-of-order segments before interval statistics.

    Statistics:
      - goodness-of-fit: K-S against fitted log-normal on interactive slices;
      - distance-to-baseline: EMD or Jensen-Shannon vs docs/Samples/Traffic dumps/*.pcap*.

    FAIL:
      - p_value < 0.05 on >=2 independent runs;
      - keepalive bypass p99 > 10ms on local loopback scenario;
      - detector-visible stalls: interval > 5s inside active flow (excluding idle windows).

    PASS:
      - log-normal fit not rejected on majority of runs;
      - keepalive bypass meets local-loopback bound;
      - baseline distance <= configured threshold from registry.
    """
```

## 13.3 `check_drs.py` — размеры TLS records (не TCP segment size)

```python
def check_drs(pcap_file: str, baseline_files: list[str]) -> bool:
    """
    Parse TLS record layer and evaluate record payload sizes.

    FAIL:
      - dominant mode == 2878 (legacy signature resurfaced);
      - >=10 consecutive records with identical payload size in active burst;
      - no growth beyond 8192 in long-flow windows where total payload >= 100KB;
      - histogram distance to HTTPS baselines exceeds configured threshold.

    PASS:
      - no 2878 dominant mode;
      - no long constant-size runs;
      - capture-driven slow_start -> congestion -> steady_state trend observed;
      - histogram distance stays below threshold.
    """
```

## 13.4 `check_flow_behavior.py` — cadence/reuse/destination concentration

```python
def check_flow_behavior(pcap_file: str, baseline_files: list[str], policy: dict) -> bool:
    """
    Parse TCP 5-tuples and destination buckets for emulate_tls sessions.

    FAIL:
      - reconnect-storm: connects_per_10s_per_destination > policy.max_connects_per_10s_per_destination;
      - reuse_ratio < policy.min_reuse_ratio in steady windows;
      - median connection lifetime < policy.min_conn_lifetime_ms;
      - long pinned socket anomaly: lifetime > policy.max_conn_lifetime_ms with sustained traffic.

    PASS:
      - cadence/reuse/lifetime stay within policy and capture-driven thresholds;
      - no detector-visible burst of short-lived reconnects to same destination;
      - no unnatural long-lived single-flow domination.
    """
```

## 13.5 Артефакты и воспроизводимость

Каждый smoke-run обязан сохранять:

- `artifacts/fingerprint_report.json` (per-sample + batch verdicts)
- `artifacts/ipt_report.json` (fit metrics + baseline distances)
- `artifacts/drs_report.json` (histogram + run-length stats)
- `artifacts/run_meta.json` (git SHA, registry version, scenario matrix, command line)

Без этих артефактов прогон считается невалидным.

## 13.6 PR-10: ServerHello Realism (tdlib + telemt)

S17 не может считаться закрытым только таблицей рисков. Нужен отдельный delivery-track.

### 13.6.1 Что именно сейчас synthetic

Текущий `td/mtproto/TlsInit.cpp::wait_hello_response()` валидирует ответ по жёсткому двухпрефиксному шаблону:

- сначала `\x16\x03\x03` + фиксированный skip-by-length;
- затем строго `\x14\x03\x03\x00\x01\x01\x17\x03\x03` + skip-by-length;
- после этого проверяется HMAC.

Это совместимо с текущим synthetic telemt-path, но не отражает реальное разнообразие ServerHello/record-layout в capture.

Критичное уточнение по смыслу этого кода: текущий validator не проверяет TLS-семантику ответа как таковую. Он принимает ровно две layout-фазы по literal record-prefix path: один handshake record (`16 03 03 ...`), затем сцепленный `CCS + ApplicationData` prefix (`14 03 03 00 01 01 17 03 03 ...`). Любой иной, но структурно валидный server path сейчас отбрасывается до HMAC. Это и есть корень S17: форма зашита сильнее, чем целостность.

### 13.6.2 Обязательные изменения на стороне tdlib

1. Вынести парсинг ответа в отдельный parser-level helper (полный TLS record parse, без substring matching).
2. Считать валидным набор из profile-allowlist record-layout/matrix, а не один literal-префикс.
3. HMAC-check сохранить, но отделить от конкретного fixed layout (инвариант целостности != инвариант формы).
4. Любой parse-fallback, unknown layout при release-gating и mismatch с profile allowlist -> FAIL (fail-closed).

### 13.6.2.1 Source-of-truth для ServerHello registry

Для response-profile registry действуют **другие** правила, чем для ClientHello registry:

1. Основной source-of-truth = только реальные `pcap/pcapng` captures с parser-level extraction ServerHello tuples и record-layout signatures.
2. `utls_snapshot` не используется для ServerHello вообще: bundled uTLS описывает клиентские hello-профили, но не browser-derived server response families.
3. `curl_cffi_capture` тоже не является ServerHello registry source-of-truth: он полезен для ClientHello baseline, но серверный ответ зависит от удалённого endpoint, CDN edge, negotiated path и capture environment.
4. Synthetic server outputs из telemt integration runs можно использовать только как smoke artifacts для сравнения с уже утверждённым pcap-derived allowlist, но не как первичный источник allowlist.
5. Если response-profile tuple не подтверждён pcap-derived fixture family, он остаётся advisory и не может попасть в release-gating `allowed_serverhello`.

### 13.6.3 Обязательные изменения на стороне telemt

1. Добавить response-profile registry (fixture-driven) для ServerHello.
2. Привязать выбор response-profile к тому же profile-family lane, что используется для ClientHello policy.
3. Не эмулировать QUIC/h3 в этом треке; стратегия остаётся TCP+TLS only для RU-sensitive маршрутов.
4. Не переносить xray finalmask/sudoku byte-mutation на TLS payload path; допустимы только shape-policy и response-profile matching.
5. Response-profile registry должен быть **pcap-derived**, а не построенным по аналогии с ClientHello uTLS snapshot. Клиентские snapshot-источники не годятся для server tuple allowlist.

### 13.6.4 Definition of Done для PR-10

PR-10 считается завершённым только если:

1. `check_server_hello_matrix` зелёный в сценариях `unknown`, `ru_egress`, `non_ru_egress`.
2. На каждом сценарии (>=100 handshakes) нет коллапса в один synthetic record-layout signature.
3. `wait_hello_response` больше не зависит от literal fixed-prefix сравнения.
4. tdlib и telemt используют согласованный response-profile registry commit (через shared params/submodule).

### 13.6.5 Draft implementation sketch (вставка в PR-10)

Ниже черновой технический draft для переноса в код без изменения security-инварианта HMAC.

#### 13.6.5.1 Новый parser seam в tdlib

```cpp
// td/mtproto/stealth/TlsServerHelloParser.h

namespace td {
namespace mtproto {
namespace stealth {

enum class ParseStage : uint8 {
  NeedMore,
  Ok,
  Error,
};

struct ParsedServerHelloEnvelope {
  size_t consumed_bytes{0};
  size_t server_hello_random_offset{0};  // offset in consumed envelope
  uint16 selected_version{0};
  uint16 cipher_suite{0};
  uint16 selected_group{0};              // 0 when not present
  vector<uint16> extension_order;
  vector<uint8> record_layout_signature; // e.g. {22}, {22,20}, {22,20,22}
};

struct ResponseProfilePolicy {
  // Allowed ServerHello tuples for current profile-family lane.
  // key: (version, cipher, ext-order hash)
  FlatHashSet<uint64> allowed_serverhello_keys;
  FlatHashSet<string> allowed_layout_signatures;
};

Result<ParsedServerHelloEnvelope> parse_server_hello_envelope(Slice input,
                                                               const ResponseProfilePolicy &policy);

}  // namespace stealth
}  // namespace mtproto
}  // namespace td
```

Ключевая политика parser:

1. Разбор только через TLS record/handshake parser path.
2. Возврат `Error` при неизвестной матрице в release-gating режиме (fail-closed).
3. Возврат `NeedMore` без частичного mutating-состояния.

#### 13.6.5.2 Рефактор `TlsInit::wait_hello_response()`

```cpp
Status TlsInit::wait_hello_response() {
  auto input = fd_.input_buffer().clone();
  auto parsed = stealth::parse_server_hello_envelope(input.as_slice(), response_policy_);

  if (parsed.is_error()) {
    if (parsed.error().code() == ErrorCode::NeedMore) {
      return Status::OK();
    }
    return Status::Error(parsed.error().message());
  }

  auto env = parsed.move_as_ok();
  auto response = fd_.input_buffer().cut_head(env.consumed_bytes).move_as_buffer_slice();

  // No hardcoded offset 11: offset comes from parser output.
  auto rnd = response.as_mutable_slice().substr(env.server_hello_random_offset, 32);
  auto rnd_copy = rnd.str();
  std::fill(rnd.begin(), rnd.end(), '\0');

  string hash_dest(32, '\0');
  hmac_sha256(password_, PSLICE() << hello_rand_ << response.as_slice(), hash_dest);
  if (hash_dest != rnd_copy) {
    return Status::Error("Response hash mismatch");
  }

  stop();
  return Status::OK();
}
```

Инвариант безопасности: HMAC-чек остается обязательным и выполняется после parser-level валидации структуры.

#### 13.6.5.3 Draft registry shape для telemt response-profile

```json
{
  "version": "2026-04-05",
  "parser_version": "tls-serverhello-parser-v1",
  "fixtures": {
    "srv_fx_tls13_0001": {
      "source_path": "docs/Samples/Traffic dumps/test_logs.pcapng",
      "source_sha256": "<sha256>",
      "parser_version": "tls-serverhello-parser-v1",
      "frame_ref": "tcp.stream==... && tls.handshake.type==2",
      "route_mode": "non_ru_egress",
      "family": "chrome131_like",
      "selected_version": 772,
      "cipher_suite": 4865,
      "extensions": [43, 51],
      "record_layout_signature": "22,20,22"
    }
  },
  "response_profiles": {
    "chrome131_like": {
      "include_fixture_ids": ["srv_fx_tls13_0001"],
      "allowed_layout_signatures": ["22", "22,20", "22,20,22"],
      "allowed_serverhello": [
        {"selected_version": 772, "cipher_suite": 4865, "extensions": [43, 51]},
        {"selected_version": 772, "cipher_suite": 4865, "extensions": [51, 43]},
        {"selected_version": 772, "cipher_suite": 4865, "extensions": [43, 51, 41]}
      ]
    },
    "legacy_tls12_like": {
      "allowed_layout_signatures": ["22", "22,20"],
      "allowed_serverhello": [
        {"selected_version": 771, "cipher_suite": 49168, "extensions": [65281, 16, 23]}
      ]
    }
  }
}
```

Примечание: exact tuples заполняются только из fixture-парсинга реальных response captures (`pcap/pcapng`) и последующего triage. Ни `utls_snapshot`, ни `curl_cffi_capture` не являются первичным источником для ServerHello tuple list; ручной literal drift запрещён.

#### 13.6.5.4 Draft тесты для PR-10

```cpp
// test/stealth/test_tls_init_response_parser.cpp

TEST(TlsInitResponseParser, AcceptsApprovedServerHelloLayouts);
TEST(TlsInitResponseParser, RejectsUnknownServerHelloLayout);
TEST(TlsInitResponseParser, NeedMoreIsNonFatalAndNonMutating);
TEST(TlsInitResponseParser, HmacCheckStillMandatoryAfterParserPass);
```

```python
# test/analysis/check_server_hello.py

def check_server_hello_matrix(samples, registry):
  # full parse -> parser_version/provenance check -> tuple extraction -> allowlist check
    # fail on unknown tuple/layout in gating scenarios
    ...
```

#### 13.6.5.5 Rollout plan (safe migration)

1. PR-10A: parser helper + unit tests, `wait_hello_response` пока без переключения runtime path.
2. PR-10B: feature-flagged switch на parser path (`stealth_response_parser=true`) + smoke in CI.
3. PR-10C: включение по умолчанию после трех провайдерных прогонов и green smoke matrix.

---

# 14. Таблица изменяемых файлов

## Изменения в существующих файлах

| Файл | Что изменяется | PR |
|---|---|---|
| `td/mtproto/IStreamTransport.h` | +5 defaulted virtual (pre_flush_write, get_shaping_wakeup, set_traffic_hint, set_max_tls_record_size, supports_tls_record_sizing) | PR-3 |
| `td/mtproto/IStreamTransport.cpp` | `create_transport()`: ветка stealth (единственный activation if) | PR-3 |
| `td/mtproto/TcpTransport.h` | `ObfuscatedTransport`: runtime TLS record cap (`set_max_tls_record_size`) + capability guard | PR-3 |
| `td/mtproto/TcpTransport.cpp` | `do_write_tls`: использует runtime `max_tls_packet_length_` вместо fixed constant path | PR-3 |
| `td/mtproto/TlsInit.cpp` | Клиентский hello-path переводится на internal builder/facade без изменения поведения текущего wire-format (PR-A); `wait_hello_response` переводится с fixed-prefix на parser-path (PR-10) | PR-A/PR-10 |
| `td/mtproto/stealth/TlsHelloBuilder.cpp` | Убрать process-wide ECH length sampling и fixed-517 padding path; `Type::EchPayload`/padding policy должны остаться context-driven, а не static-init-driven | PR-1 |
| `td/mtproto/RawConnection.h` | `send_crypto/send_no_crypto` получают `TrafficHint` для реального write-path wiring | PR-7 |
| `td/mtproto/RawConnection.cpp` | `flush_write()`: вызов `pre_flush_write` + проброс `get_shaping_wakeup` | PR-3 |
| `td/mtproto/RawConnection.cpp` | проброс `TrafficHint` в `transport_->set_traffic_hint(...)` в `send_crypto/send_no_crypto` | PR-7 |
| `td/mtproto/SessionConnection.cpp` | wakeup merge из `raw_connection_->shaping_wakeup_at()` (PR-3) + metadata-only classifier wiring (PR-7) | PR-3/PR-7 |
| `td/mtproto/SessionConnection.h` | объявление helper classifier/config fields (без фиктивного `auth_packets_remaining_`) | PR-7 |
| `CMakeLists.txt` | `TDLIB_STEALTH_SHAPING` option + `td/mtproto/stealth/*.cpp` sources | PR-3 |
| `test/CMakeLists.txt` | Подключение `test/stealth/*.cpp` к `run_all_tests` (включая `test_tls_init_response_parser.cpp`) | PR-A/PR-10 |

## Новые файлы

```
td/mtproto/stealth/
  Interfaces.h           IRng, IClock, NetworkRouteHints, PaddingPolicy      PR-A
  TlsHelloBuilder.h/cpp  internal ClientHello builder/test seam              PR-A/PR-1
  TlsHelloProfile.h      enum BrowserProfile, ProfileSpec, EchMode            PR-2
  TlsHelloProfileRegistry.h/cpp  snapshot-backed registry + sticky selection  PR-2
  StealthConfig.h/cpp    StealthConfig, from_secret()               PR-3
  ShaperState.h/cpp      IptController + MarkovChain                PR-5
  ShaperRingBuffer.h/cpp bounded ring buffer                        PR-4
  DrsEngine.h/cpp        DRS (capture-driven bins + anti-repeat)    PR-6
  StealthTransportDecorator.h/cpp                                   PR-4..7
  StealthParamsLoader.h/cpp JSON loader + hot-reload                PR-8
  TlsServerHelloParser.h/cpp parser для ServerHello/record-layout allowlist PR-10

test/stealth/
  MockRng.h              xoshiro256** ГПСЧ                          PR-A
  MockClock.h            ручное время                               PR-A
  TlsHelloParsers.h      parse helpers (extensions/groups/ECH)      PR-A
  FingerprintFixtures.h  approved structural families / invariants  PR-A
  RecordingTransport.h   fake IStreamTransport                      PR-3
  TestHelpers.h          общие утилиты                              PR-A
  test_tls_hello_wire.cpp    PR-A coverage (wire structure, ECH lengths) PR-A
  test_tls_profiles.cpp      PR-A coverage (uTLS/pcap differential) PR-A
  test_context_entropy.cpp   PR-1 coverage                          PR-1
  test_browser_profiles.cpp  PR-2 coverage (structural policy, advisory JA3/JA4, ALPS, 3DES, ECH)  PR-2
  test_decorator.cpp         PR-4 coverage (delegation, hint consume-once, backpressure, wakeup) PR-4
  test_ipt_controller.cpp    PR-5 coverage (keepalive bypass, log-normal) PR-5
  test_drs_engine.cpp        PR-6 coverage (phases, jitter, idle-reset) PR-6
  test_traffic_classifier.cpp  PR-7 coverage (pure-control keepalive, mixed-packet guard, bulk/interactive split) PR-7
  test_raw_connection_hints.cpp PR-7 coverage (`send_no_crypto`/`send_crypto` hint wiring) PR-7
  test_session_wiring.cpp      PR-7 coverage (public API -> flush -> classifier -> raw write hint) PR-7
  test_params_loader.cpp     PR-8 coverage (JSON, validation, fail-closed) PR-8
  test_tls_init_response_parser.cpp PR-10 coverage (serverhello parser + layout policy + HMAC invariants) PR-10
  test_stealth_disabled.cpp  TDLIB_STEALTH_SHAPING=OFF passthrough  ALL

test/analysis/
  common_tls.py         TLS parser/model helpers shared by smoke checks
  check_fingerprint.py   structural invariants + advisory JA3/JA4 telemetry, ECH type, GREASE, ALPS
  check_flow_behavior.py connection churn/reuse/destination concentration
  check_ipt.py           межпакетные интервалы
  check_drs.py           размеры TLS записей
  check_ech_variance.py  ECH payload per-connection variability
  check_keepalive.py     keepalive latency < 10ms
  extract_server_hello_fixtures.py parser-driven extraction of ServerHello tuples/layouts PR-10
  check_server_hello.py  serverhello tuple/layout allowlist checks  PR-10

telemt-stealth-params/   (git submodule, общий с telemt)
  params.h               IptParams, DrsPolicy с defaults
  profiles_validation.json  ClientHello registry: structural invariants + advisory observed JA3/JA4 telemetry
  response_profiles.json   ServerHello response-profile registry (pcap-derived tuples/layouts)
```

---

# 15. OWASP ASVS L2 Матрица

| ID | Требование | Статус | Комментарий |
|---|---|---|---|
| 2.1.1 | Secrets not in plaintext | ✅ | ProxySecret — raw bytes |
| 2.8.3 | HMAC с длинным ключом | ✅ | 16-byte secret + HMAC-SHA256 |
| 2.8.4 | Replay protection | ⚠️ GAP | unix_time XOR window ±30s — проверить в telemt |
| 5.3.1 | Output encoding | ✅ | Op-DSL не интерпретирует domain |
| 5.3.2 | Input validation | ✅ | PaddingPolicy validation + JSON validation |
| 6.2.1 | CSPRNG | ✅ | Random::secure_bytes production; MockRng только в тестах |
| 6.4.1 | Key management | ✅ | secret из ProxySecret; не захардкожен |
| 9.1.1 | TLS config | ✅ | TLS-in-TLS запрещён (правильно) |
| 9.2.1 | Cipher suites | ✅ | 3DES удалён из всех современных профилей |
| 10.3.1 | Anti-automation | ✅ | HMAC mutual auth блокирует scanners |
| 11.1.4 | Timing side-channel | ✅ | Keepalive bypass + consume-once hint + DRS jitter |
| 11.1.5 | Resource exhaustion | ✅ | Bounded ring; overflow не блокирует event loop |

---

# 16. Синхронизация с telemt

| tdlib-obf PR | telemt PR | Тип зависимости |
|---|---|---|
| PR-1 (dynamic padding) | — | Telemt парсит только HMAC в random, не размер Hello |
| PR-1 (PQ group codepoint) | — | Telemt не парсит supported_groups / key_share из ClientHello |
| PR-2 (ALPS fixture-driven `0x4469/0x44CD`, `0xFE0D`) | — | Сервер не парсит ALPS type / ECH extension type |
| PR-2 (profile weights) | PR-F | structural profile policy + advisory observed JA3/JA4 telemetry в `profiles_validation.json` — **синхронизировать** |
| PR-3 (StealthConfig record-size policy schema) | PR-C (DRS) | `DrsPolicy` и диапазоны initial-record policy в shared submodule — **обязательно** |
| PR-5 (IptParams) | PR-G | `IptParams` в shared submodule — **обязательно** |
| PR-8 (JSON format) | telemt config | Единый формат JSON для обоих — **синхронизировать** |
| PR-10 (ServerHello response-profile registry) | telemt PR-H | Pcap-derived response tuples/layout signatures и shared response-profile commit — **обязательно синхронизировать** |

**Shared submodule `telemt-stealth-params/`:** оба репозитория указывают на один и тот же коммит.
В submodule должны синхронно жить и `profiles_validation.json`, и `response_profiles.json`; split-brain между client/profile registry и response-profile registry недопустим.
При изменении defaults в submodule — одновременно обновлять ссылки в обоих проектах.

---

# 17. Риск-регистр

| Риск | P | S | Митигация |
|---|---|---|---|
| Регрессия ECH payload к process-wide singleton sampling | Высокая | Критическая | PR-1/PR-9: context-driven sampling + anti-singleton smoke check внутри одного runtime процесса |
| PQ group не синхронизирован с profile registry snapshot | Высокая | Критическая | PR-1/PR-2: registry-driven mapping, согласованность supported_groups и key_share |
| Регрессия ECH type к 0xFE02 → тривиальная детекция (при включенном ECH) | Высокая | Высокая | PR-2/PR-9: только 0xFE0D + smoke-regression guard |
| ECH declared encapsulated key length != фактическому количеству байт | Высокая | Критическая | PR-2: фикс wire-format + тест структурного парсинга |
| ECH enc генерируется как произвольные bytes вместо X25519-style key | Средняя | Высокая | PR-1: `EchEncKey` использует key-path (`Op::key()`), fallback random только для явно нестандартной длины |
| ECH включён в RU egress, где он блокируется | Высокая | Критическая | PR-8: route-policy (`ru_egress.ech_mode=disabled`) |
| QUIC/HTTP3 попытки в RU->non-RU маршрутах | Средняя | Высокая | PR-8: `allow_quic=false`, transport strategy TCP+TLS only |
| ALPS codepoint захардкожен глобально (всегда `0x44CD` или всегда `0x4469`) и расходится с profile snapshot/capture | Высокая | Высокая | PR-2: fixture-driven ALPS policy (`0x4469/0x44CD`) + smoke-check на соответствие профилю |
| Отсутствует ALPS — Chromium structural family и observed JA3/JA4 distribution дрейфуют | Высокая | Высокая | PR-2: fixture-driven ALPS policy + structural smoke-check + advisory hash-dispersion checks |
| Регрессия fixed padding target 513 → ClientHello снова 517 | Высокая | Высокая | PR-1/PR-9: profile-driven PaddingPolicy + regression checks на отсутствие forced 517 |
| Регрессия extension order в non-Darwin: возврат к windowed shuffle или фиксации позиции `0xff01` ломает Chrome shuffle model и создаёт лишние positional constraints | Высокая | Высокая | Текущая ветка уже использует anchored shuffle + тесты `TlsExtensionOrderPolicy_*`; PR-2 закрепляет это через `ExtensionOrderPolicy::ChromeShuffleAnchored` для Chrome и `FixedFromFixture` для остальных, PR-9: fail-closed allow-set по order hash |
| 3DES в Safari/Firefox | Высокая | Высокая | PR-2: 3DES удалён |
| `kClientPartSize = 2878` — известная сигнатура | Высокая | Высокая | PR-3+PR-6: capture-driven DRS + coalescing |
| Keepalive задерживается шейпером | Средняя | Критическая | PR-5: TrafficHint::Keepalive bypass + гарантированный bypass-priority |
| `future_salt_n > 0` ошибочно классифицируется как AuthHandshake | Средняя | Высокая | PR-7: `AuthHandshake` только при `!has_salt`, future salts классифицируются как control-path |
| Ring overflow → unmasked burst | Средняя | Высокая | PR-4: hard backpressure, без sync overflow write |
| Hint drift → Keepalive hint утекает | Средняя | Средняя | PR-4: consume-once semantics |
| DRS не сбрасывается на idle | Средняя | Средняя | PR-6: notify_idle при sampled idle threshold |
| Auth-пакеты задерживаются IPT | Средняя | Средняя | PR-7: AuthHandshake hint |
| Механистичные record sizes без jitter | Средняя | Средняя | PR-6: capture-driven bins + anti-repeat guard |
| ALPN `h2/http/1.1` при raw MTProto payload (S22) | Высокая | Критическая | Явный scope-guard + отдельный L7 трек (реальный HTTP framing) до любых заявлений о «неотличимости» |
| ТСПУ обновляет ML-модели | Высокая | Высокая | PR-8: runtime JSON hot-reload |
| Merge-конфликт с upstream TlsInit | Высокая | Средняя | Только аддитивные изменения |
| Safari ECH отсутствует — видно в 2027 | Низкая | Средняя | Будущий SafariIos18 профиль (backlog) |
| Попытка «de-entropy padding» внутри TLS record | Средняя | Высокая | Запрещено: не менять payload ciphertext на transport-слое |
| App-level ClientHello fragmentation как default (S16) | Средняя | Средняя | Запрещено в production default; возможен только как отдельный research вне критического пути |
| ServerHello/response path слишком однотипен (S17) | Высокая | Критическая | PR-10 (tdlib+telemt): убрать fixed-prefix validator в `wait_hello_response`, перейти на parser + response-profile registry + `check_server_hello_matrix` |
| Flow-cadence аномалия к одному destination (S18) | Высокая | Высокая | PR-8 owner: runtime anti-churn/connect-rate hooks + PR-9 verification (`check_flow_behavior.py`) |
| Длительная SNI/domain concentration (S19) | Высокая | Высокая | PR-8 owner: sticky domain-rotation window + destination-share budget; PR-9 smoke only validates |
| Platform/profile incoherence (class + OS-family drift) (S23) | Высокая | Высокая | Platform hints + class/OS guardrails + smoke checks `profile_matches_device_class(...)` и `profile_matches_platform(...)` |
| Capture baseline contamination (mixed browser/synthetic/legacy) (S24) | Высокая | Высокая | Provenance-tagged fixtures + per-family thresholds + fail-closed при неизвестном source-tag |
| Зависимость от устаревших JA3-листов как ground truth | Средняя | Высокая | JA3 list = advisory IOC only; решения только через structural registry + policy checks; JA4 остаётся auxiliary telemetry |
| Browser PQ profile дрейфует между релизами | Высокая | Средняя | PR-2/PR-8: обновление profile registry из capture и hot-reload |

---

# 18. Критерии готовности к релизу

1. **Все тесты PR-A..PR-10 зелёные** (без исключений; production release запрещён до завершения PR-10 parser-track).

   Текущий branch-baseline, который уже существует в `run_all_tests`, обязан оставаться зелёным минимум по следующим критическим кейсам:
  - `TlsHelloEntropy.EchPayloadLengthAllowsetAndVariance`
  - `TlsHelloEntropy.EchEncapsulatedKeyLengthMustMatch32Bytes`
  - `TlsHelloEntropy.ClientHelloLengthMustNotBePinnedToFixed517`
  - `TlsHelloWire.StructuralInvariantsAndECHLengths`
  - `TlsHelloWire.AdversarialLengthMismatchesMustBeRejected`
  - `TlsHelloWire.EchPayloadLengthMustVaryPerConnection`
  - `TlsHelloProfiles.ALPSCodepointMustMatchKnownProfilePolicy`
  - `TlsHelloProfiles.EchTypeAndDeclaredEncLengthInvariant`
  - `TlsHelloProfiles.MustSupportHybridAndClassicalKeyShareGroups`
  - `TlsRoutePolicy.RuRouteDisablesEch`
  - `TlsRoutePolicy.KnownNonRuRouteKeepsEchEnabled`
  - `TlsRoutePolicy.UnknownRouteMustNeverEmitAnyEchTypeAcrossManyConnections`
  - `TlsRoutePolicyStress.KnownNonRuEchPayloadDistributionMustRemainMultiBucket`
  - `TlsExtensionOrderPolicy.EchEnabledExtensionsMatchChromeShuffleModel`
  - `TlsExtensionOrderPolicy.EchDisabledExtensionsMatchChromeShuffleModel`
  - `TlsExtensionOrderPolicy.ChromeShuffleEntropyMustRemainNonDeterministic`
  - `TlsHelloParserSecurity.RejectsLegacyEchExtensionTypeFe02`

   Будущие PR-deliverables, которых ещё нет в текущем branch inventory, но которые обязаны появиться и стать release-gating до финального релиза:
  - transport/shaper tests (`test_decorator.cpp`, `test_ipt_controller.cpp`, `test_drs_engine.cpp`, `test_batch_builder.cpp` или эквивалентный suite)
  - response parser tests (`test_tls_init_response_parser.cpp`)
  - PR-9/PR-10 smoke scripts из `test/analysis/`

2. **Сборка с `TDLIB_STEALTH_SHAPING=OFF`** проходит все upstream tdlib тесты bit-for-bit.

3. **Smoke tests** против `stealth-drs-ipt` ветки telemt (локально) после того, как PR-9/PR-10 действительно добавят `test/analysis/` scripts в repo:
  - `check_fingerprint.py`: JA3 не совпадает с `e0e58235789a753608b12649376e91ec`
  - `check_fingerprint.py` (RU mode): ECH отсутствует, 0xFE02 отсутствует
  - `check_fingerprint.py` (non-RU mode): 0xFE0D присутствует, 0xFE02 отсутствует
  - `check_route_ech_runtime.py`: последовательный прогон `unknown -> non_ru_egress -> ru_egress` подтверждает route-aware поведение и TTL circuit-breaker fallback
  - `check_fingerprint.py`: ECH outer структура валидна (enc_key_len == фактической длине)
  - `check_fingerprint.py`: ALPS соответствует profile fixture (для выбранного профиля ровно один из `0x4469`/`0x44CD`; второй отсутствует)
  - `check_fingerprint.py`: PQ group в `supported_groups` и `key_share` совпадает с profile registry
  - `check_fixture_provenance.py`: неверные `family/tls_gen/transport/source_kind` теги детектируются fail-closed (canary contamination)
  - `check_fixture_registry_complete.py`: в `profiles_validation.json` отсутствуют `"<explicit-fixture-id>"` placeholders (иначе FAIL)
  - `check_ech_variance.py` (non-RU mode): ≥3 разных длины ECH за 50 соединений
  - `check_drs.py`: dominant-mode не равен 2878; record size варьируется ≥3 значениями
  - `check_ipt.py` / `check_drs.py`: отклонение от baseline в `docs/Samples/Traffic dumps/*.pcap*` не превышает пороги
  - `check_keepalive.py`: keepalive задержка < 10ms в 100% случаев
  - `check_flow_behavior.py`: connect-rate/reuse/lifetime в пределах capture-threshold, без reconnect-storm к одному destination
  - `check_flow_behavior.py`: destination concentration в окне сессии не выходит за policy-budget
  - `check_flow_behavior.py`: anti-churn guard соблюдён (reconnect interval >= `anti_churn_min_reconnect_interval_ms`)
  - `check_fingerprint.py`: desktop runtime не выбирает mobile-профили (`IOS14`, `Android11_OkHttp_Advisory`)
  - `check_fingerprint.py`: mobile runtime не выбирает desktop-профили (Chrome/Firefox/Safari desktop set)
  - `check_fingerprint.py`: non-Darwin desktop runtime не выбирает `Safari26_3`
  - `check_fingerprint.py`: profile соответствует platform hints (`device_class` + `os_family`)
  - `check_server_hello.py`: negotiated `(selected_version, cipher_suite, selected_group)` попадает только в pcap-derived response matrix для выбранного profile family
  - `check_server_hello.py`: registry не содержит server tuples, пришедшие только из `utls_snapshot`, `curl_cffi_capture` или synthetic telemt smoke artifacts
  - `check_fingerprint.py`: для randomized Chrome-family lanes batch-отчёт показывает non-collapsed observed JA3 и JA4 sets; exact hash equality не используется как release-gate
  - `check_fingerprint.py`: отчёт содержит JA3 raw tuple + hash (GREASE-stripped), observed JA4 token set и не допускает JA3-only или JA4-only verdict path

    До появления `test/analysis/` эти пункты являются planned release gates, а не текущим CI inventory.

4. **Shared submodule** тегирован и одновременно обновлён в tdlib-obf И telemt.

5. **Верификация от резидента** (не VPS): минимум 3 полных прогона smoke-тестов на трёх разных RU провайдерах из capture-набора (`beget.com`, `web_max.ru_`, `ur66.ru`) и отдельный прогон через мобильный LTE.

6. **`CHANGELOG-obf.md`** документирует: активируется только при `0xee`-секрете, список исправленных сигнатур, известные остаточные риски.

7. **L7 scope-guard зафиксирован письменно:** в release-note и smoke-отчёте явно указано, что PR-A..PR-9 не реализуют полноценный HTTP/2/HTTP/1.1 payload framing и не дают обещаний «100% undetectable».

8. **C++ safety-gate обязателен перед merge** (ASan/UBSan + clang-tidy + warning-as-error + cppcheck), см. 18.1.

## 18.1 Обязательный C++ Safety Gate

Минимальный набор перед merge каждого PR в stealth-треке:

1. Санитайзеры (debug/smoke): `-fsanitize=address,undefined -fno-omit-frame-pointer`
2. Предупреждения как ошибки: `-Wall -Wextra -Wpedantic -Werror`
3. Статический анализ: `clang-tidy` (`bugprone-*`, `modernize-*`, `performance-*`)
4. Дополнительный линтер: `cppcheck --enable=warning,style,performance,portability`

Рекомендуемый минимальный прогон:

```bash
cmake -S . -B build-asan \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -Wall -Wextra -Wpedantic -Werror"
cmake --build build-asan --target run_all_tests
ctest --test-dir build-asan --output-on-failure -R run_all_tests
```

И отдельным шагом:

```bash
clang-tidy <changed-files> -- -I.
cppcheck --enable=warning,style,performance,portability td/mtproto
```

---

# Приложение A: Чеклист TDD для AI-агентов

```
Каждый PR = следующая обязательная последовательность:

[ ] 1. Написать красные тесты. Убедиться: падают по ПРАВИЛЬНОЙ причине.
[ ] 2. Реализовать production код до зелени.
[ ] 3. Запустить ВСЕ тесты: ни один старый не регрессирует.
[ ] 4. cmake test && ctest — зелено.

Правила реализации:
[ ] Нет renaming существующих символов без явного запроса
[ ] Нет dynamic_cast к конкретным типам (есть virtual interface)
[ ] Нет global state для случайных значений (ECH — per-connection!)
[ ] При ring overflow: НИКОГДА не обходить IPT/DRS-путь прямым write в `inner_`
[ ] При ring overflow: НИКОГДА не писать в `inner_` синхронно (только backpressure)
[ ] Hint consume-once: ВСЕГДА reset to Unknown после write() (до PR-7: Unknown -> Interactive)
[ ] PQ group: ВСЕГДА из snapshot-backed profile registry, синхронно в supported_groups И key_share
[ ] PQ group: НЕТ hardcoded "Chrome131 == 0x6399"; только profile-registry/capture-driven
[ ] GREASE: Grease::init() для GREASE-слотов — уже корректен
[ ] ALPS: codepoint соответствует profile fixture/snapshot (Chrome131 обычно `0x4469`, Chrome133+ обычно `0x44CD`); одновременное присутствие обоих запрещено
[ ] ALPN: в TCP+TLS-only режиме `h3` не рекламируется; набор ALPN совпадает с fixture/profile policy
[ ] Dataset hygiene: при tcp_only baseline и smoke verdict строятся только по TCP ClientHello; QUIC/h3 корпуса не смешиваются с TCP policy
[ ] ECH: для RU egress disabled; для non-RU только 0xFE0D, никогда 0xFE02
[ ] ECH outer: declared encapsulated key length совпадает с фактической длиной
[ ] QUIC policy: disabled в этом дизайне (TCP+TLS only)
[ ] Activation: ЕДИНСТВЕННЫЙ if (secret.emulate_tls()) в create_transport()

Запрещено:
[ ] Нет passthrough overflow (никакого sync write в bypass ring)
[ ] Нет payload-level tampering (zero/ASCII padding внутри ciphertext)
[ ] Нет TLS-in-TLS
[ ] Нет singleton для per-connection entropy
[ ] Нет app-level ClientHello fragmentation как production default
[ ] Нет forced single-socket стратегии
[ ] Нет статических padding target
[ ] Нет рассинхронизации PQ codepoint между supported_groups и key_share
[ ] Нет заявлений про «100% undetectable» без закрытия L7-трека
```
