#pragma once
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace scheduler{
    //Phase1: basic thread-pool
    class ThreadPool{
        public:
            //@param num_threads
            // no. of worker threads
            explicit ThreadPool(
                std::size_t num_threads=default_thread_count()
            );
            ~ThreadPool();
            //non-copyable
            ThreadPool(const ThreadPool&)=delete;
            ThreadPool& operator=(const ThreadPool&)=delete;
            //non-moveable
            ThreadPool(ThreadPool&&)=delete;
            ThreadPool& operator=(ThreadPool&&)=delete;
            //add a task to the queue
            void submit(std::function<void()> task);
            //stop accepting/executing tasks
            void shutdown();
            [[nodiscard]]std::size_t worker_count() const noexcept{
                return workers_.size();
            }
            [[nodiscard]]
            std::size_t pending_tasks() const noexcept;
        private:
            static std::size_t default_thread_count() noexcept;
            //func. executed by every worker thread
            void worker_loop();
            std::vector<std::thread> workers_;
            std::queue<std::function<void()>> tasks_;
            mutable std::mutex queue_mutex_;
            std::condition_variable queue_cv_;
            bool stopping_=false;
    };
}// namespace scheduler