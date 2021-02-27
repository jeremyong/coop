#pragma once

#include <coroutine>

namespace coop
{
namespace detail
{
    template <typename UserData>
    class task_base_t
    {
    public:
        UserData& user_data() const noexcept = 0;

    protected:
        template <typename P>
        struct promise_type_base_t
        {
        };
    };
} // namespace detail
} // namespace coop