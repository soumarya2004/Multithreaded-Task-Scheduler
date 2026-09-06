#include<scheduler/work_queue.hpp>
#include<stdexcept>
#include<utility>

namespace scheduler{
    void WorkQueue::push(Task task){
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if(closed_){
                throw std::runtime_error("WorkQueue::push called on a closed queue");
            }
            tasks_.push(std::move(task));
        }
        cv_.notify_one();
    }
    std::optional<Task> WorkQueue::wait_and_pop(){
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this]{
            return closed_||!tasks_.empty();
        });
        if(tasks_.empty()){
            return std::nullopt;
        }
        Task task=std::move(tasks_.front());
        tasks_.pop();
        return task;
    }
    void WorkQueue::close(){
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if(closed_){
                return;
            }
            closed_=true;
        }
        cv_.notify_all();
    }
    std::size_t WorkQueue::size() const noexcept{
        std::lock_guard<std::mutex> lock(mutex_);
        return tasks_.size();
    }
    bool WorkQueue::empty() const noexcept{
        std::lock_guard<std::mutex> lock(mutex_);
        return tasks_.empty();
    }
}