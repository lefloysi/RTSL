#pragma once

#include "frontend/ast.hpp"

#include <span>
#include <string>
#include <string_view>

namespace rtsl {

[[nodiscard]] std::string mangle_rtsl(std::string_view name, StageKind stage, std::span<const std::string_view> parameter_types);

} // namespace rtsl
