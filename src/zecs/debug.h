#pragma once

#define ZECS_DEBUG
#ifdef ZECS_DEBUG

#include "zecs/guid.h"
#include <unordered_map>

//#include "zecs/exports.h"
//#include "zecs/types.h"

namespace zecs
{
struct IDbgPtr
{
    virtual ~IDbgPtr() = default;
};

template<typename T>
struct DbgPtr : IDbgPtr
{
    DbgPtr(const T* _ptr) : ptr(_ptr) { }
    const T* ptr;
};

struct DbgInfo
{
    std::unordered_map<GUid, IDbgPtr> comps;
};

} // namespace zecs
#endif
