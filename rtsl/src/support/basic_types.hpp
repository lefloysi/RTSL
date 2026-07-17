#pragma once

#include "rtsl/sdk/ir.hpp"

#include <cstdint>

namespace rtsl {

using u08 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

using i08 = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;

template <typename T>
using ID = ir::Id;

using ir::raw_id;

} // namespace rtsl
