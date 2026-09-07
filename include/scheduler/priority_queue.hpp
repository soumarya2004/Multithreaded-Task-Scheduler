#pragma once
#include "scheduler/task.hpp"
#include<condition_variable>
#include<cstddef>
#include<mutex>
#include<optional>
#include<queue>
#include<vector>

namespace scheduler{
    class PriorityWorkQueue{
        public:
            PriorityWorkQueue()=default;
            PriorityWorkQueue(const PriorityWorkQueue&)=delete;
            PriorityWorkQueue& operator=(const PriorityWorkQueue&)=delete;
            PriorityWorkQueue(PriorityWorkQueue&&)=delete;
            PriorityWorkQueue& operator=(PriorityWorkQueue&&)=delete;
            void push(Task task, Priority priority);
            std::optional<Task> wait_and_pop();
            void close();
            [[nodiscard]] std::size_t size() const noexcept;
            [[nodiscard]] bool empty() const noexcept;
        private:
            struct Compare{
                bool operator()(const PrioritizedTask& a, const PrioritizedTask& b)const noexcept{
                    if(a.priority!=b.priority) {
                        return a.priority<b.priority;
                    }
                    return a.sequence>b.sequence;
                }
            };
            mutable std::mutex mutex_;
            std::condition_variable cv_;
            std::priority_queue<PrioritizedTask, std::vector<PrioritizedTask>, Compare> tasks_;
            std::size_t next_sequence_=0;
            bool closed_=false;
    };
}