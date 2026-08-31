#include <rtsl/Serialization/Artifact.hpp>

#include <rtsl/IR/Verifier.hpp>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <new>
#include <type_traits>
#include <utility>

namespace rtsl {

constexpr std::array<std::byte, 8> magic{
	std::byte{'R'}, std::byte{'T'}, std::byte{'S'}, std::byte{'L'},
	std::byte{'R'}, std::byte{'T'}, std::byte{'I'}, std::byte{'R'},
};
constexpr std::uint32_t endian_marker = 0x01020304;
constexpr std::uint32_t maximum_record_count = 1u << 24;
constexpr std::size_t header_size = 24;
constexpr std::size_t directory_entry_size = 24;

enum class SectionKind : std::uint32_t {
	section_strings = 1,
	section_module = 2,
	section_types = 3,
	section_symbols = 4,
	section_functions = 5,
	section_entries = 6,
	section_resources = 7,
	section_uniforms = 8,
	section_storage = 9,
};

struct EncodedSection {
	SectionKind kind{SectionKind::section_strings};
	std::vector<std::byte> bytes;
};

struct DirectoryEntry {
	SectionKind kind{SectionKind::section_strings};
	std::uint64_t offset{};
	std::uint64_t size{};
};

class Writer {
public:
	void writeU8(std::uint8_t value) { bytes.push_back(static_cast<std::byte>(value)); }
	void writeU16(std::uint16_t value) {
		writeU8(static_cast<std::uint8_t>(value));
		writeU8(static_cast<std::uint8_t>(value >> 8));
	}
	void writeU32(std::uint32_t value) {
		for (unsigned shift = 0; shift != 32; shift += 8) writeU8(static_cast<std::uint8_t>(value >> shift));
	}
	void writeU64(std::uint64_t value) {
		for (unsigned shift = 0; shift != 64; shift += 8) writeU8(static_cast<std::uint8_t>(value >> shift));
	}
	void writeBool(bool value) { writeU8(value ? 1 : 0); }
	template <typename Enum>
	void writeEnum(Enum value) { writeU32(static_cast<std::uint32_t>(value)); }
	template <typename Tag>
	void writeId(ir::Id<Tag> value) { writeU32(value.value()); }
	void writeBytes(std::span<const std::byte> value) { bytes.insert(bytes.end(), value.begin(), value.end()); }
	template <typename Value, typename Write>
	void writeVector(const std::vector<Value>& values, Write write) {
		writeU32(static_cast<std::uint32_t>(values.size()));
		for (const Value& value : values) write(*this, value);
	}
	template <typename Value, typename Write>
	void writeOptional(const std::optional<Value>& value, Write write) {
		writeBool(value.has_value());
		if (value) write(*this, *value);
	}

	std::vector<std::byte> bytes;
};

class Reader {
public:
	Reader(std::span<const std::byte> bytes, std::size_t base, Error& error,
		std::uint16_t version_minor = artifact_version_minor)
		: bytes(bytes), base(base), error(error), version_minor(version_minor) {}

	[[nodiscard]] bool failed() const noexcept { return has_error; }
	[[nodiscard]] std::size_t remaining() const noexcept { return position <= bytes.size() ? bytes.size() - position : 0; }
	[[nodiscard]] std::size_t absoluteOffset() const noexcept { return base + position; }
	[[nodiscard]] std::uint16_t versionMinor() const noexcept { return version_minor; }
	void fail(ErrorCode code, std::string context, std::string message) {
		if (has_error) return;
		has_error = true;
		error = Error{.code = code, .offset = absoluteOffset(), .context = std::move(context), .message = std::move(message)};
	}
	bool readU8(std::uint8_t& value) {
		if (remaining() < 1) { fail(ErrorCode::error_truncated, "integer", "unexpected end of artifact"); return false; }
		value = std::to_integer<std::uint8_t>(bytes[position++]);
		return true;
	}
	bool readU16(std::uint16_t& value) {
		std::uint8_t first{}, second{};
		if (!readU8(first) || !readU8(second)) return false;
		value = static_cast<std::uint16_t>(first | static_cast<std::uint16_t>(second) << 8);
		return true;
	}
	bool readU32(std::uint32_t& value) {
		value = 0;
		for (unsigned shift = 0; shift != 32; shift += 8) { std::uint8_t part{}; if (!readU8(part)) return false; value |= static_cast<std::uint32_t>(part) << shift; }
		return true;
	}
	bool readU64(std::uint64_t& value) {
		value = 0;
		for (unsigned shift = 0; shift != 64; shift += 8) { std::uint8_t part{}; if (!readU8(part)) return false; value |= static_cast<std::uint64_t>(part) << shift; }
		return true;
	}
	bool readBool(bool& value) {
		std::uint8_t raw{};
		if (!readU8(raw)) return false;
		if (raw > 1) { fail(ErrorCode::error_invalid_enum, "boolean", "boolean encoding is not zero or one"); return false; }
		value = raw != 0;
		return true;
	}
	template <typename Enum>
	bool readEnum(Enum& value, Enum maximum, std::string_view context) {
		std::uint32_t raw{};
		if (!readU32(raw)) return false;
		if (raw > static_cast<std::uint32_t>(maximum)) { fail(ErrorCode::error_invalid_enum, std::string(context), "enum value is outside the supported range"); return false; }
		value = static_cast<Enum>(raw);
		return true;
	}
	template <typename Tag>
	bool readId(ir::Id<Tag>& value) { std::uint32_t raw{}; if (!readU32(raw)) return false; value = ir::Id<Tag>{raw}; return true; }
	bool readCount(std::uint32_t& count, std::string_view context) {
		if (!readU32(count)) return false;
		if (count > maximum_record_count) { fail(ErrorCode::error_invalid_count, std::string(context), "record count exceeds the artifact limit"); return false; }
		return true;
	}
	template <typename Value, typename Read>
	bool readVector(std::vector<Value>& values, std::string_view context, Read read) {
		std::uint32_t count{};
		if (!readCount(count, context)) return false;
		values.clear(); values.reserve(count);
		for (std::uint32_t index = 0; index < count; ++index) { Value value{}; if (!read(*this, value)) return false; values.push_back(std::move(value)); }
		return true;
	}
	template <typename Value, typename Read>
	bool readOptional(std::optional<Value>& value, Read read) {
		bool present{};
		if (!readBool(present)) return false;
		if (!present) { value.reset(); return true; }
		Value result{};
		if (!read(*this, result)) return false;
		value = std::move(result);
		return true;
	}
	bool readSpan(std::size_t size, std::span<const std::byte>& value) {
		if (size > remaining()) { fail(ErrorCode::error_truncated, "bytes", "byte range exceeds its section"); return false; }
		value = bytes.subspan(position, size); position += size; return true;
	}
	bool finish(std::string_view context) {
		if (failed()) return false;
		if (remaining() != 0) { fail(ErrorCode::error_invalid_count, std::string(context), "section contains trailing bytes"); return false; }
		return true;
	}

private:
	std::span<const std::byte> bytes;
	std::size_t base{};
	std::size_t position{};
	Error& error;
	std::uint16_t version_minor{};
	bool has_error{};
};

void writeBinding(Writer& writer, const ir::Binding& binding) { writer.writeU32(binding.set); writer.writeU32(binding.binding); }
bool readBinding(Reader& reader, ir::Binding& binding) { return reader.readU32(binding.set) && reader.readU32(binding.binding); }
void writeU32Value(Writer& writer, std::uint32_t value) { writer.writeU32(value); }
bool readU32Value(Reader& reader, std::uint32_t& value) { return reader.readU32(value); }
void writeTypeId(Writer& writer, ir::TypeId value) { writer.writeId(value); }
bool readTypeId(Reader& reader, ir::TypeId& value) { return reader.readId(value); }
void writeValueId(Writer& writer, ir::ValueId value) { writer.writeId(value); }
bool readValueId(Reader& reader, ir::ValueId& value) { return reader.readId(value); }
void writeFunctionId(Writer& writer, ir::FunctionId value) { writer.writeId(value); }
bool readFunctionId(Reader& reader, ir::FunctionId& value) { return reader.readId(value); }
void writeStringId(Writer& writer, ir::StringId value) { writer.writeId(value); }
bool readStringId(Reader& reader, ir::StringId& value) { return reader.readId(value); }

EncodedSection writeStrings(const ir::Module& module) {
	Writer writer;
	writer.writeU32(static_cast<std::uint32_t>(module.strings.size() - 1));
	for (std::uint32_t index = 1; index < module.strings.size(); ++index) {
		const std::string_view spelling = module.strings.get(ir::StringId{index});
		writer.writeU32(static_cast<std::uint32_t>(spelling.size()));
		if (!spelling.empty()) {
			writer.writeBytes(std::as_bytes(std::span{spelling.data(), spelling.size()}));
		}
	}
	return {SectionKind::section_strings, std::move(writer.bytes)};
}

EncodedSection writeModule(const ir::Module& module) { Writer writer; writer.writeId(module.name); return {SectionKind::section_module, std::move(writer.bytes)}; }

EncodedSection writeTypes(const ir::Module& module) {
	Writer writer;
	writer.writeU32(static_cast<std::uint32_t>(module.types.size()));
	for (const ir::Type& type : module.types) {
		writer.writeId(type.id); writer.writeEnum(type.kind); writer.writeU32(type.bit_width); writer.writeId(type.element_type); writer.writeU32(type.element_count); writer.writeEnum(type.address_space);
		writer.writeVector(type.parameter_types, writeTypeId);
		writer.writeU32(static_cast<std::uint32_t>(type.members.size()));
		for (const ir::StructMember& member : type.members) {
			writer.writeId(member.name); writer.writeId(member.type);
			writer.writeOptional(member.offset, writeU32Value); writer.writeOptional(member.alignment, writeU32Value);
		}
		writer.writeId(type.name);
	}
	return {SectionKind::section_types, std::move(writer.bytes)};
}

EncodedSection writeSymbols(const ir::Module& module) {
	Writer writer; writer.writeU32(static_cast<std::uint32_t>(module.symbols.size()));
	for (const ir::Symbol& symbol : module.symbols) { writer.writeId(symbol.id); writer.writeId(symbol.fully_qualified_name); writer.writeBool(symbol.exported); }
	return {SectionKind::section_symbols, std::move(writer.bytes)};
}

void writeTerminator(Writer& writer, const ir::Terminator& terminator) {
	writer.writeEnum(terminator.kind); writer.writeVector(terminator.operands, writeValueId);
	writer.writeU32(static_cast<std::uint32_t>(terminator.successors.size()));
	for (const ir::Successor& successor : terminator.successors) { writer.writeId(successor.block); writer.writeVector(successor.arguments, writeValueId); }
	writer.writeVector(terminator.immediates, writeU32Value);
}

EncodedSection writeFunctions(const ir::Module& module) {
	Writer writer; writer.writeU32(static_cast<std::uint32_t>(module.functions.size()));
	for (const ir::Function& function : module.functions) {
		writer.writeId(function.id); writer.writeId(function.symbol); writer.writeId(function.return_type); writer.writeBool(function.declaration); writer.writeBool(function.implicit_emitter);
		writer.writeU32(static_cast<std::uint32_t>(function.parameters.size()));
		for (const ir::Parameter& parameter : function.parameters) { writer.writeId(parameter.value); writer.writeId(parameter.type); writer.writeId(parameter.symbol); }
		writer.writeU32(static_cast<std::uint32_t>(function.blocks.size()));
		for (const ir::Block& block : function.blocks) {
			writer.writeId(block.id);
			writer.writeU32(static_cast<std::uint32_t>(block.arguments.size()));
			for (const ir::BlockArgument& argument : block.arguments) { writer.writeId(argument.value); writer.writeId(argument.type); }
			writer.writeU32(static_cast<std::uint32_t>(block.instructions.size()));
			for (const ir::Instruction& instruction : block.instructions) {
				writer.writeEnum(instruction.opcode); writer.writeId(instruction.result); writer.writeId(instruction.type); writer.writeId(instruction.callee);
				writer.writeVector(instruction.operands, writeValueId); writer.writeVector(instruction.immediates, writeU32Value);
			}
			writer.writeOptional(block.terminator, writeTerminator);
			writer.writeEnum(block.merge.kind); writer.writeId(block.merge.merge_block); writer.writeId(block.merge.continue_block);
		}
	}
	return {SectionKind::section_functions, std::move(writer.bytes)};
}

EncodedSection writeEntries(const ir::Module& module) {
	Writer writer; writer.writeU32(static_cast<std::uint32_t>(module.entry_points.size()));
	for (const ir::EntryPoint& entry : module.entry_points) {
		writer.writeId(entry.symbol); writer.writeId(entry.function); writer.writeId(entry.source_name); writer.writeEnum(entry.stage); writer.writeU8(static_cast<std::uint8_t>(entry.configuration.index()));
		if (const auto* value = std::get_if<ir::TessellationControlConfiguration>(&entry.configuration)) writer.writeU32(value->output_control_points);
		else if (const auto* value = std::get_if<ir::TessellationEvaluationConfiguration>(&entry.configuration)) { writer.writeEnum(value->domain); writer.writeEnum(value->spacing); writer.writeEnum(value->winding); }
		else if (const auto* value = std::get_if<ir::GeometryConfiguration>(&entry.configuration)) { writer.writeEnum(value->input); writer.writeEnum(value->output); writer.writeU32(value->maximum_vertices); writer.writeU32(value->invocations); }
		else if (const auto* value = std::get_if<ir::ComputeConfiguration>(&entry.configuration)) for (std::uint32_t size : value->workgroup_size) writer.writeU32(size);
		writer.writeU32(static_cast<std::uint32_t>(entry.attributes.size()));
		for (const ir::EntryAttribute& attribute : entry.attributes) { writer.writeId(attribute.name); writer.writeVector(attribute.tokens, writeStringId); }
		writer.writeU32(static_cast<std::uint32_t>(entry.parameter_contracts.size()));
		for (const ir::InterfaceContract& contract : entry.parameter_contracts) {
			writer.writeU32(contract.parameter_index);
			writer.writeVector(contract.member_path, writeStringId);
			writer.writeId(contract.contract);
		}
	}
	return {SectionKind::section_entries, std::move(writer.bytes)};
}

EncodedSection writeResources(const ir::Module& module) {
	Writer writer; writer.writeU32(static_cast<std::uint32_t>(module.resources.size()));
	for (const ir::Resource& resource : module.resources) { writer.writeId(resource.symbol); writer.writeEnum(resource.kind); writer.writeId(resource.type); writer.writeEnum(resource.access); writer.writeOptional(resource.binding, writeBinding); writer.writeVector(resource.users, writeFunctionId); }
	return {SectionKind::section_resources, std::move(writer.bytes)};
}

EncodedSection writeUniforms(const ir::Module& module) {
	Writer writer; writer.writeU32(static_cast<std::uint32_t>(module.uniforms.size()));
	for (const ir::Uniform& uniform : module.uniforms) { writer.writeId(uniform.symbol); writer.writeId(uniform.type); writer.writeOptional(uniform.binding, writeBinding); writer.writeOptional(uniform.offset, writeU32Value); writer.writeOptional(uniform.size, writeU32Value); writer.writeOptional(uniform.alignment, writeU32Value); }
	return {SectionKind::section_uniforms, std::move(writer.bytes)};
}

EncodedSection writeStorage(const ir::Module& module) {
	Writer writer; writer.writeU32(static_cast<std::uint32_t>(module.storage_objects.size()));
	for (const ir::StorageObject& object : module.storage_objects) { writer.writeId(object.symbol); writer.writeId(object.type); writer.writeEnum(object.address_space); writer.writeEnum(object.access); writer.writeOptional(object.binding, writeBinding); writer.writeOptional(object.initializer, writeValueId); }
	return {SectionKind::section_storage, std::move(writer.bytes)};
}

std::vector<EncodedSection> writeSections(const ir::Module& module) {
	std::vector<EncodedSection> sections;
	sections.push_back(writeStrings(module)); sections.push_back(writeModule(module)); sections.push_back(writeTypes(module)); sections.push_back(writeSymbols(module)); sections.push_back(writeFunctions(module)); sections.push_back(writeEntries(module)); sections.push_back(writeResources(module)); sections.push_back(writeUniforms(module)); sections.push_back(writeStorage(module));
	return sections;
}

bool readStrings(Reader& reader, ir::Module& module) {
	std::uint32_t count{}; if (!reader.readCount(count, "strings")) return false;
	for (std::uint32_t index = 1; index <= count; ++index) {
		std::uint32_t size{}; if (!reader.readCount(size, "string bytes")) return false;
		std::span<const std::byte> bytes; if (!reader.readSpan(size, bytes)) return false;
		const std::string_view spelling = bytes.empty() ? std::string_view{} : std::string_view{reinterpret_cast<const char*>(bytes.data()), bytes.size()};
		if (module.strings.intern(spelling) != ir::StringId{index}) { reader.fail(ErrorCode::error_invalid_reference, "strings", "string table contains a duplicate or unrepresentable record"); return false; }
	}
	return reader.finish("strings");
}

bool readModule(Reader& reader, ir::Module& module) { return reader.readId(module.name) && reader.finish("module"); }

bool readTypes(Reader& reader, ir::Module& module) {
	std::uint32_t count{}; if (!reader.readCount(count, "types")) return false; module.types.reserve(count);
	for (std::uint32_t index = 0; index < count; ++index) {
		ir::Type type;
		if (!reader.readId(type.id) || !reader.readEnum(type.kind, ir::TypeKind::type_primitive, "type.kind") || !reader.readU32(type.bit_width) || !reader.readId(type.element_type) || !reader.readU32(type.element_count) || !reader.readEnum(type.address_space, ir::AddressSpace::address_space_resource, "type.address_space") || !reader.readVector(type.parameter_types, "type.parameters", readTypeId)) return false;
		std::uint32_t members{}; if (!reader.readCount(members, "type.members")) return false; type.members.reserve(members);
		for (std::uint32_t member_index = 0; member_index < members; ++member_index) { ir::StructMember member; if (!reader.readId(member.name) || !reader.readId(member.type) || !reader.readOptional(member.offset, readU32Value) || !reader.readOptional(member.alignment, readU32Value)) return false; type.members.push_back(std::move(member)); }
		if (!reader.readId(type.name)) return false; module.types.push_back(std::move(type));
	}
	return reader.finish("types");
}

bool readSymbols(Reader& reader, ir::Module& module) {
	std::uint32_t count{}; if (!reader.readCount(count, "symbols")) return false; module.symbols.reserve(count);
	for (std::uint32_t index = 0; index < count; ++index) {
		ir::Symbol symbol;
		if (!reader.readId(symbol.id) || !reader.readId(symbol.fully_qualified_name) ||
			(reader.versionMinor() >= 3 && !reader.readBool(symbol.exported))) return false;
		module.symbols.push_back(symbol);
	}
	return reader.finish("symbols");
}

bool readTerminator(Reader& reader, ir::Terminator& terminator) {
	if (!reader.readEnum(terminator.kind, ir::TerminatorKind::terminator_unreachable, "terminator.kind") || !reader.readVector(terminator.operands, "terminator.operands", readValueId)) return false;
	std::uint32_t count{}; if (!reader.readCount(count, "terminator.successors")) return false; terminator.successors.reserve(count);
	for (std::uint32_t index = 0; index < count; ++index) { ir::Successor successor; if (!reader.readId(successor.block) || !reader.readVector(successor.arguments, "successor.arguments", readValueId)) return false; terminator.successors.push_back(std::move(successor)); }
	return reader.readVector(terminator.immediates, "terminator.immediates", readU32Value);
}

bool readFunctions(Reader& reader, ir::Module& module) {
	std::uint32_t count{}; if (!reader.readCount(count, "functions")) return false; module.functions.reserve(count);
	for (std::uint32_t index = 0; index < count; ++index) {
		ir::Function function;
		if (!reader.readId(function.id) || !reader.readId(function.symbol) || !reader.readId(function.return_type) ||
			!reader.readBool(function.declaration) ||
			(reader.versionMinor() >= 2 && !reader.readBool(function.implicit_emitter))) return false;
		std::uint32_t parameters{}; if (!reader.readCount(parameters, "function.parameters")) return false; function.parameters.reserve(parameters);
		for (std::uint32_t parameter_index = 0; parameter_index < parameters; ++parameter_index) { ir::Parameter parameter; if (!reader.readId(parameter.value) || !reader.readId(parameter.type) || !reader.readId(parameter.symbol)) return false; function.parameters.push_back(parameter); }
		std::uint32_t blocks{}; if (!reader.readCount(blocks, "function.blocks")) return false; function.blocks.reserve(blocks);
		for (std::uint32_t block_index = 0; block_index < blocks; ++block_index) {
			ir::Block block; if (!reader.readId(block.id)) return false;
			std::uint32_t arguments{}; if (!reader.readCount(arguments, "block.arguments")) return false; block.arguments.reserve(arguments);
			for (std::uint32_t argument_index = 0; argument_index < arguments; ++argument_index) { ir::BlockArgument argument; if (!reader.readId(argument.value) || !reader.readId(argument.type)) return false; block.arguments.push_back(argument); }
			std::uint32_t instructions{}; if (!reader.readCount(instructions, "block.instructions")) return false; block.instructions.reserve(instructions);
			for (std::uint32_t instruction_index = 0; instruction_index < instructions; ++instruction_index) { ir::Instruction instruction; if (!reader.readEnum(instruction.opcode, ir::Opcode::opcode_discard, "instruction.opcode") || !reader.readId(instruction.result) || !reader.readId(instruction.type) || !reader.readId(instruction.callee) || !reader.readVector(instruction.operands, "instruction.operands", readValueId) || !reader.readVector(instruction.immediates, "instruction.immediates", readU32Value)) return false; block.instructions.push_back(std::move(instruction)); }
			if (!reader.readOptional(block.terminator, readTerminator) || !reader.readEnum(block.merge.kind, ir::MergeKind::merge_loop, "block.merge") || !reader.readId(block.merge.merge_block) || !reader.readId(block.merge.continue_block)) return false;
			function.blocks.push_back(std::move(block));
		}
		module.functions.push_back(std::move(function));
	}
	return reader.finish("functions");
}

bool readEntries(Reader& reader, ir::Module& module) {
	std::uint32_t count{}; if (!reader.readCount(count, "entries")) return false; module.entry_points.reserve(count);
	for (std::uint32_t index = 0; index < count; ++index) {
		ir::EntryPoint entry; std::uint8_t configuration{};
		if (!reader.readId(entry.symbol) || !reader.readId(entry.function) || !reader.readId(entry.source_name) || !reader.readEnum(entry.stage, ir::Stage::stage_compute, "entry.stage") || !reader.readU8(configuration)) return false;
		switch (configuration) {
		case 0: entry.configuration = std::monostate{}; break;
		case 1: { ir::TessellationControlConfiguration value; if (!reader.readU32(value.output_control_points)) return false; entry.configuration = value; break; }
		case 2: { ir::TessellationEvaluationConfiguration value; if (!reader.readEnum(value.domain, ir::TessellationDomain::tessellation_domain_isolines, "entry.tessellation.domain") || !reader.readEnum(value.spacing, ir::TessellationSpacing::tessellation_spacing_fractional_odd, "entry.tessellation.spacing") || !reader.readEnum(value.winding, ir::Winding::winding_counter_clockwise, "entry.tessellation.winding")) return false; entry.configuration = value; break; }
		case 3: { ir::GeometryConfiguration value; if (!reader.readEnum(value.input, ir::PrimitiveTopology::primitive_triangle_strip, "entry.geometry.input") || !reader.readEnum(value.output, ir::PrimitiveTopology::primitive_triangle_strip, "entry.geometry.output") || !reader.readU32(value.maximum_vertices) || !reader.readU32(value.invocations)) return false; entry.configuration = value; break; }
		case 4: { ir::ComputeConfiguration value; for (std::uint32_t& size : value.workgroup_size) if (!reader.readU32(size)) return false; entry.configuration = value; break; }
		default: reader.fail(ErrorCode::error_invalid_enum, "entry.configuration", "unknown stage configuration variant"); return false;
		}
		if (reader.versionMinor() >= 4) {
			std::uint32_t attributes{};
			if (!reader.readCount(attributes, "entry.attributes")) return false;
			entry.attributes.reserve(attributes);
			for (std::uint32_t attribute_index = 0; attribute_index < attributes; ++attribute_index) {
				ir::EntryAttribute attribute;
				if (!reader.readId(attribute.name) || !reader.readVector(attribute.tokens, "entry.attribute.tokens", readStringId)) return false;
				entry.attributes.push_back(std::move(attribute));
			}
		}
		std::uint32_t contracts{};
		if (!reader.readCount(contracts, "entry.parameter_contracts")) return false;
		entry.parameter_contracts.reserve(contracts);
		for (std::uint32_t contract_index = 0; contract_index < contracts; ++contract_index) {
			ir::InterfaceContract contract;
			if (!reader.readU32(contract.parameter_index) ||
				!reader.readVector(contract.member_path, "entry.parameter_contract.path", readStringId) ||
				!reader.readId(contract.contract)) return false;
			entry.parameter_contracts.push_back(contract);
		}
		module.entry_points.push_back(std::move(entry));
	}
	return reader.finish("entries");
}

bool readResources(Reader& reader, ir::Module& module) {
	std::uint32_t count{}; if (!reader.readCount(count, "resources")) return false; module.resources.reserve(count);
	for (std::uint32_t index = 0; index < count; ++index) { ir::Resource value; if (!reader.readId(value.symbol) || !reader.readEnum(value.kind, ir::ResourceKind::resource_input_attachment, "resource.kind") || !reader.readId(value.type) || !reader.readEnum(value.access, ir::Access::access_read_write, "resource.access") || !reader.readOptional(value.binding, readBinding) || !reader.readVector(value.users, "resource.users", readFunctionId)) return false; module.resources.push_back(std::move(value)); }
	return reader.finish("resources");
}

bool readUniforms(Reader& reader, ir::Module& module) {
	std::uint32_t count{}; if (!reader.readCount(count, "uniforms")) return false; module.uniforms.reserve(count);
	for (std::uint32_t index = 0; index < count; ++index) { ir::Uniform value; if (!reader.readId(value.symbol) || !reader.readId(value.type) || !reader.readOptional(value.binding, readBinding) || !reader.readOptional(value.offset, readU32Value) || !reader.readOptional(value.size, readU32Value) || !reader.readOptional(value.alignment, readU32Value)) return false; module.uniforms.push_back(std::move(value)); }
	return reader.finish("uniforms");
}

bool readStorage(Reader& reader, ir::Module& module) {
	std::uint32_t count{}; if (!reader.readCount(count, "storage")) return false; module.storage_objects.reserve(count);
	for (std::uint32_t index = 0; index < count; ++index) { ir::StorageObject value; if (!reader.readId(value.symbol) || !reader.readId(value.type) || !reader.readEnum(value.address_space, ir::AddressSpace::address_space_resource, "storage.address_space") || !reader.readEnum(value.access, ir::Access::access_read_write, "storage.access") || !reader.readOptional(value.binding, readBinding) || !reader.readOptional(value.initializer, readValueId)) return false; module.storage_objects.push_back(std::move(value)); }
	return reader.finish("storage");
}

const DirectoryEntry* findSection(std::span<const DirectoryEntry> entries, SectionKind kind) {
	const auto found = std::ranges::find(entries, kind, &DirectoryEntry::kind);
	return found == entries.end() ? nullptr : &*found;
}

bool validStringId(const ir::Module& module, ir::StringId id) {
	return !id || id.value() < module.strings.size();
}

bool validateStringReferences(const ir::Module& module, Error& error) {
	const auto invalid = [&](std::string context) { error = Error{.code = ErrorCode::error_invalid_reference, .context = std::move(context), .message = "string id is outside the string table"}; return false; };
	if (!validStringId(module, module.name)) return invalid("module.name");
	for (const ir::Type& type : module.types) { if (!validStringId(module, type.name)) return invalid("type.name"); for (const ir::StructMember& member : type.members) if (!validStringId(module, member.name)) return invalid("type.member.name"); }
	for (const ir::Symbol& symbol : module.symbols) if (!validStringId(module, symbol.fully_qualified_name)) return invalid("symbol.name");
	for (const ir::EntryPoint& entry : module.entry_points) {
		if (!validStringId(module, entry.source_name)) return invalid("entry.source_name");
		for (const ir::EntryAttribute& attribute : entry.attributes) {
			if (!validStringId(module, attribute.name)) return invalid("entry.attribute.name");
			for (ir::StringId token : attribute.tokens) if (!validStringId(module, token)) return invalid("entry.attribute.token");
		}
		for (const ir::InterfaceContract& contract : entry.parameter_contracts) {
			for (ir::StringId member : contract.member_path)
				if (!validStringId(module, member)) return invalid("entry.parameter_contract.path");
			if (!validStringId(module, contract.contract)) return invalid("entry.parameter_contract");
		}
	}
	return true;
}

WriteResult ArtifactWriter::write(const Artifact& artifact) const {
	try {
		const auto count_fits = [](std::size_t count) { return count <= maximum_record_count; };
		bool counts_fit = count_fits(artifact.module.strings.size() - 1) && count_fits(artifact.module.types.size()) && count_fits(artifact.module.symbols.size()) && count_fits(artifact.module.functions.size()) && count_fits(artifact.module.entry_points.size()) && count_fits(artifact.module.resources.size()) && count_fits(artifact.module.uniforms.size()) && count_fits(artifact.module.storage_objects.size());
		for (std::uint32_t index = 1; counts_fit && index < artifact.module.strings.size(); ++index) counts_fit = count_fits(artifact.module.strings.get(ir::StringId{index}).size());
		for (const ir::Type& type : artifact.module.types) counts_fit = counts_fit && count_fits(type.parameter_types.size()) && count_fits(type.members.size());
		for (const ir::Function& function : artifact.module.functions) {
			counts_fit = counts_fit && count_fits(function.parameters.size()) && count_fits(function.blocks.size());
			for (const ir::Block& block : function.blocks) {
				counts_fit = counts_fit && count_fits(block.arguments.size()) && count_fits(block.instructions.size());
				for (const ir::Instruction& instruction : block.instructions) counts_fit = counts_fit && count_fits(instruction.operands.size()) && count_fits(instruction.immediates.size());
				if (block.terminator) {
					counts_fit = counts_fit && count_fits(block.terminator->operands.size()) && count_fits(block.terminator->successors.size()) && count_fits(block.terminator->immediates.size());
					for (const ir::Successor& successor : block.terminator->successors) counts_fit = counts_fit && count_fits(successor.arguments.size());
				}
			}
		}
		for (const ir::EntryPoint& entry : artifact.module.entry_points) {
			counts_fit = counts_fit && count_fits(entry.attributes.size()) && count_fits(entry.parameter_contracts.size());
			for (const ir::EntryAttribute& attribute : entry.attributes) counts_fit = counts_fit && count_fits(attribute.tokens.size());
			for (const ir::InterfaceContract& contract : entry.parameter_contracts)
				counts_fit = counts_fit && count_fits(contract.member_path.size());
		}
		for (const ir::Resource& resource : artifact.module.resources) counts_fit = counts_fit && count_fits(resource.users.size());
		if (!counts_fit) return {.error = Error{.code = ErrorCode::error_invalid_count, .context = "write", .message = "an RTIR record count exceeds the artifact limit"}};
		const ir::VerificationResult verification = ir::verify(artifact.module);
		if (!verification.valid()) return {.error = Error{.code = ErrorCode::error_invalid_ir, .context = verification.issues().front().context, .message = verification.issues().front().message}};
		Error string_error;
		if (!validateStringReferences(artifact.module, string_error)) return {.error = std::move(string_error)};
		std::vector<EncodedSection> sections = writeSections(artifact.module);
		Writer writer; writer.writeBytes(magic); writer.writeU16(artifact_version_major); writer.writeU16(artifact_version_minor); writer.writeU32(endian_marker); writer.writeU8(static_cast<std::uint8_t>(artifact.kind)); writer.writeU8(0); writer.writeU8(0); writer.writeU8(0); writer.writeU32(static_cast<std::uint32_t>(sections.size()));
		std::uint64_t offset = header_size + directory_entry_size * sections.size();
		for (const EncodedSection& section : sections) { writer.writeEnum(section.kind); writer.writeU32(0); writer.writeU64(offset); writer.writeU64(section.bytes.size()); offset += section.bytes.size(); }
		for (const EncodedSection& section : sections) writer.writeBytes(section.bytes);
		return {.bytes = std::move(writer.bytes)};
	} catch (const std::bad_alloc&) {
		return {.error = Error{.code = ErrorCode::error_allocation_failure, .context = "write", .message = "allocation failed while serializing RTIR"}};
	}
}

ReadResult ArtifactReader::read(std::span<const std::byte> bytes) const {
	try {
		Error error; Reader reader(bytes, 0, error);
		std::span<const std::byte> encoded_magic; if (!reader.readSpan(magic.size(), encoded_magic)) return {.error = std::move(error)};
		if (!std::ranges::equal(encoded_magic, magic)) return {.error = Error{.code = ErrorCode::error_invalid_magic, .context = "header.magic", .message = "artifact magic does not identify RTIR"}};
		std::uint16_t major{}, minor{}; std::uint32_t endian{}; std::uint8_t kind{}, reserved_0{}, reserved_1{}, reserved_2{}; std::uint32_t section_count{};
		if (!reader.readU16(major) || !reader.readU16(minor) || !reader.readU32(endian) || !reader.readU8(kind) || !reader.readU8(reserved_0) || !reader.readU8(reserved_1) || !reader.readU8(reserved_2) || !reader.readCount(section_count, "directory")) return {.error = std::move(error)};
		if (major != artifact_version_major || minor > artifact_version_minor) return {.error = Error{.code = ErrorCode::error_unsupported_version, .offset = 8, .context = "header.version", .message = "artifact version is not supported"}};
		if (endian != endian_marker) return {.error = Error{.code = ErrorCode::error_wrong_endian_marker, .offset = 12, .context = "header.endian", .message = "artifact endian marker is invalid"}};
		if (kind > static_cast<std::uint8_t>(ArtifactKind::artifact_program)) return {.error = Error{.code = ErrorCode::error_invalid_enum, .offset = 16, .context = "header.kind", .message = "artifact kind is invalid"}};
		if (reserved_0 != 0 || reserved_1 != 0 || reserved_2 != 0) return {.error = Error{.code = ErrorCode::error_invalid_directory, .offset = 17, .context = "header.reserved", .message = "reserved header bytes must be zero"}};
		if (section_count != 9 || header_size + static_cast<std::uint64_t>(section_count) * directory_entry_size > bytes.size()) return {.error = Error{.code = ErrorCode::error_invalid_directory, .offset = 20, .context = "directory", .message = "artifact must contain the complete RTIR section set"}};
		std::vector<DirectoryEntry> entries; entries.reserve(section_count);
		for (std::uint32_t index = 0; index < section_count; ++index) { std::uint32_t raw_kind{}, reserved{}; DirectoryEntry entry; if (!reader.readU32(raw_kind) || !reader.readU32(reserved) || !reader.readU64(entry.offset) || !reader.readU64(entry.size)) return {.error = std::move(error)}; if (raw_kind < 1 || raw_kind > 9 || reserved != 0) return {.error = Error{.code = ErrorCode::error_invalid_directory, .offset = reader.absoluteOffset() - directory_entry_size, .context = "directory.kind", .message = "section kind or reserved field is invalid"}}; entry.kind = static_cast<SectionKind>(raw_kind); if (findSection(entries, entry.kind)) return {.error = Error{.code = ErrorCode::error_duplicate_section, .context = "directory", .message = "section kind appears more than once"}}; if (entry.offset < header_size + section_count * directory_entry_size || entry.offset > bytes.size() || entry.size > bytes.size() - entry.offset) return {.error = Error{.code = ErrorCode::error_invalid_directory, .context = "directory.range", .message = "section range lies outside the artifact"}}; entries.push_back(entry); }
		std::vector<DirectoryEntry> ordered = entries; std::ranges::sort(ordered, {}, &DirectoryEntry::offset);
		for (std::size_t index = 1; index < ordered.size(); ++index) if (ordered[index - 1].offset + ordered[index - 1].size > ordered[index].offset) return {.error = Error{.code = ErrorCode::error_invalid_directory, .context = "directory.overlap", .message = "section ranges overlap"}};
		Artifact artifact; artifact.kind = static_cast<ArtifactKind>(kind);
		const auto read_section = [&](SectionKind section_kind, auto decode) { const DirectoryEntry* entry = findSection(entries, section_kind); if (!entry) { error = Error{.code = ErrorCode::error_missing_section, .context = "directory", .message = "required section is missing"}; return false; } Reader section_reader(bytes.subspan(static_cast<std::size_t>(entry->offset), static_cast<std::size_t>(entry->size)), static_cast<std::size_t>(entry->offset), error, minor); return decode(section_reader, artifact.module); };
		if (!read_section(SectionKind::section_strings, readStrings) || !read_section(SectionKind::section_module, readModule) || !read_section(SectionKind::section_types, readTypes) || !read_section(SectionKind::section_symbols, readSymbols) || !read_section(SectionKind::section_functions, readFunctions) || !read_section(SectionKind::section_entries, readEntries) || !read_section(SectionKind::section_resources, readResources) || !read_section(SectionKind::section_uniforms, readUniforms) || !read_section(SectionKind::section_storage, readStorage)) return {.error = std::move(error)};
		if (!validateStringReferences(artifact.module, error)) return {.error = std::move(error)};
		const ir::VerificationResult verification = ir::verify(artifact.module);
		if (!verification.valid()) return {.error = Error{.code = ErrorCode::error_invalid_ir, .context = verification.issues().front().context, .message = verification.issues().front().message}};
		return {.artifact = std::move(artifact)};
	} catch (const std::bad_alloc&) {
		return {.error = Error{.code = ErrorCode::error_allocation_failure, .context = "read", .message = "allocation failed while reading RTIR"}};
	}
}

} // namespace rtsl
