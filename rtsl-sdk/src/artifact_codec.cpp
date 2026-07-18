#include "artifact_codec.hpp"

#include "binary.hpp"

#include <exception>
#include <limits>
#include <string>
#include <utility>

namespace rtsl::ir {

template <bin::byte_stream Stream, bin::data<ImageShape> Shape>
bin::error process(Stream& stream, Shape& shape) {
	return stream(
		bin::field("dimension", shape.dimension),
		bin::field("arrayed", shape.arrayed));
}
} // namespace rtsl::ir

namespace rtsl {

template <bin::byte_stream Stream, bin::data<IRInstruction> Inst>
bin::error process(Stream& stream, Inst& instruction) {
	return stream(
		bin::field("op", instruction.op),
		bin::field("result_id", instruction.result_id),
		bin::field("type_id", instruction.type_id),
		bin::field("operands", instruction.operands),
		bin::field("literals", instruction.literals));
}

template <bin::byte_stream Stream, bin::data<DescriptorBinding> Binding>
bin::error process(Stream& stream, Binding& binding) {
	return stream(
		bin::field("set", binding.set),
		bin::field("binding", binding.binding));
}

template <bin::byte_stream Stream, bin::data<Resource> ResourceData>
bin::error process(Stream& stream, ResourceData& resource) {
	return stream(
		bin::field("name", resource.name),
		bin::field("kind", resource.kind),
		bin::field("image", resource.image),
		bin::field("access", resource.access),
		bin::field("descriptor", resource.descriptor),
		bin::field("variable", resource.variable),
		bin::field("value_type", resource.value_type));
}

template <bin::byte_stream Stream, bin::data<InterfaceElement> Element>
bin::error process(Stream& stream, Element& element) {
	return stream(
		bin::field("name", element.name),
		bin::field("type", element.type),
		bin::field("member", element.member),
		bin::field("location", element.location),
		bin::field("builtin", element.builtin),
		bin::field("interpolation", element.interpolation));
}

template <bin::byte_stream Stream, bin::data<Interface> InterfaceData>
bin::error process(Stream& stream, InterfaceData& interface) {
	return stream(
		bin::field("value_type", interface.value_type),
		bin::field("value", interface.value),
		bin::field("elements", interface.elements));
}

template <bin::byte_stream Stream, bin::data<EntryPoint> Entry>
bin::error process(Stream& stream, Entry& entry) {
	return stream(
		bin::field("name", entry.name),
		bin::field("stage", entry.stage),
		bin::field("function", entry.function),
		bin::field("input", entry.input),
		bin::field("output", entry.output));
}

} // namespace rtsl

namespace rtsl {

namespace {

struct ArtifactHeader {
	u32 magic = ArtifactMagic;
	u16 version_major = ArtifactVersionMajor;
	u16 version_minor = ArtifactVersionMinor;
	ArtifactKind kind = ArtifactKind::object;
	u08 endian = 1;
	u08 reserved = 0;
};

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

} // namespace

template <bin::byte_stream Stream, bin::data<IRDecoration> Decoration>
bin::error process(Stream& stream, Decoration& decoration) {
	return stream(
		bin::field("target", decoration.target),
		bin::field("kind", decoration.kind),
		bin::field("member_index", decoration.member_index),
		bin::field("literals", decoration.literals));
}

template <bin::byte_stream Stream, bin::data<ExportSymbol> Symbol>
bin::error process(Stream& stream, Symbol& symbol) {
	return stream(
		bin::field("name", symbol.name),
		bin::field("kind", symbol.kind),
		bin::field("type", symbol.type),
		bin::field("interface_hash", symbol.interface_hash));
}

template <bin::byte_stream Stream, bin::data<StructField> Field>
bin::error process(Stream& stream, Field& field) {
	return stream(
		bin::field("type", field.type),
		bin::field("name", field.name));
}

template <bin::byte_stream Stream, bin::data<ParameterDecl> Parameter>
bin::error process(Stream& stream, Parameter& parameter) {
	return stream(
		bin::field("type", parameter.type),
		bin::field("name", parameter.name),
		bin::field("is_const", parameter.is_const),
		bin::field("is_reference", parameter.is_reference));
}

template <bin::byte_stream Stream, bin::data<StructMemberFunction> Function>
bin::error process(Stream& stream, Function& function) {
	return stream(
		bin::field("name", function.name),
		bin::field("parameters", function.parameters),
		bin::field("return_type", function.return_type));
}

template <bin::byte_stream Stream, bin::data<StructDecl> Declaration>
bin::error process(Stream& stream, Declaration& declaration) {
	return stream(
		bin::field("name", declaration.name),
		bin::field("fields", declaration.fields),
		bin::field("member_functions", declaration.member_functions),
		bin::field("constructor_parameters", declaration.constructor_parameters));
}

template <bin::byte_stream Stream, bin::data<IRFunction> Function>
bin::error process(Stream& stream, Function& function) {
	return stream(
		bin::field("result_id", function.result_id),
		bin::field("return_type_id", function.return_type_id),
		bin::field("kind", function.kind),
		bin::field("display_name", function.display_name),
		bin::field("link_name", function.link_name),
		bin::field("parameter_ids", function.parameter_ids),
		bin::field("body", function.body));
}

template <bin::byte_stream Stream, bin::data<IRCallTarget> Target>
bin::error process(Stream& stream, Target& target) {
	return stream(
		bin::field("display_name", target.display_name),
		bin::field("mangled_name", target.mangled_name));
}

namespace {

LoadError make_error(LoadErrorCode code, std::size_t offset,
	std::string context, std::string message) {
	return LoadError{
		.code = code,
		.byte_offset = offset,
		.context = std::move(context),
		.message = std::move(message),
	};
}

std::vector<IRFunction> serialized_functions(const IRModule& module, bool linked_program) {
	std::vector<IRFunction> functions;
	functions.reserve(module.functions.size());
	for (auto function : module.functions) {
		if (linked_program) function.link_name.clear();
		functions.push_back(std::move(function));
	}
	return functions;
}

template <typename... Fields>
std::expected<void, LoadError> read_fields(bin::read_stream& stream,
	std::string_view context, Fields&&... fields) {
	if (auto error = stream(std::forward<Fields>(fields)...); error) {
		return std::unexpected(make_error(LoadErrorCode::malformed_artifact,
			stream.cursor(), std::string(context), std::move(error.message)));
	}
	return {};
}

} // namespace

namespace codec {

std::expected<std::vector<u08>, LoadError>
encode_artifact(ArtifactKind kind, const IRModule& module) {
	try {
		const bool linked_program = kind == ArtifactKind::program;
		ArtifactHeader header{ .kind = kind };
		auto functions = serialized_functions(module, linked_program);
		auto call_targets = linked_program ? std::vector<IRCallTarget>{} : module.call_targets;

		bin::write_stream stream;
		const auto write = [&](auto&&... fields) -> std::expected<void, LoadError> {
			if (auto error = stream(std::forward<decltype(fields)>(fields)...); error) {
				return std::unexpected(make_error(LoadErrorCode::malformed_artifact,
					stream.size(), "encode", std::move(error.message)));
			}
			return {};
		};

		if (auto result = write(bin::field("header", header)); !result) return std::unexpected(std::move(result.error()));
		std::string source_name = module.source_name;
		if (auto result = write(
				bin::field("source_name", source_name),
				bin::field("next_id", module.next_id),
				bin::field("type_constant_pool", module.type_constant_pool),
				bin::field("global_variables", module.global_variables),
				bin::field("functions", functions),
				bin::field("call_targets", call_targets)); !result) {
			return std::unexpected(std::move(result.error()));
		}

		if (!linked_program) {
			auto imports = module.imports;
			auto exports = module.exports;
			auto imported_exports = module.imported_exports;
			auto structs = module.structs;
			if (auto result = write(
					bin::field("imports", imports),
					bin::field("exports", exports),
					bin::field("imported_exports", imported_exports),
					bin::field("structs", structs)); !result) {
				return std::unexpected(std::move(result.error()));
			}
		}

		auto decorations = module.decorations;
		auto resources = module.resources;
		auto entries = module.entries;
		if (auto result = write(
				bin::field("decorations", decorations),
				bin::field("resources", resources),
				bin::field("entries", entries)); !result) {
			return std::unexpected(std::move(result.error()));
		}
		return stream.take_written();
	} catch (const std::bad_alloc&) {
		return std::unexpected(make_error(LoadErrorCode::allocation_failure, 0,
			"encode", "allocation failed while encoding RTSL artifact"));
	} catch (const std::exception& exception) {
		return std::unexpected(make_error(LoadErrorCode::malformed_artifact, 0,
			"encode", exception.what()));
	}
}

std::expected<Artifact, LoadError> decode_artifact(std::span<const u08> bytes) {
	if (bytes.data() == nullptr && !bytes.empty()) {
		return std::unexpected(make_error(LoadErrorCode::invalid_argument, 0,
			"input", "artifact byte span has a null data pointer"));
	}

	try {
		bin::read_stream stream(bytes);
		ArtifactHeader header;
		if (auto result = read_fields(stream, "header", bin::field("header", header)); !result) {
			return std::unexpected(std::move(result.error()));
		}
		if (header.magic != ArtifactMagic) {
			return std::unexpected(make_error(LoadErrorCode::invalid_magic, 0,
				"header.magic", "invalid RTSL artifact magic"));
		}
		if (header.version_major != ArtifactVersionMajor || header.version_minor != ArtifactVersionMinor) {
			return std::unexpected(make_error(LoadErrorCode::unsupported_version, 4,
				"header.version", "unsupported RTSL artifact version"));
		}
		if (header.kind < ArtifactKind::object || header.kind > ArtifactKind::program) {
			return std::unexpected(make_error(LoadErrorCode::malformed_artifact, 8,
				"header.kind", "invalid RTSL artifact kind"));
		}
		if (header.endian != 1 || header.reserved != 0) {
			return std::unexpected(make_error(LoadErrorCode::malformed_artifact, 10,
				"header", "invalid RTSL artifact header flags"));
		}

		Artifact artifact{ .kind = header.kind };
		artifact.bytes.assign(bytes.begin(), bytes.end());
		if (auto result = read_fields(stream, "program",
				bin::field("source_name", artifact.module.source_name),
				bin::field("next_id", artifact.module.next_id),
				bin::field("type_constant_pool", artifact.module.type_constant_pool),
				bin::field("global_variables", artifact.module.global_variables),
				bin::field("functions", artifact.module.functions),
				bin::field("call_targets", artifact.module.call_targets)); !result) {
			return std::unexpected(std::move(result.error()));
		}

		if (artifact.kind == ArtifactKind::program && !artifact.module.call_targets.empty()) {
			return std::unexpected(make_error(LoadErrorCode::invalid_program,
				stream.cursor(), "program.call_targets", "linked program contains unresolved call targets"));
		}
		if (artifact.kind != ArtifactKind::program) {
			if (auto result = read_fields(stream, "link_metadata",
					bin::field("imports", artifact.module.imports),
					bin::field("exports", artifact.module.exports),
					bin::field("imported_exports", artifact.module.imported_exports),
					bin::field("structs", artifact.module.structs)); !result) {
				return std::unexpected(std::move(result.error()));
			}
		}

		if (auto result = read_fields(stream, "backend_program",
				bin::field("decorations", artifact.module.decorations),
				bin::field("resources", artifact.module.resources),
				bin::field("entries", artifact.module.entries)); !result) {
			return std::unexpected(std::move(result.error()));
		}
		if (!stream.at_end()) {
			return std::unexpected(make_error(LoadErrorCode::malformed_artifact,
				stream.cursor(), "artifact", "artifact contains trailing bytes"));
		}
		return artifact;
	} catch (const std::bad_alloc&) {
		return std::unexpected(make_error(LoadErrorCode::allocation_failure, 0,
			"decode", "allocation failed while decoding RTSL artifact"));
	} catch (const std::exception& exception) {
		return std::unexpected(make_error(LoadErrorCode::malformed_artifact, 0,
			"decode", exception.what()));
	}
}

} // namespace codec
} // namespace rtsl
