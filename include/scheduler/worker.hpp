#pragma once
#include "scheduler/task.hpp"
#include<cstddef>
#include<deque>
#include<mutex>
#include<optional>
#include<atomic>
#include<cstdint>

namespace scheduler{
    class WorkStealingDeque{
        public:
            WorkStealingDeque()=default;
            WorkStealingDeque(const WorkStealingDeque&)=delete;
            WorkStealingDeque& operator=(const WorkStealingDeque&)=delete;
            WorkStealingDeque(WorkStealingDeque&&)=delete;
            WorkStealingDeque& operator=(WorkStealingDeque&&)=delete;
            void push_back(Task task);
            std::optional<Task> try_pop_back();
            std::optional<Task> try_steal();
            [[nodiscard]] std::size_t size() const noexcept;
            [[nodiscard]] std::uint64_t contention_count() const noexcept {
                return contention_count_.load(std::memory_order_relaxed);
            }
        private:
            mutable std::mutex mutex_;
            std::deque<Task> tasks_;
            mutable std::atomic<std::uint64_t> contention_count_{0};
    };
}