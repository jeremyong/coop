#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include <chrono>
#include <coop/task.hpp>
#include <thread>

coop::task_t<void, true> suspend_time()
{
    // std::printf("%zu start thread\n", coop::detail::thread_id());
    auto t1 = std::chrono::system_clock::now();
    COOP_SUSPEND();
    auto t2 = std::chrono::system_clock::now();
    // std::printf("%zu end thread\n", coop::detail::thread_id());
    size_t us
        = std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1).count();
    std::printf("Duration for suspend test: %zu us\n", us);
}

TEST_CASE("suspend overhead")
{
    std::printf("Calling suspend_test2 coroutine\n");
    // auto task = suspend_time();
    // task.join();
    suspend_time().join();
    std::printf("suspend_test2 joined\n");
}

coop::task_t<void, true> test_suspend(std::thread::id& id)
{
    COOP_SUSPEND();
    id = std::this_thread::get_id();
    co_return;
}

TEST_CASE("test suspend")
{
    std::thread::id id = std::this_thread::get_id();
    std::thread::id next;
    auto task = test_suspend(next);
    std::printf("Joining task\n");
    task.join();
    std::printf("Task joined\n");

    CHECK(id != next);
}

coop::task_t<int> chain1(int core)
{
    std::printf("chain1 suspending\n");
    COOP_SUSPEND4(1 << core);
    std::printf("chain1 resumed\n");
    co_return 1;
}

coop::task_t<int> chain2()
{
    std::printf("chain2\n");
    COOP_SUSPEND4(1 << 3);
    auto t1 = chain1(5);
    auto t2 = chain1(6);
    co_return co_await t1 + co_await t2;
}

coop::task_t<void, true> chain3(int& result)
{
    std::printf("chain3 suspending\n");
    COOP_SUSPEND4(1 << 4);
    std::printf("chain3 resumed\n");
    result = co_await chain2();
}

TEST_CASE("chained continuation")
{
    int x     = 0;
    auto task = chain3(x);
    std::printf("Joining chained continuation task\n");
    task.join();
    std::printf("Task chained continuation joined\n");
    CHECK(x == 2);
}

coop::task_t<> in_flight1()
{
    COOP_SUSPEND();
    std::this_thread::sleep_for(std::chrono::milliseconds{50});
}

coop::task_t<void, true> in_flight2(size_t& ms_elapsed)
{
    // The timing of this test will be off if you don't have at least 8
    // concurrent threads that can run on your machine
    constexpr size_t count = 8;
    coop::task_t<> tasks[count];

    for (size_t i = 0; i != count; ++i)
    {
        tasks[i] = in_flight1();
    }

    auto t1 = std::chrono::system_clock::now();
    for (size_t i = 0; i != count; ++i)
    {
        co_await tasks[i];
    }
    auto t2 = std::chrono::system_clock::now();

    ms_elapsed
        = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count();
}

TEST_CASE("multiple in flight")
{
    size_t ms;
    auto task = in_flight2(ms);
    task.join();
    std::printf("Duration for in flight test: %zu ms\n", ms);
    CHECK(ms < 150);
}

#ifdef _WIN32
coop::task_t<void, true> wait_for_event(coop::event_t& event)
{
    co_await event;
}

coop::task_t<void, true> signal_event(coop::event_t& event)
{
    COOP_SUSPEND();
    std::this_thread::sleep_for(std::chrono::milliseconds{50});
    event.signal();
}

TEST_CASE("event completion")
{
    coop::event_t event;
    event.init();
    auto t1   = std::chrono::system_clock::now();
    auto task = wait_for_event(event);

    // Fire and forget coroutine
    // signal_event(event);
    event.signal();
    task.join();
    auto t2 = std::chrono::system_clock::now();
    size_t ms
        = std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1).count();
    std::printf("Duration for event_completion test: %zu ms\n", ms);
}
#endif

int main(int argc, char* argv[])
{
    // Spawn thread pool
    coop::scheduler_t::instance();
    return doctest::Context{argc, argv}.run();
}
