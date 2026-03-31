# Low-Latency Market Data Capture with Hardware-Level Timestamping on AWS

> A feed handler instrumentation layer that makes the invisible cost of market data processing visible — measuring per-packet stack latency from NIC arrival to application receipt, enabling latency optimization and accurate cross-venue event ordering.

![Architecture](images/architecture.png)

*Designed and implemented independently as part of IE421 High Frequency Trading, UIUC.*

---

## Why

**You cannot optimize what you cannot measure. And in HFT, you cannot measure latency correctly without kernel-level timestamps.**

Every standard WebSocket market data implementation timestamps messages *after* TLS decryption and WebSocket framing. The result is a timestamp corrupted by:

- Kernel TCP queue wait
- TLS record decryption (~1–10µs, variable)
- WebSocket frame reassembly
- Linux scheduler wake-up jitter (10–500µs on EC2 under load)

This means when you record "I received this orderbook update at T," you're actually recording "I finished processing this update at T." The difference — the real cost of your pipeline — is invisible.

**What most engineers do instead:** `clock_gettime()` after `ws.read()`. This is the wrong timestamp by definition. On a loaded EC2 instance, the error is 50–500µs. You can't correct for it because you don't know how large it is on any given packet.

---

## How

Inject a `TimestampAwareStream` proxy **below TLS** — between the raw TCP socket and `beast::ssl_stream`. It replaces `read_some()` with `recvmsg()`, which retrieves the `SO_TIMESTAMPING` ancillary data the kernel attached to the packet at NIC arrival.

```
Standard path:
  NIC arrival (T0) ──→ TCP ──→ TLS decrypt ──→ WS framing ──→ app timestamps here (T4)
                                                               ↑ error = T4 - T0, unknown

This project:
  NIC arrival ──→ kernel stamps T0 ──→ TCP ──→ [proxy reads T0] ──→ TLS ──→ WS ──→ app (T4)
                                                                    ↑ T0 now known
  T4 - T0 = your stack processing cost, measured per packet
```

**Key distinction:** T0 is stamped by the NIC hardware (or kernel softirq) at packet arrival — before any CPU involvement. `TimestampAwareStream` doesn't generate T0; it *retrieves* the pre-recorded value via the kernel's CMSG interface. This is what makes it more accurate than any application-level measurement.

---

## What

A C++17 instrumentation layer for Bybit's v5 WebSocket feed that produces:

- **`T0`** — kernel/NIC RX timestamp, per packet, via `SO_TIMESTAMPING` + `recvmsg()`
- **`T4`** — application receive time, via pluggable `TimeSource` (PTP or `system_clock`)
- **`T4 − T0`** — your stack processing cost: TLS + WebSocket framing + scheduler delay
- **`T0 − exchange_ts`** — observed network latency from exchange publish to your NIC

---

## What This Enables

### 1. Latency Budget Profiling
Without T0, you know your system is "slow" but not where. With T0, you can instrument every stage:

```cpp
T0 = ts_proxy.get_last_ts_ns();   // NIC arrival
T1 = time_source->now_ns();       // after JSON parse
T2 = time_source->now_ns();       // after signal calc
T4 = time_source->now_ns();       // before order send

// Now you know: is TLS the bottleneck? JSON parser? Scheduler jitter?
```

This is the prerequisite to any meaningful latency optimization — kernel bypass (DPDK), CPU pinning, IRQ affinity, or simply reducing allocations.

### 2. Accurate Cross-Venue Event Ordering
When running two feeds simultaneously, T4 ordering can be wrong because decode time differs per feed:

```
Binance T0 = 100µs  →  T4 = 420µs  (320µs decode)
Bybit   T0 = 380µs  →  T4 = 510µs  (130µs decode)

T4 says: Binance arrived 90µs first   ← wrong
T0 says: Binance arrived 280µs first  ← correct
```

For cross-venue lead-lag strategies, T4-based ordering gives you the wrong signal. T0-based ordering gives you the ground truth of which price move arrived at your NIC first.

### 3. Exchange Health Monitoring
`T0 − exchange_ts` measures the latency from Bybit's matching engine to your NIC. When this spikes, the exchange is congested — typically before it's visible in fill rates or order rejections. Useful for:
- Pausing market making during exchange-side delays
- Comparing co-location region performance (AP vs US servers)
- Detecting feed degradation before it affects P&L

### 4. Quote Staleness Signal
```cpp
if (T4 - T0 > staleness_threshold_ns) {
    // This packet sat in the kernel longer than expected.
    // The orderbook state it carries may be stale relative
    // to what a faster competitor already acted on.
    // Widen spread or skip.
}
```

---

## Architecture

### Before: Standard Stack

```
[ WebSocket (Boost.Beast) ]   ← T4: message timestamped here (too late)
         ↑
[ SSL/TLS (OpenSSL)       ]   ← variable decode time folded into "latency"
         ↑
[ TCP Socket              ]   ← scheduler jitter folded in here too
         ↑
[ NIC / vNIC (AWS EC2)    ]   ← T0: actual arrival — never recorded
```

### After: Enhanced Stack with Proxy

```
[ WebSocket (Boost.Beast) ]   ← T4: application receive time
         ↑
[ SSL/TLS (OpenSSL)       ]
         ↑
[ TimestampAwareStream    ]   ← recvmsg() retrieves T0 from kernel CMSG
         ↑                       T0 already stamped at NIC — proxy just reads it
[ TCP Socket              ]
         ↑
[ NIC / vNIC (AWS EC2)    ]   ← T0: stamped here by HW or kernel softirq
```

**On AWS EC2:** Most ENA instances provide kernel software timestamps (`ts[0]`, softirq time). `c6in`/`m6in`/`r6in` Nitro instances provide `SOF_TIMESTAMPING_RX_HARDWARE` (`ts[2]`, NIC hardware time). The proxy requests hardware first and falls back silently — `ts[2].tv_sec == 0` at runtime is the reliable indicator, not `setsockopt` return code.

---

## Repository Structure

```
├── ProxyTemplate/
│   ├── TimestampAwareStream.hpp     # Core: SO_TIMESTAMPING proxy, Boost.Beast SyncReadStream
│   └── Guideline for using proxy.txt
│
├── TCP test/
│   ├── TcpClient.cpp                # Validates SO_TIMESTAMPING on raw TCP (no WS overhead)
│   └── TcpReceiver.cpp              # Prints ts[0]/ts[1]/ts[2] per packet
│
├── TimeSource.h                     # Interface: now_ns() → nanoseconds since Unix epoch
├── PtpTimeSource.h / .cpp           # PTP: /dev/ptp_ena (AWS Nitro PHC, ~100ns accuracy)
├── SystemClockTimeSource.h / .cpp   # Fallback: std::chrono::system_clock
├── TimeSourceFactory.h / .cpp       # Runtime clock selection
│
├── RawFileLogger.h / .cpp           # Buffered append logger, flush every N lines
└── bybit_orderbook.cpp              # Bybit v5 WebSocket client (Boost.Beast + OpenSSL)
```

---

## `TimestampAwareStream` — Technical Detail

Implements Boost.Beast's `NextLayer` / `SyncReadStream` concept. Injected between `tcp::socket` and `beast::ssl_stream` — zero changes to TLS or WebSocket layers above it.

```cpp
tcp::socket                          raw_socket(ioc);
TimestampAwareStream<tcp::socket>    ts_proxy(raw_socket);  // injected here
beast::ssl_stream<TimestampAwareStream<tcp::socket>&> ssl(ts_proxy, ctx);
ws::stream<...>                      ws_stream(ssl);

ws_stream.read(buffer);
long long t0_ns = ws_stream.next_layer().next_layer().get_last_ts_ns();
long long t4_ns = time_source->now_ns();
long long stack_latency_ns = t4_ns - t0_ns;
```

Constructor enables `SOF_TIMESTAMPING_RX_HARDWARE | SOF_TIMESTAMPING_RAW_HARDWARE | SOF_TIMESTAMPING_SOFTWARE`. Each `read_some()` calls `recvmsg()` and parses the CMSG chain (`ts[2]` raw HW → `ts[0]` kernel SW fallback).

---

## Technology Stack

| Layer | Technology |
|---|---|
| Language | C++17 |
| WebSocket / Async I/O | Boost.Beast / Boost.Asio |
| TLS | OpenSSL (TLS 1.3) |
| Packet Timestamping | `SO_TIMESTAMPING` + `recvmsg()` |
| Hardware Clock | PTP `/dev/ptp_ena` (AWS Nitro PHC) |
| Infrastructure | AWS EC2 ENA — `c6in`/`m6in` for hardware timestamps |
| Data Source | Bybit v5 WebSocket API |
| Storage | NoSQL (per-event: `arrival_ts`, `app_ts`, `exchange_ts`) |

---

## Skills Demonstrated

| Skill | Where |
|---|---|
| Linux kernel networking | `SO_TIMESTAMPING`, `recvmsg()`, CMSG layout (`ts[0]/ts[2]`), softirq vs HW timestamp |
| C++ template design | `TimestampAwareStream<NextLayer>` — Boost.Beast stream concept as transparent wrapper |
| HFT infrastructure | Latency budget decomposition, cross-venue ordering, feed handler instrumentation |
| AWS systems | ENA enhanced networking, Nitro PHC, instance-type timestamp capability matrix |
| Software architecture | Strategy pattern (TimeSource), factory, layered stream composition |

---

## Build

**Linux / AWS EC2**
```bash
g++ -std=c++17 bybit_orderbook.cpp \
  -lboost_system -lssl -lcrypto -lpthread -O2 -o bybit_ws
```

**macOS (dev)**
```bash
clang++ -std=c++17 bybit_orderbook.cpp \
  -I/opt/homebrew/include \
  -I/opt/homebrew/opt/openssl@3/include \
  -L/opt/homebrew/opt/openssl@3/lib \
  -lssl -lcrypto -lpthread -O2 -o bybit_ws
```

---

## Status

| | Item |
|---|---|
| ✅ | Bybit v5 WebSocket client (SSL + Boost.Beast) |
| ✅ | `TimestampAwareStream` — `SO_TIMESTAMPING` proxy, HW→SW fallback |
| ✅ | `TimeSource` — PTP + SystemClock with factory |
| ✅ | `RawFileLogger` — buffered file writer |
| ✅ | TCP test harness — `ts[0]/ts[2]` extraction validated on raw socket |
| 🔄 | Wire proxy into async WebSocket client (`async_read_some` wrapper) |
| ⬜ | Orderbook model (L1/L50 snapshot + delta) |
| ⬜ | Orderbook / tick handlers with `arrival_ts` |
| ⬜ | NoSQL flush pipeline |
