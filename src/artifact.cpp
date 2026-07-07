#include "artifact.hpp"

#include "ast.hpp"
#include "binary.hpp"

#include <cstring>
#include <span>
#include <unordered_map>

namespace rtsl {

constexpr u32 Magic = 0x4c535452;
constexpr u16 VersionMajor = 0;
constexpr u16 VersionMinor = 4;
constexpr u32 HeaderSize = 48;
constexpr u32 SectionEntrySize = 32;

enum class SectionKind : u32 {
	string_table = 1,
	ir_module = 2,
	import_table = 3,
	export_table = 4,
	decoration_table = 5,
	struct_table = 6,
	resource_table = 7,
	stage_interface_table = 8,
	entry_table = 9,
	debug_table = 10,
	imported_export_table = 11,
};

struct Section {
	SectionKind kind;
	std::vector<u08> bytes;
};

// ---- process() overloads for RTSL types ---------------------------------
// These live in namespace rtsl so ADL from bin::process<T>() finds them.
// Each type serializes with `stream(field("name", value), ...)`: named
// fields drive diagnostic context, byte layout is preserved from the
// hand-rolled version below.

// Each process() overload deduces its target's cv-qualification through the
// `bin::data<T, D>` concept: `T& value` binds both `D&` and `const D&`, so
// a single overload handles read and write paths. Bodies never mutate on the
// write path, so const flows through cleanly.

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
		bin::field("type", sym.type));
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
		bin::field("builtin", f.builtin),
		bin::field("location", f.location));
}

template <bin::byte_stream Stream, bin::data<StageInterface> Iface>
bin::error process(Stream& stream, Iface& iface) {
	return stream(
		bin::field("role", iface.role),
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

// ---- String pool --------------------------------------------------------

struct StringPoolHash {
	using is_transparent = void;
	std::size_t operator()(std::string_view s) const noexcept { return std::hash<std::string_view>{}(s); }
	std::size_t operator()(const std::string& s) const noexcept { return std::hash<std::string_view>{}(s); }
};

struct StringPoolEqual {
	using is_transparent = void;
	bool operator()(std::string_view a, std::string_view b) const noexcept { return a == b; }
};

struct StringPool {
	std::vector<std::string> values;
	std::unordered_map<std::string, u32, StringPoolHash, StringPoolEqual> ids;

	u32 intern(std::string_view value) {
		const auto it = ids.find(value);
		if (it != ids.end()) return it->second;
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
	for (const auto& iface : module.stage_interfaces) {
		if (iface.role == StageRole::varying) continue;
		for (const auto& field : iface.fields) pool.intern(field.name);
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
	return pool;
}

// ---- Section builders ---------------------------------------------------

Section make_string_table(const StringPool& pool) {
	Section section{ .kind = SectionKind::string_table };
	bin::write_stream stream;
	(void)stream(bin::field("strings", pool.values));
	section.bytes = stream.take_written();
	return section;
}

Section make_ir_module_section(const IRModule& module, bool linked_program) {
	Section section{ .kind = SectionKind::ir_module };
	bin::write_stream stream;

	// Constructors are dead post-inlining. Skip them so `Foo::Foo` never lands
	// in the artifact. Same reason source_name gets elided in linked programs
	// (no relink means no cross-module call resolution needed).
	std::vector<IRFunction> live_functions;
	live_functions.reserve(module.functions.size());
	for (const auto& fn : module.functions) {
		if (!fn.is_constructor) live_functions.push_back(fn);
	}
	if (linked_program) {
		for (auto& fn : live_functions) fn.source_name.clear();
	}

	(void)stream(
		bin::field("next_id", module.next_id),
		bin::field("type_constant_pool", module.type_constant_pool),
		bin::field("global_variables", module.global_variables));

	const u32 fn_count = static_cast<u32>(live_functions.size());
	(void)stream(bin::field("function_count", fn_count));
	for (auto& fn : live_functions) {
		(void)stream(
			bin::field("result_id", fn.result_id),
			bin::field("function_type_id", fn.function_type_id),
			bin::field("return_type_id", fn.return_type_id),
			bin::field("stage", fn.stage));
		const u08 generated = fn.generated ? 1 : 0;
		const u08 exported = fn.exported ? 1 : 0;
		(void)stream(
			bin::field("generated", generated),
			bin::field("exported", exported),
			bin::field("display_name", fn.display_name.value),
			bin::field("mangled_name", fn.mangled_name.value),
			bin::field("source_name", fn.source_name),
			bin::field("parameter_ids", fn.parameter_ids),
			bin::field("body", fn.body));
	}

	// Pending FunctionCall target names. Linked programs carry none.
	std::vector<std::string> target_names = linked_program ? std::vector<std::string>{} : module.call_target_names;
	(void)stream(bin::field("call_target_names", target_names));

	const u32 debug_count = static_cast<u32>(module.function_debug.size());
	(void)stream(bin::field("debug_count", debug_count));
	for (const auto& dbg : module.function_debug) {
		(void)stream(bin::field("display_name", dbg.display_name.value));
		const u32 param_count = static_cast<u32>(dbg.parameter_names.size());
		(void)stream(bin::field("param_count", param_count));
		for (const auto& name : dbg.parameter_names) {
			(void)stream(bin::field("param_name", name.value));
		}
	}

	section.bytes = stream.take_written();
	return section;
}

Section make_import_section(const IRModule& module) {
	Section section{ .kind = SectionKind::import_table };
	bin::write_stream stream;
	(void)stream(bin::field("imports", module.imports));
	section.bytes = stream.take_written();
	return section;
}

Section make_export_section(const IRModule& module) {
	Section section{ .kind = SectionKind::export_table };
	bin::write_stream stream;
	(void)stream(bin::field("exports", module.exports));
	section.bytes = stream.take_written();
	return section;
}

Section make_imported_export_section(const IRModule& module) {
	Section section{ .kind = SectionKind::imported_export_table };
	bin::write_stream stream;
	(void)stream(bin::field("imported_exports", module.imported_exports));
	section.bytes = stream.take_written();
	return section;
}

Section make_decoration_section(const IRModule& module) {
	Section section{ .kind = SectionKind::decoration_table };
	bin::write_stream stream;
	(void)stream(bin::field("decorations", module.decorations));
	section.bytes = stream.take_written();
	return section;
}

Section make_struct_section(const IRModule& module) {
	Section section{ .kind = SectionKind::struct_table };
	bin::write_stream stream;
	(void)stream(bin::field("structs", module.structs));
	section.bytes = stream.take_written();
	return section;
}

Section make_resource_section(const IRModule& module) {
	Section section{ .kind = SectionKind::resource_table };
	bin::write_stream stream;
	const u32 count = static_cast<u32>(module.uniforms.size());
	(void)stream(bin::field("count", count));
	for (const auto& u : module.uniforms) {
		const u08 access = static_cast<u08>(u.access);
		const u08 is_anonymous = u.is_anonymous ? 1 : 0;
		const u32 field_count = static_cast<u32>(u.inline_fields.size());
		(void)stream(
			bin::field("scope_name", u.scope_name),
			bin::field("name", u.name),
			bin::field("type_id", u.type_id),
			bin::field("access", access),
			bin::field("set", u.set),
			bin::field("member", u.member),
			bin::field("is_anonymous", is_anonymous),
			bin::field("anonymous_block_id", u.anonymous_block_id),
			bin::field("field_count", field_count));
		for (const auto& f : u.inline_fields) {
			(void)stream(bin::field("field_name", f.name));
		}
	}
	section.bytes = stream.take_written();
	return section;
}

Section make_stage_interface_section(const IRModule& module) {
	// Only host-visible boundaries (input, output) reach the artifact. Varyings
	// are pipeline-internal — the linker matches them by location, so no
	// reflection carries them.
	Section section{ .kind = SectionKind::stage_interface_table };
	bin::write_stream stream;
	std::vector<StageInterface> emit;
	emit.reserve(module.stage_interfaces.size());
	for (const auto& iface : module.stage_interfaces) {
		if (iface.role == StageRole::varying) continue;
		emit.push_back(iface);
	}
	(void)stream(bin::field("interfaces", emit));
	section.bytes = stream.take_written();
	return section;
}

Section make_entry_section(std::span<const Artifact::EntryPoint> entries) {
	Section section{ .kind = SectionKind::entry_table };
	bin::write_stream stream;
	// Cheap copy so the free-function process() overload's mutable requirement
	// can bind. A dedicated const overload could avoid this but at the cost of
	// duplicating every serializer.
	std::vector<Artifact::EntryPoint> local(entries.begin(), entries.end());
	(void)stream(bin::field("entries", local));
	section.bytes = stream.take_written();
	return section;
}

// ---- Container header + section table -----------------------------------

std::vector<u08> write_container(ArtifactKind kind, std::vector<Section> sections) {
	const u32 section_count = static_cast<u32>(sections.size());
	const u64 section_table_offset = HeaderSize;
	u64 data_offset = HeaderSize + static_cast<u64>(SectionEntrySize) * section_count;
	std::vector<u64> offsets;
	offsets.reserve(sections.size());
	for (const auto& section : sections) {
		offsets.push_back(data_offset);
		data_offset += section.bytes.size();
	}

	bin::write_stream stream;
	const u16 kind_val = static_cast<u16>(kind);
	const u08 endian = 1;
	const u08 reserved8 = 0;
	const u32 reserved32 = 0;
	(void)stream(
		bin::field("magic", Magic),
		bin::field("version_major", VersionMajor),
		bin::field("version_minor", VersionMinor),
		bin::field("kind", kind_val),
		bin::field("endian", endian),
		bin::field("reserved_a", reserved8),
		bin::field("reserved_b", reserved32),
		bin::field("header_size", HeaderSize),
		bin::field("section_count", section_count),
		bin::field("section_table_offset", section_table_offset),
		bin::field("file_size", data_offset));

	auto out = stream.take_written();
	// Header is fixed-size; pad any trailing bytes so the section table lands
	// at HeaderSize regardless of how many fields the header actually spent.
	out.resize(HeaderSize, u08{ 0 });

	// Section table entries — 32 bytes each, fixed layout.
	bin::write_stream table;
	for (std::size_t i = 0; i < sections.size(); ++i) {
		const u32 skind = static_cast<u32>(sections[i].kind);
		const u32 res32 = 0;
		const u64 offset = offsets[i];
		const u64 size = static_cast<u64>(sections[i].bytes.size());
		const u32 flag = 1;
		const u32 res_tail = 0;
		(void)table(
			bin::field("kind", skind),
			bin::field("reserved", res32),
			bin::field("offset", offset),
			bin::field("size", size),
			bin::field("flag", flag),
			bin::field("reserved_tail", res_tail));
	}
	auto table_bytes = table.take_written();
	out.insert(out.end(), table_bytes.begin(), table_bytes.end());

	for (const auto& section : sections)
		out.insert(out.end(), section.bytes.begin(), section.bytes.end());
	return out;
}

void report_read_error(DiagnosticEngine* diagnostics, std::string message) {
	if (diagnostics)
		diagnostics->report(DiagnosticCode::artifact_error, DiagnosticSeverity::error, {}, "<artifact>", message);
}

std::vector<Artifact::EntryPoint> default_entries_from_module(const IRModule& module) {
	std::vector<Artifact::EntryPoint> entries;
	for (const auto& fn : module.functions) {
		if (fn.stage == StageKind::none) continue;
		entries.push_back(Artifact::EntryPoint{
			.name = std::string(stage_entry_name(fn.stage)),
			.mangled_name = std::string(stage_entry_name(fn.stage)),
			.stage = fn.stage,
			.function_id = fn.result_id,
		});
	}
	return entries;
}

std::vector<u08> write_artifact(ArtifactKind kind, const IRModule& module) {
	const auto entries = default_entries_from_module(module);
	// A linked program will never re-link: dropping the struct/import/export
	// tables leaves just the reflection surface the runtime actually reads.
	const bool linked_program = kind == ArtifactKind::program;
	const auto strings = build_string_pool(module, entries, linked_program);
	std::vector<Section> sections;
	sections.push_back(make_string_table(strings));
	sections.push_back(make_ir_module_section(module, linked_program));
	if (!linked_program) {
		sections.push_back(make_import_section(module));
		sections.push_back(make_export_section(module));
		sections.push_back(make_imported_export_section(module));
		sections.push_back(make_struct_section(module));
	}
	sections.push_back(make_decoration_section(module));
	sections.push_back(make_resource_section(module));
	sections.push_back(make_stage_interface_section(module));
	sections.push_back(make_entry_section(entries));
	return write_container(kind, std::move(sections));
}

std::vector<u08> write_debug_artifact(const IRModule&) {
	return write_container(ArtifactKind::object, {});
}

std::vector<u08> write_linked_program(std::span<const Artifact> inputs) {
	if (inputs.empty()) return {};
	return write_artifact(ArtifactKind::program, inputs.front().module);
}

// ---- Reader -------------------------------------------------------------

bool read_artifact(std::span<const u08> data, Artifact& artifact, DiagnosticEngine* diagnostics) {
	if (data.size() < HeaderSize) {
		report_read_error(diagnostics, "artifact is smaller than the header");
		return false;
	}
	bin::read_stream header(data);
	u32 magic = 0;
	u16 version_major = 0;
	u16 version_minor = 0;
	u16 kind_raw = 0;
	u08 endian = 0;
	u08 reserved8 = 0;
	u32 reserved32 = 0;
	u32 header_size = 0;
	u32 section_count = 0;
	u64 section_table_offset = 0;
	u64 file_size = 0;
	if (auto err = header(
			bin::field("magic", magic),
			bin::field("version_major", version_major),
			bin::field("version_minor", version_minor),
			bin::field("kind", kind_raw),
			bin::field("endian", endian),
			bin::field("reserved_a", reserved8),
			bin::field("reserved_b", reserved32),
			bin::field("header_size", header_size),
			bin::field("section_count", section_count),
			bin::field("section_table_offset", section_table_offset),
			bin::field("file_size", file_size));
		err) {
		report_read_error(diagnostics, "invalid RTSL artifact header: " + err.message);
		return false;
	}
	if (magic != Magic) {
		report_read_error(diagnostics, "invalid RTSL artifact magic");
		return false;
	}
	if (version_major != VersionMajor) {
		report_read_error(diagnostics, "unsupported RTSL artifact version");
		return false;
	}
	if (endian != 1 || header_size != HeaderSize || file_size != data.size()) {
		report_read_error(diagnostics, "invalid RTSL artifact header");
		return false;
	}
	artifact = Artifact{};
	artifact.kind = static_cast<ArtifactKind>(kind_raw);
	artifact.bytes.assign(data.begin(), data.end());

	for (u32 i = 0; i < section_count; ++i) {
		const auto entry_offset = static_cast<std::size_t>(section_table_offset + i * SectionEntrySize);
		if (entry_offset + SectionEntrySize > data.size()) {
			report_read_error(diagnostics, "section table entry out of bounds");
			return false;
		}
		bin::read_stream entry(data.subspan(entry_offset, SectionEntrySize));
		u32 section_kind_raw = 0;
		u32 e_reserved = 0;
		u64 section_offset = 0;
		u64 section_size = 0;
		u32 e_flag = 0;
		u32 e_reserved_tail = 0;
		if (auto err = entry(
				bin::field("kind", section_kind_raw),
				bin::field("reserved", e_reserved),
				bin::field("offset", section_offset),
				bin::field("size", section_size),
				bin::field("flag", e_flag),
				bin::field("reserved_tail", e_reserved_tail));
			err) {
			report_read_error(diagnostics, "section table entry: " + err.message);
			return false;
		}
		if (section_offset + section_size > data.size()) {
			report_read_error(diagnostics, "section payload is out of bounds");
			return false;
		}
		const auto section_kind = static_cast<SectionKind>(section_kind_raw);
		auto section_bytes = data.subspan(static_cast<std::size_t>(section_offset), static_cast<std::size_t>(section_size));
		bin::read_stream r(section_bytes);
		auto propagate = [&](bin::error err, const char* what) -> bool {
			if (!err) return true;
			report_read_error(diagnostics, std::string(what) + ": " + err.message);
			return false;
		};

		switch (section_kind) {
		case SectionKind::string_table: {
			if (!propagate(r(bin::field("strings", artifact.strings)), "string_table")) return false;
			if (!artifact.strings.empty())
				artifact.module.source_name = artifact.strings.front();
			break;
		}
		case SectionKind::ir_module: {
			if (!propagate(r(
					bin::field("next_id", artifact.module.next_id),
					bin::field("type_constant_pool", artifact.module.type_constant_pool),
					bin::field("global_variables", artifact.module.global_variables)),
				"ir_module.prefix")) return false;
			u32 fn_count = 0;
			if (!propagate(r(bin::field("function_count", fn_count)), "ir_module.function_count")) return false;
			artifact.module.functions.reserve(fn_count);
			for (u32 idx = 0; idx < fn_count; ++idx) {
				IRFunction fn;
				u08 generated = 0;
				u08 exported = 0;
				if (!propagate(r(
						bin::field("result_id", fn.result_id),
						bin::field("function_type_id", fn.function_type_id),
						bin::field("return_type_id", fn.return_type_id),
						bin::field("stage", fn.stage),
						bin::field("generated", generated),
						bin::field("exported", exported),
						bin::field("display_name", fn.display_name.value),
						bin::field("mangled_name", fn.mangled_name.value),
						bin::field("source_name", fn.source_name),
						bin::field("parameter_ids", fn.parameter_ids),
						bin::field("body", fn.body)),
					"ir_module.function")) return false;
				fn.generated = generated != 0;
				fn.exported = exported != 0;
				artifact.module.functions.push_back(std::move(fn));
			}
			if (!propagate(r(bin::field("call_target_names", artifact.module.call_target_names)),
				"ir_module.call_target_names")) return false;
			u32 debug_count = 0;
			if (!propagate(r(bin::field("debug_count", debug_count)), "ir_module.debug_count")) return false;
			artifact.module.function_debug.reserve(debug_count);
			for (u32 idx = 0; idx < debug_count; ++idx) {
				IRFunctionDebugInfo dbg;
				u32 param_count = 0;
				if (!propagate(r(
						bin::field("display_name", dbg.display_name.value),
						bin::field("param_count", param_count)),
					"function_debug.header")) return false;
				dbg.parameter_names.reserve(param_count);
				for (u32 p = 0; p < param_count; ++p) {
					u32 id = 0;
					if (!propagate(r(bin::field("param_name", id)), "function_debug.param")) return false;
					dbg.parameter_names.push_back(StringId{ id });
				}
				artifact.module.function_debug.push_back(std::move(dbg));
			}
			break;
		}
		case SectionKind::import_table: {
			if (!propagate(r(bin::field("imports", artifact.module.imports)), "import_table")) return false;
			artifact.imports = artifact.module.imports;
			break;
		}
		case SectionKind::export_table: {
			if (!propagate(r(bin::field("exports", artifact.module.exports)), "export_table")) return false;
			artifact.exports = artifact.module.exports;
			break;
		}
		case SectionKind::imported_export_table: {
			if (!propagate(r(bin::field("imported_exports", artifact.module.imported_exports)), "imported_export_table")) return false;
			artifact.imported_exports = artifact.module.imported_exports;
			break;
		}
		case SectionKind::decoration_table: {
			if (!propagate(r(bin::field("decorations", artifact.module.decorations)), "decoration_table")) return false;
			break;
		}
		case SectionKind::struct_table: {
			if (!propagate(r(bin::field("structs", artifact.module.structs)), "struct_table")) return false;
			break;
		}
		case SectionKind::resource_table: {
			u32 count = 0;
			if (!propagate(r(bin::field("count", count)), "resource_table.count")) return false;
			artifact.module.uniforms.reserve(count);
			for (u32 idx = 0; idx < count; ++idx) {
				UniformBinding u;
				u08 access = 0;
				u08 is_anonymous = 0;
				u32 field_count = 0;
				if (!propagate(r(
						bin::field("scope_name", u.scope_name),
						bin::field("name", u.name),
						bin::field("type_id", u.type_id),
						bin::field("access", access),
						bin::field("set", u.set),
						bin::field("member", u.member),
						bin::field("is_anonymous", is_anonymous),
						bin::field("anonymous_block_id", u.anonymous_block_id),
						bin::field("field_count", field_count)),
					"resource_table.uniform")) return false;
				u.access = static_cast<AccessKind>(access);
				u.is_anonymous = is_anonymous != 0;
				for (u32 f = 0; f < field_count; ++f) {
					std::string fname;
					if (!propagate(r(bin::field("field_name", fname)), "resource_table.inline_field")) return false;
					u.inline_fields.push_back(StructField{ .name = std::move(fname) });
				}
				artifact.module.uniforms.push_back(std::move(u));
			}
			break;
		}
		case SectionKind::stage_interface_table: {
			if (!propagate(r(bin::field("interfaces", artifact.module.stage_interfaces)), "stage_interface_table")) return false;
			break;
		}
		case SectionKind::entry_table: {
			if (!propagate(r(bin::field("entries", artifact.entries)), "entry_table")) return false;
			break;
		}
		case SectionKind::debug_table:
			break;
		}
	}

	artifact.structs = artifact.module.structs;
	artifact.uniforms = artifact.module.uniforms;
	artifact.stage_interfaces = artifact.module.stage_interfaces;
	artifact.imports = artifact.module.imports;
	artifact.imported_exports = artifact.module.imported_exports;
	artifact.exports = artifact.module.exports;
	return true;
}

const char* artifact_extension(ArtifactKind kind) {
	switch (kind) {
	case ArtifactKind::object: return ".rtslo";
	case ArtifactKind::module: return ".rtslm";
	case ArtifactKind::library: return ".rtsll";
	case ArtifactKind::program: return ".rtslp";
	}
	return ".rtslbin";
}

const char* debug_artifact_extension() { return ".rtsld"; }

} // namespace rtsl
