#pragma once

#include "rtsl/basic_types.hpp"

#include <string>
#include <vector>

namespace rtsl {

enum class ArtifactKind : u16 {
	object = 1,
	module = 2,
	library = 3,
	program = 4,
};

constexpr u32 ArtifactMagic = 0x4c535452u;
constexpr u16 ArtifactVersionMajor = 0;
constexpr u16 ArtifactVersionMinor = 1;

struct ExportSymbol {
	std::string name;
	std::string kind;
	std::string type;
	u64 interface_hash = 0;
};

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

struct UniformBinding {
	std::string scope_name;
	std::string name;
	std::string type;
	std::vector<StructField> inline_fields;
	AccessKind access = AccessKind::read_write;
	u32 set = 0;
	u32 member = 0;
	u32 type_id = 0;
	bool is_anonymous = false;
	u32 anonymous_block_id = 0;
};

// Stage interface reflection. RTSL has no `input`/`output`/`varying` blocks or
// stage globals; the return-boundary grammar `-> T : field(tag, ...)` is how a
// stage entry declares its interface, and these records are the backend-neutral
// result. Backends map them to target inputs/outputs/varyings; RTIR itself
// carries no stage-I/O ops.
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

// One field of a stage interface payload with its ABI placement.
// location == kNoLocation means "no user location" (for example clip position).
// member_index is the struct member this field maps to, so a backend can
// extract/insert it (`OpTypeStruct` carries no member names). kNoMember means
// the entry's whole parameter/return value is the payload (e.g. a bare `vec4`
// fragment color).
struct StageIOField {
	static constexpr u32 kNoLocation = static_cast<u32>(-1);
	static constexpr u32 kNoMember = static_cast<u32>(-1);
	std::string name;
	std::vector<std::string> tags;
	InterpolationKind interpolation = InterpolationKind::none;
	StageFieldPlacement placement = StageFieldPlacement::user;
	u32 location = kNoLocation;
	u32 member_index = kNoMember;
};

struct StageInterface {
	StageRole role = StageRole::varying;
	std::string type_name;
	std::vector<StageIOField> fields;
};

} // namespace rtsl
