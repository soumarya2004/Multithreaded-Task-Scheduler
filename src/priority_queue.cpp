#include "scheduler/priority_queue.hpp"
#include<stdexcept>
#include<utility>

namespace scheduler{
    void PriorityWorkQueue::push(Task task, Priority priority){
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if(closed_){
                throw std::runtime_error("PriorityWorkQueue::push called on a closed queue");
            }
            tasks_.push(PrioritizedTask{std::move(task), priority, next_sequence_++});
        }
        cv_.notify_one();
    }
    std::optional<Task> PriorityWorkQueue::wait_and_pop(){
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this]{
            return closed_||!tasks_.empty();
        });
        if(tasks_.empty()){
            return std::nullopt;
        }
        Task task=std::move(const_cast<PrioritizedTask&>(tasks_.top()).task);
        tasks_.pop();
        return task;
    }
    void PriorityWorkQueue::close(){
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if(closed_){
                return;
            }
            closed_=true;
        }
        cv_.notify_all();
    }
    std::size_t PriorityWorkQueue::size() const noexcept{
        std::lock_guard<std::mutex> lock(mutex_);
        return tasks_.size();
    }
    bool PriorityWorkQueue::empty() const noexcept{
        std::lock_guard<std::mutex> lock(mutex_);
        return tasks_.empty();
    }
}