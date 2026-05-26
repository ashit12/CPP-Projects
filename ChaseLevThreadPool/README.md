# work-stealing thread pool

A lock-free, work-stealing thread pool in C++20.

## What's built

| File | Role |
|------|------|
| `Task.hpp` | Type-erased task wrapper — stores any callable as a `BaseTask*` |
| `GlobalSPMCQueue.hpp` | Lock-free single-producer / multi-consumer ring buffer for task submission |
| `WorkerDeque.hpp` | Per-worker Chase-Lev deque — owner pops LIFO, peers steal FIFO |
| `WorkerPool.hpp` | Orchestrates everything; each worker drains its own deque, then pulls from the global queue, then steals from peers |

Tasks submitted via `submit()` return a `std::future<T>`. Exceptions thrown inside tasks surface through the future rather than crashing workers.

## Requirements

- C++20 (`std::atomic`, `std::future`, concepts)
- Any conforming compiler: GCC 11+, Clang 13+, MSVC 19.29+

## Usage

```cpp
#include "WorkerPool.hpp"

// WorkerPool<LocalDequeCapacity, GlobalQueueCapacity, NumThreads>
WorkerPool<1024, 4096, 4> pool;

std::future<int> f = pool.submit([] { return 42; });
int result = f.get(); // 42
```

**Constraints:**
- `submit()` is **single-producer** — call only from one thread at a time.
- Both capacities must be powers of two.
- `~WorkerPool()` drains all queues before joining workers, so all futures resolve cleanly.

## Build & test

```bash
g++ -std=c++20 -O2 -pthread main.cpp -o threadpool
./threadpool
```
