#include <iostream>
#include <chrono>
#include <thread>
#include "ThreadPool.hpp"

int main() {
    std::cout << "[Main] Starting thread pool with 2 threads\n";
    ThreadPool pool(2);

    // Submit a few regular tasks
    auto [f1, c1] = pool.submit([] {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::cout << "[Task 1] Executed\n";
        return 1;
    }, ThreadPool::Medium);

    auto [f2, c2] = pool.submit([] {
        std::cout << "[Task 2] Executed\n";
        return 2;
    }, ThreadPool::High);

    auto [f3, c3] = pool.submit([] {
        std::cout << "[Task 3] Executed\n";
        return 3;
    }, ThreadPool::Low);

    // Wait for tasks to complete
    pool.wait();

    std::cout << "[Main] Task results: "
              << f1.get() << ", "
              << f2.get() << ", "
              << f3.get() << "\n";

    // Submit a quick wait_for test
    auto [f4, c4] = pool.submit([] {
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        std::cout << "[Task 4] Executed\n";
        return 4;
    });

    if (pool.wait_for(std::chrono::milliseconds(200))) {
        std::cout << "[Main] Task 4 result: " << f4.get() << "\n";
    } else {
        std::cout << "[Main] Task 4 timed out\n";
    }

    pool.wait();
    std::cout << "[Main] Final tasks done\n";

    return 0;
}
