#pragma once

#include "detail/api.hpp"
#include "detail/promise.hpp"
#include "detail/tracer.hpp"
#include "scheduler.hpp"
#include "source_location.hpp"
#include <cstdlib>
#include <functional>
#include <limits>
#if defined(__clang__)
#    include <experimental/coroutine>
namespace std
{
using experimental::noop_coroutine;
using experimental::suspend_never;
using experimental::coroutine_handle;
} // namespace std
#else
#    include <coroutine>
#endif

namespace coop
{
struct task_control_t final
{
    static void* alloc(size_t size)
    {
        return operator new(size);
    }

    static void free(void* ptr)
    {
        operator delete(ptr);
    }
};

// Conform to the TaskControl concept below and pass it as the third template
// parameter to `task_t` to override the allocation behavior
template <typename T>
concept TaskControl = requires(T t, size_t size, void* ptr)
{
#ifndef __clang__
    {
        T::alloc(size)
    }
    ->std::same_as<void*>;
#endif
    T::free(ptr);
};

template <typename T = void, bool Joinable = false, TaskControl C = task_control_t>
class task_t
{
public:
    using task_control_t = C;
    using promise_type   = detail::promise_t<task_t, T, Joinable>;

    task_t() noexcept = default;
    task_t(std::coroutine_handle<promise_type> coroutine) noexcept
        : coroutine_{coroutine}
    {
    }
    task_t(task_t const&) = delete;
    task_t& operator=(task_t const&) = delete;
    task_t(task_t&& other) noexcept
        : coroutine_{other.coroutine_}
    {
        other.coroutine_ = nullptr;
    }
    task_t& operator=(task_t&& other) noexcept
    {
        if (this != &other)
        {
            if (coroutine_)
            {
                coroutine_.destroy();
            }
            coroutine_       = other.coroutine_;
            other.coroutine_ = nullptr;
        }
        return *this;
    }
    ~task_t() noexcept
    {
        if (coroutine_)
        {
            coroutine_.destroy();
        }
    }

    // The dereferencing operators below return the data contained in the
    // associated promise
    [[nodiscard]] auto operator*() noexcept
    {
        static_assert(
            !std::is_same_v<T, void>, "This task doesn't contain any data");
        return std::ref(promise().data);
    }

    [[nodiscard]] auto operator*() const noexcept
    {
        static_assert(
            !std::is_same_v<T, void>, "This task doesn't contain any data");
        return std::cref(promise().data);
    }

    // A task_t is truthy if it is not associated with an outstanding
    // coroutine or the coroutine it is associated with is complete
    [[nodiscard]] operator bool() const noexcept
    {
        return await_ready();
    }

    [[nodiscard]] bool await_ready() const noexcept
    {
        return !coroutine_ || coroutine_.done();
    }

    void join()
    {
        static_assert(Joinable,
                      "Cannot join a task without the Joinable type "
                      "parameter "
                      "set");
        coroutine_.promise().join_sem.acquire();
    }

    // When suspending from a coroutine *within* this task's coroutine, save
    // the resume point (to be resumed when the inner coroutine finalizes)
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> coroutine) noexcept
    {
        return detail::await_suspend(coroutine_, coroutine);
    }

    // The return value of await_resume is the final result of `co_await
    // this_task` once the coroutine associated with this task completes
    auto await_resume() const noexcept
    {
        if constexpr (std::is_same_v<T, void>)
        {
            return;
        }
        else
        {
            return std::move(promise().data);
        }
    }

protected:
    [[nodiscard]] promise_type& promise() const noexcept
    {
        return coroutine_.promise();
    }

    std::coroutine_handle<promise_type> coroutine_ = nullptr;
};

// Suspend the current coroutine to be scheduled for execution on a differeent
// thread by the supplied scheduler. Remember to `co_await` this function's
// returned value.
//
// The least significant bit of the CPU mask, corresponds to CPU 0. A non-zero
// mask will prevent this coroutine from being scheduled on CPUs corresponding
// to bits that are set
//
// Threadsafe only if scheduler_t::schedule is threadsafe (the default one
// provided is threadsafe).
template <Scheduler S = scheduler_t>
inline auto suspend(S& scheduler                             = S::instance(),
                    uint64_t cpu_mask                        = 0,
                    uint32_t priority                        = 0,
                    source_location_t const& source_location = {}) noexcept
{
    struct awaiter_t
    {
        scheduler_t& scheduler;
        uint64_t cpu_mask;
        uint32_t priority;
        source_location_t source_location;

        bool await_ready() const noexcept
        {
            return false;
        }

        void await_resume() const noexcept
        {
        }

        void await_suspend(std::coroutine_handle<> coroutine) const noexcept
        {
            scheduler.schedule(coroutine, cpu_mask, priority, source_location);
        }
    };

    return awaiter_t{scheduler, cpu_mask, priority, source_location};
}

#define COOP_SUSPEND()        \
    co_await ::coop::suspend( \
        ::coop::scheduler_t::instance(), 0, 0, {__FILE__, __LINE__})

#define COOP_SUSPEND1(scheduler) \
    co_await ::coop::suspend(scheduler, 0, 0, {__FILE__, __LINE__})

#define COOP_SUSPEND2(scheduler, cpu_mask) \
    co_await ::coop::suspend(scheduler, cpu_mask, 0, {__FILE__, __LINE__})

#define COOP_SUSPEND3(scheduler, cpu_mask, priority) \
    co_await ::coop::suspend(                        \
        scheduler, cpu_mask, priority, {__FILE__, __LINE__})

#define COOP_SUSPEND4(cpu_mask) \
    co_await ::coop::suspend(   \
        ::coop::scheduler_t::instance(), cpu_mask, 0, {__FILE__, __LINE__})

#define COOP_SUSPEND5(cpu_mask, priority)                     \
    co_await ::coop::suspend(::coop::scheduler_t::instance(), \
                             cpu_mask,                        \
                             priority,                        \
                             {__FILE__, __LINE__})
} // namespace coop