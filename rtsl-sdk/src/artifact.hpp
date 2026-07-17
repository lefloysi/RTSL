#pragma once

#include "basic_types.hpp"
#include "ir.hpp"
#include "reflection.hpp"

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

[[nodiscard]] constexpr std::string_view artifact_extension(ArtifactKind kind) {
	switch (kind) {
	case ArtifactKind::object: return ".rtslo";
	case ArtifactKind::module: return ".rtslm";
	case ArtifactKind::library: return ".rtsll";
	case ArtifactKind::program: return ".rtslp";
	}
	return ".rtslbin";
}

[[nodiscard]] constexpr std::string_view source_extension() { return ".rtsl"; }

constexpr u32 ArtifactMagic = 0x4c535452u;
constexpr u16 ArtifactVersionMajor = artifact_version_major;
constexpr u16 ArtifactVersionMinor = artifact_version_minor;

// In-memory artifact after parsing a .rtslo/.rtslm/.rtsll/.rtslp byte stream.
struct Artifact {
	ArtifactKind kind = ArtifactKind::object;

	// ID<std::string> values index this table.
	IRModule module;

	std::vector<u08> bytes;
};

} // namespace rtsl
