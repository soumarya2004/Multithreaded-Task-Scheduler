#pragma once
#include "scheduler/task.hpp"
#include "scheduler/work_queue.hpp"
#include<cstddef>
#include<future>
#include<memory>
#include<thread>
#include<type_traits>
#include<utility>
#include<vector>
#include<functional>

namespace scheduler{
    //Phase1: basic thread-pool
    class ThreadPool{
        public:
            // no. of worker threads
            explicit ThreadPool(std::size_t num_threads=default_thread_count());
            ~ThreadPool();
            ThreadPool(const ThreadPool&)=delete;
            ThreadPool& operator=(const ThreadPool&)=delete;
            ThreadPool(ThreadPool&&)=delete;
            ThreadPool& operator=(ThreadPool&&)=delete;
            template <typename F, typename... Args>
            auto submit(F&& f, Args&&... args)->std::future<std::invoke_result_t<F, Args...>>;
            //stop accepting new tasks and drain queued tasks before workers exit
            void shutdown();
            [[nodiscard]]std::size_t worker_count() const noexcept{
                return workers_.size();
            }
            [[nodiscard]]
            std::size_t pending_tasks() const noexcept{
                return queue_.size();
            };
        private:
            static std::size_t default_thread_count() noexcept;
            void worker_loop();
            WorkQueue queue_;
            std::vector<std::thread> workers_;
    };
    template <typename F, typename... Args>
    auto ThreadPool::submit(F&& f, Args&&... args)->std::future<std::invoke_result_t<F, Args...>> {
        using ReturnType=std::invoke_result_t<F, Args...>;
        auto bound_call=[f=std::forward<F>(f), ... captured_args=std::forward<Args>(args)]() mutable->ReturnType {
            return std::invoke(f, captured_args...);
        };
        auto task=std::make_shared<std::packaged_task<ReturnType()>>(std::move(bound_call));
        std::future<ReturnType> future=task->get_future();
        queue_.push([task](){
            (*task)();
        });
        return future;
    }
}// namespace scheduler