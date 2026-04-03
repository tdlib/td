# tdlib-obf Stealth Plan — Полностью самостоятельный документ

**Дата:** 2026-04-03  
**Репозиторий:** `tdlib-obf` (форк TDLib, директория `td/mtproto/`)  
**Компаньон:** telemt (Rust MTProxy сервер, `IMPLEMENTATION_PLAN.md`)  
**Статус угрозы:** ТСПУ бюджет ₽84 млрд, ML-классификация TLS-фингерпринтов активна.  
**Цель:** максимально снизить детектируемость и стоимость детекта для DPI, без обещания «абсолютной неотличимости» (100% undetectable недостижимо в реальных сетях).  
**Главное правило:** вся маскировка активна **строго только** при
`ProxySecret::emulate_tls() == true` (секрет с префиксом `ee`).

### Аудит V6: Критические исправления

> V6 документ был повторно проверен против реального исходного кода TDLib, bundled uTLS (`docs/Samples/utls-code`) и TLS wire-format. Внесены следующие исправления:
>
> 1. **S3 (КРИТИЧЕСКОЕ):** `0x11EC` — это валидный IANA codepoint X25519MLKEM768, и в bundled uTLS `HelloChrome_131` сейчас тоже используется `0x11EC`. Жесткая привязка "Chrome131 == 0x6399" признана недостоверной. Решение: codepoint должен быть profile/snapshot-driven и проверяться по capture/утвержденному fingerprint-реестру.
> 2. **S8 (КРИТИЧЕСКОЕ):** ошибка была неверно описана. 5-байтный ECH префикс `\x00\x00\x01\x00\x01` (outer + kdf + aead) корректен. Реальная проблема в текущем коде: declared encapsulated key length `\x00\x20` (32), но фактически пишется `random(20)`.
> 3. **S20 (КРИТИЧЕСКОЕ):** для RU egress ECH практически блокируется. Без route-aware режима "ECH off in RU" соединения будут массово отрезаться.
> 4. **S21 (HIGH):** QUIC/HTTP3 (RU->non-RU) блокируется. Стратегия должна явно оставаться в TCP+TLS и не имитировать QUIC-поведение.
> 5. **S7 (HIGH):** ALPS расширение уже есть в коде на `0x44CD` (draft), для современного Chrome-профиля нужен `0x4469`.
> 6. **S9/S10:** 3DES присутствует только в Darwin-профиле.
> 7. Обновлены smoke-tests и чеклист: разделение Global/RU режимов для ECH, profile-registry для PQ codepoint, проверка ECH wire-структуры.

### Аудит внешнего ревью (4 спорных тезиса)

Ниже — верификация 4 тезисов из внешнего security-ревью по реальному коду репозитория и материалам в `docs/Samples`.

| Тезис внешнего ревью | Вердикт | Что исправить в плане |
|---|---|---|
| «Нужно полностью выкинуть Op-DSL и перейти только на template patching» | 🟡 Частично верно (проблема качества есть), но рецепт неверный как default | Оставить декларативный builder (Op-DSL), усилить wire-валидаторы и differential-тесты против capture/uTLS. Template replay допустим как опциональный fallback-профиль, но не как единственный режим. |
| «Sync overflow write разрушает маскировку по таймингам» | 🔴 Верно | Убрать sync overflow write полностью. Ввести hard backpressure через `can_write()` и watermark-политику. |
| «TrafficHint имеет data race между акторами» | ❌ Неверно для текущей архитектуры TDLib | Явно зафиксировать actor-confinement: один `Session` владеет своим `SessionConnection`/`RawConnection`/`transport_`, конкурентного доступа к одному transport нет. Оставить consume-once семантику, но без ложной модели race. |
| «Нужно снижать энтропию TLS record нулями/ASCII-паддингом» | ❌ Неверно и опасно для этого протокола | Запретить payload-level tampering в fake-TLS record. Для MTProto over AES-CTR это ломает протокол и не даёт корректной маскировки. Работать через timing/size/policy, а не через искусственную де-энтропизацию ciphertext. |

Ключевая поправка: модуль `sudoku` в xray-core существует, но это pre-TLS/finalmask слой для потока, а не «внутри-TLS record zero padding». Переносить этот приём в текущий TDLib transport 1:1 нельзя.

---

## Содержание

1. Модель угрозы и полная карта сигнатур
2. Архитектура
3. Граф зависимостей PR
4. PR-A: Test Infrastructure (TDD foundation)
5. PR-1: TLS ClientHello — Context + Per-Connection Entropy
6. PR-2: Browser Profile Registry (Chrome 131/120, Firefox 128, Safari iOS 17)
7. PR-3: IStreamTransport extensions + Activation Gate
8. PR-4: StealthTransportDecorator (скелет + activation)
9. PR-5: IPT — Inter-Packet Timing (Log-normal + Markov + Keepalive bypass)
10. PR-6: DRS — Dynamic Record Sizing с jitter
11. PR-7: TrafficClassifier + SessionConnection hints
12. PR-8: Runtime Params Loader (hot-reload)
13. PR-9: Integration Smoke Tests
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
| Connection | Продолжительность, объём данных | Behavioural fingerprint |

TCP-слой контролируется ОС, не приложением. Атакуем только TLS и Application Data слои.

## 1.2 Полная карта детектируемых сигнатур

| ID | Серьёзность | Где | Описание | Статус |
|---|---|---|---|---|
| S1 | 🔴 CRITICAL | `TlsHelloStore` | Padding target = ровно 513 байт → ClientHello длина 517 | Исправить |
| S2 | 🔴 CRITICAL | `ech_payload()` | ECH payload длина фиксирована **на весь процесс** (singleton) | Исправить |
| S3 | 🔴 CRITICAL | Supported Groups + Key Share | PQ group codepoint захардкожен без profile/snapshot registry. Для устойчивой маскировки codepoint должен выбираться из валидированного fingerprint-профиля и совпадать в **двух** местах: supported_groups и key_share. | Исправить |
| S4 | 🔴 CRITICAL | ECH extension | При включенном ECH используется legacy type `0xFE02` вместо `0xFE0D`. В RU-режиме ECH вообще должен быть отключаемым политикой. | Исправить |
| S5 | 🟠 HIGH | `TcpTransport` | `MAX_TLS_PACKET_LENGTH = 2878` — статическая константа, известная сигнатура | Исправить |
| S6 | 🟠 HIGH | TlsHello | Единственный статический singleton TlsHello для всех соединений | Исправить |
| S7 | 🟠 HIGH | Chrome профиль | ALPS extension присутствует, но с **устаревшим** codepoint: `0x44CD` (17613, alps-01 draft) вместо `0x4469` (17513, application_settings, Chrome 115+). Несовпадение codepoint'а → JA4 не совпадает с реальным Chrome 131. | Исправить |
| S8 | 🔴 CRITICAL | ECH inner | Длина encapsulated key объявлена как `0x0020` (32), но в текущем коде пишется `random(20)`. Это wire-format mismatch, легко детектируемый парсером DPI. | Исправить |
| S9 | 🟠 HIGH | Darwin (`#if TD_DARWIN`) профиль | 3DES suite (0xC012, 0xC008, 0x000A) — удалены Apple в iOS 15 / macOS Monterey (2021). Присутствуют **только** в Darwin-ветке кода, не в основном Chrome-профиле. | Исправить |
| S10 | 🟠 HIGH | Firefox профиль (будущий) | При создании Firefox-профиля: НЕ включать 3DES (0x000A) — Firefox удалил в 2021. Текущий код не имеет Firefox-профиля вообще. | Исправить |
| S11 | 🟠 HIGH | Darwin | `#if TD_DARWIN` использует специальный TLS-профиль эпохи 1.2 — тривиально детектируем | Исправить |
| S12 | 🟠 HIGH | IPT | Равномерное распределение межпакетных интервалов (нет jitter) | Исправить |
| S13 | 🟡 MEDIUM | DRS | Фиксированные record size (1380/4096/16384) без ±jitter — механистично | Исправить |
| S14 | 🟡 MEDIUM | Keepalive | MTProto ping задерживается IPT-шейпером → disconnect при 28с таймауте | Исправить |
| S15 | 🟡 MEDIUM | Session | Hint для первых auth-пакетов не выставляется → лишние задержки при handshake | Исправить |
| S16 | 🟡 MEDIUM | ClientHello | Отсутствует фрагментация ClientHello по нескольким TCP-сегментам. DPI часто инспектирует только первый TCP payload; нужна контролируемая фрагментация на клиенте. | Backlog |
| S17 | 🟡 MEDIUM | TLS Response | Server response pattern (`\x16\x03\x03` + CCS + Application Data) фиксирован и уникален для MTProto-proxy — потенциально детектируем на стороне сервера. | Backlog |
| S18 | 🟡 MEDIUM | Connection | Один TCP connection per DC — необычно для реального HTTPS, где браузер открывает 6+ параллельных соединений. Поведенческий фингерпринт на уровне потока. | Backlog |
| S19 | 🟡 MEDIUM | SNI | Один и тот же SNI domain для всех соединений данного пользователя. Реальный браузер обращается к десяткам доменов. | Backlog |
| S20 | 🔴 CRITICAL | Deployment policy | ECH в RU egress блокируется на части маршрутов. Всегда включенный ECH приводит к мгновенному блоку вместо маскировки. | Исправить |
| S21 | 🟠 HIGH | Protocol strategy | QUIC/HTTP3 (RU->non-RU) блокируется. Попытки имитации QUIC создают дополнительную поверхность детекта/блока. | Исправить |

## 1.3 Что уже хорошо (не трогать)

- `MlKem768Key` (1184 байт) — правильная структура ML-KEM-768 публичного ключа (384 пары NTT-коэффициентов ∈ [0, 3329) + 32 random bytes)
- `Permutation` — случайный порядок расширений per-connection (как Chrome 106+)
- GREASE в cipher suites и extensions (через `Grease::init()`) — уже рандомный, формат `(byte & 0xF0) | 0x0A` корректен по RFC 8701
- HMAC-SHA256 в ClientHello.random — взаимная аутентификация с сервером
- ALPN: `h2` + `http/1.1` — правильно
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

---

# 2. Архитектура

## 2.1 Принципы

| Принцип | Применение |
|---|---|
| **Strict Activation Gate** | Единственный `if (secret.emulate_tls())` в `create_transport()`. Нигде больше нет `if (is_stealth)` в горячем коде. |
| **Decorator** | `StealthTransportDecorator` реализует `IStreamTransport`, держит inner. Вся логика маскировки здесь. |
| **Factory** | `create_transport()` — единственная точка принятия решения о враппинге. |
| **Pre-sampled Context** | Все случайные длины (padding, ECH, record jitter) вычисляются **один раз** в `TlsHelloContext` при его создании. CalcLength и Store только читают. |
| **Consume-once Hint** | `TrafficHint` потребляется один раз в `write()`, авто-сбрасывается в `Interactive`. Это защита от hint-drift; data race между акторами здесь не ожидается из-за actor-confinement. |
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
    ├─ [1] Consume hint (auto-reset to Interactive)
    ├─ [2] Size-based fallback classification
    ├─ [3] IptController::next_delay_us(has_pending, hint)
    │       ├─ Keepalive / BulkData / AuthHandshake → delay = 0
    │       └─ Interactive → log-normal sample + Markov transition
    ├─ [4] Enqueue to ShaperRingBuffer
    │       └─ if ring full: backpressure (no direct write to inner)
    │
StealthTransportDecorator::pre_flush_write(now)
    │
    ├─ [5] Detect idle gap → DrsEngine::notify_idle() if gap > 500ms
    ├─ [6] DrsEngine::next_record_size(hint) + ±jitter
    ├─ [7] inner_->set_max_tls_record_size(jittered_size)
    ├─ [8] ring_.drain_ready(now, write_to_inner)
    └─ [9] if pending < low watermark: снять backpressure

RawConnection::flush_write()
    └─ calls pre_flush_write() on transport
```

---

# 3. Граф зависимостей PR

```
PR-A  (Test Infrastructure: MockRng, MockClock, RecordingTransport)
  │
  ├─► PR-1  (TlsHelloContext pre-sampling + ECH per-connection + GREASE fix)
  │     └─► PR-2  (Browser Profile Registry: Chrome131/120, Firefox128, Safari17)
  │           └─► PR-3  (IStreamTransport extensions + Activation Gate + StealthConfig)
  │                 └─► PR-4  (StealthTransportDecorator: skeleton + consume-once hint)
  │                       ├─► PR-5  (IPT: log-normal + Markov + Keepalive bypass)
  │                       ├─► PR-6  (DRS: phases + jitter + idle-reset)
  │                       └─► PR-7  (TrafficClassifier + SessionConnection wiring)
  │                             └─► PR-8  (Runtime Params Loader, JSON hot-reload)
  │
  └─► PR-9  (Integration Smoke Tests: Python scripts vs local telemt)
```

**Параллелизация:**
- PR-A и PR-1 независимы → стартуют одновременно (красные тесты для PR-1 пишутся в PR-A)
- PR-5, PR-6, PR-7 независимы между собой после PR-4
- PR-8 независим от PR-5/6/7, зависит только от PR-3

---

# 4. PR-A: Test Infrastructure

**Цель:** создать тестовые примитивы без изменения production кода.  
**Gate:** `cmake --build . --target tdmtproto_tests && ctest` — всё зелёное.

## 4.1 Файловая структура

```
td/mtproto/test/
  stealth/
    MockRng.h            xoshiro256** ГПСЧ, детерминированный
    MockClock.h          ручное продвижение времени
    RecordingTransport.h fake IStreamTransport с записью вызовов
    TestHelpers.h        утилиты: make_test_buffer, extract_cipher_suites, etc.
```

## 4.2 MockRng

```cpp
// td/mtproto/test/stealth/MockRng.h
// Deterministic xoshiro256** PRNG implementing IRng interface.
// Used exclusively in tests; never in production paths.
class MockRng final : public stealth::IRng {
 public:
  explicit MockRng(uint64_t seed) {
    // SplitMix64 initialization of xoshiro256** state.
    for (auto &s : state_) {
      seed ^= seed >> 30;
      seed *= 0xBF58476D1CE4E5B9ULL;
      seed ^= seed >> 27;
      seed *= 0x94D049BB133111EBULL;
      s = seed ^= seed >> 31;
    }
  }

  uint32_t next_u32() override {
    return static_cast<uint32_t>(next_u64() >> 32);
  }

  uint32_t bounded(uint32_t n) override {
    // Lemire's nearly-divisionless method.
    if (n == 0) return 0;
    uint64_t m = static_cast<uint64_t>(next_u32()) * static_cast<uint64_t>(n);
    if (static_cast<uint32_t>(m) < n) {
      uint32_t t = static_cast<uint32_t>(-static_cast<int32_t>(n) % n);
      while (static_cast<uint32_t>(m) < t) {
        m = static_cast<uint64_t>(next_u32()) * static_cast<uint64_t>(n);
      }
    }
    return static_cast<uint32_t>(m >> 32);
  }

 private:
  uint64_t state_[4]{};

  uint64_t next_u64() {
    const uint64_t r = rotl(state_[1] * 5, 7) * 9;
    const uint64_t t = state_[1] << 17;
    state_[2] ^= state_[0];
    state_[3] ^= state_[1];
    state_[1] ^= state_[2];
    state_[0] ^= state_[3];
    state_[2] ^= t;
    state_[3] = rotl(state_[3], 45);
    return r;
  }

  static uint64_t rotl(uint64_t x, int k) {
    return (x << k) | (x >> (64 - k));
  }
};
```

## 4.3 MockClock

```cpp
// td/mtproto/test/stealth/MockClock.h
class MockClock final : public stealth::IClock {
 public:
  double now() const override { return time_; }
  void advance(double seconds) { time_ += seconds; }

 private:
  double time_{1000.0};  // Start at non-zero to catch zero-init bugs.
};
```

## 4.4 RecordingTransport

```cpp
// td/mtproto/test/stealth/RecordingTransport.h
struct WriteRecord {
  size_t size;
  bool quick_ack;
  double timestamp;
};

class RecordingTransport final : public IStreamTransport {
 public:
  std::vector<WriteRecord> writes;
  std::vector<int32> set_max_tls_record_size_calls;
  std::vector<stealth::TrafficHint> received_hints;
  int32 current_max_record_size{1380};

  void write(BufferWriter &&message, bool quick_ack) override {
    writes.push_back({message.size(), quick_ack, recorded_now_});
  }
  void set_max_tls_record_size(int32 size) override {
    set_max_tls_record_size_calls.push_back(size);
    current_max_record_size = size;
  }
  void set_traffic_hint(stealth::TrafficHint hint) override {
    received_hints.push_back(hint);
  }
  void set_now(double t) { recorded_now_ = t; }

 private:
  double recorded_now_{0.0};
};
```

---

# 5. PR-1: TLS ClientHello — Context + Per-Connection Entropy

**Зависит от:** PR-A (тестовая инфра)  
**Исправляет:** S1 (static padding), S2 (ECH singleton), S3 (PQ group codepoint mismatch)

## 5.1 Проблемы (детально)

### S2: ECH singleton — фиксированная длина на весь процесс

```cpp
// ТЕКУЩИЙ КОД (баг):
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
// При рассинхроне с реальным snapshot'ом браузера JA3/JA4 расходится.
//
// GREASE в supported_groups обрабатывается отдельно через grease(4) — он КОРРЕКТЕН.
//
// Значение присутствует в ДВУХ местах и должно меняться СИНХРОННО:
//   1. supported_groups extension: \x11\xec
//   2. key_share extension: \x11\xec\x04\xc0 (group_id + key_exchange_length)
```

### S8: ECH wire-format mismatch в encapsulated key

```cpp
// ТЕКУЩИЙ КОД (баг):
Op::str("\xfe\x02"), Op::begin_scope(),
Op::str("\x00\x00\x01\x00\x01"), Op::random(1),
Op::str("\x00\x20"), Op::random(20), ...
//
// 5-байтный префикс \x00\x00\x01\x00\x01 корректен:
// [OuterClientHello=0x00][KDF=0x0001][AEAD=0x0001].
// Реальный дефект: объявлена длина encapsulated key = 32 (0x0020),
// но реально пишется только 20 байт.
// Это ломает wire-структуру и повышает детектируемость парсером DPI.
```

### S1: Static padding target

```cpp
// ТЕКУЩИЙ КОД (баг):
void TlsHelloStore::do_op(Type::Padding) {
  if (length < 513) {
    write_zero(513 - length);  // ← ровно 513 байт padding → ClientHello = 517
  }
}
// ClientHello всегда 517 байт → тривиальная сигнатура.
```

## 5.2 Решение: TlsHelloContext с pre-sampled полями

### Важная корректировка padding-стратегии

Padding не должен быть «произвольным jitter в диапазоне». Для браузерной мимикрии нужен profile-driven режим:

- Chrome-like профили: BoringPaddingStyle-семантика (padding extension добавляется только если unpadded ClientHello в окне `(0xFF, 0x200)`).
- Профили, где hello стабильно > 512 байт (например, Chrome 131 с PQ key_share): padding extension, как правило, отсутствует.
- Safari-профиль: padding extension отсутствует.

Это устраняет старую сигнатуру fixed 517, но не создаёт новую искусственную сигнатуру «рандомный padding всегда есть».

```cpp
// td/mtproto/TlsInit.h — изменения в существующем классе

class TlsHelloContext {
 public:
  // Expanded constructor: all random lengths pre-sampled exactly once.
  // CalcLength and Store must read from context, never sample independently.
  TlsHelloContext(size_t grease_size,
                  string domain,
                  size_t padding_target,   // computed by profile padding policy (Boring-style), not arbitrary range
                  size_t ech_length,       // pre-sampled: 144 + n*32, n in [0,3]
                  uint16_t pq_group_id)    // PQ named group из profile registry (capture-driven)
      : grease_(grease_size, '\0'),
        domain_(std::move(domain)),
        padding_target_(padding_target),
        ech_length_(ech_length),
        pq_group_id_(pq_group_id) {
    Grease::init(grease_);
  }

  // Existing accessors preserved:
  char get_grease(size_t i) const;
  size_t get_grease_size() const;
  Slice get_domain() const;

  // New accessors:
  size_t get_padding_target() const noexcept { return padding_target_; }
  size_t get_ech_length() const noexcept { return ech_length_; }

  // Returns the PQ hybrid named group codepoint for this profile.
  // Важно: значение берётся из capture-validated profile registry, не hardcoded по легенде версий.
  // Used in both supported_groups and key_share extensions — MUST match.
  uint16_t get_pq_group_id() const noexcept { return pq_group_id_; }

  // Returns the key_exchange_length for the PQ key share.
  // Both 0x6399 and 0x11EC use 1216 bytes (1184 ML-KEM + 32 X25519).
  uint16_t get_pq_key_share_length() const noexcept { return 0x04C0; }

 private:
  string grease_;
  string domain_;
  size_t padding_target_;
  size_t ech_length_;
  uint16_t pq_group_id_;
};
```

## 5.3 Новый Op::Type::EchPayload + Op::Type::PqGroupId

```cpp
// В TlsHello::Op::Type enum добавить:
EchPayload,   // per-connection ECH length из context
PqGroupId,    // per-connection PQ named group codepoint из context (0x6399 или 0x11EC)
PqKeyShare,   // PQ key share header: group_id (2 bytes) + key_exchange_length (2 bytes)

// Новые factory методы (заменяют static ech_payload()):
static Op ech_payload_dynamic() {
  Op res;
  res.type = Type::EchPayload;
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

## 5.4 Конфигурация PQ-группы по профилю

```cpp
// td/mtproto/stealth/Interfaces.h

// PQ hybrid named group codepoints.
// 0x11EC = X25519MLKEM768 (IANA final, RFC 9580)
// 0x6399 = X25519Kyber768Draft00 (legacy draft snapshot)
// Safari iOS 17: нет PQ группы
//
// ⚠ ВАЖНО: codepoint ДОЛЖЕН совпадать в supported_groups И key_share.
// Если кодпоинт не совпадает со снимком реального Chrome на ту же версию,
// JA3/JA4 хеш расходится и ТСПУ классификатор его ловит.
constexpr uint16_t kPqGroupMlKemFinal = 0x11EC;
constexpr uint16_t kPqGroupLegacyDraft = 0x6399;

// Returns the PQ group codepoint for a given browser profile.
inline uint16_t pq_group_for_profile(BrowserProfile profile) noexcept {
  switch (profile) {
    // По умолчанию используем snapshot из bundled uTLS (Chrome131 -> 0x11EC).
    // В production переопределяется profile-registry из capture/telemetry.
    case BrowserProfile::Chrome131:   return kPqGroupMlKemFinal;
    case BrowserProfile::Chrome120:   return kPqGroupMlKemFinal;
    case BrowserProfile::Firefox128:  return kPqGroupMlKemFinal;
    case BrowserProfile::SafariIos17: return 0;              // Safari: no PQ key exchange
    default: return kPqGroupMlKemFinal;
  }
}

// GREASE helper — still needed for grease(N) slots, but NOT for the PQ group slot.
// Grease::init() already handles this correctly. This helper is for tests only.
inline uint16_t sample_grease_value(IRng &rng) noexcept {
  uint32_t k = rng.bounded(16);
  uint8_t byte = static_cast<uint8_t>(0x0A + 0x10 * k);
  return static_cast<uint16_t>((static_cast<uint16_t>(byte) << 8) | byte);
}
```

## 5.5 Изменения в CalcLength и Store

```cpp
// TlsHelloCalcLength::do_op — новые case:

case Type::EchPayload:
  CHECK(context);
  size_ += context->get_ech_length();
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
  auto target = context->get_padding_target();
  if (target == 0) break;  // Safari: no padding extension
  auto current = static_cast<int>(size_);
  auto pad_content = static_cast<int>(target) - current;
  if (pad_content > 0) {
    size_ = target + 4;  // 2 (type \x00\x15) + 2 (length) + N zeros
  }
  break;
}

// TlsHelloStore::do_op — новые case:

case Type::EchPayload:
  CHECK(context);
  Random::secure_bytes(dest_.substr(0, context->get_ech_length()));
  dest_.remove_prefix(context->get_ech_length());
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
  auto target = context->get_padding_target();
  if (target == 0) break;
  auto current = static_cast<int>(get_offset());
  auto size = static_cast<int>(target) - current;
  if (size > 0) {
    do_op(TlsHello::Op::str("\x00\x15"), nullptr);
    do_op(TlsHello::Op::begin_scope(), nullptr);
    do_op(TlsHello::Op::zero(size), nullptr);
    do_op(TlsHello::Op::end_scope(), nullptr);
  }
  break;
}
```

## 5.6 Обновлённый generate_header

```cpp
// TlsInit.h — новая перегрузка (старая сохраняется для совместимости)
string generate_header(string domain, Slice secret, int32 unix_time,
                        const stealth::PaddingPolicy &padding_policy,
                        stealth::IRng &rng,
                        stealth::BrowserProfile profile);

// Старая перегрузка: делегирует на profile policy, а не на fixed {513,513}.
```

## 5.7 TDD (красные тесты ДО кода)

```cpp
// td/mtproto/test/stealth/test_context_entropy.cpp

TEST(ContextEntropy, PaddingAndEchSampledOnce) {
  MockRng rng(42);
  auto p = padding_policy_for_profile(BrowserProfile::Chrome131)
               .compute_target(/*unpadded_len=*/640);
  size_t e = rng.bounded(4) * 32 + 144;
  uint16_t pq = pq_group_for_profile(BrowserProfile::Chrome131);
  TlsHelloContext ctx(7, "google.com", p, e, pq);
  EXPECT_EQ(ctx.get_padding_target(), p);
  EXPECT_EQ(ctx.get_ech_length(), e);
  EXPECT_EQ(ctx.get_pq_group_id(), pq_group_for_profile(BrowserProfile::Chrome131));
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
    auto policy = padding_policy_for_profile(BrowserProfile::Chrome131);
    TlsHelloContext ctx(7, "google.com",
                        policy.compute_target(/*unpadded_len=*/640),
                        rng.bounded(4) * 32 + 144,
                        pq_group_for_profile(BrowserProfile::Chrome131));
    TlsHelloCalcLength calc;
    for (auto &op : get_hello_for_profile(BrowserProfile::Chrome131).get_ops())
      calc.do_op(op, &ctx);
    auto length = calc.finish().move_as_ok();

    string buf(length, '\0');
    TlsHelloStore store(buf);
    for (auto &op : get_hello_for_profile(BrowserProfile::Chrome131).get_ops())
      store.do_op(op, &ctx);

    // Buffer must be exactly filled — no overflow, no underrun.
    EXPECT_EQ(store.get_offset(), length) << "Mismatch at i=" << i;
  }
}

TEST(ContextEntropy, PqGroupCodepointMatchesProfile) {
  // Значения берутся из profile registry; тест не должен фиксировать конкретный codepoint.
  EXPECT_NE(pq_group_for_profile(BrowserProfile::Chrome131), 0u);
  EXPECT_NE(pq_group_for_profile(BrowserProfile::Chrome120), 0u);
  // Safari doesn't use PQ at all.
  EXPECT_EQ(pq_group_for_profile(BrowserProfile::SafariIos17), 0u);
}

TEST(ContextEntropy, PqGroupAppearsInBothGroupsAndKeyShare) {
  // Regression: 0x11EC must appear in BOTH supported_groups and key_share,
  // or in NEITHER. Mismatch → invalid ClientHello detectable by DPI.
  MockRng rng(42);
  auto h = generate_header_test(BrowserProfile::Chrome131, rng);
  auto groups = extract_supported_groups(h);
  auto key_shares = extract_key_share_groups(h);
  uint16_t pq_codepoint = pq_group_for_profile(BrowserProfile::Chrome131);
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
    auto h = generate_header("google.com", test_secret, unix_now,
                              padding_policy_for_profile(BrowserProfile::Chrome131),
                              rng, BrowserProfile::Chrome131);
    if (h.size() == 517u) {
      count_517++;
    }
  }
  // Old behavior: always 517. New behavior: never hard-forced to 517.
  EXPECT_LT(count_517, 200u);
}

TEST(ProfileTest, Chrome131PaddingExtensionAbsentWithPq) {
  auto h = generate_header_test(BrowserProfile::Chrome131);
  EXPECT_FALSE(has_extension(h, 0x0015))
      << "Chrome131+PQ should not force padding extension when ClientHello > 512";
}
```

---

# 6. PR-2: Browser Profile Registry

**Зависит от:** PR-1  
**Исправляет:** S3 (PQ group registry), S4 (ECH type), S7 (ALPS 0x44CD→0x4469), S8 (ECH wire format), S9 (Darwin 3DES), S10 (Firefox 3DES prevention), S11 (Darwin special profile), S20 (RU ECH policy)

## 6.1 Файлы

```
td/mtproto/stealth/
  TlsHelloProfile.h         enum BrowserProfile + pick_random_profile()
  TlsHelloProfiles.cpp      build_chrome131/120, firefox128, safari_ios17
```

## 6.2 BrowserProfile enum

```cpp
// td/mtproto/stealth/TlsHelloProfile.h
namespace td::mtproto::stealth {

enum class BrowserProfile {
  Chrome131,         // Default: most common in Russia (65%+ market share)
  Chrome120,         // Older Chrome (Android, corporate deployments)
  Firefox128,        // Without 3DES (Firefox removed it 2021)
  Firefox128Legacy,  // With 3DES: NOT in pick_random_profile(), exists for testing only
  SafariIos17,       // Safari on iOS 17: no session_id, no padding, no ECH
};

enum class EchMode : uint8_t {
  Disabled = 0,       // mandatory default for RU egress
  GreaseDraft17 = 1,  // 0xFE0D GREASE ECH
};

// Selects a profile based on Russia/CIS Q1 2026 device distribution.
// Called once per connection; uses pre-seeded rng from StealthConfig.
BrowserProfile pick_random_profile(IRng &rng);

const TlsHello &get_hello_for_profile(BrowserProfile profile);

// Returns the appropriate PaddingPolicy for a profile.
// Safari: disabled policy (no padding extension).
PaddingPolicy padding_policy_for_profile(BrowserProfile profile);
EchMode ech_mode_for_route(const NetworkRouteHints &route_hints) noexcept;

}  // namespace td::mtproto::stealth
```

## 6.3 Веса профилей

```cpp
BrowserProfile pick_random_profile(IRng &rng) {
  // Russia/CIS Q1 2026 (StatCounter + Telegram mobile breakdown):
  // Base weights:
  // Chrome 131: 48, Chrome 120: 17, Safari iOS 17: 20, Firefox 128: 8.
  // Sum = 93; remainder (7%) is intentionally routed to Chrome 131 to keep
  // a conservative Chromium-majority mix and avoid abrupt tail-profile growth.
  // Effective distribution: Chrome131=55%, Chrome120=17%, Safari=20%, Firefox=8%.
  auto roll = rng.bounded(100);
  if (roll < 48) return BrowserProfile::Chrome131;
  if (roll < 65) return BrowserProfile::Chrome120;
  if (roll < 85) return BrowserProfile::SafariIos17;
  if (roll < 93) return BrowserProfile::Firefox128;
  return BrowserProfile::Chrome131;
}

EchMode ech_mode_for_route(const NetworkRouteHints &route_hints) noexcept {
  // Operational rule: for RU egress ECH is disabled by default due to active blocking.
  if (route_hints.is_ru_egress) {
    return EchMode::Disabled;
  }
  return EchMode::GreaseDraft17;
}

// Darwin (iOS/macOS): higher Safari probability.
#if TD_DARWIN
BrowserProfile pick_random_profile_platform(IRng &rng) {
  auto roll = rng.bounded(100);
  if (roll < 30) return BrowserProfile::Chrome131;
  if (roll < 40) return BrowserProfile::Chrome120;
  if (roll < 88) return BrowserProfile::SafariIos17;
  return BrowserProfile::Firefox128;
}
#endif
```

## 6.4 Chrome 131 профиль (non-RU: с 0xFE0D и ALPS)

Ключевые изменения относительно текущего кода:

```cpp
static TlsHello build_chrome131_hello() {
  TlsHello res;
  res.grease_size_ = 7;

  // ECH (encrypted_client_hello) extension block.
  // Type 0xFE0D = 65037 (IANA draft-ietf-tls-esni-17, replaces 0xFE02).
  // Inner structure starts with 5-byte prefix:
  // [OuterClientHello=0x00][KDF=0x0001][AEAD=0x0001].
  auto ech_block = [] {
    return Op::permutation_element({
      Op::str("\xfe\x0d"_q),       // type: 0xFE0D (encrypted_client_hello)
      Op::begin_scope(),
      Op::str("\x00\x00\x01\x00\x01"_q), // 5-byte prefix (outer + kdf + aead)
      Op::random(1),
      Op::str("\x00\x20"_q),      // encapsulated key length = 32
      Op::random(32),               // encapsulated key bytes
      Op::begin_scope(),
      Op::extension(),             // extensions
      Op::end_scope(),
      Op::end_scope(),
    });
  };

  // ALPS extension block.
  // Application Layer Protocol Settings.
  // Chrome 91–114: 0x44CD (alps-01 draft). Chrome 115+: 0x4469 (application_settings).
  // Current code has 0x44CD — must update to 0x4469 for Chrome 131 profile.
  // Structure: extension type (2) + length scope + supported ALPN protocol ids.
  auto alps_block = [] {
    return Op::permutation_element({
      Op::str("\x44\x69"_q),       // type: ALPS (0x4469, application_settings, Chrome 115+)
      Op::begin_scope(),
      Op::str("\x00\x03\x02\x68\x32"_q),  // length=3, protocol="h2"
      Op::end_scope(),
    });
  };

  // supported_groups extension with per-profile PQ named group.
  // GREASE slot is already handled by grease(4) — correct and unchanged.
  // The PQ group codepoint comes from profile registry/context.
  auto groups_block = [] {
    return Op::permutation_element({
      Op::str("\x00\x0a"_q),       // type: supported_groups
      Op::begin_scope(),
      Op::begin_scope(),
      Op::grease(4),               // GREASE value from Grease::init() — correct as-is
      Op::pq_group_id(),           // PQ named group from context/profile registry
      Op::str(
        "\x00\x1d"                 // X25519 (29)
        "\x00\x17"                 // P-256 (23)
        "\x00\x18"_q               // P-384 (24)
      ),
      Op::end_scope(),
      Op::end_scope(),
    });
  };

  res.ops_ = {
    Op::str("\x16\x03\x01"_q),     // TLS record: Content-Type Handshake, version TLS 1.0
    Op::begin_scope(),
    Op::str("\x01\x00"_q),         // Handshake Type: ClientHello
    Op::begin_scope(),
    Op::str("\x03\x03"_q),         // ClientHello version: TLS 1.2 (compat mode)
    Op::zero(32),                  // ClientHello.random (overwritten with HMAC later)
    Op::str("\x20"_q),             // session_id_length = 32
    Op::random(32),                // session_id: 32 random bytes (compat mode)
    Op::begin_scope(),             // cipher suites list
    Op::grease(0),                 // GREASE cipher suite
    Op::str(
      "\x13\x01\x13\x02\x13\x03"  // TLS 1.3: AES-128-GCM, AES-256-GCM, CHACHA20
      "\xc0\x2b\xc0\x2f\xc0\x2c\xc0\x30"  // ECDHE-ECDSA/RSA with AES-128/256-GCM
      "\xcc\xa9\xcc\xa8"           // ECDHE-ECDSA/RSA with CHACHA20
      "\xc0\x13\xc0\x14"          // ECDHE-RSA with AES-128/256-CBC-SHA
      "\x00\x9c\x00\x9d"          // RSA with AES-128/256-GCM
      "\x00\x2f\x00\x35"_q        // RSA with AES-128/256-CBC-SHA
    ),
    Op::end_scope(),
    Op::str("\x01\x00"_q),         // compression methods: 1 method, null
    Op::begin_scope(),             // extensions list
    // SNI is always first (after GREASE extension):
    Op::grease_extension(1),       // GREASE extension (len=0)
    Op::str("\x00\x00"_q),         // server_name type
    Op::begin_scope(),
    Op::begin_scope(),
    Op::str("\x00"_q),             // name_type: host_name
    Op::begin_scope(),
    Op::domain(),                  // SNI value
    Op::end_scope(),
    Op::end_scope(),
    Op::end_scope(),
    // Permuted extensions (order random per-connection):
    Op::permutation({
      // status_request (type 0x0005):
      Op::str("\x00\x05\x00\x05\x01\x00\x00\x00\x00"_q),
      // extended_master_secret (type 0x0017, len=0):
      Op::str("\x00\x17\x00\x00"_q),
      // renegotiation_info (type 0xFF01):
      Op::str("\xff\x01\x00\x01\x00"_q),
      // supported_groups (with valid GREASE):
      groups_block(),
      // ec_point_formats (type 0x000B):
      Op::str("\x00\x0b\x00\x02\x01\x00"_q),
      // session_ticket (type 0x0023, len=0):
      Op::str("\x00\x23\x00\x00"_q),
      // ALPN (type 0x0010): h2 + http/1.1:
      Op::str("\x00\x10\x00\x0e\x00\x0c\x02\x68\x32\x08\x68\x74\x74\x70\x2f\x31\x2e\x31"_q),
      // signature_algorithms (type 0x000D) — Chrome 131 list:
      Op::str("\x00\x0d\x00\x12\x00\x10"
              "\x04\x03\x08\x04\x04\x01\x05\x03"
              "\x08\x05\x05\x01\x08\x06\x06\x01"_q),
      // signed_certificate_timestamp (type 0x0012, len=0):
      Op::str("\x00\x12\x00\x00"_q),
      // key_share (type 0x0033) — with PQ hybrid key:
      // Structure: grease key share (1 byte) + PQ key share + X25519 key share
      // PQ group_id and key_exchange_length come from context (profile-dependent).
      Op::str("\x00\x33"_q),
      Op::begin_scope(),
      Op::begin_scope(),
      Op::grease(2),               // GREASE key share
      Op::str("\x00\x01\x00"_q),   // key_exchange_length=1, key_exchange=0x00
      Op::pq_key_share(),          // PQ: group_id (2B) + key_exchange_length (2B) from context
      Op::ml_kem_768_key(),        // 1184 bytes ML-KEM-768 public key
      Op::key(),                   // 32 bytes X25519 encapsulated key
      Op::str("\x00\x1d\x00\x20"_q), // X25519 group_id + key_exchange_length=32
      Op::key(),                   // 32 bytes X25519 public key
      Op::end_scope(),
      Op::end_scope(),
      // psk_key_exchange_modes (type 0x002D): psk_dhe_ke:
      Op::str("\x00\x2d\x00\x02\x01\x01"_q),
      // supported_versions (type 0x002B): TLS 1.3, TLS 1.2:
      Op::str("\x00\x2b\x00\x05\x04\x03\x04\x03\x03"_q),
      // compress_certificate (type 0x001B): brotli:
      Op::str("\x00\x1b\x00\x03\x02\x00\x02"_q),
      // ALPS (type 0x4469, application_settings) — ОБНОВЛЕНО с 0x44CD:
      alps_block(),
      // ECH (type 0xFE0D) — добавляется только в non-RU режиме:
      // if (ech_mode == EchMode::GreaseDraft17) parts.push_back(ech_block());
    }),
    // Padding extension (type 0x0015) — last, after permutation, per-connection size:
    Op::padding(),
    Op::end_scope(),
    Op::end_scope(),
    Op::end_scope(),
  };
  return res;
}
```

## 6.5 Safari iOS 17 профиль (исправленный)

```cpp
static TlsHello build_safari_ios17_hello() {
  TlsHello res;
  res.grease_size_ = 5;

  res.ops_ = {
    Op::str("\x16\x03\x01"_q),
    Op::begin_scope(),
    Op::str("\x01\x00"_q),
    Op::begin_scope(),
    Op::str("\x03\x03"_q),          // TLS 1.2 compat
    Op::zero(32),
    // Safari: session_id_length = 0 (no session ticket in iOS TLS stack).
    // Chrome/Firefox use 32 bytes; Safari uses 0. This is a per-platform invariant.
    Op::str("\x00"_q),               // session_id_length = 0
    // No Op::random(32) here — critical Safari distinction.
    Op::begin_scope(),               // cipher suites
    // Safari 17 cipher suites (3DES removed in iOS 15 / macOS Monterey):
    Op::str(
      "\x13\x01\x13\x02\x13\x03"    // TLS 1.3 suites
      "\xc0\x2c\xc0\x2b\xcc\xa9"    // ECDSA (Safari orders ECDSA before RSA)
      "\xc0\x30\xc0\x2f\xcc\xa8"    // RSA equivalents
      "\xc0\x0a\xc0\x09"            // ECDHE-ECDSA-AES256/128-CBC-SHA
      "\xc0\x14\xc0\x13"            // ECDHE-RSA-AES256/128-CBC-SHA
      "\x00\x9d\x00\x9c"            // RSA-AES256/128-GCM
      "\x00\x35\x00\x2f"_q          // RSA-AES256/128-CBC-SHA
      // NOT included: 0xC012 (ECDHE-RSA-3DES), 0x000A (RSA-3DES)
    ),
    Op::end_scope(),
    Op::str("\x01\x00"_q),           // compression: null
    Op::begin_scope(),               // extensions
    Op::grease_extension(1),
    // SNI:
    Op::str("\x00\x00"_q),
    Op::begin_scope(),
    Op::begin_scope(),
    Op::str("\x00"_q),
    Op::begin_scope(), Op::domain(), Op::end_scope(),
    Op::end_scope(), Op::end_scope(),
    // Safari-specific extension permutation (no ECH, no ALPS, no MlKem768):
    Op::permutation({
      Op::str("\x00\x17\x00\x00"_q),  // extended_master_secret
      Op::str("\xff\x01\x00\x01\x00"_q),  // renegotiation_info
      // supported_groups — Safari has NO PQ group, only classical curves:
      Op::str("\x00\x0a\x00\x08\x00\x06"_q),
      Op::begin_scope(),
      Op::grease(4),                  // GREASE from Grease::init() — correct
      Op::str("\x00\x1d\x00\x17\x00\x18"_q),  // X25519, P-256, P-384 (no PQ)
      Op::end_scope(),
      Op::str("\x00\x0b\x00\x02\x01\x00"_q),  // ec_point_formats
      Op::str("\x00\x23\x00\x00"_q),  // session_ticket
      // ALPN: h2 + http/1.1
      Op::str("\x00\x10\x00\x0e\x00\x0c\x02\x68\x32\x08\x68\x74\x74\x70\x2f\x31\x2e\x31"_q),
      Op::str("\x00\x0d\x00\x12\x00\x10"
              "\x04\x03\x08\x04\x04\x01\x05\x03"
              "\x08\x05\x05\x01\x08\x06\x06\x01"_q),  // sig algs
      Op::str("\x00\x2b\x00\x03\x02\x03\x04"_q),  // supported_versions: TLS 1.3 only
      Op::str("\x00\x2d\x00\x02\x01\x01"_q),      // psk_key_exchange_modes
      // key_share: X25519 only (no PQ for Safari):
      Op::str("\x00\x33\x00\x26\x00\x24\x00\x1d\x00\x20"_q),
      Op::random(32),
    }),
    // Safari: NO padding extension (padding_target = 0 in context).
    Op::padding(),   // no-op when context.get_padding_target() == 0
    Op::end_scope(),
    Op::end_scope(),
    Op::end_scope(),
  };
  return res;
}
```

## 6.6 TDD для профилей

```cpp
// test_browser_profiles.cpp

TEST(ProfileTest, Chrome131EchTypeIs0xFE0D) {
  auto h = generate_header_test(BrowserProfile::Chrome131, EchMode::GreaseDraft17);
  EXPECT_TRUE(has_extension(h, 0xFE0D)) << "ECH must use 0xFE0D (IANA correct type)";
  EXPECT_FALSE(has_extension(h, 0xFE02)) << "Old ECH type 0xFE02 must be absent";
}

TEST(ProfileTest, RuRouteDisablesEch) {
  auto h = generate_header_test(BrowserProfile::Chrome131, EchMode::Disabled);
  EXPECT_FALSE(has_extension(h, 0xFE0D));
  EXPECT_FALSE(has_extension(h, 0xFE02));
}

TEST(ProfileTest, Chrome131HasAlpsExtension) {
  auto h = generate_header_test(BrowserProfile::Chrome131);
  EXPECT_TRUE(has_extension(h, 0x4469)) << "ALPS must use 0x4469 (application_settings)";
  EXPECT_FALSE(has_extension(h, 0x44CD)) << "Old ALPS code 0x44CD must be absent in Chrome131";
}

TEST(ProfileTest, Chrome131EchInnerPrefixIs5Bytes) {
  auto h = generate_header_test(BrowserProfile::Chrome131, EchMode::GreaseDraft17);
  auto body = extract_extension_body(h, 0xFE0D);
  ASSERT_GE(body.size(), 5u);
  // Prefix bytes: \x00\x00\x01\x00\x01 (outer + kdf + aead).
  EXPECT_EQ(static_cast<uint8_t>(body[0]), 0x00u);
  EXPECT_EQ(static_cast<uint8_t>(body[1]), 0x00u);
  EXPECT_EQ(static_cast<uint8_t>(body[2]), 0x01u);
  EXPECT_EQ(static_cast<uint8_t>(body[3]), 0x00u);
  EXPECT_EQ(static_cast<uint8_t>(body[4]), 0x01u);
}

TEST(ProfileTest, Chrome131PqGroupMatchesProfileRegistry) {
  // Проверяем согласованность с registry, а не hardcoded codepoint.
  auto h = generate_header_test(BrowserProfile::Chrome131);
  auto groups = extract_supported_groups(h);
  auto expected = pq_group_for_profile(BrowserProfile::Chrome131);
  EXPECT_TRUE(std::find(groups.begin(), groups.end(), expected) != groups.end())
      << "Chrome131 must include PQ group from profile registry";
  auto key_shares = extract_key_share_groups(h);
  EXPECT_TRUE(std::find(key_shares.begin(), key_shares.end(), expected) != key_shares.end())
      << "Chrome131 key_share must use registry PQ group";
}

TEST(ProfileTest, AllProfilesGreaseIsValidRfc8701) {
  // GREASE values (from Grease::init) must follow RFC 8701.
  // PQ named group (0x6399 or 0x11EC) is NOT GREASE — it must not be confused.
  for (auto profile : {BrowserProfile::Chrome131, BrowserProfile::Chrome120,
                        BrowserProfile::Firefox128, BrowserProfile::SafariIos17}) {
    MockRng rng(42);
    for (int i = 0; i < 50; i++) {
      auto h = generate_header_test(profile, rng);
      auto groups = extract_supported_groups(h);
      ASSERT_FALSE(groups.empty()) << "Profile " << (int)profile << " has no groups";
      for (auto group : groups) {
        bool looks_grease = ((group >> 8) == (group & 0xFF));
        if (looks_grease) {
          uint8_t byte = group & 0xFF;
          EXPECT_EQ((byte - 0x0A) % 0x10, 0u)
              << "Non-standard GREASE-like value 0x" << std::hex << group
              << " at profile " << (int)profile << " i=" << i;
        }
      }
    }
  }
}

TEST(ProfileTest, SafariHasNoPqGroup) {
  auto h = generate_header_test(BrowserProfile::SafariIos17);
  auto groups = extract_supported_groups(h);
  EXPECT_FALSE(contains_any_pq_group(groups));
}

TEST(ProfileTest, SafariHasNoSessionIdBytes) {
  auto h = generate_header_test(BrowserProfile::SafariIos17);
  EXPECT_EQ(extract_session_id_length(h), 0u);
}

TEST(ProfileTest, SafariHasNo3Des) {
  auto suites = extract_cipher_suites(generate_header_test(BrowserProfile::SafariIos17));
  EXPECT_FALSE(contains(suites, uint16_t{0x000A}));  // RSA-3DES
  EXPECT_FALSE(contains(suites, uint16_t{0xC012}));  // ECDHE-RSA-3DES
}

TEST(ProfileTest, FirefoxHasNo3Des) {
  auto suites = extract_cipher_suites(generate_header_test(BrowserProfile::Firefox128));
  EXPECT_FALSE(contains(suites, uint16_t{0x000A}));
}

TEST(ProfileTest, FirefoxTls13OrderDiffersFromChrome) {
  // Firefox: 1301, 1303, 1302. Chrome: 1301, 1302, 1303.
  auto ff = extract_cipher_suites(generate_header_test(BrowserProfile::Firefox128));
  auto ch = extract_cipher_suites(generate_header_test(BrowserProfile::Chrome131));
  auto ff_1303_pos = std::find(ff.begin(), ff.end(), uint16_t{0x1303}) - ff.begin();
  auto ch_1303_pos = std::find(ch.begin(), ch.end(), uint16_t{0x1303}) - ch.begin();
  EXPECT_LT(ff_1303_pos, ch_1303_pos);
}

TEST(ProfileTest, Ja3DoesNotMatchKnownTelegramHash) {
  // Known Telegram JA3 from the captured pcap.
  const string known_tg_ja3 = "e0e58235789a753608b12649376e91ec";
  for (auto profile : {BrowserProfile::Chrome131, BrowserProfile::Chrome120,
                        BrowserProfile::Firefox128, BrowserProfile::SafariIos17}) {
    MockRng rng(42);
    auto h = generate_header_test(profile, rng);
    auto ja3 = compute_ja3(h);
    EXPECT_NE(ja3, known_tg_ja3) << "Profile " << (int)profile
                                  << " still matches known Telegram JA3";
  }
}
```

---

# 7. PR-3: IStreamTransport Extensions + Activation Gate + StealthConfig

**Зависит от:** PR-2  
**Исправляет:** Activation Gate (единственный if в create_transport)

## 7.1 IStreamTransport — новые virtual методы

```cpp
// td/mtproto/IStreamTransport.h — добавить:
class IStreamTransport {
 public:
  // Called by RawConnection before each flush cycle.
  // Allows the decorator to drain the ring buffer and set record size.
  virtual void pre_flush_write(double now) {}

  // Returns the next time (double seconds since epoch) at which the decorator
  // has a pending write to deliver. 0.0 = no pending work.
  virtual double get_shaping_wakeup() const { return 0.0; }

  // Sets the traffic type hint for the next write() call.
  // Consumed once (auto-reset to Interactive after write).
  virtual void set_traffic_hint(stealth::TrafficHint hint) {}

  // Sets the maximum TLS record payload size for the next record.
  // Used by DrsEngine to control record sizes through the virtual interface
  // without requiring dynamic_cast to ObfuscatedTransport.
  virtual void set_max_tls_record_size(int32 size) {}
};
```

## 7.2 ObfuscatedTransport — set_max_tls_record_size override

```cpp
// td/mtproto/TcpTransport.h
class ObfuscatedTransport final : public IStreamTransport {
 public:
  // Primary constructor (backward-compatible).
  ObfuscatedTransport(int16 dc_id, ProxySecret secret)
      : ObfuscatedTransport(dc_id, std::move(secret), kDefaultMaxTlsPacketLength) {}

  // Extended constructor: allows DRS to set initial record size.
  ObfuscatedTransport(int16 dc_id, ProxySecret secret, int32 max_tls_packet_length)
      : dc_id_(dc_id), secret_(std::move(secret)),
        impl_(secret_.use_random_padding()),
        max_tls_packet_length_(max_tls_packet_length) {}

  void set_max_tls_record_size(int32 size) override {
    // HOT PATH: no allocation. Clamped to valid TLS record payload range.
    max_tls_packet_length_ = td::clamp(size, 512, 16384);
  }

 private:
  // Default used only by the backward-compat 2-arg constructor (call sites that
  // do not go through create_transport).  The production stealth path in
  // create_transport() always passes kInitialRecordSize explicitly, so this
  // default does NOT affect stealth connections.
  // Value 1380 (≈1 MTU payload) chosen over historic 2878 so that any legacy
  // call site starts with a sane slow-start size rather than the well-known
  // Telegram record length.
  static constexpr int32 kDefaultMaxTlsPacketLength = 1380;
  int32 max_tls_packet_length_;
};
```

## 7.3 Activation Gate: единственный if в create_transport()

```cpp
// td/mtproto/IStreamTransport.cpp

unique_ptr<IStreamTransport> create_transport(int16 dc_id, ProxySecret secret) {
  auto inner = make_unique<ObfuscatedTransport>(dc_id, secret,
                                                stealth::kInitialRecordSize);

  // Stealth shaping is active ONLY when the proxy secret has the 0xEE prefix.
  // This is the single, unconditional activation gate for all masking logic.
  // Without emulate_tls(), Telegram works normally with no masking overhead.
#if TDLIB_STEALTH_SHAPING
  if (secret.emulate_tls()) {
    auto config = stealth::StealthConfig::from_secret(secret, stealth::global_rng());
    return make_unique<stealth::StealthTransportDecorator>(std::move(inner), config);
  }
#endif

  return inner;
}
```

## 7.4 StealthConfig

```cpp
// td/mtproto/stealth/StealthConfig.h
struct StealthConfig {
  BrowserProfile profile;
  PaddingPolicy padding_policy;
  IptParams ipt_params;      // from shared submodule or runtime JSON
  DrsWeights drs_weights;    // from shared submodule or runtime JSON

  static StealthConfig from_secret(const ProxySecret &secret, IRng &rng);
  static StealthConfig default_config(IRng &rng);

  // Override defaults from a runtime-loaded params file.
  // Returns modified config; original is unchanged (immutable update).
  StealthConfig with_overrides(const StealthParamsOverride &overrides) const;
};

// Initial TLS record size for DRS SlowStart phase.
// Chosen to be within 1 IP MTU (1500 bytes) minus TCP/IP/TLS headers.
// This mimics real browser behavior at TCP slow-start.
constexpr int32 kInitialRecordSize = 1380;
```

---

# 8. PR-4: StealthTransportDecorator

**Зависит от:** PR-3  
**Реализует:** Activation Gate, consume-once hint, ring buffer + hard backpressure

## 8.1 Заголовочный файл

> **⚠ Заметка о ring buffer capacity:** `ShaperRingBuffer::kDefaultCapacity` должен быть достаточным для типичного burst'а в chat-сценарии (5-10 сообщений подряд), но не слишком большим, чтобы не копить stale данные. Рекомендуется **32** (покрывает burst из ~10 MTProto-пакетов при IPT ~30ms). Слишком маленькая ёмкость (2-4) вызывает частый вход в backpressure (снижение throughput), но не должна приводить к bypass shaping. Значение конфигурируемо через `StealthConfig`.

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

  // IStreamTransport interface:
  bool can_write() const override;
  void write(BufferWriter &&message, bool quick_ack) override;
  void pre_flush_write(double now) override;
  double get_shaping_wakeup() const override;
  void set_traffic_hint(TrafficHint hint) override;
  void set_max_tls_record_size(int32 size) override;

  // Test-only accessors (exposed via friend or #ifdef TDLIB_TEST):
  DrsEngine::Phase drs_phase() const { return drs_.current_phase(); }

 private:
  unique_ptr<IStreamTransport> inner_;
  unique_ptr<IRng> rng_;
  unique_ptr<IClock> clock_;
  IptController ipt_;
  DrsEngine drs_;
  ShaperRingBuffer ring_;
  double last_write_time_{0.0};

  struct PendingWrite {
    BufferWriter message;
    bool quick_ack;
    double send_at;
    TrafficHint hint;
  };

  // Hint is consumed-once: set by set_traffic_hint(), read and reset in write().
  // Default: Interactive. Auto-resets after each write() to prevent hint-drift.
  TrafficHint pending_hint_{TrafficHint::Interactive};

  static constexpr double kDrsIdleThresholdSec = 0.5;
  size_t high_watermark_;
  size_t low_watermark_;

  // Classifies a write by message size when no explicit hint is set.
  // This is a fallback; explicit hints from SessionConnection take priority.
  static TrafficHint classify_by_size(size_t bytes) noexcept;
};

}  // namespace td::mtproto::stealth
```

## 8.2 Реализация

```cpp
// td/mtproto/stealth/StealthTransportDecorator.cpp

void StealthTransportDecorator::set_traffic_hint(TrafficHint hint) {
  pending_hint_ = hint;
}

bool StealthTransportDecorator::can_write() const {
  // Hard backpressure: never bypass shaping path on overload.
  if (!inner_->can_write()) {
    return false;
  }
  return ring_.size() < high_watermark_;
}

void StealthTransportDecorator::write(BufferWriter &&message, bool quick_ack) {
  // Consume hint once; auto-reset prevents drift across consecutive writes.
  // If the hint was not explicitly set (Interactive default), apply size heuristic.
  auto hint = pending_hint_;
  pending_hint_ = TrafficHint::Interactive;

  // Size-based fallback: overrides Interactive with Keepalive for tiny packets.
  // Explicit BulkData hint always takes priority over size classification.
  if (hint == TrafficHint::Interactive || hint == TrafficHint::Unknown) {
    auto size_hint = classify_by_size(message.size());
    if (size_hint == TrafficHint::Keepalive) hint = TrafficHint::Keepalive;
  }

  auto delay_us = ipt_.next_delay_us(!ring_.empty(), hint);
  auto send_at = clock_->now() + static_cast<double>(delay_us) / 1e6;
  PendingWrite pw{std::move(message), quick_ack, send_at, hint};

  if (ring_.try_enqueue(std::move(pw))) {
    return;
  }

  // Overload path: still NO direct write to inner_.
  // Caller must observe can_write()==false and retry later via actor loop wakeup.
  // This preserves both DRS and IPT invariants under burst.
  // Optional: record a metric counter here.
}

void StealthTransportDecorator::pre_flush_write(double now) {
  // Detect idle gap: if the connection has been silent for > kDrsIdleThresholdSec,
  // reset DRS to SlowStart. This mimics real browser behavior: a new request
  // after a pause starts a new slow-start ramp, making record sizes consistent
  // with observed HTTPS patterns.
  if (last_write_time_ > 0.0 &&
      (now - last_write_time_) > kDrsIdleThresholdSec) {
    drs_.notify_idle();
  }

  ring_.drain_ready(now, [this, now](PendingWrite &&pw) {
    const int32 rec_size = drs_.next_record_size(pw.hint);
    inner_->set_max_tls_record_size(rec_size);
    const size_t bytes = pw.message.size();
    inner_->write(std::move(pw.message), pw.quick_ack);
    drs_.notify_bytes_written(bytes);
    last_write_time_ = now;
  });

  // Hysteresis: backpressure is naturally lifted when queue drained below low watermark.
}

TrafficHint StealthTransportDecorator::classify_by_size(size_t bytes) noexcept {
  // Keepalive/ping MTProto packets are typically very small.
  // This catches cases where SessionConnection does not set an explicit hint.
  if (bytes <= 64)   return TrafficHint::Keepalive;
  if (bytes >= 4096) return TrafficHint::BulkData;
  return TrafficHint::Interactive;
}
```

## 8.3 TDD

```cpp
// test_decorator.cpp

TEST(DecoratorActivation, PassthroughWhenStealthDisabled) {
  // Without emulate_tls(), create_transport() must return ObfuscatedTransport directly.
  auto secret = ProxySecret::from_link("abc123");  // no 0xEE prefix
  EXPECT_FALSE(secret.emulate_tls());
  auto transport = create_transport(1, secret);
  // No StealthTransportDecorator layer:
  EXPECT_EQ(nullptr, dynamic_cast<StealthTransportDecorator*>(transport.get()));
}

TEST(DecoratorActivation, DecoratorActiveWhenEmulatesTls) {
  auto secret = ProxySecret::from_link("ee" + string(64, '0'));
  EXPECT_TRUE(secret.emulate_tls());
  auto transport = create_transport(1, secret);
  EXPECT_NE(nullptr, dynamic_cast<StealthTransportDecorator*>(transport.get()));
}

TEST(DecoratorHint, ConsumeOnceHintAutoResets) {
  auto [dec, inner_ptr] = make_test_decorator();
  dec->set_traffic_hint(TrafficHint::Keepalive);
  dec->write(make_test_buffer(20), false);   // consumes Keepalive
  dec->write(make_test_buffer(500), false);  // must be Interactive (auto-reset)

  // First write: delay = 0 (Keepalive bypass).
  // Second write: delay > 0 (Interactive, not leaked Keepalive).
  // Verify via ring buffer: second write's send_at > clock.now():
  EXPECT_GT(inner_ptr->writes[1].scheduled_delay_us, 0u);
}

TEST(DecoratorBackpressure, CanWriteTurnsFalseAtHighWatermark) {
  auto [dec, inner_ptr] = make_test_decorator(/*ring_capacity=*/2);
  dec->write(make_test_buffer(100), false);  // fills ring[0]
  dec->write(make_test_buffer(100), false);  // fills ring[1]
  EXPECT_FALSE(dec->can_write());
}

TEST(DecoratorBackpressure, OverflowNeverWritesInnerSynchronously) {
  auto [dec, inner_ptr] = make_test_decorator(/*ring_capacity=*/2);
  dec->write(make_test_buffer(100), false);
  dec->write(make_test_buffer(100), false);
  dec->write(make_test_buffer(100), false);  // overload path

  // No direct write bypass is allowed.
  EXPECT_TRUE(inner_ptr->writes.empty());
}

TEST(DecoratorDrsIdle, ResetsToSlowStartAfterIdleGap) {
  MockClock clock;
  auto [dec, _] = make_test_decorator_with_clock(clock);
  drive_drs_to_steady_state(*dec, clock);
  EXPECT_EQ(dec->drs_phase(), DrsEngine::Phase::SteadyState);

  clock.advance(kDrsIdleThresholdSec + 0.01);
  dec->pre_flush_write(clock.now());
  EXPECT_EQ(dec->drs_phase(), DrsEngine::Phase::SlowStart);
}
```

---

# 9. PR-5: IPT — Inter-Packet Timing

**Зависит от:** PR-4  
**Исправляет:** S12 (равномерный IPT), S14 (keepalive delay)

## 9.1 TrafficHint

```cpp
// td/mtproto/stealth/TrafficHint.h
enum class TrafficHint : uint8_t {
  Unknown      = 0,
  Interactive  = 1,  // Chat messages, API calls — log-normal delay
  BulkData     = 2,  // File/media transfer — drain immediately
  Keepalive    = 3,  // MTProto PING — bypass IPT (critical: avoid 28s disconnect)
  AuthHandshake= 4,  // First ~3 packets after connect — drain immediately
};
```

## 9.2 IptParams (shared submodule)

```cpp
// telemt-stealth-params/params.h (синхронизирован с telemt)
struct IptParams {
  // Log-normal parameters for Burst state: μ=3.5 → median ~33ms, realistic HTTP think time.
  double burst_mu_ms    = 3.5;
  double burst_sigma    = 0.8;
  double burst_max_ms   = 200.0;  // hard cap: never exceed ping_disconnect_delay / 140

  // Pareto parameters for Idle state inter-request delays.
  // ⚠ Выбор Pareto: heavy tail создаёт реалистичные длинные паузы между запросами.
  // Альтернативы: exponential (слишком "ровный"), Weibull (хорош для modeling browse time).
  // Pareto с alpha=1.5 даёт P(X > 3s) ≈ 6%, что соответствует наблюдениям за real user idle.
  // При необходимости (если ML-модель ТСПУ настроена на Pareto-тесты) — заменяемо через PR-8 JSON.
  double idle_alpha     = 1.5;
  double idle_scale_ms  = 500.0;
  double idle_max_ms    = 3000.0;  // hard cap: 99.9th percentile << 28s disconnect

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

  // Box-Muller log-normal sampler (no std::lognormal_distribution dependency).
  double sample_lognormal(double mu, double sigma);
  MarkovState transition(bool has_pending);
};

uint64_t IptController::next_delay_us(bool has_pending_data, TrafficHint hint) {
  // Bypass path: must be first check, minimal latency for keepalive packets.
  // Critical: if keepalive is delayed, MTProto session disconnects at 28s.
  if (hint == TrafficHint::Keepalive    ||
      hint == TrafficHint::BulkData     ||
      hint == TrafficHint::AuthHandshake) {
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
      // Pareto-distributed idle delay via inverse CDF.
      double u = static_cast<double>(rng_.next_u32()) / 4294967296.0;
      u = std::max(u, 1e-9);  // Avoid zero-probability edge in inverse CDF.
      double delay_ms = params_.idle_scale_ms *
                        std::pow(u, -1.0 / params_.idle_alpha);
      delay_ms = std::min(delay_ms, params_.idle_max_ms);
      return static_cast<uint64_t>(delay_ms * 1000.0);
    }
  }, state_);
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

TEST(IptController, NoDelayExceeds28SecondDisconnectTimeout) {
  MockRng rng(0);
  IptController ipt(IptParams{}, rng);
  for (int i = 0; i < 100000; i++) {
    auto delay = ipt.next_delay_us(true, TrafficHint::Interactive);
    EXPECT_LT(delay, 28'000'000u)
        << "Delay " << delay << "us at i=" << i << " would cause disconnect";
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
```

---

# 10. PR-6: DRS — Dynamic Record Sizing с Jitter

**Зависит от:** PR-4  
**Исправляет:** S5 (статическое 2878), S13 (механистичные размеры без jitter)

## 10.1 DrsWeights (shared submodule)

```cpp
// telemt-stealth-params/params.h
struct DrsWeights {
  // Phase target sizes (before jitter).
  int32 slow_start_size    = 1380;   // ~1 MTU payload
  int32 congestion_size    = 4096;   // ~3 MTU payloads
  int32 steady_state_size  = 16384;  // max TLS record

  // Phase transition thresholds.
  int32 slow_start_records = 4;      // records before → CongestionOpen
  int32 congestion_bytes   = 32768;  // bytes before → SteadyState

  // Jitter factor (±fraction). 0.10 = ±10%.
  // Makes record sizes non-mechanical; harder for ML classifiers.
  double jitter_fraction = 0.10;
};
```

## 10.2 DrsEngine с jitter

```cpp
// td/mtproto/stealth/DrsEngine.h

class DrsEngine {
 public:
  enum class Phase { SlowStart, CongestionOpen, SteadyState };

  explicit DrsEngine(const DrsWeights &weights, IRng &rng);

  // Returns next record size with ±jitter applied.
  // BulkData hint → SteadyState size immediately.
  int32 next_record_size(TrafficHint hint);

  void notify_bytes_written(size_t bytes);

  // Called when idle gap detected (> kDrsIdleThresholdSec).
  // Resets to SlowStart; next connection-burst ramps up again.
  void notify_idle();

  Phase current_phase() const noexcept { return phase_; }

 private:
  DrsWeights weights_;
  IRng &rng_;
  Phase phase_{Phase::SlowStart};
  size_t records_in_phase_{0};
  size_t bytes_in_phase_{0};

  // Applies ±jitter_fraction to a target size.
  // Result is clamped to [512, 16384] (valid TLS record payload range).
  int32 apply_jitter(int32 target) noexcept;
  void maybe_advance_phase();
};

int32 DrsEngine::next_record_size(TrafficHint hint) {
  if (hint == TrafficHint::BulkData) {
    return apply_jitter(weights_.steady_state_size);
  }
  int32 target;
  switch (phase_) {
    case Phase::SlowStart:     target = weights_.slow_start_size;   break;
    case Phase::CongestionOpen:target = weights_.congestion_size;   break;
    case Phase::SteadyState:   target = weights_.steady_state_size; break;
    default: UNREACHABLE();
  }
  return apply_jitter(target);
}

int32 DrsEngine::apply_jitter(int32 target) noexcept {
  // Uniform jitter in [-jitter_fraction, +jitter_fraction].
  // jitter_fraction = 0.10 → ±10% of target.
  auto max_delta = static_cast<int32>(target * weights_.jitter_fraction);
  if (max_delta == 0) return target;
  auto range = static_cast<uint32_t>(2 * max_delta + 1);
  int32 delta = static_cast<int32>(rng_.bounded(range)) - max_delta;
  return td::clamp(target + delta, 512, 16384);
}

void DrsEngine::notify_bytes_written(size_t bytes) {
  records_in_phase_++;
  bytes_in_phase_ += bytes;
  maybe_advance_phase();
}

void DrsEngine::notify_idle() {
  phase_ = Phase::SlowStart;
  records_in_phase_ = 0;
  bytes_in_phase_ = 0;
}

void DrsEngine::maybe_advance_phase() {
  switch (phase_) {
    case Phase::SlowStart:
      if (records_in_phase_ >= static_cast<size_t>(weights_.slow_start_records)) {
        phase_ = Phase::CongestionOpen;
        records_in_phase_ = 0;
        bytes_in_phase_ = 0;
      }
      break;
    case Phase::CongestionOpen:
      if (bytes_in_phase_ >= static_cast<size_t>(weights_.congestion_bytes)) {
        phase_ = Phase::SteadyState;
        records_in_phase_ = 0;
        bytes_in_phase_ = 0;
      }
      break;
    case Phase::SteadyState:
      break;
  }
}
```

## 10.3 TDD для DRS

```cpp
TEST(DrsEngine, PhasesProgressCorrectly) {
  MockRng rng(1);
  DrsWeights w;
  DrsEngine drs(w, rng);
  EXPECT_EQ(drs.current_phase(), DrsEngine::Phase::SlowStart);
  for (int i = 0; i < w.slow_start_records; i++) {
    drs.next_record_size(TrafficHint::Interactive);
    drs.notify_bytes_written(1380);
  }
  EXPECT_EQ(drs.current_phase(), DrsEngine::Phase::CongestionOpen);
  drs.notify_bytes_written(w.congestion_bytes);
  EXPECT_EQ(drs.current_phase(), DrsEngine::Phase::SteadyState);
}

TEST(DrsEngine, JitterMakesRecordSizesNonConstant) {
  MockRng rng(42);
  DrsWeights w;
  DrsEngine drs(w, rng);
  std::set<int32> sizes;
  for (int i = 0; i < 100; i++) {
    sizes.insert(drs.next_record_size(TrafficHint::Interactive));
  }
  // With 10% jitter on 1380 target: range [1242, 1518].
  // Must see ≥3 different sizes in 100 draws.
  EXPECT_GE(sizes.size(), 3u);
}

TEST(DrsEngine, JitterBoundsAreRespected) {
  MockRng rng(7);
  DrsWeights w;
  DrsEngine drs(w, rng);
  for (int i = 0; i < 1000; i++) {
    int32 sz = drs.next_record_size(TrafficHint::Interactive);
    EXPECT_GE(sz, 512);
    EXPECT_LE(sz, 16384);
  }
}

TEST(DrsEngine, IdleResetsToSlowStart) {
  MockRng rng(1);
  DrsWeights w;
  DrsEngine drs(w, rng);
  // Drive to SteadyState:
  for (int i = 0; i < w.slow_start_records; i++) drs.notify_bytes_written(1380);
  drs.notify_bytes_written(w.congestion_bytes);
  ASSERT_EQ(drs.current_phase(), DrsEngine::Phase::SteadyState);
  // Idle reset:
  drs.notify_idle();
  EXPECT_EQ(drs.current_phase(), DrsEngine::Phase::SlowStart);
}

TEST(DrsEngine, RecordSizeNotFixedTo2878Signature) {
  MockRng rng(42);
  DrsWeights w;
  DrsEngine drs(w, rng);
  std::set<int32> seen;
  for (int i = 0; i < 500; i++) {
    int32 sz = drs.next_record_size(TrafficHint::Interactive);
    drs.notify_bytes_written(sz);
    seen.insert(sz);
    // Strict invariant: 2878 is the known Telegram signature and must never
    // appear.  With default DrsWeights, jitter ranges are [1242,1518],
    // [3687,4505], and [14746,16384] — none overlap 2878.  If this fires,
    // DRS weights were changed to a range that accidentally re-introduces the
    // Telegram fingerprint.
    EXPECT_NE(sz, 2878) << "Telegram-signature record size 2878 at i=" << i;
  }
  // Distribution invariant: DRS must produce variety, not a fixed size.
  EXPECT_GT(seen.size(), 3u);
}
```

---

# 11. PR-7: TrafficClassifier + SessionConnection Wiring

**Зависит от:** PR-4  
**Исправляет:** S14 (keepalive), S15 (auth handshake delay)

## 11.1 SessionConnection изменения

```cpp
// td/mtproto/SessionConnection.h — добавить:
class SessionConnection {
  // ...
 private:
  int32 auth_packets_remaining_{0};  // drain auth packets immediately
};

// td/mtproto/SessionConnection.cpp

void SessionConnection::start_auth() {
  // Signal decorator to drain the initial MTProto auth exchange immediately.
  // If this is delayed by IPT, the auth round-trip time increases and the
  // connection setup looks anomalous compared to real HTTPS.
  if (connection_) {
    connection_->set_traffic_hint(stealth::TrafficHint::AuthHandshake);
    auth_packets_remaining_ = 3;
  }
}

void SessionConnection::do_loop() {
  // ... existing code ...

  // Schedule wakeup for the shaper ring drain.
  auto wakeup_at = connection_->get_shaping_wakeup();
  if (wakeup_at > 0.0) {
    relax_timeout_at(wakeup_at);
  }
}

void SessionConnection::flush_packet() {
  // Set traffic hint before each packet write.
  // Explicit hints take priority over size-based classification in the decorator.
  if (auth_packets_remaining_ > 0) {
    connection_->set_traffic_hint(stealth::TrafficHint::AuthHandshake);
    --auth_packets_remaining_;
  } else if (is_keepalive_message(flush_msg_id_)) {
    connection_->set_traffic_hint(stealth::TrafficHint::Keepalive);
  }
  // No else: decorator's size heuristic handles the rest.
  // (Large packets → BulkData, small packets → Keepalive, medium → Interactive)

  // ... existing flush logic ...
}
```

## 11.2 RawConnection изменения

```cpp
// td/mtproto/RawConnection.cpp — в flush_write():
void RawConnection::flush_write() {
  // Notify decorator of flush cycle start. This is where the ring buffer
  // is drained and DRS record sizes are applied.
  if (transport_) {
    transport_->pre_flush_write(Time::now());
  }
  // ... existing flush logic ...
}
```

## 11.3 TDD

```cpp
TEST(SessionWiring, AuthHandshakeHintSetForFirstThreePackets) {
  auto [session, transport_ptr] = make_test_session();
  session->start_auth();
  for (int i = 0; i < 3; i++) {
    session->flush_packet();
    EXPECT_EQ(transport_ptr->received_hints.back(), TrafficHint::AuthHandshake)
        << "Packet " << i << " must have AuthHandshake hint";
  }
  // 4th packet: no explicit hint (decorator uses size heuristic):
  session->flush_packet();
  EXPECT_NE(transport_ptr->received_hints.back(), TrafficHint::AuthHandshake);
}

TEST(SessionWiring, KeepalivePacketGetsKeepaliveHint) {
  auto [session, transport_ptr] = make_test_session();
  session->send_keepalive();
  session->flush_packet();
  EXPECT_EQ(transport_ptr->received_hints.back(), TrafficHint::Keepalive);
}
```

---

# 12. PR-8: Runtime Params Loader

**Зависит от:** PR-3  
**Исправляет:** ТСПУ адаптируется быстрее чем возможен rebuild

## 12.1 Формат файла `~/.config/tdlib-obf/stealth-params.json`

```json
{
  "version": 1,
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
    "slow_start_size": 1380,
    "congestion_size": 4096,
    "steady_state_size": 16384,
    "slow_start_records": 4,
    "congestion_bytes": 32768,
    "jitter_fraction": 0.10
  },
  "profile_weights": {
    "Chrome131": 48,
    "Chrome120": 17,
    "SafariIos17": 20,
    "Firefox128": 8,
    "remainder_profile": "Chrome131"
  },
  "route_policy": {
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

## 12.2 StealthParamsLoader

```cpp
// td/mtproto/stealth/StealthParamsLoader.h

class StealthParamsLoader {
 public:
  // Returns nullptr if file not found or invalid — callers use compile-time defaults.
  // Fail-closed: any parse error → nullptr (safe default).
  static unique_ptr<StealthParamsOverride> try_load(Slice config_path) noexcept;

  // Call periodically (e.g., every 60s) to detect file changes.
  // Thread-safe: uses shared_mutex for concurrent reads.
  // Returns true if params were reloaded.
  bool try_reload() noexcept;

  // Get current params (hot path: shared_lock, no exclusive lock).
  StealthParamsOverride get() const;

 private:
  string config_path_;
  std::atomic<int64> last_mtime_{0};
  static constexpr size_t kMaxConfigBytes = 64 * 1024;
  mutable std::shared_mutex mu_;        // readers: shared_lock; writer: unique_lock
  shared_ptr<StealthParamsOverride> current_;

  // Validates all numeric fields are within safe ranges.
  // Returns false if any field is out-of-range or missing.
  static bool validate(const StealthParamsOverride &params) noexcept;

  // Security checks before JSON parse:
  // - file must be regular (no symlink/device)
  // - owner must be current user
  // - size must be <= kMaxConfigBytes
  // - file mode must not be world-writable
  static bool validate_file_security(const string &path) noexcept;
};
```

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

  // DRS validation: must stay within valid TLS record bounds.
  if (p.drs.slow_start_size < 512 || p.drs.slow_start_size > 2048)  return false;
  if (p.drs.congestion_size < 1024 || p.drs.congestion_size > 8192) return false;
  if (p.drs.steady_state_size < 8192 || p.drs.steady_state_size > 16384) return false;
  if (p.drs.jitter_fraction < 0.0 || p.drs.jitter_fraction > 0.25) return false;

  // Profile weights: explicit fields may sum to <=100.
  // Remainder is assigned to weights.remainder_profile.
  int sum = p.weights.chrome131 + p.weights.chrome120 +
            p.weights.safari17 + p.weights.firefox128;
  if (sum < 0 || sum > 100) return false;
  if (!is_valid_profile(p.weights.remainder_profile)) return false;

  // Route policy validation.
  if (p.route_policy.ru_egress.allow_quic) return false;  // TCP+TLS only in this design.
  if (p.route_policy.ru_egress.ech_mode != EchMode::Disabled) return false;
  if (p.route_policy.non_ru_egress.ech_mode != EchMode::Disabled &&
      p.route_policy.non_ru_egress.ech_mode != EchMode::GreaseDraft17) return false;

  return true;
}
```

---

# 13. PR-9: Integration Smoke Tests

**Зависит от:** PR-7 (все production PRs полностью реализованы)

## 13.1 `check_fingerprint.py` — JA3/JA4 верификация

```python
#!/usr/bin/env python3
"""
Captures 50 ClientHello packets from tdlib via local telemt proxy
and verifies JA3/JA4 do NOT match known Telegram fingerprints.

Also compares timing and TLS record-size distributions with local baseline captures
from docs/Samples/Traffic dumps/*.pcap* to detect synthetic drift.

Usage: python check_fingerprint.py --interface lo --port 8888
"""

KNOWN_TELEGRAM_JA3 = {
    "e0e58235789a753608b12649376e91ec",  # Original Telegram client
}

# PQ group codepoints are loaded from profile registry snapshot
# generated from validated captures for the target rollout wave.
EXPECTED_PQ_GROUPS = load_profile_registry("profiles_validation.json")

def check_ech_policy(ch: ClientHello, mode: str) -> bool:
    """ECH behavior must follow route policy mode."""
    has_old = any(ext.type == 0xFE02 for ext in ch.extensions)
    has_new = any(ext.type == 0xFE0D for ext in ch.extensions)
    if has_old:
        return False
    if mode == "disabled":
        return not has_new
    if mode == "grease_draft17":
        return has_new
    return False

def check_ech_enc_key_len_consistent(ch: ClientHello) -> bool:
    """For 0xFE0D extension, encapsulated key length must match payload bytes."""
    ext = extract_extension(ch, 0xFE0D)
    if ext is None:
        return True
    return parse_ech_outer(ext).is_structurally_valid()

def contains_any_pq_group(groups: list[int]) -> bool:
    return any(g in {0x11EC, 0x6399} for g in groups)

def is_safari(ch: ClientHello) -> bool:
    return detect_profile(ch) == "SafariIos17"

def runtime_mode_for_sample(ch: ClientHello) -> str:
    # Derived from test scenario: "disabled" for RU mode, "grease_draft17" for non-RU mode.
    return ch.metadata.route_mode

def check_no_old_ech_type(ch: ClientHello) -> bool:
    for ext in ch.extensions:
        if ext.type == 0xFE02:
            return False
    return True

def check_pq_group_codepoint(ch: ClientHello) -> bool:
    """PQ group in supported_groups must match profile registry."""
    profile = detect_profile(ch)
    expected = EXPECTED_PQ_GROUPS.get(profile)
    if expected is None:
        return not contains_any_pq_group(ch.supported_groups)
    return expected in ch.supported_groups

def check_alps_present(ch: ClientHello) -> bool:
    """Chrome profiles must include ALPS extension (0x4469, not old 0x44CD)."""
    profile = detect_profile(ch)
    if profile in ('Chrome131', 'Chrome120'):
        has_new = any(ext.type == 0x4469 for ext in ch.extensions)
        has_old = any(ext.type == 0x44CD for ext in ch.extensions)
        return has_new and not has_old  # Must use new code only
    return True  # Safari/Firefox don't have ALPS

def check_ech_payload_variance(samples: list[ClientHello]) -> bool:
    """
    In non-RU grease mode, ECH payload length must vary across connections.
    Expected buckets: {144, 176, 208, 240}. Require >=3 unique values in 50 samples.
    """
    lengths = set()
    for ch in samples:
        if ch.metadata.route_mode != "grease_draft17":
            continue
        ext = extract_extension(ch, 0xFE0D)
        if ext is None:
            continue
        lengths.add(parse_ech_outer(ext).payload_len)
    return len(lengths) >= 3

CHECKS = [
    ("JA3 not Telegram", lambda ch: compute_ja3(ch) not in KNOWN_TELEGRAM_JA3),
    ("ECH policy respected", lambda ch: check_ech_policy(ch, runtime_mode_for_sample(ch))),
    ("No old ECH type 0xFE02", check_no_old_ech_type),
    ("ECH wire structure valid", check_ech_enc_key_len_consistent),
    ("PQ group codepoint correct", check_pq_group_codepoint),
    ("ALPS 0x4469 for Chrome", check_alps_present),
    ("No old ALPS 0x44CD", lambda ch: not any(e.type == 0x44CD for e in ch.extensions)),
    ("No 3DES in Safari", lambda ch: 0x000A not in ch.cipher_suites if is_safari(ch) else True),
    ("Padding not exactly 517 bytes", lambda ch: len(ch.raw) != 517),
    ("ECH payload varies in grease mode", check_ech_payload_variance),
    ("PQ in groups matches key_share", lambda ch: check_pq_group_consistency(ch)),
]
```

## 13.2 `check_ipt.py` — межпакетные интервалы

```python
def check_ipt(pcap_file: str) -> bool:
    """
    K-S test of inter-packet intervals against log-normal distribution.
    Plus distribution-distance check (EMD/KL) against baseline from
    docs/Samples/Traffic dumps/test_logs.pcapng and docs/Samples/Traffic dumps/Fire.pcapng.
    FAIL: p < 0.05 (uniform distribution → detected as non-browser).
    FAIL: any interval > 5s (safety margin before 28s disconnect).
    FAIL: keepalive delayed > 100ms.
    PASS: keepalive < 10ms, interactive delays log-normal.
    """
```

## 13.3 `check_drs.py` — размеры TLS записей

```python
def check_drs(pcap_file: str) -> bool:
    """
    FAIL: dominant-mode record size is 2878 (legacy Telegram signature reappears).
    FAIL: ≥10 consecutive records of identical size (no jitter).
    FAIL: no records > 8192 after first 100KB (DRS not progressing).
    FAIL: record-size histogram diverges from baseline HTTPS captures
          (docs/Samples/Traffic dumps/*.pcap*) above configured threshold.
    PASS: records show slow-start → congestion → steady-state ramp.
    PASS: record sizes within [1200, 16384] with ±10% variance.
    """
```

---

# 14. Таблица изменяемых файлов

## Изменения в существующих файлах

| Файл | Что изменяется | PR |
|---|---|---|
| `td/mtproto/IStreamTransport.h` | +4 defaulted virtual (pre_flush_write, get_shaping_wakeup, set_traffic_hint, set_max_tls_record_size) | PR-3 |
| `td/mtproto/IStreamTransport.cpp` | `create_transport()`: ветка stealth (единственный activation if) | PR-3 |
| `td/mtproto/TcpTransport.h` | `ObfuscatedTransport`: overload + `set_max_tls_record_size` override | PR-3 |
| `td/mtproto/TcpTransport.cpp` | `do_write_tls`: использует `max_tls_packet_length_` (уже есть) | PR-3 |
| `td/mtproto/TlsInit.h` | `TlsHelloContext` расширен (padding_target, ech_length, pq_group_id) | PR-1 |
| `td/mtproto/TlsInit.cpp` | Новые `Type::EchPayload`, `Type::PqGroupId`, `Type::PqKeyShare`; удалить static `ech_payload()` | PR-1 |
| `td/mtproto/RawConnection.cpp` | `flush_write()`: вызов `pre_flush_write` | PR-7 |
| `td/mtproto/SessionConnection.cpp` | `start_auth()`, `do_loop()` wakeup scheduling, hint в `flush_packet()` | PR-7 |
| `td/mtproto/SessionConnection.h` | `+auth_packets_remaining_` | PR-7 |
| `td/mtproto/CMakeLists.txt` | `TDLIB_STEALTH_SHAPING` option + stealth/*.cpp sources | PR-3 |

## Новые файлы

```
td/mtproto/stealth/
  Interfaces.h           IRng, IClock, PaddingPolicy, TrafficHint, kPqGroupDraft/Final   PR-3
  TlsHelloProfile.h      enum BrowserProfile, pick_random_profile, pq_group_for_profile  PR-2
  TlsHelloProfiles.cpp   build_chrome131/120, firefox128, safari17  PR-2
  StealthConfig.h/cpp    StealthConfig, from_secret()               PR-3
  ShaperState.h/cpp      IptController + MarkovChain                PR-5
  ShaperRingBuffer.h/cpp bounded ring buffer                        PR-4
  DrsEngine.h/cpp        DRS + jitter                               PR-6
  StealthTransportDecorator.h/cpp                                   PR-4..7
  StealthParamsLoader.h/cpp JSON loader + hot-reload                PR-8

td/mtproto/test/stealth/
  MockRng.h              xoshiro256** ГПСЧ                          PR-A
  MockClock.h            ручное время                               PR-A
  RecordingTransport.h   fake IStreamTransport                      PR-A
  TestHelpers.h          утилиты                                    PR-A
  test_context_entropy.cpp   PR-1 coverage                          PR-1
  test_browser_profiles.cpp  PR-2 coverage (JA3, ALPS, 3DES, ECH)  PR-2
  test_decorator.cpp         PR-4 coverage (activation, hint, overflow, DRS idle) PR-4
  test_ipt_controller.cpp    PR-5 coverage (keepalive bypass, log-normal) PR-5
  test_drs_engine.cpp        PR-6 coverage (phases, jitter, idle-reset) PR-6
  test_session_wiring.cpp    PR-7 coverage (auth hint, keepalive hint) PR-7
  test_params_loader.cpp     PR-8 coverage (JSON, validation, fail-closed) PR-8
  test_stealth_disabled.cpp  TDLIB_STEALTH_SHAPING=OFF passthrough  ALL

tests/analysis/
  check_fingerprint.py   JA3, JA4, ECH type, GREASE, ALPS
  check_ipt.py           межпакетные интервалы
  check_drs.py           размеры TLS записей
  check_ech_variance.py  ECH payload per-connection variability
  check_keepalive.py     keepalive latency < 10ms

telemt-stealth-params/   (git submodule, общий с telemt)
  params.h               IptParams, DrsWeights с defaults
  profiles_validation.json  JA3/JA4 хеши для Chrome/Firefox/Safari
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
| PR-2 (ALPS 0x4469, 0xFE0D) | — | Сервер не парсит ALPS type / ECH extension type |
| PR-2 (profile weights) | PR-F | JA3/JA4 хеши в profiles_validation.json — **синхронизировать** |
| PR-3 (DRS kInitialRecordSize) | PR-C (DRS) | `DrsWeights` в shared submodule — **обязательно** |
| PR-5 (IptParams) | PR-G | `IptParams` в shared submodule — **обязательно** |
| PR-8 (JSON format) | telemt config | Единый формат JSON для обоих — **синхронизировать** |

**Shared submodule `telemt-stealth-params/`:** оба репозитория указывают на один и тот же коммит.
При изменении defaults в submodule — одновременно обновлять ссылки в обоих проектах.

---

# 17. Риск-регистр

| Риск | P | S | Митигация |
|---|---|---|---|
| ECH singleton → фиксированная длина per-process | Высокая | Критическая | PR-1: per-connection sampling в Context |
| PQ group не синхронизирован с profile registry snapshot | Высокая | Критическая | PR-1/PR-2: registry-driven `pq_group_for_profile()`, согласованность supported_groups и key_share |
| ECH type 0xFE02 → тривиальная детекция (при включенном ECH) | Высокая | Высокая | PR-2: только 0xFE0D |
| ECH declared encapsulated key length != фактическому количеству байт | Высокая | Критическая | PR-2: фикс wire-format + тест структурного парсинга |
| ECH включён в RU egress, где он блокируется | Высокая | Критическая | PR-8: route-policy (`ru_egress.ech_mode=disabled`) |
| QUIC/HTTP3 попытки в RU->non-RU маршрутах | Средняя | Высокая | PR-8: `allow_quic=false`, transport strategy TCP+TLS only |
| ALPS code 0x44CD устарел для Chrome 131 (должен быть 0x4469) | Высокая | Высокая | PR-2: обновить на 0x4469 |
| Отсутствует ALPS — Chrome JA3/JA4 не совпадает | Высокая | Высокая | PR-2: alps_block() в Chrome профилях |
| Static padding target 513 → ClientHello всегда 517 | Высокая | Высокая | PR-1: profile-driven PaddingPolicy + Context pre-sampling |
| 3DES в Safari/Firefox | Высокая | Высокая | PR-2: 3DES удалён |
| `kClientPartSize = 2878` — известная сигнатура | Высокая | Высокая | PR-3+PR-6: DRS с jitter |
| Keepalive задерживается → disconnect 28s | Средняя | Критическая | PR-5: TrafficHint::Keepalive bypass |
| Ring overflow → unmasked burst | Средняя | Высокая | PR-4: hard backpressure, без sync overflow write |
| Hint drift → Keepalive hint утекает | Средняя | Средняя | PR-4: consume-once semantics |
| DRS не сбрасывается на idle | Средняя | Средняя | PR-4: notify_idle при idle gap > 500ms |
| Auth-пакеты задерживаются IPT | Средняя | Средняя | PR-7: AuthHandshake hint |
| Механистичные record sizes без jitter | Средняя | Средняя | PR-6: ±10% jitter |
| ТСПУ обновляет ML-модели | Высокая | Высокая | PR-8: runtime JSON hot-reload |
| Merge-конфликт с upstream TlsInit | Высокая | Средняя | Только аддитивные изменения |
| Safari ECH отсутствует — видно в 2027 | Низкая | Средняя | Будущий SafariIos18 профиль (backlog) |
| Попытка «de-entropy padding» внутри TLS record | Средняя | Высокая | Запрещено: не менять payload ciphertext на transport-слое |
| ClientHello не фрагментирован по TCP — DPI парсит первый пакет целиком (S16) | Средняя | Средняя | Backlog: controlled ClientHello fragmentation |
| Server response pattern фиксирован — детектируем серверной стороной (S17) | Средняя | Средняя | Backlog: вариативность response (требуются изменения в telemt) |
| Single TCP connection per DC — не похоже на реальный HTTPS (S18) | Средняя | Средняя | Backlog: dummy connections / connection multiplexing |
| Один SNI domain для всех connections — статический фингерпринт (S19) | Средняя | Низкая | Backlog: domain rotation (требует изменений в конфигурации proxy) |
| Browser PQ profile дрейфует между релизами | Высокая | Средняя | PR-2/PR-8: обновление profile registry из capture и hot-reload |

---

# 18. Критерии готовности к релизу

1. **Все тесты PR-A..PR-8 зелёные**, включая:
  - `test_ipt_controller: KeepaliveBypassesDelayInBurstState` ← критический
  - `test_browser_profiles: Chrome131EchTypeIs0xFE0D` ← критический (для non-RU режима)
  - `test_browser_profiles: RuRouteDisablesEch` ← критический (для RU режима)
  - `test_browser_profiles: Chrome131EchInnerPrefixIs5Bytes` ← критический (НОВОЕ)
  - `test_browser_profiles: Chrome131PqGroupMatchesProfileRegistry` ← критический (НОВОЕ)
  - `test_browser_profiles: AllProfilesGreaseIsValidRfc8701` ← критический
  - `test_browser_profiles: Chrome131HasAlpsExtension` ← критический (проверяет 0x4469, не 0x44CD)
  - `test_context_entropy: NoForcedPadding517Regression` ← критический
  - `test_context_entropy: PqGroupAppearsInBothGroupsAndKeyShare` ← критический (НОВОЕ)
  - `test_drs_engine: RecordSizeNotFixedTo2878Signature` ← критический

2. **Сборка с `TDLIB_STEALTH_SHAPING=OFF`** проходит все upstream tdlib тесты bit-for-bit.

3. **Smoke tests** против `stealth-drs-ipt` ветки telemt (локально):
  - `check_fingerprint.py`: JA3 не совпадает с `e0e58235789a753608b12649376e91ec`
  - `check_fingerprint.py` (RU mode): ECH отсутствует, 0xFE02 отсутствует
  - `check_fingerprint.py` (non-RU mode): 0xFE0D присутствует, 0xFE02 отсутствует
  - `check_fingerprint.py`: ECH outer структура валидна (enc_key_len == фактической длине)
  - `check_fingerprint.py`: ALPS (0x4469) присутствует в Chrome ClientHello, старый 0x44CD отсутствует
  - `check_fingerprint.py`: PQ group в `supported_groups` и `key_share` совпадает с profile registry
  - `check_ech_variance.py` (non-RU mode): ≥3 разных длины ECH за 50 соединений
  - `check_drs.py`: dominant-mode не равен 2878; record size варьируется ≥3 значениями
  - `check_ipt.py` / `check_drs.py`: отклонение от baseline в `docs/Samples/Traffic dumps/*.pcap*` не превышает пороги
  - `check_keepalive.py`: keepalive задержка < 10ms в 100% случаев

4. **Shared submodule** тегирован и одновременно обновлён в tdlib-obf И telemt.

5. **Верификация от резидента** (не VPS): один контрибьютор запускает smoke tests с российского ISP.

6. **`CHANGELOG-obf.md`** документирует: активируется только при `0xee`-секрете, список исправленных сигнатур, известные остаточные риски.

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
[ ] Hint consume-once: ВСЕГДА reset to Interactive после write()
[ ] PQ group: ВСЕГДА из pq_group_for_profile(), синхронно в supported_groups И key_share
[ ] PQ group: НЕТ hardcoded "Chrome131 == 0x6399"; только profile-registry/capture-driven
[ ] GREASE: Grease::init() для GREASE-слотов — уже корректен
[ ] ALPS: Chrome131 → 0x4469 (application_settings), НЕ 0x44CD (alps-01 draft)
[ ] ECH: для RU egress disabled; для non-RU только 0xFE0D, никогда 0xFE02
[ ] ECH outer: declared encapsulated key length совпадает с фактической длиной
[ ] QUIC policy: disabled в этом дизайне (TCP+TLS only)
[ ] Activation: ЕДИНСТВЕННЫЙ if (secret.emulate_tls()) в create_transport()

Запрещено:
[ ] Нет passthrough overflow (никакого sync write в bypass ring)
[ ] Нет payload-level tampering (zero/ASCII padding внутри ciphertext)
[ ] Нет TLS-in-TLS
[ ] Нет singleton для per-connection entropy
[ ] Нет статических padding target
[ ] Нет рассинхронизации PQ codepoint между supported_groups и key_share
```
