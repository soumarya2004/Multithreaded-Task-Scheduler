#include "scheduler/thread_pool.hpp"
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <vector>
#include <stdexcept>

int main(){
    using namespace std::chrono_literals;
    //test 1: basic execution across multiple workers
    {
        scheduler::ThreadPool pool(4);
        std::atomic<int> counter{0};
        constexpr int kNumTasks=10'000;
        for(int i=0; i<kNumTasks; ++i){
            pool.submit([&counter]{ 
                counter.fetch_add(1, std::memory_order_relaxed); 
            });
        }
        pool.shutdown();
        assert(counter.load()==kNumTasks);
        std::printf("[test 1] OK: %d/%d tasks executed\n", counter.load(), kNumTasks);
    }
    //test 2: multiple concurrent producers
    {
        scheduler::ThreadPool pool(4);
        std::atomic<int> counter{0};
        constexpr int kProducers=8;
        constexpr int kTasksPerProducer=2'000;
        std::vector<std::thread> producers;
        for(int p=0; p<kProducers; ++p){
            producers.emplace_back([&pool, &counter]{
                for(int i=0; i<kTasksPerProducer; ++i){
                    pool.submit([&counter]{
                        counter.fetch_add(1, std::memory_order_relaxed);
                    });
                }
            });
        }
        for(auto& t:producers){
            t.join();
        }
        pool.shutdown();
        const int expected=kProducers*kTasksPerProducer;
        assert(counter.load()==expected);
        std::printf("[test 2] OK: %d/%d tasks executed (multi-producer)\n.", counter.load(), expected);
    }
    //test 3: submit() after shutdown() must throw
    {
        scheduler::ThreadPool pool(2);
        pool.shutdown();
        bool threw=false;
        try{
            pool.submit([]{});
        }catch(const std::runtime_error&){
            threw=true;
        }
        assert(threw);
        std::printf("[test 3] OK: submit() after shutdown correctly threw\n");
    }
    //test 4: an exception inside a task must not kill the pool
    {
        scheduler::ThreadPool pool(2);
        std::atomic<int> ran{0};
        pool.submit([]{
            throw std::runtime_error("boom");
        });
        pool.submit([&ran]{
            ran.fetch_add(1, std::memory_order_relaxed);
        });
        pool.shutdown();
        assert(ran.load()==1);
        std::printf("[test 4] OK: pool survived a throwing task\n");
    }
    //test 5: pending_tasks() reports queued tasks
    {
        scheduler::ThreadPool pool(1);
        std::atomic<bool> started{false};
        pool.submit([&started]{
            started.store(true, std::memory_order_release);
            std::this_thread::sleep_for(50ms);
        });
        while(!started.load(std::memory_order_acquire)){
            std::this_thread::yield();
        }
        pool.submit([]{});
        pool.submit([]{});
        pool.submit([]{});
        assert(pool.pending_tasks()==3);
        pool.shutdown();
        std::printf("[test 5] OK: pending_tasks() correctly reported 3 queued tasks\n");
    }
    std::printf("all phase 1 smoke tests passed\n");
    return 0;
}