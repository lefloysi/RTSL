#include "sema/sema.hpp"

#include <span>
#include <string_view>
#include <unordered_map>

namespace rtsl {

Sema::Sema(SourceManager& sources, DiagnosticEngine& diagnostics)
	: sources_(sources), diagnostics_(diagnostics) {}

namespace {

const StructDecl* find_struct(std::span<const StructDecl> structs, std::string_view name) {
	for (const auto& structure : structs) {
		if (structure.name == name) {
			return &structure;
		}
	}
	return nullptr;
}

bool same_parameters(std::span<const ParameterDecl> a, std::span<const ParameterDecl> b) {
	if (a.size() != b.size()) {
		return false;
	}
	for (std::size_t i = 0; i < a.size(); ++i) {
		if (a[i].type != b[i].type) {
			return false;
		}
	}
	return true;
}

bool declares_member_function(const StructDecl& owner, std::string_view name, std::span<const ParameterDecl> parameters, std::string_view return_type) {
	for (const auto& member : owner.member_functions) {
		if (member.name == name &&
			member.return_type == return_type &&
			same_parameters(member.parameters, parameters)) {
			return true;
		}
	}
	return false;
}

} // namespace

SemanticModule Sema::analyze(const TranslationUnit& unit) {
	SemanticModule module{ .source_name = std::string(sources_.name(unit.file_id)) };
	module.imports = unit.imports;
	module.structs = unit.structs;
	module.uniforms = unit.uniforms;
	module.layouts = unit.layouts;
	module.stage_interfaces = unit.stage_interfaces;
	module.using_imports = unit.using_imports;

	// Assign sequential ABI locations to user payload fields. Built-in slots
	// (for example `position(clip)`) do not consume a location.
	for (auto& interface : module.stage_interfaces) {
		u32 next_location = 0;
		for (auto& field : interface.fields) {
			if (field.builtin != BuiltinSlot::none) {
				continue;
			}
			if (field.location != StageIOField::kNoLocation) {
				next_location = field.location + 1;
				continue;
			}
			field.location = next_location++;
		}
	}
	// Every distinct scope maps to a group. Named scopes can be reopened
	// across multiple `uniform name { ... }` blocks (matched by scope_name).
	// Anonymous blocks cannot be reopened: each one gets its own unique
	// anonymous_block_id from the parser, so two `uniform { ... }` blocks end
	// up on different groups. Within a single block, fields share a group and
	// receive sequential members.
	std::unordered_map<std::string, u32> named_sets;
	u32 next_set = 0;
	std::unordered_map<u32, u32> member_counts;
	for (auto& uniform : module.uniforms) {
		const std::string set_key = uniform.is_anonymous
										? "$anon$" + std::to_string(uniform.anonymous_block_id)
										: uniform.scope_name;
		auto [it, inserted] = named_sets.emplace(set_key, next_set);
		if (inserted) {
			uniform.set = next_set++;
		} else {
			uniform.set = it->second;
		}
		uniform.member = member_counts[uniform.set]++;
	}

	for (const auto& decl : unit.declarations) {
		if (decl.kind == DeclKind::namespace_decl && decl.name == "rt") {
			diagnostics_.report(DiagnosticCode::sema_reserved_namespace, DiagnosticSeverity::error, decl.span.begin, module.source_name, "namespace 'rt' is reserved");
			continue;
		}

		if (decl.kind == DeclKind::function) {
			if (const auto scope = decl.name.find("::"); scope != std::string::npos) {
				const auto owner_name = std::string_view(decl.name).substr(0, scope);
				const auto member_name = std::string_view(decl.name).substr(scope + 2);
				const StructDecl* owner = find_struct(module.structs, owner_name);
				if (!owner) {
					diagnostics_.report(DiagnosticCode::sema_duplicate_namespace_decl, DiagnosticSeverity::error, decl.span.begin, module.source_name,
										"member function definition has unknown owner type");
				} else if (!declares_member_function(*owner, member_name, decl.parameters, decl.return_type)) {
					diagnostics_.report(DiagnosticCode::sema_duplicate_export_decl, DiagnosticSeverity::error, decl.span.begin, module.source_name,
										"member function definition has no matching declaration in its owner type");
				}
			}
		}

		module.symbols.emplace_back(SemanticSymbol{
			.kind = decl.kind,
			.name = decl.name,
			.parameters = decl.parameters,
			.return_type = decl.return_type,
			.body_statements = decl.body_statements,
			.exported = decl.exported,
			.stage = decl.stage,
			.has_body = decl.has_body,
		});
		if (decl.exported && decl.kind != DeclKind::import) {
			module.exports.emplace_back(ExportSymbol{
				.name = decl.name,
				.kind = decl.kind == DeclKind::function ? "function" : "symbol",
				.type = decl.return_type,
			});
		}
	}

	return module;
}

} // namespace rtsl
