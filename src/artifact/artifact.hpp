#pragma once

#include "support/basic_diagnostics.hpp"
#include "ir/ir.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rtsl {

enum class ArtifactKind : u16 {
	object = 1,
	module = 2,
	library = 3,
	program = 4,
};

inline constexpr u16 ArtifactVersionMajor = 0;
inline constexpr u16 ArtifactVersionMinor = 6;

struct Artifact {
	ArtifactKind kind = ArtifactKind::object;
	std::vector<std::string> strings;
	IRModule module;
	std::vector<std::string> imports;
	std::vector<ExportSymbol> imported_exports;
	std::vector<ExportSymbol> exports;
	std::vector<StructDecl> structs;
	std::vector<UniformBinding> uniforms;
	std::vector<StageInterface> stage_interfaces;
	struct EntryPoint {
		std::string name;
		std::string mangled_name;
		StageKind stage = StageKind::none;
		IRId function_id = IRId_None;
	};
	std::vector<EntryPoint> entries;
	std::vector<u08> bytes;
	std::vector<u08> debug_bytes;
};

[[nodiscard]] std::vector<u08> write_artifact(ArtifactKind kind, const IRModule& module);
[[nodiscard]] std::vector<u08> write_debug_artifact(const IRModule& module);
[[nodiscard]] std::vector<u08> write_linked_program(std::span<const Artifact> inputs);
[[nodiscard]] bool read_artifact(std::span<const u08> data, Artifact& artifact, DiagnosticEngine* diagnostics = nullptr);
// File-extension registry. Every RTSL file suffix in the toolchain comes from
// here — nothing else spells them out.
[[nodiscard]] std::string_view artifact_extension(ArtifactKind kind);
[[nodiscard]] constexpr std::string_view debug_artifact_extension() { return ".rtsld"; }
[[nodiscard]] constexpr std::string_view source_extension() { return ".rtsl"; }

} // namespace rtsl
