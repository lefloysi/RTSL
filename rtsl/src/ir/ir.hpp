#pragma once

#include <ir.hpp>
#include "sema/sema.hpp"

namespace rtsl {

[[nodiscard]] IRModule lower_to_ir(const SemanticModule& module, DiagnosticEngine* diagnostics = nullptr);
[[nodiscard]] bool verify_ir(const IRModule& module, DiagnosticEngine* diagnostics = nullptr);

} // namespace rtsl
