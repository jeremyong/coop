# 🐔 Coop

Coop is a C++20 coroutines-based library to support [*cooperative multitasking*](https://en.wikipedia.org/wiki/Cooperative_multitasking)
in the context of a multithreaded application. The syntax will be familiar to users of `async` and `await` functionality in other
programming languages. Users *do not* need to understand the C++20 coroutines API to use this library.

## Features

- Ships with a default affinity-aware two-priority threadsafe task scheduler.
- The task scheduler is swappable with your own
- Supports scheduling of user-defined code and OS completion events (e.g. events that signal after I/O completes)
- Easy to use, efficient API, with a small and digestible code footprint (hundreds of lines of code, not thousands)

Tasks in Coop are *eager* as opposed to lazy, meaning that upon suspension, the coroutine is immediately dispatched for execution on
a worker with the appropriate affinity. While there are many benefits to structuring things lazily (see this excellent [talk](https://www.youtube.com/watch?v=1Wy5sq3s2rg)),
Coop opts to do things the way it does because:

- Coop was designed to interoperate with existing job/task graph systems
- Coop was originally written within the context of a game engine, where exceptions were not used
- For game engines, having a CPU-toplogy-aware dispatch mechanism is extremely important (consider the architecture of, say, the PS5)

While game consoles don't (yet) support C++20 fully, the hope is that options like Coop will be there when the compiler support gets there as well.

## Limitations

If your use case is too far abreast of Coop's original use case (as above), you may need to do more modification to get Coop to behave the way you want.
The limitations to consider below are:

- Requires a recent C++20 compiler and code that uses Coop headers must also use C++20
- The "event_t" wrapper around Win32 events doesn't have equivalent functionality on other platforms yet (it's provided as a reference for how you might handle your own overlapped IO)
- The Clang implementation of the coroutines API at the moment doesn't work with the GCC stdlib++, so use libc++ instead
- Clang on Windows does not yet support the MSVC coroutines runtime due to ABI differences
- Coop ignores the problem of unhandled exceptions within scheduled tasks

If the above limitations make Coop unsuitable for you, consider the following libraries:

- [CppCoro](https://github.com/lewissbaker/cppcoro) - A coroutine library for C++
- [Conduit](https://github.com/loopperfect/conduit) - Lazy High Performance Streams using Coroutine TS
- [folly::coro](https://github.com/facebook/folly/tree/master/folly/experimental/coro) - a developer-friendly asynchronous C++ framework based on Coroutines TS

## Building and Running the Tests

When configured as a standalone project, the built-in scheduler and tests are enabled by default. To configure and build the project
from the command line:

```bash
mkdir build
cd build
cmake .. # Supply your own generator if you don't want the default generator
cmake --build .
./test/coop_test
```

## Integration Guide

If you don't intend on using the built in scheduler, simply copy the contents of the `include` folder somewhere in your include path.

Otherwise, the recommended integration is done via cmake. For the header only portion, link against the `coop::coop_core` target.

If you'd like both headers and the scheduler implementation, link against `coop::coop`.

Drop this quick cmake snippet somewhere in your `CMakeLists.txt` file to make both of these targets available.

```cmake
include(FetchContent)

FetchContent_Declare(
    coop
    GIT_REPOSITORY https://github.com/jeremyong/coop.git
    GIT_TAG master
    GIT_SHALLOW ON
)
FetchContent_MakeAvailable(coop)
```

## Usage

To write a coroutine, you'll use the `task_t` template type.


```c++
coop::task_t<> simple_coroutine()
{
    co_await coop::suspend();

    // Fake some work with a timer
    std::this_thread::sleep_for(std::chrono::milliseconds{50});
}
```

The first line with the `coop::suspend` function will suspend the execution of `simple_coroutine` and the next line will continue on a different thread.

To use this coroutine from another coroutine, we can do something like the following:

```c++
coop::task_t<> another_coroutine()
{
    // This will cause `simple_coroutine` to be scheduled on a thread different to this one
    auto task = simple_coroutine();

    // Do other useful work

    // Await the task when we need it to finish
    co_await task;
}
```

Tasks can hold values to be awaited on.

```c++
coop::task_t<int> coroutine_with_data()
{
    co_await coop::suspend();

    // Do some work
    int result = some_expensive_simulation();

    co_return result;
}
```

When the task above is awaited via the `co_await` operator, what results is the int returned via `co_return`.
Of course, passing other types is possible by changing the first template parameter of `task_t`.

Tasks let you do multiple async operations simultaneously, for example:

```c++
coop::task_t<> my_task(int ms)
{
    co_await coop::suspend();

    // Fake some work with a timer
    std::this_thread::sleep_for(std::chrono::milliseconds{ms});
}

coop::task_t<> big_coroutine()
{
    auto t1 = my_task(50);
    auto t2 = my_task(40);
    auto t3 = my_task(80);

    // 3 invocations of `my_task` are now potentially running concurrently on different threads

    do_something_useful();

    // Suspend until t2 is done
    co_await t2;

    // Right now, t1 and t3 are *potentially* still running

    do_something_else();

    // When awaiting a task, this coroutine will not suspend if the task
    // is already ready. Otherwise, this coroutine suspends to be continued
    // by the thread that completes the awaited task.
    co_await t1;
    co_await t3;

    // Now, all three tasks are complete
}
```

One thing to keep in mind is that after awaiting a task, the thread you resume on is *not* necessarily the same thread
you were on originally.

What if you want to await a task from `main` or some other execution context that isn't a coroutine? For this, you can
make a joinable task and `join` it.

```c++
coop::task_t<void, true> joinable_coroutine()
{
    co_await coop::suspend();

    // Fake some work with a timer
    std::this_thread::sleep_for(std::chrono::milliseconds{50});
}

int main(int argc, char** argv)
{
    auto task = joinable_coroutine();
    // The timer is now running on a different thread than the main thread

    // Pause execution until joinable_coroutine is finished on whichever thread it was scheduled on
    task.join();

    return 0;
}
```

Note that currently, there is some overhead associated with spawning a joinable task because it creates new event objects instead of reusing event handles from a pool.

The `coop::suspend` function takes additional parameters that can set the CPU affinity mask, priority (only 0 and 1 are supported at the moment,
with 1 being the higher priority), and file/line information for debugging purposes.

The `task_t` template type also takes an additional type that should implement the `TaskControl` concept. This is currently useful
if you intend on overriding the allocation and deallocation behavior of the coroutine frames.

In addition to awaiting tasks, you can also await the `event_t` object. While this currently only supports Windows, this lets a coroutine
suspend execution until an event handle is signaled - a powerful pattern for doing async I/O.

```c++
coop::task_t<> wait_for_event()
{
    // Suppose file_reading_code produces a Win32 HANDLE which will get signaled whenever the file
    // read is ready
    coop::event_t event{file_reading_code()};

    // Do something else while the file is reading

    // Suspend until the event gets signaled
    co_await event;
}
```

In the future, support may be added for epoll and kqueue abstractions.

## Convenience macro `COOP_SUSPEND#`

The full function signature of the `suspend` function is the following:

```c++
template <Scheduler S = scheduler_t>
inline auto suspend(S& scheduler                             = S::instance(),
                    uint64_t cpu_mask                        = 0,
                    uint32_t priority                        = 0,
                    source_location_t const& source_location = {}) noexcept
```

and you must await the returned result. Instead, you can use the family of macros and simply write

```
COOP_SUSPEND();
```

if you are comfortable with the default behavior. This macro will supply `__FILE__` and `__LINE__` information
to the `source_location` paramter to get additional tracking. Other macros with numerical suffixes to `COOP_SUSPEND` are
also provided to allow you to override a subset of parameters as needed.

## (Optional) Override default allocators

By default, coroutine frames are allocated via `operator new` and `operator delete`. Remember that the coroutine frames may not
always allocate if the compiler can prove the allocation isn't necessary. That said, if you'd like to override the allocator
with your own (for tracking purposes, or to use a different more specialized allocator), simply provide a `TaskControl` concept
conforming type as the third template parameter of `task_t`. The full template type signature of a `task_t` is as follows:

```c++
template <typename T = void, bool Joinable = false, TaskControl C = task_control_t>
class task_t;
```

The first template parameter refers to the type that should be `co_return`ed by the coroutine. The `Joinable` parameter indicates
whether this task should create a `binary_semaphore` which is signaled on completion (and provides the `task_t::join` method to
wait on the semaphore). The last parameter is any type that has an `alloc` and `free` function. By default, the `TaskControl` type
is the one below:


```c++
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
```

## (Optional) Use your own scheduler

Coop is designed to be a pretty thin abstraction layer to make writing async code more convenient. If you already have a robust
scheduler and thread pool, you don't have to use the one provided here. The `coop::suspend` function is templated and accepts
an optional first parameter to a class that implements the `Scheduler` concept. To qualify as a `Scheduler`, a class only needs
to implement the following function signature:

```c++
    void schedule(std::coroutine_handle<> coroutine,
                  uint64_t cpu_affinity             = 0,
                  uint32_t priority                 = 0,
                  source_location_t source_location = {});
```

Then, at the opportune time on a thread of your choosing, simply call `coroutine.resume()`. Remember that when implementing your
own scheduler, you are responsible for thread safety and ensuring that the "usual" bugs (like missed notifications) are ironed out.
You can ignore the cpu affinity and priority flags if you don't need this functionality (i.e. if you aren't targeting a NUMA).

## Hack away

The source code of Coop is pretty small all things considered, with the core of its functionality contained in only a few hundred
lines of commented code. Feel free to take it and adapt it for your use case. This was the route taken as opposed to making every
design aspect customizable (which would have made the interface far more complicated).

## Additional Resources

To learn more about coroutines in C++20, please do visit this [awesome compendium](https://gist.github.com/MattPD/9b55db49537a90545a90447392ad3aeb)
of resources compiled by @MattPD.
