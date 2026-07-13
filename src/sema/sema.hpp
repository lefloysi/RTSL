#pragma once

#include "frontend/ast.hpp"
#include "support/basic_diagnostics.hpp"

#include <string>
#include <vector>

namespace rtsl {

struct SemanticSymbol {
	DeclKind kind = DeclKind::unknown;
	std::string name;
	std::vector<ParameterDecl> parameters;
	std::string return_type;
	std::vector<Decl::BodyStatement> body_statements;
	bool exported = false;
	std::vector<Attribute> attributes;
	// Resolved from language-known function attributes.
	StageKind stage = StageKind::none;
	// True when the source-level decl had a `{ ... }` body — as opposed to a
	// `;`-terminated forward declaration.
	bool has_body = false;
	// Source span of the originating declaration, for diagnostics.
	SourceSpan span{};
};

struct SemanticModule {
	std::string source_name;
	std::vector<std::string> imports;
	std::vector<ExportSymbol> imported_exports;
	std::vector<ExportSymbol> exports;
	std::vector<SemanticSymbol> symbols;
	std::vector<StructDecl> structs;
	std::vector<UniformBinding> uniforms;
	std::vector<LayoutDecl> layouts;
	std::vector<StageInterface> stage_interfaces;
	std::vector<TypeAlias> type_aliases;
	std::vector<UsingImport> using_imports;
};

class Sema {
  public:
	Sema(SourceManager& sources, DiagnosticEngine& diagnostics);

	[[nodiscard]] SemanticModule analyze(const TranslationUnit& unit);

  private:
	SourceManager& sources_;
	DiagnosticEngine& diagnostics_;
};

} // namespace rtsl
