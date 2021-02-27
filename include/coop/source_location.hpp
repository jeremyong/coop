#pragma once

#include <cstddef>

namespace coop
{
// Temporary source location representation until <source_location> is more
// widely available
struct source_location_t
{
    char const* file;
    size_t line;
};
} // namespace coop