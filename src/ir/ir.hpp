#pragma once

// RTSL Intermediate Representation.
//
// SSA, typed, instruction-stream IR shaped after SPIR-V. See
// docs/rtir.md for the full design.

#include "sema/sema.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace rtsl {

// SSA / type / constant / function id. Shared id space within a module.
// Id 0 is reserved as "no id".
using IRId = u32;
inline constexpr IRId IRId_None = 0;

// String pool reference into the artifact's strings payload.
struct StringId {
	u32 value = 0;
};

// Source-debug origin for an instruction. file_id is a SourceManager file id
// at compile time and a debug payload file id once serialized.
struct DebugLocation {
	u32 file_id = 0;
	u32 line = 0;
	u32 column = 0;
};

// SPIR-V-like storage class for OpVariable / OpTypePointer. Encoded as a u08
// literal on those instructions.
enum class StorageClass : u08 {
	Function = 0,
	Input = 1,
	Output = 2,
	Uniform = 3,
	UniformConstant = 4,
	StorageBuffer = 5,
	PushConstant = 6,
	Private = 7,
};

// Opcode set, generated from ir/ops.def. The enum value is the wire encoding
// (u16) used by serialized artifacts — see the ordering warning in ops.def.
// Operand/literal shapes are documented per-op in ops.def and docs/rtir.md.
enum class IROp : u16 {
#define RTSL_IR_OP(name, disasm_name) name,
#include "ir/ops.def"
};

// Human-readable SPIR-V-style mnemonic for an opcode (used by the
// disassembler). Generated from the same ops.def table.
[[nodiscard]] const char* ir_op_name(IROp op);

struct IRInstruction {
	IROp op = IROp::Nop;
	IRId result_id = IRId_None; // 0 if no result
	IRId type_id = IRId_None;	// 0 if not typed
	std::vector<IRId> operands;
	std::vector<u32> literals;
	DebugLocation loc{};
};

// Decoration kinds. Kept separate from the SSA stream so backends iterate them
// directly.
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
	BufferBlock,
	ColMajor,
	RowMajor,
};

struct IRDecoration {
	IRId target = IRId_None;
	IRDecorationKind kind = IRDecorationKind::Location;
	// u32(-1) means "decorate the target itself"; otherwise the struct member
	// at that index (OpMemberDecorate equivalent).
	u32 member_index = static_cast<u32>(-1);
	std::vector<u32> literals;
};

struct IRFunction {
	IRId result_id = IRId_None;		   // OpFunction result id
	IRId function_type_id = IRId_None; // OpTypeFunction id
	IRId return_type_id = IRId_None;
	std::vector<IRId> parameter_ids; // each is an OpFunctionParameter result
	std::vector<IRInstruction> body; // starts with Label, ends with terminator

	// Backend stage. User functions are StageKind::none; the compiler-generated
	// backend wrappers (vert/frag) carry the stage.
	StageKind stage = StageKind::none;

	// True for compiler-synthesized ABI glue (the generated stage runtime).
	bool generated = false;

	// True for struct member-init constructors (`Foo::Foo(...)` declarations).
	// The IR inliner replaces every call to one with the constructor body, so
	// the standalone function is dead after lowering. We keep it in the IR
	// module for diagnostics but elide it from the serialized artifact.
	bool is_constructor = false;

	// True if the source declared the function with `export`. Exported
	// functions reach .rtslm (the public interface) and are visible to
	// importers; non-exported functions are private to the implementation.
	bool exported = false;

	StringId display_name{}; // authored entry/helper name
	StringId mangled_name{}; // canonical identity

	// Source-level identifier used by the inliner to match unresolved
	// FunctionCall instructions to their target. After lowering this stores the
	// canonical mangled identity; display_name_text keeps the authored spelling.
	std::string source_name;
	std::string display_name_text;
};

struct IRFunctionDebugInfo {
	StringId display_name{};
	std::vector<StringId> parameter_names;
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

	// Forward-only ordered pool: an entry may only reference earlier entries.
	std::vector<IRInstruction> type_constant_pool;

	// Module-scope OpVariable instructions (Input/Output/Uniform/...).
	std::vector<IRInstruction> global_variables;

	std::vector<IRDecoration> decorations;
	std::vector<IRFunction> functions;
	std::vector<IRFunctionDebugInfo> function_debug;

	// Reflection bridges. Not part of SSA semantics; they let the C API answer
	// uniform, stage-variable, and entry queries directly.
	std::vector<StructDecl> structs;
	std::vector<UniformBinding> uniforms;
	std::vector<StageInterface> stage_interfaces;

	// Pending call targets, indexed by FunctionCall.literals[0]. Each entry
	// carries both the source-facing callee spelling and its canonical mangled
	// identity. The inliner/linker use the canonical identity when available
	// and keep the display name for diagnostics.
	std::vector<IRCallTarget> call_targets;

	// Monotonic id allocator. Backends remap to their own id space.
	IRId next_id = 1;
};

[[nodiscard]] IRModule lower_to_ir(const SemanticModule& module, DiagnosticEngine* diagnostics = nullptr);
[[nodiscard]] bool verify_ir(const IRModule& module, DiagnosticEngine* diagnostics = nullptr);

} // namespace rtsl
