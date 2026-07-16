#include "sema/sema.hpp"

#include "sema/stage_rules.hpp"

#include <array>
#include <cstddef>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace rtsl {

Sema::Sema(SourceManager& sources, DiagnosticEngine& diagnostics)
	: sources_(sources), diagnostics_(diagnostics) {}

namespace {

const StructDecl* find_struct(std::span<const StructDecl> structs, std::string_view name) {
	for (const auto& structure : structs) {
		if (structure.name == name) {
			return &structure;
		}
	}
	return nullptr;
}

std::string resolve_stage_attribute(std::span<const Attribute> attributes, std::string_view source_name, DiagnosticEngine& diagnostics) {
	std::string stage;
	for (const auto& attribute : attributes) {
		if (attribute.name != "stage") {
			diagnostics.report(DiagnosticCode::sema_unknown_name, DiagnosticSeverity::error, attribute.span.begin, source_name,
							   "unknown function attribute '@" + attribute.name + "'");
			continue;
		}
		if (attribute.value.empty()) {
			diagnostics.report(DiagnosticCode::sema_unknown_name, DiagnosticSeverity::error, attribute.span.begin, source_name,
							   "stage attribute requires ': <identifier>'");
			continue;
		}
		if (!stage.empty() && stage != attribute.value) {
			diagnostics.report(DiagnosticCode::sema_unknown_name, DiagnosticSeverity::error, attribute.span.begin, source_name,
							   "function has multiple stage attributes");
			continue;
		}
		if (!is_graphics_stage(attribute.value)) {
			diagnostics.report(DiagnosticCode::sema_invalid_stage, DiagnosticSeverity::error, attribute.span.begin, source_name,
							   "unsupported stage '" + attribute.value + "'; expected 'vertex' or 'fragment'");
			continue;
		}
		stage = attribute.value;
	}
	return stage;
}

// Contextual meaning of a return-boundary pipeline tag (smooth/flat/clip).
struct StageBoundaryMeaning {
	InterpolationKind interpolation = InterpolationKind::none;
	StageFieldPlacement placement = StageFieldPlacement::user;
	bool known = false;
};

struct StageBoundaryRule {
	std::string_view name;
	InterpolationKind interpolation = InterpolationKind::none;
	StageFieldPlacement placement = StageFieldPlacement::user;
};

constexpr std::array kStageBoundaryRules{
#define RTSL_STAGE_BOUNDARY_TAG(name, interpolation, placement) \
	StageBoundaryRule{ #name, InterpolationKind::interpolation, StageFieldPlacement::placement },
#include "sema/stage_boundary_tags.def"
};

StageBoundaryMeaning stage_boundary_meaning(std::string_view name) {
	for (const StageBoundaryRule& rule : kStageBoundaryRules) {
		if (rule.name == name) {
			return StageBoundaryMeaning{ rule.interpolation, rule.placement, true };
		}
	}
	return {};
}

// Resolve each authored pipeline tag to its interpolation/placement meaning,
// diagnosing unknown tags. Tags are the contextual vocabulary of the boundary;
// their meaning lives here in sema, not in the parser.
void resolve_stage_interface_tags(std::span<StageInterface> interfaces, std::string_view source_name, DiagnosticEngine& diagnostics) {
	for (auto& interface : interfaces) {
		for (auto& field : interface.fields) {
			for (const auto& tag : field.tags) {
				const StageBoundaryMeaning meaning = stage_boundary_meaning(tag);
				if (!meaning.known) {
					diagnostics.report(DiagnosticCode::sema_unknown_name, DiagnosticSeverity::error, {}, source_name,
									   "unknown pipeline tag '" + tag + "'");
					continue;
				}
				field.interpolation = meaning.interpolation;
				field.placement = meaning.placement;
			}
		}
	}
}

bool same_parameters(std::span<const ParameterDecl> a, std::span<const ParameterDecl> b) {
	if (a.size() != b.size()) {
		return false;
	}
	for (std::size_t i = 0; i < a.size(); ++i) {
		if (a[i].type != b[i].type ||
			a[i].is_const != b[i].is_const ||
			a[i].is_reference != b[i].is_reference) {
			return false;
		}
	}
	return true;
}

bool declares_member_function(const StructDecl& owner, std::string_view name, std::span<const ParameterDecl> parameters, std::string_view return_type) {
	for (const auto& member : owner.member_functions) {
		if (member.name == name &&
			member.return_type == return_type &&
			same_parameters(member.parameters, parameters)) {
			return true;
		}
	}
	return false;
}

u64 fnv1a_append(u64 hash, std::string_view text) {
	for (const unsigned char ch : text) {
		hash ^= ch;
		hash *= 1099511628211ull;
	}
	return hash;
}

u64 export_interface_hash(const Decl& decl) {
	u64 hash = 14695981039346656037ull;
	hash = fnv1a_append(hash, decl.name);
	hash = fnv1a_append(hash, "|");
	hash = fnv1a_append(hash, decl.return_type);
	for (const auto& parameter : decl.parameters) {
		hash = fnv1a_append(hash, "|");
		if (parameter.is_const) {
			hash = fnv1a_append(hash, "const ");
		}
		hash = fnv1a_append(hash, parameter.type);
		if (parameter.is_reference) {
			hash = fnv1a_append(hash, "&");
		}
	}
	return hash;
}

// Every type spelling the module can legally name: builtin value types, the
// canonical resource types, user structs, aliases, and symbols pulled in from
// imported modules. Built once per analysis and queried per reference site.
std::unordered_set<std::string_view> collect_known_types(const SemanticModule& module) {
	std::unordered_set<std::string_view> known;
	known.insert(""); // empty return type == void
#define RTSL_SCALAR_TYPE(spelling, semantic_kind, width, ir_op) known.insert(#spelling);
#define RTSL_VECTOR_TYPE(spelling, element, components) known.insert(#spelling);
#define RTSL_MATRIX_TYPE(spelling, column_type, columns) known.insert(#spelling);
#include "frontend/value_types.def"
#define RTSL_RESOURCE_TYPE(spelling, binding_kind) known.insert(#spelling);
#include "frontend/resource_types.def"
	for (const auto& decl : module.structs) {
		known.insert(decl.name);
	}
	for (const auto& alias : module.type_aliases) {
		known.insert(alias.name);
	}
	// Imported symbols may name struct types this module uses; accept them all
	// rather than risk a false "unknown type" on a legitimate cross-module type.
	for (const auto& exported : module.imported_exports) {
		known.insert(exported.name);
	}
	return known;
}

// A type name resolves if it is empty (void), a qualified/namespaced name we do
// not attempt to resolve here, or a member of the known set.
bool type_resolves(std::string_view name, const std::unordered_set<std::string_view>& known) {
	if (name.empty() || name.find("::") != std::string_view::npos) {
		return true;
	}
	return known.contains(name);
}

void check_type_reference(std::string_view name, SourceLocation location, std::string_view source_name,
	const std::unordered_set<std::string_view>& known, DiagnosticEngine& diagnostics) {
	if (type_resolves(name, known)) {
		return;
	}
	diagnostics.report(DiagnosticCode::sema_unknown_type, DiagnosticSeverity::error, location, source_name,
					   "unknown type '" + std::string(name) + "'");
}

// ----------------------------------------------------------------------------
// Expression type inference.
//
// A deliberately conservative pass: it infers a type spelling for the
// expression forms it fully understands and returns "" (unknown) for anything
// it can't type with confidence. Downstream checks only fire when both sides of
// a comparison are concretely known, so an inference gap becomes a missed
// diagnostic, never a false positive on a valid shader.
// ----------------------------------------------------------------------------

bool is_scalar_type(std::string_view type) {
	return type == "f32" || type == "i32" || type == "u32" || type == "bool";
}

bool is_vector_type(std::string_view type) {
	return type.starts_with("vec") || type.starts_with("ivec") || type.starts_with("uvec");
}

// Scalar element spelling of a vector type ("vec4" -> "f32", "uvec2" -> "u32").
std::string_view vector_element(std::string_view type) {
	if (type.starts_with("uvec")) return "u32";
	if (type.starts_with("ivec")) return "i32";
	if (type.starts_with("vec")) return "f32";
	return {};
}

// Reconstruct a vector spelling from an element scalar and component count, e.g.
// ("f32", 3) -> "vec3". Returns "" for shapes that have no spelling.
std::string vector_spelling(std::string_view element, std::size_t components) {
	if (components < 2 || components > 4) return {};
	const char n = static_cast<char>('0' + components);
	if (element == "f32") return std::string("vec") + n;
	if (element == "i32") return std::string("ivec") + n;
	if (element == "u32") return std::string("uvec") + n;
	return {};
}

// A run of vector swizzle components (x/y/z/w, r/g/b/a, s/t/p/q) selecting 1..4
// lanes. One component yields the element scalar; several yield a vector.
bool is_swizzle(std::string_view name) {
	if (name.empty() || name.size() > 4) return false;
	for (char c : name) {
		switch (c) {
		case 'x': case 'y': case 'z': case 'w':
		case 'r': case 'g': case 'b': case 'a':
		case 's': case 't': case 'p': case 'q':
			break;
		default:
			return false;
		}
	}
	return true;
}

std::string_view struct_field_type(std::span<const StructDecl> structs, std::string_view struct_name, std::string_view field) {
	for (const auto& decl : structs) {
		if (decl.name != struct_name) continue;
		for (const auto& member : decl.fields) {
			if (member.name == field) return member.type;
		}
	}
	return {};
}

bool is_struct_type(std::span<const StructDecl> structs, std::string_view struct_name) {
	return find_struct(structs, struct_name) != nullptr;
}

struct StdlibFunction {
	std::string_view name;
	std::string_view return_type;
	u32 parameter_count = 0;
};

constexpr StdlibFunction kStdlibFunctions[] = {
#define RTSL_STDLIB_FN(name, return_type, parameter_count) StdlibFunction{ #name, #return_type, parameter_count },
#include "sema/stdlib.def"
};

const StdlibFunction* find_stdlib_function(std::string_view name, std::size_t argument_count) {
	for (const auto& fn : kStdlibFunctions) {
		if (fn.name == name && fn.parameter_count == argument_count) {
			return &fn;
		}
	}
	return nullptr;
}

std::string qualified_uniform_name(const UniformBinding& uniform) {
	if (!uniform.is_anonymous && !uniform.scope_name.empty()) {
		return uniform.scope_name + "::" + uniform.name;
	}
	return uniform.name;
}

std::string qualified_layout_name(const LayoutDecl& layout) {
	std::string name;
	for (std::size_t i = 0; i < layout.path.size(); ++i) {
		if (i > 0) {
			name += "::";
		}
		name += layout.path[i];
	}
	return name;
}

bool is_buffer_resource_type(std::string_view type) {
	return type == "UniformBuffer" || type == "StorageBuffer";
}

bool is_sampled_resource_type(std::string_view type) {
	return type == "Sampler2D" || type == "Sampler3D" || type == "SamplerCube" ||
		   type == "Sampler2DArray" || type == "Image2D" || type == "Image3D";
}

bool is_coordinate_type(std::string_view type) {
	return type == "f32" || type == "vec2" || type == "vec3" || type == "vec4";
}

std::string uniform_value_type(std::string_view qualified_name, const SemanticModule& module) {
	for (const auto& uniform : module.uniforms) {
		if (qualified_uniform_name(uniform) != qualified_name) {
			continue;
		}
		if (!is_buffer_resource_type(uniform.type)) {
			return uniform.type;
		}
		for (const auto& layout : module.layouts) {
			if (qualified_layout_name(layout) != qualified_name) {
				continue;
			}
			if (layout.is_inline_struct) {
				return "struct";
			}
			return layout.type_spelling;
		}
		return {};
	}
	return {};
}

std::string imported_uniform_name(std::string_view name, const SemanticModule& module) {
	for (const auto& use : module.using_imports) {
		if (use.imported_name != name) {
			continue;
		}
		std::string qualified;
		for (std::size_t i = 0; i < use.path.size(); ++i) {
			if (i > 0) {
				qualified += "::";
			}
			qualified += use.path[i];
		}
		if (!uniform_value_type(qualified, module).empty()) {
			return qualified;
		}
	}
	for (const auto& use : module.using_imports) {
		if (use.kind != UsingImport::Kind::namespace_scope) {
			continue;
		}
		std::string scope;
		for (std::size_t i = 0; i < use.path.size(); ++i) {
			if (i > 0) {
				scope += "::";
			}
			scope += use.path[i];
		}
		const std::string qualified = scope + "::" + std::string(name);
		if (!uniform_value_type(qualified, module).empty()) {
			return qualified;
		}
	}
	return {};
}

using LocalEnv = std::unordered_map<std::string_view, std::string_view>;

bool types_compatible(std::string_view target, std::string_view value);
const SemanticSymbol* select_call_target(std::span<const SemanticSymbol> symbols, std::string_view name,
	std::span<const std::string> argument_types);

std::string infer_expr_type(const Decl::Expr& expr, const LocalEnv& locals, const SemanticModule& module,
	const std::unordered_set<std::string_view>& known) {
	switch (expr.kind) {
	case Decl::Expr::Kind::literal_int:
		return "i32";
	case Decl::Expr::Kind::literal_float:
		return "f32";
	case Decl::Expr::Kind::literal_bool:
		return "bool";
	case Decl::Expr::Kind::name:
		if (const auto it = locals.find(expr.text); it != locals.end()) {
			return std::string(it->second);
		}
		if (std::string type = uniform_value_type(expr.text, module); !type.empty()) {
			return type;
		}
		if (std::string qualified = imported_uniform_name(expr.text, module); !qualified.empty()) {
			return uniform_value_type(qualified, module);
		}
		return {};
	case Decl::Expr::Kind::unary:
		return expr.children.empty() ? std::string{} : infer_expr_type(expr.children.front(), locals, module, known);
	case Decl::Expr::Kind::member: {
		if (expr.children.empty()) {
			return {};
		}
		if (expr.op == "::" && expr.children.front().kind == Decl::Expr::Kind::name) {
			const std::string qualified = expr.children.front().text + "::" + expr.text;
			return uniform_value_type(qualified, module);
		}
		if (expr.op == "::") {
			return {};
		}
		const std::string base = infer_expr_type(expr.children.front(), locals, module, known);
		if (base.empty()) {
			return {};
		}
		if (const std::string_view field = struct_field_type(module.structs, base, expr.text); !field.empty()) {
			return std::string(field);
		}
		if (is_vector_type(base) && is_swizzle(expr.text)) {
			const std::string_view element = vector_element(base);
			return expr.text.size() == 1 ? std::string(element) : vector_spelling(element, expr.text.size());
		}
		return {};
	}
	case Decl::Expr::Kind::call: {
		if (expr.children.empty()) {
			return {};
		}
		const std::string_view callee = expr.children.front().text;
		// Constructor: calling a type name yields that type.
		if (known.contains(callee) && !is_scalar_type(callee)) {
			// A struct/vector/matrix constructor produces its named type. (Bare
			// scalar "constructors" like f32(x) are casts; handled as unknown.)
			if (!callee.empty()) return std::string(callee);
		}
		if (const StdlibFunction* fn = find_stdlib_function(callee, expr.children.size() - 1)) {
			return std::string(fn->return_type);
		}
		std::vector<std::string> argument_types;
		argument_types.reserve(expr.children.size() - 1);
		for (std::size_t i = 1; i < expr.children.size(); ++i) {
			argument_types.push_back(infer_expr_type(expr.children[i], locals, module, known));
		}
		if (const SemanticSymbol* target = select_call_target(module.symbols, callee, argument_types)) {
			return target->return_type;
		}
		return {};
	}
	case Decl::Expr::Kind::binary: {
		if (expr.op == "=" || expr.children.size() != 2) {
			return {};
		}
		// Comparison and logical operators yield a boolean regardless of operand
		// type; this is what makes `if (x < 0.5)` type as a bool condition.
		if (expr.op == "==" || expr.op == "!=" || expr.op == "<" || expr.op == "<=" ||
			expr.op == ">" || expr.op == ">=" || expr.op == "&&" || expr.op == "||") {
			return "bool";
		}
		const std::string lhs = infer_expr_type(expr.children[0], locals, module, known);
		const std::string rhs = infer_expr_type(expr.children[1], locals, module, known);
		if (!lhs.empty() && lhs == rhs) {
			return lhs;
		}
		// scalar * vector / vector * scalar keeps the vector shape.
		if (is_vector_type(lhs) && is_scalar_type(rhs)) return lhs;
		if (is_scalar_type(lhs) && is_vector_type(rhs)) return rhs;
		return {};
	}
	case Decl::Expr::Kind::unknown:
		break;
	}
	return {};
}

// Two type spellings are return/assignment compatible when they are identical,
// or both scalars (int/float/uint literals coerce freely enough that flagging
// scalar-to-scalar would produce false positives on valid code).
bool types_compatible(std::string_view target, std::string_view value) {
	if (target.empty() || value.empty()) return true;
	if (target == value) return true;
	return is_scalar_type(target) && is_scalar_type(value);
}

// Collect a flat name -> type environment for a function body: parameters plus
// every local declaration. Flat (function-wide) scoping is enough for the v0.1
// surface and never rejects a valid program.
void collect_locals(const Decl::BodyStatement& statement, LocalEnv& locals) {
	if (statement.kind == Decl::BodyStatementKind::declaration && !statement.name.empty()) {
		locals.emplace(statement.name, statement.type_name);
	}
	for (const auto& child : statement.children) {
		collect_locals(child, locals);
	}
	for (const auto& child : statement.else_children) {
		collect_locals(child, locals);
	}
}

std::vector<const SemanticSymbol*> function_candidates(std::span<const SemanticSymbol> symbols, std::string_view name) {
	std::vector<const SemanticSymbol*> candidates;
	for (const auto& symbol : symbols) {
		if (symbol.kind != DeclKind::function || symbol.name != name) {
			continue;
		}
		candidates.push_back(&symbol);
	}
	return candidates;
}

const SemanticSymbol* select_call_target(std::span<const SemanticSymbol> symbols, std::string_view name,
	std::span<const std::string> argument_types) {
	const auto candidates = function_candidates(symbols, name);
	const SemanticSymbol* selected = nullptr;
	for (const auto* candidate : candidates) {
		if (candidate->parameters.size() != argument_types.size()) {
			continue;
		}
		bool compatible = true;
		for (std::size_t i = 0; i < argument_types.size(); ++i) {
			if (!types_compatible(candidate->parameters[i].type, argument_types[i])) {
				compatible = false;
				break;
			}
		}
		if (!compatible) {
			continue;
		}
		if (selected) {
			return nullptr;
		}
		selected = candidate;
	}
	return selected;
}

// Check every call inside an expression tree. Only user functions are checked:
// type constructors and builtins take flexible argument lists that this layer
// does not model.
void check_calls(const Decl::Expr& expr, const LocalEnv& locals, const SemanticModule& module,
	const std::unordered_set<std::string_view>& known, DiagnosticEngine& diagnostics) {
	if (expr.kind == Decl::Expr::Kind::call && !expr.children.empty()) {
		const std::string_view callee = expr.children.front().text;
		const auto candidates = function_candidates(module.symbols, callee);
		if (!candidates.empty() || find_stdlib_function(callee, expr.children.size() - 1)) {
			if (find_stdlib_function(callee, expr.children.size() - 1)) {
				if (callee == "sample" && expr.children.size() == 3) {
					const std::string resource_type = infer_expr_type(expr.children[1], locals, module, known);
					const std::string coordinate_type = infer_expr_type(expr.children[2], locals, module, known);
					if (!resource_type.empty() && !is_sampled_resource_type(resource_type)) {
						diagnostics.report(DiagnosticCode::sema_argument_mismatch, DiagnosticSeverity::error, expr.span.begin, module.source_name,
										   std::format("sample first argument must be a sampled image resource, not '{}'", resource_type));
					}
					if (!coordinate_type.empty() && !is_coordinate_type(coordinate_type)) {
						diagnostics.report(DiagnosticCode::sema_argument_mismatch, DiagnosticSeverity::error, expr.span.begin, module.source_name,
										   std::format("sample coordinates must be f32 or a float vector, not '{}'", coordinate_type));
					}
				}
				for (const auto& child : expr.children) {
					check_calls(child, locals, module, known, diagnostics);
				}
				return;
			}
			std::vector<std::string> argument_types;
			argument_types.reserve(expr.children.size() - 1);
			for (std::size_t i = 1; i < expr.children.size(); ++i) {
				argument_types.push_back(infer_expr_type(expr.children[i], locals, module, known));
			}
			if (!select_call_target(module.symbols, callee, argument_types)) {
				diagnostics.report(DiagnosticCode::sema_argument_mismatch, DiagnosticSeverity::error, expr.span.begin, module.source_name,
								   std::format("no viable overload for call to '{}' with {} argument(s)", callee, argument_types.size()));
			}
		}
	}
	for (const auto& child : expr.children) {
		check_calls(child, locals, module, known, diagnostics);
	}
}

void check_member_access(const Decl::Expr& expr, const LocalEnv& locals, const SemanticModule& module,
	const std::unordered_set<std::string_view>& known, DiagnosticEngine& diagnostics) {
	if (expr.kind == Decl::Expr::Kind::member && expr.op == "." && !expr.children.empty()) {
		const std::string base = infer_expr_type(expr.children.front(), locals, module, known);
		if (!base.empty()) {
			const bool valid_struct_field = is_struct_type(module.structs, base) &&
										   !struct_field_type(module.structs, base, expr.text).empty();
			const bool valid_vector_swizzle = is_vector_type(base) && is_swizzle(expr.text);
			if (!valid_struct_field && !valid_vector_swizzle) {
				diagnostics.report(DiagnosticCode::sema_unknown_member, DiagnosticSeverity::error, expr.span.begin, module.source_name,
								   std::format("type '{}' has no member '{}'", base, expr.text));
			}
		}
	}
	for (const auto& child : expr.children) {
		check_member_access(child, locals, module, known, diagnostics);
	}
}

// One traversal of a function body, checking each statement's declared local
// types, the calls in its expressions, and — for `return` statements — the
// returned value against `return_type` (empty means "not a checkable type,
// skip"). `locals` and `known` are shared read-only for the whole body.
void check_body_statement(const Decl::BodyStatement& statement, std::string_view return_type, const LocalEnv& locals,
	const SemanticModule& module, const std::unordered_set<std::string_view>& known, DiagnosticEngine& diagnostics) {
	if (statement.kind == Decl::BodyStatementKind::declaration) {
		check_type_reference(statement.type_name, statement.span.begin, module.source_name, known, diagnostics);
	}
	check_calls(statement.expr, locals, module, known, diagnostics);
	check_member_access(statement.expr, locals, module, known, diagnostics);
	if (statement.kind == Decl::BodyStatementKind::return_stmt && statement.expr.kind != Decl::Expr::Kind::unknown) {
		const std::string value = infer_expr_type(statement.expr, locals, module, known);
		if (!types_compatible(return_type, value)) {
			diagnostics.report(DiagnosticCode::sema_type_mismatch, DiagnosticSeverity::error, statement.span.begin, module.source_name,
							   std::format("returned value of type '{}' is not compatible with the declared return type '{}'", value, return_type));
		}
	}
	for (const auto& child : statement.children) {
		check_body_statement(child, return_type, locals, module, known, diagnostics);
	}
	for (const auto& child : statement.else_children) {
		check_body_statement(child, return_type, locals, module, known, diagnostics);
	}
}

void check_function_body(const SemanticSymbol& symbol, const SemanticModule& module,
	const std::unordered_set<std::string_view>& known, DiagnosticEngine& diagnostics) {
	LocalEnv locals;
	for (const auto& parameter : symbol.parameters) {
		locals.emplace(parameter.name, parameter.type);
	}
	for (const auto& statement : symbol.body_statements) {
		collect_locals(statement, locals);
	}
	// Return typing only runs against a concrete, known return spelling;
	// otherwise an empty target tells check_body_statement to skip it.
	const bool checkable_return = !symbol.return_type.empty() && symbol.return_type != "void" &&
								  symbol.return_type.find("::") == std::string::npos && known.contains(symbol.return_type);
	const std::string_view return_type = checkable_return ? std::string_view(symbol.return_type) : std::string_view{};
	for (const auto& statement : symbol.body_statements) {
		check_body_statement(statement, return_type, locals, module, known, diagnostics);
	}
}

// Validate that every type spelling the module names actually resolves, then
// run expression-level typing over function bodies. Both layers share the same
// known-type environment.
void check_types(const SemanticModule& module, DiagnosticEngine& diagnostics) {
	const std::unordered_set<std::string_view> known = collect_known_types(module);
	const std::string_view source = module.source_name;

	for (const auto& decl : module.structs) {
		for (const auto& field : decl.fields) {
			check_type_reference(field.type, {}, source, known, diagnostics);
		}
		for (const auto& member : decl.member_functions) {
			check_type_reference(member.return_type, {}, source, known, diagnostics);
			for (const auto& parameter : member.parameters) {
				check_type_reference(parameter.type, {}, source, known, diagnostics);
			}
		}
	}
	for (const auto& symbol : module.symbols) {
		if (symbol.kind != DeclKind::function) {
			continue;
		}
		check_type_reference(symbol.return_type, symbol.span.begin, source, known, diagnostics);
		for (const auto& parameter : symbol.parameters) {
			check_type_reference(parameter.type, symbol.span.begin, source, known, diagnostics);
		}
		check_function_body(symbol, module, known, diagnostics);
	}
	for (const auto& uniform : module.uniforms) {
		check_type_reference(uniform.type, {}, source, known, diagnostics);
		for (const auto& field : uniform.inline_fields) {
			check_type_reference(field.type, {}, source, known, diagnostics);
		}
	}
	for (const auto& layout : module.layouts) {
		if (!layout.is_inline_struct) {
			check_type_reference(layout.type_spelling, layout.span.begin, source, known, diagnostics);
		}
		for (const auto& field : layout.inline_fields) {
			check_type_reference(field.type, layout.span.begin, source, known, diagnostics);
		}
	}
	for (const auto& alias : module.type_aliases) {
		check_type_reference(alias.base, {}, source, known, diagnostics);
	}
}

LayoutRule resolve_layout_rule(LayoutRule declared, bool is_storage_buffer) {
	if (declared != LayoutRule::unset) {
		return declared;
	}
	return is_storage_buffer ? LayoutRule::std430 : LayoutRule::std140;
}

void validate_layouts(SemanticModule& module, DiagnosticEngine& diagnostics) {
	const auto layouts_match = [](const LayoutDecl& a, const LayoutDecl& b) {
		if (a.rule != b.rule || a.is_inline_struct != b.is_inline_struct || a.type_spelling != b.type_spelling ||
			a.inline_fields.size() != b.inline_fields.size()) {
			return false;
		}
		for (std::size_t i = 0; i < a.inline_fields.size(); ++i) {
			if (a.inline_fields[i].type != b.inline_fields[i].type || a.inline_fields[i].name != b.inline_fields[i].name) {
				return false;
			}
		}
		return true;
	};

	std::unordered_map<std::string, std::size_t> uniform_indices;
	for (std::size_t i = 0; i < module.uniforms.size(); ++i) {
		uniform_indices.emplace(qualified_uniform_name(module.uniforms[i]), i);
	}

	std::unordered_map<std::size_t, std::size_t> first_layout_for_uniform;
	for (std::size_t li = 0; li < module.layouts.size(); ++li) {
		auto& layout = module.layouts[li];
		const std::string qn = qualified_layout_name(layout);
		const auto target_it = uniform_indices.find(qn);
		if (target_it == uniform_indices.end()) {
			diagnostics.report(DiagnosticCode::layout_unknown_uniform, DiagnosticSeverity::error, layout.span.begin, module.source_name,
							   std::format("layout refers to unknown uniform '{}'", qn));
			continue;
		}
		const auto& target = module.uniforms[target_it->second];
		if (!is_buffer_resource_type(target.type)) {
			diagnostics.report(DiagnosticCode::layout_invalid_uniform_kind, DiagnosticSeverity::error, layout.span.begin, module.source_name,
							   std::format("uniform '{}' has type {} and does not accept a layout", qn, target.type));
			continue;
		}
		layout.rule = resolve_layout_rule(layout.rule, target.type == "StorageBuffer");
		const auto [existing_it, inserted] = first_layout_for_uniform.emplace(target_it->second, li);
		if (!inserted && !layouts_match(module.layouts[existing_it->second], layout)) {
			diagnostics.report(DiagnosticCode::layout_duplicate, DiagnosticSeverity::error, layout.span.begin, module.source_name,
							   std::format("conflicting layout for '{}'", qn));
		}
	}

	for (std::size_t i = 0; i < module.uniforms.size(); ++i) {
		const auto& uniform = module.uniforms[i];
		if (is_buffer_resource_type(uniform.type) && !first_layout_for_uniform.contains(i)) {
			diagnostics.report(DiagnosticCode::layout_missing_resource_type, DiagnosticSeverity::error, {}, module.source_name,
							   std::format("{} '{}' has no layout", uniform.type == "UniformBuffer" ? "UniformBuffer" : "StorageBuffer",
								   qualified_uniform_name(uniform)));
		}
	}
}

const StageInterface* find_stage_interface(std::span<const StageInterface> interfaces, std::string_view type_name, StageRole role) {
	for (const auto& interface : interfaces) {
		if (interface.type_name == type_name && interface.role == role) {
			return &interface;
		}
	}
	return nullptr;
}

void validate_stage_interfaces(const SemanticModule& module, DiagnosticEngine& diagnostics) {
	for (const auto& interface : module.stage_interfaces) {
		const StructDecl* payload = find_struct(module.structs, interface.type_name);
		if (!payload) {
			diagnostics.report(DiagnosticCode::ir_invalid_stage_signature, DiagnosticSeverity::error, {}, module.source_name,
							   std::format("stage return boundary requires struct payload '{}'", interface.type_name));
			continue;
		}
		for (const auto& field : interface.fields) {
			if (struct_field_type(module.structs, interface.type_name, field.name).empty()) {
				diagnostics.report(DiagnosticCode::ir_invalid_stage_signature, DiagnosticSeverity::error, {}, module.source_name,
								   std::format("stage return boundary field '{}' is not a member of '{}'", field.name, interface.type_name));
			}
		}
	}
}

void check_stage_entry_signatures(const SemanticModule& module, DiagnosticEngine& diagnostics) {
	std::unordered_set<std::string_view> vertex_return_payloads;
	for (const auto& symbol : module.symbols) {
		if (symbol.kind == DeclKind::function && is_vertex_stage(symbol.stage) && !symbol.return_type.empty() && symbol.return_type != "void") {
			vertex_return_payloads.insert(symbol.return_type);
		}
	}
	for (const auto& symbol : module.symbols) {
		if (symbol.kind != DeclKind::function || symbol.stage.empty()) {
			continue;
		}
		for (const auto& parameter : symbol.parameters) {
			if (parameter.is_reference) {
				diagnostics.report(DiagnosticCode::sema_type_mismatch, DiagnosticSeverity::error, symbol.span.begin, module.source_name,
								   "stage entry parameters must be value types");
			}
		}
		if (is_vertex_stage(symbol.stage) && symbol.return_type != "void" && symbol.return_type.empty() == false &&
			!find_stage_interface(module.stage_interfaces, symbol.return_type, StageRole::varying)) {
			diagnostics.report(DiagnosticCode::ir_invalid_stage_signature, DiagnosticSeverity::error, symbol.span.begin, module.source_name,
							   "vertex stage return type requires a return boundary");
		}
		if (is_fragment_stage(symbol.stage) && !symbol.parameters.empty()) {
			const std::string& input_type = symbol.parameters.front().type;
			if (!input_type.empty() && input_type != "void" && find_struct(module.structs, input_type) &&
				!find_stage_interface(module.stage_interfaces, input_type, StageRole::varying)) {
				diagnostics.report(DiagnosticCode::ir_invalid_stage_signature, DiagnosticSeverity::error, symbol.span.begin, module.source_name,
								   std::format("fragment input '{}' requires the vertex stage return boundary payload", input_type));
			}
			if (!input_type.empty() && input_type != "void" && find_struct(module.structs, input_type) &&
				!vertex_return_payloads.empty() && !vertex_return_payloads.contains(input_type)) {
				diagnostics.report(DiagnosticCode::ir_invalid_stage_signature, DiagnosticSeverity::error, symbol.span.begin, module.source_name,
								   std::format("fragment input '{}' does not match a vertex stage return payload", input_type));
			}
		}
	}
}

} // namespace

SemanticModule Sema::analyze(const TranslationUnit& unit) {
	SemanticModule module{ .source_name = std::string(sources_.name(unit.file_id)) };
	module.imports = unit.imports;
	module.structs = unit.structs;
	module.uniforms = unit.uniforms;
	module.layouts = unit.layouts;
	module.stage_interfaces = unit.stage_interfaces;
	module.type_aliases = unit.type_aliases;
	module.using_imports = unit.using_imports;

	resolve_stage_interface_tags(module.stage_interfaces, module.source_name, diagnostics_);
	validate_stage_interfaces(module, diagnostics_);

	// Assign sequential ABI locations to user payload fields, and resolve each
	// field to the struct member it maps to so a backend can extract/insert it.
	// Clip position does not consume a user varying location.
	for (auto& interface : module.stage_interfaces) {
		const StructDecl* payload = find_struct(module.structs, interface.type_name);
		u32 next_location = 0;
		for (auto& field : interface.fields) {
			if (payload) {
				for (u32 i = 0; i < payload->fields.size(); ++i) {
					if (payload->fields[i].name == field.name) {
						field.member_index = i;
						break;
					}
				}
			}
			if (field.placement != StageFieldPlacement::user) {
				continue;
			}
			if (field.location != StageIOField::kNoLocation) {
				next_location = field.location + 1;
				continue;
			}
			field.location = next_location++;
		}
	}
	// Every distinct scope maps to a group. Named scopes can be reopened
	// across multiple `uniform name { ... }` blocks (matched by scope_name).
	// Anonymous blocks cannot be reopened: each one gets its own unique
	// anonymous_block_id from the parser, so two `uniform { ... }` blocks end
	// up on different groups. Within a single block, fields share a group and
	// receive sequential members.
	std::unordered_map<std::string, u32> named_sets;
	u32 next_set = 0;
	std::unordered_map<u32, u32> member_counts;
	for (auto& uniform : module.uniforms) {
		const std::string set_key = uniform.is_anonymous
										? "$anon$" + std::to_string(uniform.anonymous_block_id)
										: uniform.scope_name;
		auto [it, inserted] = named_sets.emplace(set_key, next_set);
		if (inserted) {
			uniform.set = next_set++;
		} else {
			uniform.set = it->second;
		}
		uniform.member = member_counts[uniform.set]++;
	}

	for (const auto& decl : unit.declarations) {
		if (decl.kind == DeclKind::namespace_decl && decl.name == "rt") {
			diagnostics_.report(DiagnosticCode::sema_reserved_namespace, DiagnosticSeverity::error, decl.span.begin, module.source_name, "namespace 'rt' is reserved");
			continue;
		}

		if (decl.kind == DeclKind::function) {
			if (const auto scope = decl.name.find("::"); scope != std::string::npos) {
				const auto owner_name = std::string_view(decl.name).substr(0, scope);
				const auto member_name = std::string_view(decl.name).substr(scope + 2);
				const StructDecl* owner = find_struct(module.structs, owner_name);
				if (owner && !declares_member_function(*owner, member_name, decl.parameters, decl.return_type)) {
					diagnostics_.report(DiagnosticCode::sema_member_fn_no_declaration, DiagnosticSeverity::error, decl.span.begin, module.source_name,
										"member function definition has no matching declaration in its owner type");
				}
			}
		}

		const std::string stage = decl.kind == DeclKind::function
									  ? resolve_stage_attribute(decl.attributes, module.source_name, diagnostics_)
									  : std::string{};

		module.symbols.emplace_back(SemanticSymbol{
			.kind = decl.kind,
			.name = decl.name,
			.parameters = decl.parameters,
			.return_type = decl.return_type,
			.body_statements = decl.body_statements,
			.exported = decl.exported,
			.attributes = decl.attributes,
			.stage = stage,
			.has_body = decl.has_body,
			.span = decl.span,
		});
		if (decl.exported && decl.kind != DeclKind::import) {
			module.exports.emplace_back(ExportSymbol{
				.name = decl.name,
				.kind = decl.kind == DeclKind::function ? "function" : "symbol",
				.type = decl.return_type,
				.interface_hash = export_interface_hash(decl),
			});
		}
	}

	validate_layouts(module, diagnostics_);
	check_types(module, diagnostics_);
	check_stage_entry_signatures(module, diagnostics_);

	return module;
}

} // namespace rtsl
