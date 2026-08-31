#ifndef RTSL_BASIC_DIAGNOSTIC_HPP
#define RTSL_BASIC_DIAGNOSTIC_HPP

#include <rtsl/Basic/SourceLocation.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace rtsl {

enum class DiagnosticLevel : std::uint8_t { diagnostic_warning, diagnostic_error };

struct Diagnostic {
	DiagnosticLevel Level{DiagnosticLevel::diagnostic_error};
	SourceRange Range;
	std::uint32_t Code{1};
	std::string Message;
};

class DiagnosticsEngine {
public:
	void report(DiagnosticLevel Level, SourceRange Range, std::string_view Message);
	[[nodiscard]] bool hasErrorOccurred() const;
	[[nodiscard]] const std::vector<Diagnostic>& diagnostics() const { return Diagnostics; }

private:
	std::vector<Diagnostic> Diagnostics;
};

}

#endif
