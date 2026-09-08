#include "scheduler/scheduler.hpp"
#include "scheduler/thread_pool.hpp"
#include "scheduler/work_stealing_pool.hpp"
#include<benchmark/benchmark.h>
#include<atomic>
#include<cstdint>
#include<future>
#include<vector>
using scheduler::Priority;
using scheduler::PriorityThreadPool;
using scheduler::ThreadPool;
using scheduler::WorkStealingThreadPool;

namespace{
    constexpr int kBulkTasks=5'000;
    constexpr int kAsyncTasks=300;
    std::uint64_t light_work(){
        std::uint64_t acc=0;
        for(int i=1; i<=200; ++i){
            acc+=static_cast<std::uint64_t>(i)*static_cast<std::uint64_t>(i);
        }
        benchmark::DoNotOptimize(acc);
        return acc;
    }
}
//baselines
static void BM_SingleThreaded_Trivial(benchmark::State& state){
    std::atomic<int> counter{0};
    for(auto _:state){
        for(int i=0; i<kBulkTasks; ++i){
            counter.fetch_add(1, std::memory_order_relaxed);
        }
    }
    state.SetItemsProcessed(state.iterations()*kBulkTasks);
}
BENCHMARK(BM_SingleThreaded_Trivial);
static void BM_SingleThreaded_LightWork(benchmark::State& state){
    for(auto _:state){
        for(int i=0; i<kBulkTasks; ++i){
            benchmark::DoNotOptimize(light_work());
        }
    }
    state.SetItemsProcessed(state.iterations()*kBulkTasks);
}
BENCHMARK(BM_SingleThreaded_LightWork);
static void BM_StdAsync_Trivial(benchmark::State& state){
    std::atomic<int> counter{0};
    for(auto _:state){
        std::vector<std::future<void>> futures;
        futures.reserve(kAsyncTasks);
        for(int i=0; i<kAsyncTasks; ++i){
            futures.push_back(std::async(std::launch::async,
                                          [&counter] {counter.fetch_add(1, std::memory_order_relaxed);}));
        }
        for(auto& f:futures){
            f.get();
        }
    }
    state.SetItemsProcessed(state.iterations()*kAsyncTasks);
}
BENCHMARK(BM_StdAsync_Trivial);
static void BM_StdAsync_LightWork(benchmark::State& state){
    for(auto _:state){
        std::vector<std::future<std::uint64_t>> futures;
        futures.reserve(kAsyncTasks);
        for(int i=0; i<kAsyncTasks; ++i){
            futures.push_back(std::async(std::launch::async, light_work));
        }
        for(auto& f:futures) {
            benchmark::DoNotOptimize(f.get());
        }
    }
    state.SetItemsProcessed(state.iterations()*kAsyncTasks);
}
BENCHMARK(BM_StdAsync_LightWork);
//ThreadPool (phase 2)
static void BM_ThreadPool_Trivial(benchmark::State& state){
    const auto num_workers=static_cast<std::size_t>(state.range(0));
    std::atomic<int> counter{0};
    for(auto _:state){
        state.PauseTiming();
        ThreadPool pool(num_workers);
        state.ResumeTiming();
        std::vector<std::future<void>> futures;
        futures.reserve(kBulkTasks);
        for(int i=0; i<kBulkTasks; ++i){
            futures.push_back(pool.submit([&counter] {counter.fetch_add(1, std::memory_order_relaxed);}));
        }
        for(auto& f:futures){
            f.get();
        }
        state.PauseTiming();
        pool.shutdown();
        state.ResumeTiming();
    }
    state.SetItemsProcessed(state.iterations()*kBulkTasks);
}
BENCHMARK(BM_ThreadPool_Trivial)->Arg(1)->Arg(2)->Arg(4)->Arg(8)->Arg(16);
static void BM_ThreadPool_LightWork(benchmark::State& state){
    const auto num_workers=static_cast<std::size_t>(state.range(0));
    for(auto _:state){
        state.PauseTiming();
        ThreadPool pool(num_workers);
        state.ResumeTiming();
        std::vector<std::future<std::uint64_t>> futures;
        futures.reserve(kBulkTasks);
        for(int i=0; i<kBulkTasks; ++i){
            futures.push_back(pool.submit(light_work));
        }
        for(auto& f:futures){
            benchmark::DoNotOptimize(f.get());
        }
        state.PauseTiming();
        pool.shutdown();
        state.ResumeTiming();
    }
    state.SetItemsProcessed(state.iterations()*kBulkTasks);
}
BENCHMARK(BM_ThreadPool_LightWork)->Arg(1)->Arg(2)->Arg(4)->Arg(8)->Arg(16);
static void BM_ThreadPool_SingleTaskLatency(benchmark::State& state){
    ThreadPool pool(static_cast<std::size_t>(state.range(0)));
    for(auto _:state){
        pool.submit([] {}).get();
    }
    state.PauseTiming();
    pool.shutdown();
    state.ResumeTiming();
}
BENCHMARK(BM_ThreadPool_SingleTaskLatency)->Arg(1)->Arg(2)->Arg(4)->Arg(8);
//PriorityThreadPool (phase 3)
static void BM_PriorityThreadPool_Trivial(benchmark::State& state){
    const auto num_workers=static_cast<std::size_t>(state.range(0));
    std::atomic<int> counter{0};
    for (auto _:state){
        state.PauseTiming();
        PriorityThreadPool pool(num_workers);
        state.ResumeTiming();
        std::vector<std::future<void>> futures;
        futures.reserve(kBulkTasks);
        for(int i=0; i<kBulkTasks; ++i){
            futures.push_back(pool.submit(Priority::NORMAL, [&counter] {counter.fetch_add(1, std::memory_order_relaxed);}));
        }
        for(auto& f:futures){
            f.get();
        }
        state.PauseTiming();
        pool.shutdown();
        state.ResumeTiming();
    }
    state.SetItemsProcessed(state.iterations()*kBulkTasks);
}
BENCHMARK(BM_PriorityThreadPool_Trivial)->Arg(1)->Arg(2)->Arg(4)->Arg(8)->Arg(16);
static void BM_PriorityThreadPool_LightWork(benchmark::State& state){
    const auto num_workers = static_cast<std::size_t>(state.range(0));
    for(auto _:state){
        state.PauseTiming();
        PriorityThreadPool pool(num_workers);
        state.ResumeTiming();
        std::vector<std::future<std::uint64_t>> futures;
        futures.reserve(kBulkTasks);
        for(int i=0; i<kBulkTasks; ++i){
            futures.push_back(pool.submit(Priority::NORMAL, light_work));
        }
        for(auto& f:futures){
            benchmark::DoNotOptimize(f.get());
        }
        state.PauseTiming();
        pool.shutdown();
        state.ResumeTiming();
    }
    state.SetItemsProcessed(state.iterations()*kBulkTasks);
}
BENCHMARK(BM_PriorityThreadPool_LightWork)->Arg(1)->Arg(2)->Arg(4)->Arg(8)->Arg(16);
static void BM_PriorityThreadPool_SingleTaskLatency(benchmark::State& state){
    PriorityThreadPool pool(static_cast<std::size_t>(state.range(0)));
    for(auto _:state){
        pool.submit(Priority::NORMAL, [] {}).get();
    }
    state.PauseTiming();
    pool.shutdown();
    state.ResumeTiming();
}
BENCHMARK(BM_PriorityThreadPool_SingleTaskLatency)->Arg(1)->Arg(2)->Arg(4)->Arg(8);
//WorkStealingThreadPool (phase 4)
static void BM_WorkStealingThreadPool_Trivial(benchmark::State& state){
    const auto num_workers=static_cast<std::size_t>(state.range(0));
    std::atomic<int> counter{0};
    for(auto _:state){
        state.PauseTiming();
        WorkStealingThreadPool pool(num_workers);
        state.ResumeTiming();
        std::vector<std::future<void>> futures;
        futures.reserve(kBulkTasks);
        for(int i=0; i<kBulkTasks; ++i){
            futures.push_back(pool.submit([&counter] {counter.fetch_add(1, std::memory_order_relaxed);}));
        }
        for(auto& f:futures){
            f.get();
        }
        state.PauseTiming();
        pool.shutdown();
        state.ResumeTiming();
    }
    state.SetItemsProcessed(state.iterations()*kBulkTasks);
}
BENCHMARK(BM_WorkStealingThreadPool_Trivial)->Arg(1)->Arg(2)->Arg(4)->Arg(8)->Arg(16);
static void BM_WorkStealingThreadPool_LightWork(benchmark::State& state){
    const auto num_workers=static_cast<std::size_t>(state.range(0));
    for(auto _:state){
        state.PauseTiming();
        WorkStealingThreadPool pool(num_workers);
        state.ResumeTiming();
        std::vector<std::future<std::uint64_t>> futures;
        futures.reserve(kBulkTasks);
        for(int i=0; i<kBulkTasks; ++i){
            futures.push_back(pool.submit(light_work));
        }
        for(auto& f:futures){
            benchmark::DoNotOptimize(f.get());
        }
        state.PauseTiming();
        pool.shutdown();
        state.ResumeTiming();
    }
    state.SetItemsProcessed(state.iterations()*kBulkTasks);
}
BENCHMARK(BM_WorkStealingThreadPool_LightWork)->Arg(1)->Arg(2)->Arg(4)->Arg(8)->Arg(16);
static void BM_WorkStealingThreadPool_SingleTaskLatency(benchmark::State& state){
    WorkStealingThreadPool pool(static_cast<std::size_t>(state.range(0)));
    for(auto _:state){
        pool.submit([] {}).get();
    }
    state.PauseTiming();
    pool.shutdown();
    state.ResumeTiming();
}
BENCHMARK(BM_WorkStealingThreadPool_SingleTaskLatency)->Arg(1)->Arg(2)->Arg(4)->Arg(8);
static void BM_ThreadPool_UnevenLoad(benchmark::State& state){
    const auto num_workers=static_cast<std::size_t>(state.range(0));
    constexpr int kTasks=200;
    for(auto _:state){
        state.PauseTiming();
        ThreadPool pool(num_workers);
        state.ResumeTiming();
        std::vector<std::future<std::uint64_t>> futures;
        futures.reserve(kTasks);
        for(int i=0; i<kTasks; ++i){
            const int multiplier=(i%10==0)? 50:1;
            futures.push_back(pool.submit([multiplier]{
                std::uint64_t acc=0;
                for(int r=0; r<multiplier; ++r){
                    acc+=light_work();
                }
                return acc;
            }));
        }
        for(auto& f:futures){
            benchmark::DoNotOptimize(f.get());
        }
        state.PauseTiming();
        pool.shutdown();
        state.ResumeTiming();
    }
}
BENCHMARK(BM_ThreadPool_UnevenLoad)->Arg(1)->Arg(2)->Arg(4)->Arg(8)->Arg(16);
static void BM_WorkStealingThreadPool_UnevenLoad(benchmark::State& state){
    const auto num_workers=static_cast<std::size_t>(state.range(0));
    constexpr int kTasks=200;
    for(auto _:state){
        state.PauseTiming();
        WorkStealingThreadPool pool(num_workers);
        state.ResumeTiming();
        std::vector<std::future<std::uint64_t>> futures;
        futures.reserve(kTasks);
        for(int i=0; i<kTasks; ++i){
            const int multiplier=(i%10==0)? 50:1;
            futures.push_back(pool.submit([multiplier]{
                std::uint64_t acc=0;
                for(int r=0; r<multiplier; ++r){
                    acc+=light_work();
                }
                return acc;
            }));
        }
        for(auto& f:futures){
            benchmark::DoNotOptimize(f.get());
        }
        state.PauseTiming();
        pool.shutdown();
        state.ResumeTiming();
    }
}
BENCHMARK(BM_WorkStealingThreadPool_UnevenLoad)->Arg(1)->Arg(2)->Arg(4)->Arg(8)->Arg(16);
BENCHMARK_MAIN();