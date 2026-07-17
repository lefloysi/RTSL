#pragma once

#include "artifact.hpp"

#include <expected>
#include <span>
#include <vector>

namespace rtsl::codec {

[[nodiscard]] std::expected<std::vector<u08>, LoadError>
encode_artifact(ArtifactKind kind, const IRModule& module);

[[nodiscard]] std::expected<Artifact, LoadError>
decode_artifact(std::span<const u08> bytes);

} // namespace rtsl::codec
