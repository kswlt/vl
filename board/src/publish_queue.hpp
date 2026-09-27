#pragma once
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <utility>

template<class T, size_t Capacity>
class PublishQueue {
    static_assert(Capacity > 0, "queue must be bounded and nonempty");
public:
    void push(T item) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (items_.size() == Capacity) { items_.pop_front(); ++dropped_; }
            items_.push_back(std::move(item));
        }
        ready_.notify_one();
    }
    bool pop(T &item, std::chrono::milliseconds timeout = std::chrono::milliseconds(100)) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!ready_.wait_for(lock, timeout, [&]{ return !items_.empty(); })) return false;
        item = std::move(items_.front());
        items_.pop_front();
        return true;
    }
    std::pair<size_t, uint64_t> stats() {
        std::lock_guard<std::mutex> lock(mutex_);
        return {items_.size(), dropped_};
    }
private:
    std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<T> items_;
    uint64_t dropped_ = 0;
};
