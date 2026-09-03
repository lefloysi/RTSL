#ifndef RTSL_IR_IR_HPP
#define RTSL_IR_IR_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace rtsl::ir {

template <typename Tag>
class Id {
public:
	constexpr Id() = default;
	explicit constexpr Id(std::uint32_t value) : Value(value) {}

	[[nodiscard]] constexpr std::uint32_t value() const noexcept { return Value; }
	[[nodiscard]] constexpr explicit operator bool() const noexcept { return Value != 0; }
	[[nodiscard]] friend constexpr bool operator==(Id, Id) noexcept = default;

private:
	std::uint32_t Value{};
};

using TypeId = Id<struct TypeTag>;
using ValueId = Id<struct ValueTag>;
using BlockId = Id<struct BlockTag>;
using FunctionId = Id<struct FunctionTag>;
using SymbolId = Id<struct SymbolTag>;
using StringId = Id<struct StringTag>;

class StringTable {
public:
	StringTable();

	[[nodiscard]] StringId intern(std::string_view spelling);
	[[nodiscard]] std::string_view get(StringId id) const noexcept;
	[[nodiscard]] std::span<const std::byte> bytes() const noexcept;
	[[nodiscard]] std::size_t size() const noexcept { return records.size(); }

private:
	struct Record {
		std::uint32_t offset{};
		std::uint32_t size{};
	};

	[[nodiscard]] std::uint64_t hash(std::string_view spelling) const noexcept;

	std::vector<std::byte> storage;
	std::vector<Record> records;
	std::unordered_map<std::uint64_t, std::vector<StringId>> buckets;
};

enum class AddressSpace : std::uint8_t {
	address_space_function,
	address_space_private,
	address_space_workgroup,
	address_space_uniform,
	address_space_storage,
	address_space_push_constant,
	address_space_resource,
};

enum class TypeKind : std::uint8_t {
	type_void,
	type_boolean,
	type_signed_integer,
	type_unsigned_integer,
	type_floating,
	type_vector,
	type_matrix,
	type_structure,
	type_pointer,
	type_array,
	type_runtime_array,
	type_function,
	type_image,
	type_texture,
	type_sampler,
	type_patch,
	type_primitive,
};

struct StructMember {
	StringId name;
	TypeId type;
	std::optional<std::uint32_t> offset;
	std::optional<std::uint32_t> alignment;

	[[nodiscard]] friend bool operator==(const StructMember&, const StructMember&) = default;
};

enum class Builtin : std::uint8_t {
	builtin_position,
	builtin_global_invocation_x,
	builtin_global_invocation_y,
	builtin_global_invocation_z,
};

struct BuiltinMember {
	Builtin builtin{Builtin::builtin_position};
	std::vector<std::uint32_t> member_path;

	[[nodiscard]] friend bool operator==(const BuiltinMember&, const BuiltinMember&) = default;
};

struct Type {
	TypeId id;
	TypeKind kind{TypeKind::type_void};
	std::uint32_t bit_width{};
	TypeId element_type;
	std::uint32_t element_count{};
	AddressSpace address_space{AddressSpace::address_space_function};
	std::vector<TypeId> parameter_types;
	std::vector<StructMember> members;
	std::vector<BuiltinMember> builtin_members;
	StringId name;

	[[nodiscard]] bool structurallyEquals(const Type& other) const;
};

struct Symbol {
	SymbolId id;
	StringId fully_qualified_name;
	bool exported{};
};

enum class Opcode : std::uint16_t {
	opcode_undef,
	opcode_constant_boolean,
	opcode_constant_integer,
	opcode_constant_floating,
	opcode_constant_composite,
	opcode_variable,
	opcode_load,
	opcode_store,
	opcode_access,
	opcode_add,
	opcode_subtract,
	opcode_multiply,
	opcode_divide,
	opcode_remainder,
	opcode_negate,
	opcode_compare_equal,
	opcode_compare_not_equal,
	opcode_compare_less,
	opcode_compare_less_equal,
	opcode_compare_greater,
	opcode_compare_greater_equal,
	opcode_logical_and,
	opcode_logical_or,
	opcode_logical_not,
	opcode_convert,
	opcode_bitcast,
	opcode_construct,
	opcode_extract,
	opcode_insert,
	opcode_call,
	opcode_barrier,
	opcode_memory_barrier,
	opcode_resource_load,
	opcode_resource_store,
	opcode_resource_sample,
	opcode_resource_query,
	opcode_derivative,
	opcode_discard,
};

struct Instruction {
	Opcode opcode{Opcode::opcode_undef};
	ValueId result;
	TypeId type;
	FunctionId callee;
	std::vector<ValueId> operands;
	std::vector<std::uint32_t> immediates;
};

struct BlockArgument {
	ValueId value;
	TypeId type;
};

struct Successor {
	BlockId block;
	std::vector<ValueId> arguments;
};

enum class TerminatorKind : std::uint8_t {
	terminator_branch,
	terminator_conditional_branch,
	terminator_switch,
	terminator_return,
	terminator_return_value,
	terminator_kill,
	terminator_unreachable,
};

struct Terminator {
	TerminatorKind kind{TerminatorKind::terminator_unreachable};
	std::vector<ValueId> operands;
	std::vector<Successor> successors;
	std::vector<std::uint32_t> immediates;
};

enum class MergeKind : std::uint8_t {
	merge_none,
	merge_selection,
	merge_loop,
};

struct StructuredMerge {
	MergeKind kind{MergeKind::merge_none};
	BlockId merge_block;
	BlockId continue_block;
};

struct Block {
	BlockId id;
	std::vector<BlockArgument> arguments;
	std::vector<Instruction> instructions;
	std::optional<Terminator> terminator;
	StructuredMerge merge;
};

struct Parameter {
	ValueId value;
	TypeId type;
	SymbolId symbol;
	std::optional<Builtin> builtin;
};

struct Function {
	FunctionId id;
	SymbolId symbol;
	TypeId return_type;
	std::vector<Parameter> parameters;
	std::vector<Block> blocks;
	bool declaration{};
	bool implicit_emitter{};
	bool implicit{};
};

enum class Stage : std::uint8_t {
	stage_vertex,
	stage_tessellation_control,
	stage_tessellation_evaluation,
	stage_geometry,
	stage_fragment,
	stage_compute,
};

enum class TessellationDomain : std::uint8_t {
	tessellation_domain_triangles,
	tessellation_domain_quads,
	tessellation_domain_isolines,
};

enum class TessellationSpacing : std::uint8_t {
	tessellation_spacing_equal,
	tessellation_spacing_fractional_even,
	tessellation_spacing_fractional_odd,
};

enum class Winding : std::uint8_t {
	winding_clockwise,
	winding_counter_clockwise,
};

enum class PrimitiveTopology : std::uint8_t {
	primitive_points,
	primitive_lines,
	primitive_lines_adjacency,
	primitive_triangles,
	primitive_triangles_adjacency,
	primitive_line_strip,
	primitive_triangle_strip,
};

struct TessellationControlConfiguration {
	std::uint32_t output_control_points{};
};

struct TessellationEvaluationConfiguration {
	TessellationDomain domain{TessellationDomain::tessellation_domain_triangles};
	TessellationSpacing spacing{TessellationSpacing::tessellation_spacing_equal};
	Winding winding{Winding::winding_counter_clockwise};
};

struct GeometryConfiguration {
	PrimitiveTopology input{PrimitiveTopology::primitive_triangles};
	PrimitiveTopology output{PrimitiveTopology::primitive_triangle_strip};
	std::uint32_t maximum_vertices{};
	std::uint32_t invocations{1};
};

struct ComputeConfiguration {
	std::array<std::uint32_t, 3> workgroup_size{1, 1, 1};
};

struct InterfaceContract {
	std::uint32_t parameter_index{};
	std::vector<StringId> member_path;
	StringId contract;
};

struct EntryAttribute {
	StringId name;
	std::vector<StringId> tokens;
};

using StageConfiguration = std::variant<std::monostate, TessellationControlConfiguration, TessellationEvaluationConfiguration, GeometryConfiguration, ComputeConfiguration>;

struct EntryPoint {
	SymbolId symbol;
	FunctionId function;
	StringId source_name;
	Stage stage{Stage::stage_vertex};
	StageConfiguration configuration{std::monostate{}};
	std::vector<EntryAttribute> attributes;
	std::vector<InterfaceContract> parameter_contracts;
};

enum class ResourceKind : std::uint8_t {
	resource_uniform_buffer,
	resource_storage_buffer,
	resource_sampled_texture,
	resource_storage_texture,
	resource_sampler,
	resource_input_attachment,
};

enum class Access : std::uint8_t {
	access_read_only,
	access_write_only,
	access_read_write,
};

struct Binding {
	std::uint32_t set{};
	std::uint32_t binding{};
};

struct Resource {
	SymbolId symbol;
	ResourceKind kind{ResourceKind::resource_uniform_buffer};
	TypeId type;
	Access access{Access::access_read_only};
	std::optional<Binding> binding;
	std::vector<FunctionId> users;
};

struct Uniform {
	SymbolId symbol;
	TypeId type;
	std::optional<Binding> binding;
	std::optional<std::uint32_t> offset;
	std::optional<std::uint32_t> size;
	std::optional<std::uint32_t> alignment;
};

struct StorageObject {
	SymbolId symbol;
	TypeId type;
	AddressSpace address_space{AddressSpace::address_space_private};
	Access access{Access::access_read_write};
	std::optional<Binding> binding;
	std::optional<ValueId> initializer;
};

class Module {
public:
	StringTable strings;
	StringId name;
	std::vector<Type> types;
	std::vector<Symbol> symbols;
	std::vector<Function> functions;
	std::vector<EntryPoint> entry_points;
	std::vector<Resource> resources;
	std::vector<Uniform> uniforms;
	std::vector<StorageObject> storage_objects;

	[[nodiscard]] const Type* findType(TypeId id) const noexcept;
	[[nodiscard]] Type* findType(TypeId id) noexcept;
	[[nodiscard]] const Symbol* findSymbol(SymbolId id) const noexcept;
	[[nodiscard]] const Function* findFunction(FunctionId id) const noexcept;
	[[nodiscard]] Function* findFunction(FunctionId id) noexcept;
	[[nodiscard]] const Block* findBlock(const Function& function, BlockId id) const noexcept;
};

} // namespace rtsl::ir

#endif
