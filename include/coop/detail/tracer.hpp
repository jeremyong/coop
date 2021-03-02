#pragma once

#include <cstdint>
#include <thread>

namespace coop
{
namespace detail
{
    inline size_t thread_id() noexcept
    {
        return std::hash<std::thread::id>{}(std::this_thread::get_id());
    }
} // namespace detail
} // namespace coop

#if defined(COOP_TRACE) && !defined(NDEBUG)
#    include <cstdio>

#    define COOP_LOG(...) std::printf(__VA_ARGS__)

#else
#    define COOP_LOG(...)
#endif