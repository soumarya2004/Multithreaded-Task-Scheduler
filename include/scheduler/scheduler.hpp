#pragma once
#include "scheduler/priority_queue.hpp"
#include "scheduler/task.hpp"
#include<cstddef>
#include<future>
#include<memory>
#include<thread>
#include<type_traits>
#include<utility>
#include<vector>

namespace scheduler{
    class PriorityThreadPool{
        public:
            explicit PriorityThreadPool(std::size_t num_threads=default_thread_count());
            ~PriorityThreadPool();
            PriorityThreadPool(const PriorityThreadPool&)=delete;
            PriorityThreadPool& operator=(const PriorityThreadPool&)=delete;
            PriorityThreadPool(PriorityThreadPool&&)=delete;
            PriorityThreadPool& operator=(PriorityThreadPool&&)=delete;
            template <typename F, typename... Args>
            auto submit(Priority priority, F&& f, Args&&... args)->std::future<std::invoke_result_t<F, Args...>>;
            template <typename F, typename... Args>
            auto submit(F&& f, Args&&... args)->std::future<std::invoke_result_t<F, Args...>>;
            void shutdown();
            [[nodiscard]] std::size_t worker_count() const noexcept{
                return workers_.size();
            }
            [[nodiscard]] std::size_t pending_tasks() const noexcept{
                return queue_.size();
            }
        private:
            static std::size_t default_thread_count() noexcept;
            void worker_loop();
            PriorityWorkQueue queue_;
            std::vector<std::thread> workers_;
    };
    template <typename F, typename... Args>
    auto PriorityThreadPool::submit(Priority priority, F&& f, Args&&... args)
        -> std::future<std::invoke_result_t<F, Args...>>{
        using ReturnType = std::invoke_result_t<F, Args...>;
        auto bound_call = [f = std::forward<F>(f), ... captured_args = std::forward<Args>(args)]() mutable -> ReturnType {
            return std::invoke(f, captured_args...);
        };
        auto task = std::make_shared<std::packaged_task<ReturnType()>>(std::move(bound_call));
        std::future<ReturnType> future = task->get_future();
        queue_.push([task](){
            (*task)();
        }, priority);
        return future;
    }
    template <typename F, typename... Args>
    auto PriorityThreadPool::submit(F&& f, Args&&... args)->std::future<std::invoke_result_t<F, Args...>>{
        return submit(Priority::NORMAL, std::forward<F>(f), std::forward<Args>(args)...);
    }
}