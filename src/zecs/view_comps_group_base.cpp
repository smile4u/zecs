#include "zecs/view_comps_group_base.h"
#include "zecs/comp_pool_base.h"

#include <utility>

namespace zecs
{

void ViewCompGroupBase::select_lead(CompPool2Base** pools, size_t count) noexcept
{
    if (pools[0] == nullptr)
        return;

    size_t lead = 0;
    size_t lead_size = pools[0]->size();
    for (size_t i = 1; i < count; ++i)
    {
        if (pools[i] == nullptr)
        {
            lead = i;
            break;
        }

        const size_t size = pools[i]->size();
        if (size < lead_size)
        {
            lead = i;
            lead_size = size;
        }
    }

    std::swap(pools[0], pools[lead]);
}

} // namespace zecs
