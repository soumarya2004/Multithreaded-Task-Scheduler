#include "scheduler/scheduler.hpp"
#include<atomic>
#include<cassert>
#include<chrono>
#include<cstdio>
#include<mutex>
#include<string>
#include<vector>
using scheduler::Priority;
using scheduler::PriorityThreadPool;

int main(){
    using namespace std::chrono_literals;
    //test 1: among QUEUED tasks, higher priority runs first
    {
        constexpr std::size_t kWorkers=1;
        PriorityThreadPool pool(kWorkers);
        std::atomic<int> blockers_started{0};
        std::vector<std::future<void>> blockers;
        for (std::size_t i=0; i<kWorkers; ++i) {
            blockers.push_back(pool.submit(Priority::NORMAL, [&blockers_started]{
                blockers_started.fetch_add(1, std::memory_order_relaxed);
                std::this_thread::sleep_for(80ms);
            }));
        }
        while(blockers_started.load(std::memory_order_relaxed)!=static_cast<int>(kWorkers)){
            std::this_thread::yield();
        }
        std::mutex order_mutex;
        std::vector<std::string> execution_order;
        auto record=[&](const char* label) {
            std::lock_guard<std::mutex> lock(order_mutex);
            execution_order.push_back(label);
        };
        auto low=pool.submit(Priority::LOW, [&]{
            record("LOW");
        });
        auto normal=pool.submit(Priority::NORMAL, [&]{
            record("NORMAL");
        });
        auto high=pool.submit(Priority::HIGH, [&]{
            record("HIGH");
        });
        auto critical=pool.submit(Priority::CRITICAL, [&]{
            record("CRITICAL");
        });
        for(auto& b:blockers){
            b.get();
        }
        low.get();
        normal.get();
        high.get();
        critical.get();
        assert(execution_order.size()==4);
        assert(execution_order[0]=="CRITICAL");
        assert(execution_order[1]=="HIGH");
        assert(execution_order[2]=="NORMAL");
        assert(execution_order[3]=="LOW");
        std::printf("[test 1] OK: queued tasks executed in priority order: %s > %s > %s > %s\n",
                execution_order[0].c_str(), execution_order[1].c_str(),
                execution_order[2].c_str(), execution_order[3].c_str());
    }
    //test 2: FIFO tie-break among EQUAL priorities
    {
        constexpr std::size_t kWorkers=1;
        PriorityThreadPool pool(kWorkers);
        std::atomic<bool> blocker_started{false};
        auto blocker=pool.submit(Priority::NORMAL, [&]{
            blocker_started.store(true, std::memory_order_release);
            std::this_thread::sleep_for(80ms);
        });
        while(!blocker_started.load(std::memory_order_acquire)){
            std::this_thread::yield();
        }
        std::mutex order_mutex;
        std::vector<int> execution_order;
        std::vector<std::future<void>> futures;
        for(int i=0; i<5; ++i){
            futures.push_back(pool.submit(Priority::HIGH, [&, i]{
                std::lock_guard<std::mutex> lock(order_mutex);
                execution_order.push_back(i);
            }));
        }
        blocker.get();
        for(auto& f:futures){
            f.get();
        }
        assert(execution_order.size()==5);
        for(int i=0; i<5; ++i){
            assert(execution_order[static_cast<std::size_t>(i)]==i);
        }
        std::printf("[test 2] OK: equal-priority tasks stayed FIFO (0,1,2,3,4)\n");
    }
    //test 3: THE LIMITATION — priority cannot preempt running tasks
    {
        constexpr std::size_t kWorkers=2;
        PriorityThreadPool pool(kWorkers);
        std::atomic<int> low_started{0};
        std::vector<std::future<void>> low_tasks;
        constexpr auto kBlockDuration=150ms;
        for (std::size_t i=0; i<kWorkers; ++i){
            low_tasks.push_back(pool.submit(Priority::LOW, [&]{
                low_started.fetch_add(1, std::memory_order_relaxed);
                std::this_thread::sleep_for(kBlockDuration);
            }));
        }
        while (low_started.load(std::memory_order_relaxed)!=static_cast<int>(kWorkers)){
            std::this_thread::yield();
        }
        const auto submit_time=std::chrono::steady_clock::now();
        auto critical=pool.submit(Priority::CRITICAL, [] {});
        critical.get();
        const auto observed_latency=std::chrono::steady_clock::now()-submit_time;
        for(auto& f:low_tasks){
            f.get();
        }
        const auto latency_ms=std::chrono::duration_cast<std::chrono::milliseconds>(observed_latency).count();
        assert(latency_ms >= 100);
        std::printf(
            "[test 3] CONFIRMED LIMITATION: CRITICAL task still waited ~%lldms "
            "behind already-running LOW tasks (no preemption exists)\n",
            static_cast<long long>(latency_ms));
    }
    //test 4: starvation — continuous HIGH priority work can delay LOW
    {
        constexpr std::size_t kWorkers=1;
        PriorityThreadPool pool(kWorkers);
        std::promise<void> release_blocker;
        auto blocker_gate=release_blocker.get_future().share();
        std::atomic<bool> blocker_started{false};
        std::atomic<bool> low_started{false};
        auto blocker=pool.submit(Priority::NORMAL, [&]{
            blocker_started.store(true, std::memory_order_release);
            blocker_gate.wait();
        });
        while(!blocker_started.load(std::memory_order_acquire)){
            std::this_thread::yield();
        }
        auto low=pool.submit(Priority::LOW, [&]{
            low_started.store(true, std::memory_order_release);
        });
        std::vector<std::future<void>> high_tasks;
        const auto end_time=std::chrono::steady_clock::now()+200ms;
        while(std::chrono::steady_clock::now()<end_time){
            high_tasks.push_back(
                pool.submit(Priority::HIGH, []{
                    std::this_thread::sleep_for(10ms);
                })
            );
            std::this_thread::sleep_for(5ms);
        }
        release_blocker.set_value();
        assert(low.wait_for(50ms)==std::future_status::timeout);
        assert(!low_started.load(std::memory_order_acquire));
        for(auto& task:high_tasks){
            task.get();
        }
        low.get();
        blocker.get();
        assert(low_started.load(std::memory_order_acquire));
        std::printf("[test 4] OK: LOW task was starved while HIGH priority work kept arriving\n");
    }
    std::printf("All priority scheduler tests passed\n");
    return 0;
}