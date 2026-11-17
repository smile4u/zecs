#pragma once

#include "zecs/guid_crc.h"
#include "zecs/exports.h"

#include <array>
#include <algorithm>

namespace zecs
{
enum class GUid : uint32_t; // unique component type identifier

// The value comes from the type name as the compiler prints it. It can differ
// between compilers and their versions: do not save it to files or send it.
template<typename T>
constexpr GUid globalUid = static_cast<GUid>(func_CRC32<T>());
constexpr GUid GUid_INVALID = static_cast<GUid>(0);

template<typename... T>
using GUidGroup = std::array<zecs::GUid, sizeof...(T)>;

// Enum, not uint32_t: a write to an int field cannot change an enum value,
// so the compiler can find the pool once before a loop.
enum class CUidIndex : uint32_t;
enum class EUidIndex : uint32_t;

constexpr CUidIndex CUidIndex_INVALID = static_cast<CUidIndex>(0xFFFFFFFF);
constexpr EUidIndex EUidIndex_INVALID = static_cast<EUidIndex>(0xFFFFFFFF);

template<typename T>
inline CUidIndex cuidIndex = cuid_index_register(globalUid<T>);

template<typename T>
inline EUidIndex euidIndex = euid_index_register(globalUid<T>);

ZECS_EXPORT CUidIndex cuid_index_register(GUid guid);
ZECS_EXPORT CUidIndex cuid_index_get(GUid guid);

ZECS_EXPORT EUidIndex euid_index_register(GUid guid);
ZECS_EXPORT EUidIndex euid_index_get(GUid guid);

} // namespace zecs
