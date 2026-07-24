#include "rtsl/glsl.hpp"

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

namespace rtsl::glsl {

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

std::string function_name(ir::Id id) {
	return std::format("rtsl_function_{}", id.value);
}

std::string field_name(std::uint32_t index) {
	return std::format("m{}", index);
}

class Emitter {
  public:
	Emitter(const Program& program, Stage stage, Options options) : program(program), stage(stage), options(options) {}

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
			return std::unexpected(make_error(ErrorCode::invalid_entry, "entry.parameters", "GLSL stage entries support zero or one payload parameter", function->id));
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

		if (!emit_types() || !emit_resources() || !emit_interfaces() || !emit_reachable_functions() || !emit_entry()) {
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
		for (const auto& candidate : program.functions()) {
			for (const auto& parameter : candidate.parameters) {
				value_types.emplace(parameter.id, parameter.type);
			}
			for (const auto& block : candidate.blocks) {
				for (const auto& instruction : block.instructions) {
					if (instruction.result_id) {
						value_types.emplace(instruction.result_id, instruction.type_id);
					}
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

	std::optional<std::string> glsl_type(ir::Id id) {
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
			const ir::Type* element_type = type(value->element_type);
			const auto element = glsl_type(value->element_type);
			if (element && value->element_count >= 2 && value->element_count <= 4) {
				if (element_type && element_type->kind == ir::TypeKind::floating) {
					return std::format("vec{}", value->element_count);
				}
				if (element_type && element_type->kind == ir::TypeKind::signed_integer) {
					return std::format("ivec{}", value->element_count);
				}
				if (element_type && element_type->kind == ir::TypeKind::unsigned_integer) {
					return std::format("uvec{}", value->element_count);
				}
				if (element_type && element_type->kind == ir::TypeKind::boolean) {
					return std::format("bvec{}", value->element_count);
				}
			}
			break;
		}
		case ir::TypeKind::matrix: {
			const ir::Type* column = type(value->element_type);
			if (column && column->kind == ir::TypeKind::vector && column->element_count == value->element_count) {
				const auto element = glsl_type(column->element_type);
				if (element && *element == "float" && value->element_count >= 2 && value->element_count <= 4) {
					return std::format("mat{}", value->element_count);
				}
			}
			break;
		}
		case ir::TypeKind::structure: return type_name(id);
		case ir::TypeKind::pointer: return glsl_type(value->element_type);
		case ir::TypeKind::image:
		case ir::TypeKind::sampler:
		case ir::TypeKind::sampled_image:
			return std::nullopt;
		case ir::TypeKind::array:
		case ir::TypeKind::runtime_array:
		case ir::TypeKind::function:
			break;
		}
		fail(ErrorCode::unsupported_type, "types", "type is not supported by the GLSL backend", id);
		return std::nullopt;
	}

	std::optional<std::string> zero_expression(ir::Id id) {
		const ir::Type* value = type(id);
		const auto name = glsl_type(id);
		if (!value || !name) {
			return std::nullopt;
		}
		switch (value->kind) {
		case ir::TypeKind::void_:
			return std::nullopt;
		case ir::TypeKind::boolean:
			return "false";
		case ir::TypeKind::signed_integer:
		case ir::TypeKind::unsigned_integer:
		case ir::TypeKind::floating:
			return std::format("{}(0)", *name);
		case ir::TypeKind::vector:
		case ir::TypeKind::matrix:
			return std::format("{}(0)", *name);
		case ir::TypeKind::structure: {
			std::string result = *name + "(";
			for (std::size_t index = 0; index < value->members.size(); ++index) {
				const auto member = zero_expression(value->members[index]);
				if (!member) {
					return std::nullopt;
				}
				if (index) {
					result += ", ";
				}
				result += *member;
			}
			result += ")";
			return result;
		}
		case ir::TypeKind::pointer:
			return zero_expression(value->element_type);
		case ir::TypeKind::image:
		case ir::TypeKind::sampler:
		case ir::TypeKind::sampled_image:
		case ir::TypeKind::array:
		case ir::TypeKind::runtime_array:
		case ir::TypeKind::function:
			break;
		}
		return std::nullopt;
	}

	bool emit_types() {
		line(std::format("#version {} core", options.version));
		if (options.version < 410 && options.separate_shader_objects) {
			line("#extension GL_ARB_separate_shader_objects : enable");
		}
		if (options.version < 430 && options.shader_storage_buffer) {
			line("#extension GL_ARB_shader_storage_buffer_object : enable");
		}
		line();
		line("// Generated from linked RTSL IR. Do not edit.");
		line();
		for (const auto& value : program.types()) {
			if (value.kind != ir::TypeKind::structure) {
				continue;
			}
			if (value.members.size() == 1) {
				const ir::Type* member = type(value.members.front());
				if (member && member->kind == ir::TypeKind::runtime_array) {
					continue;
				}
			}
			line(std::format("struct {} {{", type_name(value.id)));
			++indent;
			for (std::uint32_t index = 0; index < value.members.size(); ++index) {
				const auto member_type = glsl_type(value.members[index]);
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

	std::string resource_uniform_name(const Resource& resource) const {
		return identifier(resource.name);
	}

	bool use_native_storage_buffer(const Resource& resource) const {
		if (resource.kind != ResourceKind::storage_buffer) {
			return false;
		}
		if (options.storage_buffer_lowering == StorageBufferLowering::native_ssbo) {
			return options.shader_storage_buffer;
		}
		if (options.storage_buffer_lowering == StorageBufferLowering::unsupported ||
			options.storage_buffer_lowering == StorageBufferLowering::texture_buffer_readonly_vec4) {
			return false;
		}
		return options.shader_storage_buffer;
	}

	bool use_texture_buffer_storage_emulation(const Resource& resource) const {
		if (resource.kind != ResourceKind::storage_buffer || resource.access != Access::read_only || !options.texture_buffer) {
			return false;
		}
		if (options.storage_buffer_lowering == StorageBufferLowering::texture_buffer_readonly_vec4) {
			return true;
		}
		if (options.storage_buffer_lowering == StorageBufferLowering::auto_) {
			return !options.shader_storage_buffer;
		}
		return false;
	}

	bool emit_resources() {
		for (const Resource* resource : resources) {
			const std::string name = resource_name(*resource);
			const auto binding = resource->descriptor.binding;
			switch (resource->kind) {
			case ResourceKind::uniform_buffer: {
				const auto value = glsl_type(resource->value_type);
				if (!value) {
					return false;
				}
				if (options.version >= 420) {
					line(std::format("layout(std140, binding = {}) uniform {} {{", binding, resource_uniform_name(*resource)));
				} else {
					line(std::format("layout(std140) uniform {} {{", resource_uniform_name(*resource)));
				}
				++indent;
				line(std::format("{} {};", *value, name));
				--indent;
				line("};");
				break;
			}
			case ResourceKind::sampled_texture:
			case ResourceKind::sampler:
				if (options.version >= 420) {
					line(std::format("layout(binding = {}) uniform sampler2D {};", binding, resource_uniform_name(*resource)));
				} else {
					line(std::format("uniform sampler2D {};", resource_uniform_name(*resource)));
				}
				break;
			case ResourceKind::storage_buffer: {
				if (use_texture_buffer_storage_emulation(*resource)) {
					const ir::Type* block = type(resource->value_type);
					const ir::Type* array = block && block->kind == ir::TypeKind::structure && block->members.size() == 1
						? type(block->members.front()) : nullptr;
					const ir::Type* element_type = array && array->kind == ir::TypeKind::runtime_array ? type(array->element_type) : nullptr;
					const auto element = array && array->kind == ir::TypeKind::runtime_array ? glsl_type(array->element_type) : std::nullopt;
					if (!element || !element_type || element_type->kind != ir::TypeKind::vector || element_type->element_count != 4) {
						return fail(ErrorCode::unsupported_resource, "resources.storage_buffer", "texture-buffer storage emulation requires a readonly vec4/uvec4 runtime array", resource->variable);
					}
					const ir::Type* scalar = type(element_type->element_type);
					if (!scalar || (scalar->kind != ir::TypeKind::floating && scalar->kind != ir::TypeKind::unsigned_integer && scalar->kind != ir::TypeKind::signed_integer)) {
						return fail(ErrorCode::unsupported_resource, "resources.storage_buffer", "texture-buffer storage emulation requires float/int/uint vector elements", resource->variable);
					}
					line(std::format("uniform {} {};", scalar->kind == ir::TypeKind::floating ? "samplerBuffer" : scalar->kind == ir::TypeKind::unsigned_integer ? "usamplerBuffer" : "isamplerBuffer", resource_uniform_name(*resource)));
					break;
				}
				if (!use_native_storage_buffer(*resource)) {
					return fail(ErrorCode::unsupported_resource, "resources.storage_buffer", "storage buffers require native SSBO support or readonly vec4/uvec4/ivec4 texture-buffer emulation", resource->variable);
				}
				const ir::Type* block = type(resource->value_type);
				const ir::Type* array = block && block->kind == ir::TypeKind::structure && block->members.size() == 1
					? type(block->members.front()) : nullptr;
				const auto element = array && array->kind == ir::TypeKind::runtime_array ? glsl_type(array->element_type) : std::nullopt;
				if (!element) {
					return fail(ErrorCode::unsupported_resource, "resources.storage_buffer", "storage buffer must contain one runtime array", resource->variable);
				}
				line(std::format("layout(std430, binding = {}) buffer {} {{", binding, resource_uniform_name(*resource)));
				++indent;
				line(std::format("{} {}[];", *element, resource_name(*resource)));
				--indent;
				line("};");
				break;
			}
			case ResourceKind::storage_image:
				return fail(ErrorCode::unsupported_resource, "resources", "storage images require image load/store support and are not lowered by the GLSL backend yet", resource->variable);
			}
			line();
		}
		return true;
	}

	bool emit_interface_struct(std::string_view name, const Interface* interface) {
		line(std::format("struct {} {{", name));
		++indent;
		if (interface) {
			for (const auto& element : interface->elements) {
				const auto value = glsl_type(element.type);
				if (!value) {
					return false;
				}
				line(std::format("{} {};", *value, identifier(element.name)));
			}
		}
		--indent;
		line("};");
		line();
		return true;
	}

	std::string interface_variable_name(const InterfaceElement& element, bool output) const {
		const std::string name = identifier(element.name);
		if (output) {
			return stage == Stage::fragment ? "rtsl_fragout_" + name : "rtsl_out_" + name;
		}
		return stage == Stage::fragment ? "rtsl_out_" + name : "rtsl_in_" + name;
	}

	bool emit_interface_globals(const Interface* interface, bool output) {
		if (!interface) {
			return true;
		}
		const char* qualifier = output ? "out" : "in";
		for (const auto& element : interface->elements) {
			if (element.builtin == Builtin::position) {
				continue;
			}
			const auto value = glsl_type(element.type);
			if (!value) {
				return false;
			}
			const std::string interpolation = element.interpolation == Interpolation::flat ? "flat " : "";
			line(std::format("layout(location = {}) {}{} {} {};",
				element.location.value_or(0),
				interpolation,
				qualifier,
				*value,
				interface_variable_name(element, output)));
		}
		if (!interface->elements.empty()) {
			line();
		}
		return true;
	}

	bool emit_interfaces() {
		const Interface* input = nullptr;
		if (entry->input) {
			input = &*entry->input;
			if (stage == Stage::fragment) {
				const EntryPoint* vertex = program.entry(Stage::vertex);
				if (vertex && vertex->output) {
					input = &*vertex->output;
				}
			}
			if (!emit_interface_struct("RtslInput", input)) {
				return false;
			}
		}
		if (!emit_interface_struct("RtslOutput", &*entry->output)) {
			return false;
		}
		return emit_interface_globals(input, false) && emit_interface_globals(&*entry->output, true);
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
				return std::format("uintBitsToFloat(0x{:08x}u)", constant.words.front());
			}
			std::string result = std::format("{:.9g}", value);
			if (result.find_first_of(".eE") == std::string::npos) {
				result += ".0";
			}
			return result;
		}
		case ir::ConstantKind::composite: {
			const auto value_type_name = glsl_type(constant.type);
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
			if (found->second->kind == ResourceKind::sampled_texture || found->second->kind == ResourceKind::sampler) {
				return resource_uniform_name(*found->second);
			}
			return resource_name(*found->second);
		}
		return std::nullopt;
	}

	bool declare_values(bool declare_parameters) {
		std::unordered_set<ir::Id> declared;
		if (declare_parameters) {
			for (const auto& parameter : function->parameters) {
				const auto value = glsl_type(parameter.type);
				if (!value) {
					return false;
				}
				line(std::format("{} {};", *value, value_name(parameter.id)));
				declared.insert(parameter.id);
			}
		} else {
			for (const auto& parameter : function->parameters) declared.insert(parameter.id);
		}
		for (const auto& block : function->blocks) {
			for (const auto& instruction : block.instructions) {
				if (!instruction.result_id || declared.contains(instruction.result_id) || instruction.op == ir::Op::AccessChain) {
					continue;
				}
				if (const ir::Type* result = type(instruction.type_id); result && result->kind == ir::TypeKind::void_) continue;
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
				const auto value = glsl_type(declaration_type);
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
			const auto& element = entry->input->elements.front();
			if (element.builtin == Builtin::position && stage == Stage::fragment) {
				line(std::format("{} = gl_FragCoord;", value_name(parameter)));
			} else {
				line(std::format("{} = {};", value_name(parameter), interface_variable_name(element, false)));
			}
			return true;
		}
		for (const auto& element : entry->input->elements) {
			if (!element.member || *element.member >= payload->members.size()) {
				return fail(ErrorCode::invalid_entry, "entry.input", "struct input has an invalid member index", payload->id);
			}
			if (element.builtin == Builtin::position && stage == Stage::fragment) {
				line(std::format("{}.{} = gl_FragCoord;", value_name(parameter), field_name(*element.member)));
			} else {
				line(std::format("{}.{} = {};", value_name(parameter), field_name(*element.member), interface_variable_name(element, false)));
			}
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
			const auto& element = output.elements.front();
			if (element.builtin == Builtin::position && stage == Stage::vertex) {
				line(std::format("gl_Position = {};", *result));
			} else {
				line(std::format("{} = {};", interface_variable_name(element, true), *result));
			}
		} else {
			for (const auto& element : output.elements) {
				if (!element.member || *element.member >= payload->members.size()) {
					return fail(ErrorCode::invalid_entry, "entry.output", "struct output has an invalid member index", payload->id);
				}
				if (element.builtin == Builtin::position && stage == Stage::vertex) {
					line(std::format("gl_Position = {}.{};", *result, field_name(*element.member)));
				} else {
					line(std::format("{} = {}.{};", interface_variable_name(element, true), *result, field_name(*element.member)));
				}
			}
		}
		line("return;");
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
		const auto resource = resource_by_variable.find(arguments->base);
		for (std::size_t position = 0; position < arguments->indices.size(); ++position) {
			const ir::Id index_id = arguments->indices[position];
			const auto index = constant_unsigned(index_id);
			if (!current) {
				return fail(ErrorCode::unsupported_instruction, "instructions.access_chain", "access chain traverses an unknown type", instruction.result_id, instruction.op);
			}
			if (current->kind == ir::TypeKind::structure) {
				if (position == 0 && resource != resource_by_variable.end() && resource->second->kind == ResourceKind::storage_buffer && index == 0 &&
					current->members.size() == 1) {
					const ir::Type* member = type(current->members.front());
					if (member && member->kind == ir::TypeKind::runtime_array) {
						current = member;
						continue;
					}
				}
				if (!index) {
					return fail(ErrorCode::unsupported_instruction, "instructions.access_chain", "struct access requires a constant index", instruction.result_id, instruction.op);
				}
				if (*index >= current->members.size()) {
					return fail(ErrorCode::unsupported_instruction, "instructions.access_chain", "struct access index is out of range", instruction.result_id, instruction.op);
				}
				*result += "." + field_name(static_cast<std::uint32_t>(*index));
				current = type(current->members[static_cast<std::size_t>(*index)]);
			} else if (current->kind == ir::TypeKind::vector || current->kind == ir::TypeKind::matrix || current->kind == ir::TypeKind::array || current->kind == ir::TypeKind::runtime_array) {
				const auto index_expression = expression(index_id);
				if (!index_expression) {
					return fail(ErrorCode::unsupported_instruction, "instructions.access_chain", "array access references an unknown index", instruction.result_id, instruction.op);
				}
				if (current->kind == ir::TypeKind::runtime_array && resource != resource_by_variable.end() && use_texture_buffer_storage_emulation(*resource->second)) {
					*result = std::format("texelFetch({}, int({}))", resource_uniform_name(*resource->second), *index_expression);
				} else {
					*result += std::format("[{}]", *index_expression);
				}
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
		const auto result_name = glsl_type(instruction.type_id);
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
		const auto result_type = glsl_type(instruction.type_id);
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
		case ir::Op::SNegate:
		case ir::Op::LogicalNot: {
			const auto* arguments = instruction.arguments_if<ir::UnaryArguments>();
			const auto operand = arguments ? expression(arguments->operand) : std::nullopt;
			if (!operand) {
				return false;
			}
			return assign(instruction.result_id, std::format("({}{})", instruction.op == ir::Op::LogicalNot ? "!" : "-", *operand));
		}
		case ir::Op::FMod: {
			const auto* arguments = instruction.arguments_if<ir::BinaryArguments>();
			const auto lhs = arguments ? expression(arguments->lhs) : std::nullopt;
			const auto rhs = arguments ? expression(arguments->rhs) : std::nullopt;
			return lhs && rhs ? assign(instruction.result_id, std::format("({0} - {1} * floor({0} / {1}))", *lhs, *rhs)) : false;
		}
		case ir::Op::BitwiseNot:
		case ir::Op::FAbs:
		case ir::Op::Floor:
		case ir::Op::Fract:
		case ir::Op::Sqrt: {
			const auto* arguments = instruction.arguments_if<ir::UnaryArguments>();
			const auto operand = arguments ? expression(arguments->operand) : std::nullopt;
			if (!operand) {
				return false;
			}
			const std::string_view function = instruction.op == ir::Op::FAbs ? "abs"
				: instruction.op == ir::Op::Floor ? "floor"
				: instruction.op == ir::Op::Fract ? "fract" : "sqrt";
			return assign(instruction.result_id, instruction.op == ir::Op::BitwiseNot
				? std::format("(~{})", *operand) : std::format("{}({})", function, *operand));
		}
		case ir::Op::FMin:
		case ir::Op::FMax: {
			const auto* arguments = instruction.arguments_if<ir::BinaryArguments>();
			const auto lhs = arguments ? expression(arguments->lhs) : std::nullopt;
			const auto rhs = arguments ? expression(arguments->rhs) : std::nullopt;
			return lhs && rhs ? assign(instruction.result_id, std::format("{}({}, {})", instruction.op == ir::Op::FMin ? "min" : "max", *lhs, *rhs)) : false;
		}
		case ir::Op::FMix:
		case ir::Op::SmoothStep: {
			const auto* arguments = instruction.arguments_if<ir::TernaryArguments>();
			const auto first = arguments ? expression(arguments->first) : std::nullopt;
			const auto second = arguments ? expression(arguments->second) : std::nullopt;
			const auto third = arguments ? expression(arguments->third) : std::nullopt;
			return first && second && third ? assign(instruction.result_id, std::format("{}({}, {}, {})", instruction.op == ir::Op::FMix ? "mix" : "smoothstep", *first, *second, *third)) : false;
		}
		case ir::Op::MatrixTimesVector:
		case ir::Op::MatrixTimesMatrix: {
			const auto* arguments = instruction.arguments_if<ir::BinaryArguments>();
			const auto lhs = arguments ? expression(arguments->lhs) : std::nullopt;
			const auto rhs = arguments ? expression(arguments->rhs) : std::nullopt;
			return lhs && rhs ? assign(instruction.result_id, std::format("({} * {})", *lhs, *rhs)) : false;
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
			const auto target = glsl_type(instruction.type_id);
			return operand && target ? assign(instruction.result_id, std::format("{}({})", *target, *operand)) : false;
		}
		case ir::Op::Bitcast: {
			const auto* arguments = instruction.arguments_if<ir::UnaryArguments>();
			const auto operand = arguments ? expression(arguments->operand) : std::nullopt;
			const ir::Type* target = type(instruction.type_id);
			if (!operand || !target) {
				return false;
			}
			const ir::Type* scalar = target->kind == ir::TypeKind::vector ? type(target->element_type) : target;
			if (!scalar) {
				return false;
			}
			const std::string function_name = scalar->kind == ir::TypeKind::floating ? "uintBitsToFloat" :
				scalar->kind == ir::TypeKind::signed_integer ? "floatBitsToInt" : "floatBitsToUint";
			return assign(instruction.result_id, std::format("{}({})", function_name, *operand));
		}
		case ir::Op::ImageQuerySize: {
			const auto* arguments = instruction.arguments_if<ir::UnaryArguments>();
			const auto image = arguments ? expression(arguments->operand) : std::nullopt;
			const ir::Type* result = type(instruction.type_id);
			const auto result_name = glsl_type(instruction.type_id);
			if (!image || !result || result->kind != ir::TypeKind::vector || result->element_count < 2 || result->element_count > 3) {
				return fail(ErrorCode::unsupported_instruction, "instructions.image_query_size", "texture size query has an invalid shape", instruction.result_id, instruction.op);
			}
			return result_name ? assign(instruction.result_id, std::format("{}(textureSize({}, 0))", *result_name, *image)) : false;
		}
		case ir::Op::ImageSampleImplicitLod: {
			const auto* arguments = instruction.arguments_if<ir::BinaryArguments>();
			const auto sampled = arguments ? expression(arguments->lhs) : std::nullopt;
			const auto coordinate = arguments ? expression(arguments->rhs) : std::nullopt;
			return sampled && coordinate ? assign(instruction.result_id, std::format("texture({}, {})", *sampled, *coordinate)) : false;
		}
		case ir::Op::ImageSampleExplicitLod: {
			const auto* arguments = instruction.arguments_if<ir::ImageSampleExplicitLodArguments>();
			const auto sampled = arguments ? expression(arguments->sampled_image) : std::nullopt;
			const auto coordinate = arguments ? expression(arguments->coordinate) : std::nullopt;
			const auto lod = arguments ? expression(arguments->lod) : std::nullopt;
			return sampled && coordinate && lod ? assign(instruction.result_id, std::format("textureLod({}, {}, {})", *sampled, *coordinate, *lod)) : false;
		}
		case ir::Op::FunctionCall: {
			const auto* arguments = instruction.arguments_if<ir::FunctionCallArguments>();
			const ir::Function* target = arguments ? program.find_function(arguments->function) : nullptr;
			if (!arguments || !target || target->parameters.size() != arguments->arguments.size()) {
				return fail(ErrorCode::unsupported_instruction, "instructions.call", "function call has invalid arguments", instruction.result_id, instruction.op);
			}
			std::string call = function_name(target->id) + "(";
			for (std::size_t index = 0; index < arguments->arguments.size(); ++index) {
				const auto argument = expression(arguments->arguments[index]);
				if (!argument) return false;
				if (index) call += ", ";
				call += *argument;
			}
			call += ")";
			const ir::Type* return_type = type(target->return_type);
			if (!return_type) return false;
			if (return_type->kind == ir::TypeKind::void_) {
				line(call + ";");
				return true;
			}
			return assign(instruction.result_id, std::move(call));
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
			line("return;");
			return true;
		case ir::Op::ReturnValue: {
			const auto* arguments = instruction.arguments_if<ir::ReturnValueArguments>();
			if (!arguments) return false;
			if (emitting_entry) return emit_output(arguments->value);
			const auto value = expression(arguments->value);
			if (!value) return false;
			line(std::format("return {};", *value));
			return true;
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
		case ir::Op::BitwiseAnd: operation = "&"; break;
		case ir::Op::BitwiseOr: operation = "|"; break;
		case ir::Op::BitwiseXor: operation = "^"; break;
		default:
			return fail(ErrorCode::unsupported_instruction, "instructions", "RTIR operation is not supported by the GLSL backend", instruction.result_id, instruction.op);
		}
		const auto value = binary_expression(instruction, operation);
		return value && assign(instruction.result_id, std::move(*value));
	}

	bool emit_reachable_functions() {
		std::vector<const ir::Function*> ordered;
		std::unordered_set<ir::Id> visited;
		const auto visit = [&](auto&& self, const ir::Function& candidate) -> bool {
			if (!visited.insert(candidate.id).second) return true;
			for (const auto& block : candidate.blocks) {
				for (const auto& instruction : block.instructions) {
					const auto* call = instruction.arguments_if<ir::FunctionCallArguments>();
					const ir::Function* target = call ? program.find_function(call->function) : nullptr;
					if (call && (!target || !self(self, *target))) return false;
				}
			}
			if (candidate.id != entry->function) ordered.push_back(&candidate);
			return true;
		};
		if (!visit(visit, *function)) return fail(ErrorCode::unsupported_instruction, "instructions.call", "function call target does not exist");

		for (const ir::Function* candidate : ordered) {
			function = candidate;
			emitting_entry = false;
			expressions.clear();
			pointers.clear();
			const auto return_type = glsl_type(function->return_type);
			if (!return_type) return false;
			std::string parameters;
			for (std::size_t index = 0; index < function->parameters.size(); ++index) {
				const auto parameter_type = glsl_type(function->parameters[index].type);
				if (!parameter_type) return false;
				if (index) parameters += ", ";
				parameters += std::format("{} {}", *parameter_type, value_name(function->parameters[index].id));
			}
			line(std::format("{} {}({}) {{", *return_type, function_name(function->id), parameters));
			++indent;
			if (!declare_values(false)) return false;
			const bool state_machine = function->blocks.size() > 1;
			if (state_machine) {
				line(std::format("uint rtsl_block = {}u;", function->blocks.front().id.value));
				line("while (true) {"); ++indent;
				line("switch (rtsl_block) {"); ++indent;
			}
			for (const auto& block : function->blocks) {
				if (state_machine) { line(std::format("case {}u: {{", block.id.value)); ++indent; }
				for (const auto& instruction : block.instructions) if (!emit_instruction(instruction, state_machine)) return false;
				if (state_machine) { line("break;"); --indent; line("}"); }
			}
			if (state_machine) {
				const ir::Type* result = type(function->return_type);
				if (result && result->kind == ir::TypeKind::void_) {
					line("default: return;");
				} else {
					const auto zero = zero_expression(function->return_type);
					if (!zero) {
						return false;
					}
					line(std::format("default: return {};", *zero));
				}
				--indent; line("}"); --indent; line("}");
			}
			--indent;
			line("}");
			line();
		}
		function = program.find_function(entry->function);
		return function != nullptr;
	}

	bool emit_entry() {
		emitting_entry = true;
		expressions.clear();
		pointers.clear();
		line("void main() {");
		++indent;
		if (!declare_values(true) || !initialize_input()) {
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
			line("default: return;");
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
	Options options;
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
	bool emitting_entry = false;
};

} // namespace

std::expected<Shader, Error> transpile(const Program& program, Stage stage) {
	return transpile(program, stage, Options{});
}

std::expected<Shader, Error> transpile(const Program& program, Stage stage, const Options& options) {
	try {
		return Emitter{ program, stage, options }.run();
	} catch (const std::bad_alloc&) {
		return std::unexpected(Error{
			.code = ErrorCode::allocation_failure,
			.stage = stage,
			.context = "allocation",
			.message = "GLSL transpilation ran out of memory",
		});
	}
}

} // namespace rtsl::glsl
