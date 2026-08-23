#include "zecs/view_comps_group.h"

namespace zecs
{

void ViewCompGroupBase::init(CompPool2Base** pools_ptr_, size_t pools_count_)
{
    pools_ptr = pools_ptr_;
    pools_count = pools_count_;

    size_t lead = 0;
    for (size_t i = 0; i < pools_count_; ++i)
    {
        if (pools_ptr[i] == nullptr)
        {
            lead = i;
            break;
        }

        if (pools_ptr[i]->size() < pools_ptr[lead]->size())
            lead = i;
    }

    std::swap(pools_ptr[0], pools_ptr[lead]);

    if (pools_ptr[0])
    {
        CompPool2Base::EntityIt internal_end = pools_ptr[0]->entities_end();
        it_begin = It(pools_ptr[0]->entities_begin(), internal_end, this);
        it_end = It(internal_end);
    }
}

}
