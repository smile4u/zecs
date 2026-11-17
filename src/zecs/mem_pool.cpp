#include "zecs/mem_pool.h"

#include <new>

namespace zecs
{
void* mem_pool_data_alloc(size_t size, size_t align)
{
    return ::operator new(size, std::align_val_t(align));
}

void mem_pool_data_free(void* data, size_t align)
{
    ::operator delete(data, std::align_val_t(align));
}
} // namespace zecs
