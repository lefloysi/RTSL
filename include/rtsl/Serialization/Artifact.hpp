#ifndef RTSL_SERIALIZATION_ARTIFACT_HPP
#define RTSL_SERIALIZATION_ARTIFACT_HPP

#include <rtsl/IR/IR.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace rtsl {

inline constexpr std::uint16_t artifact_version_major = 1;
inline constexpr std::uint16_t artifact_version_minor = 5;

enum class ArtifactKind : std::uint8_t {
	artifact_object,
	artifact_module,
	artifact_program,
};

enum class ErrorCode : std::uint8_t {
	error_invalid_argument,
	error_invalid_magic,
	error_unsupported_version,
	error_wrong_endian_marker,
	error_invalid_directory,
	error_missing_section,
	error_duplicate_section,
	error_truncated,
	error_invalid_count,
	error_invalid_enum,
	error_invalid_reference,
	error_invalid_ir,
	error_allocation_failure,
};

struct Error {
	ErrorCode code{ErrorCode::error_invalid_argument};
	std::size_t offset{};
	std::string context;
	std::string message;
};

struct Artifact {
	ArtifactKind kind{ArtifactKind::artifact_object};
	ir::Module module;
};

struct WriteResult {
	std::vector<std::byte> bytes;
	std::optional<Error> error;

	[[nodiscard]] explicit operator bool() const noexcept { return !error.has_value(); }
};

struct ReadResult {
	std::optional<Artifact> artifact;
	std::optional<Error> error;

	[[nodiscard]] explicit operator bool() const noexcept { return artifact.has_value(); }
};

class ArtifactWriter {
public:
	[[nodiscard]] WriteResult write(const Artifact& artifact) const;
};

class ArtifactReader {
public:
	[[nodiscard]] ReadResult read(std::span<const std::byte> bytes) const;
};

} // namespace rtsl

#endif
