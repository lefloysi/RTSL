#pragma once

#include <artifact.hpp>
#include "support/basic_source_manager.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace rtsl {

enum class DeclKind {
	unknown,
	import,
	function,
	struct_decl,
	uniform,
	namespace_decl,
};

struct Attribute {
	std::string name;
	std::string value;
	SourceSpan span{};
};

struct Decl {
	struct Expr {
		enum class Kind {
			unknown,
			name,
			literal_int,
			literal_float,
			literal_bool,
			call,
			member,
			unary,
			binary,
		};

		Kind kind = Kind::unknown;
		std::string text;
		std::string op;
		std::vector<Expr> children;
		SourceSpan span{};
	};

	enum class BodyStatementKind {
		unknown,
		block,
		if_stmt,
		while_stmt,
		do_stmt,
		for_stmt,
		declaration,
		assignment,
		return_stmt,
		expression,
	};

	struct BodyStatement {
		BodyStatementKind kind = BodyStatementKind::unknown;
		std::string text;
		std::string type_name;
		std::string name;
		std::string initializer;
		std::string lhs;
		std::string rhs;
		std::string condition;
		std::string loop_init;
		std::string loop_continue;
		Expr expr{};
		std::vector<BodyStatement> children;
		std::vector<BodyStatement> else_children;
		SourceSpan span{};
	};

	DeclKind kind = DeclKind::unknown;
	std::string name;
	std::vector<ParameterDecl> parameters;
	std::string return_type;
	std::vector<BodyStatement> body_statements;
	SourceSpan span{};
	bool exported = false;
	// True when the parser consumed a `{ ... }` body for this decl (even an
	// empty one). Forward declarations end in `;` and never open a brace, so
	// they leave this false. Downstream distinguishes "empty body" (valid,
	// zero statements) from "no body" (forward decl the linker resolves).
	bool has_body = false;
	// Authored `@name` or `@name : value` attributes. The parser records
	// syntax; semantic analysis resolves language-known attributes.
	std::vector<Attribute> attributes;
};

// Memory-layout rule for a resource binding's payload. Governs offsets and
// paddings when a host-writable struct becomes bytes in device memory.
// `unset` means the binding kind's default applies (std140 for UniformBuffer,
// std430 for StorageBuffer) — resolved during sema/IR lowering, never
// present past that point.
enum class LayoutRule : u08 {
	unset = 0,
	std140 = 1,
	std430 = 2,
	scalar = 3,
};

[[nodiscard]] inline LayoutRule parse_layout_rule(std::string_view text) {
	if (text == "std140")
		return LayoutRule::std140;
	if (text == "std430")
		return LayoutRule::std430;
	if (text == "scalar")
		return LayoutRule::scalar;
	return LayoutRule::unset;
}

// `layout PATH : [RULE] TYPE;` — attaches a payload type to a
// UniformBuffer/StorageBuffer binding. PATH is a `::`-separated path resolved
// against declared uniforms (e.g. `mat::camera`). RULE is an optional layout
// rule (std140/std430/scalar); when omitted the binding kind's default is
// used.
//
// TYPE is either a named type spelling (`type_spelling` populated, e.g.
// `mat4`) or an inline `struct { ... }` body (`is_inline_struct` true,
// members in `inline_fields`).
struct LayoutDecl {
	std::vector<std::string> path;
	LayoutRule rule = LayoutRule::unset;
	bool is_inline_struct = false;
	std::string type_spelling;              // only when !is_inline_struct
	std::vector<StructField> inline_fields; // only when is_inline_struct
	SourceSpan span{};
};

// `using Alias = Base;`
struct TypeAlias {
	std::string name;
	std::string base;
};

struct UsingImport {
	enum class Kind {
		symbol,
		namespace_scope,
	};
	Kind kind = Kind::symbol;
	std::vector<std::string> path;
	std::string imported_name;
	bool exported = false;
};

struct TranslationUnit {
	u32 file_id = 0;
	std::vector<Decl> declarations;
	std::vector<std::string> imports;
	std::vector<std::string> exported_imports;
	std::vector<ExportSymbol> exports;
	std::vector<StructDecl> structs;
	std::vector<UniformBinding> uniforms;
	std::vector<LayoutDecl> layouts;
	std::vector<StageInterface> stage_interfaces;
	std::vector<TypeAlias> type_aliases;
	std::vector<UsingImport> using_imports;
};

} // namespace rtsl
