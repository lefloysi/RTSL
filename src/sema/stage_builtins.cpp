#include "sema/stage_builtins.hpp"

namespace rtsl {

namespace {

constexpr StageBuiltin VertexBuiltins[] = {
	{ .member = "position", .type = "vec4", .slot = BuiltinSlot::position, .is_output = true },
	{ .member = "point_size", .type = "f32", .slot = BuiltinSlot::point_size, .is_output = true },
	{ .member = "vertex_index", .type = "i32", .slot = BuiltinSlot::vertex_index, .is_output = false },
	{ .member = "instance_index", .type = "i32", .slot = BuiltinSlot::instance_index, .is_output = false },
};

constexpr StageBuiltin FragmentBuiltins[] = {
	{ .member = "frag_coord", .type = "vec4", .slot = BuiltinSlot::frag_coord, .is_output = false },
	{ .member = "front_facing", .type = "bool", .slot = BuiltinSlot::front_facing, .is_output = false },
	{ .member = "frag_depth", .type = "f32", .slot = BuiltinSlot::frag_depth, .is_output = true },
};

struct CarrierSpec {
	std::string_view type_name;
	std::span<const StageBuiltin> members;
};

constexpr CarrierSpec Carriers[] = {
	{ "RtVertex", VertexBuiltins },
	{ "RtFragment", FragmentBuiltins },
};

} // namespace

std::span<const StageBuiltin> stage_builtins(std::string_view carrier_type) {
	for (const auto& carrier : Carriers) {
		if (carrier.type_name == carrier_type) {
			return carrier.members;
		}
	}
	return {};
}

bool is_stage_builtin_carrier(std::string_view type) {
	return !stage_builtins(type).empty();
}

} // namespace rtsl
