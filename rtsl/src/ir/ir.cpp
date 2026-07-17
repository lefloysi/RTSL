#include "ir/ir.hpp"

#include "sema/mangler.hpp"
#include "sema/stage_rules.hpp"
#include "sema/uniform_lowering.hpp"
#include "support/hashing.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <format>
#include <optional>
#include <span>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace rtsl {

namespace {

struct CallableTarget {
	std::string name;
	std::vector<std::string> value_parameter_types;
	std::string mangled_name;
};

bool is_scalar_value_type(std::string_view type) {
#define RTSL_SCALAR_TYPE(spelling, semantic_kind, width, ir_op) \
	if (type == #spelling) { return true; }
#include "frontend/value_types.def"
	return false;
}

bool call_argument_compatible(std::string_view target, std::string_view value) {
	if (target.empty() || value.empty()) return true;
	if (target == value) return true;
	return is_scalar_value_type(target) && is_scalar_value_type(value);
}

std::string parameter_identity(const ParameterDecl& parameter) {
	std::string identity;
	if (parameter.is_const) {
		identity += "const ";
	}
	identity += parameter.type;
	if (parameter.is_reference) {
		identity += "&";
	}
	return identity;
}

} // namespace

// ----------------------------------------------------------------------------
// IRBuilder: id allocation, type/constant pool with dedup, local instruction
// emission.
// ----------------------------------------------------------------------------

class IRBuilder {
  public:
	explicit IRBuilder(IRModule& module,
		StringSet callable_names = {},
		std::vector<CallableTarget> callable_targets = {},
		DiagnosticEngine* diagnostics = nullptr)
		: module_(module), callable_names_(std::move(callable_names)), callable_targets_(std::move(callable_targets)), diagnostics_(diagnostics) {}

	[[nodiscard]] ID<IRInstruction> fresh_id() {
		const ID<IRInstruction> id = module_.next_id;
		module_.next_id = ID<IRInstruction>{ raw_id(module_.next_id) + 1 };
		return id;
	}

	// Append a type/constant pool entry and return its id. The entry is keyed
	// by a stable string signature so identical types map to the same id.
	ID<IRInstruction> intern_type(std::string_view signature, IROp op, ID<IRInstruction> type_id, std::span<const ID<IRInstruction>> operands, std::span<const u32> literals) {
		auto it = type_cache_.find(signature);
		if (it != type_cache_.end()) {
			return it->second;
		}
		const ID<IRInstruction> id = fresh_id();
		IRInstruction inst;
		inst.op = op;
		inst.result_id = id;
		inst.type_id = type_id;
		inst.operands.assign(operands.begin(), operands.end());
		inst.literals.assign(literals.begin(), literals.end());
		module_.type_constant_pool.push_back(std::move(inst));
		type_cache_.emplace(signature, id);
		return id;
	}

	ID<IRInstruction> intern_type(std::string_view signature, IROp op, ID<IRInstruction> type_id, std::initializer_list<ID<IRInstruction>> operands, std::initializer_list<u32> literals) {
		return intern_type(signature, op, type_id, std::span<const ID<IRInstruction>>(operands.begin(), operands.size()), std::span<const u32>(literals.begin(), literals.size()));
	}

	ID<IRInstruction> intern_constant(std::string_view signature, IROp op, ID<IRInstruction> type_id, std::span<const ID<IRInstruction>> operands, std::span<const u32> literals) {
		// Constants share the same pool as types; deduped via the same cache.
		return intern_type(signature, op, type_id, operands, literals);
	}

	ID<IRInstruction> intern_constant(std::string_view signature, IROp op, ID<IRInstruction> type_id, std::initializer_list<ID<IRInstruction>> operands, std::initializer_list<u32> literals) {
		return intern_constant(signature, op, type_id, std::span<const ID<IRInstruction>>(operands.begin(), operands.size()), std::span<const u32>(literals.begin(), literals.size()));
	}

	void add_global_variable(IRInstruction inst) {
		module_.global_variables.push_back(std::move(inst));
	}

	void add_decoration(IRDecoration d) { module_.decorations.push_back(std::move(d)); }

	IRModule& module() { return module_; }
	bool is_known_callable(std::string_view name) const { return callable_names_.contains(name); }
	[[nodiscard]] const CallableTarget* resolve_call_target(std::string_view name, std::span<const std::string> argument_types) const {
		const CallableTarget* selected = nullptr;
		for (const auto& target : callable_targets_) {
			if (target.name != name || target.value_parameter_types.size() != argument_types.size()) {
				continue;
			}
			bool compatible = true;
			for (std::size_t i = 0; i < argument_types.size(); ++i) {
				if (!call_argument_compatible(target.value_parameter_types[i], argument_types[i])) {
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
			selected = &target;
		}
		return selected;
	}
	void diagnose(std::string_view message) const {
		if (diagnostics_) {
			diagnostics_->report(DiagnosticCode::ir_expression_error, DiagnosticSeverity::error, {}, module_.source_name, message);
		}
	}

  private:
	IRModule& module_;
	StringMap<ID<IRInstruction>> type_cache_;
	StringSet callable_names_;
	std::vector<CallableTarget> callable_targets_;
	DiagnosticEngine* diagnostics_ = nullptr;
};

// ----------------------------------------------------------------------------
// TypeRegistry: name (source spelling) -> ID<IRInstruction> for the type/constant pool.
// Also resolves member layouts so the lowerer knows the type of `p.position`,
// the index of a struct field, etc.
// ----------------------------------------------------------------------------

struct TypeInfo {
	ID<IRInstruction> id = ID<IRInstruction>{};
	enum class Kind { Void,
					  Bool,
					  Int,
					  UInt,
					  Float,
					  Vector,
					  Matrix,
					  Struct,
					  Unknown };
	Kind kind = Kind::Unknown;
	u32 width = 0;									   // scalar width in bits
	u32 components = 0;								   // vector component count, matrix column count
	ID<IRInstruction> element_type_id = ID<IRInstruction>{};				   // vector element / matrix column type
	std::vector<std::pair<std::string, ID<IRInstruction>>> members; // struct member name -> member type id
};

class TypeRegistry {
  public:
	explicit TypeRegistry(IRBuilder& builder) : builder_(builder) {
		// Prime the registry with the language's builtin scalars and the
		// common vec/mat aggregates. Anything not registered here is looked up
		// by name against user struct decls.
#define RTSL_SCALAR_TYPE(spelling, semantic_kind, width, ir_op) scalar(#spelling, TypeInfo::Kind::semantic_kind, width, IROp::ir_op);
#define RTSL_VECTOR_TYPE(spelling, element, components) vector(#spelling, find(#element), components);
#define RTSL_MATRIX_TYPE(spelling, column_type, columns) matrix(#spelling, find(#column_type), columns);
#include "frontend/value_types.def"
	}

	// Register an anonymous struct type built from a layout's inline fields.
	// The signature includes the caller-supplied nonce so two distinct
	// `layout X : struct { … };` declarations with the same field shape still
	// get separate ids — a binding's type identity is tied to its binding
	// path, not just its member list.
	ID<IRInstruction> register_anon_struct(std::string_view nonce, std::span<const StructField> fields) {
		std::vector<ID<IRInstruction>> member_ids;
		std::vector<std::pair<std::string, ID<IRInstruction>>> members;
		member_ids.reserve(fields.size());
		members.reserve(fields.size());
		std::string signature = "layout:";
		signature += nonce;
		for (const auto& field : fields) {
			const ID<IRInstruction> member_id = find_or_unknown(field.type);
			member_ids.push_back(member_id);
			members.emplace_back(field.name, member_id);
			signature += ":" + field.type;
		}
		const ID<IRInstruction> id = builder_.intern_type(signature, IROp::TypeStruct, ID<IRInstruction>{}, std::move(member_ids), {});
		return id;
	}

	void register_struct(const StructDecl& decl) {
		// Two-pass: build the type id from currently known member types. If a
		// member type is itself a struct still being defined, fall back to a
		// forward-declared opaque entry that the linker can patch later.
		std::vector<ID<IRInstruction>> member_ids;
		member_ids.reserve(decl.fields.size());
		std::vector<std::pair<std::string, ID<IRInstruction>>> members;
		members.reserve(decl.fields.size());
		std::string signature = "struct{" + decl.name + "}";
		for (const auto& field : decl.fields) {
			const ID<IRInstruction> member_id = find_or_unknown(field.type);
			member_ids.push_back(member_id);
			members.emplace_back(field.name, member_id);
			signature += ":" + field.type;
		}
		const ID<IRInstruction> id = builder_.intern_type(signature, IROp::TypeStruct, ID<IRInstruction>{}, std::move(member_ids), {});
		auto& info = info_[decl.name];
		info.id = id;
		info.kind = TypeInfo::Kind::Struct;
		info.members = std::move(members);
	}

	[[nodiscard]] ID<IRInstruction> find(std::string_view name) const {
		const auto it = info_.find(name);
		return it == info_.end() ? ID<IRInstruction>{} : it->second.id;
	}

	[[nodiscard]] const TypeInfo* info(std::string_view name) const {
		const auto it = info_.find(name);
		return it == info_.end() ? nullptr : &it->second;
	}

	[[nodiscard]] const TypeInfo* info_by_id(ID<IRInstruction> id) const {
		for (const auto& [name, info] : info_) {
			if (info.id == id) {
				return &info;
			}
		}
		return nullptr;
	}

	[[nodiscard]] std::string_view name_by_id(ID<IRInstruction> id) const {
		for (const auto& [name, info] : info_) {
			if (info.id == id) {
				return name;
			}
		}
		return "<unknown>";
	}

	// Resolve an unknown name to "unknown" (Id 0). Lowering treats this as
	// opaque — operations are still emitted but the backend gets to refuse.
	[[nodiscard]] ID<IRInstruction> find_or_unknown(std::string_view name) const {
		const ID<IRInstruction> id = find(name);
		return id ? id : ID<IRInstruction>{};
	}

	// Find the registered vector type with the given element scalar and
	// component count (vec2/vec3/vec4 and their integer/unsigned siblings are
	// all primed at construction). One component resolves to the scalar itself.
	[[nodiscard]] ID<IRInstruction> vector_of(ID<IRInstruction> element, u32 components) const {
		if (components <= 1) {
			return element;
		}
		for (const auto& [name, info] : info_) {
			if (info.kind == TypeInfo::Kind::Vector && info.element_type_id == element &&
				info.components == components) {
				return info.id;
			}
		}
		return element;
	}

	[[nodiscard]] ID<IRInstruction> pointer_to(ID<IRInstruction> pointee, StorageClass sc) {
		if (pointee == ID<IRInstruction>{}) {
			pointee = find("void");
		}
		return builder_.intern_type(std::format("ptr:{}:{}", raw_id(pointee), static_cast<u32>(sc)), IROp::TypePointer, ID<IRInstruction>{}, { pointee }, { static_cast<u32>(sc) });
	}

  private:
	ID<IRInstruction> scalar(std::string_view name, TypeInfo::Kind kind, u32 width, IROp op) {
		std::span<const u32> literals;
		u32 width_literal = width;
		if (op != IROp::TypeBool && op != IROp::TypeVoid) {
			literals = std::span<const u32>(&width_literal, 1);
		}
		const ID<IRInstruction> id = builder_.intern_type(std::format("scalar:{}", name), op, ID<IRInstruction>{}, {}, literals);
		auto& info = info_.try_emplace(std::string(name)).first->second;
		info.id = id;
		info.kind = kind;
		info.width = width;
		return id;
	}

	void vector(std::string_view name, ID<IRInstruction> elem, u32 n) {
		const ID<IRInstruction> id = builder_.intern_type(std::format("vec:{}:{}", raw_id(elem), n), IROp::TypeVector, ID<IRInstruction>{}, { elem }, { n });
		auto& info = info_.try_emplace(std::string(name)).first->second;
		info.id = id;
		info.kind = TypeInfo::Kind::Vector;
		info.components = n;
		info.element_type_id = elem;
	}

	void matrix(std::string_view name, ID<IRInstruction> col_vec, u32 n) {
		const ID<IRInstruction> id = builder_.intern_type(std::format("mat:{}:{}", raw_id(col_vec), n), IROp::TypeMatrix, ID<IRInstruction>{}, { col_vec }, { n });
		auto& info = info_.try_emplace(std::string(name)).first->second;
		info.id = id;
		info.kind = TypeInfo::Kind::Matrix;
		info.components = n;
		info.element_type_id = col_vec;
	}

	void opaque(std::string_view name, TypeInfo::Kind kind, IROp op) {
		const ID<IRInstruction> id = builder_.intern_type(std::format("opaque:{}", name), op, ID<IRInstruction>{}, {}, {});
		auto& info = info_.try_emplace(std::string(name)).first->second;
		info.id = id;
		info.kind = kind;
	}

	IRBuilder& builder_;
	StringMap<TypeInfo> info_;
};

// ----------------------------------------------------------------------------
// Expression tokenizer + Pratt parser, operating on the statement strings the
// current parser captures. The strings have already had source-level uniform
// references mangled to u_xxx form by lower_uniform_references.
// ----------------------------------------------------------------------------

enum class TokKind {
	end,
	number_int,
	number_float,
	ident,
	lparen,
	rparen,
	comma,
	dot,
	scope,
	plus,
	minus,
	star,
	slash,
	percent,
	eq,
};

struct Tok {
	TokKind kind = TokKind::end;
	std::string text;
};

class Lex {
  public:
	explicit Lex(std::string_view src) : src_(src) {}

	Tok next() {
		skip_ws();
		if (pos_ >= src_.size()) {
			return { TokKind::end, {} };
		}
		const char c = src_[pos_];
		if (c == ':' && pos_ + 1 < src_.size() && src_[pos_ + 1] == ':') {
			pos_ += 2;
			return { TokKind::scope, "::" };
		}
		static constexpr std::pair<char, TokKind> Punctuation[] = {
			{ '(', TokKind::lparen }, { ')', TokKind::rparen }, { ',', TokKind::comma },
			{ '+', TokKind::plus }, { '-', TokKind::minus }, { '*', TokKind::star },
			{ '/', TokKind::slash }, { '%', TokKind::percent }, { '=', TokKind::eq },
			{ '.', TokKind::dot }, // "1.0" never reaches here — number() owns digit-led text
		};
		for (const auto& [ch, kind] : Punctuation) {
			if (c == ch) {
				++pos_;
				return { kind, std::string(1, ch) };
			}
		}
		if (std::isdigit(static_cast<unsigned char>(c))) {
			return number();
		}
		if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
			return ident();
		}
		// Unknown character — treat as end so we don't loop forever.
		return { TokKind::end, {} };
	}

	Tok peek() {
		const auto save_pos = pos_;
		const auto tok = next();
		pos_ = save_pos;
		return tok;
	}

  private:
	void skip_ws() {
		while (pos_ < src_.size() && std::isspace(static_cast<unsigned char>(src_[pos_]))) {
			++pos_;
		}
	}

	Tok number() {
		const auto start = pos_;
		bool is_float = false;
		while (pos_ < src_.size() && (std::isdigit(static_cast<unsigned char>(src_[pos_])) ||
									  src_[pos_] == '.' || src_[pos_] == 'e' || src_[pos_] == 'E' ||
									  src_[pos_] == '+' || src_[pos_] == '-' || src_[pos_] == 'f')) {
			if (src_[pos_] == '.' || src_[pos_] == 'e' || src_[pos_] == 'E' || src_[pos_] == 'f') {
				is_float = true;
			}
			// Stop at unary '+' / '-' that aren't part of an exponent.
			if ((src_[pos_] == '+' || src_[pos_] == '-') &&
				!(pos_ > start && (src_[pos_ - 1] == 'e' || src_[pos_ - 1] == 'E'))) {
				break;
			}
			++pos_;
		}
		return { is_float ? TokKind::number_float : TokKind::number_int,
				 std::string(src_.substr(start, pos_ - start)) };
	}

	Tok ident() {
		const auto start = pos_;
		while (pos_ < src_.size() && (std::isalnum(static_cast<unsigned char>(src_[pos_])) || src_[pos_] == '_')) {
			++pos_;
		}
		return { TokKind::ident, std::string(src_.substr(start, pos_ - start)) };
	}

	std::string_view src_;
	std::size_t pos_ = 0;
};

// ----------------------------------------------------------------------------
// FunctionLowerer: per-function SSA emission. Holds the local scope (parameter
// and let bindings -> ids and types), keeps a current basic block, and emits
// IRInstructions for expressions / statements.
// ----------------------------------------------------------------------------

struct Value {
	ID<IRInstruction> id = ID<IRInstruction>{};
	ID<IRInstruction> type_id = ID<IRInstruction>{};
};

class FunctionLowerer {
  public:
	FunctionLowerer(IRBuilder& builder, TypeRegistry& types, IRFunction& fn,
		std::span<const UniformBinding> uniforms,
		const StringMap<std::string>& using_uniforms,
		const StringMap<ID<IRInstruction>>& uniform_var_ids,
		const StringMap<ID<IRInstruction>>& uniform_var_type_ids,
		const StringMap<ID<IRInstruction>>& uniform_value_type_ids)
		: builder_(builder), types_(types), fn_(fn), uniforms_(uniforms),
		  using_uniforms_(using_uniforms), uniform_var_ids_(uniform_var_ids),
		  uniform_var_type_ids_(uniform_var_type_ids), uniform_value_type_ids_(uniform_value_type_ids) {}

	void bind_parameter(std::string_view name, std::string_view type, ID<IRInstruction> param_id) {
		locals_.insert_or_assign(std::string(name), Local{
			.id = param_id,
			.type_id = types_.find(type),
			.type_name = std::string(type),
			.is_pointer = false,
		});
	}

	// Enter "constructor" lowering mode. While active, unqualified field
	// assignments in the body (e.g. `position = ...`) are recorded into a
	// per-field slot map instead of being dropped, and emit_constructor_return
	// synthesizes the trailing CompositeConstruct + ReturnValue for the
	// assembled `this`.
	void begin_constructor(std::string_view owner) {
		ctor_owner_name_ = std::string(owner);
		ctor_owner_type_id_ = types_.find(owner);
		ctor_field_values_.clear();
		const TypeInfo* info = ctor_owner_type_id_ ? types_.info_by_id(ctor_owner_type_id_) : nullptr;
		if (info) {
			ctor_field_values_.assign(info->members.size(), ID<IRInstruction>{});
		}
	}
	[[nodiscard]] bool in_constructor() const { return ctor_owner_type_id_ != ID<IRInstruction>{}; }

	// Try to record `field = value` against the constructor's implicit `this`.
	// Returns true if the field was recognized and stored.
	bool record_constructor_field(std::string_view field, ID<IRInstruction> value) {
		if (!in_constructor()) {
			return false;
		}
		const TypeInfo* info = types_.info_by_id(ctor_owner_type_id_);
		if (!info) {
			return false;
		}
		for (u32 i = 0; i < info->members.size(); ++i) {
			if (info->members[i].first == field) {
				if (i < ctor_field_values_.size()) {
					ctor_field_values_[i] = value;
				}
				return true;
			}
		}
		return false;
	}

	// Assemble the final `this` value and return it. Called after the body
	// statements have been lowered.
	void emit_constructor_return() {
		if (!in_constructor())
			return;
		IRInstruction construct;
		construct.op = IROp::CompositeConstruct;
		construct.result_id = builder_.fresh_id();
		construct.type_id = ctor_owner_type_id_;
		construct.operands.reserve(ctor_field_values_.size());
		for (ID<IRInstruction> id : ctor_field_values_) {
			construct.operands.push_back(id);
		}
		const ID<IRInstruction> result_id = construct.result_id;
		fn_.body.push_back(std::move(construct));
		IRInstruction ret;
		ret.op = IROp::ReturnValue;
		ret.operands = { result_id };
		fn_.body.push_back(std::move(ret));
	}

	// Open the function's entry basic block.
	void begin_entry_block() {
		IRInstruction label;
		label.op = IROp::Label;
		label.result_id = builder_.fresh_id();
		fn_.body.push_back(std::move(label));
	}

	void lower_statement(const Decl::BodyStatement& statement) {
		switch (statement.kind) {
		case Decl::BodyStatementKind::return_stmt:
			lower_return(statement.expr);
			return;
		case Decl::BodyStatementKind::block:
			for (const auto& child : statement.children) {
				lower_statement(child);
			}
			return;
		case Decl::BodyStatementKind::if_stmt:
			lower_if(statement);
			return;
		case Decl::BodyStatementKind::while_stmt:
			lower_while(statement);
			return;
		case Decl::BodyStatementKind::do_stmt:
			lower_do_while(statement);
			return;
		case Decl::BodyStatementKind::for_stmt:
			lower_for(statement);
			return;
		case Decl::BodyStatementKind::assignment:
			lower_assign(statement.lhs, statement.expr);
			return;
		case Decl::BodyStatementKind::declaration:
			lower_decl(statement.type_name, statement.name, statement.expr);
			return;
		case Decl::BodyStatementKind::expression:
		case Decl::BodyStatementKind::unknown:
			lower_expression(statement.expr);
			return;
		}
	}

	// Compute final-pass id for the function's parameter list now that
	// parameters have been added.
	void emit_function_parameters(std::span<const ParameterDecl> params) {
		for (const auto& p : params) {
			IRInstruction inst;
			inst.op = IROp::FunctionParameter;
			inst.result_id = builder_.fresh_id();
			inst.type_id = types_.find(p.type);
			fn_.parameter_ids.push_back(inst.result_id);
			fn_.body.push_back(std::move(inst));
			bind_parameter(p.name, p.type, fn_.parameter_ids.back());
		}
	}

	void emit_implicit_void_return() {
		IRInstruction ret;
		ret.op = IROp::Return;
		fn_.body.push_back(std::move(ret));
	}

  private:
	struct Local {
		ID<IRInstruction> id = ID<IRInstruction>{};
		ID<IRInstruction> type_id = ID<IRInstruction>{};
		std::string type_name;
		bool is_pointer = false;
	};

	static std::string trim(std::string_view text) {
		const auto is_space = [](char c) {
			return std::isspace(static_cast<unsigned char>(c)) != 0;
		};
		const auto first = std::ranges::find_if_not(text, is_space);
		const auto last = std::find_if_not(text.rbegin(), text.rend(), is_space).base();
		if (first >= last) {
			return {};
		}
		return { first, last };
	}

	static bool is_effectful_expression_statement(const Decl::Expr& expr) {
		return expr.kind == Decl::Expr::Kind::call ||
			   (expr.kind == Decl::Expr::Kind::binary && expr.op == "=");
	}

	void lower_expression(const Decl::Expr& expr) {
		if (!is_effectful_expression_statement(expr)) {
			builder_.diagnose("expression statement has no effect");
			return;
		}
		if (lower_expr(expr).id == ID<IRInstruction>{}) {
			return;
		}
	}

	void emit_branch(ID<IRInstruction> target) {
		IRInstruction inst;
		inst.op = IROp::Branch;
		inst.operands = { target };
		fn_.body.push_back(std::move(inst));
	}

	void emit_branch_conditional(ID<IRInstruction> cond, ID<IRInstruction> true_label, ID<IRInstruction> false_label) {
		IRInstruction inst;
		inst.op = IROp::BranchConditional;
		inst.operands = { cond, true_label, false_label };
		fn_.body.push_back(std::move(inst));
	}

	ID<IRInstruction> emit_label() {
		IRInstruction label;
		label.op = IROp::Label;
		label.result_id = builder_.fresh_id();
		fn_.body.push_back(std::move(label));
		return fn_.body.back().result_id;
	}

	// Lower a control-flow condition to a boolean value. An empty condition (a
	// bare `for (;;)`) is an implicit `true`. A condition that fails to lower is
	// a diagnosed error rather than a null branch operand, so a malformed
	// condition never produces silently broken control flow.
	Value lower_condition(const Decl::Expr& expr) {
		if (expr.kind == Decl::Expr::Kind::unknown) {
			return Value{ constant_bool(true), types_.find("bool") };
		}
		const Value cond = lower_expr(expr);
		if (cond.id == ID<IRInstruction>{}) {
			builder_.diagnose("condition does not produce a value");
			return Value{ constant_bool(true), types_.find("bool") };
		}
		return cond;
	}

	void lower_if(const Decl::BodyStatement& statement) {
		const Value cond = lower_condition(statement.expr);
		IRInstruction merge;
		merge.op = IROp::SelectionMerge;
		merge.operands = { builder_.fresh_id() };
		fn_.body.push_back(std::move(merge));
		const ID<IRInstruction> merge_label = fn_.body.back().operands.front();
		const ID<IRInstruction> then_label = builder_.fresh_id();
		const ID<IRInstruction> else_label = builder_.fresh_id();
		emit_branch_conditional(cond.id, then_label, else_label);
		IRInstruction then_inst;
		then_inst.op = IROp::Label;
		then_inst.result_id = then_label;
		fn_.body.push_back(std::move(then_inst));
		for (const auto& child : statement.children)
			lower_statement(child);
		emit_branch(merge_label);
		IRInstruction else_inst;
		else_inst.op = IROp::Label;
		else_inst.result_id = else_label;
		fn_.body.push_back(std::move(else_inst));
		for (const auto& child : statement.else_children)
			lower_statement(child);
		emit_branch(merge_label);
		IRInstruction merge_inst;
		merge_inst.op = IROp::Label;
		merge_inst.result_id = merge_label;
		fn_.body.push_back(std::move(merge_inst));
	}

	void lower_while(const Decl::BodyStatement& statement) {
		const ID<IRInstruction> merge_label = builder_.fresh_id();
		const ID<IRInstruction> head_label = builder_.fresh_id();
		const ID<IRInstruction> body_label = builder_.fresh_id();
		emit_branch(head_label);

		IRInstruction head_inst;
		head_inst.op = IROp::Label;
		head_inst.result_id = head_label;
		fn_.body.push_back(std::move(head_inst));

		IRInstruction merge;
		merge.op = IROp::LoopMerge;
		merge.operands = { merge_label, head_label };
		fn_.body.push_back(std::move(merge));
		const Value cond = lower_condition(statement.expr);
		emit_branch_conditional(cond.id, body_label, merge_label);

		IRInstruction body_inst;
		body_inst.op = IROp::Label;
		body_inst.result_id = body_label;
		fn_.body.push_back(std::move(body_inst));
		for (const auto& child : statement.children)
			lower_statement(child);
		emit_branch(head_label);

		IRInstruction merge_inst;
		merge_inst.op = IROp::Label;
		merge_inst.result_id = merge_label;
		fn_.body.push_back(std::move(merge_inst));
	}

	void lower_do_while(const Decl::BodyStatement& statement) {
		const ID<IRInstruction> body_label = builder_.fresh_id();
		const ID<IRInstruction> cond_label = builder_.fresh_id();
		const ID<IRInstruction> merge_label = builder_.fresh_id();
		emit_branch(body_label);

		IRInstruction body_inst;
		body_inst.op = IROp::Label;
		body_inst.result_id = body_label;
		fn_.body.push_back(std::move(body_inst));
		for (const auto& child : statement.children)
			lower_statement(child);
		emit_branch(cond_label);

		IRInstruction cond_inst;
		cond_inst.op = IROp::Label;
		cond_inst.result_id = cond_label;
		fn_.body.push_back(std::move(cond_inst));

		const Value cond = lower_condition(statement.expr);
		IRInstruction merge;
		merge.op = IROp::SelectionMerge;
		merge.operands = { merge_label };
		fn_.body.push_back(std::move(merge));
		emit_branch_conditional(cond.id, body_label, merge_label);

		IRInstruction merge_inst;
		merge_inst.op = IROp::Label;
		merge_inst.result_id = merge_label;
		fn_.body.push_back(std::move(merge_inst));
	}

	void lower_for(const Decl::BodyStatement& statement) {
		const ID<IRInstruction> head_label = builder_.fresh_id();
		const ID<IRInstruction> body_label = builder_.fresh_id();
		const ID<IRInstruction> continue_label = builder_.fresh_id();
		const ID<IRInstruction> merge_label = builder_.fresh_id();
		emit_branch(head_label);

		IRInstruction head_inst;
		head_inst.op = IROp::Label;
		head_inst.result_id = head_label;
		fn_.body.push_back(std::move(head_inst));

		IRInstruction merge;
		merge.op = IROp::LoopMerge;
		merge.operands = { merge_label, continue_label };
		fn_.body.push_back(std::move(merge));
		const Value cond = lower_condition(statement.expr);
		emit_branch_conditional(cond.id, body_label, merge_label);

		IRInstruction body_inst;
		body_inst.op = IROp::Label;
		body_inst.result_id = body_label;
		fn_.body.push_back(std::move(body_inst));
		for (const auto& child : statement.children)
			lower_statement(child);
		emit_branch(continue_label);

		IRInstruction continue_inst;
		continue_inst.op = IROp::Label;
		continue_inst.result_id = continue_label;
		fn_.body.push_back(std::move(continue_inst));
		emit_branch(head_label);

		IRInstruction merge_inst;
		merge_inst.op = IROp::Label;
		merge_inst.result_id = merge_label;
		fn_.body.push_back(std::move(merge_inst));
	}

	void lower_decl(std::string_view type_name, std::string_view local_name, const Decl::Expr& init) {
		// Inside a constructor body, `position = vec4(...)` parses as a
		// declaration where the parser fills both the "type" and "name" with
		// "position". Without intervention we'd allocate a junk Variable with
		// pointer-to-unknown-type, and the CompositeConstruct synthesized by
		// emit_constructor_return would never see the computed value. Detect
		// the constructor-owned field write here and record it directly.
		if (in_constructor() && init.kind != Decl::Expr::Kind::unknown) {
			const TypeInfo* info = types_.info_by_id(ctor_owner_type_id_);
			const bool name_matches_owner_field = [&]() {
				if (!info) {
					return false;
				}
				for (const auto& member : info->members) {
					if (member.first == local_name) {
						return true;
					}
				}
				return false;
			}();
			const bool looks_like_field_write =
				name_matches_owner_field &&
				(type_name.empty() || type_name == local_name || !types_.find(type_name));
			if (looks_like_field_write) {
				const Value v = lower_expr(init);
				if (record_constructor_field(local_name, v.id)) {
					return;
				}
			}
		}

		const ID<IRInstruction> type_id = types_.find(type_name);
		const ID<IRInstruction> ptr_ty = types_.pointer_to(type_id, StorageClass::Function);
		IRInstruction var;
		var.op = IROp::Variable;
		var.result_id = builder_.fresh_id();
		var.type_id = ptr_ty;
		var.literals.push_back(static_cast<u32>(StorageClass::Function));
		fn_.body.push_back(std::move(var));

		Local local;
		local.id = fn_.body.back().result_id;
		local.type_id = type_id;
		local.type_name = std::string(type_name);
		local.is_pointer = true;
		locals_[std::string(local_name)] = std::move(local);

		if (init.kind != Decl::Expr::Kind::unknown) {
			const Value v = lower_expr(init);
			IRInstruction store;
			store.op = IROp::Store;
			store.operands = { local.id, v.id };
			fn_.body.push_back(std::move(store));
		}
	}

	void lower_assign(std::string_view lhs, const Decl::Expr& rhs) {
		const std::string lhs_t = trim(lhs);

		// Plain assignment to a local or a member of a struct local.
		// We support: `name = expr` and `name.member = expr` and `this.member = expr`.
		const Value v = lower_expr(rhs);

		Lex lex{ lhs_t };
		const Tok head = lex.next();
		if (head.kind != TokKind::ident) {
			// Drop the rhs result; we can't honor this assignment shape.
			return;
		}

		std::vector<std::string> path;
		while (true) {
			const Tok t = lex.peek();
			if (t.kind == TokKind::dot) {
				lex.next();
				const Tok field = lex.next();
				if (field.kind != TokKind::ident) {
					break;
				}
				path.push_back(field.text);
			} else {
				break;
			}
		}

		const auto it = locals_.find(head.text);
		if (it == locals_.end()) {
			// Unknown lhs. Inside a constructor body the source writes to the
			// owning struct's fields via bare names (`position = expr` instead
			// of `this.position = expr`); record those into the constructor's
			// field-value map. The trailing CompositeConstruct + ReturnValue
			// synthesized by emit_constructor_return assembles them into the
			// returned `this`.
			if (path.empty() && in_constructor()) {
				if (record_constructor_field(head.text, v.id)) {
					return;
				}
				builder_.diagnose(std::format("no member named '{}' in type '{}'",
											  head.text, ctor_owner_name_));
				return;
			}
			// Fallback: treat as a write to an implicit "this" pointer if a
			// pointer-shaped `this` local was synthesized elsewhere.
			const auto this_it = locals_.find("this");
			if (this_it == locals_.end()) {
				builder_.diagnose(std::format("assignment to unknown name '{}'", head.text));
				return;
			}
			std::vector<std::string> full_path{ head.text };
			full_path.insert(full_path.end(), path.begin(), path.end());
			store_member(this_it->second, full_path, v);
			return;
		}

		if (path.empty()) {
			// Direct local assignment.
			if (it->second.is_pointer) {
				IRInstruction store;
				store.op = IROp::Store;
				store.operands = { it->second.id, v.id };
				fn_.body.push_back(std::move(store));
			} else {
				// SSA rebind: the local now refers to v.
				it->second.id = v.id;
				it->second.type_id = v.type_id;
			}
		} else {
			store_member(it->second, path, v);
		}
	}

	void store_member(const Local& local, std::span<const std::string> path, const Value& v) {
		// Resolve the member type and index chain.
		ID<IRInstruction> cur_type = local.type_id;
		std::vector<ID<IRInstruction>> indices;
		for (const auto& name : path) {
			const TypeInfo* info = types_.info_by_id(cur_type);
			if (!info)
				break;
			// Vector member access (`.x`, `.y`, `.z`, `.w`) -> component index.
			if (info->kind == TypeInfo::Kind::Vector) {
				const auto idx = vector_component(name);
				if (!idx)
					break;
				indices.push_back(constant_uint(*idx));
				cur_type = info->element_type_id;
				continue;
			}
			// Struct member.
			u32 found_index = 0;
			bool found = false;
			for (u32 i = 0; i < info->members.size(); ++i) {
				if (info->members[i].first == name) {
					found_index = i;
					cur_type = info->members[i].second;
					found = true;
					break;
				}
			}
			if (!found)
				break;
			indices.push_back(constant_uint(found_index));
		}

		if (!local.is_pointer) {
			// CompositeInsert path: build a new SSA value with the member set.
			IRInstruction insert;
			insert.op = IROp::CompositeInsert;
			insert.result_id = builder_.fresh_id();
			insert.type_id = local.type_id;
			insert.operands = { local.id, v.id };
			for (auto idx : indices) {
				insert.literals.push_back(raw_id(idx)); // constant ids are encoded as literal indices here.
			}
			fn_.body.push_back(std::move(insert));
			// Rebind the local SSA to the new value.
			auto& slot = locals_[local_name_for(local.id)];
			(void)slot;
			return;
		}
		// Pointer path: OpAccessChain + OpStore.
		const ID<IRInstruction> ptr_ty = types_.pointer_to(cur_type, StorageClass::Function);
		IRInstruction chain;
		chain.op = IROp::AccessChain;
		chain.result_id = builder_.fresh_id();
		chain.type_id = ptr_ty;
		chain.operands = { local.id };
		for (auto idx : indices)
			chain.operands.push_back(idx);
		const ID<IRInstruction> chain_id = chain.result_id;
		fn_.body.push_back(std::move(chain));

		IRInstruction store;
		store.op = IROp::Store;
		store.operands = { chain_id, v.id };
		fn_.body.push_back(std::move(store));
	}

	// Reverse lookup is only used to update the SSA binding of a value local
	// after a CompositeInsert. Linear scan is fine — locals_ is small.
	[[nodiscard]] std::string local_name_for(ID<IRInstruction> id) const {
		for (const auto& [name, info] : locals_) {
			if (info.id == id)
				return name;
		}
		return {};
	}

	void lower_return(const Decl::Expr& expr) {
		if (expr.kind == Decl::Expr::Kind::unknown) {
			IRInstruction r;
			r.op = IROp::Return;
			fn_.body.push_back(std::move(r));
			return;
		}
		const Value v = lower_expr(expr);
		if (v.id == ID<IRInstruction>{})
			return;
		IRInstruction r;
		r.op = IROp::ReturnValue;
		r.operands = { v.id };
		fn_.body.push_back(std::move(r));
	}

	Value lower_expr(const Decl::Expr& expr) {
		switch (expr.kind) {
		case Decl::Expr::Kind::name:
			return emit_name(expr.text);
		case Decl::Expr::Kind::literal_int: {
			Value v;
			v.id = constant_int(std::stoi(expr.text));
			v.type_id = types_.find("i32");
			return v;
		}
		case Decl::Expr::Kind::literal_float: {
			Value v;
			v.id = constant_float(std::stof(expr.text));
			v.type_id = types_.find("f32");
			return v;
		}
		case Decl::Expr::Kind::literal_bool: {
			Value v;
			v.id = constant_bool(expr.text == "1");
			v.type_id = types_.find("bool");
			return v;
		}
		case Decl::Expr::Kind::unary:
			if (!expr.children.empty()) {
				const Value child = lower_expr(expr.children.front());
				if (child.id == ID<IRInstruction>{})
					return {};
				if (expr.op == "-")
					return emit_negate(child);
				if (expr.op == "!")
					return emit_unop(IROp::LogicalNot, child, types_.find("bool"));
				if (expr.op == "+")
					return child;
				builder_.diagnose(std::format("unsupported unary operator '{}'", expr.op));
				return {};
			}
			return {};
		case Decl::Expr::Kind::binary:
			if (expr.children.size() == 2) {
				const Value lhs = lower_expr(expr.children[0]);
				const Value rhs = lower_expr(expr.children[1]);
				if (lhs.id == ID<IRInstruction>{} || rhs.id == ID<IRInstruction>{})
					return {};
				if (expr.op == "+")
					return emit_binop(IROp::FAdd, lhs, rhs);
				if (expr.op == "-")
					return emit_binop(IROp::FSub, lhs, rhs);
				if (expr.op == "*")
					return emit_mul_like(TokKind::star, lhs, rhs);
				if (expr.op == "/")
					return emit_mul_like(TokKind::slash, lhs, rhs);
				if (expr.op == "%")
					return emit_mul_like(TokKind::percent, lhs, rhs);
				if (expr.op == "&&")
					return emit_binop_typed(IROp::LogicalAnd, lhs, rhs, types_.find("bool"));
				if (expr.op == "||")
					return emit_binop_typed(IROp::LogicalOr, lhs, rhs, types_.find("bool"));
				if (const auto cmp = comparison_op(expr.op, lhs.type_id))
					return emit_binop_typed(*cmp, lhs, rhs, types_.find("bool"));
				builder_.diagnose(std::format("unsupported binary operator '{}'", expr.op));
			}
			return {};
		case Decl::Expr::Kind::call:
			if (!expr.children.empty()) {
				std::vector<Value> args;
				for (std::size_t i = 1; i < expr.children.size(); ++i) {
					args.push_back(lower_expr(expr.children[i]));
					if (args.back().id == ID<IRInstruction>{})
						return {};
				}
				return emit_call(expr.children.front().text, args);
			}
			return {};
		case Decl::Expr::Kind::member:
			if (!expr.children.empty()) {
				if (expr.op == "::") {
					if (const auto qualified = qualified_name_from_expr(expr); !qualified.empty()) {
						return emit_name(qualified);
					}
				}
				return emit_member_access(lower_expr(expr.children.front()), expr.text);
			}
			return {};
		case Decl::Expr::Kind::unknown:
			return {};
		}
		return {};
	}

	static std::string qualified_name_from_expr(const Decl::Expr& expr) {
		std::vector<std::string_view> parts;
		if (!collect_qualified_name_parts(expr, parts)) {
			return {};
		}
		std::string name;
		for (std::size_t i = 0; i < parts.size(); ++i) {
			if (i != 0) {
				name += "::";
			}
			name += parts[i];
		}
		return name;
	}

	static bool collect_qualified_name_parts(const Decl::Expr& expr, std::vector<std::string_view>& parts) {
		if (expr.kind == Decl::Expr::Kind::name) {
			parts.push_back(expr.text);
			return true;
		}
		if (expr.kind != Decl::Expr::Kind::member || expr.op != "::" || expr.children.size() != 1) {
			return false;
		}
		if (!collect_qualified_name_parts(expr.children.front(), parts)) {
			return false;
		}
		parts.push_back(expr.text);
		return true;
	}

	[[nodiscard]] static std::optional<u32> vector_component(std::string_view name) {
		if (name.size() != 1) {
			return std::nullopt;
		}
		switch (name[0]) {
		case 'x':
		case 'r':
		case 's':
			return 0;
		case 'y':
		case 'g':
		case 't':
			return 1;
		case 'z':
		case 'b':
		case 'p':
			return 2;
		case 'w':
		case 'a':
		case 'q':
			return 3;
		default:
			return std::nullopt;
		}
	}

	ID<IRInstruction> constant_uint(u32 value) {
		const std::string sig = "cu:" + std::to_string(value);
		const ID<IRInstruction> ty = types_.find("u32");
		return builder_.intern_constant(sig, IROp::ConstantUInt, ty, {}, { value });
	}

	ID<IRInstruction> constant_int(i32 value) {
		const std::string sig = "ci:" + std::to_string(value);
		const ID<IRInstruction> ty = types_.find("i32");
		return builder_.intern_constant(sig, IROp::ConstantInt, ty, {}, { static_cast<u32>(value) });
	}

	ID<IRInstruction> constant_float(float value) {
		u32 bits;
		std::memcpy(&bits, &value, sizeof(bits));
		const std::string sig = "cf:" + std::to_string(bits);
		const ID<IRInstruction> ty = types_.find("f32");
		return builder_.intern_constant(sig, IROp::ConstantFloat, ty, {}, { bits });
	}

	ID<IRInstruction> constant_bool(bool value) {
		const std::string sig = value ? "cb:1" : "cb:0";
		const ID<IRInstruction> ty = types_.find("bool");
		return builder_.intern_constant(sig, IROp::ConstantBool, ty, {}, { value ? 1u : 0u });
	}

	// Select the comparison opcode for `op` based on the operand's scalar kind
	// (float ordered, signed, or unsigned). Vector operands compare per element,
	// so the element kind drives the choice. Returns nullopt for non-comparison
	// operators.
	[[nodiscard]] std::optional<IROp> comparison_op(std::string_view op, ID<IRInstruction> operand_type) const {
		TypeInfo::Kind kind = TypeInfo::Kind::Float;
		if (const TypeInfo* info = types_.info_by_id(operand_type)) {
			kind = info->kind == TypeInfo::Kind::Vector
					   ? scalar_kind_of(info->element_type_id)
					   : info->kind;
		}
		const bool is_float = kind == TypeInfo::Kind::Float;
		const bool is_uint = kind == TypeInfo::Kind::UInt;
		if (op == "==") return is_float ? IROp::FOrdEqual : IROp::IEqual;
		if (op == "!=") return is_float ? IROp::FOrdNotEqual : IROp::INotEqual;
		if (op == "<") return is_float ? IROp::FOrdLess : (is_uint ? IROp::ULess : IROp::SLess);
		if (op == "<=") return is_float ? IROp::FOrdLessEqual : (is_uint ? IROp::ULessEqual : IROp::SLessEqual);
		if (op == ">") return is_float ? IROp::FOrdGreater : (is_uint ? IROp::UGreater : IROp::SGreater);
		if (op == ">=") return is_float ? IROp::FOrdGreaterEqual : (is_uint ? IROp::UGreaterEqual : IROp::SGreaterEqual);
		return std::nullopt;
	}

	[[nodiscard]] TypeInfo::Kind scalar_kind_of(ID<IRInstruction> type_id) const {
		const TypeInfo* info = types_.info_by_id(type_id);
		return info ? info->kind : TypeInfo::Kind::Float;
	}

	// Name lookup that handles locals, parameters, struct field names (when
	// shadowed by the implicit `this`), and global uniform variables.
	Value emit_name(std::string_view name) {
		// Local / parameter.
		if (const auto it = locals_.find(name); it != locals_.end()) {
			if (it->second.is_pointer) {
				IRInstruction load;
				load.op = IROp::Load;
				load.result_id = builder_.fresh_id();
				load.type_id = it->second.type_id;
				load.operands = { it->second.id };
				fn_.body.push_back(std::move(load));
				return Value{ load.result_id, it->second.type_id };
			}
			return Value{ it->second.id, it->second.type_id };
		}
		// Implicit struct field reference inside a constructor body: turn
		// `position` into `this.position`. The lowerer emits the field's
		// initial Load — for SSA constructors we model `this` as a function
		// local variable.
		if (const auto this_it = locals_.find("this"); this_it != locals_.end()) {
			const TypeInfo* info = types_.info_by_id(this_it->second.type_id);
			if (info) {
				for (u32 i = 0; i < info->members.size(); ++i) {
					if (info->members[i].first == name) {
						return emit_member_load_from_pointer(this_it->second, i, info->members[i].second);
					}
				}
			}
		}
		// Global uniform. The parser captures expression text into Decl::Expr
		// before any uniform-name mangling, so `transform` reaches us as its
		// source identifier rather than `u_..._h..`. Translate it here by
		// matching against reflected uniforms; the mangled form is also accepted
		// so that downstream code paths that go through lower_uniform_references
		// continue to work.
		std::string lookup_name(name);
		if (uniform_var_ids_.find(lookup_name) == uniform_var_ids_.end()) {
			if (const auto imported = using_uniforms_.find(name); imported != using_uniforms_.end()) {
				lookup_name = imported->second;
			}
		}
		if (uniform_var_ids_.find(lookup_name) == uniform_var_ids_.end()) {
			for (const auto& u : uniforms_) {
				if (u.name == name && (u.is_anonymous || u.scope_name.empty())) {
					lookup_name = uniform_binding_name(u);
					break;
				}
				if (!u.scope_name.empty() && std::format("{}::{}", u.scope_name, u.name) == name) {
					lookup_name = uniform_binding_name(u);
					break;
				}
			}
		}
		if (const auto vit = uniform_var_ids_.find(lookup_name); vit != uniform_var_ids_.end()) {
			const ID<IRInstruction> var_id = vit->second;
			const ID<IRInstruction> ptr_ty = uniform_var_type_ids_.at(lookup_name);
			const ID<IRInstruction> value_ty = uniform_value_type_ids_.at(lookup_name);
			// Find the physical pointee and storage class by scanning the small
			// compile-time type pool.
			ID<IRInstruction> pointee_ty = ID<IRInstruction>{};
			StorageClass storage = StorageClass::Uniform;
			for (const auto& inst : builder_.module().type_constant_pool) {
				if (inst.op == IROp::TypePointer && inst.result_id == ptr_ty) {
					pointee_ty = inst.operands.front();
					storage = static_cast<StorageClass>(inst.literals.front());
					break;
				}
			}
			ID<IRInstruction> load_from = var_id;
			if (pointee_ty != value_ty) {
				IRInstruction chain;
				chain.op = IROp::AccessChain;
				chain.result_id = builder_.fresh_id();
				chain.type_id = types_.pointer_to(value_ty, storage);
				chain.operands = { var_id, constant_uint(0) };
				load_from = chain.result_id;
				fn_.body.push_back(std::move(chain));
			}
			IRInstruction load;
			load.op = IROp::Load;
			load.result_id = builder_.fresh_id();
			load.type_id = value_ty;
			load.operands = { load_from };
			fn_.body.push_back(std::move(load));
			return Value{ load.result_id, value_ty };
		}
		builder_.diagnose(std::format("unknown name '{}'", name));
		return Value{ ID<IRInstruction>{}, ID<IRInstruction>{} };
	}

	Value emit_member_load_from_pointer(const Local& local, u32 index, ID<IRInstruction> member_type) {
		const ID<IRInstruction> ptr_ty = types_.pointer_to(member_type, StorageClass::Function);
		IRInstruction chain;
		chain.op = IROp::AccessChain;
		chain.result_id = builder_.fresh_id();
		chain.type_id = ptr_ty;
		chain.operands = { local.id, constant_uint(index) };
		const ID<IRInstruction> chain_id = chain.result_id;
		fn_.body.push_back(std::move(chain));

		IRInstruction load;
		load.op = IROp::Load;
		load.result_id = builder_.fresh_id();
		load.type_id = member_type;
		load.operands = { chain_id };
		fn_.body.push_back(std::move(load));
		return Value{ load.result_id, member_type };
	}

	Value emit_member_access(const Value& base, std::string_view name) {
		const TypeInfo* info = types_.info_by_id(base.type_id);
		if (!info) {
			return base;
		}
		if (info->kind == TypeInfo::Kind::Vector) {
			std::vector<u32> indices;
			indices.reserve(name.size());
			for (const char c : name) {
				const auto idx = vector_component(std::string_view{ &c, 1 });
				if (!idx) {
					builder_.diagnose(std::format("invalid swizzle '{}' on type '{}'",
												  name, types_.name_by_id(base.type_id)));
					return base;
				}
				indices.push_back(*idx);
			}
			// A single component extracts a scalar; multiple components (including
			// reordering, e.g. `.zyx`) produce a smaller/permuted vector.
			if (indices.size() == 1) {
				IRInstruction extract;
				extract.op = IROp::CompositeExtract;
				extract.result_id = builder_.fresh_id();
				extract.type_id = info->element_type_id;
				extract.operands = { base.id };
				extract.literals = { indices.front() };
				fn_.body.push_back(std::move(extract));
				return Value{ extract.result_id, info->element_type_id };
			}
			const ID<IRInstruction> result_ty = types_.vector_of(info->element_type_id, static_cast<u32>(indices.size()));
			IRInstruction shuffle;
			shuffle.op = IROp::VectorShuffle;
			shuffle.result_id = builder_.fresh_id();
			shuffle.type_id = result_ty;
			shuffle.operands = { base.id, base.id };
			shuffle.literals = std::move(indices);
			fn_.body.push_back(std::move(shuffle));
			return Value{ shuffle.result_id, result_ty };
		}
		if (info->kind == TypeInfo::Kind::Struct) {
			for (u32 i = 0; i < info->members.size(); ++i) {
				if (info->members[i].first == name) {
					IRInstruction extract;
					extract.op = IROp::CompositeExtract;
					extract.result_id = builder_.fresh_id();
					extract.type_id = info->members[i].second;
					extract.operands = { base.id };
					extract.literals = { i };
					fn_.body.push_back(std::move(extract));
					return Value{ extract.result_id, info->members[i].second };
				}
			}
			builder_.diagnose(std::format("no member named '{}' in type '{}'",
										  name, types_.name_by_id(base.type_id)));
		}
		return base;
	}

	Value emit_call(std::string_view callee, std::span<const Value> args) {
		// Constructor of a known type.
		if (const ID<IRInstruction> type_id = types_.find(callee); type_id) {
			// Type construction is structural: first try a declared member-init
			// function, then fall back to aggregate construction only when the
			// argument list exactly matches the type's fields.
			if (const Value inlined = try_inline_constructor(type_id, args); inlined.id != ID<IRInstruction>{}) {
				return inlined;
			}
			if (const TypeInfo* info = types_.info_by_id(type_id); info && info->kind == TypeInfo::Kind::Struct) {
				if (info->members.size() != args.size()) {
					builder_.diagnose(std::format("cannot construct '{}' from {} argument(s)",
												  callee, args.size()));
					return Value{};
				}
				for (std::size_t i = 0; i < args.size(); ++i) {
					if (info->members[i].second != args[i].type_id) {
						builder_.diagnose(std::format("argument {} does not match field '{}' while constructing '{}'",
													  i, info->members[i].first, callee));
						return Value{};
					}
				}
			}
			IRInstruction construct;
			construct.op = IROp::CompositeConstruct;
			construct.result_id = builder_.fresh_id();
			construct.type_id = type_id;
			construct.operands.reserve(args.size());
			for (const auto& a : args)
				construct.operands.push_back(a.id);
			fn_.body.push_back(std::move(construct));
			return Value{ fn_.body.back().result_id, type_id };
		}
		// Texture / sample primitive.
		if (callee == "sample" && args.size() == 2) {
			IRInstruction inst;
			inst.op = IROp::ImageSampleImplicitLod;
			inst.result_id = builder_.fresh_id();
			inst.type_id = types_.find("vec4");
			inst.operands = { args[0].id, args[1].id };
			fn_.body.push_back(std::move(inst));
			return Value{ inst.result_id, types_.find("vec4") };
		}
		// User function: emit a generic FunctionCall referencing the callee by
		// name. The single-module inliner (or the linker, for cross-module
		// calls) rewrites operand[0] to the resolved function's result_id and
		// ultimately inlines the body so the backend never sees a FunctionCall
		// for a user function.
		if (!builder_.is_known_callable(callee)) {
			builder_.diagnose(std::format("unknown callable '{}'", callee));
			return Value{};
		}
		IRInstruction call;
		call.op = IROp::FunctionCall;
		call.result_id = builder_.fresh_id();
		// operand[0] is the resolved function id (0 = unresolved until the
		// inliner runs); operand[1..N] are the argument values.
		call.operands.reserve(args.size() + 1);
		call.operands.push_back(ID<IRInstruction>{});
		for (const auto& a : args) {
			call.operands.push_back(a.id);
		}
		// literal[0] indexes into IRModule::call_targets. Cleared by the
		// inliner once every call in the module is resolved.
		std::vector<std::string_view> argument_types;
		std::vector<std::string> argument_type_storage;
		argument_types.reserve(args.size());
		argument_type_storage.reserve(args.size());
		for (const auto& arg : args) {
			argument_type_storage.emplace_back(types_.name_by_id(arg.type_id));
			argument_types.push_back(argument_type_storage.back());
		}
		const CallableTarget* target = builder_.resolve_call_target(callee, argument_type_storage);
		auto& targets = builder_.module().call_targets;
		const u32 name_index = static_cast<u32>(targets.size());
		targets.push_back(IRCallTarget{
			.display_name = std::string(callee),
			.mangled_name = target ? target->mangled_name : mangle_rtsl(callee, {}, argument_types),
		});
		call.literals = { name_index };
		fn_.body.push_back(std::move(call));
		return Value{ call.result_id, ID<IRInstruction>{} };
	}

	// If a previously-lowered IRFunction matches `Type::Type(args...)` for
	// this type, inline its body into the current function. The constructor
	// body's parameter ids are remapped to the call args' ids, and each of
	// its result ids is replaced with a fresh id local to this function. The
	// constructor's terminating ReturnValue is consumed: its operand becomes
	// the call's result. Returns Value{} on no match.
	[[nodiscard]] Value try_inline_constructor(ID<IRInstruction> type_id, std::span<const Value> args) {
		const auto& functions = builder_.module().functions;
		const IRFunction* ctor = nullptr;
		for (const auto& candidate : functions) {
			if (!candidate.stage.empty())
				continue;
			if (!candidate.is_constructor())
				continue;
			if (candidate.return_type_id != type_id)
				continue;
			if (candidate.parameter_ids.size() != args.size())
				continue;
			bool params_match = true;
			for (std::size_t i = 0; i < args.size(); ++i) {
				const ID<IRInstruction> param_id = candidate.parameter_ids[i];
				auto param_it = std::find_if(candidate.body.begin(), candidate.body.end(),
					[&](const IRInstruction& inst) { return inst.result_id == param_id; });
				if (param_it == candidate.body.end() || param_it->type_id != args[i].type_id) {
					params_match = false;
					break;
				}
			}
			if (!params_match)
				continue;
			ctor = &candidate;
			break;
		}
		if (!ctor)
			return Value{};

		std::unordered_map<ID<IRInstruction>, ID<IRInstruction>> remap;
		for (std::size_t i = 0; i < ctor->parameter_ids.size() && i < args.size(); ++i) {
			remap[ctor->parameter_ids[i]] = args[i].id;
		}

		Value result;
		for (const auto& inst : ctor->body) {
			if (inst.op == IROp::Label)
				continue;
			if (inst.op == IROp::FunctionParameter)
				continue;
			if (inst.op == IROp::Return)
				continue;
			if (inst.op == IROp::ReturnValue) {
				if (!inst.operands.empty()) {
					const ID<IRInstruction> operand = inst.operands.front();
					auto it = remap.find(operand);
					result.id = it != remap.end() ? it->second : operand;
					result.type_id = ctor->return_type_id;
				}
				continue;
			}
			IRInstruction copy = inst;
			if (copy.result_id != ID<IRInstruction>{}) {
				const ID<IRInstruction> fresh = builder_.fresh_id();
				remap[copy.result_id] = fresh;
				copy.result_id = fresh;
			}
			for (auto& operand : copy.operands) {
				if (auto it = remap.find(operand); it != remap.end())
					operand = it->second;
			}
			fn_.body.push_back(std::move(copy));
		}
		if (!result.id)
			return Value{};
		return result;
	}

	Value emit_binop(IROp op, const Value& lhs, const Value& rhs) {
		IRInstruction inst;
		inst.op = op;
		inst.result_id = builder_.fresh_id();
		inst.type_id = lhs.type_id ? lhs.type_id : rhs.type_id;
		inst.operands = { lhs.id, rhs.id };
		fn_.body.push_back(std::move(inst));
		return Value{ inst.result_id, inst.type_id };
	}

	Value emit_negate(const Value& v) {
		IRInstruction inst;
		inst.op = IROp::FNegate;
		inst.result_id = builder_.fresh_id();
		inst.type_id = v.type_id;
		inst.operands = { v.id };
		fn_.body.push_back(std::move(inst));
		return Value{ inst.result_id, v.type_id };
	}

	Value emit_unop(IROp op, const Value& v, ID<IRInstruction> result_ty) {
		IRInstruction inst;
		inst.op = op;
		inst.result_id = builder_.fresh_id();
		inst.type_id = result_ty;
		inst.operands = { v.id };
		fn_.body.push_back(std::move(inst));
		return Value{ inst.result_id, result_ty };
	}

	Value emit_mul_like(TokKind op, const Value& lhs, const Value& rhs) {
		// Pick the right SPIR-V-ish opcode based on the operand types.
		const TypeInfo* lhs_info = types_.info_by_id(lhs.type_id);
		const TypeInfo* rhs_info = types_.info_by_id(rhs.type_id);
		if (op == TokKind::star && lhs_info && rhs_info) {
			if (lhs_info->kind == TypeInfo::Kind::Matrix && rhs_info->kind == TypeInfo::Kind::Vector) {
				return emit_binop_typed(IROp::MatrixTimesVector, lhs, rhs, rhs.type_id);
			}
			if (lhs_info->kind == TypeInfo::Kind::Matrix && rhs_info->kind == TypeInfo::Kind::Matrix) {
				return emit_binop_typed(IROp::MatrixTimesMatrix, lhs, rhs, lhs.type_id);
			}
			if (lhs_info->kind == TypeInfo::Kind::Vector && rhs_info->kind == TypeInfo::Kind::Float) {
				return emit_binop_typed(IROp::VectorTimesScalar, lhs, rhs, lhs.type_id);
			}
		}
		IROp ir_op = IROp::FMul;
		if (op == TokKind::slash)
			ir_op = IROp::FDiv;
		else if (op == TokKind::percent)
			ir_op = IROp::FMod;
		return emit_binop(ir_op, lhs, rhs);
	}

	Value emit_binop_typed(IROp op, const Value& lhs, const Value& rhs, ID<IRInstruction> result_ty) {
		IRInstruction inst;
		inst.op = op;
		inst.result_id = builder_.fresh_id();
		inst.type_id = result_ty;
		inst.operands = { lhs.id, rhs.id };
		fn_.body.push_back(std::move(inst));
		return Value{ inst.result_id, result_ty };
	}

	IRBuilder& builder_;
	TypeRegistry& types_;
	IRFunction& fn_;
	std::span<const UniformBinding> uniforms_;
	const StringMap<std::string>& using_uniforms_;
	const StringMap<ID<IRInstruction>>& uniform_var_ids_;
	const StringMap<ID<IRInstruction>>& uniform_var_type_ids_;
	const StringMap<ID<IRInstruction>>& uniform_value_type_ids_;
	std::unordered_map<std::string, Local, TransparentStringHash, std::equal_to<>> locals_;

	// Constructor lowering state. Active when the current function is a
	// member-init body for `Type::Type(...)`. See begin_constructor.
	std::string ctor_owner_name_;
	ID<IRInstruction> ctor_owner_type_id_ = ID<IRInstruction>{};
	std::vector<ID<IRInstruction>> ctor_field_values_;
};

// Resolve every IROp::FunctionCall against the functions defined in this
// module by inlining the callee's body in place. The IR pipeline doesn't
// preserve a FunctionCall instruction for user functions: backends only see
// straight-line code plus reserved primitive ops. Constructors are already
// inlined at lower-call time; this pass handles arbitrary user-to-user calls
// and runs to a fixed point so chains of inlined calls expand fully.
//
// Calls whose target lives in another translation unit (no match in this
// module's functions) are left alone: the linker handles cross-module
// resolution in a later pass over rtslo/rtsll inputs.
bool inline_one_pass(IRFunction& fn, IRModule& ir, IRBuilder& builder) {
	bool progress = false;
	std::vector<IRInstruction> new_body;
	new_body.reserve(fn.body.size());
	// Maps the FunctionCall's result_id to the inlined callee's returned
	// value id, so subsequent instructions in fn that referenced the call
	// result get rewired to the actual return value.
	std::unordered_map<ID<IRInstruction>, ID<IRInstruction>> call_result_remap;
	const auto apply_remap = [&](IRInstruction& inst) {
		for (auto& op : inst.operands) {
			if (auto it = call_result_remap.find(op); it != call_result_remap.end())
				op = it->second;
		}
	};

	for (const auto& inst : fn.body) {
		if (inst.op != IROp::FunctionCall || inst.operands.empty() || inst.literals.empty()) {
			IRInstruction copy = inst;
			apply_remap(copy);
			new_body.push_back(std::move(copy));
			continue;
		}
		const u32 name_index = inst.literals[0];
		if (name_index >= ir.call_targets.size()) {
			IRInstruction copy = inst;
			apply_remap(copy);
			new_body.push_back(std::move(copy));
			continue;
		}
		const IRCallTarget& callee = ir.call_targets[name_index];
		const std::size_t arg_count = inst.operands.size() - 1; // operand[0] is the (unresolved) target id slot

		const IRFunction* target = nullptr;
		for (const auto& candidate : ir.functions) {
			if (&candidate == &fn)
				continue;
			if (candidate.link_name != callee.mangled_name)
				continue;
			if (candidate.parameter_ids.size() != arg_count)
				continue;
			// A forward declaration with no body cannot be inlined. Skip it
			// so the search continues to the real definition, which may live
			// later in this module or in another module the linker provides.
			if (candidate.body.empty())
				continue;
			target = &candidate;
			break;
		}
		if (!target) {
			// Leave for the linker. Apply the running remap so any prior
			// inlined-call result substitutions still propagate.
			IRInstruction copy = inst;
			apply_remap(copy);
			new_body.push_back(std::move(copy));
			continue;
		}

		std::unordered_map<ID<IRInstruction>, ID<IRInstruction>> local_remap;
		for (std::size_t i = 0; i < target->parameter_ids.size(); ++i) {
			ID<IRInstruction> arg_id = inst.operands[i + 1];
			if (auto it = call_result_remap.find(arg_id); it != call_result_remap.end())
				arg_id = it->second;
			local_remap[target->parameter_ids[i]] = arg_id;
		}

		ID<IRInstruction> returned_id = ID<IRInstruction>{};
		for (const auto& body_inst : target->body) {
			if (body_inst.op == IROp::Label)
				continue;
			if (body_inst.op == IROp::FunctionParameter)
				continue;
			if (body_inst.op == IROp::Return)
				continue;
			if (body_inst.op == IROp::ReturnValue) {
				if (!body_inst.operands.empty()) {
					ID<IRInstruction> ret = body_inst.operands[0];
					if (auto it = local_remap.find(ret); it != local_remap.end())
						ret = it->second;
					returned_id = ret;
				}
				continue;
			}
			IRInstruction copy = body_inst;
			if (copy.result_id != ID<IRInstruction>{}) {
				const ID<IRInstruction> fresh = builder.fresh_id();
				local_remap[copy.result_id] = fresh;
				copy.result_id = fresh;
			}
			for (auto& operand : copy.operands) {
				if (auto it = local_remap.find(operand); it != local_remap.end())
					operand = it->second;
			}
			new_body.push_back(std::move(copy));
		}
		if (inst.result_id != ID<IRInstruction>{} && returned_id != ID<IRInstruction>{}) {
			call_result_remap[inst.result_id] = returned_id;
		}
		progress = true;
	}

	fn.body = std::move(new_body);
	return progress;
}

void inline_resolved_calls(IRModule& ir, IRBuilder& builder) {
	// Bounded fixed-point: each pass either makes progress or it doesn't, and
	// legal RTSL forbids recursion. The cap stops a malformed module from
	// looping forever.
	for (int iteration = 0; iteration < 64; ++iteration) {
		bool any_progress = false;
		for (auto& fn : ir.functions) {
			if (inline_one_pass(fn, ir, builder))
				any_progress = true;
		}
		if (!any_progress)
			break;
	}
	// Once nothing is left to inline against this module, drop the side
	// any surviving FunctionCalls are cross-module and will get their names
	// from the artifact's strings payload after linking.
	if (ir.call_targets.empty())
		return;
	bool any_unresolved = false;
	for (const auto& fn : ir.functions) {
		for (const auto& inst : fn.body) {
			if (inst.op == IROp::FunctionCall) {
				any_unresolved = true;
				break;
			}
		}
		if (any_unresolved)
			break;
	}
	if (!any_unresolved) {
		ir.call_targets.clear();
	}
}

// ----------------------------------------------------------------------------
// Resource buffer layout: field offsets, matrix strides, and total size for
// std140, std430, and scalar layout.
// ----------------------------------------------------------------------------

// Parsed resource-layout type. `column_count == 0` means non-matrix.
struct TypeShape {
	enum class Kind : u08 { Unknown,
						   Scalar,
						   Vector,
						   Matrix } kind = Kind::Unknown;
	u32 scalar_size = 0;
	u32 components = 0;	  // vec2 → 2, vec3 → 3, vec4 → 4, mat4 → 4 (rows)
	u32 column_count = 0; // matN → N columns; 0 for non-matrix
};

[[nodiscard]] TypeShape classify_type_shape(std::string_view type) {
	if (type == "f32" || type == "i32" || type == "u32" || type == "bool") {
		return { TypeShape::Kind::Scalar, 4, 1, 0 };
	}
	// Vectors.
	if (type == "vec2" || type == "ivec2" || type == "uvec2")
		return { TypeShape::Kind::Vector, 4, 2, 0 };
	if (type == "vec3" || type == "ivec3" || type == "uvec3")
		return { TypeShape::Kind::Vector, 4, 3, 0 };
	if (type == "vec4" || type == "ivec4" || type == "uvec4")
		return { TypeShape::Kind::Vector, 4, 4, 0 };
	// Matrices — column-major, each column is a vecN.
	if (type == "mat2")
		return { TypeShape::Kind::Matrix, 4, 2, 2 };
	if (type == "mat3")
		return { TypeShape::Kind::Matrix, 4, 3, 3 };
	if (type == "mat4")
		return { TypeShape::Kind::Matrix, 4, 4, 4 };
	return {};
}

// Base alignment for a vector of `components` components, per the rule.
// std140 and std430 round vec3 up to vec4-alignment (16); scalar packs on
// component alignment (4).
[[nodiscard]] u32 vector_base_alignment(u32 components, u32 scalar_size, LayoutRule rule) {
	if (rule == LayoutRule::scalar)
		return scalar_size;
	if (components == 3)
		return scalar_size * 4;		 // vec3 gets vec4 alignment
	return scalar_size * components; // vec2 → 8, vec4 → 16
}

struct MemberLayout {
	u32 offset = 0; // byte offset from the start of the struct
	u32 base_alignment = 0;
	u32 consumed_size = 0;  // bytes advanced past `offset`
	u32 matrix_stride = 0;  // column stride for matrix members; 0 means "not a matrix"
};

struct BufferLayout {
	std::vector<MemberLayout> members;
	u32 alignment = 0; // struct's own base alignment (max of member alignments)
	u32 size = 0;	   // total struct size, padded up to `alignment`
};

// Round `value` up to a multiple of `align`. `align` must be non-zero.
[[nodiscard]] inline u32 align_up(u32 value, u32 align) {
	return (value + align - 1) & ~(align - 1);
}

[[nodiscard]] BufferLayout compute_buffer_layout(std::span<const StructField> fields, LayoutRule rule) {
	BufferLayout layout;
	layout.members.reserve(fields.size());
	u32 cursor = 0;
	for (const auto& field : fields) {
		MemberLayout m;
		const TypeShape shape = classify_type_shape(field.type);
		if (shape.kind == TypeShape::Kind::Unknown) {
			// Unknown/nested type — leave at cursor with zero size. Caller
			// sees size==0 and knows this member wasn't laid out.
			m.offset = cursor;
			layout.members.push_back(m);
			continue;
		}
		if (shape.kind == TypeShape::Kind::Scalar) {
			m.base_alignment = shape.scalar_size;
			m.consumed_size = shape.scalar_size;
		} else if (shape.kind == TypeShape::Kind::Vector) {
			m.base_alignment = vector_base_alignment(shape.components, shape.scalar_size, rule);
			m.consumed_size = shape.scalar_size * shape.components;
		} else {
			// Matrix: column-major array of column-vecN. Column stride is the
			// base alignment of a vecN under the rule. Total consumed size is
			// column_stride * column_count. A non-zero matrix_stride is what
			// marks this member as a matrix downstream.
			m.matrix_stride = vector_base_alignment(shape.components, shape.scalar_size, rule);
			m.base_alignment = m.matrix_stride;
			m.consumed_size = m.matrix_stride * shape.column_count;
		}
		cursor = align_up(cursor, m.base_alignment);
		m.offset = cursor;
		cursor += m.consumed_size;
		layout.alignment = layout.alignment > m.base_alignment ? layout.alignment : m.base_alignment;
		layout.members.push_back(m);
	}
	if (layout.alignment > 0) {
		layout.size = align_up(cursor, layout.alignment);
	} else {
		layout.size = cursor;
	}
	return layout;
}

[[nodiscard]] LayoutRule resolve_layout_rule(LayoutRule declared, bool is_ssbo) {
	if (declared != LayoutRule::unset)
		return declared;
	// Kind-driven defaults: UBOs to std140, SSBOs to std430. Matches GLSL's
	// implicit rules so users don't have to spell them out.
	return is_ssbo ? LayoutRule::std430 : LayoutRule::std140;
}

// ----------------------------------------------------------------------------
// Top-level lowering: build types, globals, decorations, then walk functions.
// ----------------------------------------------------------------------------

const StageInterface* find_interface(std::span<const StageInterface> interfaces, std::string_view type_name, StageRole role) {
	for (const auto& interface : interfaces) {
		if (interface.type_name == type_name && interface.role == role) {
			return &interface;
		}
	}
	return nullptr;
}

IRModule lower_to_ir(const SemanticModule& module, DiagnosticEngine* diagnostics) {
	IRModule ir;
	ir.source_name = module.source_name;
	ir.imports = module.imports;
	ir.imported_exports = module.imported_exports;
	ir.exports = module.exports;
	ir.structs = module.structs;
	// Authored varying interfaces (from `-> T : field(tag), ...`) arrive resolved
	// from sema. The vertex-input and fragment-output interfaces are derived
	// below from the entry signatures.
	ir.stage_interfaces = module.stage_interfaces;

	StringSet callable_names;
	std::vector<CallableTarget> callable_targets;
	for (const auto& symbol : module.symbols) {
		if (symbol.kind == DeclKind::function) {
			callable_names.insert(symbol.name);
			CallableTarget target;
			target.name = symbol.name;
			target.value_parameter_types.reserve(symbol.parameters.size());
			std::vector<std::string> parameter_identity_storage;
			std::vector<std::string_view> parameter_identities;
			parameter_identity_storage.reserve(symbol.parameters.size());
			parameter_identities.reserve(symbol.parameters.size());
			for (const auto& parameter : symbol.parameters) {
				target.value_parameter_types.push_back(parameter.type);
				parameter_identity_storage.push_back(parameter_identity(parameter));
				parameter_identities.push_back(parameter_identity_storage.back());
			}
			target.mangled_name = mangle_rtsl(symbol.name, symbol.stage, parameter_identities);
			callable_targets.push_back(std::move(target));
		}
	}
	for (const auto& export_symbol : module.imported_exports) {
		if (export_symbol.kind == "function") {
			callable_names.insert(export_symbol.name);
		}
	}

	IRBuilder builder{ ir, std::move(callable_names), std::move(callable_targets), diagnostics };
	TypeRegistry types{ builder };
	for (const auto& decl : module.structs) {
		types.register_struct(decl);
	}
	const auto report_unknown_type = [&](std::string_view where, std::string_view type) {
		if (diagnostics && !type.empty()) {
			diagnostics->report(DiagnosticCode::ir_invalid_stage_signature, DiagnosticSeverity::error, {}, module.source_name,
				std::format("unknown type '{}' in {}", type, where));
		}
	};
	const auto check_type = [&](std::string_view where, std::string_view type) {
		if (types.find(type) == ID<IRInstruction>{}) {
			report_unknown_type(where, type);
		}
	};
	for (const auto& decl : module.structs) {
		for (const auto& field : decl.fields) {
			check_type(std::format("struct '{}'", decl.name), field.type);
		}
	}
	for (const auto& symbol : module.symbols) {
		for (const auto& param : symbol.parameters) {
			check_type(std::format("parameter '{}' in '{}'", param.name, symbol.name), param.type);
		}
		if (!symbol.return_type.empty() && symbol.return_type != "void") {
			check_type(std::format("return type of '{}'", symbol.name), symbol.return_type);
		}
	}
	for (const auto& uniform : module.uniforms) {
		if (resource_binding_kind(uniform.type) != ResourceBindingKind::none) {
			continue;
		}
		check_type(std::format("uniform '{}'", uniform.name), uniform.type);
	}

	const auto resource_backend_type = [&](ResourceBindingKind kind, std::string_view spelling) -> ID<IRInstruction> {
		const ResourceTypeInfo* info = resource_type_info(spelling);
		if (!info) return ID<IRInstruction>{};
		switch (kind) {
		case ResourceBindingKind::sampler:
			return builder.intern_type(std::format("resource:{}", spelling), IROp::TypeSampler, ID<IRInstruction>{}, {}, {});
		case ResourceBindingKind::sampled_image: {
			const std::array image_operands{ types.find("f32") };
			const std::array image_literals{
				static_cast<u32>(info->image.dimension),
				info->image.arrayed ? 1u : 0u,
				static_cast<u32>(ir::ImageClass::sampled),
			};
			const ID<IRInstruction> image = builder.intern_type(std::format("resource-image:{}", spelling),
				IROp::TypeImage, ID<IRInstruction>{}, image_operands, image_literals);
			return builder.intern_type(std::format("resource:{}", spelling), IROp::TypeSampledImage,
				ID<IRInstruction>{}, { image }, {});
		}
		case ResourceBindingKind::image: {
			const std::array image_operands{ types.find("f32") };
			const std::array image_literals{
				static_cast<u32>(info->image.dimension),
				info->image.arrayed ? 1u : 0u,
				static_cast<u32>(ir::ImageClass::storage),
			};
			return builder.intern_type(std::format("resource:{}", spelling), IROp::TypeImage,
				ID<IRInstruction>{}, image_operands, image_literals);
		}
		default:
			return ID<IRInstruction>{};
		}
	};

	// Resolve `layout` declarations against uniform bindings by qualified
	// name. Every uniform sits at exactly one path in the (currently flat)
	// namespace tree: `scope::name` for a named-scope uniform, or `name` for
	// one declared in an anonymous `uniform { ... }` block. Layout paths are
	// resolved as absolute qualified names — no string-concatenation matching
	// on `::`, no ad-hoc split-into-scope-and-tail. A layout's path is a
	// sequence of identifiers; joining them gives you the qualified name of
	// the binding it refers to.
	//
	// Errors we distinguish:
	//   RTSL3102 — a UniformBuffer/StorageBuffer binding has no layout.
	//   RTSL3103 — the layout names a type spelling that doesn't exist.
	//   RTSL3104 — the layout's path names no known uniform.
	//   RTSL3105 — the path names a uniform, but its kind (sampler, image,
	//              plain value) does not accept a layout.
	//   RTSL3101 — two `layout` declarations for the same path give
	//              different types. Identical repeats are allowed (ODR).
	const auto qualified_name_of = [](const UniformBinding& u) {
		if (!u.is_anonymous && !u.scope_name.empty()) {
			return u.scope_name + "::" + u.name;
		}
		return u.name;
	};
	std::unordered_map<std::string, std::size_t> uniform_by_qualified;
	uniform_by_qualified.reserve(module.uniforms.size());
	for (std::size_t i = 0; i < module.uniforms.size(); ++i) {
		uniform_by_qualified.emplace(qualified_name_of(module.uniforms[i]), i);
	}

	const auto qualified_from_path = [](std::span<const std::string> path) {
		std::string qn;
		for (std::size_t p = 0; p < path.size(); ++p) {
			if (p)
				qn += "::";
			qn += path[p];
		}
		return qn;
	};
	const auto layouts_match = [](const LayoutDecl& a, const LayoutDecl& b) {
		if (a.is_inline_struct != b.is_inline_struct) return false;
		if (!a.is_inline_struct) return a.type_spelling == b.type_spelling;
		if (a.inline_fields.size() != b.inline_fields.size()) return false;
		for (std::size_t i = 0; i < a.inline_fields.size(); ++i) {
			if (a.inline_fields[i].type != b.inline_fields[i].type) return false;
			if (a.inline_fields[i].name != b.inline_fields[i].name) return false;
		}
		return true;
	};

	// First layout wins unless a later declaration conflicts with it.
	std::unordered_map<std::size_t, std::size_t> first_layout_for_uniform;
	for (std::size_t li = 0; li < module.layouts.size(); ++li) {
		const auto& layout = module.layouts[li];
		const auto qn = qualified_from_path(layout.path);

		const auto it = uniform_by_qualified.find(qn);
		if (it == uniform_by_qualified.end()) {
			if (diagnostics) {
				diagnostics->report(DiagnosticCode::layout_unknown_uniform, DiagnosticSeverity::error, layout.span.begin, module.source_name,
									std::format("layout refers to unknown uniform '{}'", qn));
			}
			continue;
		}
		const auto& target = module.uniforms[it->second];
		if (!is_buffer_binding(resource_binding_kind(target.type))) {
			if (diagnostics) {
				diagnostics->report(DiagnosticCode::layout_invalid_uniform_kind, DiagnosticSeverity::error, layout.span.begin, module.source_name,
									std::format("uniform '{}' has type {} and does not accept a layout", qn, target.type));
			}
			continue;
		}
		const auto [existing_it, inserted] = first_layout_for_uniform.emplace(it->second, li);
		if (!inserted) {
			const auto& existing = module.layouts[existing_it->second];
			if (!layouts_match(existing, layout) && diagnostics) {
				diagnostics->report(DiagnosticCode::layout_duplicate, DiagnosticSeverity::error, layout.span.begin, module.source_name,
									std::format("conflicting layout for '{}'", qn));
			}
		}
	}

	// Emit one global Variable per uniform with the right storage class and
	// a matching set/binding decoration pair. The mangled binding name is what
	// the SSA body references.
	StringMap<ID<IRInstruction>> uniform_var_ids;
	StringMap<ID<IRInstruction>> uniform_var_type_ids;
	StringMap<ID<IRInstruction>> uniform_value_type_ids;
	for (std::size_t uidx = 0; uidx < module.uniforms.size(); ++uidx) {
		const auto& uniform = module.uniforms[uidx];
		const std::string mangled = uniform_binding_name(uniform);
		const ResourceBindingKind binding_kind = resource_binding_kind(uniform.type);
		const bool is_ubo = binding_kind == ResourceBindingKind::uniform_buffer;
		const bool is_ssbo = binding_kind == ResourceBindingKind::storage_buffer;
		const bool needs_layout = is_buffer_binding(binding_kind);
		const bool is_resource = is_opaque_resource_binding(binding_kind);
		StorageClass sc = StorageClass::Uniform;
		if (is_resource)
			sc = StorageClass::UniformConstant;
		else if (is_ssbo)
			sc = StorageClass::StorageBuffer;

		ID<IRInstruction> value_ty = ID<IRInstruction>{};
		ID<IRInstruction> loaded_value_ty = ID<IRInstruction>{};
		std::optional<BufferLayout> buffer_layout;
		std::size_t buffer_field_count = 0;
		if (needs_layout) {
			const auto qn = qualified_name_of(uniform);
			const auto layout_it = first_layout_for_uniform.find(uidx);
			if (layout_it == first_layout_for_uniform.end()) {
				if (diagnostics) {
					diagnostics->report(DiagnosticCode::layout_missing_resource_type, DiagnosticSeverity::error, {}, module.source_name,
										std::format("{} '{}' has no layout", is_ubo ? "UniformBuffer" : "StorageBuffer", qn));
				}
				value_ty = types.find("void");
				loaded_value_ty = value_ty;
			} else {
				const auto& layout = module.layouts[layout_it->second];
				const LayoutRule rule = resolve_layout_rule(layout.rule, is_ssbo);
				if (layout.is_inline_struct) {
					value_ty = types.register_anon_struct(qn, layout.inline_fields);
					loaded_value_ty = value_ty;
					buffer_layout = compute_buffer_layout(layout.inline_fields, rule);
					buffer_field_count = layout.inline_fields.size();
				} else {
					loaded_value_ty = types.find(layout.type_spelling);
					if (loaded_value_ty == ID<IRInstruction>{} && diagnostics) {
						diagnostics->report(DiagnosticCode::layout_unknown_type, DiagnosticSeverity::error, layout.span.begin, module.source_name,
											std::format("unknown type '{}' in layout for '{}'", layout.type_spelling, qn));
						loaded_value_ty = types.find("void");
					}
					const auto declaration = std::ranges::find(module.structs, layout.type_spelling, &StructDecl::name);
					if (declaration != module.structs.end()) {
						value_ty = loaded_value_ty;
						buffer_layout = compute_buffer_layout(declaration->fields, rule);
						buffer_field_count = declaration->fields.size();
					} else if (loaded_value_ty) {
						StructField wrapper{ .type = layout.type_spelling, .name = "value" };
						const std::array fields{ wrapper };
						value_ty = types.register_anon_struct(qn + ":block", fields);
						buffer_layout = compute_buffer_layout(fields, rule);
						buffer_field_count = 1;
					} else {
						value_ty = loaded_value_ty;
					}
				}
			}
		} else {
			value_ty = is_resource ? resource_backend_type(binding_kind, uniform.type) : types.find(uniform.type);
			loaded_value_ty = value_ty;
		}
		const ID<IRInstruction> ptr_ty = types.pointer_to(value_ty, sc);
		IRInstruction var;
		var.op = IROp::Variable;
		var.result_id = builder.fresh_id();
		var.type_id = ptr_ty;
		var.literals.push_back(static_cast<u32>(sc));
		builder.add_global_variable(std::move(var));
		const ID<IRInstruction> var_id = ir.global_variables.back().result_id;
		uniform_var_ids[mangled] = var_id;
		uniform_var_type_ids[mangled] = ptr_ty;
		uniform_value_type_ids[mangled] = loaded_value_ty;

		const ResourceTypeInfo* resource_type = resource_type_info(uniform.type);
		Resource resource{
			.name = uniform.scope_name.empty() ? uniform.name : uniform.scope_name + "." + uniform.name,
			.kind = resource_type ? resource_type->kind : ResourceKind::uniform_buffer,
			.image = resource_type ? resource_type->image : ImageShape{},
			.access = static_cast<Access>(uniform.access),
			.descriptor = DescriptorBinding{ .set = uniform.set, .binding = uniform.member },
			.variable = var_id,
			.value_type = value_ty,
		};
		ir.resources.push_back(std::move(resource));

		builder.add_decoration({ .target = var_id, .kind = IRDecorationKind::DescriptorSet, .literals = { uniform.set } });
		builder.add_decoration({ .target = var_id, .kind = IRDecorationKind::Binding, .literals = { uniform.member } });

		// Buffer block decorations must stay on the value type and its fields.
		if (buffer_layout) {
			builder.add_decoration({
				.target = value_ty,
				.kind = IRDecorationKind::Block,
			});
			for (std::size_t m = 0; m < buffer_field_count && m < buffer_layout->members.size(); ++m) {
				const auto& ml = buffer_layout->members[m];
				const u32 member_index = static_cast<u32>(m);
				builder.add_decoration({
					.target = value_ty,
					.kind = IRDecorationKind::Offset,
					.member_index = member_index,
					.literals = { ml.offset },
				});
				if (ml.matrix_stride > 0) {
					builder.add_decoration({
						.target = value_ty,
						.kind = IRDecorationKind::MatrixStride,
						.member_index = member_index,
						.literals = { ml.matrix_stride },
					});
					builder.add_decoration({
						.target = value_ty,
						.kind = IRDecorationKind::ColMajor,
						.member_index = member_index,
					});
				}
			}
		}
	}

	StringMap<std::string> using_uniforms;
	const auto qualified_from_using_path = [](std::span<const std::string> path) {
		std::string qn;
		for (std::size_t i = 0; i < path.size(); ++i) {
			if (i) qn += "::";
			qn += path[i];
		}
		return qn;
	};
	for (const auto& use : module.using_imports) {
		if (use.kind == UsingImport::Kind::symbol) {
			const auto qn = qualified_from_using_path(use.path);
			if (const auto it = uniform_by_qualified.find(qn); it != uniform_by_qualified.end()) {
				using_uniforms[use.imported_name] = uniform_binding_name(module.uniforms[it->second]);
			}
			continue;
		}
		if (use.kind == UsingImport::Kind::namespace_scope) {
			const auto scope = qualified_from_using_path(use.path);
			for (const auto& uniform : module.uniforms) {
				if (!uniform.is_anonymous && uniform.scope_name == scope) {
					using_uniforms[uniform.name] = uniform_binding_name(uniform);
				}
			}
		}
	}

	// Walk the semantic symbols and lower function bodies.
	for (const auto& symbol : module.symbols) {
		if (symbol.kind != DeclKind::function)
			continue;
		// A forward declaration (`fn name(...) -> T;`) is there for source-level
		// name lookup only; the linker resolves it against another input. Skip
		// so the inliner doesn't pick a body-less entry over the real def.
		// An *empty body* (`{}`) is a distinct case — a valid zero-statement
		// function — and lowers normally.
		if (!symbol.has_body)
			continue;

		// Stage identity is resolved by sema from `@stage : identifier` and
		// forwarded as open metadata.
		const std::string_view stage = symbol.stage;
		// Detect a struct member-init constructor: a function named
		// "Type::Type" where Type is a known struct. The source declares no
		// return type for these; we promote the return type to the owning
		// struct so the inlined call site can use it like a normal expression.
		std::string ctor_owner;
		if (const auto scope = symbol.name.find("::"); scope != std::string::npos) {
			const std::string owner = symbol.name.substr(0, scope);
			const std::string method = symbol.name.substr(scope + 2);
			if (owner == method && types.find(owner)) {
				ctor_owner = owner;
			}
		}
		const bool is_constructor = !ctor_owner.empty();

		std::vector<ParameterDecl> parameters;
		parameters.reserve(symbol.parameters.size());
		for (const auto& p : symbol.parameters) {
			parameters.push_back(p);
		}

		IRFunction fn;
		fn.return_type_id = is_constructor ? types.find(ctor_owner) : types.find(symbol.return_type);
		fn.result_id = builder.fresh_id();
		fn.stage = stage;
		if (is_constructor) {
			fn.kind = IRFunction::Kind::constructor;
		} else if (symbol.exported) {
			fn.kind = IRFunction::Kind::exported;
		}
		std::vector<std::string> parameter_identity_storage;
		std::vector<std::string_view> parameter_types;
		parameter_identity_storage.reserve(parameters.size());
		parameter_types.reserve(parameters.size());
		for (const auto& parameter : parameters) {
			parameter_identity_storage.push_back(parameter_identity(parameter));
			parameter_types.push_back(parameter_identity_storage.back());
		}
		fn.link_name = mangle_rtsl(symbol.name, stage, parameter_types);
		fn.display_name = symbol.name;
		ir.functions.push_back(std::move(fn));

		FunctionLowerer lowerer{ builder, types, ir.functions.back(), module.uniforms, using_uniforms,
			uniform_var_ids, uniform_var_type_ids, uniform_value_type_ids };
		if (is_constructor) {
			lowerer.begin_constructor(ctor_owner);
		}
		lowerer.begin_entry_block();
		lowerer.emit_function_parameters(parameters);
		for (const auto& statement : symbol.body_statements) {
			// Uniform identifiers in body expressions are resolved by name
			// inside FunctionLowerer::emit_name against reflected uniforms;
			// no text-level mangling is needed here because the parser builds
			// a Decl::Expr ahead of any text substitution we could do.
			lowerer.lower_statement(statement);
		}
		if (is_constructor) {
			lowerer.emit_constructor_return();
		} else if (symbol.return_type.empty() || symbol.return_type == "void") {
			// Insert an implicit Return for void-returning functions that
			// didn't end on a terminator.
			const auto& body = ir.functions.back().body;
			const bool needs_ret = body.empty() ||
								   (body.back().op != IROp::Return &&
									body.back().op != IROp::ReturnValue &&
									body.back().op != IROp::Branch);
			if (needs_ret) {
				lowerer.emit_implicit_void_return();
			}
		}
	}

	// Derive the stage input/output interface metadata from the typed entry
	// signatures. RTSL has no input/output/varying globals: the return-boundary
	// grammar supplies varyings (already resolved into ir.stage_interfaces), and
	// the compiler derives the vertex-input and fragment-output interfaces here.
	// This is backend-neutral metadata only. RTIR carries no stage input,
	// output, or varying operations; a backend maps these records to its target
	// declarations.
	{
		// Synthesize an interface from a struct payload's fields when the source
		// didn't declare one. Fields get sequential locations and a member index
		// so a backend can extract/insert them.
		const auto synth_from_struct = [&](std::string_view type, StageRole role) {
			if (type.empty() || type == "void" || find_interface(ir.stage_interfaces, type, role)) {
				return;
			}
			for (const auto& decl : ir.structs) {
				if (decl.name != type) {
					continue;
				}
				StageInterface derived;
				derived.role = role;
				derived.type_name = std::string(type);
				u32 location = 0;
				for (u32 index = 0; index < decl.fields.size(); ++index) {
					StageIOField f;
					f.name = decl.fields[index].name;
					f.location = location++;
					f.member_index = index;
					derived.fields.push_back(std::move(f));
				}
				ir.stage_interfaces.push_back(std::move(derived));
				return;
			}
		};
		// A fragment input payload must carry a `varying` interface: its
		// interpolation qualifiers are authored, not derivable. That interface is
		// the vertex stage's return boundary (`-> V : field(tag), ...`).
		const auto require_varying = [&](std::string_view type) {
			if (type.empty() || type == "void" || find_interface(ir.stage_interfaces, type, StageRole::varying)) {
				return;
			}
			if (diagnostics) {
				diagnostics->report(DiagnosticCode::ir_lowering_failed, DiagnosticSeverity::error, {}, module.source_name,
									std::format("fragment input '{}' requires a varying interface; declare it on the vertex stage's return boundary", type));
			}
		};
		// Fragment sugar: `-> vec4` (a bare vector, not a struct) is a single color
		// output at location 0.
		const auto synth_bare_color_output = [&](std::string_view type) {
			if (type != "vec4" || find_interface(ir.stage_interfaces, type, StageRole::output)) {
				return;
			}
			StageInterface derived;
			derived.role = StageRole::output;
			derived.type_name = std::string(type);
			StageIOField f;
			f.name = "color";
			f.location = 0;
			derived.fields.push_back(std::move(f));
			ir.stage_interfaces.push_back(std::move(derived));
		};
		for (const auto& symbol : module.symbols) {
			if (symbol.kind != DeclKind::function || symbol.stage.empty() || !symbol.has_body) {
				continue;
			}
			const std::string_view input_type = symbol.parameters.empty() ? std::string_view{} : std::string_view{ symbol.parameters.front().type };
			if (is_vertex_stage(symbol.stage)) {
				synth_from_struct(input_type, StageRole::input);
			} else if (is_fragment_stage(symbol.stage)) {
				require_varying(input_type);
				synth_bare_color_output(symbol.return_type);
				synth_from_struct(symbol.return_type, StageRole::output);
			}
		}
	}

	const auto make_interface = [&](std::string_view type_name, StageRole role,
		std::optional<ir::Id> value) -> std::optional<Interface> {
		if (type_name.empty() || type_name == "void") return std::nullopt;
		const StageInterface* reflected = find_interface(ir.stage_interfaces, type_name, role);
		if (!reflected) return std::nullopt;

		const ID<IRInstruction> value_type = types.find(type_name);
		if (!value_type) return std::nullopt;
		const TypeInfo* type_info = types.info_by_id(value_type);

		Interface result{ .value_type = value_type, .value = value };
		result.elements.reserve(reflected->fields.size());
		for (const StageIOField& field : reflected->fields) {
			std::optional<u32> member;
			ID<IRInstruction> field_type = value_type;
			if (field.member_index != StageIOField::kNoMember) {
				member = field.member_index;
				if (type_info && field.member_index < type_info->members.size()) {
					field_type = type_info->members[field.member_index].second;
				}
			}
			std::optional<u32> location;
			if (field.location != StageIOField::kNoLocation) location = field.location;
			result.elements.push_back(InterfaceElement{
				.name = field.name,
				.type = field_type,
				.member = member,
				.location = location,
				.builtin = field.placement == StageFieldPlacement::clip_position
					? Builtin::position : Builtin::none,
				.interpolation = static_cast<Interpolation>(field.interpolation),
			});
		}
		return result;
	};

	for (const auto& symbol : module.symbols) {
		if (symbol.kind != DeclKind::function || symbol.stage.empty() || !symbol.has_body) continue;
		const auto function = std::find_if(ir.functions.begin(), ir.functions.end(), [&](const IRFunction& candidate) {
			return candidate.display_name == symbol.name && candidate.stage == symbol.stage;
		});
		if (function == ir.functions.end()) continue;

		const bool vertex = is_vertex_stage(symbol.stage);
		const std::string_view input_type = symbol.parameters.empty()
			? std::string_view{} : std::string_view{ symbol.parameters.front().type };
		const std::optional<ir::Id> input_value = function->parameter_ids.empty()
			? std::nullopt : std::optional<ir::Id>{ function->parameter_ids.front() };
		EntryPoint entry{
			.name = symbol.name,
			.stage = vertex ? Stage::vertex : Stage::fragment,
			.function = function->result_id,
			.input = make_interface(input_type, vertex ? StageRole::input : StageRole::varying, input_value),
			.output = make_interface(symbol.return_type, vertex ? StageRole::varying : StageRole::output, std::nullopt),
		};
		if (!vertex && entry.input) {
			std::erase_if(entry.input->elements, [](const InterfaceElement& element) {
				return element.builtin == Builtin::position;
			});
		}
		ir.entries.push_back(std::move(entry));
	}

	inline_resolved_calls(ir, builder);

	return ir;
}

bool verify_ir(const IRModule& module, DiagnosticEngine* diagnostics) {
	const auto fail = [&](std::string_view message) {
		if (diagnostics) {
			diagnostics->report(DiagnosticCode::ir_verification_failed, DiagnosticSeverity::error, {}, module.source_name, message);
		}
		return false;
	};

	// SSA invariant: every defined result id is unique across the whole module.
	std::unordered_set<ID<IRInstruction>> defined;
	const auto define = [&](ID<IRInstruction> id, std::string_view what) -> bool {
		if (id == ID<IRInstruction>{}) {
			return true;
		}
		if (!defined.insert(id).second) {
			return fail(std::format("duplicate SSA result id %{} ({})", raw_id(id), what));
		}
		return true;
	};
	for (const auto& inst : module.type_constant_pool) {
		if (!define(inst.result_id, "type/constant")) return false;
	}
	for (const auto& inst : module.global_variables) {
		if (!define(inst.result_id, "global")) return false;
	}
	for (const auto& fn : module.functions) {
		if (!define(fn.result_id, "function")) return false;
		for (const ID<IRInstruction> pid : fn.parameter_ids) {
			if (!define(pid, "parameter")) return false;
		}
		for (const auto& inst : fn.body) {
			// A parameter is listed in parameter_ids and also appears as a
			// FunctionParameter instruction in the body; count it once.
			if (inst.op == IROp::FunctionParameter) {
				continue;
			}
			if (!define(inst.result_id, "instruction")) return false;
		}
	}

	// Every function is a well-formed block sequence: it opens with a Label and
	// closes with a terminator.
	for (const auto& fn : module.functions) {
		if (fn.body.empty()) {
			return fail("function has an empty body");
		}
		if (fn.body.front().op != IROp::Label) {
			return fail("function body does not begin with a label");
		}
		switch (fn.body.back().op) {
		case IROp::Return:
		case IROp::ReturnValue:
		case IROp::Branch:
			break;
		default:
			return fail("function body does not end with a terminator");
		}
	}
	return true;
}

} // namespace rtsl
