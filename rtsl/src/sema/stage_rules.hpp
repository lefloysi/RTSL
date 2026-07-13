#pragma once

#include <string>
#include <string_view>

namespace rtsl {

[[nodiscard]] inline bool is_vertex_stage(std::string_view stage) {
	return stage == "vertex";
}

[[nodiscard]] inline bool is_fragment_stage(std::string_view stage) {
	return stage == "fragment";
}

[[nodiscard]] inline bool is_graphics_stage(std::string_view stage) {
	return is_vertex_stage(stage) || is_fragment_stage(stage);
}

[[nodiscard]] inline std::string backend_entry_name(std::string_view stage) {
	if (is_vertex_stage(stage)) {
		return "vert";
	}
	if (is_fragment_stage(stage)) {
		return "frag";
	}
	return std::string(stage);
}

} // namespace rtsl
