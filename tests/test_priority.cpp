#include "scheduler/scheduler.hpp"
#include <gtest/gtest.h>
#include<atomic>
#include<chrono>
#include<future>
#include<mutex>
#include<vector>
using namespace std::chrono_literals;
using scheduler::Priority;
using scheduler::PriorityThreadPool;

TEST(PriorityThreadPool, QueuedTasksExecuteInPriorityOrder){
    constexpr std::size_t kWorkers=1;
    PriorityThreadPool pool(kWorkers);
    std::atomic<int> blockers_started{0};
    std::vector<std::future<void>> blockers;
    for(std::size_t i=0; i<kWorkers; ++i){
        blockers.push_back(pool.submit(Priority::NORMAL, [&blockers_started]{
            blockers_started.fetch_add(1, std::memory_order_relaxed);
            std::this_thread::sleep_for(80ms);
        }));
    }
    while(blockers_started.load(std::memory_order_relaxed)!=static_cast<int>(kWorkers)){
        std::this_thread::yield();
    }
    std::mutex order_mutex;
    std::vector<std::string> order;
    auto record=[&](const char* label){
        std::lock_guard<std::mutex> lock(order_mutex);
        order.push_back(label);
    };
    auto low=pool.submit(Priority::LOW, [&]{record("LOW");});
    auto normal=pool.submit(Priority::NORMAL, [&]{record("NORMAL");});
    auto high=pool.submit(Priority::HIGH, [&]{record("HIGH");});
    auto critical=pool.submit(Priority::CRITICAL, [&]{record("CRITICAL");});
    for(auto& b:blockers){
        b.get();
    }
    low.get();
    normal.get();
    high.get();
    critical.get();
    ASSERT_EQ(order.size(), 4u);
    EXPECT_EQ(order[0], "CRITICAL");
    EXPECT_EQ(order[1], "HIGH");
    EXPECT_EQ(order[2], "NORMAL");
    EXPECT_EQ(order[3], "LOW");
}
TEST(PriorityThreadPool, EqualPriorityTasksStayFifo){
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
    std::vector<int> order;
    std::vector<std::future<void>> futures;
    for(int i=0; i<5; ++i){
        futures.push_back(pool.submit(Priority::HIGH, [&, i]{
            std::lock_guard<std::mutex> lock(order_mutex);
            order.push_back(i);
        }));
    }
    blocker.get();
    for(auto& f:futures){
        f.get();
    }
    ASSERT_EQ(order.size(), 5u);
    for(int i=0; i<5; ++i){
        EXPECT_EQ(order[static_cast<std::size_t>(i)], i);
    }
}
TEST(PriorityThreadPool, CannotPreemptAlreadyRunningTasks){
    constexpr std::size_t kWorkers=2;
    PriorityThreadPool pool(kWorkers);
    std::atomic<int> low_started{0};
    std::vector<std::future<void>> low_tasks;
    constexpr auto kBlockDuration=150ms;
    for(std::size_t i=0; i<kWorkers; ++i){
        low_tasks.push_back(pool.submit(Priority::LOW, [&]{
            low_started.fetch_add(1, std::memory_order_relaxed);
            std::this_thread::sleep_for(kBlockDuration);
        }));
    }
    while(low_started.load(std::memory_order_relaxed)!=static_cast<int>(kWorkers)){
        std::this_thread::yield();
    }
    const auto submit_time=std::chrono::steady_clock::now();
    auto critical=pool.submit(Priority::CRITICAL, []{});
    critical.get();
    const auto observed_latency=std::chrono::steady_clock::now()-submit_time;
    for(auto& f:low_tasks){
        f.get();
    }
    const auto latency_ms=std::chrono::duration_cast<std::chrono::milliseconds>(observed_latency).count();
    EXPECT_GE(latency_ms, 100) << "a CRITICAL task must still wait behind already-running LOW tasks -- "
                                   "if this ever fails, something has (incorrectly) added preemption";
}

TEST(PriorityThreadPool, SustainedHighPriorityLoadStarvesLowPriorityTask){
    PriorityThreadPool pool(1);
    std::promise<void> release_blocker;
    std::shared_future<void> blocker_gate(release_blocker.get_future());
    std::atomic<bool> blocker_started{false};
    auto blocker_done=pool.submit(Priority::NORMAL, [&blocker_started, blocker_gate]{
        blocker_started.store(true, std::memory_order_release);
        blocker_gate.wait();
    });
    while(!blocker_started.load(std::memory_order_acquire)){
        std::this_thread::yield();
    }
    auto low_future=pool.submit(Priority::LOW, []{});
    std::vector<std::future<void>> high_futures;
    for (int i=0; i<40; ++i){
        high_futures.push_back(pool.submit(Priority::HIGH, []{
            std::this_thread::sleep_for(10ms);
        }));
        std::this_thread::sleep_for(5ms);
    }
    release_blocker.set_value();
    blocker_done.get();
    EXPECT_EQ(low_future.wait_for(50ms), std::future_status::timeout);
    for (auto& f : high_futures) f.get();
    low_future.get();
}