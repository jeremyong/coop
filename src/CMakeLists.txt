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

if(BUILD_SHARED_LIBS)
    target_compile_definitions(coop_scheduler PRIVATE COOP_IMPL)
endif()