#pragma once
#include "scheduler/task.hpp"
#include "scheduler/worker.hpp"
#include<atomic>
#include<condition_variable>
#include<cstddef>
#include<future>
#include<memory>
#include<mutex>
#include<thread>
#include<type_traits>
#include<utility>
#include<vector>

namespace scheduler{
    class WorkStealingThreadPool{
        public:
            explicit WorkStealingThreadPool(std::size_t num_threads=default_thread_count());
            ~WorkStealingThreadPool();
            WorkStealingThreadPool(const WorkStealingThreadPool&)=delete;
            WorkStealingThreadPool& operator=(const WorkStealingThreadPool&)=delete;
            WorkStealingThreadPool(WorkStealingThreadPool&&)=delete;
            WorkStealingThreadPool& operator=(WorkStealingThreadPool&&)=delete;
            template <typename F, typename... Args>
            auto submit(F&& f, Args&&... args)->std::future<std::invoke_result_t<F, Args...>>;
            void shutdown();
            [[nodiscard]] std::size_t worker_count() const noexcept{
                return queues_.size();
            }
            [[nodiscard]] std::size_t pending_tasks() const noexcept;
        private:
            static std::size_t default_thread_count() noexcept;
            void worker_loop(std::size_t self_index);
            void enqueue(Task task);
            std::vector<std::unique_ptr<WorkStealingDeque>> queues_;
            std::vector<std::thread> workers_;
            std::atomic<std::size_t> next_queue_{0};
            std::atomic<std::size_t> active_submitters_{0};
            std::atomic<bool> stopping_{false};
            std::atomic<bool> drained_{false};
            std::mutex doorbell_mutex_;
            std::condition_variable doorbell_cv_;
    };
    template <typename F, typename... Args>
    auto WorkStealingThreadPool::submit(F&& f, Args&&... args)->std::future<std::invoke_result_t<F, Args...>> {
        using ReturnType=std::invoke_result_t<F, Args...>;
        auto bound_call = [f = std::forward<F>(f), ... captured_args=std::forward<Args>(args)]() mutable->ReturnType {
            return std::invoke(f, captured_args...);
        };
        auto task=std::make_shared<std::packaged_task<ReturnType()>>(std::move(bound_call));
        std::future<ReturnType> future=task->get_future();
        enqueue([task](){
            (*task)();
        });
        return future;
    }
}