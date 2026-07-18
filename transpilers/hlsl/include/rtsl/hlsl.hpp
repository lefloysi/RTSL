#pragma once

#include "rtsl/sdk/program.hpp"

#include <cstdint>
#include <expected>
#include <optional>
#include <string>

namespace rtsl::hlsl {

enum class ErrorCode : std::uint8_t {
	stage_not_found,
	invalid_entry,
	unsupported_type,
	unsupported_resource,
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
	std::string source;
};

// Extracts one stage from a linked RTSL program and emits Shader Model 6 HLSL.
// Register numbers and spaces preserve the linked program's descriptor binding.
[[nodiscard]] std::expected<Shader, Error> transpile(const Program& program, Stage stage);

} // namespace rtsl::hlsl
