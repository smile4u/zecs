#include "zecs/guid.h"
#include <cstdint>
#include <unordered_map>

namespace zecs
{

template<typename T>
std::unordered_map<GUid, T>& uid_index_map_instance()
{
    static std::unordered_map<GUid, T> uid_index_map;
    return uid_index_map;
}

template<typename T>
T uid_index_register(GUid guid)
{
    static T uid_index_counter = 0;

    auto& uid_index_map = uid_index_map_instance<T>();
    auto it = uid_index_map.find(guid);
    if (it != uid_index_map.end())
        return it->second;

    T new_index = uid_index_counter++;
    uid_index_map[guid] = new_index;
    return new_index;
}

template<typename T, T invalid>
T uid_index_get(GUid guid)
{
    auto& uid_index_map = uid_index_map_instance<T>();
    auto it = uid_index_map.find(guid);
    if (it != uid_index_map.end())
        return it->second;

    return invalid;
}

CUidIndex cuid_index_register(GUid guid)
{
    return uid_index_register<CUidIndex>(guid);
}

CUidIndex cuid_index_get(GUid guid)
{
    return uid_index_get<CUidIndex, CUidIndex_INVALID>(guid);
}

EUidIndex euid_index_register(GUid guid)
{
    return uid_index_register<EUidIndex>(guid);
}

EUidIndex euid_index_get(GUid guid)
{
    return uid_index_get<EUidIndex, EUidIndex_INVALID>(guid);
}


} // namespace zecs
