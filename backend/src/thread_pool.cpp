#include "thread_pool.h"

namespace haihunhou {

ThreadPool::ThreadPool(size_t num_threads)
    : running_(false)
    , num_threads_(num_threads) {
    start();
}

ThreadPool::~ThreadPool() {
    stop();
}

void ThreadPool::start() {
    if (running_.load()) return;
    running_.store(true);
    workers_.reserve(num_threads_);
    for (size_t i = 0; i < num_threads_; ++i) {
        workers_.emplace_back(&ThreadPool::workerLoop, this);
    }
}

void ThreadPool::stop() {
    if (!running_.exchange(false)) return;
    cv_.notify_all();
    for (auto& w : workers_) {
        if (w.joinable()) w.join();
    }
    workers_.clear();
}

size_t ThreadPool::pendingTasks() const {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return tasks_.size();
}

void ThreadPool::workerLoop() {
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            cv_.wait(lock, [this] {
                return !running_.load() || !tasks_.empty();
            });
            if (!running_.load() && tasks_.empty()) return;
            if (!tasks_.empty()) {
                task = std::move(tasks_.front());
                tasks_.pop();
            }
        }
        if (task) {
            task();
        }
    }
}

ThreadPool& ThreadPool::instance() {
    static ThreadPool pool(4);
    return pool;
}

}
