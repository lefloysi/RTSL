#pragma once

#include "rtsl/sdk/program.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <vector>

namespace rtsl::spirv {

enum class ErrorCode : std::uint8_t {
	stage_not_found,
	invalid_entry,
	unsupported_type,
	unsupported_instruction,
	allocation_failure,
};

struct Error {
	ErrorCode code = ErrorCode::unsupported_instruction;
	Stage stage = Stage::vertex;
	std::optional<ir::Id> id;
	std::optional<ir::Op> op;
	std::string context;
	std::string message;
};

struct Shader {
	Stage stage = Stage::vertex;
	std::string entry_point = "main";
	std::vector<std::uint32_t> words;

	[[nodiscard]] std::size_t byte_size() const noexcept {
		return words.size() * sizeof(std::uint32_t);
	}
};

// Extracts one stage from a linked RTSL program and emits a Vulkan-compatible
// SPIR-V module with a single `main` entry point, using the version provided
// by the configured Khronos SPIRV-Headers package.
[[nodiscard]] std::expected<Shader, Error> transpile(const Program& program, Stage stage);

} // namespace rtsl::spirv
