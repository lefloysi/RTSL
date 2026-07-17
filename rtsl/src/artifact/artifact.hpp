#pragma once

#include "support/basic_diagnostics.hpp"
#include <artifact.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rtsl {

[[nodiscard]] std::vector<u08> write_artifact(ArtifactKind kind, const IRModule& module);
[[nodiscard]] bool read_artifact(std::span<const u08> data, Artifact& artifact, DiagnosticEngine* diagnostics = nullptr);
} // namespace rtsl
