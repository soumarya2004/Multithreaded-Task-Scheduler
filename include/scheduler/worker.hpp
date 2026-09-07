#pragma once
#include "scheduler/task.hpp"
#include<cstddef>
#include<deque>
#include<mutex>
#include<optional>

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
        private:
            mutable std::mutex mutex_;
            std::deque<Task> tasks_;
    };
}