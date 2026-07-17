#pragma once

#include "basic_types.hpp"
#include "reflection.hpp"
#include "rtsl/sdk/program.hpp"

#include <string>
#include <vector>

namespace rtsl {


// Backend-neutral storage classes carried by pointer and variable ops.
enum class StorageClass : u08 {
	Function = 0,
	Uniform = 3,
	UniformConstant = 4,
	StorageBuffer = 5,
	PushConstant = 6,
	Private = 7,
};

enum class IROp : u16 {
#define RTSL_WIRE_OP(name, display_name) name,
#include "wire_ops.def"
};

[[nodiscard]] inline constexpr const char* ir_op_name(IROp op) {
	switch (op) {
#define RTSL_WIRE_OP(name, display_name) case IROp::name: return display_name;
#include "wire_ops.def"
	}
	return "OpUnknown";
}

struct IRInstruction {
	IROp op = IROp::Nop;
	ir::Id result_id{};
	ir::Id type_id{};
	std::vector<ir::Id> operands;
	std::vector<u32> literals;
};

enum class IRDecorationKind : u16 {
	Location,
	Binding,
	DescriptorSet,
	Offset,
	ArrayStride,
	MatrixStride,
	BuiltIn,
	NoPerspective,
	Flat,
	Centroid,
	Sample,
	NonWritable,
	NonReadable,
	Block,
	ColMajor,
	RowMajor,
};

struct IRDecoration {
	ID<IRInstruction> target = ID<IRInstruction>{};
	IRDecorationKind kind = IRDecorationKind::Location;
	u32 member_index = static_cast<u32>(-1);
	std::vector<u32> literals;
};

struct IRFunction {
	enum class Kind : u08 {
		normal = 0,
		generated = 1,
		constructor = 2,
		exported = 3,
	};

	ID<IRInstruction> result_id = ID<IRInstruction>{};
	ID<IRInstruction> function_type_id = ID<IRInstruction>{};
	ID<IRInstruction> return_type_id = ID<IRInstruction>{};
	std::vector<ID<IRInstruction>> parameter_ids;
	std::vector<IRInstruction> body;

	// Empty for ordinary functions. Stage names are source identifiers, not
	// keywords; v0.1 accepts vertex and fragment stage identities only.
	std::string stage;

	Kind kind = Kind::normal;

	[[nodiscard]] bool is_generated() const { return kind == Kind::generated; }
	[[nodiscard]] bool is_constructor() const { return kind == Kind::constructor; }
	[[nodiscard]] bool is_exported() const { return kind == Kind::exported; }

	std::string link_name;
	std::string display_name;
};

struct IRFunctionDebugInfo {
	ID<std::string> display_name{};
	std::vector<ID<std::string>> parameter_names;
};

struct IRCallTarget {
	std::string display_name;
	std::string mangled_name;
};

struct IRModule {
	std::string source_name;
	std::vector<std::string> imports;
	std::vector<ExportSymbol> imported_exports;
	std::vector<ExportSymbol> exports;

	// Forward-only ordered pool. An entry may only reference ids already
	// defined by earlier entries in this pool.
	std::vector<IRInstruction> type_constant_pool;

	// Module-scope variables for resources and private storage.
	std::vector<IRInstruction> global_variables;

	std::vector<IRDecoration> decorations;
	std::vector<IRFunction> functions;

	std::vector<StructDecl> structs;
	std::vector<UniformBinding> uniforms;
	std::vector<StageInterface> stage_interfaces;
	std::vector<Resource> resources;
	std::vector<EntryPoint> entries;

	// Pending call targets referenced by FunctionCall literal[0]. Linked
	// program artifacts must not contain unresolved call targets.
	std::vector<IRCallTarget> call_targets;

	// Monotonic allocator state saved so linked artifacts can keep assigning
	// ids without colliding with the serialized module.
	ID<IRInstruction> next_id{ 1 };
};

} // namespace rtsl
