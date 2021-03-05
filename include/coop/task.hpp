#pragma once

#include "detail/tracer.hpp"
#include "scheduler.hpp"
#include "source_location.hpp"
#if defined(__clang__)
#    include <experimental/coroutine>
namespace std
{
using experimental::noop_coroutine;
using experimental::suspend_never;
} // namespace std
#else
#    include <coroutine>
#endif
#include <cstdlib>
#include <limits>

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

// All promises need the `continuation` member, which is set when a coroutine is
// suspended within another coroutine. The `continuation` handle is used to hop
// back from that suspension point when the inner coroutine finishes.
struct promise_base_t
{
    // When a coroutine suspends, the continuation stores the handle to the
    // resume point, which immediately following the suspend point.
    std::coroutine_handle<> continuation = nullptr;

    std::atomic<bool> flag = false;

    // Do not suspend immediately on entry of a coroutine
    std::suspend_never initial_suspend() const noexcept
    {
        return {};
    }

    void unhandled_exception() const noexcept
    {
        // Coop doesn't currently handle exceptions.
    }
};

template <typename P>
struct final_awaiter_t
{
    bool await_ready() const noexcept
    {
        return false;
    }

    void await_resume() const noexcept
    {
    }

    std::coroutine_handle<>
    await_suspend(std::coroutine_handle<P> coroutine) const noexcept
    {
        // Check if this coroutine is being finalized from the
        // middle of a "continuation" coroutine and hop back there to
        // continue execution while *this* coroutine is suspended.

        if constexpr (P::joinable_v)
        {
            // Joinable tasks are never awaited and so cannot have a
            // continuation by definition
            return std::noop_coroutine();
        }
        else
        {
            COOP_LOG("Final await for coroutine %p on thread %zu\n",
                     coroutine.address(),
                     detail::thread_id());
            // After acquiring the flag, the other thread's write to the
            // coroutine's continuation must be visible (one-way communication)
            if (coroutine.promise().flag.exchange(true, std::memory_order_acquire))
            {
                // We're not the first to reach here, meaning the continuation
                // is installed properly (if any)
                auto continuation = coroutine.promise().continuation;
                if (continuation)
                {
                    COOP_LOG("Resuming continuation %p on %p on thread %zu\n",
                             continuation.address(),
                             coroutine.address(),
                             detail::thread_id());
                    return continuation;
                }
                else
                {
                    COOP_LOG(
                        "Coroutine %p on thread %zu missing continuation\n",
                        coroutine.address(),
                        detail::thread_id());
                }
            }
            return std::noop_coroutine();
        }
    }
};

namespace detail
{
    // Helper function for awaiting on a task. The next resume point is
    // installed as a continuation of the task being awaited.
    template <typename P>
    std::coroutine_handle<>
    await_suspend(std::coroutine_handle<P> base, std::coroutine_handle<> next)
    {
        if constexpr (P::joinable_v)
        {
            // Joinable tasks are never awaited and so cannot have a
            // continuation by definition
            return std::noop_coroutine();
        }
        else
        {
            COOP_LOG("Installing continuation %p for %p on thread %zu\n",
                     next.address(),
                     base.address(),
                     detail::thread_id());
            base.promise().continuation = next;
            // The write to the continuation must be visible to a person that
            // acquires the flag
            if (base.promise().flag.exchange(true, std::memory_order_release))
            {
                // We're not the first to reach here, meaning the continuation
                // won't get read
                return next;
            }
            return std::noop_coroutine();
        }
    }
} // namespace detail

template <bool Joinable>
class task_base_t
{
protected:
    using promise_type = promise_base_t;
};

// For tasks that are joinable, on construction they also allocate a binary
// semaphore (memory stability is required) which is signaled on task completion
template <>
class task_base_t<true>
{
public:
    constexpr static bool joinable_v = true;

protected:
    struct promise_type : public promise_base_t
    {
        std::binary_semaphore join_sem{0};
    };
};

template <typename T = void, bool Joinable = false, TaskControl C = task_control_t>
class task_t final : public task_base_t<Joinable>
{
public:
    struct promise_type : public task_base_t<Joinable>::promise_type
    {
        constexpr static bool joinable_v = Joinable;

        T data;

        static void* operator new(size_t size)
        {
            return C::alloc(size);
        }

        static void operator delete(void* ptr)
        {
            return C::free(ptr);
        }

        task_t get_return_object() noexcept
        {
            // On coroutine entry, we store as the continuation a handle
            // corresponding to the next sequence point from the caller.
            return {std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        void
        return_value(T const& value) noexcept(std::is_nothrow_copy_assignable_v<T>)
        {
            data = value;

            if constexpr (Joinable)
            {
                this->join_sem.release();
            }
        }

        void
        return_value(T&& value) noexcept(std::is_nothrow_move_assignable_v<T>)
        {
            data = std::move(value);

            if constexpr (Joinable)
            {
                this->join_sem.release();
            }
        }

        final_awaiter_t<promise_type> final_suspend() noexcept
        {
            return {};
        }
    };

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

    void join()
    {
        static_assert(Joinable,
                      "Cannot join a task without the Joinable type parameter "
                      "set");
        coroutine_.promise().join_sem.acquire();
    }

    // A task_t is truthy if it is not associated with an outstanding coroutine
    operator bool() const noexcept
    {
        return !coroutine_ || coroutine_.done();
    }

    // The dereferencing operators below return the data contained in the
    // associated promise
    [[nodiscard]] T& operator*() noexcept
    {
        return coroutine_.promise().data;
    }

    [[nodiscard]] T const& operator*() const noexcept
    {
        return coroutine_.promise().data;
    }

    bool await_ready() const noexcept
    {
        return !coroutine_ || coroutine_.done();
    }

    // The return value of await_resume is the final result of `co_await
    // this_task` once the coroutine associated with this task completes
    T await_resume() const noexcept
    {
        return std::move(coroutine_.promise().data);
    }

    // When suspending from a coroutine *within* a task's coroutine, save the
    // resume point (to be resumed when the inner coroutine finalizes)
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> coroutine) noexcept
    {
        return detail::await_suspend(coroutine_, coroutine);
    }

private:
    std::coroutine_handle<promise_type> coroutine_ = nullptr;
};

// Explicit void specialization of task_t is identical except no data is
// contained in the promise_type
template <bool Joinable, TaskControl C>
class task_t<void, Joinable, C> : public task_base_t<Joinable>
{
public:
    struct promise_type : public task_base_t<Joinable>::promise_type
    {
        constexpr static bool joinable_v = Joinable;

        static void* operator new(size_t size)
        {
            return C::alloc(size);
        }

        static void operator delete(void* ptr)
        {
            return C::free(ptr);
        }

        task_t get_return_object() noexcept
        {
            // On coroutine entry, we store as the continuation a handle
            // corresponding to the next sequence point from the caller.
            return {std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        void return_void() noexcept
        {
            if constexpr (Joinable)
            {
                this->join_sem.release();
            }
        }

        final_awaiter_t<promise_type> final_suspend() noexcept
        {
            return {};
        }
    };

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

    void join()
    {
        static_assert(Joinable,
                      "Cannot join a task without the Joinable type parameter "
                      "set");
        coroutine_.promise().join_sem.acquire();
    }

    // A task_t is truthy if it is not associated with an outstanding coroutine
    operator bool() const noexcept
    {
        return !coroutine_ || coroutine_.done();
    }

    bool await_ready() const noexcept
    {
        return !coroutine_ || coroutine_.done();
    }

    void await_resume() const noexcept
    {
    }

    // When suspending from a coroutine *within* this task's coroutine, save the
    // resume point (to be resumed when the inner coroutine finalizes)
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> coroutine) noexcept
    {
        return detail::await_suspend(coroutine_, coroutine);
    }

private:
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