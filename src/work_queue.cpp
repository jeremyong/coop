#include <coop/detail/work_queue.hpp>

#include <algorithm>
#include <cassert>
#include <coop/detail/tracer.hpp>
#include <cstdio>

#ifdef _WIN32
#    define WIN32_LEAN_AND_MEAN
#    include <Windows.h>
#elif defined(__linux__)
#    include <pthread.h>
#elif (__APPLE__)
#endif

using namespace coop;
using namespace coop::detail;

work_queue_t::work_queue_t(scheduler_t& scheduler, uint32_t id)
    : scheduler_{scheduler}
    , id_{id}
    , sem_{0}
{
    snprintf(label_, sizeof(label_), "work_queue:%i", id);
    active_ = true;
    thread_ = std::thread([this] {
#if defined(_WIN32)
        SetThreadAffinityMask(
            thread_.native_handle(), static_cast<uint32_t>(1ull << id_));
#elif defined(__linux__)
        // TODO: Android implementation
        pthread_t thread = pthread_self();
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(id_, &cpuset);
        int result = pthread_setaffinity_np(thread, sizeof(cpuset), &cpuset);

        if (result != 0)
        {
            errno = result;
            perror("Failed to set thread affinity");
            return;
        }
#elif (__APPLE__)
    // TODO: MacOS/iOS implementation
#endif

        while (true)
        {
            sem_.acquire();
            if (!active_)
            {
                return;
            }

            bool did_dequeue = false;

            // Dequeue in a loop because the concurrent queue isn't sequentially
            // consistent
            while (!did_dequeue)
            {
                for (int i = COOP_PRIORITY_COUNT - 1; i >= 0; --i)
                {
                    std::coroutine_handle<> coroutine;
                    if (queues_[i].try_dequeue(coroutine))
                    {
                        COOP_LOG("Dequeueing coroutine on CPU %i thread %zu\n",
                                 id_,
                                 std::hash<std::thread::id>{}(
                                     std::this_thread::get_id()));
                        did_dequeue = true;
                        coroutine.resume();
                        break;
                    }
                }
            }

            // TODO: Implement some sort of work stealing here
        }
    });
}

work_queue_t::~work_queue_t() noexcept
{
    active_ = false;
    sem_.release();
    thread_.join();
}

void work_queue_t::enqueue(std::coroutine_handle<> coroutine,
                           uint32_t priority,
                           source_location_t source_location)
{
    priority = std::clamp<uint32_t>(priority, 0, COOP_PRIORITY_COUNT);
    COOP_LOG("Enqueueing coroutine on CPU %i (%s:%zu)\n",
             id_,
             source_location.file,
             source_location.line);
    queues_[priority].enqueue(coroutine);
    sem_.release();
}
