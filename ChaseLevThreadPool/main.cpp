// =====================================================================
// WorkerPool stress / correctness test suite.
//
// Each test prints PASS or FAIL and the suite exits non-zero if any
// test failed. Tests are intentionally independent so adding new ones
// is cheap and the test that broke is obvious from the output.
//
// Submit is single-producer per WorkerPool's contract, so every test
// drives the pool from this (main) thread only.
// =====================================================================

#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <atomic>
#include <random>
#include <stdexcept>
#include <string>
#include <iomanip>
#include <exception>
#include "WorkerPool.hpp"

namespace test
{
    int passes = 0;
    int failures = 0;

    void check(bool cond, const std::string &name)
    {
        if (cond)
        {
            ++passes;
            std::cout << "  [PASS] " << name << "\n";
        }
        else
        {
            ++failures;
            std::cout << "  [FAIL] " << name << "\n";
        }
    }

    void section(const std::string &name)
    {
        std::cout << "\n=== " << name << " ===\n";
    }
}

// ---------------------------------------------------------------------
// Test 1: Basic correctness
//
// Submit N tasks indexed 0..N-1. Each computes a deterministic value
// derived from its index. Verify every future returns the expected
// value. Catches scheduling bugs that drop or duplicate tasks.
// ---------------------------------------------------------------------
template <typename Pool>
void test_basic_correctness(Pool &pool)
{
    test::section("Basic correctness");
    constexpr int N = 1000;
    std::vector<std::future<int>> futures;
    futures.reserve(N);
    for (int i = 0; i < N; ++i)
    {
        futures.push_back(pool.submit([i]
                                      { return i * 2; }));
    }
    bool ok = true;
    int firstBad = -1;
    int badValue = 0;
    for (int i = 0; i < N; ++i)
    {
        int v = futures[i].get();
        if (v != i * 2)
        {
            ok = false;
            firstBad = i;
            badValue = v;
            break;
        }
    }
    if (!ok)
    {
        std::cout << "    first mismatch at index " << firstBad
                  << " (got " << badValue << ", expected " << (firstBad * 2) << ")\n";
    }
    test::check(ok, "1000 indexed tasks return correct values");
}

// ---------------------------------------------------------------------
// Test 2: Mixed return types
//
// Verifies the submit() template works for int, std::string, and void
// return types and that captures by reference observe side effects.
// ---------------------------------------------------------------------
template <typename Pool>
void test_mixed_return_types(Pool &pool)
{
    test::section("Mixed return types");

    auto fInt = pool.submit([]
                            { return 42; });
    auto fStr = pool.submit([]
                            { return std::string("hello"); });

    std::atomic<int> sideEffect{0};
    auto fVoid = pool.submit([&sideEffect]
                             { sideEffect.store(7); });

    int a = fInt.get();
    std::string b = fStr.get();
    fVoid.get();

    test::check(a == 42, "int-returning task");
    test::check(b == "hello", "string-returning task");
    test::check(sideEffect.load() == 7, "void-returning task ran (side effect observed)");
}

// ---------------------------------------------------------------------
// Test 3: Exception propagation
//
// Tasks that throw should surface their exception through future.get()
// rather than terminating the worker. Mixes throwing and normal tasks
// to make sure one bad task doesn't poison the worker's state.
// ---------------------------------------------------------------------
template <typename Pool>
void test_exception_propagation(Pool &pool)
{
    test::section("Exception propagation");

    auto f = pool.submit([]() -> int
                         { throw std::runtime_error("expected failure"); });

    bool caught = false;
    std::string what;
    try
    {
        f.get();
    }
    catch (const std::runtime_error &e)
    {
        caught = true;
        what = e.what();
    }
    catch (...)
    {
    }
    test::check(caught && what == "expected failure",
                "runtime_error from task surfaces via future.get()");

    constexpr int N = 300;
    std::vector<std::future<int>> futures;
    futures.reserve(N);
    for (int i = 0; i < N; ++i)
    {
        futures.push_back(pool.submit([i]() -> int
                                      {
            if (i % 3 == 0) throw std::logic_error("i=" + std::to_string(i));
            return i; }));
    }
    int caughtCount = 0;
    int okCount = 0;
    for (int i = 0; i < N; ++i)
    {
        try
        {
            int v = futures[i].get();
            if (v == i)
                ++okCount;
        }
        catch (const std::logic_error &)
        {
            ++caughtCount;
        }
    }
    const int expectedThrowing = (N + 2) / 3;
    test::check(caughtCount == expectedThrowing && okCount == N - expectedThrowing,
                "mixed throwing/non-throwing tasks all routed correctly");
}

// ---------------------------------------------------------------------
// Test 4: Callable kinds
//
// Free functions, lambdas with and without args, capturing lambdas,
// and functors all should work through the same submit() interface.
// ---------------------------------------------------------------------
int free_multiply(int a, int b)
{
    return a * b;
}

struct Functor
{
    int operator()(int x) const { return x + 100; }
};

template <typename Pool>
void test_callable_kinds(Pool &pool)
{
    test::section("Callable kinds");

    auto fFree = pool.submit(free_multiply, 6, 7);
    auto fLambda = pool.submit([](int x, int y)
                               { return x - y; },
                               100, 42);
    Functor f;
    auto fFunctor = pool.submit(f, 5);
    auto fCapture = pool.submit([base = 1000](int delta)
                                { return base + delta; },
                                23);

    test::check(fFree.get() == 42, "free function pointer");
    test::check(fLambda.get() == 58, "lambda with positional args");
    test::check(fFunctor.get() == 105, "functor (operator())");
    test::check(fCapture.get() == 1023, "capturing lambda");
}

// ---------------------------------------------------------------------
// Test 5: Variable-duration tasks (exercises work stealing)
//
// A mix of tiny and (occasionally) longer tasks creates imbalance
// between worker deques and forces stealing. If stealing is broken
// (e.g., a deque slot is double-claimed) we see either a wrong count,
// a double-free crash, or both.
// ---------------------------------------------------------------------
template <typename Pool>
void test_variable_durations(Pool &pool)
{
    test::section("Variable durations (work stealing)");
    constexpr int N = 5000;
    std::atomic<int> counter{0};
    std::vector<std::future<void>> futures;
    futures.reserve(N);

    std::mt19937 rng(42);
    std::uniform_int_distribution<int> shortDist(0, 50);
    std::uniform_int_distribution<int> longDist(500, 2000);
    std::bernoulli_distribution longTask(0.05);

    auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < N; ++i)
    {
        int us = longTask(rng) ? longDist(rng) : shortDist(rng);
        futures.push_back(pool.submit([&counter, us]
                                      {
            if (us > 0) std::this_thread::sleep_for(std::chrono::microseconds(us));
            counter.fetch_add(1, std::memory_order_relaxed); }));
    }
    for (auto &fut : futures)
        fut.get();
    auto end = std::chrono::steady_clock::now();

    double ms = std::chrono::duration<double, std::milli>(end - start).count();
    std::cout << "    elapsed " << std::fixed << std::setprecision(1) << ms << " ms\n";
    test::check(counter.load() == N, "all 5000 variable-duration tasks completed exactly once");
}

// ---------------------------------------------------------------------
// Test 6: Backpressure
//
// Submit far more tasks than the global queue can hold so submit()'s
// spin path is exercised. The final sum verifies no task was dropped
// and none was executed twice.
// ---------------------------------------------------------------------
template <typename Pool>
void test_backpressure(Pool &pool)
{
    test::section("Backpressure (queue overflow spin)");
    constexpr int N = 50000;
    std::atomic<long long> sum{0};
    std::vector<std::future<void>> futures;
    futures.reserve(N);

    for (int i = 0; i < N; ++i)
    {
        futures.push_back(pool.submit([&sum, i]
                                      { sum.fetch_add(i, std::memory_order_relaxed); }));
    }
    for (auto &fut : futures)
        fut.get();

    const long long expected = (long long)(N - 1) * N / 2;
    if (sum.load() != expected)
    {
        std::cout << "    expected " << expected << ", got " << sum.load() << "\n";
    }
    test::check(sum.load() == expected,
                "sum of 50000 backpressured tasks matches arithmetic series");
}

// ---------------------------------------------------------------------
// Test 7: Throughput
//
// Reports tasks/sec for tight no-op tasks. Not a strict pass/fail —
// the assertion only checks that every task ran. The number is a
// regression signal for future changes.
// ---------------------------------------------------------------------
template <typename Pool>
void test_throughput(Pool &pool)
{
    test::section("Throughput (no-op tasks)");
    constexpr int N = 200000;
    std::atomic<int> counter{0};
    std::vector<std::future<void>> futures;
    futures.reserve(N);

    auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < N; ++i)
    {
        futures.push_back(pool.submit([&counter]
                                      { counter.fetch_add(1, std::memory_order_relaxed); }));
    }
    for (auto &fut : futures)
        fut.get();
    auto end = std::chrono::steady_clock::now();

    double sec = std::chrono::duration<double>(end - start).count();
    double rate = N / sec;
    std::cout << "    " << N << " tasks in "
              << std::fixed << std::setprecision(2) << (sec * 1000.0) << " ms ("
              << std::fixed << std::setprecision(0) << rate << " tasks/s)\n";
    test::check(counter.load() == N, "all throughput tasks completed");
}

// ---------------------------------------------------------------------
// Test 8: Drain-on-shutdown
//
// Submits a batch of tasks immediately before pool destruction and
// does NOT await them. The destructor must drain the queues and run
// every pending task so the counter and futures still resolve.
//
// This test owns its own pool so its destructor runs inside the test
// rather than at the end of main().
// ---------------------------------------------------------------------
void test_drain_on_shutdown()
{
    test::section("Drain on shutdown");
    constexpr size_t deque_capacity = 256;
    constexpr size_t global_queue_capacity = 4096;
    constexpr size_t num_threads = 4;
    constexpr int N = 2000;

    std::atomic<int> counter{0};
    std::vector<std::future<void>> futures;
    futures.reserve(N);

    {
        WorkerPool<deque_capacity, global_queue_capacity, num_threads> pool;
        for (int i = 0; i < N; ++i)
        {
            futures.push_back(pool.submit([&counter]
                                          {
                std::this_thread::sleep_for(std::chrono::microseconds(50));
                counter.fetch_add(1, std::memory_order_relaxed); }));
        }
    }

    int settled = 0;
    int broken = 0;
    for (auto &fut : futures)
    {
        try
        {
            fut.get();
            ++settled;
        }
        catch (const std::future_error &)
        {
            ++broken;
        }
        catch (...)
        {
            ++broken;
        }
    }
    std::cout << "    counter=" << counter.load()
              << " settled=" << settled
              << " broken=" << broken << " (of " << N << ")\n";
    test::check(counter.load() == N && settled == N,
                "every task ran and every future resolved after pool destruction");
}

int main()
{
    // Deliberately modest capacities relative to the workload below so
    // the back-pressure test (50k tasks vs 8k global slots) actually
    // makes submit() spin and exercises the wraparound math in both
    // queues.
    constexpr size_t deque_capacity = 1024;
    constexpr size_t global_queue_capacity = 8192;
    constexpr size_t num_threads = 8;

    std::cout << "=== WorkerPool stress test suite ===\n";
    std::cout << "Hardware concurrency reported by the runtime: "
              << std::thread::hardware_concurrency() << "\n";
    std::cout << "Pool config: " << num_threads << " workers, local cap "
              << deque_capacity << ", global cap " << global_queue_capacity << "\n";

    {
        WorkerPool<deque_capacity, global_queue_capacity, num_threads> pool;

        test_basic_correctness(pool);
        test_mixed_return_types(pool);
        test_exception_propagation(pool);
        test_callable_kinds(pool);
        test_variable_durations(pool);
        test_backpressure(pool);
        test_throughput(pool);
    }

    test_drain_on_shutdown();

    std::cout << "\n=== Summary ===\n";
    std::cout << "Passed: " << test::passes << "\n";
    std::cout << "Failed: " << test::failures << "\n";

    return test::failures == 0 ? 0 : 1;
}
