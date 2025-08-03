#ifndef THREADPOOL_HPP
#define THREADPOOL_HPP

#include <functional>
#include <future>
#include <queue>
#include <thread>

class ThreadPool {
public:
    enum Priority {
        Low = 0,
        Medium = 1,
        High = 2,
    };

    explicit ThreadPool(size_t n);
    ~ThreadPool();

    void stop();
    void wait();
    bool wait_for(std::chrono::milliseconds timeout);

    template<class Callable, class ... Args>
    auto submit(Callable&& callable, Priority priority = Medium, Args&&... args)
        -> std::pair<std::future<typename std::invoke_result<Callable, Args...>::type>, std::shared_ptr<bool>>;

private:
    std::vector<std::thread> workers;
    using Task = std::pair<Priority, std::function<void()>>;

    static constexpr auto comparator = [](const Task& a, const Task& b) {
        return a.first < b.first;  // Higher priority gets popped first
    };

    std::priority_queue<Task, std::vector<Task>, decltype(comparator)> tasks{comparator};

    std::mutex mtx;
    std::condition_variable cv, allTasksDoneCv;
    int activeTasks = 0;
    bool stopRequested = false;

    void workerLoop();
};

template<typename Callable, typename... Args>
auto ThreadPool::submit(Callable&& callable, Priority priority, Args&&... args)
    -> std::pair<std::future<std::invoke_result_t<Callable, Args...>>, std::shared_ptr<bool>> {

    using ReturnType = std::invoke_result_t<Callable, Args...>;

    auto cancelled = std::make_shared<bool>(false);
    auto taskPtr = std::make_shared<std::packaged_task<ReturnType()>>(
        std::bind(std::forward<Callable>(callable), std::forward<Args>(args)...)
    );
    auto future = taskPtr->get_future();

    {
        std::unique_lock lock(mtx);
        if (stopRequested)
            throw std::runtime_error("ThreadPool has been stopped. Cannot submit more tasks");

        tasks.push({priority, [taskPtr, cancelled] {
            if (*cancelled) return;
            (*taskPtr)();
        }});
    }

    cv.notify_one();
    return {std::move(future), cancelled};
}

#endif // THREADPOOL_HPP
