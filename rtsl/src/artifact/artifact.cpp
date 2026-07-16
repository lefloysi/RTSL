#include "artifact/artifact.hpp"

#include "frontend/ast.hpp"
#include "sema/stage_rules.hpp"
#include "support/binary.hpp"

#include <span>
#include <unordered_map>

namespace rtsl {

struct ArtifactHeader {
	u32 magic = ArtifactMagic;
	u16 version_major = ArtifactVersionMajor;
	u16 version_minor = ArtifactVersionMinor;
	ArtifactKind kind = ArtifactKind::object;
	u08 endian = 1;
	u08 reserved = 0;
};

template <bin::byte_stream Stream, bin::data<IRInstruction> Inst>
bin::error process(Stream& stream, Inst& inst) {
	return stream(
		bin::field("op", inst.op),
		bin::field("result_id", inst.result_id),
		bin::field("type_id", inst.type_id),
		bin::field("operands", inst.operands),
		bin::field("literals", inst.literals),
		bin::field("loc_file", inst.loc.file_id),
		bin::field("loc_line", inst.loc.line),
		bin::field("loc_column", inst.loc.column));
}

template <bin::byte_stream Stream, bin::data<IRDecoration> Dec>
bin::error process(Stream& stream, Dec& dec) {
	return stream(
		bin::field("target", dec.target),
		bin::field("kind", dec.kind),
		bin::field("member_index", dec.member_index),
		bin::field("literals", dec.literals));
}

template <bin::byte_stream Stream, bin::data<ExportSymbol> Sym>
bin::error process(Stream& stream, Sym& sym) {
	return stream(
		bin::field("name", sym.name),
		bin::field("kind", sym.kind),
		bin::field("type", sym.type),
		bin::field("interface_hash", sym.interface_hash));
}

template <bin::byte_stream Stream, bin::data<StructField> Field>
bin::error process(Stream& stream, Field& f) {
	return stream(
		bin::field("type", f.type),
		bin::field("name", f.name));
}

template <bin::byte_stream Stream, bin::data<StructDecl> Decl>
bin::error process(Stream& stream, Decl& decl) {
	return stream(
		bin::field("name", decl.name),
		bin::field("fields", decl.fields));
}

template <bin::byte_stream Stream, bin::data<StageIOField> Field>
bin::error process(Stream& stream, Field& f) {
	return stream(
		bin::field("name", f.name),
		bin::field("interpolation", f.interpolation),
		bin::field("placement", f.placement),
		bin::field("location", f.location),
		bin::field("member_index", f.member_index));
}

template <bin::byte_stream Stream, bin::data<StageInterface> Iface>
bin::error process(Stream& stream, Iface& iface) {
	return stream(
		bin::field("role", iface.role),
		bin::field("type_name", iface.type_name),
		bin::field("fields", iface.fields));
}

template <bin::byte_stream Stream, bin::data<Artifact::EntryPoint> Entry>
bin::error process(Stream& stream, Entry& entry) {
	return stream(
		bin::field("name", entry.name),
		bin::field("mangled_name", entry.mangled_name),
		bin::field("stage", entry.stage),
		bin::field("function_id", entry.function_id));
}

template <bin::byte_stream Stream, bin::data<ArtifactHeader> Header>
bin::error process(Stream& stream, Header& header) {
	return stream(
		bin::field("magic", header.magic),
		bin::field("version_major", header.version_major),
		bin::field("version_minor", header.version_minor),
		bin::field("kind", header.kind),
		bin::field("endian", header.endian),
		bin::field("reserved", header.reserved));
}

namespace {

struct StringPoolHash {
	using is_transparent = void;
	std::size_t operator()(std::string_view s) const noexcept { return std::hash<std::string_view>{}(s); }
	std::size_t operator()(const char* s) const noexcept { return std::hash<std::string_view>{}(s); }
};

struct StringPoolEqual {
	using is_transparent = void;
	bool operator()(std::string_view a, std::string_view b) const noexcept { return a == b; }
};

struct StringPool {
	std::vector<std::string> values;
	std::unordered_map<std::string, u32, StringPoolHash, StringPoolEqual> ids;

	u32 intern(std::string_view value) {
		if (const auto it = ids.find(value); it != ids.end()) return it->second;
		const u32 id = static_cast<u32>(values.size());
		values.emplace_back(value);
		ids.emplace(values.back(), id);
		return id;
	}
};

StringPool build_string_pool(const IRModule& module, std::span<const Artifact::EntryPoint> entries, bool linked_program) {
	StringPool pool;
	pool.intern(module.source_name);
	for (const auto& uniform : module.uniforms) {
		pool.intern(uniform.scope_name);
		pool.intern(uniform.name);
		for (const auto& field : uniform.inline_fields) pool.intern(field.name);
	}
	if (!linked_program) {
		for (const auto& decl : module.structs) {
			pool.intern(decl.name);
			for (const auto& field : decl.fields) pool.intern(field.name);
		}
	}
	for (const auto& entry : entries) {
		pool.intern(entry.name);
		pool.intern(entry.mangled_name);
	}
	for (const auto& fn : module.functions) {
		pool.intern(fn.source_name);
		pool.intern(fn.display_name_text);
	}
	return pool;
}

IRModule with_serialized_string_ids(IRModule module, const StringPool& pool) {
	const auto find = [&](std::string_view value) -> StringId {
		const auto it = pool.ids.find(value);
		return it == pool.ids.end() ? StringId{} : StringId{ .value = it->second };
	};
	for (auto& fn : module.functions) {
		const std::string_view display_name = fn.display_name_text.empty() ? std::string_view(fn.source_name) : std::string_view(fn.display_name_text);
		fn.display_name = find(display_name);
		fn.mangled_name = find(fn.source_name);
	}
	return module;
}

std::vector<Artifact::EntryPoint> default_entries_from_module(const IRModule& module) {
	std::vector<Artifact::EntryPoint> entries;
	for (const auto& fn : module.functions) {
		if (fn.stage.empty()) continue;
		const std::string entry_name = backend_entry_name(fn.stage);
		entries.push_back(Artifact::EntryPoint{
			.name = entry_name,
			.mangled_name = entry_name,
			.stage = fn.stage,
			.function_id = fn.result_id,
		});
	}
	return entries;
}

std::vector<IRFunction> serialized_functions(const IRModule& module, bool linked_program) {
	std::vector<IRFunction> functions;
	functions.reserve(module.functions.size());
	for (auto fn : module.functions) {
		if (fn.is_constructor) continue;
		if (linked_program) fn.source_name.clear();
		functions.push_back(std::move(fn));
	}
	return functions;
}

void report_read_error(DiagnosticEngine* diagnostics, std::string message) {
	if (diagnostics)
		diagnostics->report(DiagnosticCode::artifact_error, DiagnosticSeverity::error, {}, "<artifact>", message);
}

template <bin::byte_stream Stream>
bin::error process(Stream& stream, IRFunction& fn);

template <bin::byte_stream Stream>
bin::error process(Stream& stream, IRCallTarget& target);

template <bin::byte_stream Stream>
bin::error process(Stream& stream, IRFunctionDebugInfo& dbg);

template <bin::byte_stream Stream>
bin::error process(Stream& stream, UniformBinding& u);

template <typename Stream>
bool propagate(Stream&, bin::error err, DiagnosticEngine* diagnostics, const char* what) {
	if (!err) return true;
	report_read_error(diagnostics, std::string(what) + ": " + err.message);
	return false;
}

template <bin::writable_stream Stream>
bin::error write_header(Stream& stream, ArtifactKind kind) {
	ArtifactHeader header{ .kind = kind };
	return stream(bin::field("header", header));
}

template <bin::readable_stream Stream>
bool read_header(Stream& stream, ArtifactKind& kind, DiagnosticEngine* diagnostics) {
	ArtifactHeader header;
	if (!propagate(stream, stream(bin::field("header", header)), diagnostics, "header")) return false;
	if (header.magic != ArtifactMagic) {
		report_read_error(diagnostics, "invalid RTSL artifact magic");
		return false;
	}
	if (header.version_major != ArtifactVersionMajor) {
		report_read_error(diagnostics, "unsupported RTSL artifact version");
		return false;
	}
	if (header.kind < ArtifactKind::object || header.kind > ArtifactKind::program) {
		report_read_error(diagnostics, "invalid RTSL artifact kind");
		return false;
	}
	if (header.endian != 1) {
		report_read_error(diagnostics, "unsupported RTSL artifact endian marker");
		return false;
	}
	kind = header.kind;
	return true;
}

template <bin::writable_stream Stream>
bin::error write_ir_module_data(Stream& stream, const IRModule& module, bool linked_program) {
	auto functions = serialized_functions(module, linked_program);
	RTSL_BIN_TRY(stream(
		bin::field("next_id", module.next_id),
		bin::field("type_constant_pool", module.type_constant_pool),
		bin::field("global_variables", module.global_variables)));

	u32 function_count = static_cast<u32>(functions.size());
	RTSL_BIN_TRY(stream(bin::field("function_count", function_count)));
	for (auto& fn : functions) {
		RTSL_BIN_TRY(process(stream, fn));
	}

	const std::span<const IRCallTarget> call_targets = linked_program ? std::span<const IRCallTarget>{} : std::span<const IRCallTarget>{ module.call_targets };
	u32 call_target_count = static_cast<u32>(call_targets.size());
	RTSL_BIN_TRY(stream(bin::field("call_target_count", call_target_count)));
	for (const auto& target : call_targets) {
		IRCallTarget local = target;
		RTSL_BIN_TRY(process(stream, local));
	}

	u32 debug_count = static_cast<u32>(module.function_debug.size());
	RTSL_BIN_TRY(stream(bin::field("debug_count", debug_count)));
	for (const auto& dbg : module.function_debug) {
		IRFunctionDebugInfo local = dbg;
		RTSL_BIN_TRY(process(stream, local));
	}
	return {};
}

template <bin::readable_stream Stream>
bool read_ir_module_data(Stream& stream, Artifact& artifact, bool linked_program, DiagnosticEngine* diagnostics) {
	(void)linked_program;
	if (!propagate(stream, stream(
			bin::field("next_id", artifact.module.next_id),
			bin::field("type_constant_pool", artifact.module.type_constant_pool),
			bin::field("global_variables", artifact.module.global_variables)),
		diagnostics, "ir_module")) return false;
	u32 function_count = 0;
	if (!propagate(stream, stream(bin::field("function_count", function_count)), diagnostics, "ir_module.function_count")) return false;
	artifact.module.functions.reserve(function_count);
	for (u32 i = 0; i < function_count; ++i) {
		IRFunction fn;
		if (!propagate(stream, process(stream, fn), diagnostics, "ir_module.function")) return false;
		artifact.module.functions.push_back(std::move(fn));
	}
	u32 call_target_count = 0;
	if (!propagate(stream, stream(bin::field("call_target_count", call_target_count)), diagnostics, "ir_module.call_target_count")) return false;
	artifact.module.call_targets.reserve(call_target_count);
	for (u32 i = 0; i < call_target_count; ++i) {
		IRCallTarget target;
		if (!propagate(stream, process(stream, target), diagnostics, "ir_module.call_target")) return false;
		artifact.module.call_targets.push_back(std::move(target));
	}
	u32 debug_count = 0;
	if (!propagate(stream, stream(bin::field("debug_count", debug_count)), diagnostics, "ir_module.debug_count")) return false;
	artifact.module.function_debug.reserve(debug_count);
	for (u32 i = 0; i < debug_count; ++i) {
		IRFunctionDebugInfo dbg;
		if (!propagate(stream, process(stream, dbg), diagnostics, "ir_module.function_debug")) return false;
		artifact.module.function_debug.push_back(std::move(dbg));
	}
	for (auto& fn : artifact.module.functions) {
		if (fn.display_name.value < artifact.strings.size()) {
			fn.display_name_text = artifact.strings[fn.display_name.value];
		}
	}
	return true;
}

template <bin::byte_stream Stream>
bin::error process(Stream& stream, IRFunction& fn) {
	return stream(
		bin::field("result_id", fn.result_id),
		bin::field("function_type_id", fn.function_type_id),
		bin::field("return_type_id", fn.return_type_id),
		bin::field("stage", fn.stage),
		bin::field("generated", fn.generated),
		bin::field("exported", fn.exported),
		bin::field("display_name", fn.display_name.value),
		bin::field("mangled_name", fn.mangled_name.value),
		bin::field("source_name", fn.source_name),
		bin::field("parameter_ids", fn.parameter_ids),
		bin::field("body", fn.body));
}

template <bin::byte_stream Stream>
bin::error process(Stream& stream, IRCallTarget& target) {
	return stream(
		bin::field("display_name", target.display_name),
		bin::field("mangled_name", target.mangled_name));
}

template <bin::byte_stream Stream>
bin::error process(Stream& stream, IRFunctionDebugInfo& dbg) {
	RTSL_BIN_TRY(stream(bin::field("display_name", dbg.display_name.value)));
	if constexpr (bin::writable_stream<Stream>) {
		u32 count = static_cast<u32>(dbg.parameter_names.size());
		RTSL_BIN_TRY(stream(bin::field("parameter_count", count)));
		for (const StringId id : dbg.parameter_names) {
			u32 value = id.value;
			RTSL_BIN_TRY(stream(bin::field("parameter_name", value)));
		}
		return {};
	} else {
		u32 count = 0;
		RTSL_BIN_TRY(stream(bin::field("parameter_count", count)));
		dbg.parameter_names.clear();
		dbg.parameter_names.reserve(count);
		for (u32 i = 0; i < count; ++i) {
			u32 value = 0;
			RTSL_BIN_TRY(stream(bin::field("parameter_name", value)));
			dbg.parameter_names.push_back(StringId{ value });
		}
		return {};
	}
}

template <bin::byte_stream Stream>
bin::error process(Stream& stream, StringId& id) {
	return stream(bin::field("value", id.value));
}

template <bin::writable_stream Stream>
bin::error write_resources(Stream& stream, const IRModule& module) {
	u32 count = static_cast<u32>(module.uniforms.size());
	RTSL_BIN_TRY(stream(bin::field("resource_count", count)));
	for (const auto& uniform : module.uniforms) {
		UniformBinding local = uniform;
		RTSL_BIN_TRY(process(stream, local));
	}
	return {};
}

template <bin::readable_stream Stream>
bool read_resources(Stream& stream, Artifact& artifact, DiagnosticEngine* diagnostics) {
	u32 count = 0;
	if (!propagate(stream, stream(bin::field("resource_count", count)), diagnostics, "resources.count")) return false;
	artifact.module.uniforms.reserve(count);
	for (u32 i = 0; i < count; ++i) {
		UniformBinding uniform;
		if (!propagate(stream, process(stream, uniform), diagnostics, "resources.uniform")) return false;
		artifact.module.uniforms.push_back(std::move(uniform));
	}
	return true;
}

template <bin::byte_stream Stream>
bin::error process(Stream& stream, UniformBinding& u) {
	return stream(
		bin::field("scope_name", u.scope_name),
		bin::field("name", u.name),
		bin::field("type", u.type),
		bin::field("inline_fields", u.inline_fields),
		bin::field("access", u.access),
		bin::field("set", u.set),
		bin::field("member", u.member),
		bin::field("type_id", u.type_id),
		bin::field("is_anonymous", u.is_anonymous),
		bin::field("anonymous_block_id", u.anonymous_block_id));
}

} // namespace

std::vector<u08> write_artifact(ArtifactKind kind, const IRModule& module) {
	const bool linked_program = kind == ArtifactKind::program;
	const auto entries = default_entries_from_module(module);
	const auto strings = build_string_pool(module, entries, linked_program);
	const IRModule serialized_module = with_serialized_string_ids(module, strings);

	bin::write_stream stream;
	if (auto err = write_header(stream, kind); err) return {};
	if (auto err = stream(bin::field("strings", strings.values)); err) return {};
	if (auto err = write_ir_module_data(stream, serialized_module, linked_program); err) return {};
	if (!linked_program) {
		if (auto err = stream(
				bin::field("imports", serialized_module.imports),
				bin::field("exports", serialized_module.exports),
				bin::field("imported_exports", serialized_module.imported_exports),
				bin::field("structs", serialized_module.structs)); err) return {};
	}
	if (auto err = stream(bin::field("decorations", serialized_module.decorations)); err) return {};
	if (auto err = write_resources(stream, serialized_module); err) return {};
	if (auto err = stream(
			bin::field("stage_interfaces", serialized_module.stage_interfaces),
			bin::field("entries", entries)); err) return {};
	return stream.take_written();
}

std::vector<u08> write_debug_artifact(const IRModule& module) {
	return write_artifact(ArtifactKind::object, module);
}

bool read_artifact(std::span<const u08> data, Artifact& artifact, DiagnosticEngine* diagnostics) {
	bin::read_stream stream(data);
	ArtifactKind kind = ArtifactKind::object;
	if (!read_header(stream, kind, diagnostics)) return false;

	artifact = Artifact{};
	artifact.kind = kind;
	artifact.bytes.assign(data.begin(), data.end());
	const bool linked_program = kind == ArtifactKind::program;

	if (!propagate(stream, stream(bin::field("strings", artifact.strings)), diagnostics, "strings")) return false;
	if (!artifact.strings.empty()) artifact.module.source_name = artifact.strings.front();
	if (!read_ir_module_data(stream, artifact, linked_program, diagnostics)) return false;
	if (!linked_program) {
		if (!propagate(stream, stream(
				bin::field("imports", artifact.module.imports),
				bin::field("exports", artifact.module.exports),
				bin::field("imported_exports", artifact.module.imported_exports),
				bin::field("structs", artifact.module.structs)),
			diagnostics, "link_metadata")) return false;
	}
	if (!propagate(stream, stream(bin::field("decorations", artifact.module.decorations)), diagnostics, "decorations")) return false;
	if (!read_resources(stream, artifact, diagnostics)) return false;
	if (!propagate(stream, stream(
			bin::field("stage_interfaces", artifact.module.stage_interfaces),
			bin::field("entries", artifact.entries)),
		diagnostics, "reflection")) return false;
	if (!stream.at_end()) {
		report_read_error(diagnostics, "artifact has trailing bytes");
		return false;
	}

	artifact.structs = artifact.module.structs;
	artifact.uniforms = artifact.module.uniforms;
	artifact.stage_interfaces = artifact.module.stage_interfaces;
	artifact.imports = artifact.module.imports;
	artifact.imported_exports = artifact.module.imported_exports;
	artifact.exports = artifact.module.exports;
	return true;
}

std::string_view artifact_extension(ArtifactKind kind) {
	switch (kind) {
	case ArtifactKind::object: return ".rtslo";
	case ArtifactKind::module: return ".rtslm";
	case ArtifactKind::library: return ".rtsll";
	case ArtifactKind::program: return ".rtslp";
	}
	return ".rtslbin";
}

} // namespace rtsl
