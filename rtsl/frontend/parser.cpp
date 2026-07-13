// RTSL grammar checker / parser.
//
// Recursive descent, one function per nonterminal. Built on three first
// principles instead of per-construct special cases:
//
//   1. `struct name? { fields }` is a TYPE EXPRESSION. It is the same grammar
//      object as `mat4`. Every context that accepts a type — top-level
//      declarations, uniform members, layout payloads, using-aliases,
//      parameters, return types, struct fields, locals — accepts it through
//      the single `type` rule below. Anonymous bodies get compiler-generated
//      names. A bodyless `struct Foo` is a reference to / forward declaration
//      of that type.
//   2. The top scope holds exactly: imports, namespaces, type declarations
//      (`struct T {...};`, `using A = T;`), resource scopes (`uniform`),
//      and functions (`fn`).
//   3. The return boundary `-> T : field(tag, ...)` belongs to the return
//      type. Tags are data (a lookup table), not grammar.
//
// Grammar (EBNF):
//
// Words in single quotes that are NOT in frontend/tokens.def ('readonly',
// 'writeonly', 'smooth', 'flat', 'clip', 'std140', 'std430',
// 'scalar') are contextual identifiers: the parser matches them by spelling
// only in these positions.
//
//   translation_unit := decl*
//   decl := 'export'? attribute* ( import | namespace | function
//                     | type_decl | uniform | layout | using )
//   attribute   := '@' ident (':' ident)?
//   import      := 'import' ( '<' path '>' | string_literal ) ';'
//   namespace   := 'namespace' scoped_name '{' balanced_body '}' ';'?
//   function    := 'fn' scoped_name '(' params? ')'
//                  ('->' type return_boundary?)? ( block | ';' )
//   type_decl   := type ';'                  // `struct T {...};`, `struct T;`
//   uniform     := 'uniform' scoped_name? '{' uniform_body '}' ';'?
//   layout      := 'layout' scoped_name ':' layout_rule? type ';'
//   using       := 'using' 'namespace'? scoped_name ';'
//                | 'using' ident '=' type return_boundary? ';'
//   type        := 'const'? type_atom '&'?
//   type_atom   := 'struct' scoped_name? ('{' struct_body '}')?
//                | (ident | 'void' | 'auto') ('::' ident)* ('<' balanced '>')?
//   scoped_name := ident ('::' ('~' ident | ident))*
//
//   struct_body      := ( member_fn | field )*
//   member_fn        := 'fn' ident '(' params? ')' ('->' type)? ( block | ';' )
//   field            := type ident ';'
//
//   params := param (',' param)*
//   param  := type ident?
//
//   return_boundary := ':' entry (',' entry)*
//   entry           := ident '(' tag (',' tag)* ')'
//   tag             := ident
//
//   uniform_body := ( access_qual? field )*      // field type may be inline
//   access_qual  := 'readonly' | 'writeonly'
//   statement := block | if | while | do | for | return | local_decl | expr_stmt
//   block      := '{' statement* '}'
//   if         := 'if' '(' expr ')' statement ( 'else' statement )?
//   while      := 'while' '(' expr ')' statement
//   do         := 'do' statement 'while' '(' expr ')' ';'
//   for        := 'for' '(' for_init? ';' expr? ';' expr? ')' statement
//   for_init   := local_decl_head | expr_or_assign
//   return     := 'return' expr? ';'
//   local_decl := type ident ( '=' expr )? ';'
//   expr_stmt  := expr ( '=' expr )? ';'
//
//   expr          := assignment
//   assignment    := logical_or ( '=' assignment )?     // right-assoc
//   logical_or    := logical_and  ( '||' logical_and  )*
//   logical_and   := bitwise_or   ( '&&' bitwise_or   )*
//   bitwise_or    := bitwise_xor  ( '|'  bitwise_xor  )*
//   bitwise_xor   := bitwise_and  ( '^'  bitwise_and  )*
//   bitwise_and   := equality     ( '&'  equality     )*
//   equality      := relational   ( ('==' | '!=') relational )*
//   relational    := additive     ( ('<' | '<=' | '>' | '>=') additive )*
//   additive      := multiplicative ( ('+' | '-') multiplicative )*
//   multiplicative:= unary          ( ('*' | '/' | '%') unary )*
//   unary         := ('+' | '-' | '!' | '~') unary | postfix
//   postfix       := primary ( '.' ident | '::' ident
//                            | '(' arglist? ')' | '[' expr ']' )*
//   primary       := ident | integer | float | string
//                   | 'true' | 'false' | '(' expr ')'
//   arglist       := expr ( ',' expr )*

#include "frontend/parser.hpp"

#include <cctype>
#include <unordered_map>
#include <utility>

namespace rtsl {

namespace {

std::string join_source(std::string_view buffer, std::size_t start, std::size_t end) {
	if (end <= start || end > buffer.size()) return {};
	return std::string(buffer.substr(start, end - start));
}

Decl::Expr make_binary(std::string op, Decl::Expr lhs, Decl::Expr rhs) {
	Decl::Expr expr;
	expr.kind = Decl::Expr::Kind::binary;
	expr.op = std::move(op);
	expr.children = { std::move(lhs), std::move(rhs) };
	return expr;
}

Decl::Expr make_unary(std::string op, Decl::Expr child) {
	Decl::Expr expr;
	expr.kind = Decl::Expr::Kind::unary;
	expr.op = std::move(op);
	expr.children = { std::move(child) };
	return expr;
}

} // namespace

Parser::Parser(SourceManager& sources, DiagnosticEngine& diagnostics, u32 file_id, std::span<const Token> tokens)
	: sources_(sources), diagnostics_(diagnostics), file_id_(file_id), tokens_(tokens) {}

// ---------------------------------------------------------------------------
// Token stream helpers
// ---------------------------------------------------------------------------

const Token& Parser::peek(std::size_t lookahead) const {
	const auto index = cursor_ + lookahead;
	return tokens_[index < tokens_.size() ? index : tokens_.size() - 1];
}

bool Parser::at(TokenKind kind) const { return peek().kind == kind; }
bool Parser::at(TokenKind kind, std::size_t lookahead) const { return peek(lookahead).kind == kind; }

bool Parser::consume(TokenKind kind) {
	if (!at(kind)) return false;
	++cursor_;
	return true;
}

bool Parser::expect(TokenKind kind, std::string_view what) {
	if (consume(kind)) return true;
	diagnose_here(what);
	return false;
}

bool Parser::at_end() const { return at(TokenKind::end_of_file); }

bool Parser::at_word(std::string_view word, std::size_t lookahead) const {
	const Token& token = peek(lookahead);
	return token.kind == TokenKind::identifier && token.text == word;
}

bool Parser::consume_word(std::string_view word) {
	if (!at_word(word)) return false;
	++cursor_;
	return true;
}

bool Parser::at_type_start(std::size_t lookahead) const {
	switch (peek(lookahead).kind) {
	case TokenKind::identifier:
	case TokenKind::kw_Struct:
	case TokenKind::kw_Const:
	case TokenKind::kw_Void:
	case TokenKind::kw_Auto:
		return true;
	default:
		return false;
	}
}

// ---------------------------------------------------------------------------
// Diagnostics / recovery
// ---------------------------------------------------------------------------

void Parser::diagnose(const Token& token, std::string_view message) {
	diagnostics_.report(DiagnosticCode::parser_syntax, DiagnosticSeverity::error, token.span.begin, sources_.name(file_id_), message);
}

void Parser::diagnose_here(std::string_view message) { diagnose(peek(), message); }

void Parser::skip_to_declaration_boundary(bool consume_right_brace) {
	int depth = 0;
	while (!at_end()) {
		const auto kind = peek().kind;
		if (depth == 0 && (kind == TokenKind::semicolon || kind == TokenKind::right_brace)) break;
		if (kind == TokenKind::left_brace) ++depth;
		else if (kind == TokenKind::right_brace && depth > 0) --depth;
		++cursor_;
	}
	if (at(TokenKind::semicolon) || (consume_right_brace && at(TokenKind::right_brace))) ++cursor_;
}

void Parser::skip_to_statement_boundary() {
	int paren = 0, brack = 0, brace = 0;
	while (!at_end()) {
		const auto kind = peek().kind;
		if (kind == TokenKind::semicolon && paren == 0 && brack == 0 && brace == 0) {
			++cursor_;
			return;
		}
		if (kind == TokenKind::right_brace && paren == 0 && brack == 0 && brace == 0) return;
		if (kind == TokenKind::left_paren) ++paren;
		else if (kind == TokenKind::right_paren && paren > 0) --paren;
		else if (kind == TokenKind::left_bracket) ++brack;
		else if (kind == TokenKind::right_bracket && brack > 0) --brack;
		else if (kind == TokenKind::left_brace) ++brace;
		else if (kind == TokenKind::right_brace && brace > 0) --brace;
		++cursor_;
	}
}

// ---------------------------------------------------------------------------
// The type rule — the heart of the grammar
// ---------------------------------------------------------------------------

Parser::ParsedType Parser::parse_type() {
	ParsedType type;
	if (consume(TokenKind::kw_Const)) type.is_const = true;

	if (consume(TokenKind::kw_Struct)) {
		// struct type atom: `struct` scoped_name? (`{` struct_body `}`)?
		type.spelling = parse_scoped_name(); // may be empty (anonymous)

		if (consume(TokenKind::left_brace)) {
			type.has_body = true;
			type.is_anonymous = type.spelling.empty();
			if (type.is_anonymous) {
				type.spelling = "__anon_struct_" + std::to_string(next_anonymous_struct_id_++);
			}
			parse_struct_body(type.fields, type.member_functions, type.constructor_parameters, type.spelling);
			(void)consume(TokenKind::right_brace);

			// A struct body defines a type: register it with the unit so the
			// rest of the compiler sees it exactly like any named type.
			if (unit_) {
				unit_->structs.emplace_back(StructDecl{
					.name = type.spelling,
					.fields = type.fields,
					.member_functions = type.member_functions,
					.constructor_parameters = type.constructor_parameters,
				});
			}
		} else if (type.spelling.empty()) {
			diagnose_here("expected struct name or '{' after 'struct'");
			return {};
		}
		// else: bodyless `struct Foo` — a reference / forward declaration.
	} else if (consume(TokenKind::kw_Void)) {
		type.spelling = "void";
	} else if (consume(TokenKind::kw_Auto)) {
		type.spelling = "auto";
	} else if (at(TokenKind::identifier)) {
		type.spelling = std::string(peek().text);
		++cursor_;
		while (consume(TokenKind::colon_colon)) {
			if (!at(TokenKind::identifier)) {
				diagnose_here("expected identifier after '::' in type");
				break;
			}
			type.spelling += "::";
			type.spelling += std::string(peek().text);
			++cursor_;
		}
		if (consume(TokenKind::less)) {
			type.spelling += "<";
			int depth = 1;
			while (!at_end() && depth > 0) {
				if (consume(TokenKind::less))    { type.spelling += "<"; ++depth; continue; }
				if (consume(TokenKind::greater)) { type.spelling += ">"; --depth; continue; }
				type.spelling += std::string(peek().text);
				++cursor_;
			}
		}
	} else {
		return {};
	}

	if (consume(TokenKind::amp)) {
		type.is_reference = true;
	}
	if (consume(TokenKind::star)) {
		type.has_pointer = true;
		diagnose_here("pointers are not RTSL source syntax");
	}

	return type;
}

bool Parser::reject_non_parameter_type_qualifiers(const ParsedType& type, std::string_view context) {
	if (type.has_pointer) {
		return true;
	}
	if (!type.is_reference) {
		return false;
	}
	diagnose_here(std::string("references are only supported in parameter declarations; found reference in ") + std::string(context));
	return true;
}

void Parser::parse_struct_body(std::vector<StructField>& fields,
							   std::vector<StructMemberFunction>& member_functions,
							   std::vector<ParameterDecl>& constructor_parameters,
							   std::string_view owner_name) {
	while (!at_end() && !at(TokenKind::right_brace)) {
		// Member function declaration: `fn name(args) [-> Return];`
		if (at(TokenKind::kw_Function)) {
			++cursor_;
			if (!at(TokenKind::identifier)) {
				diagnose_here("expected member function name");
				skip_to_declaration_boundary();
				continue;
			}
			StructMemberFunction member;
			member.name = std::string(peek().text);
			++cursor_;
			if (!expect(TokenKind::left_paren, "expected '(' after member function name")) {
				skip_to_declaration_boundary();
				continue;
			}
			parse_parameter_list(member.parameters);
			if (!expect(TokenKind::right_paren, "expected ')' after member function parameters")) {
				skip_to_declaration_boundary();
				continue;
			}
			if (consume(TokenKind::arrow)) {
				ParsedType ret = parse_type();
				if (ret.empty()) {
					diagnose_here("expected return type after '->'");
					skip_to_declaration_boundary();
					continue;
				}
				if (reject_non_parameter_type_qualifiers(ret, "member function return type")) {
					skip_to_declaration_boundary();
					continue;
				}
				member.return_type = resolve_alias(ret.spelling);
			}
			if (member.name == owner_name) {
				constructor_parameters = member.parameters;
			}

			if (at(TokenKind::left_brace)) {
				Decl inline_decl{
					.kind = DeclKind::function,
					.name = std::string(owner_name) + "::" + member.name,
					.parameters = member.parameters,
					.return_type = member.return_type,
					.span = peek().span,
					.has_body = true,
				};
				auto block = parse_block_statement();
				inline_decl.body_statements = std::move(block.children);
				if (unit_) {
					unit_->declarations.push_back(std::move(inline_decl));
				}
			} else if (!expect(TokenKind::semicolon, "expected function body '{' or ';' after member function declaration")) {
				skip_to_declaration_boundary();
				continue;
			}
			member_functions.push_back(std::move(member));
			continue;
		}

		auto field = parse_field_declaration();
		if (!field.type.empty() && !field.name.empty()) {
			fields.emplace_back(std::move(field));
			continue;
		}
		diagnose_here("expected struct field");
		skip_to_declaration_boundary();
	}
}

StructField Parser::parse_field_declaration() {
	const auto save = cursor_;
	ParsedType type = parse_type();
	if (type.empty()) {
		cursor_ = save;
		return {};
	}
	if (reject_non_parameter_type_qualifiers(type, "struct field")) {
		return {};
	}
	if (!at(TokenKind::identifier)) {
		if (type.has_body) {
			// The struct body is committed — restoring would lose it silently.
			diagnose_here("expected field name after struct type");
			return {};
		}
		cursor_ = save;
		return {};
	}
	std::string name{ peek().text };
	++cursor_;
	if (!consume(TokenKind::semicolon)) {
		if (type.has_body) {
			diagnose_here("expected ';' after field");
			return {};
		}
		cursor_ = save;
		return {};
	}
	return StructField{ .type = std::move(type.spelling), .name = std::move(name) };
}

// ---------------------------------------------------------------------------
// Top-level
// ---------------------------------------------------------------------------

TranslationUnit Parser::parse_translation_unit() {
	TranslationUnit unit{ .file_id = file_id_ };
	unit_ = &unit;
	while (!at_end()) {
		const auto before = cursor_;
		auto decl = parse_declaration();
		if (decl.kind != DeclKind::unknown) unit.declarations.push_back(std::move(decl));
		if (cursor_ == before) ++cursor_; // forward-progress guard
	}
	unit_ = nullptr;
	return unit;
}

std::vector<Attribute> Parser::parse_attributes() {
	std::vector<Attribute> attributes;
	while (consume(TokenKind::at)) {
		if (!at(TokenKind::identifier)) {
			diagnose_here("expected attribute name after '@'");
			break;
		}
		Attribute attribute{
			.name = std::string(peek().text),
			.span = peek().span,
		};
		++cursor_;
		if (consume(TokenKind::colon)) {
			if (!at(TokenKind::identifier)) {
				diagnose_here("expected attribute value after ':'");
			} else {
				attribute.value = std::string(peek().text);
				++cursor_;
			}
		}
		attributes.push_back(std::move(attribute));
	}
	return attributes;
}

Decl Parser::parse_declaration() {
	const bool exported = consume(TokenKind::kw_Export);
	auto attributes = parse_attributes();

	if (!attributes.empty() && !at(TokenKind::kw_Function)) {
		diagnose_here("attributes must precede a fn declaration");
	}

	switch (peek().kind) {
	case TokenKind::kw_Import:    return parse_import(exported);
	case TokenKind::kw_Namespace: return parse_namespace(exported);
	case TokenKind::kw_Function:  return parse_function(exported, std::move(attributes));
	case TokenKind::kw_Uniform:   return parse_uniform(exported);
	case TokenKind::kw_Layout:    parse_layout(); return {};
	case TokenKind::kw_Using:     parse_using(exported); return {};
	case TokenKind::kw_Struct: {
		const auto save = cursor_;
		const auto struct_count = unit_ ? unit_->structs.size() : 0;
		ParsedType type = parse_type();
		if (!type.empty() && at(TokenKind::identifier)) {
			cursor_ = save;
			if (unit_) unit_->structs.resize(struct_count);
			return parse_invalid_global_declaration(exported);
		}
		cursor_ = save;
		if (unit_) unit_->structs.resize(struct_count);
		return parse_type_declaration(exported);
	}
	default: break;
	}

	if (at(TokenKind::identifier) || at(TokenKind::kw_Const) || at(TokenKind::kw_Void) || at(TokenKind::kw_Auto)) {
		const auto save = cursor_;
		ParsedType type = parse_type();
		if (!type.empty() && at(TokenKind::identifier)) {
			cursor_ = save;
			return parse_invalid_global_declaration(exported);
		}
		cursor_ = save;
	}

	if (exported) diagnose_here("expected declaration after 'export'");
	else diagnose_here("expected a top-level declaration: fn, struct, using, uniform, layout, import, or namespace");
	skip_to_declaration_boundary(true);
	return {};
}

Decl Parser::parse_import(bool exported) {
	const Token start = peek();
	(void)consume(TokenKind::kw_Import);

	std::string name;
	if (consume(TokenKind::less)) {
		while (!at_end() && !at(TokenKind::greater)) {
			name += std::string(peek().text);
			++cursor_;
		}
		if (!expect(TokenKind::greater, "unterminated import path")) {
			skip_to_declaration_boundary();
			return {};
		}
	} else if (at(TokenKind::string_literal)) {
		const auto raw = peek().text;
		name = std::string(raw.substr(1, raw.size() >= 2 ? raw.size() - 2 : 0));
		++cursor_;
	} else {
		diagnose_here("expected '<...>' or \"...\" after 'import'");
		skip_to_declaration_boundary();
		return {};
	}

	if (!expect(TokenKind::semicolon, "expected ';' after import")) {
		skip_to_declaration_boundary();
	}

	if (unit_) {
		unit_->imports.push_back(name);
		if (exported) {
			unit_->exported_imports.push_back(name);
		}
	}
	return Decl{ .kind = DeclKind::import, .name = std::move(name), .span = start.span, .exported = exported };
}

namespace {

[[nodiscard]] bool is_qualified(std::string_view name) {
	return name.find("::") != std::string_view::npos;
}

[[nodiscard]] std::string qualify_name(std::string_view scope, std::string_view name) {
	if (scope.empty() || name.empty() || is_qualified(name)) {
		return std::string(name);
	}
	return std::string(scope) + "::" + std::string(name);
}

void qualify_type_name(std::string& type, const std::unordered_map<std::string, std::string>& local_types) {
	if (type.empty() || is_qualified(type)) {
		return;
	}
	if (const auto it = local_types.find(type); it != local_types.end()) {
		type = it->second;
	}
}

void qualify_field_types(std::vector<StructField>& fields, const std::unordered_map<std::string, std::string>& local_types) {
	for (auto& field : fields) {
		qualify_type_name(field.type, local_types);
	}
}

void qualify_parameter_types(std::vector<ParameterDecl>& parameters, const std::unordered_map<std::string, std::string>& local_types) {
	for (auto& parameter : parameters) {
		qualify_type_name(parameter.type, local_types);
	}
}

void qualify_statement_types(Decl::BodyStatement& statement, const std::unordered_map<std::string, std::string>& local_types) {
	qualify_type_name(statement.type_name, local_types);
	for (auto& child : statement.children) {
		qualify_statement_types(child, local_types);
	}
	for (auto& child : statement.else_children) {
		qualify_statement_types(child, local_types);
	}
}

void qualify_expression_names(Decl::Expr& expr, const std::unordered_map<std::string, std::string>& local_names) {
	if (expr.kind == Decl::Expr::Kind::name && !is_qualified(expr.text)) {
		if (const auto it = local_names.find(expr.text); it != local_names.end()) {
			expr.text = it->second;
		}
	}
	for (auto& child : expr.children) {
		qualify_expression_names(child, local_names);
	}
}

void qualify_statement_expressions(Decl::BodyStatement& statement, const std::unordered_map<std::string, std::string>& local_names) {
	qualify_expression_names(statement.expr, local_names);
	for (auto& child : statement.children) {
		qualify_statement_expressions(child, local_names);
	}
	for (auto& child : statement.else_children) {
		qualify_statement_expressions(child, local_names);
	}
}

void qualify_namespace_declarations(TranslationUnit& unit, std::string_view scope,
									std::size_t decl_begin, std::size_t struct_begin,
									std::size_t uniform_begin, std::size_t layout_begin,
									std::size_t alias_begin, std::size_t interface_begin) {
	std::unordered_map<std::string, std::string> local_types;
	for (std::size_t i = struct_begin; i < unit.structs.size(); ++i) {
		if (!is_qualified(unit.structs[i].name)) {
			local_types.emplace(unit.structs[i].name, qualify_name(scope, unit.structs[i].name));
		}
	}
	for (std::size_t i = alias_begin; i < unit.type_aliases.size(); ++i) {
		if (!is_qualified(unit.type_aliases[i].name)) {
			local_types.emplace(unit.type_aliases[i].name, qualify_name(scope, unit.type_aliases[i].name));
		}
	}
	std::unordered_map<std::string, std::string> local_names = local_types;
	for (std::size_t i = decl_begin; i < unit.declarations.size(); ++i) {
		const auto& decl = unit.declarations[i];
		if (decl.kind == DeclKind::function && !is_qualified(decl.name)) {
			local_names.emplace(decl.name, qualify_name(scope, decl.name));
		}
	}

	for (std::size_t i = decl_begin; i < unit.declarations.size(); ++i) {
		auto& decl = unit.declarations[i];
		if (decl.kind == DeclKind::import || decl.kind == DeclKind::namespace_decl) {
			continue;
		}
		qualify_type_name(decl.return_type, local_types);
		qualify_parameter_types(decl.parameters, local_types);
		for (auto& statement : decl.body_statements) {
			// Flattened namespace bodies need local type/function references
			// rewritten to the same qualified spellings as their declarations.
			qualify_statement_types(statement, local_types);
			qualify_statement_expressions(statement, local_names);
		}
		decl.name = qualify_name(scope, decl.name);
	}
	for (std::size_t i = struct_begin; i < unit.structs.size(); ++i) {
		auto& decl = unit.structs[i];
		decl.name = qualify_name(scope, decl.name);
		qualify_field_types(decl.fields, local_types);
		for (auto& member : decl.member_functions) {
			for (auto& parameter : member.parameters) {
				qualify_type_name(parameter.type, local_types);
			}
			qualify_type_name(member.return_type, local_types);
		}
	}
	for (std::size_t i = uniform_begin; i < unit.uniforms.size(); ++i) {
		auto& uniform = unit.uniforms[i];
		if (!uniform.is_anonymous) {
			uniform.scope_name = qualify_name(scope, uniform.scope_name);
		}
	}
	for (std::size_t i = layout_begin; i < unit.layouts.size(); ++i) {
		auto& layout = unit.layouts[i];
		if (!layout.path.empty()) {
			layout.path.front() = qualify_name(scope, layout.path.front());
		}
		qualify_type_name(layout.type_spelling, local_types);
		qualify_field_types(layout.inline_fields, local_types);
	}
	for (std::size_t i = alias_begin; i < unit.type_aliases.size(); ++i) {
		qualify_type_name(unit.type_aliases[i].base, local_types);
		unit.type_aliases[i].name = qualify_name(scope, unit.type_aliases[i].name);
	}
	for (std::size_t i = interface_begin; i < unit.stage_interfaces.size(); ++i) {
		unit.stage_interfaces[i].type_name = qualify_name(scope, unit.stage_interfaces[i].type_name);
	}
}

} // namespace

Decl Parser::parse_namespace(bool exported) {
	const Token start = peek();
	(void)consume(TokenKind::kw_Namespace);

	const auto name = parse_scoped_name();
	if (name.empty()) diagnose_here("expected namespace name");

	if (!expect(TokenKind::left_brace, "expected '{' to open namespace")) {
		skip_to_declaration_boundary(true);
		return {};
	}

	const std::size_t decl_begin = unit_ ? unit_->declarations.size() : 0;
	const std::size_t struct_begin = unit_ ? unit_->structs.size() : 0;
	const std::size_t uniform_begin = unit_ ? unit_->uniforms.size() : 0;
	const std::size_t layout_begin = unit_ ? unit_->layouts.size() : 0;
	const std::size_t alias_begin = unit_ ? unit_->type_aliases.size() : 0;
	const std::size_t interface_begin = unit_ ? unit_->stage_interfaces.size() : 0;

	while (!at_end() && !at(TokenKind::right_brace)) {
		const auto before = cursor_;
		auto decl = parse_declaration();
		if (decl.kind != DeclKind::unknown && unit_) {
			unit_->declarations.push_back(std::move(decl));
		}
		if (cursor_ == before) {
			++cursor_;
		}
	}
	if (!expect(TokenKind::right_brace, "expected '}' to close namespace")) {
		skip_to_declaration_boundary(true);
		return {};
	}
	(void)consume(TokenKind::semicolon);

	if (unit_ && !name.empty()) {
		qualify_namespace_declarations(*unit_, name, decl_begin, struct_begin, uniform_begin, layout_begin, alias_begin, interface_begin);
	}
	return Decl{ .kind = DeclKind::namespace_decl, .name = name, .span = start.span, .exported = exported };
}

Decl Parser::parse_function(bool exported, std::vector<Attribute> attributes) {
	const Token start = peek();
	(void)consume(TokenKind::kw_Function);

	auto name = parse_scoped_name();
	if (name.empty()) {
		diagnose_here("expected function name");
		skip_to_declaration_boundary(true);
		return {};
	}

	Decl decl{
		.kind = DeclKind::function,
		.name = std::move(name),
		.span = start.span,
		.exported = exported,
		.attributes = std::move(attributes),
	};

	parse_function_signature(decl);

	// Constructor recognition: `Foo::Foo(...)` — owner segment equals member.
	if (const auto scope = decl.name.find("::"); scope != std::string::npos) {
		const auto owner  = std::string_view(decl.name).substr(0, scope);
		const auto member = std::string_view(decl.name).substr(scope + 2);
		if (owner == member && decl.return_type != "void") {
			diagnose_here("constructors must not specify a return type");
		}
	}

	if (consume(TokenKind::semicolon)) return decl; // forward declaration
	if (at(TokenKind::left_brace)) {
		auto block = parse_block_statement();
		decl.body_statements = std::move(block.children);
		decl.has_body = true;
	} else {
		diagnose_here("expected function body '{' or ';'");
		skip_to_declaration_boundary();
	}
	return decl;
}

// `struct ...;` at top scope. The type rule does all the work (body parsing,
// registration, anonymous naming); this demands the terminating ';' and a
// name — RTSL has no globals, so a type declaration must declare a type name.
Decl Parser::parse_type_declaration(bool exported) {
	const Token start = peek();
	ParsedType type = parse_type();
	if (type.empty()) {
		skip_to_declaration_boundary(true);
		return {};
	}

	if (type.is_anonymous) {
		diagnose(start, "anonymous struct declaration declares nothing");
	}
	if (reject_non_parameter_type_qualifiers(type, "type declaration")) {
		skip_to_declaration_boundary();
		return {};
	}

	if (!expect(TokenKind::semicolon, type.has_body
			? "expected ';' after struct definition"
			: "expected ';' after struct declaration")) {
		skip_to_declaration_boundary();
	}

	return Decl{
		.kind = DeclKind::struct_decl,
		.name = std::move(type.spelling),
		.span = start.span,
		.exported = exported,
		.has_body = type.has_body,
	};
}

Decl Parser::parse_invalid_global_declaration(bool exported) {
	const Token start = peek();
	const auto struct_count = unit_ ? unit_->structs.size() : 0;

	ParsedType type = parse_type();
	if (type.empty()) {
		skip_to_declaration_boundary(true);
		return {};
	}
	(void)reject_non_parameter_type_qualifiers(type, "global declaration");

	if (at(TokenKind::identifier)) {
		++cursor_;
	}

	diagnose(start, exported
		? "exported global variables are not supported"
		: "global variables are not supported");

	if (unit_) unit_->structs.resize(struct_count);

	if (!expect(TokenKind::semicolon, "expected ';' after invalid global declaration")) {
		skip_to_declaration_boundary();
	}

	return {};
}

Decl Parser::parse_uniform(bool exported) {
	const Token start = peek();
	(void)consume(TokenKind::kw_Uniform);

	Decl decl{
		.kind = DeclKind::uniform,
		.name = parse_scoped_name(),
		.span = start.span,
		.exported = exported,
	};

	if (!expect(TokenKind::left_brace, "expected '{' to open uniform scope")) {
		skip_to_declaration_boundary(true);
		return decl;
	}
	parse_uniform_body(decl);
	(void)consume(TokenKind::right_brace);
	(void)consume(TokenKind::semicolon);
	return decl;
}

// Uniform members are ordinary `type name;` fields — the type rule covers
// inline structs, so `struct { ... } params;` needs no special case.
void Parser::parse_uniform_body(const Decl& decl) {
	const bool is_anonymous = decl.name.empty();
	const u32 anonymous_block_id = is_anonymous ? next_anonymous_block_id_++ : 0;

	while (!at_end() && !at(TokenKind::right_brace)) {
		// Optional access qualifier — contextual, so a binding type may still
		// be named `readonly`; the qualifier only applies when a type follows.
		AccessKind access = AccessKind::read_write;
		if (consume(TokenKind::kw_Readonly)) {
			access = AccessKind::read_only;
		} else if (consume(TokenKind::kw_Writeonly)) {
			access = AccessKind::write_only;
		}

		ParsedType type = parse_type();
		if (type.empty()) {
			diagnose_here("expected uniform binding type");
			skip_to_declaration_boundary();
			continue;
		}
		if (reject_non_parameter_type_qualifiers(type, "uniform binding")) {
			skip_to_declaration_boundary();
			continue;
		}
		if (!at(TokenKind::identifier)) {
			diagnose_here("expected uniform binding name");
			skip_to_declaration_boundary();
			continue;
		}
		std::string name{ peek().text };
		++cursor_;
		if (!expect(TokenKind::semicolon, "expected ';' after uniform binding")) {
			skip_to_declaration_boundary();
			continue;
		}

		if (unit_) {
			UniformBinding binding{
				.scope_name = decl.name,
				.name = std::move(name),
				.access = access,
				.is_anonymous = is_anonymous,
				.anonymous_block_id = anonymous_block_id,
			};
			if (type.has_body && type.is_anonymous) {
				// Anonymous inline struct payload keeps its fields inline.
				binding.type = "struct";
				binding.inline_fields = std::move(type.fields);
			} else {
				binding.type = std::move(type.spelling);
			}
			unit_->uniforms.push_back(std::move(binding));
		}
	}
}

void Parser::parse_layout() {
	const Token start = peek();
	(void)consume(TokenKind::kw_Layout);

	LayoutDecl layout;
	layout.span = start.span;

	if (!at(TokenKind::identifier)) {
		diagnose_here("expected identifier after 'layout'");
		skip_to_declaration_boundary();
		return;
	}
	layout.path.emplace_back(peek().text);
	++cursor_;
	while (consume(TokenKind::colon_colon)) {
		if (!at(TokenKind::identifier)) {
			diagnose_here("expected identifier after '::' in layout path");
			skip_to_declaration_boundary();
			return;
		}
		layout.path.emplace_back(peek().text);
		++cursor_;
	}

	if (!expect(TokenKind::colon, "expected ':' after layout binding path")) {
		skip_to_declaration_boundary();
		return;
	}

	// Optional layout rule (std140/std430/scalar) — contextual words looked up
	// in the shared spelling table.
	if (at(TokenKind::identifier)) {
		if (const auto rule = parse_layout_rule(peek().text); rule != LayoutRule::unset) {
			layout.rule = rule;
			++cursor_;
		}
	}

	// The payload is a type; inline structs come through the type rule.
	ParsedType type = parse_type();
	if (type.empty()) {
		diagnose_here("expected layout type");
		skip_to_declaration_boundary();
		return;
	}
	if (reject_non_parameter_type_qualifiers(type, "layout declaration")) {
		skip_to_declaration_boundary();
		return;
	}
	if (type.has_body && type.is_anonymous) {
		layout.is_inline_struct = true;
		layout.inline_fields = std::move(type.fields);
	} else {
		layout.type_spelling = std::move(type.spelling);
	}

	if (!expect(TokenKind::semicolon, "expected ';' after layout declaration")) {
		skip_to_declaration_boundary();
	}

	if (unit_) unit_->layouts.push_back(std::move(layout));
}

void Parser::parse_using(bool exported) {
	(void)consume(TokenKind::kw_Using);

	UsingImport::Kind import_kind = UsingImport::Kind::symbol;
	if (consume(TokenKind::kw_Namespace)) {
		import_kind = UsingImport::Kind::namespace_scope;
	}

	if (!at(TokenKind::identifier)) {
		diagnose_here("expected name after 'using'");
		skip_to_declaration_boundary();
		return;
	}
	std::vector<std::string> path;
	path.emplace_back(peek().text);
	++cursor_;
	while (consume(TokenKind::colon_colon)) {
		if (!at(TokenKind::identifier)) {
			diagnose_here("expected identifier after '::' in using declaration");
			skip_to_declaration_boundary();
			return;
		}
		path.emplace_back(peek().text);
		++cursor_;
	}

	if (!consume(TokenKind::equal)) {
		if (!expect(TokenKind::semicolon, "expected ';' after using declaration")) {
			skip_to_declaration_boundary();
			return;
		}
		if (unit_) {
			std::string imported_name = path.empty() ? std::string{} : path.back();
			unit_->using_imports.push_back(UsingImport{
				.kind = import_kind,
				.path = std::move(path),
				.imported_name = std::move(imported_name),
				.exported = exported,
			});
		}
		return;
	}

	if (import_kind != UsingImport::Kind::symbol) {
		diagnose_here("using alias cannot use an import kind");
		skip_to_declaration_boundary();
		return;
	}
	if (path.size() != 1) {
		diagnose_here("using alias name must be unqualified");
		skip_to_declaration_boundary();
		return;
	}
	std::string alias_name = std::move(path.front());

	// The alias base is a type; `using X = struct { ... };` works through the
	// same rule as everything else.
	ParsedType base = parse_type();
	if (base.empty()) {
		diagnose_here("expected base type in using declaration");
		skip_to_declaration_boundary();
		return;
	}
	if (reject_non_parameter_type_qualifiers(base, "using alias")) {
		skip_to_declaration_boundary();
		return;
	}
	const std::string resolved_base = resolve_alias(base.spelling);

	if (unit_) unit_->type_aliases.push_back(TypeAlias{ .name = alias_name, .base = resolved_base });

	if (consume(TokenKind::colon)) {
		diagnose_here("using aliases cannot declare a stage boundary");
		skip_to_declaration_boundary();
		return;
	}
	if (!expect(TokenKind::semicolon, "expected ';' after using declaration")) {
		skip_to_declaration_boundary();
	}
}

// ---------------------------------------------------------------------------
// Function signature / parameters / return boundary
// ---------------------------------------------------------------------------

void Parser::parse_function_signature(Decl& decl) {
	if (!expect(TokenKind::left_paren, "expected '(' after function name")) {
		return;
	}
	parse_parameter_list(decl.parameters);
	if (!expect(TokenKind::right_paren, "expected ')' after function parameters")) {
		return;
	}

	if (consume(TokenKind::arrow)) {
		ParsedType ret = parse_type();
		if (ret.empty()) {
			diagnose_here("expected return type after '->'");
			decl.return_type = "void";
			return;
		}
		if (reject_non_parameter_type_qualifiers(ret, "function return type")) {
			decl.return_type = "void";
			return;
		}
		decl.return_type = resolve_alias(ret.spelling);
		// The boundary spec belongs to the return type: `-> T : field(tag),…`
		maybe_parse_return_boundary(decl.return_type);
	} else {
		decl.return_type = "void";
	}
}

void Parser::parse_parameter_list(std::vector<ParameterDecl>& out) {
	if (at(TokenKind::right_paren)) {
		return;
	}
	while (!at_end()) {
		ParsedType type = parse_type();
		if (type.empty()) {
			diagnose_here("expected parameter type");
			while (!at_end() && !at(TokenKind::comma) && !at(TokenKind::right_paren)) {
				++cursor_;
			}
			if (!consume(TokenKind::comma)) {
				break;
			}
			continue;
		}
		if (type.has_pointer) {
			while (!at_end() && !at(TokenKind::comma) && !at(TokenKind::right_paren)) {
				++cursor_;
			}
			if (!consume(TokenKind::comma)) {
				break;
			}
			continue;
		}

		std::string param_name;
		if (at(TokenKind::identifier)) {
			param_name = std::string(peek().text);
			++cursor_;
		}

		out.push_back(ParameterDecl{
			.type = resolve_alias(type.spelling),
			.name = std::move(param_name),
			.is_const = type.is_const,
			.is_reference = type.is_reference,
		});

		if (!consume(TokenKind::comma)) {
			break;
		}
	}
}

// Return boundary: `field(tag, ...) (, field(tag, ...))*`
// Tags are authored identifiers. Semantic analysis owns their contextual
// meaning for a given boundary.
void Parser::parse_return_boundary(std::string base_type) {
	auto tag_text = [&]() -> std::string_view {
		// Tags are plain identifiers.
		return at(TokenKind::identifier) ? peek().text : std::string_view{};
	};

	StageInterface interface;
	interface.role = StageRole::varying;
	interface.type_name = base_type;
	bool first = true;
	while (!at_end() && !at(TokenKind::left_brace) && !at(TokenKind::semicolon)) {
		if (!first) {
			if (!expect(TokenKind::comma, "expected ',' between boundary entries")) break;
		}
		first = false;

		if (!at(TokenKind::identifier)) {
			diagnose_here("expected field name in return boundary");
			break;
		}
		StageIOField field;
		field.name = std::string(peek().text);
		++cursor_;

		if (!expect(TokenKind::left_paren, "expected '(' after field name in return boundary")) break;

		bool first_tag = true;
		while (!at_end() && !at(TokenKind::right_paren)) {
			if (!first_tag) {
				if (!expect(TokenKind::comma, "expected ',' between tags")) break;
			}
			first_tag = false;
			const auto text = tag_text();
			if (text.empty()) {
				diagnose_here("expected pipeline tag identifier");
				break;
			}
			field.tags.emplace_back(text);
			++cursor_;
		}
		if (!expect(TokenKind::right_paren, "expected ')' after tag list")) break;
		interface.fields.push_back(std::move(field));
	}

	if (unit_ && !interface.type_name.empty()) {
		// One boundary spec per base type; later duplicates ignored.
		for (const auto& existing : unit_->stage_interfaces) {
			if (existing.role == StageRole::varying && existing.type_name == interface.type_name) return;
		}
		unit_->stage_interfaces.push_back(interface);
	}
}

void Parser::maybe_parse_return_boundary(std::string_view base_type) {
	if (!consume(TokenKind::colon)) {
		return;
	}
	parse_return_boundary(std::string(base_type));
}

// ---------------------------------------------------------------------------
// Names / aliases
// ---------------------------------------------------------------------------

std::string Parser::parse_scoped_name() {
	if (!at(TokenKind::identifier)) return {};
	std::string name{ peek().text };
	++cursor_;
	while (consume(TokenKind::colon_colon)) {
		const bool destructor = consume(TokenKind::tilde);
		if (!at(TokenKind::identifier)) {
			diagnose_here(destructor ? "expected destructor name after '~'" : "expected identifier after '::'");
			return name;
		}
		name += destructor ? "::~" : "::";
		name.append(peek().text);
		++cursor_;
		if (destructor) return name;
	}
	return name;
}

std::string Parser::resolve_alias(std::string_view name) const {
	if (!unit_) return std::string(name);
	std::string current{ name };
	for (int hops = 0; hops < 16; ++hops) {
		bool advanced = false;
		for (const auto& alias : unit_->type_aliases) {
			if (alias.name == current) { current = alias.base; advanced = true; break; }
		}
		if (!advanced) break;
	}
	return current;
}

// ---------------------------------------------------------------------------
// Statements
// ---------------------------------------------------------------------------

Decl::BodyStatement Parser::parse_statement() {
	switch (peek().kind) {
	case TokenKind::left_brace: return parse_block_statement();
	case TokenKind::kw_If:      return parse_if_statement();
	case TokenKind::kw_While:   return parse_while_statement();
	case TokenKind::kw_Do:      return parse_do_statement();
	case TokenKind::kw_For:     return parse_for_statement();
	case TokenKind::kw_Return:  return parse_return_statement();
	default: break;
	}

	Decl::BodyStatement local{};
	if (try_parse_local_declaration(local)) return local;
	return parse_expression_or_assignment_statement();
}

Decl::BodyStatement Parser::parse_block_statement() {
	Decl::BodyStatement stmt{};
	stmt.kind = Decl::BodyStatementKind::block;
	stmt.span = peek().span;
	if (!expect(TokenKind::left_brace, "expected '{'")) return stmt;
	while (!at_end() && !at(TokenKind::right_brace)) {
		const auto before = cursor_;
		auto child = parse_statement();
		if (child.kind != Decl::BodyStatementKind::unknown) stmt.children.push_back(std::move(child));
		if (cursor_ == before) ++cursor_; // forward-progress guard
	}
	(void)consume(TokenKind::right_brace);
	return stmt;
}

Decl::BodyStatement Parser::parse_if_statement() {
	Decl::BodyStatement stmt{};
	stmt.kind = Decl::BodyStatementKind::if_stmt;
	stmt.span = peek().span;
	(void)consume(TokenKind::kw_If);
	if (!expect(TokenKind::left_paren, "expected '(' after 'if'")) { skip_to_statement_boundary(); return stmt; }
	const auto cond_begin = cursor_;
	stmt.expr = parse_expression();
	stmt.condition = source_between(cond_begin, cursor_);
	if (!expect(TokenKind::right_paren, "expected ')' after if condition")) skip_to_statement_boundary();

	auto then_stmt = parse_statement();
	if (then_stmt.kind == Decl::BodyStatementKind::block) {
		stmt.children = std::move(then_stmt.children);
	} else if (then_stmt.kind != Decl::BodyStatementKind::unknown) {
		stmt.children.push_back(std::move(then_stmt));
	}

	if (consume(TokenKind::kw_Else)) {
		auto else_stmt = parse_statement();
		if (else_stmt.kind == Decl::BodyStatementKind::block) {
			stmt.else_children = std::move(else_stmt.children);
		} else if (else_stmt.kind != Decl::BodyStatementKind::unknown) {
			stmt.else_children.push_back(std::move(else_stmt));
		}
	}
	return stmt;
}

Decl::BodyStatement Parser::parse_while_statement() {
	Decl::BodyStatement stmt{};
	stmt.kind = Decl::BodyStatementKind::while_stmt;
	stmt.span = peek().span;
	(void)consume(TokenKind::kw_While);
	if (!expect(TokenKind::left_paren, "expected '(' after 'while'")) { skip_to_statement_boundary(); return stmt; }
	const auto cond_begin = cursor_;
	stmt.expr = parse_expression();
	stmt.condition = source_between(cond_begin, cursor_);
	if (!expect(TokenKind::right_paren, "expected ')' after while condition")) skip_to_statement_boundary();
	auto body = parse_statement();
	if (body.kind == Decl::BodyStatementKind::block) stmt.children = std::move(body.children);
	else if (body.kind != Decl::BodyStatementKind::unknown) stmt.children.push_back(std::move(body));
	return stmt;
}

Decl::BodyStatement Parser::parse_do_statement() {
	Decl::BodyStatement stmt{};
	stmt.kind = Decl::BodyStatementKind::do_stmt;
	stmt.span = peek().span;
	(void)consume(TokenKind::kw_Do);
	auto body = parse_statement();
	if (body.kind == Decl::BodyStatementKind::block) stmt.children = std::move(body.children);
	else if (body.kind != Decl::BodyStatementKind::unknown) stmt.children.push_back(std::move(body));
	if (!expect(TokenKind::kw_While, "expected 'while' after do-block")) { skip_to_statement_boundary(); return stmt; }
	if (!expect(TokenKind::left_paren, "expected '(' after 'while'")) { skip_to_statement_boundary(); return stmt; }
	const auto cond_begin = cursor_;
	stmt.expr = parse_expression();
	stmt.condition = source_between(cond_begin, cursor_);
	if (!expect(TokenKind::right_paren, "expected ')' after do-while condition")) skip_to_statement_boundary();
	if (!expect(TokenKind::semicolon, "expected ';' after do-while")) skip_to_statement_boundary();
	return stmt;
}

Decl::BodyStatement Parser::parse_for_statement() {
	Decl::BodyStatement stmt{};
	stmt.kind = Decl::BodyStatementKind::for_stmt;
	stmt.span = peek().span;
	(void)consume(TokenKind::kw_For);
	if (!expect(TokenKind::left_paren, "expected '(' after 'for'")) { skip_to_statement_boundary(); return stmt; }

	// init clause
	if (!at(TokenKind::semicolon)) {
		Decl::BodyStatement init{};
		if (try_parse_local_declaration(init)) {
			stmt.loop_init = init.type_name + " " + init.name;
			if (!init.initializer.empty()) stmt.loop_init += " = " + init.initializer;
		} else {
			const auto init_begin = cursor_;
			(void)parse_expression();
			if (consume(TokenKind::equal)) (void)parse_expression();
			stmt.loop_init = source_between(init_begin, cursor_);
			if (!expect(TokenKind::semicolon, "expected ';' after for-init")) { skip_to_statement_boundary(); return stmt; }
		}
	} else {
		(void)consume(TokenKind::semicolon);
	}

	// condition
	if (!at(TokenKind::semicolon)) {
		const auto cond_begin = cursor_;
		stmt.expr = parse_expression();
		stmt.condition = source_between(cond_begin, cursor_);
	}
	if (!expect(TokenKind::semicolon, "expected ';' after for-condition")) { skip_to_statement_boundary(); return stmt; }

	// continue clause
	if (!at(TokenKind::right_paren)) {
		const auto cont_begin = cursor_;
		(void)parse_expression();
		if (consume(TokenKind::equal)) (void)parse_expression();
		stmt.loop_continue = source_between(cont_begin, cursor_);
	}
	if (!expect(TokenKind::right_paren, "expected ')' after for-clauses")) { skip_to_statement_boundary(); return stmt; }

	auto body = parse_statement();
	if (body.kind == Decl::BodyStatementKind::block) stmt.children = std::move(body.children);
	else if (body.kind != Decl::BodyStatementKind::unknown) stmt.children.push_back(std::move(body));
	return stmt;
}

Decl::BodyStatement Parser::parse_return_statement() {
	Decl::BodyStatement stmt{};
	stmt.kind = Decl::BodyStatementKind::return_stmt;
	stmt.span = peek().span;
	(void)consume(TokenKind::kw_Return);
	if (!at(TokenKind::semicolon)) {
		const auto begin = cursor_;
		stmt.expr = parse_expression();
		stmt.rhs = source_between(begin, cursor_);
	}
	if (!expect(TokenKind::semicolon, "expected ';' after return")) skip_to_statement_boundary();
	return stmt;
}

bool Parser::try_parse_local_declaration(Decl::BodyStatement& out) {
	const auto save = cursor_;
	ParsedType type = parse_type();
	if (type.empty() || !at(TokenKind::identifier)) {
		// A committed struct body can't be silently rewound; anything else is
		// a clean restore so the caller can try an expression statement.
		if (type.has_body) {
			diagnose_here("expected variable name after struct type");
			return false;
		}
		cursor_ = save;
		return false;
	}
	if (reject_non_parameter_type_qualifiers(type, "local declaration")) {
		cursor_ = save;
		return false;
	}
	std::string name{ peek().text };
	++cursor_;

	if (consume(TokenKind::equal)) {
		const auto rhs_begin = cursor_;
		auto rhs_expr = parse_expression();
		const auto rhs_end = cursor_;
		if (!consume(TokenKind::semicolon)) {
			cursor_ = save;
			return false;
		}
		out = {};
		out.kind = Decl::BodyStatementKind::declaration;
		out.type_name = std::move(type.spelling);
		out.name = std::move(name);
		out.expr = std::move(rhs_expr);
		out.initializer = source_between(rhs_begin, rhs_end);
		out.span = tokens_[save].span;
		return true;
	}
	if (consume(TokenKind::semicolon)) {
		out = {};
		out.kind = Decl::BodyStatementKind::declaration;
		out.type_name = std::move(type.spelling);
		out.name = std::move(name);
		out.span = tokens_[save].span;
		return true;
	}
	cursor_ = save;
	return false;
}

Decl::BodyStatement Parser::parse_expression_or_assignment_statement() {
	Decl::BodyStatement stmt{};
	stmt.span = peek().span;

	// Parse the LHS at "no-assignment" precedence so a statement-level `=`
	// is recognised as an assignment statement rather than swallowed into a
	// binary expression tree.
	const auto lhs_begin = cursor_;
	auto lhs = parse_logical_or();
	const auto lhs_end = cursor_;

	if (consume(TokenKind::equal)) {
		const auto rhs_begin = cursor_;
		auto rhs = parse_expression();
		const auto rhs_end = cursor_;
		stmt.kind = Decl::BodyStatementKind::assignment;
		stmt.lhs = source_between(lhs_begin, lhs_end);
		stmt.rhs = source_between(rhs_begin, rhs_end);
		stmt.expr = std::move(rhs);
	} else {
		stmt.kind = Decl::BodyStatementKind::expression;
		stmt.expr = std::move(lhs);
	}

	if (!expect(TokenKind::semicolon, "expected ';' after statement")) skip_to_statement_boundary();
	return stmt;
}

// ---------------------------------------------------------------------------
// Expressions
// ---------------------------------------------------------------------------

Decl::Expr Parser::parse_expression() { return parse_assignment(); }

Decl::Expr Parser::parse_assignment() {
	auto lhs = parse_logical_or();
	if (at(TokenKind::equal)) {
		++cursor_;
		auto rhs = parse_assignment();
		return make_binary("=", std::move(lhs), std::move(rhs));
	}
	return lhs;
}

Decl::Expr Parser::parse_logical_or() {
	auto lhs = parse_logical_and();
	while (at(TokenKind::pipe_pipe)) { ++cursor_; auto rhs = parse_logical_and(); lhs = make_binary("||", std::move(lhs), std::move(rhs)); }
	return lhs;
}
Decl::Expr Parser::parse_logical_and() {
	auto lhs = parse_bitwise_or();
	while (at(TokenKind::amp_amp))   { ++cursor_; auto rhs = parse_bitwise_or(); lhs = make_binary("&&", std::move(lhs), std::move(rhs)); }
	return lhs;
}
Decl::Expr Parser::parse_bitwise_or() {
	auto lhs = parse_bitwise_xor();
	while (at(TokenKind::pipe))      { ++cursor_; auto rhs = parse_bitwise_xor(); lhs = make_binary("|", std::move(lhs), std::move(rhs)); }
	return lhs;
}
Decl::Expr Parser::parse_bitwise_xor() {
	auto lhs = parse_bitwise_and();
	while (at(TokenKind::caret))     { ++cursor_; auto rhs = parse_bitwise_and(); lhs = make_binary("^", std::move(lhs), std::move(rhs)); }
	return lhs;
}
Decl::Expr Parser::parse_bitwise_and() {
	auto lhs = parse_equality();
	while (at(TokenKind::amp))       { ++cursor_; auto rhs = parse_equality(); lhs = make_binary("&", std::move(lhs), std::move(rhs)); }
	return lhs;
}
Decl::Expr Parser::parse_equality() {
	auto lhs = parse_relational();
	while (at(TokenKind::equal_equal) || at(TokenKind::bang_equal)) {
		const std::string op = at(TokenKind::equal_equal) ? "==" : "!=";
		++cursor_;
		auto rhs = parse_relational();
		lhs = make_binary(op, std::move(lhs), std::move(rhs));
	}
	return lhs;
}
Decl::Expr Parser::parse_relational() {
	auto lhs = parse_additive();
	while (at(TokenKind::less) || at(TokenKind::less_equal) ||
		   at(TokenKind::greater) || at(TokenKind::greater_equal)) {
		std::string op;
		switch (peek().kind) {
		case TokenKind::less:          op = "<";  break;
		case TokenKind::less_equal:    op = "<="; break;
		case TokenKind::greater:       op = ">";  break;
		case TokenKind::greater_equal: op = ">="; break;
		default: break;
		}
		++cursor_;
		auto rhs = parse_additive();
		lhs = make_binary(std::move(op), std::move(lhs), std::move(rhs));
	}
	return lhs;
}
Decl::Expr Parser::parse_additive() {
	auto lhs = parse_multiplicative();
	while (at(TokenKind::plus) || at(TokenKind::minus)) {
		const std::string op = at(TokenKind::plus) ? "+" : "-";
		++cursor_;
		auto rhs = parse_multiplicative();
		lhs = make_binary(op, std::move(lhs), std::move(rhs));
	}
	return lhs;
}
Decl::Expr Parser::parse_multiplicative() {
	auto lhs = parse_unary();
	while (at(TokenKind::star) || at(TokenKind::slash) || at(TokenKind::percent)) {
		const std::string op = at(TokenKind::star) ? "*" : (at(TokenKind::slash) ? "/" : "%");
		++cursor_;
		auto rhs = parse_unary();
		lhs = make_binary(op, std::move(lhs), std::move(rhs));
	}
	return lhs;
}
Decl::Expr Parser::parse_unary() {
	if (at(TokenKind::plus) || at(TokenKind::minus) ||
		at(TokenKind::bang) || at(TokenKind::tilde)) {
		std::string op;
		switch (peek().kind) {
		case TokenKind::plus:  op = "+"; break;
		case TokenKind::minus: op = "-"; break;
		case TokenKind::bang:  op = "!"; break;
		case TokenKind::tilde: op = "~"; break;
		default: break;
		}
		++cursor_;
		auto child = parse_unary();
		return make_unary(std::move(op), std::move(child));
	}
	return parse_postfix();
}

Decl::Expr Parser::parse_postfix() {
	auto base = parse_primary();
	while (!at_end()) {
		if (at(TokenKind::dot) || at(TokenKind::colon_colon)) {
			const std::string op = at(TokenKind::dot) ? "." : "::";
			++cursor_;
			if (!at(TokenKind::identifier)) {
				diagnose_here("expected identifier after member operator");
				break;
			}
			Decl::Expr expr;
			expr.kind = Decl::Expr::Kind::member;
			expr.op = op;
			expr.text = std::string(peek().text);
			expr.children = { std::move(base) };
			++cursor_;
			base = std::move(expr);
			continue;
		}
		if (at(TokenKind::left_paren)) {
			++cursor_;
			Decl::Expr expr;
			expr.kind = Decl::Expr::Kind::call;
			expr.children.push_back(std::move(base));
			if (!at(TokenKind::right_paren)) {
				while (true) {
					expr.children.push_back(parse_expression());
					if (!consume(TokenKind::comma)) break;
				}
			}
			if (!expect(TokenKind::right_paren, "expected ')' after call arguments")) break;
			base = std::move(expr);
			continue;
		}
		if (at(TokenKind::left_bracket)) {
			++cursor_;
			auto index = parse_expression();
			if (!expect(TokenKind::right_bracket, "expected ']' after index")) break;
			base = make_binary("[]", std::move(base), std::move(index));
			continue;
		}
		break;
	}
	return base;
}

Decl::Expr Parser::parse_primary() {
	Decl::Expr expr;
	const Token tok = peek();
	switch (tok.kind) {
	case TokenKind::identifier:
		expr.kind = Decl::Expr::Kind::name;
		expr.text = std::string(tok.text);
		expr.span = tok.span;
		++cursor_;
		return expr;
	case TokenKind::integer_literal:
		expr.kind = Decl::Expr::Kind::literal_int;
		expr.text = std::string(tok.text);
		expr.span = tok.span;
		++cursor_;
		return expr;
	case TokenKind::float_literal:
		expr.kind = Decl::Expr::Kind::literal_float;
		expr.text = std::string(tok.text);
		expr.span = tok.span;
		++cursor_;
		return expr;
	case TokenKind::string_literal:
		expr.kind = Decl::Expr::Kind::name;
		expr.text = std::string(tok.text);
		expr.span = tok.span;
		++cursor_;
		return expr;
	case TokenKind::kw_True:
		expr.kind = Decl::Expr::Kind::literal_int;
		expr.text = "1";
		expr.span = tok.span;
		++cursor_;
		return expr;
	case TokenKind::kw_False:
		expr.kind = Decl::Expr::Kind::literal_int;
		expr.text = "0";
		expr.span = tok.span;
		++cursor_;
		return expr;
	case TokenKind::left_paren: {
		++cursor_;
		auto inner = parse_expression();
		(void)expect(TokenKind::right_paren, "expected ')' in expression");
		return inner;
	}
	default:
		diagnose_here("expected expression");
		expr.kind = Decl::Expr::Kind::unknown;
		return expr;
	}
}

std::string Parser::source_between(std::size_t begin_cursor, std::size_t end_cursor) const {
	if (end_cursor <= begin_cursor || begin_cursor >= tokens_.size()) return {};
	const std::size_t end = end_cursor > tokens_.size() ? tokens_.size() : end_cursor;
	const auto s = tokens_[begin_cursor].span.begin.offset;
	const auto e = tokens_[end - 1].span.begin.offset + tokens_[end - 1].span.length;
	return join_source(sources_.buffer(file_id_), s, e);
}

} // namespace rtsl
