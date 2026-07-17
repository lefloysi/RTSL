#pragma once

#include "artifact/artifact.hpp"
#include "support/basic_diagnostics.hpp"

#include <vector>

namespace rtsl {

class Linker {
  public:
	explicit Linker(DiagnosticEngine& diagnostics);

	bool add_artifact_bytes(std::span<const u08> bytes);
	bool add_artifact(Artifact artifact);
	// Produce a runnable program. Requires at least one stage entry point;
	// the program is what Rutile backends actually load.
	[[nodiscard]] Artifact link_program();
	// Produce a linked library. No entry points required; can be fed into
	// subsequent link-program or link-library invocations.
	[[nodiscard]] Artifact link_library();

  private:
	// Diagnose missing or conflicting stage entry points for a linked program.
	void validate_program_stages(const Artifact& program);

	DiagnosticEngine& diagnostics_;
	std::vector<Artifact> inputs_;
};

// Module artifacts contain exported signatures and public reflection only.
[[nodiscard]] Artifact extract_module_interface(const Artifact& source);

} // namespace rtsl
