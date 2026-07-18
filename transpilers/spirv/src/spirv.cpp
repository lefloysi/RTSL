#include "rtsl/spirv.hpp"

#include <spirv/unified1/GLSL.std.450.h>
#include <spirv/unified1/spirv.hpp11>

#include <algorithm>
#include <array>
#include <exception>
#include <limits>
#include <span>
#include <string_view>
#include <utility>

namespace rtsl::spirv {

namespace {

template <typename Enum>
[[nodiscard]] constexpr std::uint32_t word(Enum value) {
	return static_cast<std::uint32_t>(value);
}

struct InterfaceVariable {
	const InterfaceElement* element = nullptr;
	ir::Id pointer_type{};
	ir::Id variable{};
	spv::StorageClass storage = spv::StorageClass::Input;
};

struct PointerType {
	ir::Id value_type{};
	spv::StorageClass storage = spv::StorageClass::Input;
	ir::Id id{};
};

void append_string(std::vector<std::uint32_t>& words, std::string_view value) {
	const std::size_t word_count = (value.size() + 1 + 3) / 4;
	const std::size_t begin = words.size();
	words.resize(begin + word_count, 0);
	for (std::size_t i = 0; i < value.size(); ++i) {
		words[begin + i / 4] |= static_cast<std::uint32_t>(static_cast<unsigned char>(value[i])) << ((i % 4) * 8);
	}
}

void emit(std::vector<std::uint32_t>& section, spv::Op op, std::span<const std::uint32_t> operands = {}) {
	const std::uint32_t word_count = static_cast<std::uint32_t>(operands.size() + 1);
	section.push_back((word_count << spv::WordCountShift) | word(op));
	section.insert(section.end(), operands.begin(), operands.end());
}

void emit(std::vector<std::uint32_t>& section, spv::Op op, std::initializer_list<std::uint32_t> operands) {
	emit(section, op, std::span<const std::uint32_t>{ operands.begin(), operands.size() });
}

void emit_string(std::vector<std::uint32_t>& section, spv::Op op, std::span<const std::uint32_t> prefix, std::string_view value, std::span<const std::uint32_t> suffix = {}) {
	std::vector<std::uint32_t> operands;
	operands.reserve(prefix.size() + (value.size() + 4) / 4 + suffix.size());
	operands.insert(operands.end(), prefix.begin(), prefix.end());
	append_string(operands, value);
	operands.insert(operands.end(), suffix.begin(), suffix.end());
	emit(section, op, operands);
}

std::optional<spv::StorageClass> storage_class(ir::StorageClass value) {
	switch (value) {
	case ir::StorageClass::function: return spv::StorageClass::Function;
	case ir::StorageClass::uniform: return spv::StorageClass::Uniform;
	case ir::StorageClass::uniform_constant: return spv::StorageClass::UniformConstant;
	case ir::StorageClass::storage_buffer: return spv::StorageClass::StorageBuffer;
	case ir::StorageClass::push_constant: return spv::StorageClass::PushConstant;
	case ir::StorageClass::private_: return spv::StorageClass::Private;
	}
	return std::nullopt;
}

spv::Decoration decoration(ir::DecorationKind kind) {
	switch (kind) {
	case ir::DecorationKind::block: return spv::Decoration::Block;
	case ir::DecorationKind::row_major: return spv::Decoration::RowMajor;
	case ir::DecorationKind::column_major: return spv::Decoration::ColMajor;
	case ir::DecorationKind::array_stride: return spv::Decoration::ArrayStride;
	case ir::DecorationKind::matrix_stride: return spv::Decoration::MatrixStride;
	case ir::DecorationKind::builtin: return spv::Decoration::BuiltIn;
	case ir::DecorationKind::no_perspective: return spv::Decoration::NoPerspective;
	case ir::DecorationKind::flat: return spv::Decoration::Flat;
	case ir::DecorationKind::centroid: return spv::Decoration::Centroid;
	case ir::DecorationKind::sample: return spv::Decoration::Sample;
	case ir::DecorationKind::non_writable: return spv::Decoration::NonWritable;
	case ir::DecorationKind::non_readable: return spv::Decoration::NonReadable;
	case ir::DecorationKind::location: return spv::Decoration::Location;
	case ir::DecorationKind::binding: return spv::Decoration::Binding;
	case ir::DecorationKind::descriptor_set: return spv::Decoration::DescriptorSet;
	case ir::DecorationKind::offset: return spv::Decoration::Offset;
	}
	std::unreachable();
}

struct DirectOperation {
	ir::Op source;
	spv::Op target;
};

constexpr std::array direct_operations{
	DirectOperation{ ir::Op::Load, spv::Op::OpLoad },
	DirectOperation{ ir::Op::AccessChain, spv::Op::OpAccessChain },
	DirectOperation{ ir::Op::CompositeConstruct, spv::Op::OpCompositeConstruct },
	DirectOperation{ ir::Op::CompositeExtract, spv::Op::OpCompositeExtract },
	DirectOperation{ ir::Op::CompositeInsert, spv::Op::OpCompositeInsert },
	DirectOperation{ ir::Op::VectorShuffle, spv::Op::OpVectorShuffle },
	DirectOperation{ ir::Op::FAdd, spv::Op::OpFAdd },
	DirectOperation{ ir::Op::FSub, spv::Op::OpFSub },
	DirectOperation{ ir::Op::FMul, spv::Op::OpFMul },
	DirectOperation{ ir::Op::FDiv, spv::Op::OpFDiv },
	DirectOperation{ ir::Op::FMod, spv::Op::OpFMod },
	DirectOperation{ ir::Op::FNegate, spv::Op::OpFNegate },
	DirectOperation{ ir::Op::IAdd, spv::Op::OpIAdd },
	DirectOperation{ ir::Op::ISub, spv::Op::OpISub },
	DirectOperation{ ir::Op::IMul, spv::Op::OpIMul },
	DirectOperation{ ir::Op::SDiv, spv::Op::OpSDiv },
	DirectOperation{ ir::Op::UDiv, spv::Op::OpUDiv },
	DirectOperation{ ir::Op::SMod, spv::Op::OpSMod },
	DirectOperation{ ir::Op::UMod, spv::Op::OpUMod },
	DirectOperation{ ir::Op::VectorTimesScalar, spv::Op::OpVectorTimesScalar },
	DirectOperation{ ir::Op::MatrixTimesScalar, spv::Op::OpMatrixTimesScalar },
	DirectOperation{ ir::Op::MatrixTimesVector, spv::Op::OpMatrixTimesVector },
	DirectOperation{ ir::Op::MatrixTimesMatrix, spv::Op::OpMatrixTimesMatrix },
	DirectOperation{ ir::Op::Dot, spv::Op::OpDot },
	DirectOperation{ ir::Op::FOrdEqual, spv::Op::OpFOrdEqual },
	DirectOperation{ ir::Op::FOrdNotEqual, spv::Op::OpFOrdNotEqual },
	DirectOperation{ ir::Op::FOrdLess, spv::Op::OpFOrdLessThan },
	DirectOperation{ ir::Op::FOrdLessEqual, spv::Op::OpFOrdLessThanEqual },
	DirectOperation{ ir::Op::FOrdGreater, spv::Op::OpFOrdGreaterThan },
	DirectOperation{ ir::Op::FOrdGreaterEqual, spv::Op::OpFOrdGreaterThanEqual },
	DirectOperation{ ir::Op::IEqual, spv::Op::OpIEqual },
	DirectOperation{ ir::Op::INotEqual, spv::Op::OpINotEqual },
	DirectOperation{ ir::Op::SLess, spv::Op::OpSLessThan },
	DirectOperation{ ir::Op::SLessEqual, spv::Op::OpSLessThanEqual },
	DirectOperation{ ir::Op::SGreater, spv::Op::OpSGreaterThan },
	DirectOperation{ ir::Op::SGreaterEqual, spv::Op::OpSGreaterThanEqual },
	DirectOperation{ ir::Op::ULess, spv::Op::OpULessThan },
	DirectOperation{ ir::Op::ULessEqual, spv::Op::OpULessThanEqual },
	DirectOperation{ ir::Op::UGreater, spv::Op::OpUGreaterThan },
	DirectOperation{ ir::Op::UGreaterEqual, spv::Op::OpUGreaterThanEqual },
	DirectOperation{ ir::Op::LogicalAnd, spv::Op::OpLogicalAnd },
	DirectOperation{ ir::Op::LogicalOr, spv::Op::OpLogicalOr },
	DirectOperation{ ir::Op::LogicalNot, spv::Op::OpLogicalNot },
	DirectOperation{ ir::Op::ConvertFToU, spv::Op::OpConvertFToU },
	DirectOperation{ ir::Op::ConvertFToS, spv::Op::OpConvertFToS },
	DirectOperation{ ir::Op::ConvertSToF, spv::Op::OpConvertSToF },
	DirectOperation{ ir::Op::ConvertUToF, spv::Op::OpConvertUToF },
	DirectOperation{ ir::Op::Bitcast, spv::Op::OpBitcast },
	DirectOperation{ ir::Op::BitwiseAnd, spv::Op::OpBitwiseAnd },
	DirectOperation{ ir::Op::BitwiseOr, spv::Op::OpBitwiseOr },
	DirectOperation{ ir::Op::BitwiseXor, spv::Op::OpBitwiseXor },
	DirectOperation{ ir::Op::BitwiseNot, spv::Op::OpNot },
	DirectOperation{ ir::Op::SNegate, spv::Op::OpSNegate },
	DirectOperation{ ir::Op::SampledImage, spv::Op::OpSampledImage },
	DirectOperation{ ir::Op::ImageSampleImplicitLod, spv::Op::OpImageSampleImplicitLod },
	DirectOperation{ ir::Op::ImageSampleExplicitLod, spv::Op::OpImageSampleExplicitLod },
	DirectOperation{ ir::Op::ImageRead, spv::Op::OpImageRead },
};

std::optional<spv::Op> direct_operation(ir::Op operation) {
	const auto found = std::ranges::find_if(direct_operations, [operation](const DirectOperation& candidate) {
		return candidate.source == operation;
	});
	if (found == direct_operations.end()) {
		return std::nullopt;
	}
	return found->target;
}

void append_ids(std::vector<std::uint32_t>& words, std::span<const ir::Id> ids) {
	for (const ir::Id id : ids) {
		words.push_back(id.value);
	}
}

void append_arguments(std::vector<std::uint32_t>&, const ir::NoArguments&) {}

void append_arguments(std::vector<std::uint32_t>& words, const ir::VariableArguments& arguments) {
	if (arguments.initializer) {
		words.push_back(arguments.initializer->value);
	}
}

void append_arguments(std::vector<std::uint32_t>& words, const ir::LoadArguments& arguments) {
	words.push_back(arguments.pointer.value);
}

void append_arguments(std::vector<std::uint32_t>& words, const ir::StoreArguments& arguments) {
	words.insert(words.end(), { arguments.pointer.value, arguments.value.value });
}

void append_arguments(std::vector<std::uint32_t>& words, const ir::AccessChainArguments& arguments) {
	words.push_back(arguments.base.value);
	append_ids(words, arguments.indices);
}

void append_arguments(std::vector<std::uint32_t>& words, const ir::CompositeConstructArguments& arguments) {
	append_ids(words, arguments.constituents);
}

void append_arguments(std::vector<std::uint32_t>& words, const ir::CompositeExtractArguments& arguments) {
	words.push_back(arguments.composite.value);
	words.insert(words.end(), arguments.indices.begin(), arguments.indices.end());
}

void append_arguments(std::vector<std::uint32_t>& words, const ir::CompositeInsertArguments& arguments) {
	words.insert(words.end(), { arguments.object.value, arguments.composite.value });
	words.insert(words.end(), arguments.indices.begin(), arguments.indices.end());
}

void append_arguments(std::vector<std::uint32_t>& words, const ir::VectorShuffleArguments& arguments) {
	words.insert(words.end(), { arguments.first.value, arguments.second.value });
	words.insert(words.end(), arguments.components.begin(), arguments.components.end());
}

void append_arguments(std::vector<std::uint32_t>& words, const ir::UnaryArguments& arguments) {
	words.push_back(arguments.operand.value);
}

void append_arguments(std::vector<std::uint32_t>& words, const ir::BinaryArguments& arguments) {
	words.insert(words.end(), { arguments.lhs.value, arguments.rhs.value });
}

void append_arguments(std::vector<std::uint32_t>& words, const ir::TernaryArguments& arguments) {
	words.insert(words.end(), { arguments.first.value, arguments.second.value, arguments.third.value });
}

void append_arguments(std::vector<std::uint32_t>& words, const ir::BranchArguments& arguments) {
	words.push_back(arguments.target.value);
}

void append_arguments(std::vector<std::uint32_t>& words, const ir::BranchConditionalArguments& arguments) {
	words.insert(words.end(), {
		arguments.condition.value,
		arguments.true_target.value,
		arguments.false_target.value,
	});
}

void append_arguments(std::vector<std::uint32_t>& words, const ir::SelectionMergeArguments& arguments) {
	words.insert(words.end(), { arguments.merge_block.value, word(spv::SelectionControlMask::MaskNone) });
}

void append_arguments(std::vector<std::uint32_t>& words, const ir::LoopMergeArguments& arguments) {
	words.insert(words.end(), {
		arguments.merge_block.value,
		arguments.continue_block.value,
		word(spv::LoopControlMask::MaskNone),
	});
}

void append_arguments(std::vector<std::uint32_t>& words, const ir::ReturnValueArguments& arguments) {
	words.push_back(arguments.value.value);
}

void append_arguments(std::vector<std::uint32_t>& words, const ir::ImageSampleExplicitLodArguments& arguments) {
	words.insert(words.end(), {
		arguments.sampled_image.value,
		arguments.coordinate.value,
		word(spv::ImageOperandsMask::Lod),
		arguments.lod.value,
	});
}

void append_arguments(std::vector<std::uint32_t>& words, const ir::ImageWriteArguments& arguments) {
	words.insert(words.end(), { arguments.image.value, arguments.coordinate.value, arguments.texel.value });
}

void append_arguments(std::vector<std::uint32_t>& words, const ir::InstructionArguments& arguments) {
	std::visit([&words](const auto& value) {
		append_arguments(words, value);
	}, arguments);
}

class SpirvEmitter {
  public:
	SpirvEmitter(const Program& program, Stage stage) : program(program), stage(stage), next_id(program.id_bound()) {}

	std::expected<Shader, Error> run() {
		entry = program.entry(stage);
		if (!entry) {
			return std::unexpected(make_error(ErrorCode::stage_not_found, "entry", "requested stage is not present"));
		}
		function = program.find_function(entry->function);
		if (!function) {
			return std::unexpected(make_error(ErrorCode::invalid_entry, "entry.function", "entry function does not exist", entry->function));
		}
		if (function->parameters.size() > 1) {
			return std::unexpected(make_error(ErrorCode::invalid_entry, "entry.parameters", "SPIR-V stage entries currently support zero or one payload parameter", function->id));
		}
		if (function->parameters.empty() != !entry->input) {
			return std::unexpected(make_error(ErrorCode::invalid_entry, "entry.input", "entry input does not match the function parameter list", function->id));
		}
		if (entry->input && (function->parameters.front().type != entry->input->value_type ||
			!entry->input->value || *entry->input->value != function->parameters.front().id)) {
			return std::unexpected(make_error(ErrorCode::invalid_entry, "entry.input", "entry input payload does not match its function parameter", function->id));
		}

		for (const auto& type : program.types()) {
			if (type.kind == ir::TypeKind::void_) {
				void_type = type.id;
			}
			if (type.kind == ir::TypeKind::signed_integer && type.bit_width == 32) {
				signed_int_type = type.id;
			}
		}
		if (!void_type) {
			return std::unexpected(make_error(ErrorCode::unsupported_type, "types", "program has no void type"));
		}

		for (const auto& resource : program.resources()) {
			if (contains(resource.stages, stage)) {
				selected_resources.push_back(&resource);
			}
		}
		if (!prepare_interface(entry->input, spv::StorageClass::Input, inputs) ||
			!prepare_interface(entry->output, spv::StorageClass::Output, outputs)) {
			return std::unexpected(std::move(*error));
		}

		glsl_ext = fresh_id();
		if (uses_image_queries()) {
			if (!signed_int_type) {
				return std::unexpected(make_error(ErrorCode::unsupported_type, "types", "image size query requires i32"));
			}
			image_query_lod = fresh_id();
		}
		source_function_type = fresh_id();
		wrapper_function_type = fresh_id();
		wrapper_function = fresh_id();
		wrapper_label = fresh_id();

		emit_preamble();
		emit_debug_names();
		if (!emit_annotations() || !emit_types_constants_globals() || !emit_source_function() || !emit_wrapper()) {
			return std::unexpected(std::move(*error));
		}

		Shader shader{ .stage = stage };
		shader.words.reserve(5 + capabilities.size() + extensions.size() + memory_model.size() +
			entries.size() + execution_modes.size() + debug.size() + annotations.size() +
			types_globals.size() + functions.size());
		shader.words.insert(shader.words.end(), { spv::MagicNumber, spv::Version, 0, next_id, 0 });
		append(shader.words, capabilities);
		append(shader.words, extensions);
		append(shader.words, memory_model);
		append(shader.words, entries);
		append(shader.words, execution_modes);
		append(shader.words, debug);
		append(shader.words, annotations);
		append(shader.words, types_globals);
		append(shader.words, functions);
		return shader;
	}

  private:
	static void append(std::vector<std::uint32_t>& target, const std::vector<std::uint32_t>& source) {
		target.insert(target.end(), source.begin(), source.end());
	}

	Error make_error(ErrorCode code, std::string context, std::string message,
		std::optional<ir::Id> id = std::nullopt, std::optional<ir::Op> op = std::nullopt) const {
		return Error{
			.code = code,
			.stage = stage,
			.id = id,
			.op = op,
			.context = std::move(context),
			.message = std::move(message),
		};
	}

	bool fail(ErrorCode code, std::string context, std::string message,
		std::optional<ir::Id> id = std::nullopt, std::optional<ir::Op> op = std::nullopt) {
		if (!error) {
			error = make_error(code, std::move(context), std::move(message), id, op);
		}
		return false;
	}

	ir::Id fresh_id() {
		return ir::Id{ next_id++ };
	}

	bool uses_image_queries() const {
		return std::ranges::any_of(function->blocks, [](const ir::Block& block) {
			return std::ranges::any_of(block.instructions, [](const ir::Instruction& instruction) {
				return instruction.op == ir::Op::ImageQuerySize;
			});
		});
	}

	std::optional<ir::Id> value_type(ir::Id id) const {
		if (const ir::Constant* constant = program.find_constant(id)) {
			return constant->type;
		}
		if (const ir::Global* global = program.find_global(id)) {
			return global->type;
		}
		for (const auto& parameter : function->parameters) {
			if (parameter.id == id) {
				return parameter.type;
			}
		}
		for (const auto& block : function->blocks) {
			for (const auto& instruction : block.instructions) {
				if (instruction.result_id == id) {
					return instruction.type_id;
				}
			}
		}
		return std::nullopt;
	}

	ir::Id pointer_type(ir::Id value_type, spv::StorageClass storage) {
		for (const auto& pointer : pointer_types) {
			if (pointer.value_type == value_type && pointer.storage == storage) {
				return pointer.id;
			}
		}
		const ir::Id id = fresh_id();
		pointer_types.push_back(PointerType{ .value_type = value_type, .storage = storage, .id = id });
		return id;
	}

	bool prepare_interface(const std::optional<Interface>& interface, spv::StorageClass storage,
		std::vector<InterfaceVariable>& variables) {
		if (!interface) {
			return true;
		}
		if (!program.find_type(interface->value_type)) {
			return fail(ErrorCode::invalid_entry, "entry.interface", "interface payload type does not exist", interface->value_type);
		}
		variables.reserve(interface->elements.size());
		for (const auto& element : interface->elements) {
			if (!program.find_type(element.type)) {
				return fail(ErrorCode::invalid_entry, "entry.interface", "interface element type does not exist", element.type);
			}
			if ((element.builtin == Builtin::none) == !element.location) {
				return fail(ErrorCode::invalid_entry, "entry.interface", "interface element needs exactly one location or built-in", element.type);
			}
			variables.push_back(InterfaceVariable{
				.element = &element,
				.pointer_type = pointer_type(element.type, storage),
				.variable = fresh_id(),
				.storage = storage,
			});
		}
		return true;
	}

	void emit_preamble() {
		emit(capabilities, spv::Op::OpCapability, { word(spv::Capability::Shader) });
		if (uses_image_queries()) {
			emit(capabilities, spv::Op::OpCapability, { word(spv::Capability::ImageQuery) });
		}
		for (const auto* resource : selected_resources) {
			if (resource->kind != ResourceKind::storage_image) {
				continue;
			}
			if (resource->access != Access::write_only) {
				emit(capabilities, spv::Op::OpCapability, { word(spv::Capability::StorageImageReadWithoutFormat) });
			}
			if (resource->access != Access::read_only) {
				emit(capabilities, spv::Op::OpCapability, { word(spv::Capability::StorageImageWriteWithoutFormat) });
			}
		}
		const std::array ext_prefix{ glsl_ext.value };
		emit_string(extensions, spv::Op::OpExtInstImport, ext_prefix, "GLSL.std.450");
		emit(memory_model, spv::Op::OpMemoryModel,
			{ word(spv::AddressingModel::Logical), word(spv::MemoryModel::GLSL450) });

		std::vector<std::uint32_t> interface_ids;
		interface_ids.reserve(inputs.size() + outputs.size() + selected_resources.size());
		for (const auto& input : inputs) {
			interface_ids.push_back(input.variable.value);
		}
		for (const auto& output : outputs) {
			interface_ids.push_back(output.variable.value);
		}
		for (const auto* resource : selected_resources) {
			interface_ids.push_back(resource->variable.value);
		}
		const std::array entry_prefix{
			word(stage == Stage::vertex ? spv::ExecutionModel::Vertex : spv::ExecutionModel::Fragment),
			wrapper_function.value,
		};
		emit_string(entries, spv::Op::OpEntryPoint, entry_prefix, "main", interface_ids);
		if (stage == Stage::fragment) {
			emit(execution_modes, spv::Op::OpExecutionMode,
				{ wrapper_function.value, word(spv::ExecutionMode::OriginUpperLeft) });
		}
	}

	void emit_debug_names() {
		const auto name = [&](ir::Id id, std::string_view value) {
			const std::array prefix{ id.value };
			emit_string(debug, spv::Op::OpName, prefix, value);
		};
		name(wrapper_function, "main");
		name(function->id, function->name.empty() ? entry->name : function->name);
		for (const auto* resource : selected_resources) {
			name(resource->variable, resource->name);
		}
		for (const auto& variable : inputs) {
			name(variable.variable, "in_" + variable.element->name);
		}
		for (const auto& variable : outputs) {
			name(variable.variable, "out_" + variable.element->name);
		}
	}

	bool emit_annotations() {
		const auto is_selected_global = [&](ir::Id id) {
			return std::ranges::any_of(selected_resources, [&](const Resource* resource) { return resource->variable == id; });
		};
		for (const auto& value : program.decorations()) {
			if (program.find_global(value.target) && !is_selected_global(value.target)) {
				continue;
			}
			std::vector<std::uint32_t> operands{ value.target.value };
			if (value.member()) {
				operands.push_back(*value.member());
			}
			operands.push_back(word(decoration(value.kind)));
			operands.insert(operands.end(), value.literals.begin(), value.literals.end());
			emit(annotations, value.member() ? spv::Op::OpMemberDecorate : spv::Op::OpDecorate, operands);
		}

		const auto decorate_interface = [&](const InterfaceVariable& variable) {
			if (variable.element->builtin == Builtin::position) {
				emit(annotations, spv::Op::OpDecorate,
					{ variable.variable.value, word(spv::Decoration::BuiltIn), word(spv::BuiltIn::Position) });
			} else {
				emit(annotations, spv::Op::OpDecorate,
					{ variable.variable.value, word(spv::Decoration::Location), *variable.element->location });
			}
			if (variable.element->interpolation == Interpolation::flat) {
				emit(annotations, spv::Op::OpDecorate,
					{ variable.variable.value, word(spv::Decoration::Flat) });
			}
		};
		for (const auto& input : inputs) {
			decorate_interface(input);
		}
		for (const auto& output : outputs) {
			decorate_interface(output);
		}

		for (const auto* resource : selected_resources) {
			if (resource->access == Access::read_only) {
				emit(annotations, spv::Op::OpDecorate,
					{ resource->variable.value, word(spv::Decoration::NonWritable) });
			}
			if (resource->access == Access::write_only) {
				emit(annotations, spv::Op::OpDecorate,
					{ resource->variable.value, word(spv::Decoration::NonReadable) });
			}
		}
		return true;
	}

	bool emit_type(const ir::Type& type) {
		std::vector<std::uint32_t> operands{ type.id.value };
		switch (type.kind) {
		case ir::TypeKind::void_:
			emit(types_globals, spv::Op::OpTypeVoid, operands);
			return true;
		case ir::TypeKind::boolean:
			emit(types_globals, spv::Op::OpTypeBool, operands);
			return true;
		case ir::TypeKind::signed_integer:
		case ir::TypeKind::unsigned_integer:
			operands.push_back(type.bit_width);
			operands.push_back(type.kind == ir::TypeKind::signed_integer ? 1u : 0u);
			emit(types_globals, spv::Op::OpTypeInt, operands);
			return true;
		case ir::TypeKind::floating:
			operands.push_back(type.bit_width);
			emit(types_globals, spv::Op::OpTypeFloat, operands);
			return true;
		case ir::TypeKind::vector:
			operands.insert(operands.end(), { type.element_type.value, type.element_count });
			emit(types_globals, spv::Op::OpTypeVector, operands);
			return true;
		case ir::TypeKind::matrix:
			operands.insert(operands.end(), { type.element_type.value, type.element_count });
			emit(types_globals, spv::Op::OpTypeMatrix, operands);
			return true;
		case ir::TypeKind::structure:
			for (const auto member : type.members) {
				operands.push_back(member.value);
			}
			emit(types_globals, spv::Op::OpTypeStruct, operands);
			return true;
		case ir::TypeKind::pointer: {
			const auto storage = storage_class(type.storage_class);
			if (!storage) {
				return fail(ErrorCode::unsupported_type, "types.pointer", "pointer uses an unsupported storage class", type.id);
			}
			operands.insert(operands.end(), { word(*storage), type.element_type.value });
			emit(types_globals, spv::Op::OpTypePointer, operands);
			return true;
		}
		case ir::TypeKind::array:
			operands.insert(operands.end(), { type.element_type.value, type.array_length.value });
			emit(types_globals, spv::Op::OpTypeArray, operands);
			return true;
		case ir::TypeKind::runtime_array:
			operands.push_back(type.element_type.value);
			emit(types_globals, spv::Op::OpTypeRuntimeArray, operands);
			return true;
		case ir::TypeKind::function:
			operands.push_back(type.element_type.value);
			for (const auto parameter : type.members) {
				operands.push_back(parameter.value);
			}
			emit(types_globals, spv::Op::OpTypeFunction, operands);
			return true;
		case ir::TypeKind::sampler:
			emit(types_globals, spv::Op::OpTypeSampler, operands);
			return true;
		case ir::TypeKind::image: {
			if (!type.element_type || type.image.dimension == ir::ImageDimension::none) {
				return fail(ErrorCode::unsupported_type, "types.image", "image type is missing its sampled type or shape", type.id);
			}
			const spv::Dim dimension = type.image.dimension == ir::ImageDimension::two ? spv::Dim::Dim2D
				: type.image.dimension == ir::ImageDimension::three ? spv::Dim::Dim3D : spv::Dim::Cube;
			operands.insert(operands.end(), {
				type.element_type.value, word(dimension), 0,
				type.image.arrayed ? 1u : 0u, 0,
				type.image_class == ir::ImageClass::sampled ? 1u : 2u,
				word(spv::ImageFormat::Unknown),
			});
			emit(types_globals, spv::Op::OpTypeImage, operands);
			return true;
		}
		case ir::TypeKind::sampled_image: {
			const ir::Type* image = program.find_type(type.element_type);
			if (!image || image->kind != ir::TypeKind::image || image->image_class != ir::ImageClass::sampled) {
				return fail(ErrorCode::unsupported_type, "types.sampled_image", "sampled-image type does not reference a sampled image", type.id);
			}
			emit(types_globals, spv::Op::OpTypeSampledImage, { type.id.value, type.element_type.value });
			return true;
		}
		}
		return fail(ErrorCode::unsupported_type, "types", "type is not supported by the SPIR-V backend", type.id);
	}

	bool emit_types_constants_globals() {
		for (const auto& type : program.types()) {
			if (!emit_type(type)) {
				return false;
			}
		}
		for (const auto& pointer : pointer_types) {
			emit(types_globals, spv::Op::OpTypePointer,
				{ pointer.id.value, word(pointer.storage), pointer.value_type.value });
		}
		std::vector<std::uint32_t> source_function_signature{ source_function_type.value, function->return_type.value };
		for (const auto& parameter : function->parameters) {
			source_function_signature.push_back(parameter.type.value);
		}
		emit(types_globals, spv::Op::OpTypeFunction, source_function_signature);
		emit(types_globals, spv::Op::OpTypeFunction, { wrapper_function_type.value, void_type.value });

		for (const auto& constant : program.constants()) {
			std::vector<std::uint32_t> operands{ constant.type.value, constant.id.value };
			switch (constant.kind) {
			case ir::ConstantKind::boolean:
				emit(types_globals, constant.words.front() ? spv::Op::OpConstantTrue : spv::Op::OpConstantFalse, operands);
				break;
			case ir::ConstantKind::signed_integer:
			case ir::ConstantKind::unsigned_integer:
			case ir::ConstantKind::floating:
				operands.insert(operands.end(), constant.words.begin(), constant.words.end());
				emit(types_globals, spv::Op::OpConstant, operands);
				break;
			case ir::ConstantKind::composite:
				for (const auto element : constant.elements) {
					operands.push_back(element.value);
				}
				emit(types_globals, spv::Op::OpConstantComposite, operands);
				break;
			}
		}
		if (image_query_lod) {
			emit(types_globals, spv::Op::OpConstant, { signed_int_type.value, image_query_lod.value, 0 });
		}

		for (const auto* resource : selected_resources) {
			const ir::Global* global = program.find_global(resource->variable);
			if (!global) {
				return fail(ErrorCode::invalid_entry, "resources.variable", "resource global does not exist", resource->variable);
			}
			const auto storage = storage_class(global->storage_class);
			if (!storage) {
				return fail(ErrorCode::unsupported_type, "resources.storage", "resource uses an unsupported storage class", global->id);
			}
			std::vector<std::uint32_t> operands{ global->type.value, global->id.value, word(*storage) };
			if (global->initializer) {
				operands.push_back(global->initializer->value);
			}
			emit(types_globals, spv::Op::OpVariable, operands);
		}
		for (const auto& input : inputs) {
			emit(types_globals, spv::Op::OpVariable,
				{ input.pointer_type.value, input.variable.value, word(input.storage) });
		}
		for (const auto& output : outputs) {
			emit(types_globals, spv::Op::OpVariable,
				{ output.pointer_type.value, output.variable.value, word(output.storage) });
		}
		return true;
	}

	bool emit_result_instruction(spv::Op op, const ir::Instruction& instruction,
		std::span<const std::uint32_t> prefix = {}) {
		if (!instruction.result_id || !instruction.type_id) {
			return fail(ErrorCode::unsupported_instruction, "instructions", "result instruction is missing its result or type", instruction.result_id, instruction.op);
		}
		std::vector<std::uint32_t> operands{ instruction.type_id.value, instruction.result_id.value };
		operands.insert(operands.end(), prefix.begin(), prefix.end());
		append_arguments(operands, instruction.arguments);
		emit(functions, op, operands);
		return true;
	}

	void emit_instruction_arguments(spv::Op op, const ir::Instruction& instruction) {
		std::vector<std::uint32_t> operands;
		append_arguments(operands, instruction.arguments);
		emit(functions, op, operands);
	}

	bool emit_instruction(const ir::Instruction& instruction) {
		if (const auto operation = direct_operation(instruction.op)) {
			return emit_result_instruction(*operation, instruction);
		}
		switch (instruction.op) {
		case ir::Op::Nop:
			emit(functions, spv::Op::OpNop);
			return true;
		case ir::Op::Variable: {
			const ir::Type* pointer = program.find_type(instruction.type_id);
			const auto* arguments = instruction.arguments_if<ir::VariableArguments>();
			if (!instruction.result_id || !pointer || pointer->kind != ir::TypeKind::pointer || !arguments) {
				return fail(ErrorCode::unsupported_instruction, "instructions.variable", "local variable has an invalid shape", instruction.result_id, instruction.op);
			}
			const auto storage = storage_class(pointer->storage_class);
			if (storage != spv::StorageClass::Function) {
				return fail(ErrorCode::unsupported_instruction, "instructions.variable", "function variable does not use function storage", instruction.result_id, instruction.op);
			}
			std::vector<std::uint32_t> operands{ instruction.type_id.value, instruction.result_id.value, word(*storage) };
			append_arguments(operands, *arguments);
			emit(functions, spv::Op::OpVariable, operands);
			return true;
		}
		case ir::Op::Store:
			emit_instruction_arguments(spv::Op::OpStore, instruction);
			return true;
		case ir::Op::Cross: {
			const std::array prefix{ glsl_ext.value, static_cast<std::uint32_t>(GLSLstd450Cross) };
			return emit_result_instruction(spv::Op::OpExtInst, instruction, prefix);
		}
		case ir::Op::FAbs:
		case ir::Op::Floor:
		case ir::Op::Fract:
		case ir::Op::Sqrt:
		case ir::Op::FMin:
		case ir::Op::FMax:
		case ir::Op::FMix:
		case ir::Op::SmoothStep: {
			const std::uint32_t operation = instruction.op == ir::Op::FAbs ? GLSLstd450FAbs
				: instruction.op == ir::Op::Floor ? GLSLstd450Floor
				: instruction.op == ir::Op::Fract ? GLSLstd450Fract
				: instruction.op == ir::Op::Sqrt ? GLSLstd450Sqrt
				: instruction.op == ir::Op::FMin ? GLSLstd450FMin
				: instruction.op == ir::Op::FMax ? GLSLstd450FMax
				: instruction.op == ir::Op::FMix ? GLSLstd450FMix : GLSLstd450SmoothStep;
			const std::array prefix{ glsl_ext.value, operation };
			return emit_result_instruction(spv::Op::OpExtInst, instruction, prefix);
		}
		case ir::Op::ImageQuerySize: {
			const auto* arguments = instruction.arguments_if<ir::UnaryArguments>();
			const auto sampled_type_id = arguments ? value_type(arguments->operand) : std::nullopt;
			const ir::Type* sampled_type = sampled_type_id ? program.find_type(*sampled_type_id) : nullptr;
			if (!arguments || !sampled_type || sampled_type->kind != ir::TypeKind::sampled_image) {
				return fail(ErrorCode::unsupported_instruction, "instructions.image_query_size", "image query operand is not a sampled image", instruction.result_id, instruction.op);
			}
			const ir::Id image = fresh_id();
			emit(functions, spv::Op::OpImage, { sampled_type->element_type.value, image.value, arguments->operand.value });
			emit(functions, spv::Op::OpImageQuerySizeLod, { instruction.type_id.value, instruction.result_id.value, image.value, image_query_lod.value });
			return true;
		}
		case ir::Op::Branch:
			emit_instruction_arguments(spv::Op::OpBranch, instruction);
			return true;
		case ir::Op::BranchConditional:
			emit_instruction_arguments(spv::Op::OpBranchConditional, instruction);
			return true;
		case ir::Op::SelectionMerge:
			emit_instruction_arguments(spv::Op::OpSelectionMerge, instruction);
			return true;
		case ir::Op::LoopMerge:
			emit_instruction_arguments(spv::Op::OpLoopMerge, instruction);
			return true;
		case ir::Op::Return:
			emit(functions, spv::Op::OpReturn);
			return true;
		case ir::Op::ReturnValue:
			emit_instruction_arguments(spv::Op::OpReturnValue, instruction);
			return true;
		case ir::Op::ImageWrite:
			emit_instruction_arguments(spv::Op::OpImageWrite, instruction);
			return true;
		default:
			return fail(ErrorCode::unsupported_instruction, "instructions", "RTIR operation is not supported by the SPIR-V backend", instruction.result_id, instruction.op);
		}
	}

	bool emit_source_function() {
		emit(functions, spv::Op::OpFunction,
			{ function->return_type.value, function->id.value,
				word(spv::FunctionControlMask::MaskNone), source_function_type.value });
		for (const auto& parameter : function->parameters) {
			emit(functions, spv::Op::OpFunctionParameter, { parameter.type.value, parameter.id.value });
		}
		for (std::size_t block_index = 0; block_index < function->blocks.size(); ++block_index) {
			const auto& block = function->blocks[block_index];
			emit(functions, spv::Op::OpLabel, { block.id.value });
			if (block_index == 0) {
				for (const auto& instruction : block.instructions) {
					if (instruction.op == ir::Op::Variable && !emit_instruction(instruction)) {
						return false;
					}
				}
			}
			for (const auto& instruction : block.instructions) {
				if (instruction.op == ir::Op::Variable) {
					if (block_index != 0) {
						return fail(ErrorCode::unsupported_instruction, "instructions.variable", "local variable appears outside the entry block", instruction.result_id, instruction.op);
					}
					continue;
				}
				if (!emit_instruction(instruction)) {
					return false;
				}
			}
		}
		emit(functions, spv::Op::OpFunctionEnd);
		return true;
	}

	std::optional<ir::Id> load_interface_payload(const Interface& interface,
		std::span<const InterfaceVariable> variables) {
		std::vector<std::pair<std::optional<std::uint32_t>, ir::Id>> loaded;
		loaded.reserve(variables.size());
		for (const auto& variable : variables) {
			const ir::Id value = fresh_id();
			emit(functions, spv::Op::OpLoad, { variable.element->type.value, value.value, variable.variable.value });
			loaded.emplace_back(variable.element->member, value);
		}

		const ir::Type* payload = program.find_type(interface.value_type);
		if (!payload) {
			fail(ErrorCode::invalid_entry, "entry.input", "input payload type does not exist", interface.value_type);
			return std::nullopt;
		}
		if (payload->kind != ir::TypeKind::structure) {
			if (loaded.size() != 1 || loaded.front().first) {
				fail(ErrorCode::invalid_entry, "entry.input", "non-struct input must contain one whole-value element", interface.value_type);
				return std::nullopt;
			}
			return loaded.front().second;
		}

		std::vector<std::uint32_t> members;
		members.reserve(payload->members.size());
		for (std::uint32_t index = 0; index < payload->members.size(); ++index) {
			const auto found = std::ranges::find_if(loaded, [&](const auto& value) { return value.first == index; });
			if (found != loaded.end()) {
				members.push_back(found->second.value);
				continue;
			}
			const ir::Id undef = fresh_id();
			emit(types_globals, spv::Op::OpUndef, { payload->members[index].value, undef.value });
			members.push_back(undef.value);
		}
		const ir::Id result = fresh_id();
		std::vector<std::uint32_t> operands{ interface.value_type.value, result.value };
		operands.insert(operands.end(), members.begin(), members.end());
		emit(functions, spv::Op::OpCompositeConstruct, operands);
		return result;
	}

	bool store_interface_payload(const Interface& interface, std::span<const InterfaceVariable> variables,
		ir::Id payload_value) {
		const ir::Type* payload = program.find_type(interface.value_type);
		if (!payload) {
			return fail(ErrorCode::invalid_entry, "entry.output", "output payload type does not exist", interface.value_type);
		}
		if (payload->kind != ir::TypeKind::structure) {
			if (variables.size() != 1 || variables.front().element->member) {
				return fail(ErrorCode::invalid_entry, "entry.output", "non-struct output must contain one whole-value element", interface.value_type);
			}
			emit(functions, spv::Op::OpStore, { variables.front().variable.value, payload_value.value });
			return true;
		}
		for (const auto& variable : variables) {
			if (!variable.element->member || *variable.element->member >= payload->members.size()) {
				return fail(ErrorCode::invalid_entry, "entry.output", "struct output element has an invalid member index", interface.value_type);
			}
			const ir::Id extracted = fresh_id();
			emit(functions, spv::Op::OpCompositeExtract,
				{ variable.element->type.value, extracted.value, payload_value.value, *variable.element->member });
			emit(functions, spv::Op::OpStore, { variable.variable.value, extracted.value });
		}
		return true;
	}

	bool emit_wrapper() {
		emit(functions, spv::Op::OpFunction,
			{ void_type.value, wrapper_function.value,
				word(spv::FunctionControlMask::MaskNone), wrapper_function_type.value });
		emit(functions, spv::Op::OpLabel, { wrapper_label.value });

		std::vector<std::uint32_t> arguments;
		if (entry->input) {
			const auto payload = load_interface_payload(*entry->input, inputs);
			if (!payload) {
				return false;
			}
			arguments.push_back(payload->value);
		}
		const ir::Id call_result = fresh_id();
		std::vector<std::uint32_t> call{ function->return_type.value, call_result.value, function->id.value };
		call.insert(call.end(), arguments.begin(), arguments.end());
		emit(functions, spv::Op::OpFunctionCall, call);
		if (entry->output && !store_interface_payload(*entry->output, outputs, call_result)) {
			return false;
		}
		emit(functions, spv::Op::OpReturn);
		emit(functions, spv::Op::OpFunctionEnd);
		return true;
	}

	const Program& program;
	Stage stage;
	const EntryPoint* entry = nullptr;
	const ir::Function* function = nullptr;
	std::uint32_t next_id = 1;
	std::optional<Error> error;

	ir::Id void_type{};
	ir::Id glsl_ext{};
	ir::Id signed_int_type{};
	ir::Id image_query_lod{};
	ir::Id source_function_type{};
	ir::Id wrapper_function_type{};
	ir::Id wrapper_function{};
	ir::Id wrapper_label{};

	std::vector<const Resource*> selected_resources;
	std::vector<PointerType> pointer_types;
	std::vector<InterfaceVariable> inputs;
	std::vector<InterfaceVariable> outputs;

	std::vector<std::uint32_t> capabilities;
	std::vector<std::uint32_t> extensions;
	std::vector<std::uint32_t> memory_model;
	std::vector<std::uint32_t> entries;
	std::vector<std::uint32_t> execution_modes;
	std::vector<std::uint32_t> debug;
	std::vector<std::uint32_t> annotations;
	std::vector<std::uint32_t> types_globals;
	std::vector<std::uint32_t> functions;
};

} // namespace

std::expected<Shader, Error> transpile(const Program& program, Stage stage) {
	try {
		return SpirvEmitter{ program, stage }.run();
	} catch (const std::bad_alloc&) {
		return std::unexpected(Error{
			.code = ErrorCode::allocation_failure,
			.stage = stage,
			.context = "rtsl::spirv::transpile",
			.message = "allocation failed while emitting SPIR-V",
		});
	} catch (const std::exception& exception) {
		return std::unexpected(Error{
			.code = ErrorCode::unsupported_instruction,
			.stage = stage,
			.context = "rtsl::spirv::transpile",
			.message = exception.what(),
		});
	}
}

} // namespace rtsl::spirv
