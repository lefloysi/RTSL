#pragma once

// Line-oriented conditional-compilation preprocessor.
//
// Supported directives (see frontend/directives.def):
//   #define NAME          add NAME to the defined set (no macro expansion)
//   #ifdef NAME           emit following lines only if NAME is defined
//   #ifndef NAME          emit following lines only if NAME is not defined
//   #else                 flip the innermost condition
//   #endif                close the innermost condition
//
// Directive lines never reach the output. Unknown directives are ignored.
// Conditions nest; a region is emitted only when every enclosing condition
// holds.

#include <span>
#include <string>
#include <string_view>

namespace rtsl {

[[nodiscard]] std::string preprocess_source(std::string_view source, std::span<const std::string> defines);

} // namespace rtsl
