#include "artifact/linker.hpp"

#include <algorithm>
#include <cstddef>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace rtsl {

[[nodiscard]] ID<IRInstruction> shifted_id(ID<IRInstruction> id, u32 offset) {
	return ID<IRInstruction>{ raw_id(id) + offset };
}

// Apply a flat id offset to every ID<IRInstruction> reference in an instruction so a
// freshly-loaded module's id space stops overlapping with the merged module's.
void shift_instruction(IRInstruction& inst, u32 offset) {
	if (inst.result_id != ID<IRInstruction>{})
		inst.result_id = shifted_id(inst.result_id, offset);
	if (inst.type_id != ID<IRInstruction>{})
		inst.type_id = shifted_id(inst.type_id, offset);
	for (auto& operand : inst.operands) {
		if (operand != ID<IRInstruction>{})
			operand = shifted_id(operand, offset);
	}
}

void shift_interface(Interface& interface, u32 offset) {
	interface.value_type = shifted_id(interface.value_type, offset);
	if (interface.value) *interface.value = shifted_id(*interface.value, offset);
	for (auto& element : interface.elements) {
		element.type = shifted_id(element.type, offset);
	}
}

void shift_entry(EntryPoint& entry, u32 offset) {
	entry.function = shifted_id(entry.function, offset);
	if (entry.input) shift_interface(*entry.input, offset);
	if (entry.output) shift_interface(*entry.output, offset);
}

// Merge `src` into `dst`. After this, `dst.next_id` is the new high-water
// mark and every id originally living in `src` has been shifted by
// (dst.next_id - 1). The shift relies on the convention that id 0 is the
// reserved "no id" sentinel.
void merge_module(IRModule& dst, IRModule src) {
	const u32 offset = raw_id(dst.next_id) - 1;
	u32 descriptor_set_offset = 0;
	if (!dst.resources.empty() && !src.resources.empty()) {
		for (const auto& resource : dst.resources) {
			descriptor_set_offset = std::max(descriptor_set_offset, resource.descriptor.set + 1);
		}
		for (auto& resource : src.resources) resource.descriptor.set += descriptor_set_offset;
		for (auto& decoration : src.decorations) {
			if (decoration.kind == IRDecorationKind::DescriptorSet && !decoration.literals.empty()) {
				decoration.literals.front() += descriptor_set_offset;
			}
		}
	}
	dst.next_id = shifted_id(dst.next_id, raw_id(src.next_id) - 1);
	dst.imports.insert(dst.imports.end(), src.imports.begin(), src.imports.end());
	dst.imported_exports.insert(dst.imported_exports.end(), src.imported_exports.begin(), src.imported_exports.end());
	dst.exports.insert(dst.exports.end(), src.exports.begin(), src.exports.end());

	for (auto inst : src.type_constant_pool) {
		shift_instruction(inst, offset);
		dst.type_constant_pool.push_back(std::move(inst));
	}
	for (auto inst : src.global_variables) {
		shift_instruction(inst, offset);
		dst.global_variables.push_back(std::move(inst));
	}
	for (auto decoration : src.decorations) {
		if (decoration.target != ID<IRInstruction>{})
			decoration.target = shifted_id(decoration.target, offset);
		dst.decorations.push_back(std::move(decoration));
	}

	// FunctionCall.literals[0] is an index into call_targets; shift it
	// by the existing target count so cross-module call resolution still
	// works after the merge.
	const u32 name_offset = static_cast<u32>(dst.call_targets.size());
	for (auto& target : src.call_targets) {
		dst.call_targets.push_back(std::move(target));
	}

	for (auto fn : src.functions) {
		if (fn.result_id != ID<IRInstruction>{})
			fn.result_id = shifted_id(fn.result_id, offset);
		if (fn.return_type_id != ID<IRInstruction>{})
			fn.return_type_id = shifted_id(fn.return_type_id, offset);
		for (auto& pid : fn.parameter_ids) {
			if (pid != ID<IRInstruction>{})
				pid = shifted_id(pid, offset);
		}
		for (auto& inst : fn.body) {
			shift_instruction(inst, offset);
			if (inst.op == IROp::FunctionCall && !inst.literals.empty()) {
				inst.literals[0] += name_offset;
			}
		}
		dst.functions.push_back(std::move(fn));
	}

	for (auto& decl : src.structs) {
		dst.structs.push_back(std::move(decl));
	}
	for (auto resource : src.resources) {
		resource.variable = shifted_id(resource.variable, offset);
		resource.value_type = shifted_id(resource.value_type, offset);
		dst.resources.push_back(std::move(resource));
	}
	for (auto entry : src.entries) {
		shift_entry(entry, offset);
		dst.entries.push_back(std::move(entry));
	}
}

bool resolve_function_calls(IRModule& ir) {
	bool resolved_all = true;
	for (auto& fn : ir.functions) {
		for (auto& inst : fn.body) {
			if (inst.op != IROp::FunctionCall || (!inst.operands.empty() && inst.operands.front() != ID<IRInstruction>{} && inst.literals.empty())) {
				continue;
			}
			if (inst.operands.empty() || inst.literals.empty() || inst.literals.front() >= ir.call_targets.size()) {
				resolved_all = false;
				continue;
			}

			const IRCallTarget& callee = ir.call_targets[inst.literals.front()];
			const std::size_t argument_count = inst.operands.size() - 1;
			const auto target = std::ranges::find_if(ir.functions, [&](const IRFunction& candidate) {
				return candidate.link_name == callee.mangled_name && candidate.parameter_ids.size() == argument_count && !candidate.body.empty();
			});
			if (target == ir.functions.end()) {
				resolved_all = false;
				continue;
			}

			inst.operands.front() = target->result_id;
			inst.literals.clear();
		}
	}
	return resolved_all;
}

bool report_unresolved_program_calls(const IRModule& ir, DiagnosticEngine& diagnostics) {
	bool found = false;
	for (const auto& fn : ir.functions) {
		for (const auto& inst : fn.body) {
			if (inst.op != IROp::FunctionCall || (!inst.operands.empty() && inst.operands.front() != ID<IRInstruction>{} && inst.literals.empty())) {
				continue;
			}
			std::string name = "<unknown>";
			if (!inst.literals.empty() && inst.literals[0] < ir.call_targets.size()) {
				name = ir.call_targets[inst.literals[0]].display_name;
			}
			diagnostics.report(DiagnosticCode::link_unresolved_call, DiagnosticSeverity::error, {}, "<link>",
							   "unresolved function call '" + name + "' in program link");
			found = true;
		}
	}
	return found;
}

std::string exported_function_identity(const IRFunction& fn) {
	if (!fn.is_exported()) {
		return {};
	}
	return fn.link_name;
}

bool report_duplicate_exported_functions(const Artifact& artifact, DiagnosticEngine& diagnostics) {
	std::unordered_set<std::string> identities;
	bool found = false;
	for (const auto& fn : artifact.module.functions) {
		std::string identity = exported_function_identity(fn);
		if (identity.empty()) {
			continue;
		}
		if (!identities.insert(identity).second) {
			diagnostics.report(DiagnosticCode::link_conflict, DiagnosticSeverity::error, {}, "<link>",
							   "duplicate exported function identity '" + identity + "'");
			found = true;
		}
	}
	return found;
}

bool report_stale_imported_exports(const Artifact& artifact, DiagnosticEngine& diagnostics) {
	std::unordered_map<std::string, u64> export_hashes;
	for (const auto& exported : artifact.module.exports) {
		if (exported.interface_hash == 0) {
			continue;
		}
		const std::string key = exported.kind + ":" + exported.name;
		export_hashes.emplace(key, exported.interface_hash);
	}

	bool found = false;
	for (const auto& imported : artifact.module.imported_exports) {
		if (imported.interface_hash == 0) {
			continue;
		}
		const std::string key = imported.kind + ":" + imported.name;
		const auto it = export_hashes.find(key);
		if (it == export_hashes.end() || it->second == imported.interface_hash) {
			continue;
		}
		diagnostics.report(DiagnosticCode::link_conflict, DiagnosticSeverity::error, {}, "<link>",
						   "stale imported interface for '" + imported.name + "'");
		found = true;
	}
	return found;
}

bool is_link_input_kind(ArtifactKind kind) {
	return kind == ArtifactKind::object || kind == ArtifactKind::library;
}

std::string_view artifact_kind_name(ArtifactKind kind) {
	switch (kind) {
	case ArtifactKind::object: return "object";
	case ArtifactKind::module: return "module interface";
	case ArtifactKind::library: return "library";
	case ArtifactKind::program: return "program";
	}
	return "unknown";
}

void serialize_program(Artifact& program, DiagnosticEngine& diagnostics) {
	program.bytes = write_artifact(ArtifactKind::program, program.module);
	const auto bytes = std::span<const std::byte>{
		reinterpret_cast<const std::byte*>(program.bytes.data()),
		program.bytes.size(),
	};
	if (auto loaded = load_program(bytes); !loaded) {
		diagnostics.report(DiagnosticCode::link_conflict, DiagnosticSeverity::error, {}, "<link>",
			"linked program violates the SDK contract at " + loaded.error().context + ": " + loaded.error().message);
		program.bytes.clear();
	}
}

Linker::Linker(DiagnosticEngine& diagnostics) : diagnostics_(diagnostics) {}

bool Linker::add_artifact_bytes(std::span<const u08> bytes) {
	Artifact artifact;
	if (!read_artifact(bytes, artifact, &diagnostics_)) {
		return false;
	}
	return add_artifact(std::move(artifact));
}

bool Linker::add_artifact(Artifact artifact) {
	if (artifact.bytes.empty()) {
		diagnostics_.report(DiagnosticCode::link_empty_artifact, DiagnosticSeverity::error, {}, "<link>", "cannot link an empty artifact");
		return false;
	}
	if (!is_link_input_kind(artifact.kind)) {
		diagnostics_.report(DiagnosticCode::link_invalid_artifact_kind, DiagnosticSeverity::error, {}, "<link>",
							"link input must be an object or library artifact, not " + std::string(artifact_kind_name(artifact.kind)));
		return false;
	}
	inputs_.push_back(std::move(artifact));
	return true;
}

Artifact Linker::link_program() {
	Artifact program{ .kind = ArtifactKind::program };
	if (inputs_.empty()) {
		diagnostics_.report(DiagnosticCode::link_no_inputs, DiagnosticSeverity::error, {}, "<link>", "no input artifacts provided");
		return program;
	}

	if (inputs_.size() == 1) {
		program = inputs_.front();
		program.kind = ArtifactKind::program;
		program.bytes.clear();
		const auto error_count = diagnostics_.diagnostics().size();
		report_duplicate_exported_functions(program, diagnostics_);
		report_stale_imported_exports(program, diagnostics_);
		resolve_function_calls(program.module);
		report_unresolved_program_calls(program.module, diagnostics_);
		validate_program_stages(program);
		if (diagnostics_.diagnostics().size() != error_count) {
			return program;
		}
		serialize_program(program, diagnostics_);
		return program;
	}

	program = inputs_.front();
	program.kind = ArtifactKind::program;
	for (std::size_t i = 1; i < inputs_.size(); ++i) {
		merge_module(program.module, std::move(inputs_[i].module));
	}
	program.bytes.clear();
	const auto error_count = diagnostics_.diagnostics().size();
	report_duplicate_exported_functions(program, diagnostics_);
	report_stale_imported_exports(program, diagnostics_);
	resolve_function_calls(program.module);
	report_unresolved_program_calls(program.module, diagnostics_);
	validate_program_stages(program);
	if (diagnostics_.diagnostics().size() != error_count) {
		return program;
	}
	serialize_program(program, diagnostics_);
	return program;
}

Artifact Linker::link_library() {
	Artifact library{ .kind = ArtifactKind::library };
	if (inputs_.empty()) {
		diagnostics_.report(DiagnosticCode::link_no_inputs, DiagnosticSeverity::error, {}, "<link>", "no input artifacts provided");
		return library;
	}

	library = inputs_.front();
	library.kind = ArtifactKind::library;
	for (std::size_t i = 1; i < inputs_.size(); ++i) {
		merge_module(library.module, std::move(inputs_[i].module));
	}
	resolve_function_calls(library.module);

	const bool invalid_exports = report_duplicate_exported_functions(library, diagnostics_);
	const bool stale_imports = report_stale_imported_exports(library, diagnostics_);
	if (invalid_exports || stale_imports) {
		library.bytes.clear();
		return library;
	}

	library.bytes = write_artifact(ArtifactKind::library, library.module);
	return library;
}

Artifact extract_module_interface(const Artifact& source) {
	Artifact module_artifact{ .kind = ArtifactKind::module };
	// Carry over the source name so importer-side diagnostics can point
	// back to the .rtsl this interface came from.
	module_artifact.module.source_name = source.module.source_name;
	module_artifact.module.imports = source.module.imports;
	module_artifact.module.exports = source.module.exports;
	for (const auto& fn : source.module.functions) {
		if (!fn.is_exported())
			continue;
		module_artifact.module.functions.emplace_back(IRFunction{
			.result_id = fn.result_id,
			.return_type_id = fn.return_type_id,
			.parameter_ids = fn.parameter_ids,
			.stage = fn.stage,
			.kind = IRFunction::Kind::exported,
			.link_name = fn.link_name,
			.display_name = fn.display_name,
		});
	}
	if (module_artifact.module.exports.empty()) {
		return module_artifact;
	}
	module_artifact.module.structs = source.module.structs;
	module_artifact.bytes = write_artifact(ArtifactKind::module, module_artifact.module);
	return module_artifact;
}

static bool declares_graphics_stage(const Artifact& program) {
	return !program.module.entries.empty();
}

void Linker::validate_program_stages(const Artifact& program) {
	if (program.module.entries.empty()) {
		diagnostics_.report(DiagnosticCode::link_missing_entry, DiagnosticSeverity::error, {}, "<link>",
							"program link requires at least one stage entry point");
		return;
	}
	if (!declares_graphics_stage(program)) return;

	bool has_vertex = false;
	bool has_fragment = false;
	for (const auto& entry : program.module.entries) {
		if (entry.stage == Stage::vertex) {
			if (has_vertex) {
				diagnostics_.report(DiagnosticCode::link_duplicate_stage, DiagnosticSeverity::error, {}, "<link>",
									"graphics program has more than one vertex stage entry point");
			}
			has_vertex = true;
		}
		if (entry.stage == Stage::fragment) {
			if (has_fragment) {
				diagnostics_.report(DiagnosticCode::link_duplicate_stage, DiagnosticSeverity::error, {}, "<link>",
									"graphics program has more than one fragment stage entry point");
			}
			has_fragment = true;
		}
	}
	if (!has_vertex) {
		diagnostics_.report(DiagnosticCode::link_missing_stage, DiagnosticSeverity::error, {}, "<link>",
							"graphics program is missing a vertex stage (vert)");
	}
	if (!has_fragment) {
		diagnostics_.report(DiagnosticCode::link_missing_stage, DiagnosticSeverity::error, {}, "<link>",
							"graphics program is missing a fragment stage (frag)");
	}
}

} // namespace rtsl
