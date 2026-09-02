#ifndef RTSL_LINKER_LINKER_HPP
#define RTSL_LINKER_LINKER_HPP

#include <rtsl/IR/Builder.hpp>
#include <rtsl/IR/Verifier.hpp>

#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace rtsl {

enum class LinkDiagnosticCode : std::uint8_t {
	link_duplicate_definition,
	link_missing_definition,
	link_incompatible_declaration,
	link_duplicate_stage,
	link_incompatible_stage_interface,
	link_identity_vertex_unavailable,
};

struct LinkDiagnostic {
	LinkDiagnosticCode Code{LinkDiagnosticCode::link_incompatible_declaration};
	std::string Symbol;
	std::string Message;
};

struct LinkResult {
	ir::Module Module;
	std::vector<LinkDiagnostic> Diagnostics;
	ir::VerificationResult Verification;
	[[nodiscard]] bool succeeded() const { return Diagnostics.empty() && Verification.valid(); }
};

class Linker {
public:
	[[nodiscard]] LinkResult link(std::string_view ProgramName, std::span<const ir::Module> Modules);

private:
	struct ModuleMaps {
		std::unordered_map<std::uint32_t, ir::TypeId> Types;
		std::unordered_map<std::uint32_t, ir::SymbolId> Symbols;
		std::unordered_map<std::uint32_t, ir::FunctionId> Functions;
	};

	void copyTypes(const ir::Module& Module, ModuleMaps& Maps);
	void copySymbols(const ir::Module& Module, ModuleMaps& Maps);
	void declareFunctions(const ir::Module& Module, ModuleMaps& Maps);
	void defineFunctions(const ir::Module& Module, ModuleMaps& Maps);
	void copyMetadata(const ir::Module& Module, const ModuleMaps& Maps);
	void synthesizeIdentityVertexStage();
	void validateDefinitions();
	void validateStageInterfaces();
	[[nodiscard]] ir::TypeId interfaceInput(const ir::EntryPoint& Entry) const;
	[[nodiscard]] ir::TypeId interfaceOutput(const ir::EntryPoint& Entry) const;
	[[nodiscard]] ir::TypeId unwrapInterfaceType(ir::TypeId Type) const;
	[[nodiscard]] bool sameSignature(const ir::Function& Left, ir::TypeId ReturnType,
		std::span<const ir::TypeId> Parameters) const;
	void diagnose(LinkDiagnosticCode Code, std::string_view Symbol, std::string_view Message);

	ir::ModuleBuilder Builder;
	std::vector<LinkDiagnostic> Diagnostics;
	std::unordered_map<std::uint32_t, ir::FunctionId> FunctionsBySymbol;
	std::unordered_set<const ir::Function*> SelectedDefinitions;
	using EntryStages = std::unordered_map<std::uint32_t, ir::EntryPoint>;
	std::unordered_map<std::string, EntryStages> EntriesBySourceName;
};

}

#endif
