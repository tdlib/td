<!--
SPDX-FileCopyrightText: Copyright 2026 telemt community
SPDX-License-Identifier: MIT
telemt: https://github.com/telemt
telemt: https://t.me/telemtrs
-->

# tdlib-obf: что реально реализовано в stealth-контуре и зачем

Документ составлен по текущему состоянию репозитория `tdlib-obf` на 2026-04-11. Это не пересказ `docs/Plans/tdlib-obf-stealth-plan_v6.md`, а фиксация того, что уже присутствует в исходниках, тестах и checked-in артефактах.

Главная operational-предпосылка у этой ветки простая: противник смотрит не только на один `ClientHello`, а на всю совокупность признаков. Поэтому работа ушла дальше «сделать браузероподобный TLS hello» и фактически закрыла четыре слоя сразу:

1. runtime-выбор и сборка browser-like TLS `ClientHello`;
2. route-aware политика ECH и профильная fail-closed логика;
3. transport shaping на уровне inter-packet timing, record sizing и hint wiring;
4. capture-driven pipeline верификации по real-world артефактам `ClientHello` и `ServerHello`.

Из этого следуют три жёстких вывода, которые уже отражены в коде:

- stealth-логика активируется только для `ProxySecret::emulate_tls()`;
- для `RU` и `unknown` маршрутов ECH по умолчанию выключается, а не «оптимистично пробуется всегда»;
- QUIC/HTTP3 не считается допустимой стратегией для этой ветки и прямо запрещён runtime-policy.

## 1. Что реализовано в production-path

### 1.1. Строгий activation gate и точка подключения

В `td/mtproto/IStreamTransport.cpp` stealth-shaping подключается только в одном месте: внутри `create_transport()` для `TransportType::ObfuscatedTcp`, под compile-time флагом `TDLIB_STEALTH_SHAPING`, и только если секрет действительно `emulate_tls()`.

Что это даёт:

- нет расползания условий `if (is_stealth)` по горячему коду;
- обычные obfuscated/TCP/HTTP transport-ы не меняют своё поведение;
- stealth-контур остаётся локализованным вокруг `StealthTransportDecorator`.

Отдельно важно, что этот слой сделан availability-first: если runtime-конфиг невалиден или decorator не может быть собран, код пишет warning и откатывается к обычному `tcp::ObfuscatedTransport`, а не валит соединение. С точки зрения эксплуатации это компромисс в пользу живучести клиента.

### 1.2. Реестр TLS-профилей, а не один synthetic hello

В `td/mtproto/stealth/TlsHelloProfileRegistry.h/.cpp` реализован реальный runtime registry с профилями:

| Профиль | Статус | Существенные свойства |
| --- | --- | --- |
| `Chrome133` | verified | `ALPS=0x44CD`, `ECH=yes`, `padding=yes`, `PQ=0x11EC`, `ChromeShuffleAnchored` |
| `Chrome131` | verified | `ALPS=0x4469`, `ECH=yes`, `padding=yes`, `PQ=0x11EC`, `ChromeShuffleAnchored` |
| `Chrome120` | verified | `ALPS=0x4469`, `ECH=yes`, `padding=yes`, без `PQ`, `ChromeShuffleAnchored` |
| `Firefox148` | verified | fixed-order extension set, `ECH=yes`, padding выключен, `record_size_limit=0x4001`, `PQ=0x11EC` |
| `Firefox149_MacOS26_3` | verified | отдельная macOS family: fixed-order extension set, `ECH=yes`, padding выключен, `record_size_limit=0x4001`, `PQ=0x11EC`, без `0x0023`, c trailing `0x0029` |
| `Safari26_3` | advisory | fixed-order, `ECH=no`, padding выключен |
| `IOS14` | advisory | fixed-order, `ECH=no`, без `PQ` |
| `Android11_OkHttp_Advisory` | advisory | fixed-order, `ECH=no`, без `PQ` |

Ключевой смысл этого реестра:

- профиль задаёт не только имя, но и wire-level инварианты: ALPS codepoint, наличие PQ, разрешён ли ECH, нужен ли padding, policy порядка расширений, предельный record size;
- verified и advisory профили жёстко разделены на уровне provenance metadata;
- runtime больше не притворяется, что все профили одинаково подтверждены живыми capture.
- Darwin Firefox теперь не схлопывается в Linux Firefox: для macOS заведён отдельный verified runtime-profile, чтобы не смешивать families с разными extension-set и ECH-size инвариантами.

Текущее разделение доверия в коде тоже показательно:

- `Chrome133` и `Chrome131` размечены как verified на базе `curl_cffi` capture и corroboration;
- `Chrome120`, `Firefox148` и `Firefox149_MacOS26_3` размечены как verified на базе network/browser captures;
- `Safari26_3`, `IOS14` и `Android11_OkHttp_Advisory` остаются advisory и не выдаются за release-gating ground truth.

### 1.3. Платформенная когерентность и sticky profile selection

Registry не просто хранит список профилей, а ограничивает их по платформе:

- non-Darwin desktop: `Chrome133`, `Chrome131`, `Chrome120`, `Firefox148`;
- Darwin desktop: `Chrome133`, `Chrome131`, `Chrome120`, `Firefox149_MacOS26_3`, `Safari26_3`;
- iOS mobile: только `IOS14`;
- Android mobile: только `Android11_OkHttp_Advisory`.

Это сделано, чтобы не появлялись кросс-слойные противоречия вида «Linux/Windows TCP-поведение + Safari/iOS TLS-профиль». Дополнительно в `StealthRuntimeParams.cpp` валидация запрещает cross-class rotation и отдельно запрещает ненулевой вес Safari в `desktop_non_darwin`.

Выбор профиля сделан sticky, а не purely random. `pick_runtime_profile()` строит ключ из:

- destination;
- time bucket, зависящего от `sticky_domain_rotation_window_sec`;
- platform hints.

Дальше по этому ключу считается стабильный hash, и профиль выбирается взвешенно. Смысл тут практический: не дёргать TLS-семейство на каждом новом соединении к одному и тому же направлению, но и не залипать в один профиль навсегда.

### 1.4. Route-aware ECH policy и circuit breaker

В `td/mtproto/stealth/TlsHelloProfileRegistry.cpp` реализована не «галочка ECH», а полноценная policy-модель:

- `unknown` route: ECH отключён;
- `ru` route: ECH отключён;
- `non_ru` route: ECH допускается, но только если это разрешает профиль и если не сработал circuit breaker.

В том же файле есть per-destination failure cache:

- фиксируются recent ECH failures;
- после достижения threshold ECH для этого направления выключается на TTL;
- успех очищает состояние;
- состояние может храниться через `KeyValueSyncInterface`, то есть переживать рестарт процесса.

Это важный практический сдвиг. В hostile сети нельзя считать, что ECH «если есть, то всегда лучше». Для RU/unknown код уже исходит из противоположного: глобально включённый ECH опаснее, чем отключённый по route-policy.

### 1.5. `TlsInit` больше не живёт в модели одного fixed hello

В `td/mtproto/TlsInit.cpp` runtime path изменён принципиально:

- hello собирается через `build_proxy_tls_client_hello_for_profile(...)`, а не через один synthetic шаблон;
- профиль выбирается из registry по destination/time/platform;
- ECH включается только если это одновременно разрешают route-policy и profile spec;
- результат решения ECH протоколируется в runtime counters, а fail/success обновляют circuit breaker.

Отдельно важно, что proxy-ветка использует не browser-default ALPN, а `Http11Only`. Это сделано осознанно: после TLS handshake здесь идёт сырой MTProto, а не настоящий HTTP/2. Значит, рекламировать `h2` без оговорок было бы лишним L7-риском.

### 1.6. Клиентский `ClientHello` builder действительно стал profile-driven

В `td/mtproto/stealth/TlsHelloBuilder.cpp` profile-aware builder уже привязан к `ProfileSpec`:

- подставляет profile-specific `ALPS` type;
- синхронизирует `PQ group id` между `supported_groups` и `key_share`;
- управляет ECH только через `EchMode`;
- считает padding не как fixed magic number, а через policy и фактическую длину unpadded hello;
- делит browser-default и proxy-only ALPN semantics.

Самое важное для Chrome-family: порядок расширений больше не описывается как слабый windowed shuffle. В registry закреплён `ExtensionOrderPolicy::ChromeShuffleAnchored`, а для Firefox/Safari/mobile профилей используется `FixedFromFixture`.

То есть модель стала такой:

- Chrome-like lanes получают полный anchored shuffle с сохранением только нужных якорей;
- не-Chrome профили остаются fixture-like и не подвергаются ложной «хромификации».

### 1.7. Парсинг ответа на hello больше не завязан на хрупкий fixed prefix

В `td/mtproto/TlsInit.cpp` реализован `consume_tls_hello_response_records(...)`, который:

- читает TLS records последовательно;
- требует `0x16` handshake-record первым;
- валидирует версию `0x03 0x03` и лимит длины record body;
- допускает `0x14` ChangeCipherSpec с корректным payload `0x01`;
- считает ответ complete только после непустого `0x17` Application Data record.

Практический смысл здесь такой: клиент больше не привязан к одному literal-ответу вида «ровно такой префикс, и ничего больше». Это не делает ServerHello «браузерным само по себе», но убирает старую синтетичность со стороны client-side validator.

### 1.8. HMAC-проверка ответа сделана constant-time

В том же `TlsInit.cpp` hash ответа сравнивается через `constant_time_equals(...)`, а не через обычное строковое сравнение. Это важная security-мелочь, но именно такие мелочи и отличают серьёзную ветку от «демо-обфускации».

### 1.9. `StealthTransportDecorator` уже не skeleton, а реальный shaping-layer

В `td/mtproto/stealth/StealthTransportDecorator.cpp` реализован полноценный transport decorator, который:

- держит два ring buffer-а: `bypass_ring_` для zero-delay path и `ring_` для shaped path;
- считает задержку через `IptController` на запись;
- держит high/low watermark и включает backpressure при переполнении очереди;
- не делает silent bypass при переполнении ring, а считает это invariant break и падает fail-closed;
- умеет coalesce-ить только совместимые non-quick-ack записи;
- не ломает packet order;
- прокидывает `max_tls_record_size` внутрь transport-а;
- чередует обслуживание bypass/shaped очередей при contention, чтобы не загнать одну из них в starvation.

Отдельно важно, чего здесь уже нет: в обход shaping ничего «тайком» не пишется. Это прямо устраняет старую проблему из плана про sync overflow write.

### 1.10. IPT реализован как sampler, а не как декоративная задержка

`td/mtproto/stealth/IptController.cpp` и покрывающие его тесты показывают, что inter-packet timing — это не один `sleep(random)`, а явная модель:

- burst/idle параметры задаются в `IptParams`;
- есть переключение состояний;
- keepalive bypass-ит artificial delay;
- `Unknown` до классификатора нормализуется в `Interactive`;
- верхние границы задержек валидируются fail-closed.

С практической точки зрения это означает, что код реально различает «control keepalive» и «обычную интерактивную полезную нагрузку», а не просто растягивает весь поток одинаково.

### 1.11. DRS реализован как phase-aware record sizing engine

В `StealthTransportDecorator` и связанных `DrsEngine`/`StealthConfig` уже есть:

- slow-start, congestion-open и steady-state фазы;
- jitter внутри диапазонов;
- idle reset;
- profile-aware cap на payload size;
- связь с `set_max_tls_record_size()` нижележащего transport-а.

Отдельно показательно, что `Firefox148` в registry имеет `record_size_limit=0x4001`, а `StealthConfig.cpp` умеет зажимать DRS policy до profile-specific предела. Это именно тот тип деталей, без которого shaping быстро вырождается в «написали красивые слова, а в wire всё равно невозможно для профиля». Здесь этот разрыв уже закрыт.

### 1.12. Traffic hint classification реально заведён в продовый send-path

В `td/mtproto/stealth/TrafficClassifier.cpp` классификация работает по реальным признакам сессии:

- нет salt -> `AuthHandshake`;
- чистый ping/ack control traffic -> `Keepalive`;
- большой user payload или ack flood -> `BulkData`;
- всё остальное -> `Interactive`.

Дальше эта логика не остаётся «мертвой»:

- `td/mtproto/SessionConnection.cpp` реально вычисляет hint перед упаковкой контейнера;
- `td/mtproto/RawConnection.cpp` реально прокидывает hint в transport на `send_crypto()` и `send_no_crypto()`;
- `StealthTransportDecorator` реально consume-ит hint на write-path.

Это важная точка. В текущем коде hint wiring не декоративный и не test-only.

### 1.13. Anti-churn и destination-budget заведены в сетевой runtime

Flow-behavior policy не осталась просто полями в JSON-схеме. Она реально используется в `td/telegram/net`:

- `ConnectionFlowController.cpp` ограничивает burst reconnect-ов и соблюдает `anti_churn_min_reconnect_interval_ms` плюс `max_connects_per_10s_per_destination`;
- `ConnectionDestinationBudgetController.cpp` контролирует долю попыток на конкретное направление и не даёт одному destination захватить весь connect budget;
- `ConnectionCreator.cpp` учитывает оба контроллера при планировании новых соединений;
- `Session.cpp` и pool policy используют те же runtime params для expiry pooled connections.

Именно это заменяет опасные идеи из ранних обсуждений вроде «схлопнуть всё в один TCP» или «лечить всё одной фрагментацией hello». Вместо грубой аномалии появился управляемый flow-budget.

### 1.14. Proxy-routing discipline доведён до fail-closed на уровне TCP dial path

После дополнительного аудита были закрыты две отдельные ветки, которые были опасны не fingerprint-ом, а самим фактом прямого TCP SYN в ASN Telegram при уже включённом proxy mode.

Во-первых, `ConfigManager.cpp` в recovery-path (`get_full_config(...)`) запрашивал raw connection к явному DC IP через `ConnectionCreator::request_raw_connection_by_ip(...)`. Исторически этот helper открывал `SocketFd::open(ip_address)` напрямую и тем самым обходил активный proxy, даже если обычный session-path уже был привязан к MTProto proxy.

Во-вторых, `ConnectionCreator::ping_proxy(nullptr, ...)` трактовал `nullptr` как «пинговать main DC напрямую». Это шло через `Requests.cpp` и тоже могло давать прямой SYN в Telegram ASN, хотя у процесса уже был активный proxy.

Текущее поведение после hardening следующее:

- `request_raw_connection_by_ip(...)` больше не делает blind direct dial, а сначала вычисляет `RawIpConnectionRoute`;
- при активном `MTProto` proxy TCP socket открывается только к proxy IP, а не к Telegram DC;
- при `SOCKS5`/`HTTP TCP` socket тоже открывается к proxy IP, а Telegram-адрес передаётся как tunneled destination;
- если proxy включён, но его IP ещё не разрешён, код fail-closed возвращает ошибку вместо тихого direct fallback;
- `HTTP caching proxy` для explicit raw-IP path не считается безопасным эквивалентом и поэтому тоже отбрасывается fail-closed;
- `ping_proxy(nullptr, ...)` теперь наследует уже активный proxy, а не использует отдельный direct route.

Это архитектурно важно. Если у клиента уже включён proxy-режим, то требование «не посылать TCP SYN в Telegram ASN поверх обфусцированного proxy сценария» должно соблюдаться не только в основном session-path, но и в recovery / diagnostics ветках. Иначе DPI видит сам факт прямого назначения ещё до того, как wire-маскировка вообще начинает иметь значение.

**Почему это было возможно в оригинальном tdlib.** Основной поток (отправка сообщений, получение апдейтов) через proxy ходил правильно. Но в коде было два места, которые про proxy просто не знали.

*Первое.* Есть служебный механизм: когда клиент не может подключиться штатно, он идёт за свежим конфигом к Telegram, чтобы узнать актуальные IP серверов. Этот код писался как утилитарный — «просто достань конфиг любым способом». Он вызывал `request_raw_connection_by_ip()`, который буквально делал `open(telegram_dc_ip)` без вопросов. Про активный proxy не спрашивал.

*Второе.* Есть API-вызов «пинганй proxy» — проверить, живой ли он. Если вызвать с `nullptr` (типа «пингани то, что сейчас настроено»), код интерпретировал это как «пинганй main DC напрямую». Логика была такая: раз конкретный proxy не указан — значит, меряем latency до Telegram без прокладок.

*Почему оригинальный tdlib это позволял.* Потому что оригинальный tdlib вообще не ставил целью скрыть сам факт подключения к Telegram. Proxy там — инструмент обхода блокировок (SOCKS5, MTProto), а не инструмент сокрытия. Поэтому «config recovery должен идти через proxy» и «диагностический ping не должен светить прямой IP Telegram» — это просто не было требованием: не баг, а другая модель угроз.

Stealth-ветка выдвигает более строгое требование: если proxy включён, ни один TCP SYN не должен уходить напрямую в IP-пространство Telegram — ни основной сессией, ни служебным кодом, ни диагностикой. DPI смотрит на факт соединения ещё до того, как начинается какое-либо маскирование трафика.

Под это добавлены отдельные регрессии:

- `test/stealth/test_connection_creator_proxy_route_security.cpp` фиксирует route policy для explicit raw-IP соединений;
- `test/stealth/test_connection_creator_ping_proxy_security.cpp` фиксирует, что `nullptr` в ping API означает «использовать активный proxy, если он уже включён», а не «обходить его».

### 1.15. Runtime params loader сделан с security hardening, а не как «прочитать JSON как-нибудь»

В `td/mtproto/stealth/StealthRuntimeParams.*` и `StealthParamsLoader.cpp` реализованы:

- строгая версия схемы (`version == 1`);
- точная форма JSON-объекта, без неучтённых полей;
- валидация platform/profile coherence;
- запрет `allow_cross_class_rotation`;
- принудительно отключённый QUIC в route policy;
- обязательное `persist_across_restart` для route failure state;
- secure file loading на POSIX через `lstat`, `open(..., O_NOFOLLOW)`, `fstat`, проверку owner и запрет group/world write;
- ограничение размера файла;
- cooldown после серии ошибок reload-а.

Практический смысл: hot-reload существует, но он не превращён в новую дырку или источник недетерминизма.

## 2. Что реализовано в verification/analysis контуре

### 2.1. Frozen corpus и checked-in empirical basis

Под `test/analysis/fixtures/clienthello` и `test/analysis/fixtures/serverhello` уже лежит checked-in reviewed corpus по четырём платформенным веткам:

- `clienthello/android`, `clienthello/ios`, `clienthello/linux_desktop`, `clienthello/macos`;
- `serverhello/android`, `serverhello/ios`, `serverhello/linux_desktop`, `serverhello/macos`.

На момент проверки в репозитории находятся:

- 28 JSON-артефактов `ClientHello`;
- 28 JSON-артефактов `ServerHello`.

Это не «сырой dump для ручного triage», а уже часть release-gating pipeline.

### 2.2. `profiles_validation.json` — это уже не список пожеланий, а contamination guard

`test/analysis/profiles_validation.json` делает две важные вещи.

Во-первых, он вводит contamination guard:

- `fail_on_unknown_fixture_id`;
- `fail_on_unknown_tag`;
- `fail_on_source_hash_mismatch`;
- запрет mixed source kind/family внутри профиля;
- запрет legacy TLS1.2 contamination в modern profiles;
- обязательность network-derived fixture для release profiles;
- обязательность independent corroborating source.

Во-вторых, он задаёт release-gating `server_hello_matrix` с разрешёнными tuples/layout signatures для Linux desktop, Android, iOS и macOS семейств.

Это критически важно, потому что такая схема не даёт quietly подменить verified profile синтетикой или случайно смешать разнородные capture-семейства.

### 2.3. Reviewed summary header генерируется из frozen JSON, а не пишется руками

`test/analysis/merge_client_hello_fixture_summary.py` сводит checked-in JSON corpus в `test/stealth/ReviewedClientHelloFixtures.h`.

Что это означает практически:

- PR-2 style differential tests читают reviewed constants не из ручных literals, а из freeze-артефактов;
- provenance (`source_path`, `source_sha256`, `scenario_id`, `route_mode`) уходит в сгенерированный header;
- reviewed header становится derived artifact, а не ручным источником дрейфа.

Это один из самых важных шагов «дальше плана»: проект убрал ручную копипасту capture-derived значений из C++ тестов и заменил её воспроизводимой генерацией.

### 2.4. ServerHello realism реально зафиксирован отдельным parser-driven pipeline

`test/analysis/extract_server_hello_fixtures.py` извлекает `ServerHello` из pcap/pcapng parser-driven способом через `tshark`, а `test/analysis/check_server_hello_matrix.py` валидирует:

- tuple `(selected_version, cipher_suite, extensions)`;
- `record_layout_signature`;
- batch-consistency по `route_mode`, `scenario_id`, `source_path`, `source_sha256`, `fixture_family_id`;
- authoritative source kinds (`browser_capture`/`pcap`);
- отсутствие synthetic layout collapse на крупных батчах.

`run_corpus_smoke.py` дополнительно связывает `ServerHello` batch с соответствующим `ClientHello` capture по `source_path`, `source_sha256` и family. То есть ответ нельзя quietly проверить «сам по себе», в отрыве от исходного capture-контекста.

### 2.5. Появился отдельный imported candidate corpus, который позволяет использовать новые capture без ручного promote

Отдельно от reviewed corpus теперь существует candidate-ветка под:

- `test/analysis/fixtures/imported/clienthello/...`;
- `test/analysis/fixtures/imported/serverhello/...`;
- `test/analysis/fixtures/imported/import_manifest.json`;
- `test/analysis/profiles_imported.json`.

Смысл этой ветки не в том, чтобы автоматически расширять release-gating corpus, а в том, чтобы не терять новые browser captures и прогонять их через executable smoke без ручного «promote в reviewed truth».

Логика здесь уже не ручная.

Во-первых, `test/analysis/import_traffic_dumps.py` делает ровно то, чего раньше не хватало:

- сортирует `docs/Samples/Traffic dumps/Unsorted` в канонические platform roots (`Android`, `iOS`, `Linux, desktop`, `macOS`, `Windows`);
- извлекает imported `ClientHello` и `ServerHello` артефакты в отдельное дерево, не трогая reviewed corpus;
- ведёт `import_manifest.json` с provenance и результатами импорта;
- предпочитает user-provided OS/browser token из имени файла и использует `auto ...` token только как fallback;
- fail-closed отклоняет capture, если browser family из имени файла надёжно определить нельзя, вместо создания `unknown_browser` профиля;
- по умолчанию ставит imported capture в canonical `route_mode=non_ru_egress`, чтобы candidate lane был исполним в текущих fail-closed smoke checks, а не оставался навсегда в `unknown`.

Во-вторых, поверх этого появился `test/analysis/generate_imported_fixture_registry.py`, который строит `profiles_imported.json` не руками и не копированием из reviewed registry, а прямо из imported artifacts и `import_manifest.json`.

Это важно, потому что imported corpus ведёт себя не как reviewed corpus:

- почти все imported artifacts содержат не один sample, а несколько;
- для современных браузеров порядок TLS extensions часто плавает между sample-ами даже внутри одного capture;
- в части imported lanes плавают не только extension order, но и наличие `ALPS`, `ECH` и даже `PQ`-группы.

Из-за этого generated candidate registry теперь фиксирует не ложную «одну правильную строку байтов», а observed policy envelope:

- каждый imported capture остаётся отдельным profile/family и не объявляется release-gating truth;
- `release_gating=false`, а fixture trust маркируется как `candidate`, а не `verified`;
- для Safari imported profiles остаётся `FixedFromFixture`, потому что в текущей модели Safari-family считается fixed-order lane;
- для остальных imported browser families используется `ChromeShuffleAnchored`, то есть extension order трактуется как браузерно-подобная перестановка, а не как один жестко зафиксированный порядок;
- `ech_type`, `alps_type` и `pq_group` теперь могут быть не только exact value, но и policy-object вида «разрешено присутствие и отсутствие», если именно это наблюдается в multi-sample capture;
- fingerprint checker для таких profiles умеет принимать любой matching fixture variant из `include_fixture_ids`, а не только первый попавшийся ordering.

Практический результат этого сдвига уже проверен на реальном imported tree: candidate-corpus smoke проходит отдельно от reviewed registry и на текущем head покрывает `78` imported profiles, `430` ClientHello samples и `437` ServerHello samples.

Это и есть главный operational смысл новой логики: новые capture больше не надо вручную «продвигать» в reviewed corpus, чтобы начать получать от них пользу. Reviewed tree остаётся консервативным и release-gating, а imported lane даёт использовать весь хвост свежих артефактов как candidate corpus без смешения trust levels.

### 2.6. JA3/JA4 audit сделан в двух языках и against reference implementations

Проверка отпечатков построена не на одной самописной функции:

- в Python: `test/analysis/check_fingerprint.py` и `test/analysis/test_check_fingerprint_ja3_ja4_reference_audit.py`;
- в C++: `test/stealth/test_tls_ja3_ja4_cross_validation.cpp` и `test/stealth/test_tls_ja4_fingerprint_adversarial.cpp`.

Это даёт сразу несколько защит:

- анти-Telegram guard не должен случайно выродиться обратно в известный fingerprint;
- логика JA3/JA4 сверяется с reference implementations;
- спорные детали вроде padding/SNI/ALPN и сегмента A в JA4 закреплены тестами, а не обсуждениями в чате.

### 2.7. Поверх capture-driven пайплайна добавлен полноценный `1k` statistical corpus layer

В дереве теперь есть не локальный набор экспериментов, а полноценный многоплатформенный слой нативной валидации отпечатков, построенный вокруг `test/stealth/CorpusStatHelpers.h` и целой группы `1024-seed` suite-файлов. На текущем head этот слой включает:

- Chrome-family suites: `test_tls_corpus_chrome_extension_set_1k.cpp`, `test_tls_corpus_chrome_grease_uniformity_1k.cpp`, `test_tls_corpus_chrome_ech_payload_uniformity_1k.cpp`, `test_tls_corpus_chrome_permutation_position_1k.cpp`, `test_tls_corpus_alps_type_consistency_1k.cpp`, `test_tls_corpus_grease_slot_independence_1k.cpp`, `test_tls_corpus_wire_size_distribution_1k.cpp`;
- desktop browser suites: `test_tls_corpus_firefox_invariance_1k.cpp`, `test_tls_corpus_firefox_macos_1k.cpp`, `test_tls_corpus_safari26_3_invariance_1k.cpp`;
- mobile/platform-gap suites: `test_tls_corpus_ios_apple_tls_1k.cpp`, `test_tls_corpus_ios_chromium_gap_1k.cpp`, `test_tls_corpus_android_chromium_alps_1k.cpp`, `test_tls_corpus_android_chromium_no_alps_1k.cpp`, `test_tls_corpus_fixed_mobile_profile_invariance_1k.cpp`;
- fingerprint/cross-family suites: `test_tls_corpus_ja3_ja4_stability_1k.cpp`, `test_tls_corpus_cross_platform_contamination_1k.cpp`, `test_tls_corpus_adversarial_dpi_1k.cpp`.

Все они подключены в `test/CMakeLists.txt`, то есть это уже часть общего `run_all_tests`, а не ad-hoc файлы рядом с кодом.

Содержательно этот corpus-layer закрывает уже не один класс инвариантов, а несколько независимых осей детекта:

- точные structural invariants для Chrome-family, включая `ALPS` split, отсутствие `0x0029` в fresh path и запрет duplicate extension type;
- статистическую правдоподобность GREASE, ECH payload length и permutation coverage на 1024 сидах;
- fixed-order/fixed-size families для Linux Firefox, macOS Firefox, Safari и advisory mobile profiles;
- platform-family divergence между Linux, macOS, iOS и Android capture families;
- wire-size distribution и anti-regression guards против возврата к узкому набору длин или legacy fixed `517`;
- массовую проверку JA3/JA4 stability и anti-Telegram guard уже на runtime-generated wire image;
- adversarial DPI corpus для route-aware ECH gating, proxy `ALPN=http/1.1` only, session-id/client-random/key-share uniqueness и отсутствия wire-image reuse.

Отдельно важно, что этот слой теперь фиксирует и platform-gap reality, а не замалчивает её. Для macOS Firefox заведён отдельный verified runtime-profile с собственным wire-family, а iOS/Android gap-suites явно проверяют, что advisory/mobile fallback не начинает тихо маскироваться под чужой capture family.

### 2.8. Тестовый контур остаётся плотным не только по числу, но и по типу инвариантов

По уже проверенному aggregate-run можно зафиксировать конкретный результат: `./build/test/run_all_tests --filter 1k` выбрал `171` тест из `1432` зарегистрированных и завершился `passed 171/171` примерно за `40.0s`.

Это означает, что `1k` corpus layer больше не ограничивается четырьмя статистическими suite-ами. Он уже охватывает Chrome, Firefox Linux/macOS, Safari, iOS Apple TLS, iOS Chromium family gap, Android ALPS/no-ALPS families, cross-platform contamination, wire-size distribution и adversarial DPI invariants в одном общем прогоне.

Даже без привязки к одной «магической» суммарной цифре по всему binary теперь можно сказать строго: native test binary содержит сотни отдельных C++ cases, а только `1k` corpus layer сверху даёт уже `171` статистический/семейный fingerprint-check, запускаемый через тот же `run_all_tests`.

Кроме этого, под `test/analysis` остаётся плотный Python/analysis контур со smoke, extractor, checker и audit-скриптами, который закрывает:

- differential tests против real captures;
- parser security и light fuzz;
- route stress и response stress;
- IPT/DRS distribution, fairness, precision, soak и adversarial tests;
- profile provenance/platform coherence tests;
- HMAC timing, replay и wire regression tests;
- parser-driven fingerprint audit в Python параллельно с C++ runtime audit.

То есть плотность покрытия теперь определяется не только количеством unit cases, а тем, что поверх runtime-path уже есть отдельный corpus-derived статистический слой, который специально ловит медленный entropy drift и fingerprint collapse.

## 3. Что в коде сделано сверх исходного плана

Ниже то, что особенно заметно как выход за рамки базовой идеи «улучшить hello и добавить shaping».

### 3.1. Привязка доверия к происхождению fixture

Ветка явно различает verified и advisory профили, а release gating завязан на network-derived и independently corroborated fixture. Это сильно взрослее, чем типичный подход «нашли удобный sample, объявили его эталоном».

### 3.2. Secure hot-reload вместо небезопасного runtime toggle

Параметры теперь не просто можно менять на лету. Их загрузка ограничена по владельцу, типу файла, symlink-policy, размеру и cooldown-логике. Это уже эксплуатационный контур, а не лабораторный флажок.

### 3.3. Flow realism закрывается не словами, а контроллерами

Anti-churn, destination-budget и pool retention заведены в `td/telegram/net`. Это значит, что ветка вышла за пределы TLS-handshake-only thinking и занялась поведением соединений как таковых.

### 3.4. Corpus ушёл вперёд относительно runtime registry

Checked-in reviewed corpus уже содержит более свежие capture-семейства вроде Chrome 146/147, Firefox 149, Safari 26.4 и ряда Android/iOS браузеров. Даже там, где runtime registry пока намеренно уже и консервативнее, empirical guard rails уже шире, чем минимально требовал исходный план.

### 3.5. Response-path hardening стал практическим, а не декларативным

`TlsInit` теперь не просто «ждёт корректный ответ», а умеет безопасно принимать многозаписный TLS response flow, вести ECH failure/success state и делать constant-time hash comparison.

### 3.6. Fingerprint validation ушёл от single-capture regression к массовой статистической проверке

Изначальная проверка realism-а в этой ветке строилась в основном вокруг reviewed captures, differential tests и JA3/JA4 cross-validation. Сейчас поверх этого уже реализован отдельный слой массовых `1024-seed` проверок.

Это важно по двум причинам.

Первая: часть ошибок не проявляется на одном-двух примерах, но очень хорошо проявляется как distribution collapse на длинной серии сидов. Примеры:

- GREASE начинает повторяться слишком часто;
- один bucket длины ECH payload получает аномально высокий вес;
- profile-specific extension set дрейфует и тихо захватывает чужое Firefox-like расширение;
- JA4-C перестаёт быть стабильным там, где должен быть стабильным.

Вторая: такой слой ближе к тому, как реально смотрит DPI. Противник не ограничивается одной сессией; он накапливает статистику по направлению, профилю и семейству traffic. Значит, и защитный regression gate должен проверять не только корректность одного hello, но и отсутствие систематического перекоса на серии генераций.

Именно поэтому новый `1k` corpus layer стоит считать не косметическим дополнением к test-suite, а прямым operational hardening-ом fingerprint subsystem-а.

### 3.7. Вместо ручного "promote" для новых capture появился отдельный candidate lane

Раньше между двумя крайностями был неудобный зазор:

- либо capture оставался в `Unsorted` и практически не участвовал в верификации;
- либо его нужно было вручную тащить в reviewed registry, даже если его статус ещё нельзя честно назвать verified.

Сейчас этот зазор закрыт отдельным imported lane.

- Новые capture можно отсортировать и извлечь автоматически.
- Они получают свой own candidate registry и свой smoke path.
- Browser-order/ECH/ALPS/PQ policy для них выводится из реально наблюдаемого набора sample-ов, а не подгоняется под reviewed инварианты.
- Reviewed corpus остаётся узким, консервативным и пригодным для release gating.

С архитектурной точки зрения это сильный шаг вперёд: проект научился использовать все новые captures как empirical input, не подменяя ими более строгий reviewed truth.

## 4. Почему архитектурные решения именно такие

### 4.1. Почему ECH не включён глобально

Потому что в hostile среде это неверное default-предположение. Ветка исходит из того, что `RU` и `unknown` маршруты должны fail-closed отключать ECH, а `non_ru` должен жить под circuit breaker. Это не «осторожность ради осторожности», а эксплуатационная необходимость.

### 4.2. Почему QUIC/HTTP3 не используются

Потому что runtime policy в коде считает `allow_quic=true` ошибкой конфигурации. В текущей модели транспорт должен оставаться TCP+TLS, а не добавлять ещё одну плоскость блокировок и детекта.

### 4.3. Почему не делается forced single-socket и why not app-level ClientHello fragmentation

Потому что такие меры слишком легко превращаются в собственную сигнатуру. Ветка предпочитает ограничивать churn, budget и reuse через flow controllers, а не через грубое схлопывание поведения в одну искусственную форму.

### 4.4. Почему proxy-path тяготеет к `http/1.1`, а не к `h2`

Потому что после handshake здесь всё ещё идёт сырой MTProto, а не настоящий HTTP/2 framing. Значит, из двух зол меньшее — минимизировать заведомо ложные L7-обещания, а не рекламировать `h2` там, где дальнейший wire не может его честно поддержать.

### 4.5. Почему разделены verified и advisory профили

Потому что Safari/iOS/Android lanes нельзя честно приравнивать к тем профилям, которые уже подтверждены network captures и corroboration. Для серьёзного DPI-противостояния такая честность важнее косметического «у нас много профилей».

## 5. Что важно понимать честно: ограничения всё ещё есть

Этот репозиторий заметно укреплён, но он не обещает невозможного.

- После TLS handshake transport всё ещё несёт сырой MTProto, а не настоящий browser-grade HTTP/2 или HTTP/1.1 session grammar.
- Значит, полная L7-неотличимость от реального браузера в этом репозитории не заявляется и не должна заявляться.
- Runtime registry пока консервативнее и уже, чем empirical corpus; это осознанный выбор в пользу проверяемости.
- Проверка реализма `ServerHello` в этой ветке уже есть, но фактическая генерация ответной стороны зависит не только от `tdlib-obf`, а от того, как устроен компаньон на серверной стороне и как собран deployment.

Именно поэтому сильной стороной этой ветки является не обещание «абсолютной невидимости», а сочетание:

- более правдоподобного handshake;
- route-aware fail-closed policy;
- shaping с реальной проводкой hints и backpressure;
- empirical capture-driven regression gates.

## 6. Итог

Если коротко, то план в коде реализован не как набор точечных патчей, а как полноценный stealth-subsystem:

- profile-driven `ClientHello` generation;
- platform coherence и sticky selection;
- route-aware ECH с circuit breaker и persistence;
- constant-time validated response path;
- IPT + DRS + bounded shaping queues + traffic hints;
- anti-churn/destination-budget в сетевом runtime;
- secure runtime params loader;
- checked-in multi-platform corpus и `ServerHello` matrix;
- отдельный imported candidate corpus и generated registry без ручного `promote` новых capture;
- `1k` statistical corpus validation layer для extension set, GREASE, ECH payload, permutation coverage, platform-family divergence, wire-size distribution, JA3/JA4 stability и adversarial DPI invariants;
- сотни зарегистрированных native test cases, включая уже 171 проверенный `1k` statistical/семейный corpus check, плюс отдельный плотный Python analysis контур.

С практической точки зрения это уже не «попытка замаскировать TLS», а инфраструктура для систематического снижения детектируемости и стоимости классификации в сети, где нельзя полагаться ни на ECH как на серебряную пулю, ни на QUIC как на безопасный обходной путь.