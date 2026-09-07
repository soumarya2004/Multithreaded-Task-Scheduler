#include "scheduler/scheduler.hpp"
#include "scheduler/thread_pool.hpp"
#include "scheduler/work_stealing_pool.hpp"
#include<gtest/gtest.h>
#include<atomic>
#include<future>
#include<thread>
#include<vector>
using scheduler::Priority;
using scheduler::PriorityThreadPool;
using scheduler::ThreadPool;
using scheduler::WorkStealingThreadPool;

namespace{
    template <typename PoolT, typename SubmitFn>
    void StressManyProducersManyTasks(PoolT& pool, SubmitFn submit_task, int producers, int tasks_per_producer){
        std::atomic<int> counter{0};
        std::vector<std::thread> threads;
        threads.reserve(static_cast<std::size_t>(producers));
        for(int p=0; p<producers; ++p){
            threads.emplace_back([&pool, &counter, &submit_task, tasks_per_producer]{
                for(int i=0; i<tasks_per_producer; ++i){
                    submit_task(pool, [&counter]{
                        counter.fetch_add(1, std::memory_order_relaxed);
                    });
                }
            });
        }
        for(auto& t:threads){
            t.join();
        }
        pool.shutdown();
        EXPECT_EQ(counter.load(), producers*tasks_per_producer);
    }
}
TEST(Concurrency, ThreadPoolSurvivesManyConcurrentProducers){
    ThreadPool pool(4);
    StressManyProducersManyTasks(
        pool, [](ThreadPool& p, auto task){
            p.submit(std::move(task));
        }, /*producers=*/8, /*tasks_per_producer=*/2000);
}
TEST(Concurrency, PriorityThreadPoolSurvivesManyConcurrentProducers){
    PriorityThreadPool pool(4);
    StressManyProducersManyTasks(
        pool, [](PriorityThreadPool& p, auto task){
            p.submit(Priority::NORMAL, std::move(task));
        }, /*producers=*/8, /*tasks_per_producer=*/2000);
}

TEST(Concurrency, WorkStealingThreadPoolSurvivesManyConcurrentProducers){
    WorkStealingThreadPool pool(4);
    StressManyProducersManyTasks(
        pool, [](WorkStealingThreadPool& p, auto task){
            p.submit(std::move(task));
        }, /*producers=*/8, /*tasks_per_producer=*/2000);
}
template <typename PoolT, typename SubmitFn>
static void SubmitVsShutdownNeverLosesATask(SubmitFn make_pool_and_submit, int trials){
    int succeeded=0, threw=0;
    for (int trial=0; trial<trials; ++trial){
        PoolT pool(2);
        std::atomic<bool> ok{false};
        std::atomic<bool> rejected{false};
        std::thread producer([&]{
            try{
                make_pool_and_submit(pool);
                ok.store(true);
            }catch(const std::runtime_error&){
                rejected.store(true);
            }
        });
        pool.shutdown();
        producer.join();
        if(ok.load()){
            ++succeeded;
        }else{
            ASSERT_TRUE(rejected.load()) << "submit() neither succeeded nor threw -- task may be lost";
            ++threw;
        }
    }
    EXPECT_EQ(succeeded + threw, trials);
}
TEST(Concurrency, ThreadPoolSubmitVsShutdownNeverLosesATask){
    SubmitVsShutdownNeverLosesATask<ThreadPool>([](ThreadPool& p){auto f=p.submit([] {}); (void)f;}, 500);
}

TEST(Concurrency, PriorityThreadPoolSubmitVsShutdownNeverLosesATask){
    SubmitVsShutdownNeverLosesATask<PriorityThreadPool>(
        [](PriorityThreadPool& p){auto f=p.submit(Priority::NORMAL, [] {}); (void)f;}, 500);
}
TEST(Concurrency, NoDuplicateExecutionUnderConcurrentSubmission){
    ThreadPool pool(8);
    constexpr int kProducers=8;
    constexpr int kTasksPerProducer=500;
    constexpr int kTotal=kProducers*kTasksPerProducer;
    std::vector<std::atomic<int>> execution_counts(kTotal);
    std::vector<std::thread> producers;
    for (int p=0; p<kProducers; ++p){
        producers.emplace_back([&pool, &execution_counts, p]{
            for (int i=0; i<kTasksPerProducer; ++i){
                const int id=p*kTasksPerProducer+i;
                pool.submit([&execution_counts, id]{
                    execution_counts[static_cast<std::size_t>(id)].fetch_add(1);
                });
            }
        });
    }
    for(auto& t:producers){
        t.join();
    }
    pool.shutdown();
    for(int i=0; i<kTotal; ++i){
        ASSERT_EQ(execution_counts[static_cast<std::size_t>(i)].load(), 1) << "task " << i << " ran more than once";
    }
}