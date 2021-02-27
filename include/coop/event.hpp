#pragma once

#include <coroutine>
#include <cstdint>

namespace coop
{
class scheduler_t;

// Non-owning reference to an event
class event_ref_t
{
public:
    enum class status_e
    {
        normal,
        abandoned,
        timeout,
        failed
    };

    struct wait_result_t
    {
        status_e status;
        uint32_t index = 0;
    };

    // Return the index of the first event signaled in a given array of events
    static wait_result_t wait_many(event_ref_t* events, uint32_t count);

    event_ref_t() = default;
#if defined(_WIN32)
    event_ref_t(void* handle) noexcept
        : handle_{handle}
    {
    }
#elif defined(__linux__)
    // TODO: Android/Linux implementation
#elif (__APPLE__)
    // TODO: MacOS/iOS implementation
#endif
    event_ref_t(event_ref_t&&)      = default;
    event_ref_t(event_ref_t const&) = default;
    event_ref_t& operator=(event_ref_t&&) = default;
    event_ref_t& operator=(event_ref_t const&) = default;

    void init(bool manual_reset = false, char const* label = nullptr);

    // Check if this event is signaled (returns immediately)
    bool is_signaled() const;
    operator bool() const noexcept
    {
        return is_signaled();
    }

    // Wait (potentially indefinitely) for this event to be signaled
    bool wait() const;

    // Mark this event as signaled
    void signal();

    // Mark this event as unsignaled (needed for events that are manually reset,
    // as opposed to reset after wait)
    void reset();

protected:
    friend class event_t;

#if defined(_WIN32)
    void* handle_ = nullptr;
#elif defined(__linux__)
    // TODO: Android/Linux implementation
#elif (__APPLE__)
    // TODO: MacOS/iOS implementation
#endif
};

class event_t final : public event_ref_t
{
public:
    event_t() = default;
#if defined(_WIN32)
    event_t(void* handle) noexcept
        : event_ref_t{handle}
    {
    }
#elif defined(__linux__)
    // TODO: Android/Linux implementation
#elif (__APPLE__)
    // TODO: MacOS/iOS implementation
#endif
    ~event_t() noexcept;
    event_t(event_t const& other) = delete;
    event_t& operator=(event_t const& other) = delete;
    event_t(event_t&& other) noexcept;
    event_t& operator=(event_t&& other) noexcept;

    event_ref_t ref() const noexcept;

    void set_cpu_affinity(uint32_t affinity) noexcept
    {
        cpu_affinity_ = affinity;
    }

    void set_priority(uint32_t priority) noexcept
    {
        priority_ = priority;
    }

    // Awaiter traits
    bool await_ready() const noexcept
    {
        return is_signaled();
    }

    void await_resume() const noexcept
    {
    }

    // Enqueue coroutine for resumption when this event transitions to the
    // signaled state
    void await_suspend(std::coroutine_handle<> coroutine) noexcept;

private:
    uint64_t cpu_affinity_ = 0;
    uint32_t priority_     = 0;
};
} // namespace coop