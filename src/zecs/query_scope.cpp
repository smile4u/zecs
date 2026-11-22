#include "zecs/query_scope.h"

#include <utility>

namespace zecs
{

void move_leading_pool_front(CompPoolBase** pools, size_t count) noexcept
{
    if (count < 2 || pools[0] == nullptr)
        return;

    size_t lead = 0;
    DIndex lead_last = pools[0]->dense_last();
    for (size_t i = 1; i < count; ++i)
    {
        if (pools[i] == nullptr)
        {
            lead = i;
            break;
        }

        const DIndex last = pools[i]->dense_last();
        if (last < lead_last)
        {
            lead = i;
            lead_last = last;
        }
    }

    std::swap(pools[0], pools[lead]);
}

} // namespace zecs
