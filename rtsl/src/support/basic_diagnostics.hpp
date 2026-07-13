#pragma once

#include "support/basic_source_manager.hpp"

#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rtsl {

enum class DiagnosticSeverity : u08 {
	ignored,
	note,
	warning,
	error,
	fatal,
};

enum class DiagnosticCode : int {
	none = 0,

	lexer_invalid_character = 1001,

	parser_syntax = 2001,
	compiler_no_input = 2002,
	compiler_file_read_failed = 2003,
	compiler_validation_failed = 2004,

	sema_reserved_namespace = 3001,
	sema_duplicate_namespace_decl = 3002,
	sema_duplicate_export_decl = 3003,
	sema_unknown_type = 3004,
	sema_unknown_name = 3005,
	sema_type_mismatch = 3006,
	sema_argument_mismatch = 3007,
	sema_unknown_member = 3008,
	sema_not_callable = 3009,
	sema_member_fn_unknown_owner = 3010,
	sema_member_fn_no_declaration = 3011,

	ir_lowering_failed = 3100,
	layout_duplicate = 3101,
	layout_missing_resource_type = 3102,
	layout_unknown_type = 3103,
	layout_unknown_uniform = 3104,
	layout_invalid_uniform_kind = 3105,
	ir_expression_error = 3201,
	ir_invalid_stage_signature = 3202,
	ir_verification_failed = 3203,

	artifact_error = 5001,

	link_empty_artifact = 6001,
	link_no_inputs = 6002,
	link_conflict = 6003,
	link_unresolved_call = 6004,
	link_missing_entry = 6005,
	link_duplicate_stage = 6006,
	link_missing_stage = 6007,
};

struct Diagnostic {
	int code = 0;
	DiagnosticSeverity severity = DiagnosticSeverity::error;
	std::string source_name;
	SourceLocation location{};
	std::string message;
};

class DiagnosticEngine {
  public:
	void clear();
	void report(int code, DiagnosticSeverity severity, SourceLocation location, std::string_view source_name, std::string_view message);
	void report(DiagnosticCode code, DiagnosticSeverity severity, SourceLocation location, std::string_view source_name, std::string_view message);
	void render(std::ostream& out, const SourceManager* sources = nullptr) const;

	[[nodiscard]] bool has_error() const;
	[[nodiscard]] std::span<const Diagnostic> diagnostics() const { return diagnostics_; }

  private:
	std::vector<Diagnostic> diagnostics_;
};

} // namespace rtsl
