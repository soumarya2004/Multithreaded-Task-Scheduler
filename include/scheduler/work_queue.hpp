#pragma once
#include<scheduler/task.hpp>
#include<condition_variable>
#include<cstddef>
#include<mutex>
#include<optional>
#include<queue>

namespace scheduler{
    class WorkQueue{
        public:
            WorkQueue()=default;
            WorkQueue(const WorkQueue&)=delete;
            WorkQueue& operator=(const WorkQueue&)=delete;
            WorkQueue(WorkQueue&&)=delete;
            WorkQueue& operator=(WorkQueue&&)=delete;
            void push(Task task);
            std::optional<Task> wait_and_pop();
            void close();
            [[nodiscard]] std::size_t size() const noexcept;
            [[nodiscard]] bool empty() const noexcept;
        private:
            mutable std::mutex mutex_;
            std::condition_variable cv_;
            std::queue<Task> tasks_;
            bool closed_=false;
    };
}