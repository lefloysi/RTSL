#pragma once

#include "frontend/ast.hpp"

#include <span>
#include <string_view>

namespace rtsl {

// One member of a builtin carrier struct (RtVertex/RtFragment). The carrier
// is passed by reference into a stage entry; the generated runtime copies
// inputs in before the call and outputs out after it. Members map to abstract
// BuiltinSlot values — no target-specific (GLSL/SPIR-V) names appear at this
// level; backends translate the slot to their own ABI.
struct StageBuiltin {
	std::string_view member;              // member name as written in RTSL source
	std::string_view type;                // member type
	BuiltinSlot slot = BuiltinSlot::none; // pipeline slot the member carries
	bool is_output = false;               // true: written by the shader; false: read-only input
};

// Members of a builtin carrier type, or empty if `carrier_type` is not a carrier.
[[nodiscard]] std::span<const StageBuiltin> stage_builtins(std::string_view carrier_type);

// Whether a parameter type names a builtin carrier struct.
[[nodiscard]] bool is_stage_builtin_carrier(std::string_view type);

} // namespace rtsl
