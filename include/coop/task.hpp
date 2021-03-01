#pragma once

#include "detail/tracer.hpp"
#include "scheduler.hpp"
#include "source_location.hpp"
#ifdef __clang__
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
#include <semaphore>
#include <unordered_map>

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
#ifndef __clang__
    {
        T::alloc(size)
    }
    ->std::same_as<void*>;
#endif
    T::free(ptr);
};

struct promise_base_t
{
    // When a coroutine suspends, the precursor stores the handle to the
    // resume point, which immediately following the suspend point.
    std::coroutine_handle<> precursor = nullptr;

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
struct task_awaiter_t
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
        // middle of a "precursor" coroutine and hop back there to
        // continue execution while *this* coroutine is suspended.

        auto precursor = coroutine.promise().precursor;
        return precursor ? precursor : std::noop_coroutine();
    }
};

struct join_data_t
{
    std::binary_semaphore sem{0};
};

template <bool Joinable>
class task_base_t
{
protected:
    using promise_type = promise_base_t;
};

template <>
class task_base_t<true>
{
public:
    void join()
    {
        join_data_->sem.acquire();
    }

protected:
    struct promise_type : public promise_base_t
    {
        join_data_t* join_data = nullptr;
    };

    void join_init()
    {
        join_data_ = std::make_unique<join_data_t>();
    }

    std::unique_ptr<join_data_t> join_data_;
};

template <typename T = void, bool Joinable = false, TaskControl C = task_control_t>
class task_t final : public task_base_t<Joinable>
{
public:
    struct promise_type : public task_base_t<Joinable>::promise_type
    {
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
                out.join_init();
                this->join_data = out.join_data_.get();
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

        task_awaiter_t<promise_type> final_suspend() noexcept
        {
            if constexpr (Joinable)
            {
                this->join_data->sem.release();
            }

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
    {
        if (coroutine_)
        {
            coroutine_.destroy();
        }
        coroutine_       = other.coroutine_;
        other.coroutine_ = nullptr;

#ifdef _MSC_VER
        // On recent MSVC versions, the move constructor of the base template
        // member field isn't invoked (??)
        if constexpr (Joinable)
        {
            this->join_data_ = std::move(other.join_data_);
        }
#endif
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

#ifdef _MSC_VER
            // On recent MSVC versions, the move constructor of the base
            // template member field isn't invoked (??)
            if constexpr (Joinable)
            {
                this->join_data_ = std::move(other.join_data_);
            }
#endif
        }
        return *this;
    }
    ~task_t() noexcept
    {
        if (coroutine_)
        {
            coroutine_.destroy();
            coroutine_ = nullptr;
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
                out.join_init();
                this->join_data = out.join_data_.get();
            }
            return out;
        }

        void return_void() const noexcept
        {
        }

        task_awaiter_t<promise_type> final_suspend() noexcept
        {
            if constexpr (Joinable)
            {
                this->join_data->sem.release();
            }

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
    {
        if (coroutine_)
        {
            coroutine_.destroy();
        }
        coroutine_       = other.coroutine_;
        other.coroutine_ = nullptr;

#ifdef _MSC_VER
        // On recent MSVC versions, the move constructor of the base
        // template member field isn't invoked (??)
        if constexpr (Joinable)
        {
            this->join_data_ = std::move(other.join_data_);
        }
#endif
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

#ifdef _MSC_VER
            // On recent MSVC versions, the move constructor of the base
            // template member field isn't invoked (??)
            if constexpr (Joinable)
            {
                this->join_data_ = std::move(other.join_data_);
            }
#endif
        }
        return *this;
    }
    ~task_t() noexcept
    {
        if (coroutine_)
        {
            coroutine_.destroy();
            coroutine_ = nullptr;
        }
    }

    // A task_t is truthy if it is not associated with an outstanding coroutine
    operator bool() const noexcept
    {
        return !coroutine_ || coroutine_.done();
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

    COOP_LOG("Suspending coroutine from thread %i\n", std::this_thread::get_id());

    return awaiter_t{scheduler, cpu_mask, priority, source_location};
}
} // namespace coop