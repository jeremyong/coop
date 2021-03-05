#pragma once

// Add defines needed for exporting and importing symbols needed in a shared
// linkage environment

#ifdef COOP_BUILD_SHARED
#    ifdef _MSC_VER
#        ifdef COOP_IMPL
#            define COOP_API __declspec(dllexport)
#        else
#            define COOP_API __declspec(dllimport)
#        endif
#    else
#        define COOP_API __attribute__((visibility(default)))
#    endif
#else
#    define COOP_API
#endif