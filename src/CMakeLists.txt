add_library(
    coop_scheduler
    event.cpp
    scheduler.cpp
    work_queue.cpp
)

target_link_libraries(
    coop_scheduler
    PUBLIC
    coop
)