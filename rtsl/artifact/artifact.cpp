#include "artifact/artifact.hpp"

#include "frontend/ast.hpp"
#include "sema/stage_rules.hpp"
#include "support/binary.hpp"

#include <cstring>
#include <limits>
#include <span>
#include <unordered_map>

namespace rtsl {

constexpr u32 Magic = RTSL_SDK_ARTIFACT_MAGIC;
constexpr u32 HeaderSize = RTSL_SDK_ARTIFACT_HEADER_SIZE;
constexpr u32 PayloadRecordSize = RTSL_SDK_PAYLOAD_RECORD_SIZE;

enum class PayloadKind : u32 {
	strings = 1,
	ir_module = 2,
	imports = 3,
	exports = 4,
	decorations = 5,
	structs = 6,
	resources = 7,
	stage_interfaces = 8,
	entries = 9,
	debug = 10,
	imported_exports = 11,
};

struct Payload {
	PayloadKind kind;
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

// ---- String pool --------------------------------------------------------

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

// ---- Payload builders ---------------------------------------------------

Payload make_string_payload(const StringPool& pool) {
	Payload payload{ .kind = PayloadKind::strings };
	bin::write_stream stream;
	(void)stream(bin::field("strings", pool.values));
	payload.bytes = stream.take_written();
	return payload;
}

Payload make_ir_module_payload(const IRModule& module, bool linked_program) {
	Payload payload{ .kind = PayloadKind::ir_module };
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

	// Pending FunctionCall target identities. Linked programs carry none.
	const u32 call_target_count = linked_program ? 0 : static_cast<u32>(module.call_targets.size());
	(void)stream(bin::field("call_target_count", call_target_count));
	if (!linked_program) {
		for (const auto& target : module.call_targets) {
			(void)stream(
				bin::field("display_name", target.display_name),
				bin::field("mangled_name", target.mangled_name));
		}
	}

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

	payload.bytes = stream.take_written();
	return payload;
}

Payload make_import_payload(const IRModule& module) {
	Payload payload{ .kind = PayloadKind::imports };
	bin::write_stream stream;
	(void)stream(bin::field("imports", module.imports));
	payload.bytes = stream.take_written();
	return payload;
}

Payload make_export_payload(const IRModule& module) {
	Payload payload{ .kind = PayloadKind::exports };
	bin::write_stream stream;
	(void)stream(bin::field("exports", module.exports));
	payload.bytes = stream.take_written();
	return payload;
}

Payload make_imported_export_payload(const IRModule& module) {
	Payload payload{ .kind = PayloadKind::imported_exports };
	bin::write_stream stream;
	(void)stream(bin::field("imported_exports", module.imported_exports));
	payload.bytes = stream.take_written();
	return payload;
}

Payload make_decoration_payload(const IRModule& module) {
	Payload payload{ .kind = PayloadKind::decorations };
	bin::write_stream stream;
	(void)stream(bin::field("decorations", module.decorations));
	payload.bytes = stream.take_written();
	return payload;
}

Payload make_struct_payload(const IRModule& module) {
	Payload payload{ .kind = PayloadKind::structs };
	bin::write_stream stream;
	(void)stream(bin::field("structs", module.structs));
	payload.bytes = stream.take_written();
	return payload;
}

Payload make_resource_payload(const IRModule& module) {
	Payload payload{ .kind = PayloadKind::resources };
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
	payload.bytes = stream.take_written();
	return payload;
}

Payload make_stage_interface_payload(const IRModule& module) {
	// All stage interfaces are serialized, including `varying`: it is the
	// vertex-output / fragment-input contract a backend needs to emit connected
	// stages (interpolation, locations, member mapping). Host reflection hides
	// varyings on its own side (see the C API), so this does not widen the
	// host-visible surface.
	Payload payload{ .kind = PayloadKind::stage_interfaces };
	bin::write_stream stream;
	std::vector<StageInterface> emit(module.stage_interfaces.begin(), module.stage_interfaces.end());
	(void)stream(bin::field("interfaces", emit));
	payload.bytes = stream.take_written();
	return payload;
}

Payload make_entry_payload(std::span<const Artifact::EntryPoint> entries) {
	Payload payload{ .kind = PayloadKind::entries };
	bin::write_stream stream;
	// Cheap copy so the free-function process() overload's mutable requirement
	// can bind. A dedicated const overload could avoid this but at the cost of
	// duplicating every serializer.
	std::vector<Artifact::EntryPoint> local(entries.begin(), entries.end());
	(void)stream(bin::field("entries", local));
	payload.bytes = stream.take_written();
	return payload;
}

// ---- Container header + payload records ---------------------------------

std::vector<u08> write_container(ArtifactKind kind, std::vector<Payload> payloads) {
	const u32 payload_count = static_cast<u32>(payloads.size());
	const u64 payload_record_offset = HeaderSize;
	u64 data_offset = HeaderSize + static_cast<u64>(PayloadRecordSize) * payload_count;
	std::vector<u64> offsets;
	offsets.reserve(payloads.size());
	for (const auto& payload : payloads) {
		offsets.push_back(data_offset);
		data_offset += payload.bytes.size();
	}

	bin::write_stream stream;
	const u16 kind_val = static_cast<u16>(kind);
	const u08 endian = 1;
	const u08 reserved8 = 0;
	const u32 reserved32 = 0;
	(void)stream(
		bin::field("magic", Magic),
		bin::field("version_major", ArtifactVersionMajor),
		bin::field("version_minor", ArtifactVersionMinor),
		bin::field("kind", kind_val),
		bin::field("endian", endian),
		bin::field("reserved_a", reserved8),
		bin::field("reserved_b", reserved32),
		bin::field("header_size", HeaderSize),
		bin::field("payload_count", payload_count),
		bin::field("payload_record_offset", payload_record_offset),
		bin::field("file_size", data_offset));

	auto out = stream.take_written();
	// Header is fixed-size; pad any trailing bytes so payload records land at
	// HeaderSize regardless of how many fields the header actually spent.
	out.resize(HeaderSize, u08{ 0 });

	// Payload records: 32 bytes each, fixed layout.
	bin::write_stream records;
	for (std::size_t i = 0; i < payloads.size(); ++i) {
		const u32 skind = static_cast<u32>(payloads[i].kind);
		const u32 res32 = 0;
		const u64 offset = offsets[i];
		const u64 size = static_cast<u64>(payloads[i].bytes.size());
		const u32 flag = 1;
		const u32 res_tail = 0;
		(void)records(
			bin::field("kind", skind),
			bin::field("reserved", res32),
			bin::field("offset", offset),
			bin::field("size", size),
			bin::field("flag", flag),
			bin::field("reserved_tail", res_tail));
	}
	auto record_bytes = records.take_written();
	out.insert(out.end(), record_bytes.begin(), record_bytes.end());

	for (const auto& payload : payloads)
		out.insert(out.end(), payload.bytes.begin(), payload.bytes.end());
	return out;
}

void report_read_error(DiagnosticEngine* diagnostics, std::string message) {
	if (diagnostics)
		diagnostics->report(DiagnosticCode::artifact_error, DiagnosticSeverity::error, {}, "<artifact>", message);
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

std::vector<u08> write_artifact(ArtifactKind kind, const IRModule& module) {
	const auto entries = default_entries_from_module(module);
	// A linked program will never re-link: dropping import/export/type-decl
	// payloads leaves just the reflection surface the runtime actually reads.
	const bool linked_program = kind == ArtifactKind::program;
	const auto strings = build_string_pool(module, entries, linked_program);
	const IRModule serialized_module = with_serialized_string_ids(module, strings);
	std::vector<Payload> payloads;
	payloads.push_back(make_string_payload(strings));
	payloads.push_back(make_ir_module_payload(serialized_module, linked_program));
	if (!linked_program) {
		payloads.push_back(make_import_payload(serialized_module));
		payloads.push_back(make_export_payload(serialized_module));
		payloads.push_back(make_imported_export_payload(serialized_module));
		payloads.push_back(make_struct_payload(serialized_module));
	}
	payloads.push_back(make_decoration_payload(serialized_module));
	payloads.push_back(make_resource_payload(serialized_module));
	payloads.push_back(make_stage_interface_payload(serialized_module));
	payloads.push_back(make_entry_payload(entries));
	return write_container(kind, std::move(payloads));
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
	rtsl_sdk_artifact_view view{};
	const rtsl_sdk_result header = rtslSdkReadArtifactHeader(data.data(), data.size(), &view);
	if (!header.ok) {
		report_read_error(diagnostics, header.error ? header.error : "invalid RTSL artifact header");
		return false;
	}
	const auto payload_count = static_cast<u32>(view.header.payload_count);
	const auto payload_record_offset = static_cast<u64>(view.header.payload_record_offset);
	artifact = Artifact{};
	artifact.kind = static_cast<ArtifactKind>(view.header.kind);
	artifact.bytes.assign(data.begin(), data.end());

	for (u32 i = 0; i < payload_count; ++i) {
		const auto entry_offset = static_cast<std::size_t>(payload_record_offset + i * PayloadRecordSize);
		if (entry_offset + PayloadRecordSize > data.size()) {
			report_read_error(diagnostics, "payload record out of bounds");
			return false;
		}
		bin::read_stream entry(data.subspan(entry_offset, PayloadRecordSize));
		u32 payload_kind_raw = 0;
		u32 e_reserved = 0;
		u64 payload_offset = 0;
		u64 payload_size = 0;
		u32 e_flag = 0;
		u32 e_reserved_tail = 0;
		if (auto err = entry(
				bin::field("kind", payload_kind_raw),
				bin::field("reserved", e_reserved),
				bin::field("offset", payload_offset),
				bin::field("size", payload_size),
				bin::field("flag", e_flag),
				bin::field("reserved_tail", e_reserved_tail));
			err) {
			report_read_error(diagnostics, "payload record: " + err.message);
			return false;
		}
		if (payload_offset + payload_size > data.size()) {
			report_read_error(diagnostics, "payload is out of bounds");
			return false;
		}
		const auto payload_kind = static_cast<PayloadKind>(payload_kind_raw);
		auto payload_bytes = data.subspan(static_cast<std::size_t>(payload_offset), static_cast<std::size_t>(payload_size));
		bin::read_stream r(payload_bytes);
		auto propagate = [&](bin::error err, const char* what) -> bool {
			if (!err) return true;
			report_read_error(diagnostics, std::string(what) + ": " + err.message);
			return false;
		};

		switch (payload_kind) {
		case PayloadKind::strings: {
			if (!propagate(r(bin::field("strings", artifact.strings)), "strings")) return false;
			if (!artifact.strings.empty())
				artifact.module.source_name = artifact.strings.front();
			break;
		}
		case PayloadKind::ir_module: {
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
				if (fn.display_name.value < artifact.strings.size()) {
					fn.display_name_text = artifact.strings[fn.display_name.value];
				}
				artifact.module.functions.push_back(std::move(fn));
			}
			u32 call_target_count = 0;
			if (!propagate(r(bin::field("call_target_count", call_target_count)),
				"ir_module.call_target_count")) return false;
			artifact.module.call_targets.reserve(call_target_count);
			for (u32 t = 0; t < call_target_count; ++t) {
				IRCallTarget target;
				if (!propagate(r(
						bin::field("display_name", target.display_name),
						bin::field("mangled_name", target.mangled_name)),
					"ir_module.call_target")) return false;
				artifact.module.call_targets.push_back(std::move(target));
			}
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
		case PayloadKind::imports: {
			if (!propagate(r(bin::field("imports", artifact.module.imports)), "imports")) return false;
			artifact.imports = artifact.module.imports;
			break;
		}
		case PayloadKind::exports: {
			if (!propagate(r(bin::field("exports", artifact.module.exports)), "exports")) return false;
			artifact.exports = artifact.module.exports;
			break;
		}
		case PayloadKind::imported_exports: {
			if (!propagate(r(bin::field("imported_exports", artifact.module.imported_exports)), "imported_exports")) return false;
			artifact.imported_exports = artifact.module.imported_exports;
			break;
		}
		case PayloadKind::decorations: {
			if (!propagate(r(bin::field("decorations", artifact.module.decorations)), "decorations")) return false;
			break;
		}
		case PayloadKind::structs: {
			if (!propagate(r(bin::field("structs", artifact.module.structs)), "structs")) return false;
			break;
		}
		case PayloadKind::resources: {
			u32 count = 0;
			if (!propagate(r(bin::field("count", count)), "resources.count")) return false;
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
					"resources.uniform")) return false;
				u.access = static_cast<AccessKind>(access);
				u.is_anonymous = is_anonymous != 0;
				for (u32 f = 0; f < field_count; ++f) {
					std::string fname;
					if (!propagate(r(bin::field("field_name", fname)), "resources.inline_field")) return false;
					u.inline_fields.push_back(StructField{ .name = std::move(fname) });
				}
				artifact.module.uniforms.push_back(std::move(u));
			}
			break;
		}
		case PayloadKind::stage_interfaces: {
			if (!propagate(r(bin::field("interfaces", artifact.module.stage_interfaces)), "stage_interfaces")) return false;
			break;
		}
		case PayloadKind::entries: {
			if (!propagate(r(bin::field("entries", artifact.entries)), "entries")) return false;
			break;
		}
		case PayloadKind::debug:
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
