# Architecture

The main ideas behind Coop are actually quite simple, and the scheduler may be too unsophisticated for the most demanding users.
This doc exists to describe in rough terms how the scheduler works so you can decide for yourself if it's worth swapping out.
If you've been working with a mature thread pool and scheduling library for a while, it is almost certainly worth it to stick
with it. That said, for other users, my recommendation is to profile the functionality here to see the builtin scheduler is
sufficient for your needs.

The primary thread pool is defined in `src/scheduler.cpp` and a worker thread is defined in `src/work_queue.cpp`. The thread pool
is initialized with threads equal to the hardware concurrency available. Each thread sets its affinity to a distinct core.

When a coroutine suspends, it enqueues its associated coroutine to an idle thread, if any. If a CPU affinity mask is provided,
only threads pinned to the requested cores are considered. After a thread is selected, the coroutine handle is enqueued on a
lock free queue and a semaphore is released so the worker thread can wake up. When the worker thread wakes up, it always checks
the higher priority queue first to see if work is available, otherwise it will dequeue from the lower priority queue.

When a coroutine completes on a worker thread, the resume point (if any) before the coroutine was scheduled is invoked immediately.
That is, it doesn't get requeued on the thread pool for later execution.

The concurrent queue used to push work to worker threads is provided by [`moodycamel::ConcurrentQueue`](https://github.com/cameron314/concurrentqueue).
Under the hood, the queue provides multiple-consumer multiple-producer usage, although in this case, only a single producer per queue
exists. The thread pool worker threads currently do *not* support work stealing, which is a slightly more complicated endeavor
for job schedulers that support task affinity.

The granularity of your jobs shouldn't be too fine - maybe having jobs that are at least 100 us or more is a good idea, or you'll
end up paying disproportionately for scheduling costs.

The Win32 event awaiter works by having a single IO thread which blocks in a single `WaitForMultipleObjects` call. One of the
events it waits on is used to signal the available of more events to wait on. All the other events waited on are user awaited.
If a user-awaited event is signaled, the coroutine associated with that event is then queued to a worker thread, passing along
the requested CPU affinity and priority.