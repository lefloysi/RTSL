#include <rtsl/Basic/Diagnostic.hpp>

#include <algorithm>

namespace rtsl {

void DiagnosticsEngine::report(DiagnosticLevel Level, SourceRange Range, std::string_view Message) {
	Diagnostics.push_back({Level, Range, 1, std::string(Message)});
}

bool DiagnosticsEngine::hasErrorOccurred() const {
	return std::ranges::any_of(Diagnostics, [](const Diagnostic& Value) {
		return Value.Level == DiagnosticLevel::diagnostic_error;
	});
}

}
