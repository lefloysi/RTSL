#include "rtsl/sdk/program.hpp"

#include "artifact_codec.hpp"

#include <algorithm>
#include <array>
#include <exception>
#include <limits>
#include <string_view>
#include <type_traits>
#include <utility>

namespace rtsl {

namespace {

struct DecorationRange {
	std::size_t begin = 0;
	std::size_t count = 0;
};

LoadError invalid_program(std::string context, std::string message) {
	return LoadError{
		.code = LoadErrorCode::invalid_program,
		.context = std::move(context),
		.message = std::move(message),
	};
}

bool valid_storage_class(std::uint32_t value) {
	switch (value) {
	case 0:
	case 3:
	case 4:
	case 5:
	case 6:
	case 7:
		return true;
	default:
		return false;
	}
}

ir::StorageClass storage_class(std::uint32_t value) {
	switch (value) {
	case 0: return ir::StorageClass::function;
	case 3: return ir::StorageClass::uniform;
	case 4: return ir::StorageClass::uniform_constant;
	case 5: return ir::StorageClass::storage_buffer;
	case 6: return ir::StorageClass::push_constant;
	default: return ir::StorageClass::private_;
	}
}

bool is_type_op(IROp op) {
	return (op >= IROp::TypeVoid && op <= IROp::TypeSampledImage) || op == IROp::TypeRuntimeArray;
}

bool is_constant_op(IROp op) {
	return op >= IROp::ConstantBool && op <= IROp::ConstantComposite;
}

std::expected<ir::Type, LoadError> make_type(const IRInstruction& instruction) {
	ir::Type type{ .id = instruction.result_id };
	const auto exact = [&](std::size_t operands, std::size_t literals) {
		return instruction.operands.size() == operands && instruction.literals.size() == literals;
	};
	const auto malformed = [&] {
		return std::unexpected(invalid_program("types", std::string(ir_op_name(instruction.op)) + " has an invalid operand shape"));
	};

	switch (instruction.op) {
	case IROp::TypeVoid:
		if (!exact(0, 0)) return malformed();
		type.kind = ir::TypeKind::void_;
		break;
	case IROp::TypeBool:
		if (!exact(0, 0)) return malformed();
		type.kind = ir::TypeKind::boolean;
		break;
	case IROp::TypeInt:
	case IROp::TypeUInt:
	case IROp::TypeFloat:
		if (!exact(0, 1) || instruction.literals.front() == 0) return malformed();
		type.kind = instruction.op == IROp::TypeInt ? ir::TypeKind::signed_integer
			: instruction.op == IROp::TypeUInt ? ir::TypeKind::unsigned_integer : ir::TypeKind::floating;
		type.bit_width = instruction.literals.front();
		break;
	case IROp::TypeVector:
	case IROp::TypeMatrix:
		if (!exact(1, 1) || instruction.literals.front() == 0) return malformed();
		type.kind = instruction.op == IROp::TypeVector ? ir::TypeKind::vector : ir::TypeKind::matrix;
		type.element_type = instruction.operands.front();
		type.element_count = instruction.literals.front();
		break;
	case IROp::TypeStruct:
		if (!instruction.literals.empty()) return malformed();
		type.kind = ir::TypeKind::structure;
		type.members = instruction.operands;
		break;
	case IROp::TypePointer:
		if (!exact(1, 1) || !valid_storage_class(instruction.literals.front())) return malformed();
		type.kind = ir::TypeKind::pointer;
		type.element_type = instruction.operands.front();
		type.storage_class = storage_class(instruction.literals.front());
		break;
	case IROp::TypeArray:
		if (!exact(2, 0)) return malformed();
		type.kind = ir::TypeKind::array;
		type.element_type = instruction.operands[0];
		type.array_length = instruction.operands[1];
		break;
	case IROp::TypeRuntimeArray:
		if (!exact(1, 0)) return malformed();
		type.kind = ir::TypeKind::runtime_array;
		type.element_type = instruction.operands[0];
		break;
	case IROp::TypeFunction:
		if (instruction.operands.empty() || !instruction.literals.empty()) return malformed();
		type.kind = ir::TypeKind::function;
		type.element_type = instruction.operands.front();
		type.members.assign(instruction.operands.begin() + 1, instruction.operands.end());
		break;
	case IROp::TypeImage:
		if (!exact(1, 3) || instruction.literals[0] == 0 ||
			instruction.literals[0] > static_cast<std::uint32_t>(ir::ImageDimension::cube) ||
			instruction.literals[1] > 1 ||
			instruction.literals[2] > static_cast<std::uint32_t>(ir::ImageClass::storage)) return malformed();
		type.kind = ir::TypeKind::image;
		type.element_type = instruction.operands[0];
		type.image = ir::ImageShape{
			.dimension = static_cast<ir::ImageDimension>(instruction.literals[0]),
			.arrayed = instruction.literals[1] != 0,
		};
		type.image_class = static_cast<ir::ImageClass>(instruction.literals[2]);
		break;
	case IROp::TypeSampler:
		if (!exact(0, 0)) return malformed();
		type.kind = ir::TypeKind::sampler;
		break;
	case IROp::TypeSampledImage:
		if (!exact(1, 0)) return malformed();
		type.kind = ir::TypeKind::sampled_image;
		type.element_type = instruction.operands[0];
		break;
	default:
		return malformed();
	}
	return type;
}

std::expected<ir::Constant, LoadError> make_constant(const IRInstruction& instruction) {
	ir::Constant constant{ .id = instruction.result_id, .type = instruction.type_id };
	switch (instruction.op) {
	case IROp::ConstantBool:
		if (!instruction.operands.empty() || instruction.literals.size() != 1 || instruction.literals.front() > 1)
			return std::unexpected(invalid_program("constants", "boolean constant has an invalid shape"));
		constant.kind = ir::ConstantKind::boolean;
		constant.words = instruction.literals;
		break;
	case IROp::ConstantInt:
	case IROp::ConstantUInt:
	case IROp::ConstantFloat:
		if (!instruction.operands.empty() || instruction.literals.empty())
			return std::unexpected(invalid_program("constants", "scalar constant has an invalid shape"));
		constant.kind = instruction.op == IROp::ConstantInt ? ir::ConstantKind::signed_integer
			: instruction.op == IROp::ConstantUInt ? ir::ConstantKind::unsigned_integer : ir::ConstantKind::floating;
		constant.words = instruction.literals;
		break;
	case IROp::ConstantComposite:
		if (instruction.operands.empty() || !instruction.literals.empty())
			return std::unexpected(invalid_program("constants", "composite constant has an invalid shape"));
		constant.kind = ir::ConstantKind::composite;
		constant.elements = instruction.operands;
		break;
	default:
		return std::unexpected(invalid_program("constants", "type/constant pool contains an executable operation"));
	}
	return constant;
}

bool is_terminator(ir::Op op) {
	return op == ir::Op::Branch || op == ir::Op::BranchConditional ||
		op == ir::Op::Return || op == ir::Op::ReturnValue;
}

const ir::Type* find_type(std::span<const ir::Type> types, ir::Id id) {
	const auto found = std::ranges::find(types, id, &ir::Type::id);
	return found == types.end() ? nullptr : &*found;
}

const ir::Constant* find_constant(std::span<const ir::Constant> constants, ir::Id id) {
	const auto found = std::ranges::find(constants, id, &ir::Constant::id);
	return found == constants.end() ? nullptr : &*found;
}

bool equivalent_type(std::span<const ir::Type> types, std::span<const ir::Constant> constants,
	ir::Id lhs_id, ir::Id rhs_id, std::size_t depth = 0) {
	if (lhs_id == rhs_id) return true;
	if (depth > 64) return false;
	const ir::Type* lhs = find_type(types, lhs_id);
	const ir::Type* rhs = find_type(types, rhs_id);
	if (!lhs || !rhs || lhs->kind != rhs->kind || lhs->bit_width != rhs->bit_width ||
		lhs->element_count != rhs->element_count || lhs->storage_class != rhs->storage_class ||
		lhs->image.dimension != rhs->image.dimension || lhs->image.arrayed != rhs->image.arrayed ||
		lhs->image_class != rhs->image_class ||
		lhs->members.size() != rhs->members.size()) {
		return false;
	}
	if (lhs->element_type || rhs->element_type) {
		if (!equivalent_type(types, constants, lhs->element_type, rhs->element_type, depth + 1)) return false;
	}
	if (lhs->array_length || rhs->array_length) {
		const ir::Constant* lhs_length = find_constant(constants, lhs->array_length);
		const ir::Constant* rhs_length = find_constant(constants, rhs->array_length);
		if (!lhs_length || !rhs_length || lhs_length->kind != rhs_length->kind ||
			lhs_length->words != rhs_length->words || lhs_length->elements != rhs_length->elements) {
			return false;
		}
	}
	for (std::size_t i = 0; i < lhs->members.size(); ++i) {
		if (!equivalent_type(types, constants, lhs->members[i], rhs->members[i], depth + 1)) return false;
	}
	return true;
}

std::optional<ir::Op> normalized_op(IROp op) {
	switch (op) {
#define RTSL_IR_OP(name, display_name) case IROp::name: return ir::Op::name;
#include "rtsl/sdk/ops.def"
	default: return std::nullopt;
	}
}

std::expected<ir::Instruction, LoadError> normalize_instruction(const IRInstruction& source) {
	const auto op = normalized_op(source.op);
	if (!op) {
		if (source.op == IROp::FunctionCall) {
			return std::unexpected(invalid_program("instructions", "linked program contains an unresolved function call"));
		}
		if (source.op == IROp::Label || source.op == IROp::FunctionParameter) {
			return std::unexpected(invalid_program("instructions", "function body contains an operation owned by another IR section"));
		}
		return std::unexpected(invalid_program("instructions", "function body contains a type, constant, or invalid operation"));
	}

	const std::size_t operands = source.operands.size();
	const std::size_t literals = source.literals.size();
	const auto exact = [&](std::size_t operand_count, std::size_t literal_count) {
		return operands == operand_count && literals == literal_count;
	};
	ir::Instruction instruction{
		.op = *op,
		.result_id = source.result_id,
		.type_id = source.type_id,
	};
	bool valid = true;
	switch (source.op) {
	case IROp::Nop:
		valid = exact(0, 0) && !source.result_id && !source.type_id;
		break;
	case IROp::Variable:
		valid = source.result_id && source.type_id && operands <= 1 && literals == 1 &&
			source.literals.front() == static_cast<std::uint32_t>(ir::StorageClass::function);
		if (valid) instruction.arguments = ir::VariableArguments{
			.initializer = operands == 0 ? std::nullopt : std::optional<ir::Id>{ source.operands.front() },
		};
		break;
	case IROp::Load:
		valid = exact(1, 0);
		if (valid) instruction.arguments = ir::LoadArguments{ .pointer = source.operands[0] };
		break;
	case IROp::Store:
		valid = exact(2, 0);
		if (valid) instruction.arguments = ir::StoreArguments{ .pointer = source.operands[0], .value = source.operands[1] };
		break;
	case IROp::AccessChain:
		valid = operands >= 1 && literals == 0;
		if (valid) instruction.arguments = ir::AccessChainArguments{
			.base = source.operands[0],
			.indices = { source.operands.begin() + 1, source.operands.end() },
		};
		break;
	case IROp::CompositeConstruct:
		valid = literals == 0;
		if (valid) instruction.arguments = ir::CompositeConstructArguments{ .constituents = source.operands };
		break;
	case IROp::CompositeExtract:
		valid = operands == 1 && literals >= 1;
		if (valid) instruction.arguments = ir::CompositeExtractArguments{
			.composite = source.operands[0],
			.indices = source.literals,
		};
		break;
	case IROp::CompositeInsert:
		valid = operands == 2 && literals >= 1;
		if (valid) instruction.arguments = ir::CompositeInsertArguments{
			.composite = source.operands[0],
			.object = source.operands[1],
			.indices = source.literals,
		};
		break;
	case IROp::VectorShuffle:
		valid = operands == 2 && literals >= 1;
		if (valid) instruction.arguments = ir::VectorShuffleArguments{
			.first = source.operands[0],
			.second = source.operands[1],
			.components = source.literals,
		};
		break;
	case IROp::FNegate:
	case IROp::LogicalNot:
	case IROp::ConvertFToU:
	case IROp::ConvertFToS:
	case IROp::ConvertSToF:
	case IROp::ConvertUToF:
	case IROp::Bitcast:
	case IROp::BitwiseNot:
	case IROp::FAbs:
	case IROp::Floor:
	case IROp::Fract:
	case IROp::Sqrt:
	case IROp::ImageQuerySize:
	case IROp::SNegate:
		valid = exact(1, 0);
		if (valid) instruction.arguments = ir::UnaryArguments{ .operand = source.operands[0] };
		break;
	case IROp::FAdd: case IROp::FSub: case IROp::FMul: case IROp::FDiv: case IROp::FMod:
	case IROp::IAdd: case IROp::ISub: case IROp::IMul: case IROp::SDiv: case IROp::UDiv:
	case IROp::SMod: case IROp::UMod: case IROp::VectorTimesScalar: case IROp::MatrixTimesScalar:
	case IROp::MatrixTimesVector: case IROp::MatrixTimesMatrix: case IROp::Dot: case IROp::Cross:
	case IROp::FOrdEqual: case IROp::FOrdNotEqual: case IROp::FOrdLess: case IROp::FOrdLessEqual:
	case IROp::FOrdGreater: case IROp::FOrdGreaterEqual: case IROp::IEqual: case IROp::INotEqual:
	case IROp::SLess: case IROp::SLessEqual: case IROp::SGreater: case IROp::SGreaterEqual:
	case IROp::ULess: case IROp::ULessEqual: case IROp::UGreater: case IROp::UGreaterEqual:
	case IROp::LogicalAnd: case IROp::LogicalOr: case IROp::SampledImage:
	case IROp::ImageSampleImplicitLod: case IROp::ImageRead:
	case IROp::BitwiseAnd: case IROp::BitwiseOr: case IROp::BitwiseXor:
	case IROp::FMin: case IROp::FMax:
		valid = exact(2, 0);
		if (valid) instruction.arguments = ir::BinaryArguments{ .lhs = source.operands[0], .rhs = source.operands[1] };
		break;
	case IROp::FMix:
	case IROp::SmoothStep:
		valid = exact(3, 0);
		if (valid) instruction.arguments = ir::TernaryArguments{
			.first = source.operands[0],
			.second = source.operands[1],
			.third = source.operands[2],
		};
		break;
	case IROp::ImageSampleExplicitLod:
		valid = exact(3, 0);
		if (valid) instruction.arguments = ir::ImageSampleExplicitLodArguments{
			.sampled_image = source.operands[0],
			.coordinate = source.operands[1],
			.lod = source.operands[2],
		};
		break;
	case IROp::ImageWrite:
		valid = exact(3, 0);
		if (valid) instruction.arguments = ir::ImageWriteArguments{
			.image = source.operands[0],
			.coordinate = source.operands[1],
			.texel = source.operands[2],
		};
		break;
	case IROp::Branch:
		valid = exact(1, 0);
		if (valid) instruction.arguments = ir::BranchArguments{ .target = source.operands[0] };
		break;
	case IROp::SelectionMerge:
		valid = exact(1, 0);
		if (valid) instruction.arguments = ir::SelectionMergeArguments{ .merge_block = source.operands[0] };
		break;
	case IROp::ReturnValue:
		valid = exact(1, 0);
		if (valid) instruction.arguments = ir::ReturnValueArguments{ .value = source.operands[0] };
		break;
	case IROp::BranchConditional:
		valid = exact(3, 0);
		if (valid) instruction.arguments = ir::BranchConditionalArguments{
			.condition = source.operands[0],
			.true_target = source.operands[1],
			.false_target = source.operands[2],
		};
		break;
	case IROp::LoopMerge:
		valid = exact(2, 0);
		if (valid) instruction.arguments = ir::LoopMergeArguments{
			.merge_block = source.operands[0],
			.continue_block = source.operands[1],
		};
		break;
	case IROp::Return:
		valid = exact(0, 0);
		break;
	default:
		valid = false;
	}

	const bool produces_value = source.op == IROp::Variable || source.op == IROp::Load ||
		source.op == IROp::AccessChain || source.op == IROp::CompositeConstruct ||
		source.op == IROp::CompositeExtract || source.op == IROp::CompositeInsert ||
		source.op == IROp::VectorShuffle ||
		(source.op >= IROp::FAdd && source.op <= IROp::Bitcast) ||
		(source.op >= IROp::SampledImage && source.op <= IROp::ImageRead) ||
		(source.op >= IROp::BitwiseAnd && source.op <= IROp::SNegate);
	if (produces_value != static_cast<bool>(source.result_id) ||
		produces_value != static_cast<bool>(source.type_id)) {
		valid = false;
	}
	if (!valid) {
		return std::unexpected(invalid_program("instructions",
			std::string(ir_op_name(source.op)) + " has an invalid shape"));
	}
	return instruction;
}

} // namespace

bool ir::Instruction::references(ir::Id id) const noexcept {
	return std::visit([&](const auto& value) {
		using Arguments = std::decay_t<decltype(value)>;
		if constexpr (std::is_same_v<Arguments, ir::NoArguments>) {
			return false;
		} else if constexpr (std::is_same_v<Arguments, ir::VariableArguments>) {
			return value.initializer == id;
		} else if constexpr (std::is_same_v<Arguments, ir::LoadArguments>) {
			return value.pointer == id;
		} else if constexpr (std::is_same_v<Arguments, ir::StoreArguments>) {
			return value.pointer == id || value.value == id;
		} else if constexpr (std::is_same_v<Arguments, ir::AccessChainArguments>) {
			return value.base == id || std::ranges::find(value.indices, id) != value.indices.end();
		} else if constexpr (std::is_same_v<Arguments, ir::CompositeConstructArguments>) {
			return std::ranges::find(value.constituents, id) != value.constituents.end();
		} else if constexpr (std::is_same_v<Arguments, ir::CompositeExtractArguments>) {
			return value.composite == id;
		} else if constexpr (std::is_same_v<Arguments, ir::CompositeInsertArguments>) {
			return value.composite == id || value.object == id;
		} else if constexpr (std::is_same_v<Arguments, ir::VectorShuffleArguments>) {
			return value.first == id || value.second == id;
		} else if constexpr (std::is_same_v<Arguments, ir::UnaryArguments>) {
			return value.operand == id;
		} else if constexpr (std::is_same_v<Arguments, ir::BinaryArguments>) {
			return value.lhs == id || value.rhs == id;
		} else if constexpr (std::is_same_v<Arguments, ir::TernaryArguments>) {
			return value.first == id || value.second == id || value.third == id;
		} else if constexpr (std::is_same_v<Arguments, ir::BranchArguments>) {
			return value.target == id;
		} else if constexpr (std::is_same_v<Arguments, ir::BranchConditionalArguments>) {
			return value.condition == id || value.true_target == id || value.false_target == id;
		} else if constexpr (std::is_same_v<Arguments, ir::SelectionMergeArguments>) {
			return value.merge_block == id;
		} else if constexpr (std::is_same_v<Arguments, ir::LoopMergeArguments>) {
			return value.merge_block == id || value.continue_block == id;
		} else if constexpr (std::is_same_v<Arguments, ir::ReturnValueArguments>) {
			return value.value == id;
		} else if constexpr (std::is_same_v<Arguments, ir::ImageSampleExplicitLodArguments>) {
			return value.sampled_image == id || value.coordinate == id || value.lod == id;
		} else if constexpr (std::is_same_v<Arguments, ir::ImageWriteArguments>) {
			return value.image == id || value.coordinate == id || value.texel == id;
		}
		return false;
	}, arguments);
}

struct Program::Data {
	std::vector<ir::Type> types;
	std::vector<ir::Constant> constants;
	std::vector<ir::Global> globals;
	std::vector<ir::Function> functions;
	std::vector<ir::Decoration> decorations;
	std::vector<Resource> resources;
	std::vector<EntryPoint> entries;

	std::vector<const ir::Type*> type_index;
	std::vector<const ir::Constant*> constant_index;
	std::vector<const ir::Global*> global_index;
	std::vector<const ir::Function*> function_index;
	std::vector<DecorationRange> decoration_index;
};

Program::Program(std::unique_ptr<Data> data) : data_(std::move(data)) {}
Program::Program(Program&&) noexcept = default;
Program& Program::operator=(Program&&) noexcept = default;
Program::~Program() = default;

std::span<const ir::Type> Program::types() const noexcept { return data_ ? std::span<const ir::Type>{ data_->types } : std::span<const ir::Type>{}; }
std::span<const ir::Constant> Program::constants() const noexcept { return data_ ? std::span<const ir::Constant>{ data_->constants } : std::span<const ir::Constant>{}; }
std::span<const ir::Global> Program::globals() const noexcept { return data_ ? std::span<const ir::Global>{ data_->globals } : std::span<const ir::Global>{}; }
std::span<const ir::Function> Program::functions() const noexcept { return data_ ? std::span<const ir::Function>{ data_->functions } : std::span<const ir::Function>{}; }
std::span<const ir::Decoration> Program::decorations() const noexcept { return data_ ? std::span<const ir::Decoration>{ data_->decorations } : std::span<const ir::Decoration>{}; }
std::span<const Resource> Program::resources() const noexcept { return data_ ? std::span<const Resource>{ data_->resources } : std::span<const Resource>{}; }
std::span<const EntryPoint> Program::entries() const noexcept { return data_ ? std::span<const EntryPoint>{ data_->entries } : std::span<const EntryPoint>{}; }
std::uint32_t Program::id_bound() const noexcept {
	return data_ ? static_cast<std::uint32_t>(data_->type_index.size()) : 1;
}

std::span<const ir::Decoration> Program::decorations(ir::Id target) const noexcept {
	if (!data_ || target.value >= data_->decoration_index.size()) return {};
	const DecorationRange range = data_->decoration_index[target.value];
	return std::span<const ir::Decoration>{ data_->decorations }.subspan(range.begin, range.count);
}

const ir::Type* Program::find_type(ir::Id id) const noexcept {
	return data_ && id.value < data_->type_index.size() ? data_->type_index[id.value] : nullptr;
}

const ir::Constant* Program::find_constant(ir::Id id) const noexcept {
	return data_ && id.value < data_->constant_index.size() ? data_->constant_index[id.value] : nullptr;
}

const ir::Global* Program::find_global(ir::Id id) const noexcept {
	return data_ && id.value < data_->global_index.size() ? data_->global_index[id.value] : nullptr;
}

const ir::Function* Program::find_function(ir::Id id) const noexcept {
	return data_ && id.value < data_->function_index.size() ? data_->function_index[id.value] : nullptr;
}

const EntryPoint* Program::entry(Stage stage) const noexcept {
	if (!data_) return nullptr;
	for (const auto& entry_point : data_->entries) {
		if (entry_point.stage == stage) return &entry_point;
	}
	return nullptr;
}

std::expected<Program, LoadError> load_program(std::span<const std::byte> bytes) {
	if (bytes.data() == nullptr && !bytes.empty()) {
		return std::unexpected(LoadError{
			.code = LoadErrorCode::invalid_argument,
			.context = "input",
			.message = "program byte span has a null data pointer",
		});
	}

	try {
		const auto encoded = std::span<const u08>{ reinterpret_cast<const u08*>(bytes.data()), bytes.size() };
		auto decoded = codec::decode_artifact(encoded);
		if (!decoded) return std::unexpected(std::move(decoded.error()));
		if (decoded->kind != ArtifactKind::program) {
			return std::unexpected(LoadError{
				.code = LoadErrorCode::wrong_artifact_kind,
				.context = "header.kind",
				.message = "rtsl::load_program requires a linked program artifact",
			});
		}

		IRModule& module = decoded->module;
		const std::size_t id_limit = module.next_id.value;
		if (id_limit == 0 || id_limit > bytes.size() + 1) {
			return std::unexpected(invalid_program("ids", "program declares an invalid ID bound"));
		}
		std::vector<bool> definitions(id_limit, false);
		const auto define = [&](ir::Id id, std::string_view context) -> std::expected<void, LoadError> {
			if (!id || id.value >= id_limit)
				return std::unexpected(invalid_program(std::string(context), "definition ID is outside the program bound"));
			if (definitions[id.value])
				return std::unexpected(invalid_program(std::string(context), "program contains a duplicate definition ID"));
			definitions[id.value] = true;
			return {};
		};

		auto data = std::make_unique<Program::Data>();
		data->types.reserve(module.type_constant_pool.size());
		data->constants.reserve(module.type_constant_pool.size());
		std::vector<bool> pool_definitions(id_limit, false);
		for (const IRInstruction& instruction : module.type_constant_pool) {
			if (static_cast<std::size_t>(instruction.op) >= wire_op_count)
				return std::unexpected(invalid_program("type_constant_pool", "pool entry has an invalid opcode"));
			for (const ir::Id operand : instruction.operands) {
				if (!operand || operand.value >= id_limit || !pool_definitions[operand.value])
					return std::unexpected(invalid_program("type_constant_pool", "pool entry does not reference an earlier pool definition"));
			}
			if (instruction.type_id && (instruction.type_id.value >= id_limit || !pool_definitions[instruction.type_id.value]))
				return std::unexpected(invalid_program("type_constant_pool", "constant type does not reference an earlier type"));
			if (auto result = define(instruction.result_id, "type_constant_pool"); !result)
				return std::unexpected(std::move(result.error()));
			pool_definitions[instruction.result_id.value] = true;
			if (is_type_op(instruction.op)) {
				auto type = make_type(instruction);
				if (!type) return std::unexpected(std::move(type.error()));
				data->types.push_back(std::move(*type));
			} else if (is_constant_op(instruction.op)) {
				auto constant = make_constant(instruction);
				if (!constant) return std::unexpected(std::move(constant.error()));
				data->constants.push_back(std::move(*constant));
			} else {
				return std::unexpected(invalid_program("type_constant_pool", "pool contains a non type/constant operation"));
			}
		}

		data->globals.reserve(module.global_variables.size());
		for (const IRInstruction& global : module.global_variables) {
			if (global.op != IROp::Variable || !global.result_id || !global.type_id ||
				global.literals.size() != 1 || !valid_storage_class(global.literals.front()) ||
				global.literals.front() == static_cast<std::uint32_t>(ir::StorageClass::function) || global.operands.size() > 1) {
				return std::unexpected(invalid_program("globals", "global variable has an invalid shape"));
			}
			if (auto result = define(global.result_id, "globals"); !result)
				return std::unexpected(std::move(result.error()));
			data->globals.push_back(ir::Global{
				.id = global.result_id,
				.type = global.type_id,
				.storage_class = storage_class(global.literals.front()),
				.initializer = global.operands.empty() ? std::nullopt : std::optional<ir::Id>{ global.operands.front() },
			});
		}

		data->functions.reserve(module.functions.size());
		for (const IRFunction& source : module.functions) {
			if (!source.result_id || !source.return_type_id)
				return std::unexpected(invalid_program("functions", "function is missing its result or return type"));
			if (auto result = define(source.result_id, "functions"); !result)
				return std::unexpected(std::move(result.error()));

			ir::Function function{
				.id = source.result_id,
				.return_type = source.return_type_id,
				.name = source.display_name,
			};
			for (const ir::Id parameter : source.parameter_ids) {
				if (auto result = define(parameter, "function.parameters"); !result)
					return std::unexpected(std::move(result.error()));
			}

			for (const IRInstruction& instruction : source.body) {
				if (instruction.op == IROp::Label) {
					if (!instruction.result_id || instruction.type_id || !instruction.operands.empty() || !instruction.literals.empty())
						return std::unexpected(invalid_program("function.blocks", "label has an invalid shape"));
					if (auto result = define(instruction.result_id, "function.blocks"); !result)
						return std::unexpected(std::move(result.error()));
					if (!function.blocks.empty() && (function.blocks.back().instructions.empty() ||
						!is_terminator(function.blocks.back().instructions.back().op))) {
						return std::unexpected(invalid_program("function.blocks", "basic block does not end with a terminator"));
					}
					function.blocks.push_back(ir::Block{ .id = instruction.result_id });
					continue;
				}
				if (instruction.op == IROp::FunctionParameter) {
					if (!instruction.result_id || !instruction.type_id || !instruction.operands.empty() || !instruction.literals.empty() ||
						std::find(source.parameter_ids.begin(), source.parameter_ids.end(), instruction.result_id) == source.parameter_ids.end()) {
						return std::unexpected(invalid_program("function.parameters", "parameter instruction has an invalid shape"));
					}
					function.parameters.push_back(ir::Parameter{ .id = instruction.result_id, .type = instruction.type_id });
					continue;
				}
				if (function.blocks.empty())
					return std::unexpected(invalid_program("function.blocks", "instruction appears before the first label"));
				auto normalized = normalize_instruction(instruction);
				if (!normalized) return std::unexpected(std::move(normalized.error()));
				if (instruction.result_id) {
					if (auto result = define(instruction.result_id, "function.instructions"); !result)
						return std::unexpected(std::move(result.error()));
				}
				function.blocks.back().instructions.push_back(std::move(*normalized));
			}
			if (function.parameters.size() != source.parameter_ids.size())
				return std::unexpected(invalid_program("function.parameters", "function parameter table does not match its instructions"));
			if (function.blocks.empty() || function.blocks.back().instructions.empty() ||
				!is_terminator(function.blocks.back().instructions.back().op)) {
				return std::unexpected(invalid_program("function.blocks", "function does not end with a terminated basic block"));
			}
			data->functions.push_back(std::move(function));
		}

		const auto reference = [&](ir::Id id, std::string_view context) -> std::expected<void, LoadError> {
			if (id && (id.value >= definitions.size() || !definitions[id.value]))
				return std::unexpected(invalid_program(std::string(context), "reference does not resolve to a program definition"));
			return {};
		};
		for (const auto& instruction : module.type_constant_pool) {
			if (auto result = reference(instruction.type_id, "type_constant_pool.type"); !result) return std::unexpected(std::move(result.error()));
			for (const auto operand : instruction.operands)
				if (auto result = reference(operand, "type_constant_pool.operand"); !result) return std::unexpected(std::move(result.error()));
		}
		for (const auto& global : module.global_variables) {
			if (auto result = reference(global.type_id, "globals.type"); !result) return std::unexpected(std::move(result.error()));
			for (const auto operand : global.operands)
				if (auto result = reference(operand, "globals.initializer"); !result) return std::unexpected(std::move(result.error()));
		}
		for (const auto& function : module.functions) {
			if (auto result = reference(function.return_type_id, "functions.return_type"); !result) return std::unexpected(std::move(result.error()));
			for (const auto& instruction : function.body) {
				if (auto result = reference(instruction.type_id, "instructions.type"); !result) return std::unexpected(std::move(result.error()));
				for (const auto operand : instruction.operands)
					if (auto result = reference(operand, "instructions.operand"); !result) return std::unexpected(std::move(result.error()));
			}
		}

		if (module.entries.size() != 2)
			return std::unexpected(invalid_program("entries", "graphics program must contain exactly one vertex and one fragment entry"));
		std::array<bool, 2> seen_stages{};
		for (const EntryPoint& entry_point : module.entries) {
			const std::size_t stage_index = static_cast<std::size_t>(entry_point.stage);
			if (stage_index >= seen_stages.size() || seen_stages[stage_index])
				return std::unexpected(invalid_program("entries.stage", "program contains an invalid or duplicate shader stage"));
			seen_stages[stage_index] = true;
			if (auto result = reference(entry_point.function, "entries.function"); !result) return std::unexpected(std::move(result.error()));
			const auto validate_interface = [&](const std::optional<Interface>& interface, std::string_view context) -> std::expected<void, LoadError> {
				if (!interface) return {};
				if (auto result = reference(interface->value_type, context); !result) return result;
				if (interface->value) {
					if (auto result = reference(*interface->value, context); !result) return result;
				}
				for (const auto& element : interface->elements) {
					if (auto result = reference(element.type, context); !result) return result;
					if (element.builtin != Builtin::none && element.location)
						return std::unexpected(invalid_program(std::string(context), "built-in interface element also has a user location"));
				}
				return {};
			};
			if (auto result = validate_interface(entry_point.input, "entries.input"); !result) return std::unexpected(std::move(result.error()));
			if (auto result = validate_interface(entry_point.output, "entries.output"); !result) return std::unexpected(std::move(result.error()));
		}

		data->resources = std::move(module.resources);
		for (const auto& resource : data->resources) {
			if (static_cast<std::size_t>(resource.kind) > static_cast<std::size_t>(ResourceKind::storage_image) ||
				static_cast<std::size_t>(resource.access) > static_cast<std::size_t>(Access::write_only) ||
				static_cast<std::size_t>(resource.image.dimension) > static_cast<std::size_t>(ImageDimension::cube)) {
				return std::unexpected(invalid_program("resources", "resource contains an invalid enum value"));
			}
			if (auto result = reference(resource.variable, "resources.variable"); !result) return std::unexpected(std::move(result.error()));
			if (auto result = reference(resource.value_type, "resources.value_type"); !result) return std::unexpected(std::move(result.error()));
		}

		data->entries = std::move(module.entries);
		for (auto& resource : data->resources) {
			for (const auto& entry_point : data->entries) {
				const ir::Function* function = nullptr;
				for (const auto& candidate : data->functions) {
					if (candidate.id == entry_point.function) {
						function = &candidate;
						break;
					}
				}
				if (!function) continue;
				bool used = false;
				for (const auto& block : function->blocks) {
					for (const auto& instruction : block.instructions) {
						if (instruction.references(resource.variable)) {
							used = true;
							break;
						}
					}
					if (used) break;
				}
				if (used) resource.stages |= entry_point.stage == Stage::vertex ? StageMask::vertex : StageMask::fragment;
			}
		}

		data->decorations.reserve(module.decorations.size());
		for (const IRDecoration& decoration : module.decorations) {
			if (static_cast<std::size_t>(decoration.kind) > static_cast<std::size_t>(IRDecorationKind::RowMajor))
				return std::unexpected(invalid_program("decorations", "decoration contains an invalid kind"));
			if (auto result = reference(decoration.target, "decorations.target"); !result) return std::unexpected(std::move(result.error()));
			data->decorations.push_back(ir::Decoration{
				.target = decoration.target,
				.kind = static_cast<ir::DecorationKind>(decoration.kind),
				.member_index = decoration.member_index,
				.literals = decoration.literals,
			});
		}
		std::stable_sort(data->decorations.begin(), data->decorations.end(), [](const auto& lhs, const auto& rhs) {
			return lhs.target.value < rhs.target.value;
		});

		data->type_index.resize(id_limit);
		data->constant_index.resize(id_limit);
		data->global_index.resize(id_limit);
		data->function_index.resize(id_limit);
		data->decoration_index.resize(id_limit);
		for (const auto& type : data->types) data->type_index[type.id.value] = &type;
		for (const auto& constant : data->constants) data->constant_index[constant.id.value] = &constant;
		for (const auto& global : data->globals) data->global_index[global.id.value] = &global;
		for (const auto& function : data->functions) data->function_index[function.id.value] = &function;
		for (std::size_t i = 0; i < data->decorations.size();) {
			const std::size_t begin = i;
			const std::uint32_t target = data->decorations[i].target.value;
			while (i < data->decorations.size() && data->decorations[i].target.value == target) ++i;
			data->decoration_index[target] = DecorationRange{ .begin = begin, .count = i - begin };
		}

		const auto validate_normalized_interface = [&](const Interface& interface,
			std::string_view context) -> std::expected<void, LoadError> {
			const ir::Type* payload = data->type_index[interface.value_type.value];
			if (!payload) return std::unexpected(invalid_program(std::string(context), "interface payload is not a type"));
			for (std::size_t i = 0; i < interface.elements.size(); ++i) {
				const InterfaceElement& element = interface.elements[i];
				if (element.builtin == Builtin::none && !element.location) {
					return std::unexpected(invalid_program(std::string(context), "interface element has neither a location nor a built-in"));
				}
				if (element.member) {
					if (payload->kind != ir::TypeKind::structure || *element.member >= payload->members.size() ||
						payload->members[*element.member] != element.type) {
						return std::unexpected(invalid_program(std::string(context), "interface member does not match its payload type"));
					}
				} else if (payload->kind == ir::TypeKind::structure || payload->id != element.type) {
					return std::unexpected(invalid_program(std::string(context), "whole-value interface element does not match its payload type"));
				}
				for (std::size_t j = 0; j < i; ++j) {
					const InterfaceElement& previous = interface.elements[j];
					if ((element.location && previous.location == element.location) ||
						(element.builtin != Builtin::none && previous.builtin == element.builtin)) {
						return std::unexpected(invalid_program(std::string(context), "interface contains a duplicate location or built-in"));
					}
				}
			}
			return {};
		};

		for (const EntryPoint& entry_point : data->entries) {
			const ir::Function* function = data->function_index[entry_point.function.value];
			if (!function) return std::unexpected(invalid_program("entries.function", "entry does not reference a function"));
			if (entry_point.input) {
				if (auto result = validate_normalized_interface(*entry_point.input, "entries.input"); !result)
					return std::unexpected(std::move(result.error()));
				if (function->parameters.size() != 1 || !entry_point.input->value ||
					function->parameters.front().id != *entry_point.input->value ||
					function->parameters.front().type != entry_point.input->value_type) {
					return std::unexpected(invalid_program("entries.input", "entry input does not match the function signature"));
				}
			} else if (!function->parameters.empty()) {
				return std::unexpected(invalid_program("entries.input", "entry function parameters have no reflected input"));
			}
			if (entry_point.output) {
				if (auto result = validate_normalized_interface(*entry_point.output, "entries.output"); !result)
					return std::unexpected(std::move(result.error()));
				if (function->return_type != entry_point.output->value_type) {
					return std::unexpected(invalid_program("entries.output", "entry output does not match the function return type"));
				}
			} else {
				const ir::Type* return_type = data->type_index[function->return_type.value];
				if (!return_type || return_type->kind != ir::TypeKind::void_) {
					return std::unexpected(invalid_program("entries.output", "non-void entry function has no reflected output"));
				}
			}
		}

		for (std::size_t i = 0; i < data->resources.size(); ++i) {
			const Resource& resource = data->resources[i];
			const ir::Global* global = data->global_index[resource.variable.value];
			const ir::Type* pointer = global ? data->type_index[global->type.value] : nullptr;
			if (!global || !pointer || pointer->kind != ir::TypeKind::pointer || pointer->element_type != resource.value_type) {
				return std::unexpected(invalid_program("resources", "resource does not match its global pointer"));
			}
			const ir::StorageClass expected_storage =
				resource.kind == ResourceKind::uniform_buffer ? ir::StorageClass::uniform :
				resource.kind == ResourceKind::storage_buffer ? ir::StorageClass::storage_buffer :
				ir::StorageClass::uniform_constant;
			if (global->storage_class != expected_storage || pointer->storage_class != expected_storage) {
				return std::unexpected(invalid_program("resources", "resource kind does not match its storage class"));
			}
			const bool image = resource.kind == ResourceKind::sampled_texture || resource.kind == ResourceKind::storage_image;
			if (image == (resource.image.dimension == ImageDimension::none)) {
				return std::unexpected(invalid_program("resources", "resource kind does not match its image shape"));
			}
			const ir::Type* value_type = data->type_index[resource.value_type.value];
			if (!value_type) return std::unexpected(invalid_program("resources", "resource value is not a type"));
			if (resource.kind == ResourceKind::sampler && value_type->kind != ir::TypeKind::sampler) {
				return std::unexpected(invalid_program("resources", "sampler resource does not use a sampler type"));
			}
			if (resource.kind == ResourceKind::sampled_texture) {
				const ir::Type* image_type = value_type->kind == ir::TypeKind::sampled_image
					? data->type_index[value_type->element_type.value] : nullptr;
				if (!image_type || image_type->kind != ir::TypeKind::image ||
					image_type->image_class != ir::ImageClass::sampled ||
					image_type->image.dimension != resource.image.dimension ||
					image_type->image.arrayed != resource.image.arrayed) {
					return std::unexpected(invalid_program("resources", "sampled texture does not match its normalized image type"));
				}
			}
			if (resource.kind == ResourceKind::storage_image &&
				(value_type->kind != ir::TypeKind::image || value_type->image_class != ir::ImageClass::storage ||
				 value_type->image.dimension != resource.image.dimension || value_type->image.arrayed != resource.image.arrayed)) {
				return std::unexpected(invalid_program("resources", "storage image does not match its normalized image type"));
			}
			if ((resource.kind == ResourceKind::uniform_buffer || resource.kind == ResourceKind::storage_buffer) &&
				value_type->kind != ir::TypeKind::structure) {
				return std::unexpected(invalid_program("resources", "buffer resource value type is not a block structure"));
			}
			if (resource.kind == ResourceKind::uniform_buffer || resource.kind == ResourceKind::storage_buffer) {
				const DecorationRange range = data->decoration_index[value_type->id.value];
				const auto begin = data->decorations.begin() + static_cast<std::ptrdiff_t>(range.begin);
				const auto end = begin + static_cast<std::ptrdiff_t>(range.count);
				if (std::ranges::find_if(begin, end, [](const ir::Decoration& value) {
					return value.kind == ir::DecorationKind::block && !value.member();
				}) == end) {
					return std::unexpected(invalid_program("resources", "buffer resource type is missing its block decoration"));
				}
			}
			for (std::size_t j = 0; j < i; ++j) {
				if (data->resources[j].descriptor.set == resource.descriptor.set &&
					data->resources[j].descriptor.binding == resource.descriptor.binding) {
					return std::unexpected(invalid_program("resources", "program contains duplicate descriptor bindings"));
				}
			}
		}

		const EntryPoint* vertex = nullptr;
		const EntryPoint* fragment = nullptr;
		for (const auto& entry_point : data->entries) {
			if (entry_point.stage == Stage::vertex) vertex = &entry_point;
			if (entry_point.stage == Stage::fragment) fragment = &entry_point;
		}
		if (vertex && fragment && fragment->input) {
			if (!vertex->output) {
				return std::unexpected(invalid_program("entries.interface", "fragment input has no vertex output"));
			}
			for (const InterfaceElement& input : fragment->input->elements) {
				const auto output = std::ranges::find_if(vertex->output->elements, [&](const InterfaceElement& candidate) {
					return input.builtin != Builtin::none
						? candidate.builtin == input.builtin
						: candidate.builtin == Builtin::none && candidate.location == input.location;
				});
				if (output == vertex->output->elements.end() || output->interpolation != input.interpolation ||
					!equivalent_type(data->types, data->constants, output->type, input.type)) {
					return std::unexpected(invalid_program("entries.interface", "vertex output does not satisfy fragment input"));
				}
			}
		}

		return Program{ std::move(data) };
	} catch (const std::bad_alloc&) {
		return std::unexpected(LoadError{
			.code = LoadErrorCode::allocation_failure,
			.context = "load_program",
			.message = "allocation failed while loading RTSL program",
		});
	} catch (const std::exception& exception) {
		return std::unexpected(LoadError{
			.code = LoadErrorCode::invalid_program,
			.context = "load_program",
			.message = exception.what(),
		});
	}
}

} // namespace rtsl
