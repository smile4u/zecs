#pragma once

#include "zecs/exports.h"

#include <cstddef>

namespace zecs
{

ZECS_EXPORT void* mem_pool_data_alloc(size_t size, size_t align);
ZECS_EXPORT void mem_pool_data_free(void* data, size_t align);

}
