#pragma once
#include<functional>
#include<cstddef>
#include<cstdint>

namespace scheduler{
    using Task=std::function<void()>;
    enum class Priority: std::uint8_t{
        LOW=0,
        NORMAL=1,
        HIGH=2,
        CRITICAL=3,
    };
    struct PrioritizedTask{
        Task task;
        Priority priority=Priority::NORMAL;
        std::size_t sequence=0;
    };
}