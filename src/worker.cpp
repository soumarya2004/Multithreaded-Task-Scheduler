#include "scheduler/worker.hpp"
#include<utility>

namespace scheduler{
    void WorkStealingDeque::push_back(Task task){
        std::lock_guard<std::mutex> lock(mutex_);
        tasks_.push_back(std::move(task));
    }
    std::optional<Task> WorkStealingDeque::try_pop_back(){
        std::lock_guard<std::mutex> lock(mutex_);
        if(tasks_.empty()){
            return std::nullopt;
        }
        Task task=std::move(tasks_.back());
        tasks_.pop_back();
        return task;
    }
    std::optional<Task> WorkStealingDeque::try_steal(){
        std::lock_guard<std::mutex> lock(mutex_);
        if(tasks_.empty()){
            return std::nullopt;
        }
        Task task=std::move(tasks_.front());
        tasks_.pop_front();
        return task;
    }
    std::size_t WorkStealingDeque::size() const noexcept{
        std::lock_guard<std::mutex> lock(mutex_);
        return tasks_.size();
    }
}