#pragma once

#include "detail/api.hpp"
#include <cstddef>

namespace coop
{
// Temporary source location representation until <source_location> is more
// widely available
struct COOP_API source_location_t
{
    char const* file;
    size_t line;
};
} // namespace coop