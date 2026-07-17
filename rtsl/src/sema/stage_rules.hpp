#pragma once

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

} // namespace rtsl
