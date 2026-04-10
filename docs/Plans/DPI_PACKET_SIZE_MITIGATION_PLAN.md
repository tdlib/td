<!--
SPDX-FileCopyrightText: Copyright 2026 telemt community
SPDX-License-Identifier: MIT
telemt: https://github.com/telemt
telemt: https://t.me/telemtrs
-->

# DPI Packet-Size Analysis Mitigation Plan

**Date:** 2026-04-09 (v2 — capture-driven revision)
**Repository:** `tdlib-obf` (fork of TDLib, `td/mtproto/`)
**Threat model:** TSPU with ₽84B RUB budget, ML-classification active, ECH blocked in RU, QUIC (RU→non-RU) blocked.
**Scope:** TLS Application Data record size distributions after TLS handshake completes.
**Approach:** TDD — test suite first, fix code second. All tests in separate files.
**Core principle:** **Capture-driven parameters only.** All size distributions, phase models, and greeting templates are derived from empirical analysis of 41 real browser pcap captures under `docs/Samples/Traffic dumps/`. No synthetic generation (random walks, Markov chains, uniform/Gaussian sampling). Validated by chi-squared and Kolmogorov-Smirnov statistical tests.
**Prerequisite:** The TLS ClientHello masking (profiles, JA3/JA4, extensions, DRS, IPT) is fully implemented per `docs/Plans/tdlib-obf-stealth-plan_v6.md`. This plan addresses the **post-handshake traffic analysis** attack surface.

---

## Table of Contents

1. [Threat Analysis: What the DPI Sees](#1-threat-analysis-what-the-dpi-sees)
2. [Current State & Gaps](#2-current-state--gaps)
3. [Attack Vectors](#3-attack-vectors)
4. [Mitigation Architecture](#4-mitigation-architecture)
5. [Implementation Priority (PRs)](#5-implementation-priority-prs)
6. [PR-S0: Capture-Driven Baseline Extraction & Statistical Quality Gates](#6-pr-s0-capture-driven-baseline-extraction--statistical-quality-gates)
7. [PR-S1: MTProto Crypto Bucket Elimination](#7-pr-s1-mtproto-crypto-bucket-elimination)
8. [PR-S2: TLS Record Pad-to-Target (Capture-Driven)](#8-pr-s2-tls-record-pad-to-target-capture-driven)
9. [PR-S3: Post-Handshake Greeting Camouflage](#9-pr-s3-post-handshake-greeting-camouflage)
10. [PR-S4: Bidirectional Size Correlation Defense](#10-pr-s4-bidirectional-size-correlation-defense)
11. [PR-S5: Idle Connection Chaff Traffic](#11-pr-s5-idle-connection-chaff-traffic)
12. [PR-S6: Inter-Record Size Pattern Hardening](#12-pr-s6-inter-record-size-pattern-hardening)
13. [PR-S7: Stealth Connection Count Cap](#13-pr-s7-stealth-connection-count-cap)
14. [Test Strategy](#14-test-strategy)
15. [Residual Risks & Non-Goals](#15-residual-risks--non-goals)
16. [Success Criteria](#16-success-criteria)

---

## 1. Threat Analysis: What the DPI Sees

### 1.1. Wire-Level Observable Information

After the TLS handshake, the TSPU DPI box on the network path observes **only**:

| Observable | What it reveals | Precision |
|---|---|---|
| TLS record lengths (Application Data `0x17`) | Exact payload size per record | Byte-exact (2-byte length field in TLS header) |
| TLS record count per TCP segment | Batching behavior | Exact |
| TCP segment sizes | MSS, Nagle, delayed-ACK hints | Byte-exact |
| Direction (C→S vs S→C) | Request-response correlation | Exact |
| Inter-packet timing | Application behavior patterns | ~microsecond |
| Connection duration | Session lifecycle | Exact |
| Total bytes per direction | Bandwidth asymmetry ratio | Exact |
| TLS record content entropy | Always ~1.0 for encrypted, irrelevant | N/A |

DPI **cannot** see: plaintext content, MTProto message boundaries, message types (the AES-CTR + IntermediateTransport framing makes inner content opaque). The entire attack surface is **metadata**: sizes, timing, direction, volume.

### 1.2. What Real HTTPS Browser Traffic Looks Like

A real Chrome → website TLS 1.3 session has these size characteristics (measured from live captures):

**Post-handshake initial records (C→S):**
1. HTTP/2 connection preface + SETTINGS: ~90-150 bytes of application data
2. WINDOW_UPDATE: ~13 bytes
3. HEADERS frame (GET request): ~200-800 bytes (depending on headers)
4. Usually coalesced into 1-3 TLS records of 200-1500 bytes

**Subsequent records (S→C):**
1. SETTINGS + WINDOW_UPDATE response: ~50-100 bytes
2. HEADERS frame (response headers): ~200-500 bytes
3. DATA frames: Typically 16384 bytes (h2 max frame size default), or smaller for final chunk
4. Server sends large records: 1-16384 bytes

**Interactive browsing (C→S):**
- Many small-to-medium records: 50-500 bytes (navigation clicks, AJAX, settings)
- Occasional medium records: 500-2000 bytes (form submissions)
- Rare large records: 2000-16384 bytes (file uploads)

**Browsing (S→C):**
- Mix of medium and large records: 1000-16384 bytes
- Server tends toward near-MTU or max-frame-size records for efficiency

### 1.3. What MTProto Over TLS Looks Like Without Mitigations

Without any size obfuscation, MTProto has a **distinctive** fingerprint:

**C→S:**
- Keepalive pings: ~88-100 bytes inner → 64-byte encrypted bucket + 24 header = 88 bytes → ~97 TLS record
- ACKs/confirms: ~88-128 bytes → 64-128 bucket → ~97-157 TLS record
- Interactive RPCs (getHistory, etc.): ~128-384 bytes → 128-384 bucket
- Auth handshake: ~200-300 bytes
- Large uploads: ~4000+ bytes

**S→C:**
- Status updates/session info: ~88-256 bytes
- Message/update notifications: ~200-800 bytes
- Media downloads: ~4000-16384 bytes
- Response to getHistory: ~2000-8000 bytes

**The killer fingerprint:** MTProto's `do_calc_crypto_size2_basic()` rounds to buckets `{64, 128, 192, 256, 384, 512, 768, 1024, 1280}`. These exact sizes repeat over thousands of records and create a distinctive histogram. No browser HTTPS traffic produces this pattern.

### 1.4. GoodbyeDPI Lessons (Source Code Analysis)

From `docs/Samples/GoodbyeDPI/src/`:

GoodbyeDPI fights a **different layer** of DPI attack: content-based classification of HTTP/TLS handshakes before encryption is established. It targets **Passive DPI** (optical splitter / port mirror that injects RST/302 redirect) and **Active DPI** (inline TSPU). Its primary techniques are:

1. **TCP-level fragmentation** (native + window-size-based): Splits the TLS ClientHello SNI across multiple TCP segments so the DPI parser cannot extract the domain. Two variants: `--native-frag` (actual payload split, more reliable) and window-size reduction (SYN-ACK manipulation, slower).
2. **Fake/decoy packets**: Sends HTTP requests or TLS ClientHellos with wrong TTL, wrong TCP checksum, or wrong SEQ number. DPI processes the fake, then discards the real (or vice versa), causing desynchronization. Supports `--auto-ttl` (distance-adaptive), `--wrong-chksum`, `--wrong-seq`.
3. **HTTP header manipulation**: `Host:` → `hoSt:` case change, space removal, mixed case domain (`eXaMpLe.CoM`) — all exploit DPI's string-matching rigidity.
4. **QUIC blocking** (`-q`): Drops local UDP/443 QUIC packets so browsers fall back to TCP TLS — prevents QUIC analysis vector.
5. **DNS redirection**: Redirects UDP/53 to alternative resolvers to bypass DNS poisoning.
6. **TSPU signature detection**: Filters inbound packets with IP ID in `0x0000-0x000F` range — a known TSPU RST injection marker.

**Why GoodbyeDPI's core techniques are NOT directly applicable post-handshake:**

1. **TCP reassembly**: TSPU (Active DPI) reassembles TCP streams. TCP-level fragmentation doesn't change the TLS record size histogram visible after reassembly.
2. **Post-handshake opaqueness**: After the TLS handshake completes, the DPI cannot inspect application content. Our threat is purely statistical (sizes + timing), not content-based.
3. **Fake packets**: Inserting dummy TCP segments confuses the DPI's TCP state machine but doesn't alter the TLS Application Data record size distribution.

**What IS relevant from GoodbyeDPI for our packet-size mitigation:**

1. **The `--max-payload` concept** (`goodbyedpi.c`): DPI boxes have a per-flow processing budget. Packets above a configurable threshold are passed without deep inspection. This directly implies: **DPI primarily scrutinizes small TLS records** (< ~1200 bytes), where MTProto pings/acks live. Small records are the highest-priority camouflage target.
2. **Fake packet → chaff record analogy**: GoodbyeDPI's decoy packets disrupt DPI state. Within our encrypted TLS connection, the equivalent is **chaff TLS records** — dummy Application Data records injected during idle periods to break silence-detection patterns (Section 10).
3. **TSPU behavioral insight**: GoodbyeDPI's IP ID detection (`0x0000-0x000F`) confirms TSPU injects packets into the stream. This means TSPU is an **active** inline device, not just a passive tap — it can correlate bidirectional flows and count concurrent connections per (src_ip, dst_ip:port) pair (AV-8).
4. **Hardcoded fake ClientHello** (`fakepackets.c`): GoodbyeDPI embeds a 516-byte Firefox 130 ClientHello as decoy. This shows the adversary (TSPU developers) is aware of browser fingerprinting — they will use the same technique in reverse to validate that our ClientHello matches real browser patterns.
5. **QUIC blocking precedent**: Russia already blocks QUIC (RU→non-RU). GoodbyeDPI's `-q` flag drops QUIC locally to force TCP fallback. This confirms QUIC/HTTP3 imitation is not a viable stealth strategy for our use case.

---

## 2. Current State & Gaps

### 2.1. What Already Works

| Component | Status | Location |
|---|---|---|
| DRS (Dynamic Record Sizing) with 3-phase model | ✅ Implemented | `DrsEngine.h/cpp` |
| IPT (Inter-Packet Timing) burst/idle model | ✅ Implemented | `IptController.h/cpp` |
| StealthTransportDecorator (DRS+IPT integration) | ✅ Implemented | `StealthTransportDecorator.h/cpp` |
| Batch coalescing of same-hint messages | ✅ Implemented | `StealthTransportDecorator.cpp` (BatchBuilder) |
| TLS record splitting for large payloads | ✅ Implemented | `ObfuscatedTransport::do_write_tls()` |
| Traffic hint wiring (Interactive/Keepalive/BulkData/Auth) | ✅ Implemented | `RawConnection.cpp`, `Session.cpp` |
| `use_random_padding` (0-255 random bytes in MTProto) | ✅ Implemented | `Transport.cpp` `do_calc_crypto_size2_rand()` |
| TLS record size clamping [256, 16384] | ✅ Implemented | `ObfuscatedTransport::set_max_tls_record_size()` |

### 2.2. Critical Gaps

| Gap | Severity | Description |
|---|---|---|
| **G1: MTProto fixed bucket sizes leak** | CRITICAL | `do_calc_crypto_size2_basic()` uses `{64, 128, 192, 256, 384, 512, 768, 1024, 1280}` buckets. These exact sizes create a perfect fingerprint visible through TLS record lengths. DRS controls the **maximum** record size, but the **inner** MTProto packet size is still quantized to these buckets. |
| **G2: Small records unmistakably small** | CRITICAL | A keepalive ping after crypto+framing produces a ~97-byte TLS record. DRS `min_payload_cap=900` is only a *maximum record size cap*, not a *minimum padding floor*. The actual TLS record contains only the small MTProto message — the DRS doesn't pad it up to 900. |
| **G3: No TLS record-level padding** | HIGH | DRS/BatchBuilder coalesce messages but never **pad** a record to reach the target size. If only one 88-byte MTProto message is ready, it ships as an 88-byte TLS record payload, regardless of DRS target. |
| **G4: Post-handshake first records are distinctly MTProto** | HIGH | The very first TLS Application Data records after handshake carry raw MTProto auth or encrypted RPC. Browser traffic would start with HTTP/2 SETTINGS + WINDOW_UPDATE + GET. The size pattern of initial records is a strong classifier. |
| **G5: No chaff for idle connections** | MEDIUM | During idle periods, a real browser sends h2 PING/SETTINGS_ACK at periodic intervals. MTProto idle connections either send nothing (detectable silence) or send keepalive pings at distinctive intervals with distinctive small sizes. |
| **G6: Bidirectional size correlation** | MEDIUM | MTProto has distinctive request→response size ratios that differ from h2. A 88-byte ping produces a 88-byte pong. A getHistory request (~200 bytes) triggers a large response (~4000 bytes). These correlations are different from h2 patterns. |
| **G7: Inter-record size entropy within a TLS session** | LOW | Within a session, h2 DATA frames tend toward consistent near-16KiB sizes during downloads, with occasional smaller frames. MTProto produces a wider distribution. The DRS phase model already partially mitigates this. |
| **G8: ALPN/connection-count behavioral mismatch** | CRITICAL | The TLS ClientHello ALPN affects how many parallel TCP connections a browser would open to the same origin. Real HTTP/2 (h2) multiplexes everything over **1 TCP connection**. Real HTTP/1.1 opens **6** (Chrome default) per origin. MTProto via proxy opens **18 connections** (non-premium: 1 main×2 + 4 upload×2 + 2 download×2 + 2 download_small×2) or up to **50 connections** (premium: 1×2 + 8×2 + 8×2 + 8×2) to the **same proxy IP:port**. This is a trivially detectable anomaly regardless of ALPN choice. The proxy path already uses ALPN `http/1.1` only (correct), but the connection count is 3-8× higher than any browser would produce to a single origin. |

### 2.3. Gap Severity Rationale

**G1+G2+G3 are CRITICAL** because they are the *lowest-cost* detection signals. A TSPU DPI box can simply histogram TLS record sizes across a connection and flag connections where:
- Many records are in the 64-256 byte range (MTProto pings/acks)
- Record sizes cluster at multiples of 64/128 (bucket quantization)
- No records match h2 DATA frame sizes (near-16KiB)

This is a trivially cheap ML feature (histogram + threshold) that runs at line speed. It does not require deep state tracking or expensive correlation.

**G4 is HIGH** because the first 3-5 records after handshake are frequently inspected by DPI precisely because they are cheap to capture (only inspect the beginning of each flow) and highly discriminative.

### 2.4. Available Empirical Data (Traffic Dump Corpus)

The repository contains **41 real browser PCAP/PCAPNG captures** under `docs/Samples/Traffic dumps/` covering all target platforms. These captures are already used for ClientHello fixture extraction and must now also serve as the **authoritative source** for TLS Application Data record size distributions.

| Platform | Captures | Browsers |
|---|---|---|
| **Android** | 10 | Chrome 146 (Pixel 9 Pro XL, OnePlus 13, Samsung S25+), Firefox 149, Brave 1.88, Yandex Browser 26.3, Samsung Internet 29 |
| **iOS** | 15 | Safari 26.2/26.3/26.4, Chrome 146/147, Brave 1.88, Firefox 149 |
| **Linux Desktop** | 9 | Chrome 144/146, Firefox 148/149, tdesktop 6.7.3 (including file-upload and file-download traces) |
| **macOS** | 2 | Firefox 149, Safari 26.4 |
| **Mixed (root)** | 5 | Firefox, gosuslugi.pcap, various Russian sites (ur66.ru, beget.com, web_max.ru) |

**Critical: Russian-site captures** (gosuslugi.pcap, ur66.ru.pcap, beget.com.pcap, web_max.ru_.pcap) provide ground truth for what TSPU considers "normal" domestic HTTPS traffic — the very traffic our masking must resemble.

These captures must be processed by the new `extract_tls_record_size_histograms.py` tool (PR-S0, Section 6) to produce:
1. **Per-connection phase histograms**: First-flight (records 1-5), active browsing, bulk transfer, idle periods
2. **Per-platform reference CDFs**: Chrome-Android, Safari-iOS, Chrome-Linux, Firefox-Linux, etc.
3. **Aggregate HTTPS baseline CDF**: Weighted average across all browsers, used as the universal comparison target
4. **Small-record frequency baseline**: Measured fraction of records < 200 bytes in real browser traffic (expected: < 5%)
5. **First-flight size sequences**: Ordered (direction, size) tuples for the first 10 records of each connection

### 2.5. Capture-Driven Methodology (Core Design Principle)

**The central insight, validated by DPI research and the expert review, is that purely synthetic size generation (random walks, Markov chains, uniform distributions) is detectable by modern ML-based DPI classifiers. The correct approach is empirical alignment: sample target sizes from distributions measured on real browser traffic.**

The methodology has three steps:

**Step 1: Extract** — Parse the 41 pcap dumps to extract TLS Application Data (`0x17`) record sizes, directions, and inter-record timing. Produce per-connection JSON artifacts containing:
```json
{
  "source": "chrome146_android16_pixel9.pcap",
  "platform": "android",
  "browser": "chrome_146",
  "connections": [
    {
      "server_name": "www.google.com",
      "records": [
        {"direction": "c2s", "size": 182, "timestamp_us": 0},
        {"direction": "s2c", "size": 93, "timestamp_us": 1245},
        {"direction": "c2s", "size": 517, "timestamp_us": 3891},
        ...
      ]
    }
  ]
}
```

**Step 2: Model** — From the extracted data, compute phase-aware empirical CDFs:
- **Connection Phase 0 — Greeting** (records 1-5): Fixed-template size sequences per browser family
- **Connection Phase 1 — Active** (records 6-50, or until bulk detected): Weighted size bin distribution, mostly 200-4000 bytes
- **Connection Phase 2 — Bulk** (when consecutive large records detected): Heavily weighted toward 14000-16384 bytes (h2 DATA frames)
- **Connection Phase 3 — Idle** (inter-record gap > 5 seconds): PING/SETTINGS_ACK sizes, typically 50-150 bytes padded to ~200-400

**Step 3: Embed** — Compile the CDFs into `StealthConfig` DRS phase parameters. Replace the current manually-specified bins:
```
// CURRENT (manually specified):
slow_start:     [1200, 1460] w=1, [1461, 1700] w=1
congestion_open: [1400, 1900] w=1, [1901, 2600] w=2
steady_state:   [2400, 4096] w=2, [4097, 8192] w=2, [8193, 12288] w=1

// IMPROVED (capture-derived, example — actual values from pcap analysis):
greeting:        [100, 200] w=3, [200, 600] w=5, [600, 1400] w=2   ← first-flight
active_browsing: [200, 600] w=2, [600, 1400] w=5, [1400, 4000] w=3 ← request-response
bulk_transfer:   [8192, 12288] w=1, [12288, 16384] w=4              ← large DATA frames
idle_chaff:      [200, 400] w=3, [400, 800] w=1                     ← PING/SETTINGS_ACK
```

**Validation**: Quality of the capture-derived distributions is measured by:
- **Two-sample Kolmogorov-Smirnov test**: Compare the CDF of mitigated MTProto record sizes against the CDF of the reference browser corpus. Null hypothesis: both samples come from the same distribution. Require p > 0.05.
- **Chi-squared goodness-of-fit test**: Bin the mitigated record sizes into 50-byte buckets and compare against the reference histogram. Require p > 0.05.
- **Small-record frequency test**: Fraction of records < 200 bytes must be < 5% (matching browser baseline).
- **Bucket quantization detection test**: No statistically significant peaks at MTProto bucket boundaries (64, 128, 192, 256, 384, 512, 768, 1024, 1280 ± 9 bytes overhead).

---

## 3. Attack Vectors

### AV-1: Record Size Histogram Fingerprinting

**Attack:** DPI collects size of every TLS Application Data record for first N records (N=20-100). Builds a size histogram. Compares against known browser h2 baseline vs MTProto baseline using ML classifier (even simple decision tree/SVM suffices).

**Distinguishing features:**
- MTProto overrepresents 64-256 byte records (pings, acks, small RPCs)
- MTProto underrepresents 14000-16384 byte records (h2 DATA frames)
- MTProto shows quantization peaks at bucket boundaries

**Cost to adversary:** Very low. Single-pass histogram on first N records. Can be implemented in FPGA for line-rate classification.

### AV-2: First-Flight Record Size Sequence

**Attack:** DPI captures the ordered sequence of (direction, size) for the first 5-10 TLS records after handshake. This sequence is a strong protocol discriminator.

**Browser h2 baseline (C→S first):** `[C:~200, S:~100, C:~500, S:~16384, S:~16384, ...]`
**MTProto baseline:** `[C:~88, S:~300, C:~300, S:~200, C:~128, ...]`

**Cost to adversary:** Very low. Fixed-length feature vector from connection start.

### AV-3: Small-Record Frequency Analysis

**Attack:** DPI counts fraction of TLS records smaller than a threshold T (e.g., T=200 bytes). For browser h2 traffic, this fraction is typically < 5% (only SETTINGS/PING control frames). For MTProto, this fraction is 30-60% (keepalives, acks, confirmations, small RPCs).

**Cost to adversary:** Trivial counter. No ML needed.

### AV-4: Bucket Quantization Detection

**Attack:** DPI checks if record sizes follow a multinomial distribution restricted to known MTProto bucket sizes ({64, 128, 192, 256, 384, 512, 768, 1024, 1280} + 448-multiples). Even with noise from IntermediateTransport padding (0-15 bytes), the +4 byte header, and +5 byte TLS header, the underlying quantization is recoverable.

Expected TLS record sizes with bucket transport:
- 64 → 64 + 4 (frame) + [0-15] (padding) → 68-83 (pre-TLS encrypt) → TLS record: 68-83 + 5 = 73-88
- 128 → 128 + 4 + [0-15] → 132-147 → TLS record: 137-152
- 256 → 256 + 4 + [0-15] → 260-275 → TLS record: 265-280
- etc.

Subtracting the constant +4+5=+9 byte overhead, the DPI can reconstruct the original bucket with high confidence.

**Cost to adversary:** Low. Subtract constant, check if distribution matches known bucket set.

### AV-5: Bidirectional Size Ratio Fingerprinting  

**Attack:** DPI computes the ratio of upstream to downstream bytes per time window. MTProto has distinctive ratios (mostly symmetric for chat, highly asymmetric for media download) that differ from h2 browsing patterns.

**Cost to adversary:** Medium. Requires bidirectional correlation.

### AV-6: Idle Silence Pattern

**Attack:** DPI measures inter-record gaps. MTProto idle connections show distinctive timing: no records for long stretches, then a burst of small records (keepalive). Browser h2 shows periodic PING frames at regular intervals.

**Cost to adversary:** Low. Timer-based classification.

### AV-7: Connection Lifetime Size Evolution (HMM)

**Attack:** Hidden Markov Model (see `docs/Researches/HMM-Stanford.pdf`) that models the state transitions of record sizes over a connection lifetime. Different protocol types (h2 browsing, h2 streaming, MTProto chat, MTProto media) produce different state transition matrices.

**Cost to adversary:** Medium-High. Requires per-connection state.

### AV-8: ALPN / Parallel Connection Count Mismatch

**Attack:** DPI counts concurrent TCP connections from the same client IP to the same destination IP:port. Cross-references this count with the ALPN offered in each connection's ClientHello.

**Behavioral baseline per ALPN:**
- Client offers `h2` (preferred) + `http/1.1` (fallback) → If server picks h2, browser opens **1 connection**. If server picks http/1.1, browser opens **up to 6** (Chrome). In TLS 1.3, server's ALPN choice is in EncryptedExtensions (encrypted), so DPI can't see which was chosen — but it can still count connections.
- Client offers `http/1.1` only → Browser opens **up to 6** connections per origin.

**MTProto actual behavior:**
- Per DC: `main_session` (1-N sessions × 2 conns) + `upload_session` (4-8 × 2) + `download_session` (2-8 × 2) + `download_small_session` (2-8 × 2)
- Non-premium to same proxy: **(1×2) + (4×2) + (2×2) + (2×2) = 18 connections**
- Premium: **(1×2) + (8×2) + (8×2) + (8×2) = 50 connections**
- Multiple active DCs multiply this further

**DPI classification rule (trivial):**
```
IF concurrent_tcp_to_same_dest > 8 AND tls_version >= 1.3:
  FLAG as non-browser (99.9% confidence for h2 ALPN, 95%+ for h1.1-only)
```

**Cost to adversary:** Extremely low. Simple counter per (src_ip, dst_ip:port) tuple. No ML needed.

**Current mitigation status:** Proxy path uses `http/1.1` only ALPN (good, avoids the h2-implies-1-connection trap), but connection count is still 3-8× above browser http/1.1 norms.

**Why advertising `h2` + `http/1.1` together is an option but not sufficient:**
- In TLS 1.3, the server's ALPN choice is encrypted. DPI sees the client offered both but doesn't know which the server picked.
- If DPI assumes http/1.1 was chosen (safer interpretation), up to 6 connections are plausible.
- But 18-50 is still way beyond plausible for either protocol.
- Additionally, advertising `h2` but never performing h2-like behavior (single multiplexed connection for the majority of traffic) is a secondary behavioral anomaly.
- **Conclusion:** The ALPN choice matters less than fixing the raw connection count.

---

## 4. Mitigation Architecture

### 4.1. Layered Defense Model

```
Layer 6: Statistical Quality Gate (CI chi-squared / K-S validation) ..... [PR-S0]
Layer 5: Chaff Traffic Injector (idle-period dummy records) .............. [PR-S5]
Layer 4: Post-Handshake Greeting Camouflage (first-flight record sizes) . [PR-S3]
Layer 3: Connection Count Cap (browser-realistic parallel conn limit) .... [PR-S7]
Layer 2: TLS Record Padding Module (pad-to-target within TLS record) .... [PR-S2]
Layer 1: MTProto Random Padding (eliminate bucket quantization) ......... [PR-S1]
Layer 0: Existing DRS + IPT + BatchBuilder (timing + record cap) ........ [DONE]
```

Each layer is independent and additively reduces the DPI attack surface:

- **Layer 0** (existing) controls TLS record capping and inter-packet timing
- **Layer 1** eliminates the bucket fingerprint at the MTProto encryption level
- **Layer 2** ensures every TLS record reaches a browser-realistic minimum size via padding
- **Layer 3** caps parallel connections to a single proxy to browser-realistic levels
- **Layer 4** makes connection starts indistinguishable from h2 browsing
- **Layer 5** makes idle patterns indistinguishable from h2 keepalive
- **Layer 6** provides continuous statistical validation that mitigated traffic is indistinguishable from reference browser captures

### 4.2. Design Principles

1. **Stealth-only activation**: All mitigations active only when `ProxySecret::emulate_tls() == true` and runtime policy is enabled. No behavioral change for non-stealth connections.
2. **Fail-closed**: If the mitigation layer encounters an error (e.g., insufficient buffer space for padding), the connection is torn down — not silently sent unpadded.
3. **No content-level tampering**: We never inject bytes into the MTProto plaintext stream. Padding happens at the TLS record level (post-encryption) or at the MTProto crypto padding level (pre-encryption, already existing).
4. **Capture-driven parameters** (NOT synthetic generation): All size distribution targets, phase transition thresholds, greeting templates, and idle chaff sizes are derived from empirical pcap analysis of the 41 browser captures under `docs/Samples/Traffic dumps/`. No manually invented heuristics. No random walks or Markov chains for size generation — only weighted sampling from measured real-browser CDFs.
5. **Actor-confined state**: All new state lives within `StealthTransportDecorator` or components it owns. No new shared mutable state.
6. **Connection-phase state machine**: The transport decorator models a connection as a finite automaton with phases (Greeting → Active → Bulk → Idle), each with its own capture-derived size distribution. This mirrors how real HTTP/2 connections evolve over time.
7. **Statistical quality gates in CI**: Every PR touching size distributions must pass chi-squared and Kolmogorov-Smirnov tests against the reference browser corpus before merge.

### 4.3. Activation Gate

All new mitigations check the same gate as existing stealth:

```
TDLIB_STEALTH_SHAPING compile flag
  → ProxySecret::emulate_tls() runtime check
    → StealthTransportDecorator wrapping
      → New mitigations via config/policy
```

---

## 5. Implementation Priority (PRs)

| PR | Title | Addresses | Severity | Effort |
|---|---|---|---|---|
| **PR-S0** | Capture-driven baseline extraction & statistical quality gates | Foundation for all | PREREQUISITE | Medium |
| **PR-S1** | MTProto crypto bucket elimination | G1, AV-4 | CRITICAL | Medium |
| **PR-S2** | TLS record padding to capture-driven target | G2, G3, AV-1, AV-3 | CRITICAL | Medium |
| **PR-S3** | Post-handshake greeting camouflage | G4, AV-2 | HIGH | Medium |
| **PR-S4** | Bidirectional size correlation defense | G6, AV-5 | MEDIUM | Low |
| **PR-S5** | Idle connection chaff traffic | G5, AV-6 | MEDIUM | Medium |
| **PR-S6** | Inter-record size pattern hardening | G7, AV-7 | LOW | Low |
| **PR-S7** | Stealth connection count cap | G8, AV-8 | CRITICAL | High |

**Dependency order:** **PR-S0** is the prerequisite for all others — it produces the reference histograms and statistical test framework. Then PR-S1 → PR-S2 → PR-S3 (sequential). PR-S4, PR-S5, PR-S6, PR-S7 are independent of each other and can be done after PR-S2. **PR-S7 is highest real-world impact** — without it, the other mitigations are undermined by the trivially detectable 18+ parallel connections.

---

## 6. PR-S0: Capture-Driven Baseline Extraction & Statistical Quality Gates

### 6.0. Rationale

Modern TSPU with ML classifiers does not check for "randomness" or "entropy" — it checks for **conformance to a known protocol profile**. A perfectly uniform or Gaussian size distribution is just as anomalous as MTProto bucket peaks, because real HTTP/2 traffic has a specific non-trivial distribution shape (bimodal: small control frames + large DATA frames, with connection-phase-dependent mixing).

The only reliable way to produce a non-distinguishable distribution is to **measure the real distribution and sample from it**. This PR builds the measurement and validation tooling.

### 6.1. Component 1: `extract_tls_record_size_histograms.py`

**Location:** `test/analysis/extract_tls_record_size_histograms.py`

**Input:** All pcap/pcapng files under `docs/Samples/Traffic dumps/`

**Output:** JSON artifacts under `test/analysis/fixtures/record_sizes/` with structure:
```json
{
  "source_pcap": "Chrome_146_Android_16_Pixel_9_Pro_XL.pcap",
  "platform": "android",
  "browser_family": "chrome_146",
  "extraction_date": "2026-04-09",
  "connections": [
    {
      "server_name": "www.google.com",
      "tls_version": "1.3",
      "alpn_negotiated": "h2",
      "duration_ms": 45230,
      "records": [
        {"seq": 0, "direction": "c2s", "tls_record_size": 182, "relative_time_us": 0},
        {"seq": 1, "direction": "s2c", "tls_record_size": 93, "relative_time_us": 1245},
        {"seq": 2, "direction": "c2s", "tls_record_size": 517, "relative_time_us": 3891},
        {"seq": 3, "direction": "s2c", "tls_record_size": 16389, "relative_time_us": 5102}
      ]
    }
  ],
  "aggregate_stats": {
    "total_connections": 47,
    "total_records": 2341,
    "c2s_size_percentiles": {"p5": 142, "p25": 312, "p50": 891, "p75": 2341, "p95": 14892},
    "s2c_size_percentiles": {"p5": 93, "p25": 451, "p50": 4096, "p75": 16384, "p95": 16389},
    "small_record_fraction": 0.031,
    "first_flight_c2s_sizes": [[142, 517, 312], [189, 623, 287], "..."]
  }
}
```

**Implementation:** Uses `tshark` for pcap parsing (already a project dependency for `extract_client_hello_fixtures.py`):
```bash
tshark -r capture.pcap -Y "tls.record.content_type == 23" \
  -T fields -e frame.number -e frame.time_relative \
  -e ip.src -e ip.dst -e tcp.srcport -e tcp.dstport \
  -e tls.record.length -e tls.record.content_type
```

### 6.2. Component 2: `check_record_size_distribution.py`

**Location:** `test/analysis/check_record_size_distribution.py`

**Purpose:** Statistical validation of record size distributions against captured baselines. Invoked by the corpus smoke test and by C++ test harnesses.

**Statistical tests implemented:**

| Test | Purpose | Threshold |
|---|---|---|
| **Two-sample K-S test** | Overall CDF conformance to reference browser corpus | p > 0.05 |
| **Chi-squared goodness-of-fit** | Binned histogram conformance (50-byte bins) | p > 0.05 |
| **Bucket quantization detector** | No peaks at MTProto bucket boundaries ± 16 bytes | Bin excess ratio < 1.5× expected |
| **Small-record frequency** | Fraction of records < 200 bytes | < 5% (measured browser baseline) |
| **Large-record presence** | Fraction of records > 12000 bytes in bulk phase | > 20% (browser baseline for downloads) |
| **First-flight template conformance** | First 5 C→S record sizes within greeting template ranges | 100% compliance |
| **Lag-1 autocorrelation** | No deterministic size sequences | |r| < 0.4 |
| **Phase transition smoothness** | Size ratio between adjacent records at phase boundaries | < 3.0× |

### 6.3. Component 3: Aggregate Reference CDFs (Embedded in C++)

**Location:** `td/mtproto/stealth/StealthRecordSizeBaselines.h`

After pcap extraction, the aggregate CDFs are compiled into a C++ header as constexpr arrays for use by the DRS engine:

```cpp
// Auto-generated from extract_tls_record_size_histograms.py
// Source: 41 pcap captures, 47,000+ TLS Application Data records
namespace td::stealth::baselines {

// Greeting phase (first 5 C→S records) — capture-derived size ranges per record
constexpr RecordSizeBin kGreetingRecord1[] = {{100, 200, 3}, {200, 400, 5}, {400, 900, 2}};
constexpr RecordSizeBin kGreetingRecord2[] = {{300, 600, 4}, {600, 1400, 6}};
constexpr RecordSizeBin kGreetingRecord3[] = {{150, 400, 3}, {400, 900, 4}, {900, 1600, 3}};

// Active browsing phase — request/response mixed traffic
constexpr RecordSizeBin kActiveBrowsingBins[] = {
    {200, 600, 2}, {600, 1400, 5}, {1400, 4096, 3}, {4096, 8192, 1}};

// Bulk transfer phase — large DATA frames
constexpr RecordSizeBin kBulkTransferBins[] = {
    {8192, 12288, 1}, {12288, 16384, 4}};

// Idle chaff phase — PING/SETTINGS_ACK record sizes
constexpr RecordSizeBin kIdleChaffBins[] = {{200, 400, 3}, {400, 800, 1}};

// Small-record frequency budget (measured: 3.1% across all captures)
constexpr double kSmallRecordThreshold = 200.0;
constexpr double kSmallRecordMaxFraction = 0.05;  // 5% budget, measured 3.1%

}  // namespace td::stealth::baselines
```

### 6.4. Test Plan (TDD — tests FIRST)

**File: `test/analysis/test_extract_tls_record_size_histograms.py`**

| # | Test Name | Type | Description |
|---|---|---|---|
| 1 | `test_tshark_extraction_produces_valid_json` | Positive | Run extraction on one known pcap, verify JSON schema compliance |
| 2 | `test_record_sizes_are_positive_integers` | Invariant | All extracted sizes > 0 and ≤ 16389 (max TLS record) |
| 3 | `test_directions_are_c2s_or_s2c` | Invariant | All direction fields are exactly "c2s" or "s2c" |
| 4 | `test_timestamps_are_monotonically_increasing` | Invariant | Within each connection, timestamps never decrease |
| 5 | `test_aggregate_percentiles_monotonic` | Invariant | p5 ≤ p25 ≤ p50 ≤ p75 ≤ p95 |
| 6 | `test_small_record_fraction_below_ten_percent` | Baseline | For all real browser captures, small record fraction < 10% (sanity) |
| 7 | `test_at_least_one_connection_per_capture` | Positive | Each pcap produces ≥ 1 TLS connection |
| 8 | `test_chrome_android_has_h2_alpn` | Baseline | Chrome Android captures negotiate h2 |
| 9 | `test_first_flight_has_5_records_minimum` | Positive | Most connections have ≥ 5 records |

**File: `test/analysis/test_check_record_size_distribution.py`**

| # | Test Name | Type | Description |
|---|---|---|---|
| 1 | `test_ks_test_passes_for_real_browser_vs_browser` | Positive | Two different browser captures should pass K-S test (same population) |
| 2 | `test_ks_test_fails_for_mtproto_vs_browser` | Negative | Synthetic MTProto bucket distribution should FAIL K-S test against browser baseline |
| 3 | `test_chi_squared_rejects_uniform_distribution` | Negative | Perfectly uniform sizes should be rejected (not browser-like) |
| 4 | `test_chi_squared_rejects_bucket_quantized` | Negative | Distribution with peaks at {64, 128, 256, 512} should be strongly rejected |
| 5 | `test_bucket_detector_flags_known_mtproto_pattern` | Adversarial | Feed exact MTProto bucket sizes + 9 byte overhead, verify detection |
| 6 | `test_bucket_detector_passes_browser_traffic` | Positive | Real browser record sizes pass bucket detection (no false positives) |
| 7 | `test_small_record_frequency_rejects_30_percent` | Adversarial | Distribution with 30% < 200 bytes should be rejected |
| 8 | `test_first_flight_validation_rejects_mtproto_pattern` | Adversarial | First-flight sequence [88, 300, 300, 200, 128] should be rejected |
| 9 | `test_autocorrelation_rejects_monotonic_sequence` | Adversarial | [100, 200, 300, 400, 500, ...] should fail lag-1 autocorrelation test |
| 10 | `test_phase_transition_rejects_abrupt_jump` | Adversarial | Sequence [..., 1400, 1400, 16384, 16384, ...] (sudden 10× jump) should be flagged |
| 11 | `test_gaussian_distribution_rejected` | Adversarial | N(8000, 2000) noise is rejected — proves synthetic generation detectable |
| 12 | `test_lognormal_distribution_rejected` | Adversarial | LogNormal synthetic generation is also rejected — not browser-like |

**File: `test/stealth/test_stealth_record_size_baselines.cpp`**

| # | Test Name | Type | Description |
|---|---|---|---|
| 1 | `BaselineBinsNonEmpty` | Positive | All constexpr bin arrays have ≥ 1 entry |
| 2 | `BaselineBinsNonOverlapping` | Invariant | Within each phase, bins don't overlap |
| 3 | `BaselineBinWeightsPositive` | Invariant | All weights > 0 |
| 4 | `BaselineGreetingCoversFirstFlight` | Positive | Greeting bins produce sizes in [80, 1600] |
| 5 | `BaselineBulkIncludesNear16K` | Positive | Bulk bins include sizes in [14000, 16384] |
| 6 | `BaselineSmallRecordBudgetSane` | Invariant | `kSmallRecordMaxFraction` ∈ (0.0, 0.10] |
| 7 | `SamplingFromBaselineProducesRealisticDistribution` | Adversarial | 10000 samples from active browsing bins, K-S tested against reference CDF |
| 8 | `SamplingNeverExceedsTlsMax` | Invariant | No sampled value > 16384 |
| 9 | `SamplingNeverBelowZero` | Invariant | No sampled value ≤ 0 |

---

## 7. PR-S1: MTProto Crypto Bucket Elimination

### 7.1. Problem

`Transport.cpp :: do_calc_crypto_size2_basic()` uses fixed bucket sizes `{64, 128, 192, 256, 384, 512, 768, 1024, 1280}`. When `use_random_padding` is false (the non-stealth default for TLS-emulating transport), packets are quantized to these exact sizes plus the 24-byte unencrypted header.

Even when `use_random_padding` is true, the random padding is only `Random::secure_uint32() & 0xff` (0-255 bytes), which adds noise but doesn't eliminate the quantization — the result is still 16-byte aligned (AES block size), producing a distinctive modular pattern.

### 7.2. Solution

**Enforce `use_random_padding = true` for all stealth transport paths.** This is the minimal correct fix — it replaces deterministic bucket rounding with random padding of 0-255 bytes, breaking the histogram fingerprint. The result is still 16-byte aligned (required by AES-IGE), but the quantization bands are wide enough to smear the histogram.

Additionally, when stealth is active, increase the random padding range from 0-255 to a configurable range (default 12-480 bytes) to ensure:
- Minimum 12 bytes padding (already required by MTProto v2 validation)
- Average padding ~240 bytes, making small packets reliably larger
- No two consecutive packets with identical size (modulo inevitable AES alignment collisions)

### 7.3. Code Changes

**File: `td/mtproto/Transport.cpp`**
- Add `do_calc_crypto_size2_stealth()` that uses a wider, policy-driven padding range
- Guard activation behind stealth flag in `PacketInfo`

**File: `td/mtproto/PacketInfo.h`**
- Add `bool use_stealth_padding{false}` alongside `use_random_padding`

**File: `td/mtproto/stealth/StealthConfig.h`**
- Add `CryptoPaddingPolicy` with configurable min/max padding bytes

**File: `td/mtproto/stealth/StealthTransportDecorator.cpp`**
- Pass stealth padding flag through to inner transport `use_random_padding` configuration

### 7.4. Test Plan (TDD — tests FIRST)

**File: `test/stealth/test_mtproto_crypto_padding_bucket_elimination.cpp`**

| # | Test Name | Type | Description |
|---|---|---|---|
| 1 | `BasicPaddingDefaultsToRandomWhenStealthActive` | Positive | Verify `use_random_padding=true` when stealth decorator is active |
| 2 | `BucketEliminationNoBucketPeaksIn10000Packets` | Adversarial | Generate 10000 packets of various sizes (4-4096 bytes payload) with stealth padding. Histogram the encrypted sizes. Chi-squared test against uniform distribution must pass (no peaks at bucket boundaries). |
| 3 | `BucketQuantization_NonStealth_HasPeaks` | Negative | Confirm that without stealth, the bucket peaks ARE present (regression baseline). |
| 4 | `StealthPaddingMinimumIs12Bytes` | Edge case | Verify padding is always ≥ 12 bytes per MTProto v2 spec |
| 5 | `StealthPaddingMaximumRespected` | Edge case | Verify padding never exceeds configured max |
| 6 | `StealthPaddingSizeDistributionIsUniform` | Adversarial | K-S test: padding size distribution should be approximately uniform within [min_pad, max_pad] |
| 7 | `PaddedSizeAlwaysMod16Zero` | Invariant | AES-IGE requires 16-byte alignment. Every encrypted size mod 16 == 0. |
| 8 | `SmallPayload4BytesPaddedToAtLeast128` | Adversarial | A 4-byte ping payload after stealth padding must produce encrypted size ≥ 128 bytes (not 64 bucket). |
| 9 | `ConsecutiveSameSizePayloadsProduceDifferentWireSizes` | Adversarial | 100 consecutive 4-byte payloads must produce ≥ 10 distinct encrypted sizes. |
| 10 | `PaddingRngDependency_DifferentSeedsYieldDifferentDistributions` | Security | Two different RNG seeds must produce statistically distinguishable padding size sequences (guards against RNG misuse). |
| 11 | `PacketInfoFlagsOrthogonal` | Integration | `use_stealth_padding` and `use_random_padding` are independent flags. Stealth implies random but random doesn't imply stealth range. |
| 12 | `LargePaylad4096BytesStealthPaddingStillApplied` | Edge case | Even for large payloads, random padding in [min,max] is added. |

**File: `test/stealth/test_mtproto_crypto_padding_bucket_elimination_fuzz.cpp`**

| # | Test Name | Type | Description |
|---|---|---|---|
| 1 | `LightFuzz_RandomPayloadSize_NoAssertionFailure` | Fuzz | Random payload sizes 0-65535, random RNG seeds, 1000 iterations. No crash, no assertion failure, all results valid. |
| 2 | `LightFuzz_RecoverableFromMaxUint32Padding` | Fuzz | Extreme `max_pad = UINT32_MAX` config must be rejected by validation, not cause overflow. |

---

## 8. PR-S2: TLS Record Pad-to-Target (Capture-Driven)

### 8.1. Problem

Even after PR-S1 eliminates MTProto bucket quantization, the TLS records still carry the actual encrypted MTProto message size. A keepalive ping with PR-S1 random padding might be 128-256 bytes encrypted, which produces a TLS record of 133-261 bytes (including 5-byte TLS header). This is still distinctly small compared to browser h2 traffic where the fraction of records < 200 bytes is < 5%.

The DRS engine returns a `payload_cap` (e.g., 1400 bytes during congestion_open phase), but this is a **maximum** — the `BatchBuilder` never **pads up** to this target. It only coalesces multiple messages that happen to be queued simultaneously. The fundamental gap: **DRS is a ceiling, not a floor.**

Verified wire-level impact: An 88-byte keepalive ping in current stealth mode produces a TLS record of ~133 bytes (88 + 4 IntermediateTransport header + 5 TLS header + MTProto crypto overhead). The DRS `min_payload_cap=900` is the *sampled target cap*, but the actual message ships at its native size because nothing pads it upward. This is the single strongest DPI fingerprint after bucket quantization.

### 8.2. Solution: IntermediateTransport Padding Extension

**Architecture decision rationale:**

Three padding locations were evaluated:
1. ❌ **After AES-CTR encryption, before TLS header**: Extra bytes in the AES-CTR stream would be decrypted by the receiver as the start of the next IntermediateTransport frame, causing protocol desynchronization.
2. ❌ **Dummy IntermediateTransport frames alongside real ones**: The standard MTProto proxy protocol has no "discard" flag for frames; the proxy server cannot be modified.
3. ✅ **Within the IntermediateTransport frame**: The 4-byte length header INCLUDES the trailing padding in its count. The receiver reads `header_length` bytes, extracts the MTProto packet via the internal CryptoHeader's `message_data_length` field, and discards trailing bytes. This is already how `with_padding_` mode works (0-15 bytes). **Extending the range from 0-15 to 0-N is fully protocol-compatible.**

**Verified from `TcpTransport.cpp`:**
```cpp
// Current: 4-byte header includes padding in its count
as<uint32>(message->as_mutable_slice().begin()) = static_cast<uint32>(size + append_size);
// append_size is currently Random::secure_uint32() % 16 (0-15 bytes)
```

The receiver's frame parsing:
1. Read 4-byte header → get `frame_length` (masking off bit 31 for quick_ack)
2. Read `frame_length` bytes → entire frame including padding
3. Decrypt inner MTProto packet → `CryptoHeader.message_data_length` tells exact payload size
4. Remaining bytes = padding → silently discarded

**We extend `append_size` to reach the DRS target.** The limit is `uint32` (minus bit 31 for quick_ack) = 2^31 - 1 bytes. Practical limit: TLS record max payload = 16384 bytes. So we pad frames up to 16384 bytes.

**Two-layer padding strategy (PR-S1 + PR-S2 combined):**

```
small MTProto message (e.g., 88 bytes keepalive ping)
  → [MTProto v2 crypto padding: +12 to +1024 random bytes] (PR-S1)
     Result: ~128-1112 bytes encrypted (16-byte aligned), average ~600 bytes
  → [IntermediateTransport frame: 4-byte header + encrypted MTProto + N stealth padding bytes]
     N computed to reach DRS target: stealth_pad = max(0, target - encrypted_size - 4)
     Result: exactly DRS target bytes (e.g., 1400 bytes)
  → [AES-CTR encrypt entire frame] (length-preserving stream cipher)
  → [TLS record: 0x17 0x03 0x03 [len] [ciphertext]]
     where len = frame_size (≈ DRS target, e.g., 1400 bytes)
     Wire size = frame_size + 5 (TLS header) = 1405 bytes
```

**Padding target selection (capture-driven, not synthetic):**
```
target = DRS.next_payload_cap(hint)
```
Where `DRS.next_payload_cap()` now samples from capture-derived bins (PR-S0) instead of manually specified ranges:
- **Greeting phase** (first 5 records): Fixed template from first-flight captures
- **Active phase** (request-response): Weighted random from active browsing CDF
- **Bulk phase** (large transfers): Heavily weighted toward 14000-16384 bytes
- **Idle phase** (chaff): Small-to-medium PING/SETTINGS sizes from idle captures

**Small-record budget enforcement:**
The transport decorator maintains a rolling window counter of records < 200 bytes (the `kSmallRecordThreshold` from baselines). If the small-record fraction approaches 5%, the DRS MUST NOT return a target below 200 bytes for any hint, even Keepalive. This is a hard constraint, not advisory.

```cpp
if (small_record_count_in_window_ > kSmallRecordMaxFraction * window_size_) {
    // Budget exceeded — force minimum target to kSmallRecordThreshold
    target = std::max(target, static_cast<int32>(kSmallRecordThreshold));
}
```

### 8.3. Code Changes

**File: `td/mtproto/TcpTransport.cpp`**
- In `IntermediateTransport::write_prepare_inplace()`: when `stealth_target_frame_size_ > 0`, compute `append_size = target - (size + 4)` instead of `Random::secure_uint32() % 16`
- Clamp: `append_size = clamp(append_size, 0, max_tls_record_payload - size - 4)`
- Fill padding bytes with `Random::secure_bytes()` (they will be AES-CTR encrypted, indistinguishable from ciphertext)
- Bounds check: `CHECK(size + append_size + 4 <= static_cast<size_t>(INT32_MAX >> 1))` (quick_ack bit safety)

**File: `td/mtproto/TcpTransport.h`**
- Add `void set_stealth_target_frame_size(size_t target)` to IntermediateTransport
- Add member: `size_t stealth_target_frame_size_{0}`

**File: `td/mtproto/IStreamTransport.h`**
- Add `virtual void set_stealth_record_padding_target(int32 target_bytes)` with default no-op

**File: `td/mtproto/stealth/StealthTransportDecorator.cpp`**
- In `pre_flush_write()`: compute `frame_target = drs_target - TLS_HEADER_SIZE` and pass to inner transport via `set_stealth_record_padding_target(frame_target)`
- Maintain rolling small-record counter: increment when actual write < 200 bytes, enforce budget
- After write: verify `actual_record_size` is within [target - 32, target + 32] tolerance (fail-closed on violation)

**File: `td/mtproto/stealth/StealthConfig.h`**
- Add `RecordPaddingPolicy` struct:
  ```cpp
  struct RecordPaddingPolicy {
      int32 small_record_threshold{200};       // bytes — from baseline measurement
      double small_record_max_fraction{0.05};   // 5% budget
      int32 small_record_window_size{200};      // rolling window
      int32 target_tolerance{32};               // ± bytes around DRS target
  };
  ```

### 8.4. Test Plan (TDD — tests FIRST)

**File: `test/stealth/test_tls_record_padding_target.cpp`**

| # | Test Name | Type | Description |
|---|---|---|---|
| 1 | `SmallPayloadPaddedToTarget1400` | Positive | 88-byte MTProto payload + padding must produce ~1400-byte TLS record when DRS target is 1400. |
| 2 | `PaddingTargetRespectedWithin32ByteTolerance` | Positive | For 100 writes with target=1400, all TLS record sizes must be within [1368, 1432]. |
| 3 | `ZeroTargetProducesNoPadding` | Negative | When stealth padding is disabled (target=0), frame size matches native MTProto size. |
| 4 | `TargetSmallerThanPayloadNoUnderflow` | Edge case | If MTProto packet is already larger than DRS target, padding is 0 (not negative/underflow). No crash. |
| 5 | `TargetExactlyEqualsPayloadNoPadding` | Edge case | When packet size = target, no extra padding added. |
| 6 | `MaxTarget16384SaturatesCorrectly` | Edge case | Target 16384 produces record ≤ 16389 (16384 + 5 TLS header). |
| 7 | `PaddingBytesAreRandomNotZero` | Security | Captured padding bytes must not be all-zero (entropy check). At least 75% of bytes must be non-zero over 1000 samples. |
| 8 | `PaddingBytesEncryptedByAesCtr` | Security | The padding bytes pass through AES-CTR encryption. Verify they are not plaintext random on the wire. |
| 9 | `ReceiverCanStillParseFrameWithLargePadding` | Integration | Write a frame with 4096 bytes padding, verify the IntermediateTransport reader can decode the MTProto payload. |
| 10 | `ConsecutiveRecordSizeDistributionMatchesDrsTarget` | Adversarial | 500 consecutive writes with DRS targets sampled from capture-derived active browsing bins. Record sizes must correlate with targets (R² > 0.8). |
| 11 | `QuickAckBitPreservedWithLargePadding` | Edge case | Frame with quick_ack=true and 8000 bytes padding: quick_ack bit survives in 4-byte header (bit 31 not clobbered). |

**File: `test/stealth/test_tls_record_padding_target_adversarial.cpp`**

| # | Test Name | Type | Description |
|---|---|---|---|
| 1 | `SmallRecordFrequency_MustBeBelow5Percent` | Adversarial | Over 1000 records with mixed Interactive/Keepalive hints, < 5% of records should be < 200 bytes. This is the key metric — real browsers produce ~3.1%. |
| 2 | `RecordSizeHistogram_NoBucketPeaks` | Adversarial | Chi-squared test: TLS record size distribution should not show peaks at MTProto bucket boundaries (64, 128, 192, 256, 384, 512, 768, 1024, 1280) ± 9 bytes. Each bucket bin count must be < 1.5× expected under uniform in its neighborhood. |
| 3 | `RecordSizeDistribution_KsTestVsCaptureBaseline` | Adversarial | Two-sample K-S test: 1000 mitigated record sizes vs reference browser CDF from PR-S0. Require p > 0.05. This is the DEFINITIVE test — if it fails, the mitigation is insufficient. |
| 4 | `KeepalivePingSize_NotDetectably88Bytes` | Adversarial | 200 keepalive pings must ALL produce records ≥ configured small_record_threshold (200 bytes, or min_payload_cap if higher). |
| 5 | `AuthHandshakeSize_NotDetectablySmall` | Adversarial | Auth handshake records must produce records ≥ min_payload_cap. |
| 6 | `MixedTrafficHints_NoSizeClusteringByHint` | Adversarial | DPI adversary should not be able to separate Interactive from Keepalive records by size alone. Generate 500 of each, compute 2-sample K-S: require p > 0.05 (distributions overlap). |
| 7 | `FrameSizeOverflowPrevented` | Security | Target = INT32_MAX must be clamped to 16384, not cause integer overflow in padding calculation. |
| 8 | `NegativeTargetFailsClosed` | Security | Target = -1 or INT32_MIN must be rejected by validation or clamped to 0. |
| 9 | `SmallRecordBudgetEnforcement` | Adversarial | Artificially exhaust the 5% small-record budget (send 10 tiny messages), then verify next 190 messages ALL get padded above threshold. |
| 10 | `BulkTransferRecordSizesNear16K` | Adversarial | During BulkData hint phase, ≥ 80% of records should be in [12000, 16389] range (matching h2 DATA frames). |
| 11 | `ChiSquaredRejectsUniformDistribution` | Adversarial | Verify that a uniform(0, 16384) distribution IS rejected by the chi-squared test — proves the test actually detects non-browser patterns, not just anything "random." |
| 12 | `RecordSizeSequence_NotDeterministic` | Adversarial | Two connections with same traffic pattern but different RNG seeds must produce statistically distinguishable size sequences (prevents replay fingerprinting). |

**File: `test/stealth/test_tls_record_padding_target_stress.cpp`**

| # | Test Name | Type | Description |
|---|---|---|---|
| 1 | `StressMixedPayloadsAndTargets_NoOOM` | Stress | 50000 writes with random payloads (4-8192 bytes) and random DRS targets (256-16384). No OOM, no assertion failure. |
| 2 | `StressRapidTargetChanges_Stability` | Stress | DRS target changes every write for 10000 writes. All records valid. |
| 3 | `StressSmallRecordBudget_HighLoadNoViolation` | Stress | 50000 writes, all tiny payloads (4-32 bytes). Small-record budget never exceeded. All records padded. |

---

## 9. PR-S3: Post-Handshake Greeting Camouflage

### 9.1. Problem

The first 3-5 TLS Application Data records after the TLS handshake are a strong classifier. Browser HTTP/2 sends a specific sequence:
1. C→S: HTTP/2 connection preface (24 bytes magic) + SETTINGS frame (~50-100 bytes) = ~74-124 bytes, typically padded/coalesced to one TLS record
2. C→S: WINDOW_UPDATE + initial HEADERS frame (~200-800 bytes)
3. S→C: SETTINGS response + WINDOW_UPDATE (~50-100 bytes)
4. S→C: HEADERS + DATA frames (large)

MTProto instead sends:
1. C→S: Encrypted auth key exchange or encrypted first RPC (~88-300 bytes)
2. S→C: Auth response or RPC response

The size sequence and direction pattern are different.

### 9.2. Solution

**Pad the first N outgoing records to match capture-derived "greeting template" size sequences.**

The `StealthTransportDecorator` maintains a `GreetingCamouflage` state machine that:
1. Tracks how many records have been sent since connection establishment
2. For the first `greeting_records` (default: 5) C→S records, overrides the DRS target with sizes from a greeting size template
3. The greeting template is derived from PR-S0 pcap analysis — NOT manually invented

**Greeting size template (capture-derived from `docs/Samples/Traffic dumps/` first-flight analysis):**

The template is per-browser-family (chrome_android, safari_ios, chrome_desktop, firefox_desktop). Example for Chrome h2:

```
Record 1 (C→S): sample from kGreetingRecord1 bins  (HTTP/2 preface + SETTINGS)
Record 2 (C→S): sample from kGreetingRecord2 bins  (WINDOW_UPDATE + HEADERS request)
Record 3 (C→S): sample from kGreetingRecord3 bins  (additional request or SETTINGS ACK)
Record 4 (C→S): sample from kGreetingRecord4 bins  (iff capture has 4+ first-flight)
Record 5 (C→S): sample from kGreetingRecord5 bins  (iff capture has 5+ first-flight)
```

The bins are extracted from real first-flight sequences across all 41 captures by PR-S0's `extract_tls_record_size_histograms.py`. For example, if Chrome Android captures show first C→S records in the range [142, 189, 156, 167, 201], the bin would be `{100, 250, weight=1}`.

**After the greeting phase, the DRS transitions to the active browsing phase** with capture-derived bins. The transition is smooth — the last greeting record's size is used as the seed for DRS's slow_start phase.

### 9.3. Code Changes

**File: `td/mtproto/stealth/StealthConfig.h`**
- Add `GreetingCamouflagePolicy` struct with `std::array<RecordSizeBin, 5> greeting_record_bins` and `uint8 greeting_record_count`

**File: `td/mtproto/stealth/StealthTransportDecorator.cpp`**
- Add `uint8 greeting_records_sent_{0}` counter
- In `pre_flush_write`, when `greeting_records_sent_ < greeting_record_count`, override the DRS target with the greeting template
- After greeting phase completes, seed DRS slow_start with the last greeting size

### 9.4. Test Plan (TDD — tests FIRST)

**File: `test/stealth/test_post_handshake_greeting_camouflage.cpp`**

| # | Test Name | Type | Description |
|---|---|---|---|
| 1 | `FirstRecordMatchesGreetingTemplate` | Positive | First C→S TLS record size matches greeting template bin 0 range. |
| 2 | `SecondRecordMatchesGreetingTemplate` | Positive | Second C→S TLS record size matches greeting template bin 1 range. |
| 3 | `ThirdRecordMatchesGreetingTemplate` | Positive | Third record matches bin 2. |
| 4 | `FourthRecordUsesDrsNotGreeting` | Positive | After greeting phase, DRS takes over. Fourth record size matches DRS policy, not greeting template. |
| 5 | `GreetingDisabledProducesNormalDrsSizes` | Negative | With `greeting_record_count=0`, all records use DRS from the start. |
| 6 | `GreetingSizesSpanBrowserRealisticRange` | Invariant | All greeting template sizes must be within [80, 1500] (realistic h2 first-flight range). |
| 7 | `GreetingRecordCountCapped` | Edge case | `greeting_record_count > 10` should be rejected by config validation. |
| 8 | `GreetingDoesNotAffectIncomingRecords` | Integration | Server→client records are never modified by greeting logic. |

**File: `test/stealth/test_post_handshake_greeting_adversarial.cpp`**

| # | Test Name | Type | Description |
|---|---|---|---|
| 1 | `FirstFlightNotDistinguishableFromH2_KSTest` | Adversarial | Generate 200 connection first-flights. Compare size distributions of record 1/2/3 against known h2 distribution. K-S p-value > 0.05. |
| 2 | `FirstFlightNotAllIdentical` | Adversarial | 100 connections must produce ≥ 20 distinct first-record sizes (randomization works). |
| 3 | `GreetingBypassWhenNoStealthActive` | Security | Without stealth activation, greeting is never applied. |

---

## 10. PR-S4: Bidirectional Size Correlation Defense

### 10.1. Problem

DPI can correlate (request_size, response_size) pairs. MTProto has distinctive patterns:
- Ping (88 bytes) → Pong (88 bytes) → ratio ≈ 1.0
- getHistory (200 bytes) → large response (4000+ bytes) → ratio ≈ 20:1
- sendMessage (300 bytes) → ack (88 bytes) → ratio ≈ 0.3:1

Browser h2 has different ratios:
- GET request (500 bytes) → response (varies widely, 1000-100000+)
- AJAX/API calls: more symmetric, but typically 200-1000 → 500-5000

### 10.2. Solution

This is partially addressed by PR-S1 (random padding removes exact size correspondence) and PR-S2 (record padding smears sizes toward DRS targets). Additional mitigation:

1. **Response padding awareness**: When the `StealthTransportDecorator` receives a small server→client response, note the size for telemetry. The DRS engine can be made aware of bidirectional flow to select target sizes that reduce correlation.

2. **Inject response delay jitter**: Vary the time between sending a request and the next client write after receiving the response, breaking timing-based correlation.

This PR is lighter-touch — it mainly adds **monitoring and testing** to quantify the bidirectional correlation after PR-S1+PR-S2 are applied.

### 10.3. Test Plan (TDD — tests FIRST)

**File: `test/stealth/test_bidirectional_size_correlation.cpp`**

| # | Test Name | Type | Description |
|---|---|---|---|
| 1 | `PingPongSizesNotIdenticalAfterPadding` | Adversarial | After PR-S1+S2, a 4-byte ping and 4-byte pong must produce different TLS record sizes ≥ 80% of the time. |
| 2 | `RequestResponseRatioSpread` | Adversarial | For 500 simulated request-response pairs of varying inner sizes, the (req_record_size / resp_record_size) ratio must have variance > 0.2 (not tightly clustered). |
| 3 | `SmallRequestLargeResponseNotCorrelated` | Adversarial | Send 100 small requests. Verify the TLS record sizes of the requests do not statistically predict the response sizes (Pearson r < 0.3). |

---

## 11. PR-S5: Idle Connection Chaff Traffic

### 11.1. Problem

During idle periods, MTProto connections show one of two patterns:
1. Complete silence (no TLS records for 30+ seconds)
2. Periodic keepalive pings at regular intervals

Browser h2 connections show:
1. HTTP/2 PING frames at semi-regular intervals (typically 15-30 second period)
2. Occasional SETTINGS updates
3. Server push notifications
4. The record sizes for h2 PING/PONG are small but consistent

The absence of any traffic, or the precisely-timed small keepalive pattern, is a detectable anomaly.

### 11.2. Solution

**Add a chaff traffic generator to `StealthTransportDecorator`** that, during idle periods:

1. Injects dummy IntermediateTransport frames at intervals sampled from IPT idle distribution
2. **Chaff frame sizes are sampled from the capture-derived `kIdleChaffBins`** (PR-S0) — these are extracted from actual browser idle periods observed in the 41 pcap captures. Real browsers send h2 PING frames of 17-50 bytes in TLS records of ~50-100 bytes, but with our IntermediateTransport padding (PR-S2), chaff records are padded to the idle chaff bin range (typically 200-800 bytes, matching the padded browser idle records in the reference captures).
3. The chaff frames contain random encrypted padding that the receiver silently discards (they are valid IntermediateTransport frames with padding-only payload)

**Implementation constraint:** Chaff must use existing protocol mechanisms. The simplest approach is to have the `Session` layer periodically send MTProto ping/pong or HTTP-header-like keepalive messages (which are valid MTProto operations) during idle periods, with the stealth transport padding them to browser-realistic sizes.

Alternatively, the chaff can be injected purely at the transport level by writing zero-content IntermediateTransport frames (a frame with just a 4-byte header of 0x00000000 means "0 bytes of MTProto payload" — the server reads 0 bytes and continues). — **Need to verify this is valid; if the server rejects zero-length frames, use minimal valid MTProto ping messages instead.**

### 11.3. Code Changes

**File: `td/mtproto/stealth/ChaffScheduler.h`**
- New class that schedules dummy writes during idle periods
- Configurable via `ChaffPolicy` in `StealthConfig`

**File: `td/mtproto/stealth/StealthConfig.h`**
- Add `ChaffPolicy` struct with idle detection threshold, send interval distribution, record size bins

**File: `td/mtproto/stealth/StealthTransportDecorator.cpp`**
- Integrate `ChaffScheduler` into `pre_flush_write()`: if both rings are empty and idle > threshold, schedule chaff write

### 11.4. Test Plan (TDD — tests FIRST)

**File: `test/stealth/test_idle_chaff_traffic.cpp`**

| # | Test Name | Type | Description |
|---|---|---|---|
| 1 | `ChaffInjectedAfterIdleThreshold` | Positive | After 5 seconds of no writes, chaff record appears within next IPT cycle. |
| 2 | `ChaffRecordSizeMatchesPolicy` | Positive | Chaff record sizes match the configured chaff size bins. |
| 3 | `ChaffStopsDuringActiveTraffic` | Positive | When real MTProto messages are queued, no chaff is injected. |
| 4 | `ChaffDisabledByPolicyProducesNoTraffic` | Negative | With chaff disabled in config, idle period produces no records. |
| 5 | `ChaffTimingFollowsIptDistribution` | Adversarial | 200 chaff intervals must be statistically consistent with IPT idle distribution (K-S test). |
| 6 | `ChaffDoesNotBreakMtprotoReceiver` | Integration | Server can continue processing real messages after receiving chaff frames. |
| 7 | `ChaffBandwidthBudgetRespected` | Resource | Over 60 seconds idle, total chaff bytes ≤ configured max_chaff_bytes_per_minute. |
| 8 | `ChaffNotInjectedWhenConnectionClosing` | Edge case | During connection teardown, no chaff is scheduled. |

**File: `test/stealth/test_idle_chaff_traffic_adversarial.cpp`**

| # | Test Name | Type | Description |
|---|---|---|---|
| 1 | `IdleSilencePatternEliminatedByChaffOverDpiWindow` | Adversarial | Simulate DPI observing inter-record gaps. With chaff, no gap exceeds the h2 PING deadline × 2. |
| 2 | `ChaffFloodProtection_MaxRateEnforced` | Security | Hostile config with min_interval=0 must be rejected (prevents self-DoS). |

---

## 12. PR-S6: Inter-Record Size Pattern Hardening

### 12.1. Problem

Within a TLS session, the sequence of record sizes follows different patterns for different protocols. An HMM-based classifier (see `docs/Researches/HMM-Stanford.pdf`) can model state transitions to distinguish MTProto from h2.

### 12.2. Solution

This is largely mitigated by PR-S1 through PR-S5 combined with the existing DRS phase model. The remaining hardening:

1. **DRS phase parameters tuned against h2 capture baselines**: Verify that the slow_start → congestion_open → steady_state transition produces a size sequence statistically similar to real h2 downloads.
2. **Anti-monotonicity guard**: Ensure the DRS doesn't produce monotonically increasing or decreasing size sequences (which h2 doesn't exhibit).

### 12.3. Test Plan (TDD — tests FIRST)

**File: `test/stealth/test_record_size_sequence_hardening.cpp`**

| # | Test Name | Type | Description |
|---|---|---|---|
| 1 | `SequenceNotMonotonicallyIncreasing` | Adversarial | Over 100 records after slow_start, ≥ 30% of consecutive pairs must be (larger, smaller) — not monotonically growing. |
| 2 | `SequenceAutoCorrelationLow` | Adversarial | Lag-1 autocorrelation of record sizes < 0.5 (sizes are not highly predictable from previous size). |
| 3 | `PhaseTransitionNotAbrupt` | Adversarial | When DRS transitions from slow_start to congestion_open, the size change between boundary records is < 3× (no sudden 1400→4000 jump). |
| 4 | `SteadyStateNotFixedSize` | Adversarial | In steady_state, coefficient of variation of record sizes > 0.2 (real h2 has substantial size variation). |

---

## 13. PR-S7: Stealth Connection Count Cap

### 13.1. Problem

This is arguably the **most severe** DPI fingerprint of the entire system, and the cheapest to detect.

**The math:**

| Session Type | Sessions (non-prem) | ×2 conns each | Sessions (premium) | ×2 conns each |
|---|---|---|---|---|
| main_session | 1 | 2 | 1 | 2 |
| upload_session | 4 | 8 | 8 | 16 |
| download_session | 2 | 4 | 8 | 16 |
| download_small_session | 2 | 4 | 8 | 16 |
| **Total to same proxy** | | **18** | | **50** |

**Browser norms:**
- HTTP/2: **1 connection** per origin (multiplexed)
- HTTP/1.1: **6 connections** per origin (Chrome default, de facto standard)
- Edge cases: some CDN scenarios open 2-3 h2 connections, but never >6 total from one browser to one origin

A DPI rule of `concurrent_tcp_to_same_ip_port > 8 ? flag_as_anomalous` catches MTProto with near-zero false positives against real browser traffic. This is a **single counter** — cheaper than any ML model.

### 13.2. ALPN Analysis: h2 vs http/1.1 vs Both

The current code already has the right ALPN split:
- **Proxy path** (stealth mode): `http/1.1` only — correct
- **Direct path**: `h2, http/1.1` — correct for browser mimicry

The user raised the question of advertising `h2` alongside `http/1.1` in proxy mode. Analysis:

| ALPN Choice | Pros | Cons |
|---|---|---|
| `http/1.1` only | Multiple connections expected (up to 6). Matches captured traffic from real Android Chrome to proxied destinations. | Changes JA4 fingerprint vs browser default. Some profiles (desktop Chrome) always offer h2 first. |
| `h2` + `http/1.1` | Matches default browser ClientHello ALPN. DPI can't see server's choice (TLS 1.3 encrypts EncryptedExtensions). | If DPI assumes h2 was negotiated, it expects 1 connection. Even under charitable h1.1 assumption, 18+ connections still anomalous. Doesn't fix the core problem. |
| `h2` only | Strongest browser match for desktop profiles. | Mandates 1 connection per origin — impossible with current architecture without massive rework. |

**Recommendation:** Keep `http/1.1` only for proxy, but this is a **second-order concern** compared to the connection count itself. Real Android Chrome captures in the corpus *already* show `http/1.1` only ALPN to proxy destinations, so it's a validated real-world pattern. The critical fix is the connection count.

For desktop profiles (Chrome/Firefox on Linux/macOS/Windows), the ALPN should be `h2, http/1.1` (browser default) because:
1. Desktop browsers always offer h2
2. In TLS 1.3, the server's ALPN choice is in EncryptedExtensions (encrypted), so DPI can't tell if h2 or http/1.1 was negotiated
3. The connection count cap (see below) makes either interpretation plausible

**Tentative policy:**
```
if (profile.is_mobile()) → ALPN = http/1.1 only    (matches real mobile proxy captures)
if (profile.is_desktop()) → ALPN = h2, http/1.1     (matches real desktop browser default)
```
This should be validated against the fixture corpus before implementation.

### 13.3. Solution: Connection Multiplexing or Session Consolidation

When stealth mode is active (`emulate_tls()` + stealth runtime policy), cap the total concurrent TCP connections to the same destination (proxy IP:port) to **a browser-realistic limit**.

**Option A: Hard session count cap (simplest, recommended for MVP)**

Override session counts in `NetQueryDispatcher::ensure_dc_inited()` when stealth is active:

```
stealth mode session counts:
  main_session_count     = 1  (→ 2 TCP connections)
  upload_session_count   = 1  (→ 2 TCP connections)
  download_session_count = 1  (→ 2 TCP connections)
  download_small_session_count = 0  (merged into download)
  
  Total: 6 TCP connections (matches http/1.1 browser norm exactly)
```

This sacrifices upload/download parallelism for stealth. With DRS/IPT shaping already active, TLS record multiplexing within each connection compensates somewhat.

**Option B: Dynamic session scaling (more complex, better perf)**

Start with 2 connections (main + long-poll), scale up to max 6 under load. Mirror how browsers open h1.1 connections: lazily, one at a time, as concurrent requests require it.

**Option C: True h2-style multiplexing (most complex, best stealth)**

Implement an internal multiplexing layer that routes all session types through **1-2** underlying TCP connections, similar to how h2 multiplexes streams. This is a major architectural change but would make the connection pattern indistinguishable from real h2 traffic.

**Recommendation: Option A for MVP, Option B as follow-up.**

### 13.4. Code Changes

**File: `td/telegram/net/NetQueryDispatcher.cpp`**
- In `ensure_dc_inited()`: when stealth connection policy is active, override session counts
- Read stealth policy from `G()->get_option_boolean("stealth_shaping_active")` or a dedicated accessor

**File: `td/mtproto/stealth/StealthConfig.h`**
- Add `ConnectionPoolPolicy` with `max_total_connections_per_destination` (default: 6)

**File: `td/telegram/net/ConnectionCreator.cpp`**
- Enforce total connections per destination via `ConnectionDestinationBudgetController`
- When budget is full, queue the connection request instead of opening a new socket

**File: `td/telegram/net/SessionMultiProxy.cpp`**
- Accept dynamic session count limits from stealth policy

### 13.5. Test Plan (TDD — tests FIRST)

**File: `test/stealth/test_stealth_connection_count_cap.cpp`**

| # | Test Name | Type | Description |
|---|---|---|---|
| 1 | `StealthModeCapsTotalConnectionsTo6` | Positive | With stealth active, total concurrent TCP connections to same destination ≤ 6. |
| 2 | `NonStealthModeAllowsFullSessionCount` | Negative | Without stealth, original session counts (18/50) are preserved. |
| 3 | `ConnectionCapDoesNotPreventAuth` | Integration | Auth handshake completes successfully with reduced session count. |
| 4 | `MainAndLongPollAlwaysAvailable` | Integration | main_connection_ and long_poll_connection_ for the main session are never starved by the cap. |
| 5 | `UploadStillWorksWithReducedSessions` | Integration | File upload completes (albeit potentially slower) with 1 upload session. |
| 6 | `DownloadStillWorksWithReducedSessions` | Integration | Media download completes with 1 download session. |
| 7 | `ConnectionCapAppliesToSingleProxy` | Positive | Cap is per-destination, not global. Different proxies can each have up to 6. |
| 8 | `ConnectionCapDoesNotApplyToDirectDc` | Edge case | Direct DC connections (no proxy) are not capped (they go to different IPs). |

**File: `test/stealth/test_stealth_connection_count_adversarial.cpp`**

| # | Test Name | Type | Description |
|---|---|---|---|
| 1 | `DpiConcurrentConnectionCounter_StealthUnder8` | Adversarial | Simulate DPI counter: at no point during a 60-second session do concurrent connections to same dest exceed 8. |
| 2 | `ConnectionOpeningCadence_NotBursty` | Adversarial | Connections don't all open simultaneously (stagger over 2+ seconds, like real browser h1.1 lazy opening). |
| 3 | `PremiumUserStillCappedInStealth` | Adversarial | Premium accounts (which normally get 50 connections) are still capped at 6 in stealth. |
| 4 | `SessionCountRecoveryAfterStealthDisable` | Edge case | If stealth is deactivated mid-session, sessions can scale back up to normal counts. |

### 13.6. ALPN Coherence Test Plan

**File: `test/stealth/test_stealth_alpn_connection_coherence.cpp`**

| # | Test Name | Type | Description |
|---|---|---|---|
| 1 | `MobileProfileUsesHttp11OnlyAlpn` | Positive | Mobile profiles (IOS14, Android11_OkHttp) always advertise `http/1.1` only when going through proxy. |
| 2 | `DesktopProfileUsesH2Http11Alpn` | Positive | Desktop profiles (Chrome*, Firefox*) advertise `h2, http/1.1` when through proxy. |
| 3 | `ConnectionCountCoherentWithAlpn_Http11` | Adversarial | When ALPN is `http/1.1` only, connections ≤ 6 (http/1.1 browser norm). |
| 4 | `ConnectionCountCoherentWithAlpn_H2` | Adversarial | When ALPN includes `h2`, connections ≤ 6 (plausible: server may have chosen http/1.1 fallback, or up to 2 h2 connections for connection coalescing). |
| 5 | `AlpnChoiceDoesNotBreakJa4` | Regression | Changing ALPN for desktop proxy mode does not produce a JA4 that differs from the corresponding browser profile's known JA4 family. |

---

## 14. Test Strategy

### 14.1. Test File Organization

All tests in separate files under `test/stealth/`:

```
test/stealth/
  test_stealth_record_size_baselines.cpp                    [PR-S0]
  test_mtproto_crypto_padding_bucket_elimination.cpp       [PR-S1]
  test_mtproto_crypto_padding_bucket_elimination_fuzz.cpp   [PR-S1]
  test_tls_record_padding_target.cpp                        [PR-S2]
  test_tls_record_padding_target_adversarial.cpp            [PR-S2]
  test_tls_record_padding_target_stress.cpp                 [PR-S2]
  test_post_handshake_greeting_camouflage.cpp               [PR-S3]
  test_post_handshake_greeting_adversarial.cpp              [PR-S3]
  test_bidirectional_size_correlation.cpp                    [PR-S4]
  test_idle_chaff_traffic.cpp                               [PR-S5]
  test_idle_chaff_traffic_adversarial.cpp                   [PR-S5]
  test_record_size_sequence_hardening.cpp                   [PR-S6]
  test_stealth_connection_count_cap.cpp                     [PR-S7]
  test_stealth_connection_count_adversarial.cpp             [PR-S7]
  test_stealth_alpn_connection_coherence.cpp                [PR-S7]
```

Python analysis tests in `test/analysis/`:

```
test/analysis/
  test_extract_tls_record_size_histograms.py                [PR-S0]
  test_check_record_size_distribution.py                    [PR-S0]
```

### 14.2. Test Philosophy

**Think like a black hat DPI researcher with ₽84B budget per TSPU:**

1. **Statistical hypothesis tests** (chi-squared, K-S, autocorrelation, Pearson correlation) — the adversary has ML classifiers trained on millions of flows. A test that "looks random" to a human developer can still be classified with >99% accuracy by a gradient-boosted tree. The ONLY reliable test is statistical comparison against the actual reference browser corpus.
2. **Edge cases** — the adversary will probe boundary conditions (min/max sizes, empty payloads, oversized payloads, INT32_MAX, zero-length frames).
3. **Timing-size correlation** — the adversary jointly models (size, inter-record-gap) pairs. A record of size 88 arriving after a 30-second gap screams "MTProto keepalive." Timing tests (existing IPT suite) and size tests (this plan) must be validated **jointly**, not in isolation.
4. **Cross-connection fingerprinting** — the adversary looks at patterns across multiple concurrent connections from the same client IP (addressed by PR-S7 + existing connection diversity tests).
5. **Phase detection** — sophisticated adversaries detect protocol phase transitions (handshake → auth → chat → idle). Our connection lifecycle state machine must produce phase transitions that resemble real h2 (addressed by DRS phase model + greeting camouflage).
6. **Fail-open detection** — the adversary specifically looks for flows where mitigation failed to activate. One unmasked keepalive in 1000 flows is enough to confirm MTProto presence for that client IP. All error paths must fail-closed.
7. **Synthetic distribution detection** — ML classifiers can distinguish synthetic distributions (uniform, Gaussian, log-normal) from real browser traffic. This is why capture-driven sampling is essential — it is the only approach that survives this test.

### 14.3. Statistical Test Parameters

For all chi-squared and K-S tests:
- **Significance level:** α = 0.01 (1% false-positive rate) for hypothesis tests
- **Threshold for acceptance:** p > 0.05 (conservative — allows some deviation due to finite sample)
- **Sample size:** ≥ 200 records per test (statistical power > 0.95 against effect size d = 0.3)
- **Baseline distributions:** From capture-derived CDFs (PR-S0 output), NOT from manually specified expected values
- **Multi-test correction:** When running multiple statistical tests on the same data, apply Holm-Bonferroni correction to avoid false discoveries

### 14.4. Build Integration

All new C++ test files added to `test/CMakeLists.txt` under `run_all_tests` target. Each test registered individually with CTest for isolated execution.

Python analysis tests run via:
```bash
python3 -m pytest test/analysis/test_extract_tls_record_size_histograms.py
python3 -m pytest test/analysis/test_check_record_size_distribution.py
```

Corpus validation integrates with existing smoke:
```bash
python3 test/analysis/run_corpus_smoke.py \
  --registry test/analysis/profiles_validation.json \
  --fixtures-root test/analysis/fixtures/clienthello \
  --server-hello-fixtures-root test/analysis/fixtures/serverhello \
  --record-size-fixtures-root test/analysis/fixtures/record_sizes
```

---

## 15. Residual Risks & Non-Goals

### 15.1. Accepted Residual Risks

| Risk | Severity | Rationale for acceptance |
|---|---|---|
| **L7 ALPN mismatch (S22)** | HIGH | MTProto advertises h2/http1.1 in ALPN but sends binary MTProto, not HTTP/2. This is detectable by Active DPI performing protocol conformance checking (sending h2 SETTINGS to the server and analyzing the non-conforming response). Fixing this requires full h2 framing or h2 CONNECT tunneling, which is out of scope for this plan. |
| **Server-side record sizes** | MEDIUM | We control only client→server record sizes. Server→client record sizes depend on the MTProto proxy server implementation (telemt). Server-side mitigations must be implemented in the proxy. |
| **SNI-based blocking** | MEDIUM | Domain fronting / SNI-based blocking is orthogonal to packet-size analysis. Addressed by existing TLS profile masking and ECH. |
| **TCP-level anomalies** | LOW | TCP window sizes, MSS, timestamps — some of these leak information. Out of scope; they are OS-level, not application-controllable. |
| **Pcap corpus staleness** | MEDIUM | The capture-driven baselines will become stale as browsers update their h2 behavior. Periodic refresh of the 41-pcap corpus (quarterly) is required to maintain CDF accuracy. |

### 15.2. Non-Goals

- **Perfect indistinguishability**: 100% undetectable is impossible. Goal is to raise the cost of detection to the point where it causes unacceptable false positive rates on legitimate HTTPS.
- **Application-level TCP fragmentation**: Per plan v6 audit and GoodbyeDPI analysis, TCP-level fragmentation is not adopted as a default strategy (TSPU reassembles TCP streams).
- **Single-socket enforcement**: Forced single-connection creates its own anomaly (h2 with multiple connections is plausible). Not adopted.
- **Payload entropy tampering**: Intentionally reducing ciphertext entropy (zero-padding) is dangerous and unnecessary for AES-CTR encrypted transport. A DPI adversary detecting zero-padded regions within ciphertext would have a stronger signal than the original size fingerprint.
- **QUIC/HTTP3 imitation**: QUIC is blocked RU→non-RU (confirmed by GoodbyeDPI's `-q` flag dropping QUIC locally). Not a viable strategy.
- **Purely synthetic size generation**: Random walks, Markov chains for sizes, uniform distributions, Gaussian distributions — all are detectable by ML classifiers. NOT adopted. Only capture-driven sampling used.

---

## 16. Success Criteria

### 16.1. Quantitative Metrics (Statistical Quality Gates)

| Metric | Threshold | Measurement Method | Reference Data |
|---|---|---|---|
| **Two-sample K-S test: mitigated MTProto vs browser corpus** | p > 0.05 | 1000 mitigated record sizes vs reference CDF | PR-S0 aggregate browser CDF |
| **Chi-squared goodness-of-fit** | p > 0.05 | 50-byte bins over 1000 records | PR-S0 capture-derived histogram |
| **Bucket quantization detector** | No bin excess > 1.5× expected | Check MTProto bucket boundaries ± 16 bytes | {64, 128, 192, 256, 384, 512, 768, 1024, 1280} + 9 overhead |
| **Small-record frequency (< 200 bytes)** | < 5% | Rolling window over 1000 records | Browser baseline: ~3.1% (from PR-S0 pcap analysis) |
| **Fraction of records > 12000 bytes in bulk phase** | > 20% | During BulkData hint traffic | Browser baseline: ~60-80% during downloads |
| **First-flight K-S test vs browser greeting sizes** | p > 0.05 | 200 simulated connection starts, first 5 records each | PR-S0 first-flight size sequences |
| **Lag-1 autocorrelation of record sizes** | \|r\| < 0.4 | Over 500 consecutive records in active phase | |
| **Phase transition smoothness** | Adjacent record size ratio < 3.0× | At all phase boundaries | |
| **Concurrent TCP connections to same proxy** | ≤ 6 | At any instant during normal operation with stealth active | |
| **Max idle gap with chaff active** | < 45 seconds | Over 10 minutes of simulated idle | Browser PING period: 15-30 seconds |
| **Keepalive record sizes** | All ≥ min(small_record_threshold, 200) bytes | 200 keepalive pings | |
| **Ping/pong size correlation (bidirectional)** | Pearson r < 0.3 | 500 request-response pairs | |

### 16.2. Qualitative Criteria

- All existing stealth tests continue to pass (no regression)
- All new tests pass (green after code changes)
- No new compilation warnings
- No hardcoded magic numbers — all DRS bin parameters derived from PR-S0 capture analysis
- Fail-closed on all error paths (no silent degradation to unmasked traffic)
- OWASP ASVS L2 compliance maintained (constant-time comparisons, no secret leaks, proper RNG usage)
- Python analysis tests pass with the full 41-pcap corpus

### 16.3. Release Gate

**C++ tests:**
```bash
cmake --build build --target run_all_tests --parallel 4
./build/test/run_all_tests --filter "MtprotoCryptoPadding\|TlsRecordPadding\|PostHandshakeGreeting\|BidirectionalSize\|IdleChaff\|RecordSizeSequence\|StealthConnection\|AlpnConnection\|StealthRecordSizeBaseline"
```

**Python statistical validation:**
```bash
python3 -m pytest test/analysis/test_extract_tls_record_size_histograms.py -v
python3 -m pytest test/analysis/test_check_record_size_distribution.py -v
python3 test/analysis/run_corpus_smoke.py \
  --registry test/analysis/profiles_validation.json \
  --fixtures-root test/analysis/fixtures/clienthello \
  --server-hello-fixtures-root test/analysis/fixtures/serverhello \
  --record-size-fixtures-root test/analysis/fixtures/record_sizes
```

**All tests green → ready for merge.** Any K-S or chi-squared failure is a **hard block** — it means the adversary can distinguish our traffic from real browsing. No exceptions, no threshold relaxation without analysis of the specific failure mode.
