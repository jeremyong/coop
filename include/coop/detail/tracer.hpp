#pragma once

#if defined(COOP_TRACE) && !defined(NDEBUG)
#    include <cstdio>

#    define COOP_LOG(...) std::printf(__VA_ARGS__)

#else
#    define COOP_LOG(...)
#endif