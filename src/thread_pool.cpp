#include "scheduler/thread_pool.hpp"
#include<stdexcept>
#include<utility>

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
    void ThreadPool::shutdown(){
        queue_.close();
        for(auto& worker: workers_) {
            if(worker.joinable()){
                worker.join();
            }
        }
    }
    void ThreadPool::worker_loop(){
        for(;;){
            std::optional<Task> task=queue_.wait_and_pop();
            if(!task){
                return;
            }
            try{
                (*task)();
            }
            catch(...){}
        }
    }
}