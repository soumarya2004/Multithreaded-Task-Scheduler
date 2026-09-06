#include "scheduler/thread_pool.hpp"
#include<atomic>
#include<cassert>
#include<chrono>
#include<cstdio>
#include<vector>
#include<stdexcept>
#include<future>
#include<thread>
#include<string>

int main(){
    using namespace std::chrono_literals;
    //test 1: void-returning task, still works via future<void>
    {
        scheduler::ThreadPool pool(4);
        std::atomic<int> counter{0};
        constexpr int kNumTasks=10'000;
        std::vector<std::future<void>> futures;
        futures.reserve(kNumTasks);
        for(int i=0; i<kNumTasks; ++i){
            futures.push_back(pool.submit([&counter]{ 
                counter.fetch_add(1, std::memory_order_relaxed); 
            }));
        }
        for(auto& f:futures){
            f.get();
        }
        pool.shutdown();
        assert(counter.load()==kNumTasks);
        std::printf("[test 1] OK: %d/%d void tasks executed, all futures resolved\n", counter.load(), kNumTasks);
    }
    //test 2: value-returning task
    {
        scheduler::ThreadPool pool(4);
        std::future<int> result=pool.submit([]{
            return 21*2;
        });
        assert(result.get()==42);
        std::printf("[test 2] OK: value-returning task produced 42\n");
    }
    //test 3: task with arguments
    {
        scheduler::ThreadPool pool(4);
        auto add=[](int a, int b){
            return a+b;
        };
        std::future<int> result=pool.submit(add, 17, 25);
        assert(result.get()==42);
        std::printf("[test 3] OK: task with arguments produced 42\n");
    }
    //test 4: exception propagation through the future
    {
        scheduler::ThreadPool pool(4);
        std::future<int> result=pool.submit([]()->int {
            throw std::runtime_error("boom");
        });
        bool threw=false;
        try{
            result.get();
        }catch(const std::runtime_error& e){
            threw=true;
            assert(std::string(e.what())=="boom");
        }
        assert(threw);
        std::printf("[test 4] OK: exception propagated through future.get()\n");
    }
    //test 5: pool survives a throwing task and keeps serving others
    {
        scheduler::ThreadPool pool(2);
        auto bad=pool.submit([]()->int {
            throw std::runtime_error("boom");
        });
        auto good=pool.submit([]{
            return 7;
        });
        bool threw=false;
        try{
            bad.get();
        }catch(const std::runtime_error&){
            threw=true;
        }
        assert(threw);
        assert(good.get()==7);
        std::printf("[test 5] OK: pool kept serving other tasks after one threw\n");
    }
    //test 6: submit() after shutdown() must throw synchronously
    {
        scheduler::ThreadPool pool(2);
        pool.shutdown();
        bool threw=false;
        try{
            auto f=pool.submit([]{
                return 1;
            });
            (void)f;
        }catch(const std::runtime_error&){
            threw=true;
        }
        assert(threw);
        std::printf("[test 6] OK: submit() after shutdown correctly threw\n");
    }
    //test 7: queued-but-not-yet-started tasks still complete on shutdown
    {
        scheduler::ThreadPool pool(1);
        std::atomic<bool> started{false};
        pool.submit([&started]{
            started.store(true, std::memory_order_release);
            std::this_thread::sleep_for(30ms);
        });
        while(!started.load(std::memory_order_acquire)){
            std::this_thread::yield();
        }
        auto f1=pool.submit([]{
            return 1;
        });
        auto f2=pool.submit([]{
            return 2;
        });
        auto f3=pool.submit([]{
            return 3;
        });
        assert(pool.pending_tasks()==3);
        pool.shutdown();
        assert(f1.get()==1);
        assert(f2.get()==2);
        assert(f3.get()==3);
        std::printf("[test 7] OK: queued tasks drained and futures fulfilled on shutdown\n");
    }
    std::printf("all phase 2 smoke tests passed\n");
    return 0;
}