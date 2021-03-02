#include <coop/scheduler.hpp>

#include <bit>
#include <cassert>
#include <coop/detail/tracer.hpp>
#include <cstdlib>
#include <cstring>
#include <numbers>
#include <thread>

using namespace coop;

scheduler_t& scheduler_t::instance() noexcept
{
    static scheduler_t scheduler;
    return scheduler;
}

scheduler_t::scheduler_t()
{
    // Determine CPU count
    cpu_count_ = std::thread::hardware_concurrency();
    assert(cpu_count_ > 0 && cpu_count_ <= 64
           && "Coop does not yet support CPUs with more than 64 cores");
    cpu_mask_ = (1 << (cpu_count_ + 1)) - 1;

    COOP_LOG("Spawning coop scheduler with %i threads\n", cpu_count_);

    void* raw = operator new[](sizeof(detail::work_queue_t) * cpu_count_);
    queues_   = static_cast<detail::work_queue_t*>(raw);

    for (decltype(cpu_count_) i = 0; i != cpu_count_; ++i)
    {
        new (queues_ + i) detail::work_queue_t(*this, i);
    }

    // Initialize room for 32 events
    event_capacity_      = 32;
    event_count_         = 1;
    events_              = new event_ref_t[event_capacity_];
    event_continuations_ = new event_continuation_t[event_capacity_ - 1];
    event_thread_signal_.init(false, "coop_main_event");
    events_[0] = event_thread_signal_;

    // A high quality PRNG number isn't needed here, as this update counter is
    // used to drive a low discrepancy sequence
    update_ = std::rand();

#ifdef _WIN32
    event_thread_ = std::thread([this] {
        active_ = true;
        while (active_)
        {
            auto [status, index] = event_t::wait_many(events_, event_count_);

            if (status == event_ref_t::status_e::failed
                || status == event_ref_t::status_e::timeout)
            {
                continue;
            }

            if (index == 0)
            {
                // The event at index 0 is special in that it is used to
                // indicate the availability of additional events or to stop
                // this thread
                if (!active_)
                {
                    return;
                }

                // Dequeue continuation requests from the concurrent queue in
                // bulk
                size_t size = pending_events_.size_approx();

                // Resize arrays holding event refs and coroutines if necessary
                if (size + event_count_ > event_capacity_)
                {
                    event_capacity_     = size + event_count_ * 2;
                    event_ref_t* events = new event_ref_t[event_capacity_];
                    std::memcpy(events_, events, sizeof(event_t) * event_count_);
                    delete[] events_;
                    events_ = events;

                    event_continuation_t* event_continuations
                        = new event_continuation_t[event_capacity_ - 1];
                    for (size_t i = 0; i != event_count_ - 1; ++i)
                    {
                        // Use moves here instead of a memcpy in case
                        // std::coroutine_handle<> has a non-trivial move
                        event_continuations[i]
                            = std::move(event_continuations_[i]);
                    }
                    delete[] event_continuations_;
                    event_continuations_ = event_continuations;
                }

                // Note that the number of items we actually dequeue may be more
                // than originally advertised
                size = pending_events_.try_dequeue_bulk(
                    event_continuations_ + event_count_ - 1,
                    event_capacity_ - event_count_);

                for (size_t i = 0; i != size; ++i)
                {
                    events_[i + event_count_]
                        = event_continuations_[i + event_count_ - 1].event;
                }

                COOP_LOG(
                    "Added %zu events to the event processing thread\n", size);
                event_count_ += size;
            }
            else
            {
                COOP_LOG("Event %i signaled on the event processing thread\n",
                         index);

                // An event has been signaled. Enqueue its associated
                // continuation.
                event_continuation_t& continuation
                    = event_continuations_[index - 1];
                schedule(continuation.coroutine,
                         continuation.cpu_affinity,
                         continuation.priority);

                // NOTE: if this event was the only event in the queue (aside
                // from the thread signaler), these swaps are in-place swaps and
                // thus no-ops
                std::swap(events_[index], events_[event_count_ - 1]);
                std::swap(event_continuations_[index - 1],
                          event_continuations_[event_count_ - 1]);
                --event_count_;
            }
        }
    });
#endif
}

scheduler_t::~scheduler_t() noexcept
{
    active_ = false;
#ifdef _WIN32
    events_[0].signal();
    event_thread_.join();
#endif
    delete[] events_;
    delete[] event_continuations_;

    for (decltype(cpu_count_) i = 0; i != cpu_count_; ++i)
    {
        queues_[i].~work_queue_t();
    }
    operator delete[](static_cast<void*>(queues_));
}

void scheduler_t::schedule(std::coroutine_handle<> coroutine,
                           uint64_t cpu_affinity,
                           uint32_t priority,
                           source_location_t source_location)
{
    if (cpu_affinity == 0)
    {
        cpu_affinity = ~cpu_affinity & cpu_mask_;
    }

    for (uint32_t i = 0; i != cpu_count_; ++i)
    {
        if (cpu_affinity & (1ull << i))
        {
            if (queues_[i].size_approx() == 0)
            {
                COOP_LOG("Empty work queue %i identified\n", i);
                queues_[i].enqueue(coroutine, priority, source_location);
                return;
            }
        }
    }

    // All queues appear to be busy, pick a random one with reasonably low
    // discrepancy (Kronecker recurrence sequence)
    uint32_t index = static_cast<uint32_t>(update_++ * std::numbers::phi_v<float>)
                     % std::popcount(cpu_affinity);

    // Iteratively unset bits to determine the nth set bit
    for (uint32_t i = 0; i != index; ++i)
    {
        cpu_affinity &= ~(1 << (std::countr_zero(cpu_affinity) + 1));
    }
    uint32_t queue = std::countr_zero(cpu_affinity);
    COOP_LOG("Work queue %i identified\n", queue);

    queues_[queue].enqueue(coroutine, priority, source_location);
}

void scheduler_t::schedule(std::coroutine_handle<> coroutine,
                           event_ref_t event,
                           uint64_t cpu_affinity,
                           uint32_t priority)
{
    pending_events_.enqueue({coroutine, event, cpu_affinity, priority});
    events_[0].signal();
}
