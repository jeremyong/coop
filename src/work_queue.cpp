#include <coop/detail/work_queue.hpp>

#include <cstdio>
#include <coop/detail/tracer.hpp>
#include <cassert>

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
            std::unique_lock lock{mutex_};
            cvar_.wait(lock);

            for (int i = COOP_PRIORITY_COUNT - 1; i >= 0; --i)
            {
                std::coroutine_handle<> coroutine;
                while (queues_[i].try_dequeue(coroutine))
                {
                    COOP_LOG("Dequeueing coroutine on CPU %i thread %i\n",
                             id_,
                             std::this_thread::get_id());
                    coroutine.resume();
                }
            }

            // TODO: Implement some sort of work stealing here

            if (!active_)
            {
                return;
            }
        }
    });
}

work_queue_t::~work_queue_t() noexcept
{
    active_ = false;
    cvar_.notify_one();
    thread_.join();
}

void work_queue_t::enqueue(std::coroutine_handle<> coroutine,
                           uint32_t priority,
                           source_location_t source_location)
{
    COOP_LOG("Enqueueing coroutine on CPU %i (%s:%zu)\n",
             id_,
             source_location.file,
             source_location.line);
    queues_[priority].enqueue(coroutine);
    cvar_.notify_one();
}
