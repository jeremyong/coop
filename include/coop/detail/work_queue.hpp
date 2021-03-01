#pragma once

#include "concurrentqueue.h"
#include <atomic>
#include <coop/source_location.hpp>
#include <semaphore>
#if defined(__clang__)
#    include <experimental/coroutine>
namespace std
{
using experimental::coroutine_handle;
}
#else
#    include <coroutine>
#endif
#include <thread>

// Currently, COOP supports exactly two priority levels, 0 (default) and 1
// (high)
#define COOP_PRIORITY_COUNT 2

namespace coop
{
class scheduler_t;

namespace detail
{
    class work_queue_t
    {
    public:
        work_queue_t(scheduler_t& scheduler, uint32_t id);
        ~work_queue_t() noexcept;
        work_queue_t(work_queue_t const&) = delete;
        work_queue_t(work_queue_t&&)      = delete;
        work_queue_t& operator=(work_queue_t const&) = delete;
        work_queue_t& operator=(work_queue_t&&) = delete;

        // Returns the approximate size across all queues of any priority
        size_t size_approx() const noexcept
        {
            size_t out = 0;
            for (size_t i = 0; i != COOP_PRIORITY_COUNT; ++i)
            {
                out += queues_[i].size_approx();
            }
            return out;
        }

        void enqueue(std::coroutine_handle<> coroutine,
                     uint32_t priority                 = 0,
                     source_location_t source_location = {});

    private:
        scheduler_t& scheduler_;
        uint32_t id_;
        std::thread thread_;
        std::atomic<bool> active_;
        std::counting_semaphore<> sem_;

        moodycamel::ConcurrentQueue<std::coroutine_handle<>>
            queues_[COOP_PRIORITY_COUNT];

        char label_[64];
    };
} // namespace detail
} // namespace coop