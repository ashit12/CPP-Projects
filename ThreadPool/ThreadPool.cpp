#include "ThreadPool.hpp"

ThreadPool::ThreadPool(size_t n) {
    for (size_t i = 0; i < n; ++i) {
        workers.emplace_back([this] {
            this->workerLoop();
        });
    }
}

void ThreadPool::workerLoop() {
    while (true) {
        std::function<void()> task;

        {
            std::unique_lock lock(mtx);
            cv.wait(lock, [this] {
                return stopRequested || !tasks.empty();
            });

            if (stopRequested && tasks.empty())
                return;

            task = std::move(tasks.top().second);
            tasks.pop();
            ++activeTasks;
        }

        task();

        {
            std::unique_lock lock(mtx);
            --activeTasks;
            if (activeTasks == 0 && tasks.empty())
                allTasksDoneCv.notify_all();
        }
    }
}

void ThreadPool::stop() {
    {
        std::unique_lock lock(mtx);
        stopRequested = true;
    }

    cv.notify_all();

    for (auto& worker : workers) {
        if (worker.joinable())
            worker.join();
    }

    workers.clear();
}

void ThreadPool::wait() {
    std::unique_lock lock(mtx);
    allTasksDoneCv.wait(lock, [this] {
        return tasks.empty() && activeTasks == 0;
    });
}

bool ThreadPool::wait_for(std::chrono::milliseconds timeout) {
    std::unique_lock lock(mtx);
    return allTasksDoneCv.wait_for(lock, timeout, [this] {
        return tasks.empty() && activeTasks == 0;
    });
}

ThreadPool::~ThreadPool() {
    stop();
}
