# Low-Latency Market Data Capture with Hardware-Level Timestamping on AWS

> **Design goal:** Capture the exact kernel-level arrival time of every market data packet — before any application-layer processing — by intercepting the TCP read path with a custom `SO_TIMESTAMPING` proxy.

![Architecture](images/architecture.png)

---

## The Problem

In a standard WebSocket-based market data feed, the application timestamps a message **after** it has been decrypted and framed — introducing hundreds of microseconds of unmeasurable, variable latency from:

- Kernel TCP queue wait
- TLS record decryption
- WebSocket frame assembly

For HFT strategies that rely on precise event ordering (e.g., cross-venue arbitrage, latency-adjusted signal weighting), this latency is not just large — it's **unknowable**. You can't correct for what you can't measure.

---

## The Solution

Insert a **`TimestampAwareStream`** proxy between the raw TCP socket and the TLS layer. It wraps the socket's `read_some` calls with `recvmsg()`, extracting the kernel-attached `SO_TIMESTAMPING` ancillary data for each packet.

This gives a **per-packet kernel RX timestamp** at the earliest observable point in the software stack — before decryption, before framing, before any user-space logic runs.

---

## Architecture

### Standard Stack (Before)

```
[ WebSocket (Boost.Beast) ]   ← application sees message here (T4)
         ↑
[ SSL/TLS (OpenSSL)       ]   ← decrypts TLS records
         ↑
[ TCP Socket (Linux)      ]   ← packets queued here
         ↑
[ NIC / vNIC (AWS EC2)    ]   ← packet arrives here (T0, unmeasured)
```

### Enhanced Stack with Proxy (After)

```
[ WebSocket (Boost.Beast) ]   ← T4 (application receive time)
         ↑
[ SSL/TLS (OpenSSL)       ]   ← decrypts TLS records
         ↑
[ TimestampAwareStream    ]   ← T0: recvmsg() extracts kernel RX timestamp
         ↑                         stored per-read, accessible via get_last_ts_ns()
[ TCP Socket (Linux)      ]
         ↑
[ NIC / vNIC (AWS EC2)    ]   ← packet arrives
```

**Measured latency:** `T4 − T0` = internal stack latency (kernel → application), isolating jitter introduced by TLS + WebSocket framing.

---

## Repository Structure

```
├── ProxyTemplate/
│   ├── TimestampAwareStream.hpp     # Core: custom Boost.Beast SyncReadStream
│   └── Guideline for using proxy.txt
│
├── TCP test/
│   ├── TcpClient.cpp                # Test sender: sends messages with 200ms interval
│   └── TcpReceiver.cpp              # Test server: validates SO_TIMESTAMPING extraction
│
├── TimeSource.h                     # Abstract time source interface
├── PtpTimeSource.h / .cpp           # PTP hardware clock (/dev/ptp_ena on AWS Nitro)
├── SystemClockTimeSource.h / .cpp   # std::chrono::system_clock fallback
├── TimeSourceFactory.h / .cpp       # Factory: selects best available clock
│
├── RawFileLogger.h / .cpp           # Buffered file logger (flush every N lines)
└── bybit_orderbook.cpp              # Bybit v5 WebSocket client (Boost.Beast + OpenSSL)
```

---

## Key Components

### `TimestampAwareStream` (Core Innovation)

A C++ template class implementing the **Boost.Beast `SyncReadStream` concept** — making it a transparent drop-in replacement at the SSL stream layer.

```cpp
// Drop-in between TCP and TLS:
tcp::socket                          raw_socket(ioc);
TimestampAwareStream<tcp::socket>    ts_proxy(raw_socket);   // ← injected here
beast::ssl_stream<TimestampAwareStream<tcp::socket>&>  ssl(ts_proxy, ctx);
```

**Timestamp extraction flow:**
1. Constructor calls `setsockopt(SO_TIMESTAMPING)` with `SOF_TIMESTAMPING_RX_HARDWARE | SOF_TIMESTAMPING_RAW_HARDWARE | SOF_TIMESTAMPING_SOFTWARE`
2. On each `read_some()`, calls `recvmsg()` instead of `recv()` to get ancillary data
3. Parses `CMSG` chain for `SOL_SOCKET / SO_TIMESTAMPING`
4. Reads `ts[2]` (raw HW) if available, else falls back to `ts[0]` (kernel SW)
5. Exposes timestamp via `get_last_ts_ns()` after each read

**Graceful fallback:** If hardware timestamping fails (unsupported NIC/kernel), automatically downgrades to kernel software timestamp — never silently breaks.

---

### `TimeSource` Abstraction

Pluggable clock interface with two implementations:

| Implementation | Clock Source | Accuracy | Platform |
|---|---|---|---|
| `PtpTimeSource` | `/dev/ptp_ena` (AWS Nitro PHC) | ~100ns | Linux / AWS EC2 |
| `SystemClockTimeSource` | `std::chrono::system_clock` | ~1µs | Any |

`TimeSourceFactory` selects the best available source at runtime.

---

### TCP Test Harness

Standalone client/server pair (`TCP test/`) to validate `SO_TIMESTAMPING` independently of the full WebSocket stack — isolating kernel timestamp behavior before integrating with Boost.Beast.

---

## Data Flow

```
Bybit Exchange (WSS)
        │
        ▼
AWS EC2 NIC/vNIC (ENA)
        │  packet arrives
        ▼
TCP Socket kernel queue
        │  recvmsg() called by TimestampAwareStream
        ▼
TimestampAwareStream.read_some()
        ├──→ T0 = kernel RX timestamp (from cmsg SO_TIMESTAMPING)
        ▼
SSL/TLS decryption (OpenSSL)
        ▼
WebSocket frame parser (Boost.Beast)
        ▼
JSON message decoded
        ├──→ T4 = TimeSource::now_ns()
        ├── Orderbook Handler  → {bid, ask, exchange_ts, arrival_ts=T0, app_ts=T4}
        └── Tick Handler       → {price, qty, exchange_ts, arrival_ts=T0, app_ts=T4}
                │
                ▼
        RawFileLogger → NoSQL DB
        Derived: kernel_latency = T4 − T0
                 one_way_latency = T0 − exchange_ts
```

---

## Technology Stack

| Layer | Technology | Notes |
|---|---|---|
| Language | **C++17** | Templates, RAII, zero-overhead abstractions |
| WebSocket | **Boost.Beast / Boost.Asio** | Custom stream injection via `SyncReadStream` concept |
| TLS | **OpenSSL** | TLS 1.3, SNI |
| Timestamping | **`SO_TIMESTAMPING` + `recvmsg()`** | `SOF_TIMESTAMPING_RX_HARDWARE` / `_RX_SOFTWARE` |
| Hardware Clock | **PTP (`/dev/ptp_ena`)** | AWS Nitro PHC, ~100ns accuracy |
| Infrastructure | **AWS EC2 (ENA)** | Enhanced Networking, Nitro hypervisor |
| Data Source | **Bybit v5 WebSocket API** | `orderbook.1/50.BTCUSDT`, tick stream |
| Storage | **NoSQL** | Per-event: arrival_ts + exchange_ts + latency |

---

## Skills Demonstrated

| Skill | Where Applied |
|---|---|
| **Linux systems programming** | `SO_TIMESTAMPING`, `recvmsg()`, CMSG ancillary data parsing |
| **C++ template metaprogramming** | `TimestampAwareStream<NextLayer>` as a generic Boost.Beast stream concept |
| **Network stack internals** | TCP socket queue, kernel RX path, NIC/PHC timestamping pipeline |
| **HFT infrastructure design** | Sub-microsecond timestamp propagation through multi-layer I/O stack |
| **AWS networking** | ENA enhanced networking, Nitro PTP clock (`/dev/ptp_ena`) |
| **Async I/O** | Boost.Asio event loop, async read chains |
| **Software architecture** | Strategy pattern (TimeSource), factory pattern, layered stream abstraction |

---

## Build

**macOS (development)**
```bash
clang++ -std=c++17 bybit_orderbook.cpp \
  -I/opt/homebrew/include \
  -I/opt/homebrew/opt/openssl@3/include \
  -L/opt/homebrew/opt/openssl@3/lib \
  -lssl -lcrypto -lpthread -O2 -o bybit_ws
```

**Linux / AWS EC2 (production)**
```bash
g++ -std=c++17 bybit_orderbook.cpp \
  -lboost_system -lssl -lcrypto -lpthread -O2 -o bybit_ws
```

**Integrate TimestampAwareStream** (see `ProxyTemplate/Guideline for using proxy.txt`):
```cpp
#include "ProxyTemplate/TimestampAwareStream.hpp"

// 1. Create raw socket and connect
tcp::socket raw_socket(ioc);
asio::connect(raw_socket, results.begin(), results.end());

// 2. Wrap with proxy (injects SO_TIMESTAMPING in constructor)
TimestampAwareStream<tcp::socket> ts_proxy(raw_socket);

// 3. Build SSL + WebSocket on top
beast::ssl_stream<TimestampAwareStream<tcp::socket>&> ssl_stream(ts_proxy, ssl_ctx);
ws::stream<beast::ssl_stream<TimestampAwareStream<tcp::socket>&>&> ws_stream(ssl_stream);

// 4. After each read, extract T0
ws_stream.read(buffer);
long long t0_ns = ws_stream.next_layer().next_layer().get_last_ts_ns();
long long t4_ns = time_source->now_ns();
long long latency_ns = t4_ns - t0_ns;
```

---

## Roadmap

| Status | Item |
|---|---|
| ✅ | Bybit v5 WebSocket client (SSL + Boost.Beast) |
| ✅ | `TimestampAwareStream` — `recvmsg` + `SO_TIMESTAMPING` proxy |
| ✅ | `TimeSource` abstraction (PTP / SystemClock factory) |
| ✅ | `RawFileLogger` — buffered file logger |
| ✅ | TCP test harness (validate SO_TIMESTAMPING independently) |
| 🔄 | Wire `TimestampAwareStream` into main WebSocket client |
| ⬜ | Orderbook data model (L1/L50 snapshot + delta) |
| ⬜ | Tick data model |
| ⬜ | Stable client (reconnect, heartbeat, error handling) |
| ⬜ | Orderbook / tick handlers with `arrival_ts` attachment |
| ⬜ | NoSQL flush pipeline (arrival_ts + exchange_ts per event) |
