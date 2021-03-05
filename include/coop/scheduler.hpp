#pragma once

#include "detail/api.hpp"
#include "detail/concurrentqueue.h"
#include "detail/work_queue.hpp"
#include "event.hpp"
#include "source_location.hpp"
#include <atomic>
#if defined(__clang__)
#    include <experimental/coroutine>
namespace std
{
using experimental::coroutine_handle;
}
#else
#    include <coroutine>
#endif
#include <cstdint>
#include <thread>

namespace coop
{
class event_ref_t;

template <typename S>
concept Scheduler = requires(S scheduler,
                             std::coroutine_handle<> coroutine,
                             uint64_t cpu_affinity,
                             uint32_t priority,
                             source_location_t source_location)
{
    scheduler.schedule(coroutine, cpu_affinity, priority, source_location);
};

// Implement the Scheduler concept above to use your own coroutine scheduler
class COOP_API scheduler_t final
{
public:
    // Returns the default global threadsafe scheduler
    static scheduler_t& instance() noexcept;

    scheduler_t();
    ~scheduler_t() noexcept;
    scheduler_t(scheduler_t const&) = delete;
    scheduler_t(scheduler_t&&)      = delete;
    scheduler_t& operator=(scheduler_t const&) = delete;
    scheduler_t&& operator=(scheduler_t&&) = delete;

    // Schedules a coroutine to be resumed at a later time as soon as a thread
    // is available. If you wish to provide your own custom scheduler, you can
    // schedule the coroutine in a single-threaded context, or with different
    // runtime behavior.
    //
    // In addition, you are free to handle or ignore the cpu affinity and
    // priority parameters differently. The default scheduler here supports TWO
    // priorities: 0 and 1. Coroutines with priority 1 will (in a best-effort
    // sense), be scheduled ahead of coroutines with priority 0.
    void schedule(std::coroutine_handle<> coroutine,
                  uint64_t cpu_affinity             = 0,
                  uint32_t priority                 = 0,
                  source_location_t source_location = {});

    void schedule(std::coroutine_handle<> coroutine,
                  event_ref_t event,
                  uint64_t cpu_affinity,
                  uint32_t priority);

private:
    friend class detail::work_queue_t;

    struct event_continuation_t
    {
        std::coroutine_handle<> coroutine;
        event_ref_t event;
        uint64_t cpu_affinity;
        uint32_t priority;
    };

    std::thread event_thread_;
    size_t event_count_    = 0;
    size_t event_capacity_ = 0;
    event_t event_thread_signal_;
    event_ref_t* events_                       = nullptr;
    event_continuation_t* event_continuations_ = nullptr;
    size_t temp_storage_size_                  = 0;
    event_continuation_t* temp_storage_        = nullptr;
    moodycamel::ConcurrentQueue<event_continuation_t> pending_events_;

    std::atomic<bool> active_;

    // Allocated as an array. One queue is assigned to each CPU
    detail::work_queue_t* queues_ = nullptr;

    // Used to perform a low-discrepancy selection of work queue to enqueue a
    // coroutine to
    std::atomic<uint32_t> update_;

    // Specifically, this is the number of concurrent threads possible, which
    // may be double the physical CPU count if hyperthreading or similar
    // technology is enabled
    uint32_t cpu_count_;
    uint32_t cpu_mask_;
};
} // namespace coop