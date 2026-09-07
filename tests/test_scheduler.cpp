#include "scheduler/work_stealing_pool.hpp"
#include <gtest/gtest.h>
#include<atomic>
#include<chrono>
#include<future>
#include<stdexcept>
#include<vector>
#include<cstdio>
using namespace std::chrono_literals;
using scheduler::WorkStealingThreadPool;

TEST(WorkStealingThreadPool, ExecutesManyTasksCorrectly) {
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
    for(auto& f:futures){
        f.get();
    }
    EXPECT_EQ(counter.load(), kNumTasks);
}
TEST(WorkStealingThreadPool, ArgumentsAndExceptionsWorkLikeThreadPool){
    WorkStealingThreadPool pool(4);
    auto add=[](int a, int b){return a + b;};
    EXPECT_EQ(pool.submit(add, 17, 25).get(), 42);
    auto bad=pool.submit([]()->int {throw std::runtime_error("boom");});
    EXPECT_THROW(bad.get(), std::runtime_error);
}
TEST(WorkStealingThreadPool, LoadBalancesViaStealing){
    constexpr std::size_t kWorkers=4;
    WorkStealingThreadPool pool(kWorkers);
    constexpr int kNumTasks=200;
    constexpr auto kTaskDuration=5ms;
    const auto start = std::chrono::steady_clock::now();
    std::vector<std::future<void>> futures;
    futures.reserve(kNumTasks);
    for(int i=0; i<kNumTasks; ++i){
        futures.push_back(pool.submit([kTaskDuration]{
            std::this_thread::sleep_for(kTaskDuration);
        }));
    }
    for (auto& f:futures){
        f.get();
    }
    const auto elapsed_ms=std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now()-start)
                                 .count();
    const auto serial_ms=kNumTasks*kTaskDuration.count();
    std::printf(
            "Work-stealing elapsed: %lld ms, serial estimate: %lld ms\n",
            static_cast<long long>(elapsed_ms),
            static_cast<long long>(serial_ms)
    );
}
TEST(WorkStealingThreadPool, EveryTaskExecutesExactlyOnce){
    WorkStealingThreadPool pool(8);
    constexpr int kNumTasks=5'000;
    std::vector<std::atomic<int>> execution_counts(kNumTasks);
    std::vector<std::future<void>> futures;
    futures.reserve(kNumTasks);
    for(int i=0; i<kNumTasks; ++i){
        futures.push_back(pool.submit([&execution_counts, i]{
            execution_counts[static_cast<std::size_t>(i)].fetch_add(1);
        }));
    }
    for(auto& f:futures){
        f.get();
    }
    for(int i=0; i<kNumTasks; ++i){
        ASSERT_EQ(execution_counts[static_cast<std::size_t>(i)].load(), 1)
            << "task " << i << " executed " << execution_counts[static_cast<std::size_t>(i)].load() << " times -- "
            << "a stolen task must never also run on its original owner";
    }
}
TEST(WorkStealingThreadPool, SubmitVsShutdownRaceNeverLosesATask){
    constexpr int kTrials=300;
    int succeeded=0, threw=0;
    for(int trial=0; trial<kTrials; ++trial){
        WorkStealingThreadPool pool(2);
        std::atomic<bool> submit_succeeded{false};
        std::atomic<bool> submit_threw{false};
        std::future<int> fut;
        std::thread producer([&]{
            try{
                fut=pool.submit([] {return 99;});
                submit_succeeded.store(true);
            }catch(const std::runtime_error&){
                submit_threw.store(true);
            }
        });
        pool.shutdown();
        producer.join();
        if(submit_succeeded.load()){
            ++succeeded;
            EXPECT_EQ(fut.get(), 99);
        }else{
            ASSERT_TRUE(submit_threw.load()) << "submit() neither succeeded nor threw -- task may be lost";
            ++threw;
        }
    }
    EXPECT_EQ(succeeded+threw, kTrials);
}