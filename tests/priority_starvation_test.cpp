#include "scheduler/scheduler.hpp"
#include<atomic>
#include<cassert>
#include<chrono>
#include<future>
#include<iostream>
#include<thread>
#include<vector>

int main(){
    using namespace std::chrono_literals;
    scheduler::PriorityThreadPool pool(1);
    std::promise<void> release_blocker;
    std::shared_future<void> blocker_gate=release_blocker.get_future().share();
    std::atomic<bool> blocker_started{false};
    auto blocker=pool.submit(
        scheduler::Priority::NORMAL, [&]{
            blocker_started.store(true, std::memory_order_release);
            blocker_gate.wait();
        }
    );
    while(!blocker_started.load(std::memory_order_acquire)){
        std::this_thread::yield();
    }
    std::atomic<bool> low_started{false};
    auto low_future=pool.submit(
        scheduler::Priority::LOW, [&]{
            low_started.store(true, std::memory_order_release);
        }
    );
    assert(!low_started.load(std::memory_order_acquire));
    std::vector<std::future<void>> high_futures;
    const auto stream_end=std::chrono::steady_clock::now()+200ms;
    while (std::chrono::steady_clock::now()<stream_end){
        high_futures.push_back(
            pool.submit(
                scheduler::Priority::HIGH, []{
                    std::this_thread::sleep_for(10ms);
                }
            )
        );
        std::this_thread::sleep_for(5ms);
    }
    release_blocker.set_value();
    assert(
        low_future.wait_for(50ms)==std::future_status::timeout
    );
    assert(!low_started.load(std::memory_order_acquire));
    for (auto& future : high_futures) {
        future.get();
    }
    low_future.get();
    assert(low_started.load(std::memory_order_acquire));
    blocker.get();
    pool.shutdown();
    std::cout << "Priority starvation test passed\n";
}