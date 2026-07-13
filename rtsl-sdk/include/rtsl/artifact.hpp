#pragma once

#include <cstddef>
#include <cstdint>

namespace rtsl {

enum class ArtifactKind : std::uint16_t {
	object = 1,
	module = 2,
	library = 3,
	program = 4,
};

inline constexpr std::uint32_t ArtifactMagic = 0x4c535452u;
inline constexpr std::uint16_t ArtifactVersionMajor = 0;
inline constexpr std::uint16_t ArtifactVersionMinor = 1;
inline constexpr std::size_t ArtifactHeaderSize = 64;
inline constexpr std::size_t PayloadRecordSize = 32;

} // namespace rtsl
