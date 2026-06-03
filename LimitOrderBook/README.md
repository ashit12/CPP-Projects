# LimitOrderBook

A low-latency C++20 limit order book with a lock-free SPSC command queue and a single-writer matching engine.

The project is an end-to-end implementation of the core data structures behind an exchange matching engine: a price-time-priority order book, a lock-free single-producer/single-consumer queue, and a two-thread architecture that keeps the hot path free of locks. It includes a Google Test suite and a five-benchmark suite with calibrated nanosecond reporting.

---

## Architecture

```
Producer thread(s)                    Worker thread (run())
──────────────────                    ──────────────────────
submit(price, qty, side)  ──push──►  pop Command
cancel(id)                ──push──►  pop Command
                                        │
                          SPSCQueue      ▼
                          <Command,   OrderBook::add / cancel
                           4096>        │
                                        ▼
                                     publishedBestAsk.store(…, release)
                                     publishedBestBid.store(…, release)

getBestAsk() / getBestBid()  ◄──── .load(acquire) from any thread
```

`submit()` and `cancel()` construct a tagged `Command` (`CommandType::Add` or `CommandType::Cancel`) and push it onto a lock-free SPSC queue. The worker thread (started by `run()`) is the **only thread that ever touches the `OrderBook`** — no mutex is needed on the matching or book-mutation paths. After each command is processed the worker atomically publishes the new best bid and ask to `std::atomic<Price>` fields using release semantics; external threads read a consistent snapshot with a single acquire load.

The zero sentinel (`Price == 0`) signals an empty side. This avoids an extra `std::optional` wrapper in the hot read path at the cost of reserving price 0 as invalid, which `isValid()` enforces.

---

## Design Decisions

### Scaled-integer prices (`int64_t` ticks)

Prices are `int64_t` throughout. Floating-point comparison in a matching engine is unreliable: `0.1 + 0.2 != 0.3` in IEEE 754, which can cause orders that should match to not match, or incorrect price-level routing. Integer tick arithmetic eliminates the class of bugs entirely.

### `Order` struct layout (32 bytes)

```cpp
struct Order {
    Price     price;      // int64_t   — 8 bytes, offset 0
    Timestamp timestamp;  // uint64_t  — 8 bytes, offset 8
    Quantity  quantity;   // uint32_t  — 4 bytes, offset 16
    OrderId   id;         // uint32_t  — 4 bytes, offset 20
    Side      side;       // uint8_t   — 1 byte,  offset 24
                          // 7 bytes compiler padding
};                        // total: 32 bytes
```

Members are ordered largest-to-smallest to minimise internal padding. At 32 bytes, exactly two `Order` objects fit in a 64-byte cache line, which matters for `SPSCQueue` throughput when the producer and consumer are on different cores.

### Price-time (FIFO) priority

Each side of the book uses a `std::map` for sorted price-level routing:

- `std::map<Price, PriceLevel> asks` — ascending; `begin()` is the best ask.
- `std::map<Price, PriceLevel, std::greater<Price>> bids` — descending; `begin()` is the best bid.

Within a `PriceLevel`, orders are held in a `std::list<Order>` for strict FIFO. Cancel by ID is O(1) via a parallel `std::unordered_map<OrderId, std::list<Order>::iterator>` that maps each live order to its list node. The `OrderBook` keeps its own `orderIndex` mapping `OrderId → {Side, Price}` so `cancel()` can locate the right price level in O(1) before delegating to `PriceLevel::cancel()`.

### Lock-free SPSC queue

```cpp
template <typename T, size_t Capacity>   // Capacity must be a power of 2
class SPSCQueue {
    alignas(64) std::array<T, Capacity> buffer{};
    alignas(64) std::atomic<size_t> head{0};
    alignas(64) std::atomic<size_t> tail{0};
};
```

Key properties:
- **Power-of-2 capacity** with bitmask indexing (`index & (Capacity - 1)`) avoids a modulo on every access.
- **`alignas(64)` on `head`, `tail`, and `buffer`** places each on its own cache line, preventing false sharing between the producer (which writes `tail`) and the consumer (which writes `head`).
- **Memory ordering**: `push` reads `head` with `acquire`, writes `tail` with `release`; `pop` reads `tail` with `relaxed` (consumer owns `head`), writes `head` with `release`. No fences beyond what acquire/release implies.
- `push()` returns `false` when full; `pop()` returns `std::nullopt` when empty. Both callers busy-wait.

---

## Components

**`include/types.hpp`** — All shared primitive types: `Price` (`int64_t`), `Timestamp` (`uint64_t`), `Quantity` (`uint32_t`), `OrderId` (`uint32_t`), `Side` (`uint8_t` enum), `CommandType` (`uint8_t` enum: `Add`/`Cancel`), `Order` struct, and `Command` struct (a `CommandType` tag plus an embedded `Order`).

**`PriceLevel`** (`include/price_level.hpp`, `src/price_level.cpp`) — Manages all resting orders at one price. Internally a `std::list<Order>` for FIFO ordering and a `std::unordered_map<OrderId, list::iterator>` for O(1) cancel. Public interface: `add`, `cancel`, `pop` (removes and returns the front order), `peek`, `modifyFrontQuantity` (used by partial fills), `isEmpty`, `size`.

**`OrderBook`** (`include/order_book.hpp`, `src/order_book.cpp`) — The matching engine core. Maintains `asks` and `bids` as `std::map`s of `PriceLevel`s, and `orderIndex` for O(1) cancel routing. `add()` calls `match()` first and rests any unfilled remainder. `match()` walks levels from best price outward, consuming resting orders and erasing them from `orderIndex` as it goes. `getBestAsk()` / `getBestBid()` return `std::optional<Price>` from `map::begin()`.

**`MatchingEngine`** (`include/matching_engine.hpp`, `src/matching_engine.cpp`) — The two-thread boundary. `submit()` and `cancel()` (callable from any thread) push `Command` objects onto `SPSCQueue<Command, 4096>`. `run()` (the worker loop) pops commands and dispatches to `OrderBook`, then stores the new best prices into `publishedBestAsk` / `publishedBestBid` with release semantics. `getBestAsk()` / `getBestBid()` do acquire loads on those atomics and return `std::nullopt` for the zero sentinel.

**`SPSCQueue<T, Capacity>`** (`include/spsc_queue.hpp`) — A header-only, lock-free single-producer/single-consumer ring buffer. See *Design Decisions* above.

---

## Building & Testing

**Requirements:** CMake ≥ 3.20, a C++20-capable compiler (tested with Apple Clang on arm64). GoogleTest is fetched automatically by CMake.

```bash
# Configure (Release)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

# Build everything
cmake --build build

# Run the test suite
ctest --test-dir build --output-on-failure

# Run the benchmark suite
./build/bench
```

The library (`lob`) is compiled with `-O3 -march=native -fno-exceptions`. Tests are linked against `gtest_main`; `gtest_discover_tests` registers each `TEST_F` individually with CTest.

---

## Tests

The project includes a GoogleTest suite covering order-book correctness (full and partial matches, multi-level sweeps, FIFO consumption within a level, cancel) and an end-to-end test that exercises the full producer → queue → worker → book path across two threads. Run it with `ctest --test-dir build --output-on-failure`.

---

## Benchmarks

The bench binary runs five suites in sequence. Each prints min / p50 / p90 / p99 / p99.9 / p99.99 / max / mean / stddev in both nanoseconds and raw ticks.

**Tick calibration:** On Apple Silicon, `mach_timebase_info()` gives the exact `numer/denom` ratio for converting `CNTVCT_EL0` ticks to nanoseconds. On x86-64, the TSC is calibrated against `std::chrono::steady_clock` over a 50 ms interval. The calibration constant is printed at startup.

| Bench | What is timed | Samples | Purpose |
|---|---|---|---|
| **A** — `add` (same price, non-crossing) | `OrderBook::add` for a single sell into a growing level | 200 k | Isolates `PriceLevel::add`: `list::push_back` + `unordered_map` insert |
| **B** — `cancel` (random id, multi-level book) | `OrderBook::cancel` over 500 k randomly-shuffled ids | ~499 k | Isolates cancel hot path: `orderIndex` lookup + `list::erase` + possible `map::erase` |
| **C** — single-level deep pop | `OrderBook::add` for a crossing bid that consumes DEPTH=1000 orders at one price | 20 k | Isolates `PriceLevel::pop` × DEPTH; also reports derived per-order figure |
| **D** — N-level sweep | `OrderBook::add` for a crossing bid that sweeps N=1000 separate price levels | 20 k | Adds `std::map` traversal per level on top of the pop cost; C vs D difference = map-walk overhead |
| **E** — SPSC ping-pong | One-way push→pop latency with a strict ack queue (depth=1) | 200 k | Measures cross-core cache-line transfer time for the SPSC queue; NOT queue dwell under backpressure |

**Headline finding:** the per-popped-order cost in Bench C (single-level pop, no map traversal) is ~2 310 ns; in Bench D (N-level sweep, one pop per level) it is ~4 157 ns. The ~1 847 ns gap is the **measured per-level cost of the `std::map`-based price ladder plus `PriceLevel` teardown**, isolated from the per-order pop cost — that's the concrete optimization target for an array-indexed ladder with reusable level slots.

**Sample results** (Apple Silicon M-series, unpinned, `QOS_CLASS_USER_INTERACTIVE`, single representative run; medians are stable across runs but tail latencies drift with system load):

```
Bench A: add (same price, non-crossing)
  p50  :     3 458 ns
  p99  :     6 958 ns
  max  : 4 891 792 ns

Bench B: cancel (random id, multi-level book)
  p50  :    55 542 ns
  p99  :   102 417 ns
  max  : 99 505 042 ns

Bench C: single-level deep pop (DEPTH=1000)
  p50  (whole batch)      : 2 310 500 ns
  per-popped-order @ p50  :     2 311 ns

Bench D: N-level sweep (N=1000)
  p50  (whole sweep)      : 4 156 750 ns
  per-popped-order @ p50  :     4 157 ns
  Δ vs Bench C per order  :     1 847 ns   ← cost of std::map walk + PriceLevel teardown per level

Bench E: SPSC one-way latency (ping-pong, depth=1)
  p50  :     3 500 ns
  p99  :     6 958 ns
  max  : 1 221 208 ns
```

**Methodology note:** Tail latencies (p99.99, max) are inflated by OS scheduling on a shared macOS machine. macOS does not expose `pthread_setaffinity_np`, so threads can migrate between cores during a run; the `QOS_CLASS_USER_INTERACTIVE` hint reduces but does not eliminate preemption. Treat tail numbers as an upper bound, not a property of the data structure.

---

## Known Limitations / Future Work

- **Flat-array price ladder:** A `std::array<PriceLevel, MAX_LEVELS>` variant (indexed by `price − basePrice`) was implemented and benchmarked. It was slower than the `std::map` version at the shallow book depths typical in practice (10–100 active levels) and only roughly even at ~1000 levels, so it was not adopted. A flat structure with tracked best-price would likely win at large depth but needs the best-price scan handled carefully.

- **Pool / slab allocator:** `std::list` node allocation goes through the global allocator on every `add` and `pop`. A slab or pool allocator would reduce per-node allocation overhead and improve locality. Not yet implemented.

- **No core pinning on macOS:** `pthread_setaffinity_np` is not available on macOS. Bench E (SPSC latency) therefore reflects unpinned cross-core cache traffic, which adds OS scheduling jitter. On Linux with pinning the latency would be lower and the tail tighter.

- **Timing-based waits in threaded tests:** Although the tests use a `waitFor` polling loop (not a fixed sleep), there is no guaranteed-by-design synchronisation point — if the worker thread is not scheduled within the `waitFor` deadline the test fails spuriously under extreme load.

- **`cancel` command carries a full `Order`:** The `Command` struct embeds a full `Order` (32 bytes) even for a cancel, which only uses the `id` field. A tagged union or a smaller cancel payload would reduce queue bandwidth for cancel-heavy workloads.
