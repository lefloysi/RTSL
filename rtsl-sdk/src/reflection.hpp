#pragma once

#include "basic_types.hpp"

#include <string>
#include <vector>

namespace rtsl {

// Public symbol visible through a module interface.
struct ExportSymbol {
	std::string name;
	std::string kind;
	std::string type;
	u64 interface_hash = 0;
};

// Source-level struct field retained for reflection, module interfaces, and
// resource layout metadata.
struct StructField {
	std::string type;
	std::string name;
};

struct ParameterDecl {
	std::string type;
	std::string name;
	bool is_const = false;
	bool is_reference = false;
};

struct StructMemberFunction {
	std::string name;
	std::vector<ParameterDecl> parameters;
	std::string return_type = "void";
};

struct StructDecl {
	std::string name;
	std::vector<StructField> fields;
	std::vector<StructMemberFunction> member_functions;
	std::vector<ParameterDecl> constructor_parameters;
};

enum class AccessKind : u08 {
	read_write = 0,
	read_only = 1,
	write_only = 2,
};

// Reflected resource binding after semantic analysis and lowering. `scope_name`
// is the containing uniform block or namespace-like resource scope; `name` is
// the binding inside that scope. `type_id` points into the loaded IR module.
struct UniformBinding {
	std::string scope_name;
	std::string name;
	std::string type;
	std::vector<StructField> inline_fields;
	AccessKind access = AccessKind::read_write;
	u32 set = 0;
	u32 member = 0;
	bool is_anonymous = false;
	u32 anonymous_block_id = 0;
};

enum class StageRole : u08 {
	input,
	varying,
	output,
};

enum class InterpolationKind : u08 {
	none = 0,
	smooth = 1,
	flat = 2,
};

enum class StageFieldPlacement : u08 {
	user = 0,
	clip_position = 1,
};

struct StageIOField {
	static constexpr u32 kNoLocation = static_cast<u32>(-1);
	static constexpr u32 kNoMember = static_cast<u32>(-1);

	std::string name;

	// Source boundary tags, kept for tools and backend decisions that need the
	// authored interface annotation.
	std::vector<std::string> tags;
	InterpolationKind interpolation = InterpolationKind::none;
	StageFieldPlacement placement = StageFieldPlacement::user;
	u32 location = kNoLocation;
	u32 member_index = kNoMember;
};

// One logical stage interface payload. RTSL source declares interfaces through
// function signatures and return-boundary fields; backends consume this
// resolved model.
struct StageInterface {
	StageRole role = StageRole::varying;
	std::string type_name;
	std::vector<StageIOField> fields;
};

} // namespace rtsl
