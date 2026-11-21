#include "zecs/query_scope.h"

#include <utility>

namespace zecs
{

void move_leading_pool_front(CompPool2Base** pools, size_t count) noexcept
{
    if (count < 2 || pools[0] == nullptr)
        return;

    size_t lead = 0;
    size_t lead_size = pools[0]->dense_size();
    for (size_t i = 1; i < count; ++i)
    {
        if (pools[i] == nullptr)
        {
            lead = i;
            break;
        }

        const size_t size = pools[i]->dense_size();
        if (size < lead_size)
        {
            lead = i;
            lead_size = size;
        }
    }

    std::swap(pools[0], pools[lead]);
}

} // namespace zecs
