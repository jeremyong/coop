# 🐔 Coop

Coop is a C++20 coroutines-based library to support [*cooperative multitasking*](https://en.wikipedia.org/wiki/Cooperative_multitasking)
in the context of a multithreaded application. The syntax will be familiar to users of `async` and `await` functionality in other
programming languages. Users *do not* need to understand the C++20 coroutines API to use this library.

## Features

- Ships with a default affinity-aware two-priority threadsafe task scheduler.
- The task scheduler is swappable with your own
- Supports scheduling of user-defined code and OS completion events (e.g. events that signal after I/O completes)
- Easy to use, efficient API

## Limitations

- Currently only tested on Windows against MSVC (more platforms coming)
- Requires a recent C++20 compiler and code that uses Coop headers must also use C++20

## Integration Guide

If you don't intend on using the built in scheduler, simply copy the contents of the `include` folder somewhere in your include path.

Otherwise, the recommended integration is done via cmake. For the header only portion, link against the `coop` target.

If you'd like both headers and the scheduler implementation, link against both `coop` and `coop_scheduler`.

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

## Notice

This software is alpha quality, but testers and contributors are appreciated.
