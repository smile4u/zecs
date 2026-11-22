#include "zecs/guid.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <concepts>
#include <unordered_map>

namespace zecs
{

// The index types that have a registry. Each of them has its own one.
template<typename T>
concept UidIndexType = std::same_as<T, CUidIndex> || std::same_as<T, EUidIndex>;

template<UidIndexType T>
struct UidIndexEntry
{
    T index;
    std::string name;
};

template<UidIndexType T>
std::unordered_map<GUid, UidIndexEntry<T>>& uid_index_map_instance()
{
    static std::unordered_map<GUid, UidIndexEntry<T>> uid_index_map;
    return uid_index_map;
}

template<UidIndexType T>
T uid_index_register(GUid guid, const char* name)
{
    // Index 0 means "no index yet", so the first type gets 1.
    static uint32_t uid_index_counter = 1;

    auto& uid_index_map = uid_index_map_instance<T>();
    auto it = uid_index_map.find(guid);
    if (it != uid_index_map.end())
    {
        // Two types with one GUID would use one pool. It must not happen,
        // so the check works in release builds too. It runs one time
        // for a type, when the program starts.
        if (it->second.name != name)
        {
            std::fprintf(stderr, "zecs: two types have the same GUID 0x%08x:\n  %s\n  %s\nRename one of them.\n",
                static_cast<unsigned>(guid), it->second.name.c_str(), name);
            std::abort();
        }

        return it->second.index;
    }

    T new_index = static_cast<T>(uid_index_counter++);
    uid_index_map[guid] = UidIndexEntry<T>{ new_index, name };
    return new_index;
}

template<UidIndexType T, T invalid>
T uid_index_get(GUid guid)
{
    auto& uid_index_map = uid_index_map_instance<T>();
    auto it = uid_index_map.find(guid);
    if (it != uid_index_map.end())
        return it->second.index;

    return invalid;
}

CUidIndex cuid_index_register(GUid guid, const char* name)
{
    return uid_index_register<CUidIndex>(guid, name);
}

CUidIndex cuid_index_get(GUid guid)
{
    return uid_index_get<CUidIndex, CUidIndex_INVALID>(guid);
}

EUidIndex euid_index_register(GUid guid, const char* name)
{
    return uid_index_register<EUidIndex>(guid, name);
}

EUidIndex euid_index_get(GUid guid)
{
    return uid_index_get<EUidIndex, EUidIndex_INVALID>(guid);
}

} // namespace zecs
