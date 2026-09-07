#include "scheduler/work_queue.hpp"
#include "scheduler/worker.hpp"
#include<atomic>
#include<chrono>
#include<cstdio>
#include<memory>
#include<random>
#include<thread>
#include<vector>
using namespace scheduler;
using Clock=std::chrono::steady_clock;

constexpr int kNumWorkers=8;
constexpr int kNumTasks=200'000;
struct Result{
    double elapsed_ms;
    std::uint64_t total_contention;
};

//scenario a: single shared WorkQueue, n consumers
Result run_shared_queue(){
    WorkQueue queue;
    std::atomic<int> completed{0};
    for(int i=0; i<kNumTasks; ++i){
        queue.push([&completed] {completed.fetch_add(1, std::memory_order_relaxed);});
    }
    queue.close();
    const auto start=Clock::now();
    std::vector<std::thread> consumers;
    for(int w=0; w<kNumWorkers; ++w){
        consumers.emplace_back([&queue]{
            while(auto task=queue.wait_and_pop()){
                (*task)();
            }
        });
    }
    for(auto& t:consumers){
        t.join();
    }
    const auto elapsed=std::chrono::duration<double, std::milli>(Clock::now()-start).count();
    if(completed.load()!=kNumTasks){
        std::fprintf(stderr, "FATAL: shared-queue scenario lost tasks (%d/%d)\n", completed.load(), kNumTasks);
        std::abort();
    }
    return {elapsed, queue.contention_count()};
}
//scenario b: n independent WorkStealingDeques, n consumers
Result run_work_stealing(){
    std::vector<std::unique_ptr<WorkStealingDeque>> queues;
    for(int w=0; w<kNumWorkers; ++w){
        queues.push_back(std::make_unique<WorkStealingDeque>());
    }
    std::atomic<int> completed{0};
    for(int i=0; i<kNumTasks; ++i){
        queues[static_cast<std::size_t>(i) % kNumWorkers]->push_back(
            [&completed] {completed.fetch_add(1, std::memory_order_relaxed);});
    }
    const auto start=Clock::now();
    std::vector<std::thread> consumers;
    for(int self=0; self<kNumWorkers; ++self){
        consumers.emplace_back([&queues, &completed, self]{
            std::mt19937 rng(std::random_device{}() ^ static_cast<unsigned>(self));
            const int n=kNumWorkers;
            for(;;){
                if(auto task=queues[static_cast<std::size_t>(self)]->try_pop_back()){
                    (*task)();
                    continue;
                }
                bool stole=false;
                std::uniform_int_distribution<int> dist(0, n-2);
                const int start_off=dist(rng);
                for(int offset=0; offset<n-1; ++offset){
                    const int victim=(self+1+(start_off+offset)%(n-1))%n;
                    if(auto task=queues[static_cast<std::size_t>(victim)]->try_steal()){
                        (*task)();
                        stole=true;
                        break;
                    }
                }
                if(stole){
                    continue;
                }
                if(completed.load(std::memory_order_relaxed)>=kNumTasks){
                    return;
                }
                std::this_thread::yield();
            }
        });
    }
    for(auto& t:consumers){
        t.join();
    }
    const auto elapsed=std::chrono::duration<double, std::milli>(Clock::now()-start).count();
    std::uint64_t total_contention=0;
    for(auto& q:queues){
        total_contention+=q->contention_count();
    }
    if(completed.load()!=kNumTasks){
        std::fprintf(stderr, "FATAL: work-stealing scenario lost tasks (%d/%d)\n", completed.load(), kNumTasks);
        std::abort();
    }
    return {elapsed, total_contention};
}
int main(){
    std::printf("Workload: %d tasks, %d consumer threads, identical trivial task body\n\n", kNumTasks, kNumWorkers);
    constexpr int kRuns=5;
    double shared_total_ms=0, steal_total_ms=0;
    std::uint64_t shared_total_contention=0, steal_total_contention=0;
    for(int r=0; r<kRuns; ++r){
        Result shared=run_shared_queue();
        Result steal=run_work_stealing();
        std::printf("run %d: shared_queue: %.2fms, %llu contended acquisitions | "
                    "work_stealing: %.2fms, %llu contended acquisitions\n",
                    r+1, shared.elapsed_ms, static_cast<unsigned long long>(shared.total_contention),
                    steal.elapsed_ms, static_cast<unsigned long long>(steal.total_contention));
        shared_total_ms+=shared.elapsed_ms;
        steal_total_ms+=steal.elapsed_ms;
        shared_total_contention+=shared.total_contention;
        steal_total_contention+=steal.total_contention;
    }
    std::printf("\n--- Averages over %d runs ---\n", kRuns);
    std::printf("Shared WorkQueue:       %.2fms avg, %.0f contended acquisitions avg (%.1f%% of %d total ops)\n",
                shared_total_ms/kRuns, static_cast<double>(shared_total_contention)/kRuns,
                100.0*(static_cast<double>(shared_total_contention)/kRuns)/kNumTasks, kNumTasks);
    std::printf("Work-stealing deques:   %.2fms avg, %.0f contended acquisitions avg (%.1f%% of %d total ops)\n",
                steal_total_ms/kRuns, static_cast<double>(steal_total_contention)/kRuns,
                100.0*(static_cast<double>(steal_total_contention)/kRuns)/kNumTasks, kNumTasks);
    const double contention_ratio=
        static_cast<double>(shared_total_contention)/static_cast<double>(steal_total_contention==0? 1:steal_total_contention);
    std::printf("\nShared queue saw %.1fx more contended lock acquisitions than work-stealing deques.\n", contention_ratio);
    return 0;
}