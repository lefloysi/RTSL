#include <rtsl/Serialization/Artifact.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace {
// A deliberately small test oracle for pure SSA functions. Production software
// execution currently only supports one block; this also follows CFG edges and
// transfers block arguments simultaneously to exercise generated loop phis.
struct Value {
	rtsl::ir::TypeId type;
	std::vector<double> components;
};

class Evaluator {
public:
	explicit Evaluator(const rtsl::ir::Module& module) : module(module) {}

	Value execute(rtsl::ir::FunctionId id, const std::vector<Value>& arguments, unsigned depth = 0) {
		using namespace rtsl::ir;
		const auto* function = module.findFunction(id);
		if (!function || function->declaration || function->blocks.empty() ||
			arguments.size() != function->parameters.size() || depth > 64) fail("invalid call");
		std::unordered_map<std::uint32_t, Value> values;
		for (std::size_t i = 0; i < arguments.size(); ++i)
			values.emplace(function->parameters[i].value.value(), arguments[i]);
		const auto value = [&](ValueId id) -> const Value& {
			const auto found = values.find(id.value());
			if (found == values.end()) fail("use of unavailable SSA value");
			return found->second;
		};
		const Block* block = &function->blocks.front();
		for (unsigned steps = 0; steps < 100000; ++steps) {
			for (const auto& instruction : block->instructions) {
				Value result{instruction.type, {}};
				const auto operand = [&](std::size_t i) -> const Value& { return value(instruction.operands.at(i)); };
				const auto scalar = [&](std::size_t i) { return operand(i).components.at(0); };
				const auto operation = instruction.opcode;
				if (operation == Opcode::opcode_constant_boolean) result.components = {double(instruction.immediates.at(0))};
				else if (operation == Opcode::opcode_constant_integer) {
					const auto raw = std::uint64_t(instruction.immediates.at(0)) | std::uint64_t(instruction.immediates.at(1)) << 32;
					const auto* type = module.findType(instruction.type);
					result.components = {type && type->kind == TypeKind::type_signed_integer
						? double(static_cast<std::int32_t>(raw)) : double(raw)};
				} else if (operation == Opcode::opcode_constant_floating)
					result.components = {std::bit_cast<float>(instruction.immediates.at(0))};
				else if (operation == Opcode::opcode_construct || operation == Opcode::opcode_constant_composite) {
					for (auto id : instruction.operands) {
						const auto& input = value(id).components;
						result.components.insert(result.components.end(), input.begin(), input.end());
					}
				} else if (operation == Opcode::opcode_access) {
					const auto& input = operand(0);
					const auto* type = module.findType(input.type);
					if (!type || instruction.immediates.size() != 1) fail("unsupported access");
					const auto name = module.strings.get(StringId{instruction.immediates.front()});
					std::size_t offset = 0;
					bool found = false;
					for (const auto& member : type->members) {
						const auto width = components(member.type);
						if (module.strings.get(member.name) == name) {
							result.components.assign(input.components.begin() + offset, input.components.begin() + offset + width);
							found = true;
							break;
						}
						offset += width;
					}
					if (!found) fail("unknown aggregate member");
				} else if (operation == Opcode::opcode_call) {
					std::vector<Value> parameters;
					for (auto id : instruction.operands) parameters.push_back(value(id));
					result = execute(instruction.callee, parameters, depth + 1);
					result.type = instruction.type;
				} else if (operation == Opcode::opcode_sqrt) result.components = {std::sqrt(scalar(0))};
				else if (operation == Opcode::opcode_clamp) result.components = {std::clamp(scalar(0), scalar(1), scalar(2))};
				else if (operation == Opcode::opcode_convert) result.components = operand(0).components;
				else if (operation == Opcode::opcode_negate || operation == Opcode::opcode_logical_not) {
					for (double component : operand(0).components)
						result.components.push_back(operation == Opcode::opcode_negate ? -component : double(!component));
				} else {
					const auto& a = operand(0).components;
					const auto& b = operand(1).components;
					if (a.size() != b.size()) fail("incompatible operand shapes");
					for (std::size_t i = 0; i < a.size(); ++i) {
						double output;
						switch (operation) {
						case Opcode::opcode_add: output = a[i] + b[i]; break;
						case Opcode::opcode_subtract: output = a[i] - b[i]; break;
						case Opcode::opcode_multiply: output = a[i] * b[i]; break;
						case Opcode::opcode_divide: if (!b[i]) fail("division by zero"); output = a[i] / b[i]; break;
						case Opcode::opcode_remainder: if (!b[i]) fail("remainder by zero"); output = std::fmod(a[i], b[i]); break;
						case Opcode::opcode_compare_equal: output = a[i] == b[i]; break;
						case Opcode::opcode_compare_not_equal: output = a[i] != b[i]; break;
						case Opcode::opcode_compare_less: output = a[i] < b[i]; break;
						case Opcode::opcode_compare_less_equal: output = a[i] <= b[i]; break;
						case Opcode::opcode_compare_greater: output = a[i] > b[i]; break;
						case Opcode::opcode_compare_greater_equal: output = a[i] >= b[i]; break;
						case Opcode::opcode_logical_and: output = a[i] && b[i]; break;
						case Opcode::opcode_logical_or: output = a[i] || b[i]; break;
						default: fail("unsupported instruction in test evaluator");
						}
						result.components.push_back(output);
					}
				}
				if (instruction.result) {
					normalize(result);
					values[instruction.result.value()] = std::move(result);
				}
			}
			if (!block->terminator) fail("missing terminator");
			const auto& terminator = *block->terminator;
			if (terminator.kind == TerminatorKind::terminator_return_value) return value(terminator.operands.at(0));
			if (terminator.kind == TerminatorKind::terminator_return) return {};
			std::size_t selected = 0;
			if (terminator.kind == TerminatorKind::terminator_conditional_branch)
				selected = value(terminator.operands.at(0)).components.at(0) ? 0 : 1;
			else if (terminator.kind != TerminatorKind::terminator_branch) fail("unsupported terminator");
			const auto& successor = terminator.successors.at(selected);
			const Block* destination = module.findBlock(*function, successor.block);
			if (!destination || destination->arguments.size() != successor.arguments.size()) fail("invalid edge");
			std::vector<Value> transferred;
			for (auto id : successor.arguments) transferred.push_back(value(id));
			for (std::size_t i = 0; i < transferred.size(); ++i)
				values[destination->arguments[i].value.value()] = std::move(transferred[i]);
			block = destination;
		}
		fail("execution step limit reached");
	}

private:
	[[noreturn]] static void fail(const char* message) { throw std::runtime_error(message); }
	std::size_t components(rtsl::ir::TypeId id) const {
		using namespace rtsl::ir;
		const auto* type = module.findType(id);
		if (!type) fail("unknown type");
		if (type->kind == TypeKind::type_vector || type->kind == TypeKind::type_array)
			return type->element_count * components(type->element_type);
		if (type->kind != TypeKind::type_structure) return 1;
		std::size_t result = 0;
		for (const auto& member : type->members) result += components(member.type);
		return result;
	}
	void normalize(Value& value) const {
		using namespace rtsl::ir;
		const auto* type = module.findType(value.type);
		if (!type) fail("unknown result type");
		for (double& component : value.components) {
			if (type->kind == TypeKind::type_signed_integer) component = static_cast<std::int32_t>(component);
			if (type->kind == TypeKind::type_unsigned_integer) component = static_cast<std::uint32_t>(component);
			if (type->kind == TypeKind::type_floating) component = static_cast<float>(component);
		}
	}
	const rtsl::ir::Module& module;
};
}

extern "C" int rtsl_test_execute_scalar(const std::uint8_t* data, std::uint64_t size, const char* name, double argument, double expected) {
	try {
		const auto decoded = rtsl::ArtifactReader{}.read(std::as_bytes(std::span{data, static_cast<std::size_t>(size)}));
		if (!decoded) throw std::runtime_error(decoded.error->message);
		const auto& module = decoded.artifact->module;
		const std::string prefix = std::string(name) + "(";
		for (const auto& function : module.functions) {
			const auto* symbol = module.findSymbol(function.symbol);
			if (!symbol || !module.strings.get(symbol->fully_qualified_name).starts_with(prefix)) continue;
			std::vector<Value> arguments;
			if (function.parameters.size() == 1) arguments.push_back({function.parameters[0].type, {argument}});
			else if (!function.parameters.empty()) throw std::runtime_error("test entry accepts at most one scalar");
			const auto actual = Evaluator(module).execute(function.id, arguments).components.at(0);
			if (std::isfinite(actual) && std::abs(actual - expected) < 0.00001) return 0;
			std::cerr << name << "(" << argument << "): expected " << expected << ", got " << actual << '\n';
			return 1;
		}
		throw std::runtime_error("test entry function missing");
	} catch (const std::exception& error) {
		std::cerr << name << ": " << error.what() << '\n';
		return 2;
	}
}
