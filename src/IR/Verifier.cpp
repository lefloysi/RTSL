#include <rtsl/IR/Verifier.hpp>

#include <algorithm>
#include <format>
#include <unordered_map>
#include <unordered_set>

namespace rtsl::ir {

void VerificationResult::add(VerificationCode code, std::string context, std::string message) {
	Issues.push_back(VerificationIssue{.code = code, .context = std::move(context), .message = std::move(message)});
}

VerificationResult verify(const Module& module) {
	VerificationResult result;
	std::unordered_set<std::uint32_t> type_ids;
	std::unordered_set<std::uint32_t> symbol_ids;
	std::unordered_set<std::uint32_t> function_ids;

	for (const Type& type : module.types) {
		const std::string context = std::format("type {}", type.id.value());
		if (!type.id || !type_ids.insert(type.id.value()).second) {
			result.add(VerificationCode::verification_duplicate_id, context, "type id is zero or duplicated");
		}
		const auto require_type = [&](TypeId id, std::string_view field) {
			if (id && !module.findType(id)) {
				result.add(VerificationCode::verification_unknown_type, context, std::format("{} references an unknown type", field));
			}
		};
		require_type(type.element_type, "element type");
		for (TypeId parameter : type.parameter_types) require_type(parameter, "parameter type");
		for (const StructMember& member : type.members) require_type(member.type, "member type");
		for (const BuiltinMember& member : type.builtin_members) {
			if (type.kind != TypeKind::type_structure || member.member_path.empty() || member.member_path.front() >= type.members.size())
				result.add(VerificationCode::verification_invalid_metadata, std::format("type {}", type.id.value()),
					"builtin member path does not identify a structure member");
		}
	}

	for (const Symbol& symbol : module.symbols) {
		if (!symbol.id || !symbol_ids.insert(symbol.id.value()).second) {
			result.add(VerificationCode::verification_duplicate_id, std::format("symbol {}", symbol.id.value()), "symbol id is zero or duplicated");
		}
		if (module.strings.get(symbol.fully_qualified_name).empty()) {
			result.add(VerificationCode::verification_invalid_metadata, std::format("symbol {}", symbol.id.value()), "symbol name is empty");
		}
	}

	std::unordered_set<std::uint32_t> entry_emitters;
	for (const EntryPoint& entry : module.entry_points)
		if (entry.stage == Stage::stage_geometry) entry_emitters.insert(entry.function.value());

	for (const Function& function : module.functions) {
		const std::string function_context = std::format("function {}", function.id.value());
		if (!function.id || !function_ids.insert(function.id.value()).second) {
			result.add(VerificationCode::verification_duplicate_id, function_context, "function id is zero or duplicated");
		}
		if (!module.findSymbol(function.symbol)) result.add(VerificationCode::verification_unknown_symbol, function_context, "function symbol does not exist");
		if (!module.findType(function.return_type)) result.add(VerificationCode::verification_unknown_type, function_context, "function return type does not exist");

		std::unordered_map<std::uint32_t, TypeId> values;
		std::unordered_set<std::uint32_t> blocks;
		for (const Parameter& parameter : function.parameters) {
			if (!parameter.value || !values.emplace(parameter.value.value(), parameter.type).second) result.add(VerificationCode::verification_duplicate_id, function_context, "parameter value id is zero or duplicated");
			if (!module.findType(parameter.type)) result.add(VerificationCode::verification_unknown_type, function_context, "parameter type does not exist");
			if (parameter.symbol && !module.findSymbol(parameter.symbol)) result.add(VerificationCode::verification_unknown_symbol, function_context, "parameter symbol does not exist");
		}
		for (const Block& block : function.blocks) {
			if (!block.id || !blocks.insert(block.id.value()).second) result.add(VerificationCode::verification_duplicate_id, function_context, "block id is zero or duplicated");
			for (const BlockArgument& argument : block.arguments) {
				if (!argument.value || !values.emplace(argument.value.value(), argument.type).second) result.add(VerificationCode::verification_duplicate_id, function_context, "block argument value id is zero or duplicated");
				if (!module.findType(argument.type)) result.add(VerificationCode::verification_unknown_type, function_context, "block argument type does not exist");
			}
			for (const Instruction& instruction : block.instructions) {
				if (instruction.result) {
					if (!instruction.type || !module.findType(instruction.type)) result.add(VerificationCode::verification_unknown_type, function_context, "result instruction has no valid result type");
					if (!values.emplace(instruction.result.value(), instruction.type).second) result.add(VerificationCode::verification_duplicate_id, function_context, "instruction result value id is duplicated");
				} else if (instruction.type) {
					result.add(VerificationCode::verification_invalid_instruction, function_context, "resultless instruction has a result type");
				}
				for (ValueId operand : instruction.operands) {
					if (!values.contains(operand.value())) result.add(VerificationCode::verification_unknown_value, function_context, "instruction references an undefined or forward value");
				}
				if (instruction.opcode == Opcode::opcode_construct) {
					const Type* constructed_type = module.findType(instruction.type);
					bool operands_exist = std::ranges::all_of(instruction.operands,
						[&](ValueId operand) { return values.contains(operand.value()); });
					bool valid = constructed_type && operands_exist;
					if (valid && constructed_type->kind == TypeKind::type_structure) {
						valid = instruction.operands.size() == constructed_type->members.size();
						for (std::size_t index = 0; valid && index < instruction.operands.size(); ++index)
							valid = values.at(instruction.operands[index].value()) == constructed_type->members[index].type;
					} else if (valid && constructed_type->kind == TypeKind::type_vector) {
						std::uint32_t components{};
						for (ValueId operand : instruction.operands) {
							const Type* operand_type = module.findType(values.at(operand.value()));
							if (!operand_type) {
								valid = false;
								break;
							}
							if (operand_type->id == constructed_type->element_type) {
								++components;
							} else if (operand_type->kind == TypeKind::type_vector &&
								operand_type->element_type == constructed_type->element_type) {
								components += operand_type->element_count;
							} else {
								valid = false;
								break;
							}
						}
						valid = valid && components == constructed_type->element_count;
					} else if (valid && constructed_type->kind == TypeKind::type_primitive) {
						valid = instruction.operands.size() <= constructed_type->element_count;
						for (ValueId operand : instruction.operands)
							valid = valid && values.at(operand.value()) == constructed_type->element_type;
					} else if (valid) {
						valid = false;
					}
					if (!valid) result.add(VerificationCode::verification_type_mismatch, function_context,
						"construct operands do not match the constructed type");
				}
				if (instruction.opcode == Opcode::opcode_call) {
					const Function* callee = module.findFunction(instruction.callee);
					if (!callee) result.add(VerificationCode::verification_unknown_function, function_context, "call target does not exist");
					else if (callee->implicit_emitter && !function.implicit_emitter && !entry_emitters.contains(function.id.value()))
						result.add(VerificationCode::verification_invalid_instruction, function_context, "call requires an implicit emitter");
				} else if (instruction.callee) {
					result.add(VerificationCode::verification_invalid_instruction, function_context, "non-call instruction has a call target");
				}
				if (instruction.opcode == Opcode::opcode_insert) {
					const Type* object_type = module.findType(instruction.type);
					const bool valid = instruction.result && object_type && object_type->kind == TypeKind::type_primitive &&
						instruction.operands.size() == 2 && values.at(instruction.operands[0].value()) == instruction.type &&
						values.at(instruction.operands[1].value()) == object_type->element_type;
					if (!valid) result.add(VerificationCode::verification_type_mismatch, function_context,
						"insert operands do not match the aggregate object");
				}
			}
		}

		if (function.declaration) {
			if (!function.blocks.empty()) result.add(VerificationCode::verification_invalid_metadata, function_context, "function declaration has blocks");
			continue;
		}
		if (function.blocks.empty()) result.add(VerificationCode::verification_invalid_metadata, function_context, "function definition has no blocks");
		for (const Block& block : function.blocks) {
			const std::string block_context = std::format("function {} block {}", function.id.value(), block.id.value());
			if (!block.terminator) {
				result.add(VerificationCode::verification_missing_terminator, block_context, "block has no terminator");
				continue;
			}
			const Terminator& terminator = *block.terminator;
			for (ValueId operand : terminator.operands) if (!values.contains(operand.value())) result.add(VerificationCode::verification_unknown_value, block_context, "terminator references an unknown value");
			for (const Successor& successor : terminator.successors) {
				const Block* target = module.findBlock(function, successor.block);
				if (!target) {
					result.add(VerificationCode::verification_unknown_block, block_context, "successor block does not exist");
					continue;
				}
				if (successor.arguments.size() != target->arguments.size()) {
					result.add(VerificationCode::verification_invalid_successor_arguments, block_context, "successor argument count does not match target block arguments");
					continue;
				}
				for (std::size_t index = 0; index < successor.arguments.size(); ++index) {
					const auto value = values.find(successor.arguments[index].value());
					if (value == values.end()) result.add(VerificationCode::verification_unknown_value, block_context, "successor references an unknown value");
					else if (value->second != target->arguments[index].type) result.add(VerificationCode::verification_type_mismatch, block_context, "successor argument type does not match target block argument");
				}
			}
			const bool branches = terminator.kind == TerminatorKind::terminator_branch || terminator.kind == TerminatorKind::terminator_conditional_branch || terminator.kind == TerminatorKind::terminator_switch;
			if ((branches && terminator.successors.empty()) || (!branches && !terminator.successors.empty())) result.add(VerificationCode::verification_invalid_terminator, block_context, "terminator successor shape is invalid");
			if (terminator.kind == TerminatorKind::terminator_conditional_branch && (terminator.operands.size() != 1 || terminator.successors.size() != 2)) result.add(VerificationCode::verification_invalid_terminator, block_context, "conditional branch requires one condition and two successors");
			if (terminator.kind == TerminatorKind::terminator_return_value && terminator.operands.size() != 1) result.add(VerificationCode::verification_invalid_terminator, block_context, "value return requires one operand");
			if (block.merge.kind != MergeKind::merge_none) {
				if (!module.findBlock(function, block.merge.merge_block)) result.add(VerificationCode::verification_invalid_structured_merge, block_context, "merge block does not exist");
				if (block.merge.kind == MergeKind::merge_loop && !module.findBlock(function, block.merge.continue_block)) result.add(VerificationCode::verification_invalid_structured_merge, block_context, "loop continue block does not exist");
			}
		}
	}

	for (const EntryPoint& entry : module.entry_points) {
		const std::string context = std::format("entry {}", entry.symbol.value());
		if (!module.findSymbol(entry.symbol)) result.add(VerificationCode::verification_unknown_symbol, context, "entry symbol does not exist");
		if (!module.findFunction(entry.function)) result.add(VerificationCode::verification_unknown_function, context, "entry function does not exist");
		const std::size_t configuration = entry.configuration.index();
		const bool valid_configuration = (entry.stage == Stage::stage_tessellation_control && configuration == 1) || (entry.stage == Stage::stage_tessellation_evaluation && configuration == 2) || (entry.stage == Stage::stage_geometry && configuration == 3) || (entry.stage == Stage::stage_compute && configuration == 4) || ((entry.stage == Stage::stage_vertex || entry.stage == Stage::stage_fragment) && configuration == 0);
		if (!valid_configuration) result.add(VerificationCode::verification_invalid_stage_configuration, context, "stage configuration does not match the entry stage");
		if (const auto* value = std::get_if<TessellationControlConfiguration>(&entry.configuration); value && value->output_control_points == 0) result.add(VerificationCode::verification_invalid_stage_configuration, context, "tessellation control output size is zero");
		if (const auto* value = std::get_if<GeometryConfiguration>(&entry.configuration); value && (value->maximum_vertices == 0 || value->invocations == 0)) result.add(VerificationCode::verification_invalid_stage_configuration, context, "geometry output limit or invocation count is zero");
		if (const auto* value = std::get_if<ComputeConfiguration>(&entry.configuration); value && (value->workgroup_size[0] == 0 || value->workgroup_size[1] == 0 || value->workgroup_size[2] == 0)) result.add(VerificationCode::verification_invalid_stage_configuration, context, "compute workgroup dimension is zero");
	}

	const auto verify_metadata = [&](SymbolId symbol, TypeId type, std::string_view context) {
		if (!module.findSymbol(symbol)) result.add(VerificationCode::verification_unknown_symbol, std::string(context), "metadata symbol does not exist");
		if (!module.findType(type)) result.add(VerificationCode::verification_unknown_type, std::string(context), "metadata type does not exist");
	};
	for (const Resource& resource : module.resources) {
		verify_metadata(resource.symbol, resource.type, "resource");
		for (FunctionId user : resource.users) if (!module.findFunction(user)) result.add(VerificationCode::verification_unknown_function, "resource", "resource user does not exist");
	}
	for (const Uniform& uniform : module.uniforms) verify_metadata(uniform.symbol, uniform.type, "uniform");
	for (const StorageObject& storage : module.storage_objects) verify_metadata(storage.symbol, storage.type, "storage object");

	return result;
}

} // namespace rtsl::ir
