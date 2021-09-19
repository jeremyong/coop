#pragma once

#include "tracer.hpp"
#include <atomic>
#include <semaphore>
#include <thread>
#if defined(__clang__)
#    include <experimental/coroutine>
namespace std
{
using experimental::coroutine_handle;
using experimental::noop_coroutine;
using experimental::suspend_never;
} // namespace std
#else
#    include <coroutine>
#endif

namespace coop
{
namespace detail
{
    template <typename P, bool Joinable>
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

            COOP_LOG("Final await for coroutine %p on thread %zu\n",
                     coroutine.address(),
                     detail::thread_id());
            // After acquiring the flag, the other thread's write to the
            // coroutine's continuation must be visible (one-way
            // communication)
            if (coroutine.promise().flag.exchange(true, std::memory_order_acquire))
            {
                // We're not the first to reach here, meaning the
                // continuation is installed properly (if any)
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
    };

    template <typename P>
    struct final_awaiter_t<P, true>
    {
        bool await_ready() const noexcept
        {
            return false;
        }

        void await_resume() const noexcept
        {
        }

        void await_suspend(std::coroutine_handle<P> coroutine) const noexcept
        {
            coroutine.promise().join_sem.release();
            coroutine.destroy();
        }
    };

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

    // All promises need the `continuation` member, which is set when a
    // coroutine is suspended within another coroutine. The `continuation`
    // handle is used to hop back from that suspension point when the inner
    // coroutine finishes.
    template <bool Joinable>
    struct promise_base_t
    {
        constexpr static bool joinable_v = Joinable;

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

    // Joinable tasks need an additional semaphore the joiner can wait on
    template <>
    struct promise_base_t<true> : public promise_base_t<false>
    {
        std::binary_semaphore join_sem{0};
    };

    template <typename Task, typename T, bool Joinable>
    struct promise_t : public promise_base_t<Joinable>
    {
        T data;

        Task get_return_object() noexcept
        {
            // On coroutine entry, we store as the continuation a handle
            // corresponding to the next sequence point from the caller.
            return {std::coroutine_handle<promise_t>::from_promise(*this)};
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

        final_awaiter_t<promise_t, Joinable> final_suspend() noexcept
        {
            return {};
        }
    };

    template <typename Task, bool Joinable>
    struct promise_t<Task, void, Joinable> : public promise_base_t<Joinable>
    {
        Task get_return_object() noexcept
        {
            // On coroutine entry, we store as the continuation a handle
            // corresponding to the next sequence point from the caller.
            return {std::coroutine_handle<promise_t>::from_promise(*this)};
        }

        void return_void() noexcept
        {
        }

        final_awaiter_t<promise_t, Joinable> final_suspend() noexcept
        {
            return {};
        }
    };
} // namespace detail
} // namespace coop
