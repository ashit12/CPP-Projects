// ─────────────────────────────────────────────────────────────────────────────
// LimitOrderBook benchmark suite
//
// Five benches:
//   A. add (same price, non-crossing)        — isolates PriceLevel::add
//   B. cancel (random id, multi-level book)  — isolates cancel hot path
//   C. single-level deep pop (per popped order, amortized)
//                                            — isolates PriceLevel::pop
//   D. N-level sweep (single crossing order eats N price levels)
//                                            — composite: map walk + pop
//   E. SPSC round-trip latency               — queue contention + cache coherence
//
// Per-bench output: min / p50 / p90 / p99 / p99.9 / p99.99 / max / mean / stddev,
// reported in BOTH wall-clock nanoseconds and raw ticks so you can sanity-check
// the calibration.
//
// Tick → ns calibration:
//   - Apple Silicon: CNTVCT_EL0 shares the mach timebase. mach_timebase_info()
//     gives exact numer/denom.
//   - x86-64: RDTSC ticks at the invariant TSC rate. Calibrated against
//     std::chrono::steady_clock over a short interval.
//
// Run on the performance cluster (macOS): we request QOS_CLASS_USER_INTERACTIVE
// for the bench threads. Plug in the laptop and close hungry apps before
// trusting tail numbers.
// ─────────────────────────────────────────────────────────────────────────────

#include "order_book.hpp"
#include "spsc_queue.hpp"
#include "types.hpp"

#include <algorithm>
#include <chrono>
#include <cinttypes>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <numeric>
#include <optional>
#include <random>
#include <string>
#include <thread>
#include <vector>

#if defined(__APPLE__)
    #include <mach/mach_time.h>
    #include <pthread/qos.h>
#endif

// ── Cycle / tick counter ─────────────────────────────────────────────────────
static inline uint64_t rdtsc()
{
#if defined(__aarch64__)
    uint64_t v;
    asm volatile("mrs %0, cntvct_el0" : "=r"(v));
    return v;
#elif defined(__x86_64__) || defined(_M_X64)
    return __builtin_ia32_rdtsc();
#else
    #error "rdtsc(): unsupported architecture"
#endif
}

// ── Tick → nanosecond calibration ────────────────────────────────────────────
struct TickToNs
{
    double ns_per_tick = 1.0;

    static TickToNs calibrate()
    {
        TickToNs out;
#if defined(__APPLE__) && defined(__aarch64__)
        mach_timebase_info_data_t info{};
        mach_timebase_info(&info);
        out.ns_per_tick =
            static_cast<double>(info.numer) / static_cast<double>(info.denom);
#else
        constexpr auto kInterval = std::chrono::milliseconds(50);
        const uint64_t t0 = rdtsc();
        const auto     w0 = std::chrono::steady_clock::now();
        std::this_thread::sleep_for(kInterval);
        const auto     w1 = std::chrono::steady_clock::now();
        const uint64_t t1 = rdtsc();
        const double   ns =
            std::chrono::duration<double, std::nano>(w1 - w0).count();
        out.ns_per_tick = ns / static_cast<double>(t1 - t0);
#endif
        return out;
    }

    double toNs(double ticks) const { return ticks * ns_per_tick; }
};

// ── Stats over a sample vector ───────────────────────────────────────────────
struct Stats
{
    uint64_t min{}, p50{}, p90{}, p99{}, p99_9{}, p99_99{}, max{};
    double   mean{}, stddev{};
    std::size_t n{};

    static Stats compute(std::vector<uint64_t>& v)
    {
        Stats s{};
        s.n = v.size();
        if (v.empty()) return s;

        std::sort(v.begin(), v.end());
        const auto idx = [&](double p) {
            auto i = static_cast<std::size_t>(p * (v.size() - 1));
            return v[i];
        };
        s.min    = v.front();
        s.max    = v.back();
        s.p50    = idx(0.50);
        s.p90    = idx(0.90);
        s.p99    = idx(0.99);
        s.p99_9  = idx(0.999);
        s.p99_99 = idx(0.9999);

        const double sum = std::accumulate(v.begin(), v.end(), 0.0);
        s.mean = sum / static_cast<double>(v.size());
        double var = 0.0;
        for (uint64_t x : v)
        {
            const double d = static_cast<double>(x) - s.mean;
            var += d * d;
        }
        s.stddev = std::sqrt(var / static_cast<double>(v.size()));
        return s;
    }
};

static std::string fmtPair(uint64_t ticks, const TickToNs& t)
{
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%10.1f ns  (%8" PRIu64 " t)",
                  t.toNs(static_cast<double>(ticks)), ticks);
    return std::string(buf);
}

static void printStats(const char* name,
                       std::vector<uint64_t>& samples,
                       const TickToNs& t)
{
    auto s = Stats::compute(samples);
    std::printf("── %s ──\n", name);
    std::printf("  n      : %zu\n",                            s.n);
    std::printf("  min    : %s\n", fmtPair(s.min,    t).c_str());
    std::printf("  p50    : %s\n", fmtPair(s.p50,    t).c_str());
    std::printf("  p90    : %s\n", fmtPair(s.p90,    t).c_str());
    std::printf("  p99    : %s\n", fmtPair(s.p99,    t).c_str());
    std::printf("  p99.9  : %s\n", fmtPair(s.p99_9,  t).c_str());
    std::printf("  p99.99 : %s\n", fmtPair(s.p99_99, t).c_str());
    std::printf("  max    : %s\n", fmtPair(s.max,    t).c_str());
    std::printf("  mean   : %10.1f ns\n", t.toNs(s.mean));
    std::printf("  stddev : %10.1f ns\n", t.toNs(s.stddev));
    std::printf("\n");
}

// ── Scheduling hint ──────────────────────────────────────────────────────────
static void pinHotThread()
{
#if defined(__APPLE__)
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#endif
}

// ── Shared constants ─────────────────────────────────────────────────────────
namespace
{
constexpr lob::Price    BASE_PRICE = 10'000;
constexpr lob::Quantity QTY        = 100;
}

// ─────────────────────────────────────────────────────────────────────────────
// Bench A — add only (same price, non-crossing)
// Goal: isolate PriceLevel::add cost. We anchor a single resting bid one tick
// below our sells so match() always exits without touching anything, then time
// the steady-state insert into a single growing price level.
// ─────────────────────────────────────────────────────────────────────────────
static void benchAdd(const TickToNs& t)
{
    constexpr int K           = 200'000;   // measured samples
    constexpr int RESET_EVERY = 10'000;    // bound depth + flush allocator
    constexpr int WARMUP      = 10'000;

    lob::OrderBook book;
    lob::OrderId   nextId = 1;

    book.add(lob::Order{BASE_PRICE - 1, 0, QTY, nextId++, lob::Side::Buy});

    for (int i = 0; i < WARMUP; ++i)
    {
        book.add(lob::Order{BASE_PRICE, 0, QTY, nextId++, lob::Side::Sell});
    }

    std::vector<uint64_t> samples;
    samples.reserve(K);

    for (int i = 0; i < K; ++i)
    {
        if (i && (i % RESET_EVERY) == 0)
        {
            book = lob::OrderBook{};
            book.add(lob::Order{BASE_PRICE - 1, 0, QTY, nextId++,
                                lob::Side::Buy});
        }

        lob::Order o{BASE_PRICE, 0, QTY, nextId++, lob::Side::Sell};
        const uint64_t s = rdtsc();
        book.add(o);
        const uint64_t e = rdtsc();
        samples.push_back(e - s);
    }

    printStats("Bench A: add (same price, non-crossing)", samples, t);
}

// ─────────────────────────────────────────────────────────────────────────────
// Bench B — cancel only (random id over a multi-level book)
// Goal: isolate the cancel hot path: orderIndex lookup + price-level cancel
// (orderMap lookup + list erase) + possible empty-level erase from the map.
// We cancel ids in randomized order so list locality cannot help us.
// ─────────────────────────────────────────────────────────────────────────────
static void benchCancel(const TickToNs& t)
{
    constexpr int M      = 500'000;
    constexpr int WARMUP = 1'000;

    lob::OrderBook book;
    std::vector<lob::OrderId> ids;
    ids.reserve(M);

    std::mt19937_64 rng(0xC0FFEEULL);
    std::uniform_int_distribution<int> priceOffset(0, 999);

    lob::OrderId nextId = 1;
    for (int i = 0; i < M; ++i)
    {
        // Far above any bid; guaranteed not to cross.
        const lob::Price p = BASE_PRICE + 10'000 + priceOffset(rng);
        ids.push_back(nextId);
        book.add(lob::Order{p, 0, QTY, nextId++, lob::Side::Sell});
    }
    std::shuffle(ids.begin(), ids.end(), rng);

    for (int i = 0; i < WARMUP; ++i) book.cancel(ids[i]);

    std::vector<uint64_t> samples;
    samples.reserve(static_cast<std::size_t>(M - WARMUP));

    for (int i = WARMUP; i < M; ++i)
    {
        const uint64_t s = rdtsc();
        book.cancel(ids[i]);
        const uint64_t e = rdtsc();
        samples.push_back(e - s);
    }

    printStats("Bench B: cancel (random id, multi-level book)", samples, t);
}

// ─────────────────────────────────────────────────────────────────────────────
// Bench C — single-level deep pop
// Pre-load DEPTH orders at one price, then submit ONE crossing bid that fully
// consumes the level. The timed region covers the entire add(bid) call.
// Reported whole-batch ticks, with a derived per-popped-order figure at the
// end so it's directly comparable to Bench D.
// Isolates PriceLevel::pop + std::list node free from the std::map walk.
// ─────────────────────────────────────────────────────────────────────────────
static void benchSingleLevelPop(const TickToNs& t)
{
    constexpr int ITERATIONS = 20'000;
    constexpr int DEPTH      = 1'000;
    constexpr int WARMUP     = 200;

    lob::OrderId nextId = 1;

    const auto runOnce = [&]() -> uint64_t {
        lob::OrderBook book;
        for (int j = 0; j < DEPTH; ++j)
        {
            book.add(lob::Order{BASE_PRICE, 0, QTY, nextId++,
                                lob::Side::Sell});
        }
        lob::Order bid{BASE_PRICE,
                       0,
                       QTY * static_cast<lob::Quantity>(DEPTH),
                       nextId++,
                       lob::Side::Buy};
        const uint64_t s = rdtsc();
        book.add(bid);
        const uint64_t e = rdtsc();
        return e - s;
    };

    for (int i = 0; i < WARMUP; ++i) (void)runOnce();

    std::vector<uint64_t> samples;
    samples.reserve(ITERATIONS);
    for (int i = 0; i < ITERATIONS; ++i) samples.push_back(runOnce());

    char header[128];
    std::snprintf(header, sizeof(header),
                  "Bench C: single-level deep pop (whole batch, DEPTH=%d)",
                  DEPTH);
    printStats(header, samples, t);

    if (!samples.empty())
    {
        // printStats sorted samples; pick the median raw value.
        const double per_op_ns =
            t.toNs(static_cast<double>(samples[samples.size() / 2])) /
            static_cast<double>(DEPTH);
        std::printf("  (derived per-popped-order @ p50: %.1f ns)\n\n",
                    per_op_ns);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Bench D — N-level sweep (whole sweep)
// Pre-load N consecutive ask price levels with one resting sell each, then a
// single big crossing bid consumes them all. The reported number is the WHOLE
// sweep — divide by N to compare against Bench C.
// ─────────────────────────────────────────────────────────────────────────────
static void benchSweep(const TickToNs& t)
{
    constexpr int N          = 1'000;
    constexpr int ITERATIONS = 20'000;
    constexpr int WARMUP     = 200;

    lob::OrderId nextId = 1;

    const auto runOnce = [&]() -> uint64_t {
        lob::OrderBook book;
        for (int j = 0; j < N; ++j)
        {
            book.add(lob::Order{BASE_PRICE + j, 0, QTY, nextId++,
                                lob::Side::Sell});
        }
        lob::Order bid{BASE_PRICE + N,
                       0,
                       QTY * static_cast<lob::Quantity>(N),
                       nextId++,
                       lob::Side::Buy};
        const uint64_t s = rdtsc();
        book.add(bid);
        const uint64_t e = rdtsc();
        return e - s;
    };

    for (int i = 0; i < WARMUP; ++i) (void)runOnce();

    std::vector<uint64_t> samples;
    samples.reserve(ITERATIONS);
    for (int i = 0; i < ITERATIONS; ++i) samples.push_back(runOnce());

    char header[128];
    std::snprintf(header, sizeof(header),
                  "Bench D: N-level sweep (whole sweep, N=%d)", N);
    printStats(header, samples, t);

    // Helpful derived number: average per popped order for direct A/B against
    // Bench C. We compute this from the already-sorted vector (printStats
    // sorted it).
    if (!samples.empty())
    {
        const double per_op_ns =
            t.toNs(static_cast<double>(samples[samples.size() / 2])) /
            static_cast<double>(N);
        std::printf("  (derived per-popped-order @ p50: %.1f ns)\n\n",
                    per_op_ns);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Bench E — SPSC one-way latency (strict ping-pong, depth=1)
// Producer stamps rdtsc() on each Order, pushes, then busy-waits on an ack
// queue. Consumer pops, records latency = (rdtsc - timestamp), then acks.
// Forward queue depth stays at 0 or 1, so we measure true push→pop latency
// (cross-core cache-line transfer + queue overhead), NOT queue dwell time
// under back-pressure.
// ─────────────────────────────────────────────────────────────────────────────
static void benchSPSC(const TickToNs& t)
{
    constexpr int ITERS  = 200'000;
    constexpr int WARMUP = 10'000;

    lob::SPSCQueue<lob::Order, 4096> q;
    lob::SPSCQueue<uint8_t,    64>   ack;

    const auto run = [&](int n, std::vector<uint64_t>* out) {
        std::thread prod([&] {
            pinHotThread();
            for (int i = 0; i < n; ++i)
            {
                lob::Order o{BASE_PRICE, rdtsc(), QTY,
                             static_cast<lob::OrderId>(i),
                             lob::Side::Buy};
                while (!q.push(o)) {}
                while (!ack.pop().has_value()) {}
            }
        });
        std::thread cons([&] {
            pinHotThread();
            for (int i = 0; i < n; ++i)
            {
                std::optional<lob::Order> x;
                while (!(x = q.pop())) {}
                const uint64_t lat = rdtsc() - x->timestamp;
                if (out) out->push_back(lat);
                while (!ack.push(0)) {}
            }
        });
        prod.join();
        cons.join();
    };

    run(WARMUP, nullptr);

    std::vector<uint64_t> lat;
    lat.reserve(ITERS);
    run(ITERS, &lat);

    printStats("Bench E: SPSC one-way latency (ping-pong, depth=1)", lat, t);
}

// ─────────────────────────────────────────────────────────────────────────────

int main()
{
    pinHotThread();

    const auto t = TickToNs::calibrate();
    std::printf("Calibration: %.6f ns/tick\n", t.ns_per_tick);
#if defined(__APPLE__) && defined(__aarch64__)
    std::printf("Timer       : CNTVCT_EL0 (Apple Silicon, shared timebase)\n");
#elif defined(__x86_64__) || defined(_M_X64)
    std::printf("Timer       : RDTSC (calibrated)\n");
#endif
    std::printf("Iterations  : A=200k  B=499k  C=20k  D=20k  E=200k\n\n");

    benchAdd(t);
    benchCancel(t);
    benchSingleLevelPop(t);
    benchSweep(t);
    benchSPSC(t);

    return 0;
}
