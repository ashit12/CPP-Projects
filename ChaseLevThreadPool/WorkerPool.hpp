#include <vector>
#include <memory>
#include <atomic>
#include <future>
#include <thread>
#include <optional>
#include <array>
#include <type_traits>
#include <functional>
#include "WorkerDeque.hpp"
#include "Task.hpp"
#include "GlobalSPMCQueue.hpp"

// =====================================================================
// WorkerPool: bounded work-stealing thread pool.
//
// Architecture:
//
//   submit() --> GlobalSPMCQueue --> worker pulls one task at a time
//                                    |
//                                    v
//                          pushed to that worker's local
//                                  WorkerDeque
//                                    |
//                       LIFO pop by owner / FIFO steal by peers
//
// Each worker drains its own deque first (LIFO, hot cache), then pulls
// from the global queue (and republishes through its deque so peers
// can steal), then attempts to steal from another worker, then yields.
//
// Threading contract:
//   - submit() is single-producer: do not call from multiple threads
//     concurrently.
//   - The pool is non-copyable and non-movable.
//   - Destruction must not race with submit().
//
// Shutdown:
//   ~WorkerPool() sets running_ = false and joins all workers. Any
//   tasks still in the global queue or in any local deque are drained
//   and executed on the destructor thread so that futures handed out
//   by submit() resolve normally. Exceptions thrown during drain are
//   swallowed because the destructor cannot surface them.
// =====================================================================
template <size_t LocalCapacity, size_t GlobalCapacity, size_t NumThreads>
class WorkerPool
{
public:
    WorkerPool(const WorkerPool &) = delete;
    WorkerPool(WorkerPool &&) = delete;
    WorkerPool &operator=(const WorkerPool &) = delete;
    WorkerPool &operator=(WorkerPool &&) = delete;

    // running_ is set true before any worker is spawned so the first
    // iteration of the worker loop sees the correct value.
    WorkerPool() : running_{true}
    {
        for (size_t id = 0; id < NumThreads; id++)
        {
            workers_.emplace_back([this, id]
                                  { this->workerLoop(id); });
        }
    }

    // Shutdown sequence: signal stop, join workers, drain queues.
    // Every step that could throw is wrapped because destructors are
    // implicitly noexcept: any uncaught exception would terminate.
    ~WorkerPool()
    {
        running_.store(false, std::memory_order_release);

        for (size_t id = 0; id < NumThreads; id++)
        {
            if (workers_[id].joinable())
            {
                try
                {
                    workers_[id].join();
                }
                catch (...)
                {
                }
            }
        }

        // Drain the global queue first, then each worker's deque.
        // Workers are joined, so we are now the sole accessor of every
        // queue and may call pop() from this (non-owner) thread safely.
        while (true)
        {
            std::optional<BaseTask *> leftoverTask = globalQueue_.pop();
            if (leftoverTask.has_value())
            {
                try
                {
                    executeTask(leftoverTask.value());
                }
                catch (...)
                {
                }
            }
            else
            {
                break;
            }
        }

        for (size_t id = 0; id < NumThreads; id++)
        {
            while (true)
            {
                std::optional<BaseTask *> leftoverTask = tasksDeque_[id].pop();
                if (leftoverTask.has_value())
                {
                    try
                    {
                        executeTask(leftoverTask.value());
                    }
                    catch (...)
                    {
                    }
                }
                else
                {
                    break;
                }
            }
        }
    }

    // Schedule t(args...) for execution on a worker.
    //
    // Returns a future for the result. If the pool is shut down before
    // the task can be enqueued the future will eventually surface a
    // broken_promise on get().
    //
    // Pre: caller must be the single producer thread.
    template <typename Task, typename... Args>
    std::future<std::invoke_result_t<Task, Args...>> submit(Task &&t, Args &&...args)
    {
        using ReturnType = std::invoke_result_t<Task, Args...>;
        // Captures move the callable and its arguments into the lambda
        // so it remains valid after submit() returns. mutable lets us
        // std::move them out on invocation (the lambda is single-shot,
        // matching packaged_task's one-call contract).
        auto invocation = [t = std::forward<Task>(t), ... args = std::forward<Args>(args)]() mutable
        {
            return std::invoke(std::move(t), std::move(args)...);
        };
        std::packaged_task<ReturnType()> packedTask(std::move(invocation));
        std::future<ReturnType> future = packedTask.get_future();
        // unique_ptr keeps the heap allocation safe across the push
        // spin loop; ownership is released only after a successful push,
        // at which point the queue (and eventually executeTask) owns it.
        std::unique_ptr<BaseTask> taskPtr = std::make_unique<DerivedTask<decltype(packedTask)>>(std::move(packedTask));
        bool pushed = true;
        while (!globalQueue_.push(taskPtr.get()))
        {
            // Abort the spin if the pool is being shut down so we
            // don't busy-wait forever on a queue nobody is draining.
            if (!running_.load(std::memory_order_relaxed))
            {
                pushed = false;
                break;
            }
            std::this_thread::yield();
        }
        if (pushed)
        {
            taskPtr.release();
        }
        return future;
    }

private:
    GlobalSPMCQueue<BaseTask *, GlobalCapacity> globalQueue_;
    std::atomic<bool> running_;
    std::array<WorkerDeque<BaseTask *, LocalCapacity>, NumThreads> tasksDeque_;
    std::vector<std::thread> workers_;

    // Worker loop, in priority order:
    //   1. drain own deque (LIFO, best cache locality);
    //   2. take one task from the global queue and republish it through
    //      our own deque so an idle peer has a chance to steal it,
    //      then pop and run something (usually that same task);
    //   3. try to steal from another worker;
    //   4. yield and retry.
    //
    // Republishing global tasks via the local deque (step 2) is what
    // keeps the pool load-balanced even when one thread happens to be
    // the only one pulling from the global queue.
    void workerLoop(size_t id)
    {
        while (running_.load(std::memory_order_acquire))
        {
            std::optional<BaseTask *> task = tasksDeque_[id].pop();
            if (task.has_value())
            {
                executeTask(task.value());
                continue;
            }
            std::optional<BaseTask *> globalTask = globalQueue_.pop();
            if (globalTask.has_value())
            {
                // If the local deque is full we cannot publish for
                // stealers; just run it inline rather than dropping it.
                if (!tasksDeque_[id].push(globalTask.value()))
                {
                    executeTask(globalTask.value());
                    continue;
                }
                task = tasksDeque_[id].pop();
                if (task.has_value())
                {
                    executeTask(task.value());
                }
                continue;
            }
            bool stole = false;
            // Walk peers starting from the neighbour immediately after
            // us so different workers prefer different victims and we
            // don't all hammer worker 0.
            for (size_t i = 1; i < NumThreads; i++)
            {
                size_t threadId = (id + i) % NumThreads;
                std::optional<BaseTask *> stolenTask = tasksDeque_[threadId].steal();
                if (stolenTask.has_value())
                {
                    stole = true;
                    executeTask(stolenTask.value());
                    break;
                }
            }
            if (!stole)
            {
                std::this_thread::yield();
            }
        }
    }

    // Takes ownership of the raw BaseTask* via unique_ptr so the
    // allocation is reclaimed even if execute() throws.
    void executeTask(BaseTask *task)
    {
        std::unique_ptr<BaseTask> taskPtr(task);
        taskPtr->execute();
    }
};
