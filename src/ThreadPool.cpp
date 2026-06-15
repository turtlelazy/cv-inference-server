#include <iostream>

#include "ThreadPool.hpp"

ThreadPool::ThreadPool(size_t num_threads) {
    shutdown_ = false;
    for (size_t i = 0; i < num_threads; i++){
        std::thread worker(&ThreadPool::workerLoop, this);
        workers_.push_back(std::move(worker));
    }
}

ThreadPool::~ThreadPool() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        shutdown_ = true;
    }
    cv_.notify_all();
    for (auto &worker : workers_)
    {
        worker.join();
    }
}

void ThreadPool::workerLoop() {
    std::function<void()> task;
    while (true)
    {
        {
            std::unique_lock<std::mutex> lock(mutex_); // Aquire mutex lock
            cv_.wait(
                lock,
                [this]
                { return shutdown_ || !tasks_.empty();}
            );                    // If not shutting down and queue is empty, wait

            if (shutdown_ && tasks_.empty()) // Deconstructor response; Graceful shutdown behavior
            {
                return;
            }

            std::cout << "Worker: Processing data now...\n"; // Mutex reaquired; safely process
            task = tasks_.front();
            tasks_.pop();
        }
        task();           // Do task outside of mutex block so other threads can work
    }
}

void ThreadPool::enqueue(std::function<void()> task) {
    {
        std::lock_guard<std::mutex> lock(mutex_); // Locks until end of brace
        tasks_.push(task);
    }    
    cv_.notify_one(); // Notify the next waiting thread
}