#pragma once

#include "artifact/artifact.hpp"
#include "support/basic_diagnostics.hpp"

#include <string>
#include <string_view>

namespace rtsl {

[[nodiscard]] std::string disassemble_artifact(const Artifact& artifact);

} // namespace rtsl
