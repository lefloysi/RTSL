#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace rtsl::ir {

struct Id {
	std::uint32_t value = 0;

	[[nodiscard]] constexpr explicit operator bool() const { return value != 0; }
	[[nodiscard]] friend constexpr auto operator<=>(Id, Id) = default;
};

inline constexpr Id no_id{};

[[nodiscard]] constexpr std::uint32_t raw_id(Id id) {
	return id.value;
}

enum class StorageClass : std::uint8_t {
	function = 0,
	uniform = 3,
	uniform_constant = 4,
	storage_buffer = 5,
	push_constant = 6,
	private_ = 7,
};

enum class Op : std::uint16_t {
#define RTSL_IR_OP(name, display_name) name,
#include "rtsl/sdk/ops.def"
};

inline constexpr std::size_t op_count = 0
#define RTSL_IR_OP(name, display_name) +1
#include "rtsl/sdk/ops.def"
;

[[nodiscard]] inline constexpr std::string_view op_name(Op op) {
	switch (op) {
#define RTSL_IR_OP(name, display_name) case Op::name: return display_name;
#include "rtsl/sdk/ops.def"
	}
	return "Unknown";
}

struct NoArguments {};

struct VariableArguments {
	std::optional<Id> initializer;
};

struct LoadArguments {
	Id pointer{};
};

struct StoreArguments {
	Id pointer{};
	Id value{};
};

struct AccessChainArguments {
	Id base{};
	std::vector<Id> indices;
};

struct CompositeConstructArguments {
	std::vector<Id> constituents;
};

struct CompositeExtractArguments {
	Id composite{};
	std::vector<std::uint32_t> indices;
};

struct CompositeInsertArguments {
	Id composite{};
	Id object{};
	std::vector<std::uint32_t> indices;
};

struct VectorShuffleArguments {
	Id first{};
	Id second{};
	std::vector<std::uint32_t> components;
};

struct UnaryArguments {
	Id operand{};
};

struct BinaryArguments {
	Id lhs{};
	Id rhs{};
};

struct BranchArguments {
	Id target{};
};

struct BranchConditionalArguments {
	Id condition{};
	Id true_target{};
	Id false_target{};
};

struct SelectionMergeArguments {
	Id merge_block{};
};

struct LoopMergeArguments {
	Id merge_block{};
	Id continue_block{};
};

struct ReturnValueArguments {
	Id value{};
};

struct ImageSampleExplicitLodArguments {
	Id sampled_image{};
	Id coordinate{};
	Id lod{};
};

struct ImageWriteArguments {
	Id image{};
	Id coordinate{};
	Id texel{};
};

using InstructionArguments = std::variant<
	NoArguments,
	VariableArguments,
	LoadArguments,
	StoreArguments,
	AccessChainArguments,
	CompositeConstructArguments,
	CompositeExtractArguments,
	CompositeInsertArguments,
	VectorShuffleArguments,
	UnaryArguments,
	BinaryArguments,
	BranchArguments,
	BranchConditionalArguments,
	SelectionMergeArguments,
	LoopMergeArguments,
	ReturnValueArguments,
	ImageSampleExplicitLodArguments,
	ImageWriteArguments>;

struct Instruction {
	Op op = Op::Nop;
	Id result_id{};
	Id type_id{};
	InstructionArguments arguments;

	template <typename Arguments>
	[[nodiscard]] const Arguments* arguments_if() const noexcept {
		return std::get_if<Arguments>(&arguments);
	}

	[[nodiscard]] bool references(Id id) const noexcept;
};

enum class DecorationKind : std::uint16_t {
	location,
	binding,
	descriptor_set,
	offset,
	array_stride,
	matrix_stride,
	builtin,
	no_perspective,
	flat,
	centroid,
	sample,
	non_writable,
	non_readable,
	block,
	column_major,
	row_major,
};

inline constexpr std::uint32_t no_member = UINT32_MAX;

struct Decoration {
	Id target{};
	DecorationKind kind = DecorationKind::location;
	std::uint32_t member_index = no_member;
	std::vector<std::uint32_t> literals;

	[[nodiscard]] std::optional<std::uint32_t> member() const {
		if (member_index == no_member) return std::nullopt;
		return member_index;
	}
};

enum class TypeKind : std::uint8_t {
	void_,
	boolean,
	signed_integer,
	unsigned_integer,
	floating,
	vector,
	matrix,
	structure,
	pointer,
	array,
	function,
	image,
	sampler,
	sampled_image,
};

enum class ImageDimension : std::uint8_t {
	none,
	two,
	three,
	cube,
};

struct ImageShape {
	ImageDimension dimension = ImageDimension::none;
	bool arrayed = false;
};

enum class ImageClass : std::uint8_t {
	sampled,
	storage,
};

struct Type {
	Id id{};
	TypeKind kind = TypeKind::void_;
	std::uint32_t bit_width = 0;
	Id element_type{};
	std::uint32_t element_count = 0;
	Id array_length{};
	StorageClass storage_class = StorageClass::function;
	ImageShape image{};
	ImageClass image_class = ImageClass::sampled;
	std::vector<Id> members;
};

enum class ConstantKind : std::uint8_t {
	boolean,
	signed_integer,
	unsigned_integer,
	floating,
	composite,
};

struct Constant {
	Id id{};
	Id type{};
	ConstantKind kind = ConstantKind::boolean;
	std::vector<std::uint32_t> words;
	std::vector<Id> elements;
};

struct Global {
	Id id{};
	Id type{};
	StorageClass storage_class = StorageClass::private_;
	std::optional<Id> initializer;
};

struct Parameter {
	Id id{};
	Id type{};
};

struct Block {
	Id id{};
	std::vector<Instruction> instructions;
};

struct Function {
	Id id{};
	Id return_type{};
	std::string name;
	std::vector<Parameter> parameters;
	std::vector<Block> blocks;
};

} // namespace rtsl::ir

template <>
struct std::hash<rtsl::ir::Id> {
	std::size_t operator()(rtsl::ir::Id id) const noexcept {
		return std::hash<std::uint32_t>{}(id.value);
	}
};
