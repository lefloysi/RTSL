#pragma once

#include "rtsl/sdk/program.hpp"

#include <cstdint>
#include <expected>
#include <optional>
#include <string>

namespace rtsl::glsl {

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

enum class StorageBufferLowering : std::uint8_t {
	auto_,
	native_ssbo,
	texture_buffer_readonly_vec4,
	unsupported,
};

struct Options {
	std::uint32_t version = 460;
	bool separate_shader_objects = true;
	bool shader_storage_buffer = true;
	bool texture_buffer = true;
	StorageBufferLowering storage_buffer_lowering = StorageBufferLowering::auto_;
};

// Extracts one graphics stage from a linked RTSL program and emits GLSL.
// The caller supplies the available GLSL version/features. Storage buffers are
// emitted as native SSBOs when available, or as readonly texture-buffer fetches
// for vec4/uvec4/ivec4 runtime arrays when SSBOs are not available.
[[nodiscard]] std::expected<Shader, Error> transpile(const Program& program, Stage stage);
[[nodiscard]] std::expected<Shader, Error> transpile(const Program& program, Stage stage, const Options& options);

} // namespace rtsl::glsl
