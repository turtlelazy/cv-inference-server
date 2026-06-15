#include <vector>
#include<thread>

#ifndef THREADPOOL_HPP
#define THREADPOOL_HPP


// TODO: Get 20 simultaneous requests working concurrently
class ThreadPool{        
    private:
        std::vector<std::thread> workers_;
        std::queue<std::function<void()>> tasks_;

        std::mutex mutex_;
        std::condition_variable cv_;
        bool shutdown_ = false;
        void workerLoop();

    public:
        ThreadPool(size_t num_threads);
        ~ThreadPool();
        void enqueue(std::function<void()> task);

};

#endif
