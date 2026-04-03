# tdlib-obf Stealth Plan — Полностью самостоятельный документ

**Дата:** 2026-04-03  
**Репозиторий:** `tdlib-obf` (форк TDLib, директория `td/mtproto/`)  
**Компаньон:** telemt (Rust MTProxy сервер, `IMPLEMENTATION_PLAN.md`)  
**Статус угрозы:** ТСПУ бюджет ₽84 млрд, ML-классификация TLS-фингерпринтов активна.  
**Главное правило:** вся маскировка активна **строго только** при
`ProxySecret::emulate_tls() == true` (секрет с префиксом `ee`).

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
| S3 | 🔴 CRITICAL | Supported Groups + Key Share | `\x11\xec` (0x11EC = 4588) — это IANA codepoint **X25519MLKEM768** (финальный ML-KEM стандарт). Chrome 131 (Q1 2025) использует **0x6399** (X25519Kyber768Draft00, draft). Несоответствие codepoint'а → TLS-парсер видит группу, которую Chrome данной версии НЕ отправляет. Значение присутствует в **двух** местах: supported_groups И key_share — оба нужно менять синхронно. | Исправить |
| S4 | 🔴 CRITICAL | ECH extension | Тип `0xFE02` (65026) вместо `0xFE0D` (65037, IANA draft-ietf-tls-esni-17) | Исправить |
| S5 | 🟠 HIGH | `TcpTransport` | `MAX_TLS_PACKET_LENGTH = 2878` — статическая константа, известная сигнатура | Исправить |
| S6 | 🟠 HIGH | TlsHello | Единственный статический singleton TlsHello для всех соединений | Исправить |
| S7 | 🟠 HIGH | Chrome профиль | ALPS extension присутствует, но с **устаревшим** codepoint: `0x44CD` (17613, alps-01 draft) вместо `0x4469` (17513, application_settings, Chrome 115+). Несовпадение codepoint'а → JA4 не совпадает с реальным Chrome 131. | Исправить |
| S8 | 🟠 HIGH | ECH inner | 5-байтный header `\x00\x00\x01\x00\x01` вместо 4-байтного `\x00\x01\x00\x01` | Исправить |
| S9 | 🟠 HIGH | Darwin (`#if TD_DARWIN`) профиль | 3DES suite (0xC012, 0xC008, 0x000A) — удалены Apple в iOS 15 / macOS Monterey (2021). Присутствуют **только** в Darwin-ветке кода, не в основном Chrome-профиле. | Исправить |
| S10 | 🟠 HIGH | Firefox профиль (будущий) | При создании Firefox-профиля: НЕ включать 3DES (0x000A) — Firefox удалил в 2021. Текущий код не имеет Firefox-профиля вообще. | Исправить |
| S11 | 🟠 HIGH | Darwin | `#if TD_DARWIN` использует специальный TLS-профиль эпохи 1.2 — тривиально детектируем | Исправить |
| S12 | 🟠 HIGH | IPT | Равномерное распределение межпакетных интервалов (нет jitter) | Исправить |
| S13 | 🟡 MEDIUM | DRS | Фиксированные record size (1380/4096/16384) без ±jitter — механистично | Исправить |
| S14 | 🟡 MEDIUM | Keepalive | MTProto ping задерживается IPT-шейпером → disconnect при 28с таймауте | Исправить |
| S15 | 🟡 MEDIUM | Session | Hint для первых auth-пакетов не выставляется → лишние задержки при handshake | Исправить |
| S16 | 🟡 MEDIUM | ClientHello | Отсутствует фрагментация ClientHello по нескольким TCP-сегментам. DPI часто инспектирует только первый TCP payload — фрагментация сломает single-packet парсинг (xray-core реализует это как `FragmentMask`). | Backlog |
| S17 | 🟡 MEDIUM | TLS Response | Server response pattern (`\x16\x03\x03` + CCS + Application Data) фиксирован и уникален для MTProto-proxy — потенциально детектируем на стороне сервера. | Backlog |
| S18 | 🟡 MEDIUM | Connection | Один TCP connection per DC — необычно для реального HTTPS, где браузер открывает 6+ параллельных соединений. Поведенческий фингерпринт на уровне потока. | Backlog |
| S19 | 🟡 MEDIUM | SNI | Один и тот же SNI domain для всех соединений данного пользователя. Реальный браузер обращается к десяткам доменов. | Backlog |

## 1.3 Что уже хорошо (не трогать)

- `MlKem768Key` (1184 байт) — правильная структура ML-KEM-768 публичного ключа (384 пары NTT-коэффициентов ∈ [0, 3329) + 32 random bytes). **NО**: codepoint группы (0x11EC vs 0x6399) нуждается в обновлении (см. S3)
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

---

# 2. Архитектура

## 2.1 Принципы

| Принцип | Применение |
|---|---|
| **Strict Activation Gate** | Единственный `if (secret.emulate_tls())` в `create_transport()`. Нигде больше нет `if (is_stealth)` в горячем коде. |
| **Decorator** | `StealthTransportDecorator` реализует `IStreamTransport`, держит inner. Вся логика маскировки здесь. |
| **Factory** | `create_transport()` — единственная точка принятия решения о враппинге. |
| **Pre-sampled Context** | Все случайные длины (padding, ECH, record jitter) вычисляются **один раз** в `TlsHelloContext` при его создании. CalcLength и Store только читают. |
| **Consume-once Hint** | `TrafficHint` потребляется один раз в `write()`, авто-сбрасывается в `Interactive`. Предотвращает hint-drift. |
| **Bounded Ring + Sync Overflow** | При переполнении ring — DRS-record-size применяется к overflow-write. Нет unmasked passthrough. |
| **Hot Path: Zero Alloc** | После init нет аллокаций на пакет. Нет `dynamic_cast`. Все через virtual. |
| **TDD: Red First** | Каждый PR начинается с красных тестов, которые падают на текущем коде по правильной причине. |
| **TDLIB_STEALTH_SHAPING=OFF** | Compile-time feature flag. При OFF — все upstream тесты проходят bit-for-bit. |

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
    │       └─ if ring full: apply DRS size → sync write (no unmasked passthrough)
    │
StealthTransportDecorator::pre_flush_write(now)
    │
    ├─ [5] Detect idle gap → DrsEngine::notify_idle() if gap > 500ms
    ├─ [6] DrsEngine::next_record_size(hint) + ±jitter
    ├─ [7] inner_->set_max_tls_record_size(jittered_size)
    └─ [8] ring_.drain_ready(now, write_to_inner)

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
  int32 current_max_record_size{2878};

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
**Исправляет:** S1 (static padding), S2 (ECH singleton), S3 (invalid GREASE in groups)

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

### S3: Невалидный GREASE в supported_groups

```cpp
// ТЕКУЩИЙ КОД (баг):
Op::str("\x11\xec"_q)  // 0x11EC = 4588 — НЕ является GREASE-значением
// RFC 8701: GREASE значения = { 0x0A0A, 0x1A1A, 0x2A2A, ..., 0xFAFA }
// 0x11EC не в этом множестве → любой TLS-парсер видит невалидный тип
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

```cpp
// td/mtproto/TlsInit.h — изменения в существующем классе

class TlsHelloContext {
 public:
  // Expanded constructor: all random lengths pre-sampled exactly once.
  // CalcLength and Store must read from context, never sample independently.
  TlsHelloContext(size_t grease_size,
                  string domain,
                  size_t padding_target,   // pre-sampled from PaddingRange
                  size_t ech_length,       // pre-sampled: 144 + n*32, n in [0,3]
                  uint16_t groups_grease)  // pre-sampled valid GREASE for supported_groups
      : grease_(grease_size, '\0'),
        domain_(std::move(domain)),
        padding_target_(padding_target),
        ech_length_(ech_length),
        groups_grease_(groups_grease) {
    Grease::init(grease_);
  }

  // Existing accessors preserved:
  char get_grease(size_t i) const;
  size_t get_grease_size() const;
  Slice get_domain() const;

  // New accessors:
  size_t get_padding_target() const noexcept { return padding_target_; }
  size_t get_ech_length() const noexcept { return ech_length_; }

  // Provides a valid RFC 8701 GREASE value for the supported_groups extension.
  // This replaces the static \x11\xec placeholder that is not a valid GREASE value.
  uint16_t get_groups_grease() const noexcept { return groups_grease_; }

 private:
  string grease_;
  string domain_;
  size_t padding_target_;
  size_t ech_length_;
  uint16_t groups_grease_;
};
```

## 5.3 Новый Op::Type::EchPayload + Op::Type::GroupsGrease

```cpp
// В TlsHello::Op::Type enum добавить:
EchPayload,   // per-connection ECH length из context
GroupsGrease, // per-connection valid GREASE из context

// Новые factory методы (заменяют static ech_payload()):
static Op ech_payload_dynamic() {
  Op res;
  res.type = Type::EchPayload;
  return res;
}

static Op groups_grease() {
  Op res;
  res.type = Type::GroupsGrease;
  return res;
}

// УДАЛИТЬ:
// static Op ech_payload() { ... Random::fast(0,3)*32+144 ... }
```

## 5.4 Функция-helper для валидного GREASE

```cpp
// td/mtproto/stealth/Interfaces.h

// Returns a random valid RFC 8701 GREASE value.
// GREASE values are { 0x0A0A, 0x1A1A, ..., 0xFAFA } — 16 values total.
// Invariant: the returned value is always from the canonical GREASE set.
inline uint16_t sample_grease_value(IRng &rng) noexcept {
  // Each GREASE value = (n << 8 | n) where n = 0x0A + 0x10*k, k in [0,15]
  // Equivalently: pick k in [0,15], then value = (0x0A + 0x10*k) * 0x0101
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

case Type::GroupsGrease:
  CHECK(context);
  size_ += 2;  // uint16_t GREASE value
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

case Type::GroupsGrease: {
  CHECK(context);
  uint16_t gv = context->get_groups_grease();
  dest_[0] = static_cast<char>((gv >> 8) & 0xFF);
  dest_[1] = static_cast<char>(gv & 0xFF);
  dest_.remove_prefix(2);
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
                        const stealth::PaddingRange &padding_range,
                        stealth::IRng &rng,
                        stealth::BrowserProfile profile);

// Старая перегрузка: делегирует с {513,513}, GlobalRng, Chrome131 — backward compat.
```

## 5.7 TDD (красные тесты ДО кода)

```cpp
// td/mtproto/test/stealth/test_context_entropy.cpp

TEST(ContextEntropy, PaddingAndEchSampledOnce) {
  MockRng rng(42);
  PaddingRange range{492, 540};
  size_t p = range.sample(rng);
  size_t e = rng.bounded(4) * 32 + 144;
  uint16_t gg = sample_grease_value(rng);
  TlsHelloContext ctx(7, "google.com", p, e, gg);
  EXPECT_EQ(ctx.get_padding_target(), p);
  EXPECT_EQ(ctx.get_ech_length(), e);
  EXPECT_EQ(ctx.get_groups_grease(), gg);
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
    PaddingRange range{492, 540};
    TlsHelloContext ctx(7, "google.com",
                        range.sample(rng),
                        rng.bounded(4) * 32 + 144,
                        sample_grease_value(rng));
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

TEST(ContextEntropy, GroupsGreaseIsValidRfc8701) {
  MockRng rng(99);
  for (int i = 0; i < 1000; i++) {
    uint16_t gv = sample_grease_value(rng);
    // Valid GREASE values: lower byte == upper byte, and each byte is 0x0A+0x10*k.
    uint8_t lo = gv & 0xFF;
    uint8_t hi = (gv >> 8) & 0xFF;
    EXPECT_EQ(lo, hi) << "GREASE byte mismatch at i=" << i;
    EXPECT_EQ((lo - 0x0A) % 0x10, 0u) << "Not a valid GREASE byte at i=" << i;
  }
}

TEST(ContextEntropy, NoPaddingTarget517Regression) {
  MockRng rng(7);
  PaddingRange range{492, 540};
  for (int i = 0; i < 200; i++) {
    auto h = generate_header("google.com", test_secret, unix_now,
                              range, rng, BrowserProfile::Chrome131);
    // The old static padding produced exactly 517-byte ClientHello.
    EXPECT_NE(h.size(), 517u) << "Static padding regression at i=" << i;
  }
}
```

---

# 6. PR-2: Browser Profile Registry

**Зависит от:** PR-1  
**Исправляет:** S4 (ECH type 0xFE02→0xFE0D), S7 (ALPS missing), S8 (ECH inner structure), S9 (Safari 3DES), S10 (Firefox 3DES), S11 (Darwin special profile)

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

// Selects a profile based on Russia/CIS Q1 2026 device distribution.
// Called once per connection; uses pre-seeded rng from StealthConfig.
BrowserProfile pick_random_profile(IRng &rng);

const TlsHello &get_hello_for_profile(BrowserProfile profile);

// Returns the appropriate PaddingRange for a profile.
// Safari: {0,0} — no padding extension.
PaddingRange padding_range_for_profile(BrowserProfile profile);

}  // namespace td::mtproto::stealth
```

## 6.3 Веса профилей

```cpp
BrowserProfile pick_random_profile(IRng &rng) {
  // Russia/CIS Q1 2026 (StatCounter + Telegram mobile breakdown):
  // Chrome 131 (Desktop + Android WebView): 48%
  // Chrome 120 (older Android, corporate):  17%
  // Safari iOS 17 (iPhone users):           20%
  // Firefox 128:                             8%
  // Remainder → Chrome 131
  auto roll = rng.bounded(100);
  if (roll < 48) return BrowserProfile::Chrome131;
  if (roll < 65) return BrowserProfile::Chrome120;
  if (roll < 85) return BrowserProfile::SafariIos17;
  return BrowserProfile::Firefox128;
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

## 6.4 Chrome 131 профиль (полный, с 0xFE0D и ALPS)

Ключевые изменения относительно текущего кода:

```cpp
static TlsHello build_chrome131_hello() {
  TlsHello res;
  res.grease_size_ = 7;

  // ECH (encrypted_client_hello) extension block.
  // Type 0xFE0D = 65037 (IANA draft-ietf-tls-esni-17, replaces 0xFE02).
  // Inner structure: 4-byte header (not 5-byte as in old code), K() in scope.
  auto ech_block = [] {
    return Op::permutation_element({
      Op::str("\xfe\x0d"_q),       // type: 0xFE0D (encrypted_client_hello)
      Op::begin_scope(),
      Op::str("\x00\x01\x00\x01"_q), // 4-byte header (was \x00\x00\x01\x00\x01)
      Op::random(1),
      Op::begin_scope(),
      Op::key(),                   // MlKem768Key (1184 bytes, X25519Kyber768Draft00)
      Op::end_scope(),
      Op::begin_scope(),
      Op::extension(),             // extensions
      Op::end_scope(),
      Op::end_scope(),
    });
  };

  // ALPS extension block.
  // Application Layer Protocol Settings (type 0x4469 = 17513).
  // Chrome 91+ includes this for h2 ALPN; absence is a detectable fingerprint.
  auto alps_block = [] {
    return Op::permutation_element({
      Op::str("\x44\x69"_q),       // type: ALPS (0x4469)
      Op::begin_scope(),
      Op::str("\x00\x03\x02\x68\x32"_q),  // length=3, protocol="h2"
      Op::end_scope(),
    });
  };

  // supported_groups extension with valid GREASE (not static \x11\xec).
  auto groups_block = [] {
    return Op::permutation_element({
      Op::str("\x00\x0a"_q),       // type: supported_groups
      Op::begin_scope(),
      Op::begin_scope(),
      Op::groups_grease(),         // valid RFC 8701 GREASE from context
      Op::str(                     // Chrome 131 groups: X25519Kyber768, X25519, P256, P384
        "\x63\x99"                 // X25519Kyber768Draft00 (25497)
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
      // key_share (type 0x0033) — with MlKem768Key:
      Op::str("\x00\x33"_q),
      Op::begin_scope(),
      Op::begin_scope(),
      Op::grease(2),               // GREASE key share
      Op::str("\x00\x01\x00"_q),
      Op::key(),                   // X25519Kyber768Draft00 key
      Op::end_scope(),
      Op::end_scope(),
      // psk_key_exchange_modes (type 0x002D): psk_dhe_ke:
      Op::str("\x00\x2d\x00\x02\x01\x01"_q),
      // supported_versions (type 0x002B): TLS 1.3, TLS 1.2:
      Op::str("\x00\x2b\x00\x05\x04\x03\x04\x03\x03"_q),
      // compress_certificate (type 0x001B): brotli:
      Op::str("\x00\x1b\x00\x03\x02\x00\x02"_q),
      // ALPS (type 0x4469, Application Layer Protocol Settings) — НОВОЕ:
      alps_block(),
      // ECH (type 0xFE0D) — ИСПРАВЛЕНО с 0xFE02:
      ech_block(),
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
      // supported_groups with valid GREASE:
      Op::str("\x00\x0a\x00\x08\x00\x06"_q),
      Op::begin_scope(),
      Op::groups_grease(),            // valid GREASE (not \x11\xec)
      Op::str("\x00\x1d\x00\x17\x00\x18"_q),  // X25519, P-256, P-384
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
  auto h = generate_header_test(BrowserProfile::Chrome131);
  EXPECT_TRUE(has_extension(h, 0xFE0D)) << "ECH must use 0xFE0D (IANA correct type)";
  EXPECT_FALSE(has_extension(h, 0xFE02)) << "Old ECH type 0xFE02 must be absent";
}

TEST(ProfileTest, Chrome131HasAlpsExtension) {
  auto h = generate_header_test(BrowserProfile::Chrome131);
  EXPECT_TRUE(has_extension(h, 0x4469)) << "ALPS extension must be present in Chrome131";
}

TEST(ProfileTest, Chrome131EchInnerHeaderIs4Bytes) {
  auto h = generate_header_test(BrowserProfile::Chrome131);
  auto body = extract_extension_body(h, 0xFE0D);
  ASSERT_GE(body.size(), 4u);
  // First 4 bytes: \x00\x01\x00\x01 (not old 5-byte \x00\x00\x01\x00\x01)
  EXPECT_EQ(static_cast<uint8_t>(body[0]), 0x00u);
  EXPECT_EQ(static_cast<uint8_t>(body[1]), 0x01u);
  EXPECT_EQ(static_cast<uint8_t>(body[2]), 0x00u);
  EXPECT_EQ(static_cast<uint8_t>(body[3]), 0x01u);
}

TEST(ProfileTest, AllProfilesHaveValidGreaseInSupportedGroups) {
  for (auto profile : {BrowserProfile::Chrome131, BrowserProfile::Chrome120,
                        BrowserProfile::Firefox128, BrowserProfile::SafariIos17}) {
    MockRng rng(42);
    for (int i = 0; i < 50; i++) {
      auto h = generate_header_test(profile, rng);
      auto groups = extract_supported_groups(h);
      ASSERT_FALSE(groups.empty()) << "Profile " << (int)profile << " has no groups";
      uint16_t first_group = groups[0];
      // If first group is GREASE, it must be a valid RFC 8701 value.
      bool is_grease = ((first_group & 0x0F0F) == 0x0A0A) ||
                       ((first_group >> 8) == (first_group & 0xFF) &&
                        ((first_group & 0xFF) - 0x0A) % 0x10 == 0);
      if (is_grease) {
        EXPECT_NE(first_group, 0x11ECu) << "Invalid GREASE 0x11EC detected at i=" << i;
      }
    }
  }
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
  static constexpr int32 kDefaultMaxTlsPacketLength = 2878;
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
  PaddingRange padding_range;
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
**Реализует:** Activation Gate, consume-once hint, ring buffer с DRS-safe overflow

## 8.1 Заголовочный файл

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

  // Hint is consumed-once: set by set_traffic_hint(), read and reset in write().
  // Default: Interactive. Auto-resets after each write() to prevent hint-drift.
  TrafficHint pending_hint_{TrafficHint::Interactive};

  static constexpr double kDrsIdleThresholdSec = 0.5;

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
  PendingWrite pw{std::move(message), quick_ack, send_at};

  if (ring_.try_enqueue(std::move(pw))) {
    return;
  }

  // Ring is full (burst condition: large media transfer).
  // Apply DRS record size before the overflow write so the TLS record is
  // correctly sized even without passing through the ring buffer.
  // This preserves the DRS invariant under burst: the censor sees correctly
  // sized TLS records, not the raw 2878-byte default.
  int32 rec_size = drs_.next_record_size(hint);
  inner_->set_max_tls_record_size(rec_size);
  inner_->write(std::move(pw.message), pw.quick_ack);
  drs_.notify_bytes_written(pw.message.size());
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

  const int32 rec_size = drs_.next_record_size(pending_hint_);
  inner_->set_max_tls_record_size(rec_size);

  ring_.drain_ready(now, [this, now](PendingWrite &&pw) {
    inner_->write(std::move(pw.message), pw.quick_ack);
    drs_.notify_bytes_written(pw.message.size());
    last_write_time_ = now;
  });
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

TEST(DecoratorOverflow, OverflowAppliesDrsRecordSize) {
  auto [dec, inner_ptr] = make_test_decorator(/*ring_capacity=*/2);
  dec->write(make_test_buffer(100), false);  // fills ring[0]
  dec->write(make_test_buffer(100), false);  // fills ring[1]
  dec->write(make_test_buffer(100), false);  // overflow: must call set_max_tls_record_size
  EXPECT_FALSE(inner_ptr->set_max_tls_record_size_calls.empty());
  EXPECT_EQ(inner_ptr->set_max_tls_record_size_calls.back(),
            kDrsSlowStartSize);  // SlowStart phase
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
      u = std::max(u, 1e-9);  // Avoid ln(0).
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

TEST(DrsEngine, StaticSize2878NeverAppearsAfterDrs) {
  MockRng rng(42);
  DrsWeights w;
  DrsEngine drs(w, rng);
  for (int i = 0; i < 500; i++) {
    int32 sz = drs.next_record_size(TrafficHint::Interactive);
    drs.notify_bytes_written(sz);
    EXPECT_NE(sz, 2878) << "Known Telegram signature size 2878 must never appear";
  }
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
    "remainder": "Chrome131"
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
  mutable std::shared_mutex mu_;        // readers: shared_lock; writer: unique_lock
  shared_ptr<StealthParamsOverride> current_;

  // Validates all numeric fields are within safe ranges.
  // Returns false if any field is out-of-range or missing.
  static bool validate(const StealthParamsOverride &params) noexcept;
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

  // Profile weights must sum to 100 ± 1.
  int sum = p.weights.chrome131 + p.weights.chrome120 +
            p.weights.safari17 + p.weights.firefox128;
  if (std::abs(sum - 100) > 1) return false;

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

Usage: python check_fingerprint.py --interface lo --port 8888
"""

KNOWN_TELEGRAM_JA3 = {
    "e0e58235789a753608b12649376e91ec",  # Original Telegram client
}

INVALID_GREASE = {0x11EC}  # Must never appear in supported_groups

def check_ech_type(ch: ClientHello) -> bool:
    """ECH extension must be 0xFE0D, not 0xFE02."""
    for ext in ch.extensions:
        if ext.type == 0xFE02:
            return False  # FAIL: old Telegram ECH type
    return True

def check_grease_validity(ch: ClientHello) -> bool:
    """All GREASE values must be from {0x0A0A, 0x1A1A, ..., 0xFAFA}."""
    for group in ch.supported_groups:
        if is_grease_candidate(group) and group in INVALID_GREASE:
            return False
    return True

def check_alps_present(ch: ClientHello) -> bool:
    """Chrome profiles must include ALPS extension (0x4469)."""
    profile = detect_profile(ch)
    if profile in ('Chrome131', 'Chrome120'):
        return any(ext.type == 0x4469 for ext in ch.extensions)
    return True  # Safari/Firefox don't have ALPS

CHECKS = [
    ("JA3 not Telegram", lambda ch: compute_ja3(ch) not in KNOWN_TELEGRAM_JA3),
    ("ECH type 0xFE0D", check_ech_type),
    ("Valid GREASE in groups", check_grease_validity),
    ("ALPS present for Chrome", check_alps_present),
    ("No 3DES in Safari", lambda ch: 0x000A not in ch.cipher_suites if is_safari(ch) else True),
    ("Padding not exactly 517 bytes", lambda ch: len(ch.raw) != 517),
    ("ECH payload varies", None),  # Checked across multiple hellos
]
```

## 13.2 `check_ipt.py` — межпакетные интервалы

```python
def check_ipt(pcap_file: str) -> bool:
    """
    K-S test of inter-packet intervals against log-normal distribution.
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
    FAIL: any record exactly 2878 bytes (known Telegram size).
    FAIL: ≥10 consecutive records of identical size (no jitter).
    FAIL: no records > 8192 after first 100KB (DRS not progressing).
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
| `td/mtproto/TlsInit.h` | `TlsHelloContext` расширен (padding_target, ech_length, groups_grease) | PR-1 |
| `td/mtproto/TlsInit.cpp` | Новые `Type::EchPayload`, `Type::GroupsGrease`; удалить static `ech_payload()` | PR-1 |
| `td/mtproto/RawConnection.cpp` | `flush_write()`: вызов `pre_flush_write` | PR-7 |
| `td/mtproto/SessionConnection.cpp` | `start_auth()`, `do_loop()` wakeup scheduling, hint в `flush_packet()` | PR-7 |
| `td/mtproto/SessionConnection.h` | `+auth_packets_remaining_` | PR-7 |
| `td/mtproto/CMakeLists.txt` | `TDLIB_STEALTH_SHAPING` option + stealth/*.cpp sources | PR-3 |

## Новые файлы

```
td/mtproto/stealth/
  Interfaces.h           IRng, IClock, PaddingRange, TrafficHint   PR-3
  TlsHelloProfile.h      enum BrowserProfile, pick_random_profile   PR-2
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
| 5.3.2 | Input validation | ✅ | PaddingRange::create() + JSON validation |
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
| PR-2 (ALPS, 0xFE0D) | — | Сервер не парсит ECH extension type |
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
| `\x11\xec` (0x11EC) — невалидный GREASE детектируется | Высокая | Критическая | PR-1: sample_grease_value() + GroupsGrease op |
| ECH type 0xFE02 → тривиальная детекция | Высокая | Высокая | PR-2: 0xFE0D во всех Chrome профилях |
| Отсутствует ALPS — Chrome JA3/JA4 не совпадает | Высокая | Высокая | PR-2: alps_block() в Chrome профилях |
| Static padding target 513 → ClientHello всегда 517 | Высокая | Высокая | PR-1: PaddingRange + Context pre-sampling |
| 3DES в Safari/Firefox | Высокая | Высокая | PR-2: 3DES удалён |
| `kClientPartSize = 2878` — известная сигнатура | Высокая | Высокая | PR-3+PR-6: DRS с jitter |
| Keepalive задерживается → disconnect 28s | Средняя | Критическая | PR-5: TrafficHint::Keepalive bypass |
| Ring overflow → unmasked burst | Средняя | Высокая | PR-4: DRS apply при overflow |
| Hint drift → Keepalive hint утекает | Средняя | Средняя | PR-4: consume-once semantics |
| DRS не сбрасывается на idle | Средняя | Средняя | PR-4: notify_idle при idle gap > 500ms |
| Auth-пакеты задерживаются IPT | Средняя | Средняя | PR-7: AuthHandshake hint |
| Механистичные record sizes без jitter | Средняя | Средняя | PR-6: ±10% jitter |
| ТСПУ обновляет ML-модели | Высокая | Высокая | PR-8: runtime JSON hot-reload |
| Merge-конфликт с upstream TlsInit | Высокая | Средняя | Только аддитивные изменения |
| Safari ECH отсутствует — видно в 2027 | Низкая | Средняя | Будущий SafariIos18 профиль (backlog) |
| Backpressure через can_write() | Средняя | Средняя | Backlog PR-10 |

---

# 18. Критерии готовности к релизу

1. **Все тесты PR-A..PR-8 зелёные**, включая:
   - `test_ipt_controller: KeepaliveBypassesDelayInBurstState` ← критический
   - `test_browser_profiles: Chrome131EchTypeIs0xFE0D` ← критический
   - `test_browser_profiles: AllProfilesHaveValidGreaseInSupportedGroups` ← критический
   - `test_browser_profiles: Chrome131HasAlpsExtension` ← критический
   - `test_context_entropy: NoPaddingTarget517Regression` ← критический
   - `test_drs_engine: StaticSize2878NeverAppearsAfterDrs` ← критический

2. **Сборка с `TDLIB_STEALTH_SHAPING=OFF`** проходит все upstream tdlib тесты bit-for-bit.

3. **Smoke tests** против `stealth-drs-ipt` ветки telemt (локально):
   - `check_fingerprint.py`: JA3 не совпадает с `e0e58235789a753608b12649376e91ec`
   - `check_fingerprint.py`: 0xFE0D во всех Chrome ClientHello, 0xFE02 отсутствует
   - `check_fingerprint.py`: ALPS (0x4469) присутствует в Chrome ClientHello
   - `check_fingerprint.py`: ни один supported_groups не содержит 0x11EC
   - `check_ech_variance.py`: ≥3 разных длины ECH за 50 соединений
   - `check_drs.py`: ни одного record = 2878; record size варьируется ≥3 значениями
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
[ ] При ring overflow: ВСЕГДА применять DRS record size перед write
[ ] Hint consume-once: ВСЕГДА reset to Interactive после write()
[ ] GREASE: ВСЕГДА из sample_grease_value(), никогда \x11\xec
[ ] ECH type: ВСЕГДА 0xFE0D, никогда 0xFE02
[ ] Activation: ЕДИНСТВЕННЫЙ if (secret.emulate_tls()) в create_transport()

Запрещено:
[ ] Нет passthrough overflow без apply DRS size
[ ] Нет TLS-in-TLS
[ ] Нет singleton для per-connection entropy
[ ] Нет статических padding target
```
