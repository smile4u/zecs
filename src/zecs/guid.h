#pragma once

#include "zecs/guid_crc.h"
#include "zecs/exports.h"

#include <array>
#include <algorithm>

namespace zecs
{
enum class GUid : uint32_t; // unique component type identifier

template<typename T>
constexpr GUid globalUid = static_cast<GUid>(func_CRC32<T>());
constexpr GUid GUid_INVALID = static_cast<GUid>(0);

template<typename... T>
using GUidGroup = std::array<zecs::GUid, sizeof...(T)>;

using CUidIndex = uint32_t;
using EUidIndex = uint32_t;

constexpr CUidIndex CUidIndex_INVALID = 0xFFFFFFFF;
constexpr EUidIndex EUidIndex_INVALID = 0xFFFFFFFF;

template<typename T>
inline CUidIndex cuidIndex = cuid_index_register(globalUid<T>);

template<typename T>
inline EUidIndex euidIndex = euid_index_register(globalUid<T>);

ZECS_EXPORT CUidIndex cuid_index_register(GUid guid);
ZECS_EXPORT CUidIndex cuid_index_get(GUid guid);

ZECS_EXPORT EUidIndex euid_index_register(GUid guid);
ZECS_EXPORT EUidIndex euid_index_get(GUid guid);

} // namespace zecs
