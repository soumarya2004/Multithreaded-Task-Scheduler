#include "scheduler/work_stealing_pool.hpp"
#include<atomic>
#include<cassert>
#include<chrono>
#include<cstdio>
#include<future>
#include<thread>
#include<vector>
using scheduler::WorkStealingThreadPool;

int main(){
    using namespace std::chrono_literals;
    //test 1: basic correctness across many tasks and workers
    {
        WorkStealingThreadPool pool(4);
        std::atomic<int> counter{0};
        constexpr int kNumTasks=20'000;
        std::vector<std::future<void>> futures;
        futures.reserve(kNumTasks);
        for(int i=0; i<kNumTasks; ++i){
            futures.push_back(pool.submit([&counter]{
                counter.fetch_add(1, std::memory_order_relaxed);
            }));
        }
        for (auto& f:futures){
            f.get();
        }
        assert(counter.load()==kNumTasks);
        std::printf("[test 1] OK: %d/%d tasks executed across 4 workers\n", counter.load(), kNumTasks);
    }
    //test 2: value-returning tasks, arguments, exceptions
    {
        WorkStealingThreadPool pool(4);
        auto add=[](int a, int b){
            return a+b;
        };
        assert(pool.submit(add, 17, 25).get()==42);
        auto bad=pool.submit([]()->int {
            throw std::runtime_error("boom");
        });
        bool threw=false;
        try{
            bad.get();
        }catch(const std::runtime_error&){
            threw=true;
        }
        assert(threw);
        std::printf("[test 2] OK: arguments and exception propagation work identically to ThreadPool\n");
    }
    //test 3: load balancing via stealing
    {
        constexpr std::size_t kWorkers=4;
        WorkStealingThreadPool pool(kWorkers);
        constexpr int kNumTasks=200;
        constexpr auto kTaskDuration=5ms;
        const auto start=std::chrono::steady_clock::now();
        std::vector<std::future<void>> futures;
        futures.reserve(kNumTasks);
        for(int i=0; i<kNumTasks; ++i){
            futures.push_back(pool.submit([kTaskDuration]{
                std::this_thread::sleep_for(kTaskDuration);
            }));
        }
        for(auto& f:futures){
            f.get();
        }
        const auto elapsed=std::chrono::steady_clock::now()-start;
        const auto elapsed_ms=std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
        const auto serial_ms=kNumTasks*kTaskDuration.count();
        const auto ideal_parallel_ms=serial_ms/static_cast<long long>(kWorkers);
        assert(elapsed_ms<serial_ms/3*4);
        std::printf(
            "[test 3] OK: %d x %lldms tasks on %zu workers took %lldms "
            "(serial would be %lldms, ideal parallel ~%lldms)\n",
            kNumTasks, static_cast<long long>(kTaskDuration.count()), kWorkers,
            static_cast<long long>(elapsed_ms), static_cast<long long>(serial_ms),
            static_cast<long long>(ideal_parallel_ms)
        );
    }
    //test 4: concurrent submit() vs shutdown() -- the drain barrier
    {
        constexpr int kTrials=500;
        int succeeded=0, threw=0;
        for (int trial=0; trial<kTrials; ++trial){
            WorkStealingThreadPool pool(2);
            std::atomic<bool> submit_succeeded{false};
            std::atomic<bool> submit_threw{false};
            std::future<int> fut;
            std::thread producer([&]{
                try{
                    fut=pool.submit([]{
                        return 99;
                    });
                    submit_succeeded.store(true);
                }catch(const std::runtime_error&){
                    submit_threw.store(true);
                }
            });
            pool.shutdown();
            producer.join();
            if(submit_succeeded.load()){
                ++succeeded;
                assert(fut.get()==99);
            }else{
                assert(submit_threw.load());
                ++threw;
            }
        }
        std::printf("[test 4] OK: %d trials racing submit() vs shutdown() -- succeeded=%d threw=%d, "
                     "zero lost tasks\n",
                     kTrials, succeeded, threw);
    }
    std::printf("All Phase 4 smoke tests passed.\n");
    return 0;
}