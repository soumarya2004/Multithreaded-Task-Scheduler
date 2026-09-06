#pragma once
#include<functional>

namespace scheduler{
    using Task=std::function<void()>;
}