set(COOP_SOURCES
    ../include/coop/event.hpp
    ../include/coop/scheduler.hpp
    ../include/coop/source_location.hpp
    ../include/coop/task.hpp
    ../include/coop/detail/api.hpp
    ../include/coop/detail/blockingconcurrentqueue.h
    ../include/coop/detail/concurrentqueue.h
    ../include/coop/detail/lightweightsemaphore.h
    ../include/coop/detail/promise.hpp
    ../include/coop/detail/tracer.hpp
    ../include/coop/detail/work_queue.hpp
    event.cpp
    scheduler.cpp
    work_queue.cpp
)
source_group(
    TREE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    FILES
    ${COOP_SOURCES}
)

add_library(
    coop
    ${COOP_SOURCES}
)
add_library(coop::coop ALIAS coop)

target_link_libraries(
    coop
    PUBLIC
    coop_core
)

if(BUILD_SHARED_LIBS)
    target_compile_definitions(coop PRIVATE COOP_IMPL)
endif()