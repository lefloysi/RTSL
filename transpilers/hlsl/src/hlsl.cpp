#include "rtsl/hlsl.hpp"

#include <algorithm>
#include <bit>
#include <cctype>
#include <cmath>
#include <format>
#include <limits>
#include <new>
#include <ranges>
#include <span>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace rtsl::hlsl {

namespace {

std::string identifier(std::string_view value) {
	std::string result;
	result.reserve(value.size() + 1);
	for (const char character : value) {
		const unsigned char byte = static_cast<unsigned char>(character);
		result.push_back(std::isalnum(byte) || character == '_' ? character : '_');
	}
	if (result.empty() || std::isdigit(static_cast<unsigned char>(result.front()))) {
		result.insert(result.begin(), '_');
	}
	return result;
}

std::string value_name(ir::Id id) {
	return std::format("v{}", id.value);
}

std::string type_name(ir::Id id) {
	return std::format("rtsl_type_{}", id.value);
}

std::string field_name(std::uint32_t index) {
	return std::format("m{}", index);
}

std::string semantic(const InterfaceElement& element, Stage stage, bool output) {
	if (element.builtin == Builtin::position) {
		return "SV_Position";
	}
	if (stage == Stage::fragment && output) {
		return std::format("SV_Target{}", element.location.value_or(0));
	}
	return std::format("TEXCOORD{}", element.location.value_or(0));
}

class Emitter {
  public:
	Emitter(const Program& program, Stage stage) : program(program), stage(stage) {}

	std::expected<Shader, Error> run() {
		entry = program.entry(stage);
		if (!entry) {
			return std::unexpected(make_error(ErrorCode::stage_not_found, "entry", "requested stage is not present"));
		}
		function = program.find_function(entry->function);
		if (!function) {
			return std::unexpected(make_error(ErrorCode::invalid_entry, "entry.function", "entry function does not exist", entry->function));
		}
		if (function->parameters.size() > 1 || (function->parameters.empty() != !entry->input)) {
			return std::unexpected(make_error(ErrorCode::invalid_entry, "entry.parameters", "HLSL stage entries support zero or one payload parameter", function->id));
		}
		if (entry->input && (!entry->input->value || *entry->input->value != function->parameters.front().id ||
			entry->input->value_type != function->parameters.front().type)) {
			return std::unexpected(make_error(ErrorCode::invalid_entry, "entry.input", "entry input does not match its function parameter", function->id));
		}
		if (!entry->output) {
			return std::unexpected(make_error(ErrorCode::invalid_entry, "entry.output", "graphics entry has no output interface", function->id));
		}

		index_values();
		for (const auto& resource : program.resources()) {
			if (contains(resource.stages, stage)) {
				resources.push_back(&resource);
				resource_by_variable.emplace(resource.variable, &resource);
			}
		}

		if (!emit_types() || !emit_resources() || !emit_interfaces() || !emit_entry()) {
			return std::unexpected(std::move(*error));
		}
		return Shader{ .stage = stage, .source = std::move(source) };
	}

  private:
	Error make_error(
		ErrorCode code,
		std::string context,
		std::string message,
		std::optional<ir::Id> id = std::nullopt,
		std::optional<ir::Op> op = std::nullopt
	) const {
		return Error{
			.code = code,
			.stage = stage,
			.id = id,
			.op = op,
			.context = std::move(context),
			.message = std::move(message),
		};
	}

	bool fail(
		ErrorCode code,
		std::string context,
		std::string message,
		std::optional<ir::Id> id = std::nullopt,
		std::optional<ir::Op> op = std::nullopt
	) {
		if (!error) {
			error = make_error(code, std::move(context), std::move(message), id, op);
		}
		return false;
	}

	void line(std::string_view value = {}) {
		source.append(static_cast<std::size_t>(indent) * 4, ' ');
		source.append(value);
		source.push_back('\n');
	}

	void index_values() {
		for (const auto& constant : program.constants()) {
			value_types.emplace(constant.id, constant.type);
		}
		for (const auto& global : program.globals()) {
			value_types.emplace(global.id, global.type);
		}
		for (const auto& parameter : function->parameters) {
			value_types.emplace(parameter.id, parameter.type);
		}
		for (const auto& block : function->blocks) {
			for (const auto& instruction : block.instructions) {
				if (instruction.result_id) {
					value_types.emplace(instruction.result_id, instruction.type_id);
				}
			}
		}
	}

	const ir::Type* type(ir::Id id) const {
		return program.find_type(id);
	}

	std::optional<ir::Id> value_type(ir::Id id) const {
		const auto found = value_types.find(id);
		if (found == value_types.end()) {
			return std::nullopt;
		}
		return found->second;
	}

	std::optional<std::string> hlsl_type(ir::Id id) {
		const ir::Type* value = type(id);
		if (!value) {
			fail(ErrorCode::unsupported_type, "types", "type does not exist", id);
			return std::nullopt;
		}
		switch (value->kind) {
		case ir::TypeKind::void_: return "void";
		case ir::TypeKind::boolean: return "bool";
		case ir::TypeKind::signed_integer:
			if (value->bit_width == 32) {
				return "int";
			}
			break;
		case ir::TypeKind::unsigned_integer:
			if (value->bit_width == 32) {
				return "uint";
			}
			break;
		case ir::TypeKind::floating:
			if (value->bit_width == 32) {
				return "float";
			}
			break;
		case ir::TypeKind::vector: {
			const auto element = hlsl_type(value->element_type);
			if (element && value->element_count >= 2 && value->element_count <= 4) {
				return std::format("{}{}", *element, value->element_count);
			}
			break;
		}
		case ir::TypeKind::matrix: {
			const ir::Type* column = type(value->element_type);
			if (column && column->kind == ir::TypeKind::vector && column->element_count == value->element_count) {
				const auto element = hlsl_type(column->element_type);
				if (element && *element == "float" && value->element_count >= 2 && value->element_count <= 4) {
					return std::format("column_major float{}x{}", value->element_count, value->element_count);
				}
			}
			break;
		}
		case ir::TypeKind::structure: return type_name(id);
		case ir::TypeKind::pointer: return hlsl_type(value->element_type);
		case ir::TypeKind::image:
		case ir::TypeKind::sampler:
		case ir::TypeKind::sampled_image:
			return std::nullopt;
		case ir::TypeKind::array:
		case ir::TypeKind::function:
			break;
		}
		fail(ErrorCode::unsupported_type, "types", "type is not supported by the HLSL backend", id);
		return std::nullopt;
	}

	bool emit_types() {
		line("// Generated from linked RTSL IR. Do not edit.");
		line();
		for (const auto& value : program.types()) {
			if (value.kind != ir::TypeKind::structure) {
				continue;
			}
			line(std::format("struct {} {{", type_name(value.id)));
			++indent;
			for (std::uint32_t index = 0; index < value.members.size(); ++index) {
				const auto member_type = hlsl_type(value.members[index]);
				if (!member_type) {
					return false;
				}
				line(std::format("{} {};", *member_type, field_name(index)));
			}
			--indent;
			line("};");
			line();
		}
		return true;
	}

	std::string resource_name(const Resource& resource) const {
		return "rtsl_resource_" + identifier(resource.name);
	}

	bool emit_resources() {
		for (const Resource* resource : resources) {
			const std::string name = resource_name(*resource);
			const auto space = resource->descriptor.set;
			const auto binding = resource->descriptor.binding;
			switch (resource->kind) {
			case ResourceKind::uniform_buffer: {
				const auto value = hlsl_type(resource->value_type);
				if (!value) {
					return false;
				}
				line(std::format("cbuffer {}_buffer : register(b{}, space{}) {{", name, binding, space));
				++indent;
				line(std::format("{} {};", *value, name));
				--indent;
				line("};");
				break;
			}
			case ResourceKind::sampled_texture:
			case ResourceKind::sampler:
				line(std::format("Texture2D<float4> {}_texture : register(t{}, space{});", name, binding, space));
				line(std::format("SamplerState {}_sampler : register(s{}, space{});", name, binding, space));
				break;
			case ResourceKind::storage_buffer:
			case ResourceKind::storage_image:
				return fail(ErrorCode::unsupported_resource, "resources", "storage resources are not implemented by the HLSL backend", resource->variable);
			}
			line();
		}
		return true;
	}

	bool emit_interface(std::string_view name, const Interface* interface, bool output) {
		line(std::format("struct {} {{", name));
		++indent;
		if (interface) {
			for (const auto& element : interface->elements) {
				const auto value = hlsl_type(element.type);
				if (!value) {
					return false;
				}
				const std::string interpolation = element.interpolation == Interpolation::flat ? "nointerpolation " : "";
				line(std::format("{}{} {} : {};", interpolation, *value, identifier(element.name), semantic(element, stage, output)));
			}
		}
		--indent;
		line("};");
		line();
		return true;
	}

	bool emit_interfaces() {
		if (entry->input) {
			const Interface* input = &*entry->input;
			if (stage == Stage::fragment) {
				const EntryPoint* vertex = program.entry(Stage::vertex);
				if (vertex && vertex->output) {
					input = &*vertex->output;
				}
			}
			if (!emit_interface("RtslInput", input, false)) {
				return false;
			}
		}
		return emit_interface("RtslOutput", &*entry->output, true);
	}

	std::optional<std::uint64_t> constant_unsigned(ir::Id id) const {
		const ir::Constant* constant = program.find_constant(id);
		if (!constant || constant->words.empty()) {
			return std::nullopt;
		}
		return constant->words.front();
	}

	std::optional<std::string> constant_expression(const ir::Constant& constant) {
		switch (constant.kind) {
		case ir::ConstantKind::boolean:
			return constant.words.front() ? "true" : "false";
		case ir::ConstantKind::signed_integer:
			return std::format("{}", static_cast<std::int32_t>(constant.words.front()));
		case ir::ConstantKind::unsigned_integer:
			return std::format("{}u", constant.words.front());
		case ir::ConstantKind::floating: {
			const float value = std::bit_cast<float>(constant.words.front());
			if (!std::isfinite(value)) {
				return std::format("asfloat(0x{:08x}u)", constant.words.front());
			}
			std::string result = std::format("{:.9g}", value);
			if (result.find_first_of(".eE") == std::string::npos) {
				result += ".0";
			}
			return result;
		}
		case ir::ConstantKind::composite: {
			const auto value_type_name = hlsl_type(constant.type);
			if (!value_type_name) {
				return std::nullopt;
			}
			std::string result = *value_type_name + "(";
			for (std::size_t index = 0; index < constant.elements.size(); ++index) {
				if (index) {
					result += ", ";
				}
				const auto element = expression(constant.elements[index]);
				if (!element) {
					return std::nullopt;
				}
				result += *element;
			}
			result += ")";
			return result;
		}
		}
		return std::nullopt;
	}

	std::optional<std::string> expression(ir::Id id) {
		if (const ir::Constant* constant = program.find_constant(id)) {
			return constant_expression(*constant);
		}
		if (const auto found = expressions.find(id); found != expressions.end()) {
			return found->second;
		}
		return value_name(id);
	}

	std::optional<std::string> pointer_expression(ir::Id id) {
		if (const auto found = pointers.find(id); found != pointers.end()) {
			return found->second;
		}
		if (const auto found = resource_by_variable.find(id); found != resource_by_variable.end()) {
			return resource_name(*found->second);
		}
		return std::nullopt;
	}

	bool declare_values() {
		std::unordered_set<ir::Id> declared;
		for (const auto& parameter : function->parameters) {
			const auto value = hlsl_type(parameter.type);
			if (!value) {
				return false;
			}
			line(std::format("{} {};", *value, value_name(parameter.id)));
			declared.insert(parameter.id);
		}
		for (const auto& block : function->blocks) {
			for (const auto& instruction : block.instructions) {
				if (!instruction.result_id || declared.contains(instruction.result_id) || instruction.op == ir::Op::AccessChain) {
					continue;
				}
				if (instruction.op == ir::Op::Load) {
					const auto* arguments = instruction.arguments_if<ir::LoadArguments>();
					if (arguments && resource_by_variable.contains(arguments->pointer)) {
						const Resource* resource = resource_by_variable.at(arguments->pointer);
						if (resource->kind == ResourceKind::sampled_texture || resource->kind == ResourceKind::sampler) {
							continue;
						}
					}
				}
				ir::Id declaration_type = instruction.type_id;
				if (instruction.op == ir::Op::Variable) {
					const ir::Type* pointer = type(instruction.type_id);
					if (!pointer || pointer->kind != ir::TypeKind::pointer) {
						return fail(ErrorCode::unsupported_instruction, "instructions.variable", "local variable has an invalid pointer type", instruction.result_id, instruction.op);
					}
					declaration_type = pointer->element_type;
					pointers.emplace(instruction.result_id, value_name(instruction.result_id));
				}
				const auto value = hlsl_type(declaration_type);
				if (!value) {
					return false;
				}
				line(std::format("{} {};", *value, value_name(instruction.result_id)));
				declared.insert(instruction.result_id);
			}
		}
		return true;
	}

	bool initialize_input() {
		if (!entry->input) {
			return true;
		}
		const ir::Type* payload = type(entry->input->value_type);
		if (!payload) {
			return fail(ErrorCode::invalid_entry, "entry.input", "input payload type does not exist", entry->input->value_type);
		}
		const ir::Id parameter = *entry->input->value;
		if (payload->kind != ir::TypeKind::structure) {
			if (entry->input->elements.size() != 1 || entry->input->elements.front().member) {
				return fail(ErrorCode::invalid_entry, "entry.input", "non-struct input must have one whole-value element", payload->id);
			}
			line(std::format("{} = input.{};", value_name(parameter), identifier(entry->input->elements.front().name)));
			return true;
		}
		for (const auto& element : entry->input->elements) {
			if (!element.member || *element.member >= payload->members.size()) {
				return fail(ErrorCode::invalid_entry, "entry.input", "struct input has an invalid member index", payload->id);
			}
			line(std::format("{}.{} = input.{};", value_name(parameter), field_name(*element.member), identifier(element.name)));
		}
		return true;
	}

	bool emit_output(ir::Id value) {
		const Interface& output = *entry->output;
		const ir::Type* payload = type(output.value_type);
		if (!payload) {
			return fail(ErrorCode::invalid_entry, "entry.output", "output payload type does not exist", output.value_type);
		}
		const auto result = expression(value);
		if (!result) {
			return false;
		}
		if (payload->kind != ir::TypeKind::structure) {
			if (output.elements.size() != 1 || output.elements.front().member) {
				return fail(ErrorCode::invalid_entry, "entry.output", "non-struct output must have one whole-value element", payload->id);
			}
			line(std::format("output.{} = {};", identifier(output.elements.front().name), *result));
		} else {
			for (const auto& element : output.elements) {
				if (!element.member || *element.member >= payload->members.size()) {
					return fail(ErrorCode::invalid_entry, "entry.output", "struct output has an invalid member index", payload->id);
				}
				line(std::format("output.{} = {}.{};", identifier(element.name), *result, field_name(*element.member)));
			}
		}
		line("return output;");
		return true;
	}

	std::optional<std::string> binary_expression(const ir::Instruction& instruction, std::string_view operation) {
		const auto* arguments = instruction.arguments_if<ir::BinaryArguments>();
		if (!arguments) {
			fail(ErrorCode::unsupported_instruction, "instructions.binary", "binary operation has invalid arguments", instruction.result_id, instruction.op);
			return std::nullopt;
		}
		const auto lhs = expression(arguments->lhs);
		const auto rhs = expression(arguments->rhs);
		if (!lhs || !rhs) {
			return std::nullopt;
		}
		return std::format("({} {} {})", *lhs, operation, *rhs);
	}

	bool assign(ir::Id id, std::string value) {
		line(std::format("{} = {};", value_name(id), value));
		return true;
	}

	bool emit_access_chain(const ir::Instruction& instruction) {
		const auto* arguments = instruction.arguments_if<ir::AccessChainArguments>();
		if (!arguments) {
			return fail(ErrorCode::unsupported_instruction, "instructions.access_chain", "access chain has invalid arguments", instruction.result_id, instruction.op);
		}
		auto result = pointer_expression(arguments->base);
		const auto base_value_type = value_type(arguments->base);
		const ir::Type* current = base_value_type ? type(*base_value_type) : nullptr;
		if (!result || !current || current->kind != ir::TypeKind::pointer) {
			return fail(ErrorCode::unsupported_instruction, "instructions.access_chain", "access chain base is not a known pointer", instruction.result_id, instruction.op);
		}
		current = type(current->element_type);
		for (const ir::Id index_id : arguments->indices) {
			const auto index = constant_unsigned(index_id);
			if (!index || !current) {
				return fail(ErrorCode::unsupported_instruction, "instructions.access_chain", "access chain requires constant valid indices", instruction.result_id, instruction.op);
			}
			if (current->kind == ir::TypeKind::structure) {
				if (*index >= current->members.size()) {
					return fail(ErrorCode::unsupported_instruction, "instructions.access_chain", "struct access index is out of range", instruction.result_id, instruction.op);
				}
				*result += "." + field_name(static_cast<std::uint32_t>(*index));
				current = type(current->members[static_cast<std::size_t>(*index)]);
			} else if (current->kind == ir::TypeKind::vector || current->kind == ir::TypeKind::matrix || current->kind == ir::TypeKind::array) {
				*result += std::format("[{}]", *index);
				current = type(current->element_type);
			} else {
				return fail(ErrorCode::unsupported_instruction, "instructions.access_chain", "access chain traverses a non-composite type", instruction.result_id, instruction.op);
			}
		}
		pointers[instruction.result_id] = std::move(*result);
		return true;
	}

	bool emit_construct(const ir::Instruction& instruction) {
		const auto* arguments = instruction.arguments_if<ir::CompositeConstructArguments>();
		const ir::Type* result_type = type(instruction.type_id);
		if (!arguments || !result_type) {
			return fail(ErrorCode::unsupported_instruction, "instructions.construct", "composite construction has invalid arguments", instruction.result_id, instruction.op);
		}
		if (result_type->kind == ir::TypeKind::structure) {
			if (arguments->constituents.size() != result_type->members.size()) {
				return fail(ErrorCode::unsupported_instruction, "instructions.construct", "struct construction has the wrong member count", instruction.result_id, instruction.op);
			}
			for (std::uint32_t index = 0; index < arguments->constituents.size(); ++index) {
				const auto value = expression(arguments->constituents[index]);
				if (!value) {
					return false;
				}
				line(std::format("{}.{} = {};", value_name(instruction.result_id), field_name(index), *value));
			}
			return true;
		}
		const auto result_name = hlsl_type(instruction.type_id);
		if (!result_name) {
			return false;
		}
		std::string value = *result_name + "(";
		for (std::size_t index = 0; index < arguments->constituents.size(); ++index) {
			if (index) {
				value += ", ";
			}
			const auto constituent = expression(arguments->constituents[index]);
			if (!constituent) {
				return false;
			}
			value += *constituent;
		}
		value += ")";
		return assign(instruction.result_id, std::move(value));
	}

	bool emit_extract(const ir::Instruction& instruction) {
		const auto* arguments = instruction.arguments_if<ir::CompositeExtractArguments>();
		if (!arguments) {
			return fail(ErrorCode::unsupported_instruction, "instructions.extract", "composite extraction has invalid arguments", instruction.result_id, instruction.op);
		}
		auto result = expression(arguments->composite);
		auto current_id = value_type(arguments->composite);
		const ir::Type* current = current_id ? type(*current_id) : nullptr;
		if (!result || !current) {
			return false;
		}
		for (const std::uint32_t index : arguments->indices) {
			if (current->kind == ir::TypeKind::structure) {
				if (index >= current->members.size()) {
					return fail(ErrorCode::unsupported_instruction, "instructions.extract", "struct extraction index is out of range", instruction.result_id, instruction.op);
				}
				*result += "." + field_name(index);
				current = type(current->members[index]);
			} else if (current->kind == ir::TypeKind::vector || current->kind == ir::TypeKind::matrix || current->kind == ir::TypeKind::array) {
				*result += std::format("[{}]", index);
				current = type(current->element_type);
			} else {
				return fail(ErrorCode::unsupported_instruction, "instructions.extract", "extraction traverses a non-composite type", instruction.result_id, instruction.op);
			}
		}
		return assign(instruction.result_id, std::move(*result));
	}

	bool emit_insert(const ir::Instruction& instruction) {
		const auto* arguments = instruction.arguments_if<ir::CompositeInsertArguments>();
		if (!arguments) {
			return fail(ErrorCode::unsupported_instruction, "instructions.insert", "composite insertion has invalid arguments", instruction.result_id, instruction.op);
		}
		const auto composite = expression(arguments->composite);
		const auto object = expression(arguments->object);
		if (!composite || !object) {
			return false;
		}
		line(std::format("{} = {};", value_name(instruction.result_id), *composite));
		std::string target = value_name(instruction.result_id);
		const ir::Type* current = type(instruction.type_id);
		for (const std::uint32_t index : arguments->indices) {
			if (!current) {
				return false;
			}
			if (current->kind == ir::TypeKind::structure) {
				target += "." + field_name(index);
				current = index < current->members.size() ? type(current->members[index]) : nullptr;
			} else {
				target += std::format("[{}]", index);
				current = type(current->element_type);
			}
		}
		line(std::format("{} = {};", target, *object));
		return true;
	}

	bool emit_shuffle(const ir::Instruction& instruction) {
		const auto* arguments = instruction.arguments_if<ir::VectorShuffleArguments>();
		const auto first_type_id = arguments ? value_type(arguments->first) : std::nullopt;
		const ir::Type* first_type = first_type_id ? type(*first_type_id) : nullptr;
		const auto result_type = hlsl_type(instruction.type_id);
		const auto first = arguments ? expression(arguments->first) : std::nullopt;
		const auto second = arguments ? expression(arguments->second) : std::nullopt;
		if (!arguments || !first_type || first_type->kind != ir::TypeKind::vector || !result_type || !first || !second) {
			return fail(ErrorCode::unsupported_instruction, "instructions.shuffle", "vector shuffle has invalid arguments", instruction.result_id, instruction.op);
		}
		std::string value = *result_type + "(";
		for (std::size_t index = 0; index < arguments->components.size(); ++index) {
			if (index) {
				value += ", ";
			}
			const std::uint32_t component = arguments->components[index];
			if (component < first_type->element_count) {
				value += std::format("{}[{}]", *first, component);
			} else {
				value += std::format("{}[{}]", *second, component - first_type->element_count);
			}
		}
		value += ")";
		return assign(instruction.result_id, std::move(value));
	}

	bool emit_instruction(const ir::Instruction& instruction, bool state_machine) {
		switch (instruction.op) {
		case ir::Op::Nop:
		case ir::Op::SelectionMerge:
		case ir::Op::LoopMerge:
			return true;
		case ir::Op::Variable: {
			const auto* arguments = instruction.arguments_if<ir::VariableArguments>();
			if (!arguments) {
				return false;
			}
			if (arguments->initializer) {
				const auto value = expression(*arguments->initializer);
				if (!value) {
					return false;
				}
				line(std::format("{} = {};", value_name(instruction.result_id), *value));
			}
			return true;
		}
		case ir::Op::Load: {
			const auto* arguments = instruction.arguments_if<ir::LoadArguments>();
			const auto pointer = arguments ? pointer_expression(arguments->pointer) : std::nullopt;
			if (!arguments || !pointer) {
				return fail(ErrorCode::unsupported_instruction, "instructions.load", "load references an unknown pointer", instruction.result_id, instruction.op);
			}
			if (const auto found = resource_by_variable.find(arguments->pointer); found != resource_by_variable.end() &&
				(found->second->kind == ResourceKind::sampled_texture || found->second->kind == ResourceKind::sampler)) {
				expressions[instruction.result_id] = *pointer;
				return true;
			}
			return assign(instruction.result_id, *pointer);
		}
		case ir::Op::Store: {
			const auto* arguments = instruction.arguments_if<ir::StoreArguments>();
			const auto pointer = arguments ? pointer_expression(arguments->pointer) : std::nullopt;
			const auto value = arguments ? expression(arguments->value) : std::nullopt;
			if (!arguments || !pointer || !value) {
				return fail(ErrorCode::unsupported_instruction, "instructions.store", "store has invalid arguments", instruction.result_id, instruction.op);
			}
			line(std::format("{} = {};", *pointer, *value));
			return true;
		}
		case ir::Op::AccessChain: return emit_access_chain(instruction);
		case ir::Op::CompositeConstruct: return emit_construct(instruction);
		case ir::Op::CompositeExtract: return emit_extract(instruction);
		case ir::Op::CompositeInsert: return emit_insert(instruction);
		case ir::Op::VectorShuffle: return emit_shuffle(instruction);
		case ir::Op::FNegate:
		case ir::Op::LogicalNot: {
			const auto* arguments = instruction.arguments_if<ir::UnaryArguments>();
			const auto operand = arguments ? expression(arguments->operand) : std::nullopt;
			if (!operand) {
				return false;
			}
			return assign(instruction.result_id, std::format("({}{})", instruction.op == ir::Op::FNegate ? "-" : "!", *operand));
		}
		case ir::Op::FMod: {
			const auto* arguments = instruction.arguments_if<ir::BinaryArguments>();
			const auto lhs = arguments ? expression(arguments->lhs) : std::nullopt;
			const auto rhs = arguments ? expression(arguments->rhs) : std::nullopt;
			return lhs && rhs ? assign(instruction.result_id, std::format("fmod({}, {})", *lhs, *rhs)) : false;
		}
		case ir::Op::MatrixTimesVector:
		case ir::Op::MatrixTimesMatrix: {
			const auto* arguments = instruction.arguments_if<ir::BinaryArguments>();
			const auto lhs = arguments ? expression(arguments->lhs) : std::nullopt;
			const auto rhs = arguments ? expression(arguments->rhs) : std::nullopt;
			return lhs && rhs ? assign(instruction.result_id, std::format("mul({}, {})", *lhs, *rhs)) : false;
		}
		case ir::Op::Dot:
		case ir::Op::Cross: {
			const auto* arguments = instruction.arguments_if<ir::BinaryArguments>();
			const auto lhs = arguments ? expression(arguments->lhs) : std::nullopt;
			const auto rhs = arguments ? expression(arguments->rhs) : std::nullopt;
			return lhs && rhs ? assign(instruction.result_id, std::format("{}({}, {})", instruction.op == ir::Op::Dot ? "dot" : "cross", *lhs, *rhs)) : false;
		}
		case ir::Op::ConvertFToU:
		case ir::Op::ConvertFToS:
		case ir::Op::ConvertSToF:
		case ir::Op::ConvertUToF: {
			const auto* arguments = instruction.arguments_if<ir::UnaryArguments>();
			const auto operand = arguments ? expression(arguments->operand) : std::nullopt;
			const auto target = hlsl_type(instruction.type_id);
			return operand && target ? assign(instruction.result_id, std::format("{}({})", *target, *operand)) : false;
		}
		case ir::Op::Bitcast: {
			const auto* arguments = instruction.arguments_if<ir::UnaryArguments>();
			const auto operand = arguments ? expression(arguments->operand) : std::nullopt;
			const ir::Type* target = type(instruction.type_id);
			if (!operand || !target) {
				return false;
			}
			const std::string function_name = target->kind == ir::TypeKind::floating ? "asfloat" :
				target->kind == ir::TypeKind::signed_integer ? "asint" : "asuint";
			return assign(instruction.result_id, std::format("{}({})", function_name, *operand));
		}
		case ir::Op::ImageSampleImplicitLod: {
			const auto* arguments = instruction.arguments_if<ir::BinaryArguments>();
			const auto sampled = arguments ? expression(arguments->lhs) : std::nullopt;
			const auto coordinate = arguments ? expression(arguments->rhs) : std::nullopt;
			return sampled && coordinate ? assign(instruction.result_id, std::format("{}_texture.Sample({}_sampler, {})", *sampled, *sampled, *coordinate)) : false;
		}
		case ir::Op::ImageSampleExplicitLod: {
			const auto* arguments = instruction.arguments_if<ir::ImageSampleExplicitLodArguments>();
			const auto sampled = arguments ? expression(arguments->sampled_image) : std::nullopt;
			const auto coordinate = arguments ? expression(arguments->coordinate) : std::nullopt;
			const auto lod = arguments ? expression(arguments->lod) : std::nullopt;
			return sampled && coordinate && lod ? assign(instruction.result_id, std::format("{}_texture.SampleLevel({}_sampler, {}, {})", *sampled, *sampled, *coordinate, *lod)) : false;
		}
		case ir::Op::Branch: {
			const auto* arguments = instruction.arguments_if<ir::BranchArguments>();
			if (!arguments || !state_machine) {
				return fail(ErrorCode::unsupported_instruction, "instructions.branch", "branch requires structured block emission", instruction.result_id, instruction.op);
			}
			line(std::format("rtsl_block = {}u;", arguments->target.value));
			line("continue;");
			return true;
		}
		case ir::Op::BranchConditional: {
			const auto* arguments = instruction.arguments_if<ir::BranchConditionalArguments>();
			const auto condition = arguments ? expression(arguments->condition) : std::nullopt;
			if (!arguments || !condition || !state_machine) {
				return fail(ErrorCode::unsupported_instruction, "instructions.branch_conditional", "conditional branch has invalid arguments", instruction.result_id, instruction.op);
			}
			line(std::format("rtsl_block = {} ? {}u : {}u;", *condition, arguments->true_target.value, arguments->false_target.value));
			line("continue;");
			return true;
		}
		case ir::Op::Return:
			line("return output;");
			return true;
		case ir::Op::ReturnValue: {
			const auto* arguments = instruction.arguments_if<ir::ReturnValueArguments>();
			return arguments && emit_output(arguments->value);
		}
		default:
			break;
		}

		std::string_view operation;
		switch (instruction.op) {
		case ir::Op::FAdd:
		case ir::Op::IAdd: operation = "+"; break;
		case ir::Op::FSub:
		case ir::Op::ISub: operation = "-"; break;
		case ir::Op::FMul:
		case ir::Op::IMul:
		case ir::Op::VectorTimesScalar:
		case ir::Op::MatrixTimesScalar: operation = "*"; break;
		case ir::Op::FDiv:
		case ir::Op::SDiv:
		case ir::Op::UDiv: operation = "/"; break;
		case ir::Op::SMod:
		case ir::Op::UMod: operation = "%"; break;
		case ir::Op::FOrdEqual:
		case ir::Op::IEqual: operation = "=="; break;
		case ir::Op::FOrdNotEqual:
		case ir::Op::INotEqual: operation = "!="; break;
		case ir::Op::FOrdLess:
		case ir::Op::SLess:
		case ir::Op::ULess: operation = "<"; break;
		case ir::Op::FOrdLessEqual:
		case ir::Op::SLessEqual:
		case ir::Op::ULessEqual: operation = "<="; break;
		case ir::Op::FOrdGreater:
		case ir::Op::SGreater:
		case ir::Op::UGreater: operation = ">"; break;
		case ir::Op::FOrdGreaterEqual:
		case ir::Op::SGreaterEqual:
		case ir::Op::UGreaterEqual: operation = ">="; break;
		case ir::Op::LogicalAnd: operation = "&&"; break;
		case ir::Op::LogicalOr: operation = "||"; break;
		default:
			return fail(ErrorCode::unsupported_instruction, "instructions", "RTIR operation is not supported by the HLSL backend", instruction.result_id, instruction.op);
		}
		const auto value = binary_expression(instruction, operation);
		return value && assign(instruction.result_id, std::move(*value));
	}

	bool emit_entry() {
		const std::string parameters = entry->input ? "RtslInput input" : "";
		line(std::format("RtslOutput main({}) {{", parameters));
		++indent;
		line("RtslOutput output = (RtslOutput)0;");
		if (!declare_values() || !initialize_input()) {
			return false;
		}
		const bool state_machine = function->blocks.size() > 1;
		if (state_machine) {
			line(std::format("uint rtsl_block = {}u;", function->blocks.front().id.value));
			line("while (true) {");
			++indent;
			line("switch (rtsl_block) {");
			++indent;
		}
		for (const auto& block : function->blocks) {
			if (state_machine) {
				line(std::format("case {}u: {{", block.id.value));
				++indent;
			}
			for (const auto& instruction : block.instructions) {
				if (!emit_instruction(instruction, state_machine)) {
					return false;
				}
			}
			if (state_machine) {
				line("break;");
				--indent;
				line("}");
			}
		}
		if (state_machine) {
			line("default: return output;");
			--indent;
			line("}");
			--indent;
			line("}");
		}
		--indent;
		line("}");
		return true;
	}

	const Program& program;
	Stage stage;
	const EntryPoint* entry = nullptr;
	const ir::Function* function = nullptr;
	std::optional<Error> error;
	std::string source;
	int indent = 0;

	std::vector<const Resource*> resources;
	std::unordered_map<ir::Id, const Resource*> resource_by_variable;
	std::unordered_map<ir::Id, ir::Id> value_types;
	std::unordered_map<ir::Id, std::string> expressions;
	std::unordered_map<ir::Id, std::string> pointers;
};

} // namespace

std::expected<Shader, Error> transpile(const Program& program, Stage stage) {
	try {
		return Emitter{ program, stage }.run();
	} catch (const std::bad_alloc&) {
		return std::unexpected(Error{
			.code = ErrorCode::allocation_failure,
			.stage = stage,
			.context = "allocation",
			.message = "HLSL transpilation ran out of memory",
		});
	}
}

} // namespace rtsl::hlsl
