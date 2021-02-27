#pragma once

#include "detail/tracer.hpp"
#include "event.hpp"
#include "scheduler.hpp"
#include "source_location.hpp"
#include <coroutine>
#include <cstdlib>
#include <limits>

namespace coop
{
// This is the default "payload" and configuration associated with a task. If
// you'd like to include additional information, simply create your own struct
// or class and specify it as the second template parameter of the task_t below.
// Use information here *might* be thread or cpu affinity, priority, debug
// information, etc.
//
// It can have state, and also override functionality
struct task_control_t final
{
    static void* alloc(size_t size)
    {
        return std::malloc(size);
    }

    static void free(void* ptr)
    {
        std::free(ptr);
    }
};

template <typename T>
concept TaskControl = requires(T t, size_t size, void* ptr)
{
    {
        T::alloc(size)
    }
    ->std::same_as<void*>;
    T::free(ptr);
};

template <typename T = void, bool Joinable = false, TaskControl C = task_control_t>
class task_t final
{
public:
    struct promise_type
    {
        // When a coroutine suspends, the precursor stores the handle to the
        // resume point, which immediately following the suspend point.
        std::coroutine_handle<> precursor;

        event_ref_t event;

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
            // On coroutine entry, we store as the precursor a handle
            // corresponding to the next sequence point from the caller.
            task_t out{std::coroutine_handle<promise_type>::from_promise(*this)};
            if constexpr (Joinable)
            {
                // For joinable tasks, initialize the event so we can wait on it
                // later
                out.event_.init();
                event = out.event_.ref();
            }
            return out;
        }

        void
        return_value(T const& value) noexcept(std::is_nothrow_copy_assignable_v<T>)
        {
            data = value;
        }

        void
        return_value(T&& value) noexcept(std::is_nothrow_move_assignable_v<T>)
        {
            data = std::move(value);
        }

        // Do not suspend immediately on entry of a coroutine
        std::suspend_never initial_suspend() const noexcept
        {
            return {};
        }

        // Define what happens on coroutine exit
        auto final_suspend() noexcept
        {
            struct awaiter_t
            {
                bool await_ready() const noexcept
                {
                    return false;
                }

                void await_resume() const noexcept
                {
                }

                std::coroutine_handle<> await_suspend(
                    std::coroutine_handle<promise_type> coroutine) const noexcept
                {
                    // Check if this coroutine is being finalized from the
                    // middle of a "precursor" coroutine and hop back there to
                    // continue execution while *this* coroutine is suspended.

                    auto precursor = coroutine.promise().precursor;
                    return precursor ? precursor : std::noop_coroutine();
                }
            };

            if constexpr (Joinable)
            {
                event.signal();
            }

            return awaiter_t{};
        }

        void unhandled_exception() const noexcept
        {
            // Coop doesn't currently handle exceptions.
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
    {
        std::swap(coroutine_, other.coroutine_);
        std::swap(event_, other.event_);
    }
    task_t& operator=(task_t&& other) noexcept
    {
        if (this != other)
        {
            std::swap(coroutine_, other.coroutine_);
            std::swap(event_, other.event_);
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

    void join() const
    {
        static_assert(Joinable,
                      "Cannot join task that doesn't specify joinability as a "
                      "template parameter");
        event_.wait();
    }

    // When awaiting a task, avoid suspending at all if the associated coroutine
    // is finished already
    bool await_ready() const noexcept
    {
        return coroutine_.done();
    }

    // The return value of await_resume is the final result of `co_await
    // this_task` once the coroutine associated with this task completes
    T await_resume() const noexcept
    {
        return std::move(coroutine_.promise().data);
    }

    // When suspending from a coroutine *within* this task's coroutine, save the
    // resume point (to be resumed when the inner coroutine finalizes)
    void await_suspend(std::coroutine_handle<> coroutine) const noexcept
    {
        coroutine_.promise().precursor = coroutine;
    }

private:
    std::coroutine_handle<promise_type> coroutine_;
    event_t event_;
};

// Explicit void specialization of task_t is identical except no data is
// contained in the promise_type
template <bool Joinable, TaskControl C>
class task_t<void, Joinable, C>
{
public:
    struct promise_type
    {
        // When a coroutine suspends, the precursor stores the handle to the
        // resume point, which immediately following the suspend point.
        std::coroutine_handle<> precursor;
        event_ref_t event;

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
            // On coroutine entry, we store as the precursor a handle
            // corresponding to the next sequence point from the caller.
            task_t out{std::coroutine_handle<promise_type>::from_promise(*this)};
            if constexpr (Joinable)
            {
                // For joinable tasks, initialize the event so we can wait on it
                // later
                out.event_.init();
                event = out.event_.ref();
            }
            return out;
        }

        void return_void() const noexcept
        {
        }

        // Do not suspend immediately on entry of a coroutine
        std::suspend_never initial_suspend() const noexcept
        {
            return {};
        }

        // Define what happens on coroutine exit
        auto final_suspend() noexcept
        {
            struct awaiter_t
            {
                bool await_ready() const noexcept
                {
                    return false;
                }

                void await_resume() const noexcept
                {
                }

                std::coroutine_handle<> await_suspend(
                    std::coroutine_handle<promise_type> coroutine) const noexcept
                {
                    // Check if this coroutine is being finalized from the
                    // middle of a "precursor" coroutine and hop back there to
                    // continue execution while *this* coroutine is suspended.

                    auto precursor = coroutine.promise().precursor;
                    return precursor ? precursor : std::noop_coroutine();
                }
            };

            if constexpr (Joinable)
            {
                event.signal();
            }

            return awaiter_t{};
        }

        void unhandled_exception() const noexcept
        {
            // Coop doesn't currently handle exceptions.
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
    {
        std::swap(coroutine_, other.coroutine_);
        std::swap(event_, other.event_);
    }
    task_t& operator=(task_t&& other) noexcept
    {
        if (this != &other)
        {
            std::swap(coroutine_, other.coroutine_);
            std::swap(event_, other.event_);
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

    // A task_t is truthy if it is not associated with an outstanding coroutine
    operator bool() const noexcept
    {
        return !coroutine_ || coroutine_.done();
    }

    void join() const
    {
        static_assert(Joinable,
                      "Cannot join task that doesn't specify joinability as a "
                      "template parameter");
        event_.wait();
    }

    // When awaiting a task, avoid suspending at all if the associated coroutine
    // is finished already
    bool await_ready() const noexcept
    {
        return coroutine_.done();
    }

    void await_resume() const noexcept
    {
    }

    // When suspending from a coroutine *within* this task's coroutine, save the
    // resume point (to be resumed when the inner coroutine finalizes)
    void await_suspend(std::coroutine_handle<> coroutine) const noexcept
    {
        coroutine_.promise().precursor = coroutine;
    }

private:
    std::coroutine_handle<promise_type> coroutine_;
    event_t event_;
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

    COOP_LOG("Suspending coroutine from thread %i\n", std::this_thread::get_id());

    return awaiter_t{scheduler, cpu_mask, priority, source_location};
}
} // namespace coop