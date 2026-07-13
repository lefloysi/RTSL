#pragma once

#include "frontend/ast.hpp"
#include "support/basic_diagnostics.hpp"
#include "frontend/token.hpp"

#include <span>
#include <string>
#include <string_view>

namespace rtsl {

// Recursive-descent parser for RTSL.
//
// First principles the grammar is built on:
//
//   1. A type is a value of the grammar. `struct name? { fields }` is a type
//      expression exactly like `mat4` is — anything that accepts a type
//      accepts an inline struct. Anonymous struct types get compiler-generated
//      names. `struct Foo` without a body is a reference to (or forward
//      declaration of) that type.
//   2. The top scope holds declarations: imports, namespaces, types,
//      functions, uniforms, layouts, and using declarations. Everything else
//      is a diagnostic.
//   3. A return boundary (`-> T : field(tag, ...)`) is part of the return
//      type spec; tags are data-driven, not per-keyword grammar.
//
// See parser.cpp for the full grammar (EBNF).
class Parser {
  public:
	Parser(SourceManager& sources, DiagnosticEngine& diagnostics, u32 file_id, std::span<const Token> tokens);

	[[nodiscard]] TranslationUnit parse_translation_unit();

  private:
	// The result of parsing a type expression. `spelling` is the type's name:
	// the declared name for named types, the source name for `struct Foo
	// {...}`, or a compiler-generated `__anon_struct_N` for anonymous bodies.
	struct ParsedType {
		std::string spelling;
		bool has_body = false;     // a `{ fields }` body was parsed
		bool is_anonymous = false; // body without a source-level name
		std::vector<StructField> fields;               // body fields (copy)
		std::vector<StructMemberFunction> member_functions; // body `fn name(...)`
		std::vector<ParameterDecl> constructor_parameters; // body `fn T(...)`
		bool is_reference = false;
		bool is_const = false;
		bool has_pointer = false;

		[[nodiscard]] bool empty() const { return spelling.empty(); }
	};

	// Token stream navigation.
	[[nodiscard]] const Token& peek(std::size_t lookahead = 0) const;
	[[nodiscard]] bool at(TokenKind kind) const;
	[[nodiscard]] bool at(TokenKind kind, std::size_t lookahead) const;
	[[nodiscard]] bool consume(TokenKind kind);
	bool expect(TokenKind kind, std::string_view what);
	[[nodiscard]] bool at_end() const;

	// Contextual words such as `readonly`, `smooth`, and `std140`
	// are identifiers matched by spelling only where the grammar allows them.
	[[nodiscard]] bool at_word(std::string_view word, std::size_t lookahead = 0) const;
	bool consume_word(std::string_view word);
	// Whether the token at `lookahead` can begin a type expression.
	[[nodiscard]] bool at_type_start(std::size_t lookahead = 0) const;

	// Top-level.
	Decl parse_declaration();
	Decl parse_import(bool exported);
	Decl parse_namespace(bool exported);
	Decl parse_function(bool exported, std::vector<Attribute> attributes);
	// `struct ...;` at top scope: a type expression used as a declaration.
	Decl parse_type_declaration(bool exported);
	Decl parse_invalid_global_declaration(bool exported);
	Decl parse_uniform(bool exported);
	void parse_layout();
	void parse_using(bool exported);

	// Authored `@name` attributes. The parser records spelling only; sema owns
	// the meaning of known attributes.
	std::vector<Attribute> parse_attributes();

	// The single type rule. Consumes:
	//   'const'? type_atom '&'?
	//   type_atom := 'struct' scoped_name? ('{' struct_body '}' )?
	//              | (ident | 'void' | 'auto') ('::' ident)* ('<' ... '>')?
	// A struct body registers the type in the translation unit (generated name
	// when anonymous). Returns an empty ParsedType if the cursor isn't on a
	// type-starting token (cursor untouched in that case unless a body or
	// 'const' was already consumed).
	ParsedType parse_type();

	// Function pieces.
	void parse_function_signature(Decl& decl);
	void parse_parameter_list(std::vector<ParameterDecl>& out);
	// Return boundary (post `-> T`, consumed `:`): `field(tag, ...) , ...`.
	// Tags are recorded by spelling; sema owns their contextual meaning.
	void parse_return_boundary(std::string base_type);
	void maybe_parse_return_boundary(std::string_view base_type);

	// Struct body: fields + member function declarations.
	void parse_struct_body(std::vector<StructField>& fields,
						   std::vector<StructMemberFunction>& member_functions,
						   std::vector<ParameterDecl>& constructor_parameters,
						   std::string_view owner_name);
	void parse_uniform_body(const Decl& decl);

	// Statements.
	Decl::BodyStatement parse_statement();
	Decl::BodyStatement parse_block_statement();
	Decl::BodyStatement parse_if_statement();
	Decl::BodyStatement parse_while_statement();
	Decl::BodyStatement parse_do_statement();
	Decl::BodyStatement parse_for_statement();
	Decl::BodyStatement parse_return_statement();
	// Tries `type ident (= expr)? ;`. Restores the cursor and returns false
	// when the shape doesn't fit (only when no struct body was committed).
	bool try_parse_local_declaration(Decl::BodyStatement& out);
	Decl::BodyStatement parse_expression_or_assignment_statement();

	std::string parse_scoped_name();

	// `type ident ;` via parse_type. Returns empty on failure without
	// consuming (unless a struct body was already committed, which is
	// diagnosed instead).
	StructField parse_field_declaration();

	// Expression parser (precedence climbing).
	Decl::Expr parse_expression();
	Decl::Expr parse_assignment();
	Decl::Expr parse_logical_or();
	Decl::Expr parse_logical_and();
	Decl::Expr parse_bitwise_or();
	Decl::Expr parse_bitwise_xor();
	Decl::Expr parse_bitwise_and();
	Decl::Expr parse_equality();
	Decl::Expr parse_relational();
	Decl::Expr parse_additive();
	Decl::Expr parse_multiplicative();
	Decl::Expr parse_unary();
	Decl::Expr parse_postfix();
	Decl::Expr parse_primary();

	// Raw source substring spanned by tokens [begin, end). Populates the
	// BodyStatement text fields downstream IR still reads.
	std::string source_between(std::size_t begin_cursor, std::size_t end_cursor) const;

	[[nodiscard]] std::string resolve_alias(std::string_view name) const;
	bool reject_non_parameter_type_qualifiers(const ParsedType& type, std::string_view context);

	// Recovery.
	void skip_to_declaration_boundary(bool consume_right_brace = false);
	void skip_to_statement_boundary();

	void diagnose(const Token& token, std::string_view message);
	void diagnose_here(std::string_view message);

	TranslationUnit* unit_ = nullptr;

	SourceManager& sources_;
	DiagnosticEngine& diagnostics_;
	u32 file_id_ = 0;
	std::span<const Token> tokens_;
	std::size_t cursor_ = 0;
	u32 next_anonymous_block_id_ = 0;
	u32 next_anonymous_struct_id_ = 0;
};

} // namespace rtsl
