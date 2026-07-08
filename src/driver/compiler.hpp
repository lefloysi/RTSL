#pragma once

#include "artifact/artifact.hpp"
#include "support/basic_diagnostics.hpp"
#include "support/basic_source_manager.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace rtsl {

struct CompilerInvocation {
	std::string source_name = "<memory>";
	std::vector<std::string> defines;
	std::vector<std::string> import_paths;
};

class CompilerInstance {
  public:
	[[nodiscard]] Artifact compile_source(std::string_view source, CompilerInvocation invocation = {});
	void compile_source_to(Artifact& artifact, std::string_view source, CompilerInvocation invocation = {});

	[[nodiscard]] DiagnosticEngine& diagnostics() { return diagnostics_; }
	[[nodiscard]] SourceManager& sources() { return sources_; }

  private:
	// Locate a .rtslm interface for `name`. If none exists, fall through to a
	// sibling .rtsl source and compile it in-memory, returning the module
	// interface bytes. `active_builds` guards against import cycles. Returns
	// false only for hard failures (parse/sema errors in the imported source
	// or a cycle); a "not found anywhere" result returns true with `bytes`
	// empty so the caller can emit its own diagnostic.
	bool load_or_build_import(const CompilerInvocation& invocation, std::string_view name,
		std::vector<u08>& bytes, std::unordered_set<std::string>& active_builds);
	// The workhorse behind `compile_source_to`; carries the active-build set
	// through recursive autobuilds so an `import` chain that comes back to
	// where it started reports as a cycle instead of stack-overflowing.
	void compile_source_to_impl(Artifact& artifact, std::string_view source,
		CompilerInvocation invocation, std::unordered_set<std::string>& active_builds);

	SourceManager sources_;
	DiagnosticEngine diagnostics_;
};

} // namespace rtsl
