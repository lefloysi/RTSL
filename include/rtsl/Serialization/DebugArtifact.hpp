#ifndef RTSL_SERIALIZATION_DEBUG_ARTIFACT_HPP
#define RTSL_SERIALIZATION_DEBUG_ARTIFACT_HPP

// The RTSL debug artifact (.rtsld) is deliberately independent from RTIR
// artifacts.  Its records use stable numeric ids and owned strings only; it
// never serializes compiler addresses, AST nodes, or IR object pointers.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace rtsl::debug {

inline constexpr std::uint16_t artifact_version_major = 1;
inline constexpr std::uint16_t artifact_version_minor = 0;

enum class SourceEntityKind : std::uint32_t {
	declaration,
	expression,
	statement,
	lowered_instruction,
};

struct SourceRange {
	std::uint32_t file{};
	std::uint32_t begin{}; // UTF-8 byte offset, inclusive.
	std::uint32_t end{};   // UTF-8 byte offset, exclusive.
};

struct SourceFile {
	std::string logical_name;
	// SHA-256 source digest. All-zero means the producing compiler did not
	// record a digest; the source text remains authoritative in that case.
	std::array<std::byte, 32> content_hash{};
	std::string text;
	std::vector<std::uint32_t> line_starts;
};

struct SourceEntity {
	SourceEntityKind kind{SourceEntityKind::declaration};
	std::uint32_t id{};
	SourceRange range;
};

struct Symbol {
	std::uint32_t id{};
	std::string name;
	std::uint32_t type{};
	SourceRange declaration;
};

struct Type {
	std::uint32_t id{};
	std::string name;
	std::string spelling;
	std::uint32_t kind{};
	SourceRange declaration;
};

struct Function {
	std::uint32_t id{};
	std::uint32_t symbol{};
	std::uint32_t return_type{};
	std::vector<std::uint32_t> parameter_symbols;
	SourceRange declaration;
};

struct Template {
	std::uint32_t id{};
	std::uint32_t origin_symbol{};
	std::uint32_t specialized_symbol{};
	std::vector<std::string> arguments;
	std::vector<std::uint32_t> instantiation_chain;
	SourceRange origin;
	SourceRange instantiation;
};

struct IRInstructionMapping {
	std::uint32_t function{};
	std::uint32_t block{};
	std::uint32_t instruction{};
	std::uint32_t opcode{};
	std::uint32_t result_symbol{};
	SourceRange source;
};

struct InterfaceVariable {
	std::uint32_t symbol{};
	std::uint32_t type{};
	std::uint32_t location{};
	std::uint32_t direction{};
	SourceRange declaration;
};

struct ResourceBinding {
	std::uint32_t symbol{};
	std::uint32_t type{};
	std::uint32_t descriptor_set{};
	std::uint32_t binding{};
	std::uint32_t kind{};
	SourceRange declaration;
};

struct EntryPoint {
	std::uint32_t function{};
	std::string name;
	std::uint32_t stage{};
	std::vector<InterfaceVariable> interfaces;
	std::vector<ResourceBinding> resources;
	SourceRange declaration;
};

struct Artifact {
	std::string compilation_identity;
	std::string compiler_identity;
	std::string target_identity;
	std::vector<SourceFile> source_files;
	std::vector<SourceEntity> source_entities;
	std::vector<Symbol> symbols;
	std::vector<Type> types;
	std::vector<Function> functions;
	std::vector<Template> templates;
	std::vector<IRInstructionMapping> ir_instructions;
	std::vector<EntryPoint> entry_points;
};

enum class ErrorCode : std::uint32_t {
	invalid_argument,
	invalid_magic,
	unsupported_version,
	truncated,
	invalid_count,
	invalid_enum,
	invalid_range,
	invalid_trailing_data,
	allocation_failure,
};

struct Error { ErrorCode code{}; std::size_t offset{}; std::string message; };
struct WriteResult { std::vector<std::byte> bytes; std::optional<Error> error; [[nodiscard]] explicit operator bool() const noexcept { return !error; } };
struct ReadResult { std::optional<Artifact> artifact; std::optional<Error> error; [[nodiscard]] explicit operator bool() const noexcept { return artifact.has_value(); } };

class ArtifactWriter { public: [[nodiscard]] WriteResult write(const Artifact& artifact) const; };
class ArtifactReader { public: [[nodiscard]] ReadResult read(std::span<const std::byte> bytes) const; };

} // namespace rtsl::debug

#endif
