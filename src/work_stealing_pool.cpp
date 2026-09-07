#include "scheduler/work_stealing_pool.hpp"
#include<random>
#include<stdexcept>

namespace scheduler{
    std::size_t WorkStealingThreadPool::default_thread_count() noexcept{
        const unsigned int hw=std::thread::hardware_concurrency();
        return hw==1? 1:static_cast<std::size_t>(hw);
    }
    WorkStealingThreadPool::WorkStealingThreadPool(std::size_t num_threads){
        queues_.reserve(num_threads);
        for(std::size_t i=0; i<num_threads; ++i){
            queues_.push_back(std::make_unique<WorkStealingDeque>());
        }
        workers_.reserve(num_threads);
        for(std::size_t i=0; i<num_threads; ++i){
            workers_.emplace_back([this, i]{
                worker_loop(i);
            });
        }
    }
    WorkStealingThreadPool::~WorkStealingThreadPool(){
        shutdown();
    }
    void WorkStealingThreadPool::enqueue(Task task){
        active_submitters_.fetch_add(1);
        struct Deregister{
            std::atomic<std::size_t>& counter;
            ~Deregister(){
                counter.fetch_sub(1);
            }
        }deregister{active_submitters_};
        if(stopping_.load()){
            throw std::runtime_error("WorkStealingThreadPool::submit called after shutdown");
        }
        const std::size_t idx=next_queue_.fetch_add(1)%queues_.size();
        queues_[idx]->push_back(std::move(task));
        doorbell_cv_.notify_one();
    }
    void WorkStealingThreadPool::shutdown(){
        if(!stopping_.exchange(true)){
            while(active_submitters_.load()!=0){
                std::this_thread::yield();
            }
            drained_.store(true);
            doorbell_cv_.notify_all();
        }
        for(auto& worker:workers_){
            if(worker.joinable()){
                worker.join();
            }
        }
    }
    std::size_t WorkStealingThreadPool::pending_tasks() const noexcept{
        std::size_t total=0;
        for(const auto& queue:queues_){
            total+=queue->size();
        }
        return total;
    }
    void WorkStealingThreadPool::worker_loop(std::size_t self_index){
        std::mt19937 rng(std::random_device{}() ^ static_cast<unsigned>(self_index));
        const std::size_t n=queues_.size();
        auto run=[](Task& task){
            try{
                task();
            }catch(...){}
        };
        auto try_steal_from_a_sibling=[&]()->bool {
            if(n<=1){
                return false;
            }
            std::uniform_int_distribution<std::size_t> dist(0, n-2);
            const std::size_t start=dist(rng);
            for(std::size_t offset=0; offset<n-1; ++offset){
            const std::size_t victim=(self_index+1+(start+offset)%(n-1))%n;
                if(auto task=queues_[victim]->try_steal()){
                    run(*task);
                    return true;
                }
            }
            return false;
        };
        for(;;){
            if(auto task=queues_[self_index]->try_pop_back()){
                run(*task);
                continue;
            }
            if(try_steal_from_a_sibling()){
                continue;
            }
            if(drained_.load()){
                if(auto task=queues_[self_index]->try_pop_back()){
                    run(*task);
                    continue;
                }
                bool found=false;
                for(std::size_t v=0; v<n; ++v){
                    if(v==self_index){
                        continue;
                    }
                    if(auto task=queues_[v]->try_steal()){
                        run(*task);
                        found=true;
                        break;
                    }
                }
                if(found){
                    continue;
                }
                return;
            }
            std::unique_lock<std::mutex> lock(doorbell_mutex_);
            doorbell_cv_.wait_for(lock, std::chrono::microseconds(500));
        }
    }
}