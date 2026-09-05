#include "scheduler/thread_pool.hpp"
#include <stdexcept>
#include <utility>

namespace scheduler{
    std::size_t ThreadPool::default_thread_count() noexcept{
        const unsigned int hw=std::thread::hardware_concurrency();
        return hw==0? 1: static_cast<std::size_t>(hw);
    }
    ThreadPool::ThreadPool(std::size_t num_threads){
        workers_.reserve(num_threads);
        for(std::size_t i=0; i<num_threads; i++){
            workers_.emplace_back([this]{
                worker_loop();
            });
        }
    }
    ThreadPool::~ThreadPool(){
        shutdown();
    }
    void ThreadPool::submit(std::function<void()> task){
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            if(stopping_){
                throw std::runtime_error("ThreadPool::submit called after shutdown");
            }
            tasks_.push(std::move(task));
        }
        queue_cv_.notify_one();
    }
    std::size_t ThreadPool::pending_tasks() const noexcept{
        std::lock_guard<std::mutex> lock(queue_mutex_);
        return tasks_.size();
    }
    void ThreadPool::shutdown(){
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            if(stopping_){
                return;
            }
            stopping_=true;
        }
        queue_cv_.notify_all();
        for(auto& worker: workers_){
            if(worker.joinable()){
                worker.join();
            }
        }
    }
    void ThreadPool::worker_loop(){
        for(;;){
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(queue_mutex_);
                queue_cv_.wait(lock, [this]{
                    return stopping_||!tasks_.empty();
                });
                if(stopping_ && tasks_.empty()){
                    return;
                }
                task=std::move(tasks_.front());
                tasks_.pop();
            }
            try{
                task();
            }
            catch(...){}
        }
    }
}