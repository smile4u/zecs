#pragma once

#include "zecs/guid_crc.h"
#include "zecs/exports.h"

#include <array>
#include <algorithm>
#include <concepts>
#include <type_traits>

namespace zecs
{
enum class GUid : uint32_t; // unique component type identifier

// A type that can have a GUID: an object type without const and volatile.
// The GUID comes from the name of the type, and `const Position` has
// another name than `Position`. Without this check it would get its own
// GUID and its own pool, and its components would not be found.
template<typename T>
concept GUidT = std::is_object_v<T> && std::same_as<T, std::remove_cv_t<T>>;

// The value comes from the type name as the compiler prints it. It can differ
// between compilers and their versions: don't save it to files or send it.
template<GUidT T>
constexpr GUid globalUid = static_cast<GUid>(guid_crc32(guid_name<T>()));
constexpr GUid GUid_INVALID = static_cast<GUid>(0);

template<typename... T>
using GUidGroup = std::array<zecs::GUid, sizeof...(T)>;

// Enum, not uint32_t: a write to an int field can't change an enum value,
// so the compiler can find the pool once before a loop.
enum class CUidIndex : uint32_t;
enum class EUidIndex : uint32_t;

constexpr CUidIndex CUidIndex_INVALID = static_cast<CUidIndex>(0xFFFFFFFF);
constexpr EUidIndex EUidIndex_INVALID = static_cast<EUidIndex>(0xFFFFFFFF);

// No type has index 0. A variable that the program didn't initialize yet
// is 0, so 0 means "this type has no index yet" (see cuidIndex).
constexpr CUidIndex CUidIndex_NONE = static_cast<CUidIndex>(0);
constexpr EUidIndex EUidIndex_NONE = static_cast<EUidIndex>(0);

// Gives an index to the GUID, or returns the index that it has already.
// `name` is the text of guid_name<T>(). The GUID is a CRC of a type name,
// so two types can have the same GUID. Then the names are different, and
// the function prints both names and stops the program.
ZECS_EXPORT CUidIndex cuid_index_register(GUid guid, const char* name);
ZECS_EXPORT CUidIndex cuid_index_get(GUid guid);

ZECS_EXPORT EUidIndex euid_index_register(GUid guid, const char* name);
ZECS_EXPORT EUidIndex euid_index_get(GUid guid);

// The index of the pool of a component type.
// The program sets it before main(). The constructor of a global object
// can run before that and read 0 here (CUidIndex_NONE). No pool has
// index 0, so a search finds nothing. When World makes a pool for such
// a type, it sets the index. So components can be used in such
// a constructor too.
template<GUidT T>
inline CUidIndex cuidIndex = cuid_index_register(globalUid<T>, guid_name<T>());

template<GUidT T>
inline EUidIndex euidIndex = euid_index_register(globalUid<T>, guid_name<T>());

} // namespace zecs
