#include "scheduler/thread_pool.hpp"
#include<gtest/gtest.h>
#include<atomic>
#include<chrono>
#include<stdexcept>
#include<string>
#include<vector>
using namespace std::chrono_literals;
using scheduler::ThreadPool;

//basic execution 
TEST(ThreadPool, ExecutesASingleVoidTask){
    ThreadPool pool(2);
    std::atomic<bool> ran{false};
    pool.submit([&ran]{ran.store(true);}).get();
    EXPECT_TRUE(ran.load());
}
TEST(ThreadPool, ExecutesManyTasksAcrossMultipleWorkers){
    ThreadPool pool(4);
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
    EXPECT_EQ(counter.load(), kNumTasks);
}
TEST(ThreadPool, EmptyQueueShutdownReturnsCleanly){
    ThreadPool pool(4);
    pool.shutdown();
    SUCCEED();
}
//futures: value-returning, arguments, heterogeneous types
TEST(ThreadPool, ReturnsAValueThroughTheFuture){
    ThreadPool pool(2);
    std::future<int> result=pool.submit([] {return 21*2;});
    EXPECT_EQ(result.get(), 42);
}
TEST(ThreadPool, ForwardsArgumentsToTheCallable){
    ThreadPool pool(2);
    auto add=[](int a, int b) {return a+b;};
    EXPECT_EQ(pool.submit(add, 17, 25).get(), 42);
}
TEST(ThreadPool, ForwardsHeterogeneousArguments){
    ThreadPool pool(2);
    auto describe=[](const std::string& name, int count, double weight){
        return name+":"+std::to_string(count)+":"+std::to_string(weight);
    };
    auto result=pool.submit(describe, std::string("widget"), 3, 1.5).get();
    EXPECT_EQ(result, "widget:3:1.500000");
}
TEST(ThreadPool, ReturnsNonTrivialTypes){
    ThreadPool pool(2);
    std::future<std::string> result=pool.submit([] {return std::string("hello from a worker");});
    EXPECT_EQ(result.get(), "hello from a worker");
}
//exceptions
TEST(ThreadPool, ExceptionPropagatesThroughTheFuture){
    ThreadPool pool(2);
    std::future<int> result=pool.submit([]()->int {throw std::runtime_error("boom");});
    EXPECT_THROW(
        {
            try{
                result.get();
            }catch(const std::runtime_error& e){
                EXPECT_STREQ(e.what(), "boom");
                throw;
            }
        },
        std::runtime_error);
}
TEST(ThreadPool, PoolSurvivesAThrowingTaskAndKeepsServingOthers){
    ThreadPool pool(2);
    auto bad=pool.submit([]()->int {throw std::runtime_error("boom");});
    auto good=pool.submit([] {return 7;});
    EXPECT_THROW(bad.get(), std::runtime_error);
    EXPECT_EQ(good.get(), 7);
}
//shutdown semantics
TEST(ThreadPool, SubmitAfterShutdownThrowsSynchronously){
    ThreadPool pool(2);
    pool.shutdown();
    EXPECT_THROW({auto f=pool.submit([] {return 1;}); (void)f;}, std::runtime_error);
}
TEST(ThreadPool, ShutdownIsIdempotent){
    ThreadPool pool(2);
    pool.shutdown();
    pool.shutdown();
    SUCCEED();
}
TEST(ThreadPool, QueuedTasksDrainAndResolveOnShutdown){
    ThreadPool pool(1);
    std::atomic<bool> started{false};
    pool.submit([&started]{
        started.store(true, std::memory_order_release);
        std::this_thread::sleep_for(30ms);
    });
    while(!started.load(std::memory_order_acquire)){
        std::this_thread::yield();
    }
    auto f1=pool.submit([] {return 1;});
    auto f2=pool.submit([] {return 2;});
    auto f3=pool.submit([] {return 3;});
    EXPECT_EQ(pool.pending_tasks(), 3u);
    pool.shutdown();
    EXPECT_EQ(f1.get(), 1);
    EXPECT_EQ(f2.get(), 2);
    EXPECT_EQ(f3.get(), 3);
}
//worker synchronization: no task is ever executed more than once
TEST(ThreadPool, EveryTaskExecutesExactlyOnce){
    ThreadPool pool(8);
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
        ASSERT_EQ(execution_counts[static_cast<std::size_t>(i)].load(), 1) << "task " << i << " ran " << execution_counts[static_cast<std::size_t>(i)].load() << " times";
    }
}
//concurrent submission (multiple producers)
TEST(ThreadPool, HandlesConcurrentSubmittersCorrectly){
    ThreadPool pool(4);
    std::atomic<int> counter{0};
    constexpr int kProducers=8;
    constexpr int kTasksPerProducer=1'000;
    std::vector<std::thread> producers;
    for(int p=0; p<kProducers; ++p){
        producers.emplace_back([&pool, &counter]{
            for(int i=0; i<kTasksPerProducer; ++i){
                pool.submit([&counter] {counter.fetch_add(1, std::memory_order_relaxed);});
            }
        });
    }
    for(auto& t:producers){
        t.join();
    }
    pool.shutdown();
    EXPECT_EQ(counter.load(), kProducers*kTasksPerProducer);
}