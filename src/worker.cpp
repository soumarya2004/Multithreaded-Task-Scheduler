#include "scheduler/worker.hpp"
#include<utility>

namespace scheduler{
    void WorkStealingDeque::push_back(Task task){
        std::unique_lock<std::mutex> lock(mutex_, std::defer_lock);
        if(!lock.try_lock()){
            contention_count_.fetch_add(1, std::memory_order_relaxed);
            lock.lock();
        }
        tasks_.push_back(std::move(task));
    }
    std::optional<Task> WorkStealingDeque::try_pop_back(){
        std::unique_lock<std::mutex> lock(mutex_, std::defer_lock);
        if(!lock.try_lock()){
            contention_count_.fetch_add(1, std::memory_order_relaxed);
            lock.lock();
        }
        if(tasks_.empty()){
            return std::nullopt;
        }
        Task task=std::move(tasks_.back());
        tasks_.pop_back();
        return task;
    }
    std::optional<Task> WorkStealingDeque::try_steal(){
        std::unique_lock<std::mutex> lock(mutex_, std::defer_lock);
        if(!lock.try_lock()){
            contention_count_.fetch_add(1, std::memory_order_relaxed);
            lock.lock();
        }
        if(tasks_.empty()){
            return std::nullopt;
        }
        Task task=std::move(tasks_.front());
        tasks_.pop_front();
        return task;
    }
    std::size_t WorkStealingDeque::size() const noexcept{
        std::unique_lock<std::mutex> lock(mutex_, std::defer_lock);
        if(!lock.try_lock()){
            contention_count_.fetch_add(1, std::memory_order_relaxed);
            lock.lock();
        }
        return tasks_.size();
    }
}