#include <rtsl/Frontend/ModuleInterface.hpp>

#include <array>
#include <limits>
#include <span>
#include <string_view>

namespace rtsl {
namespace {

constexpr std::array<std::byte, 8> interface_magic{
	std::byte{'R'}, std::byte{'T'}, std::byte{'S'}, std::byte{'L'},
	std::byte{'M'}, std::byte{'O'}, std::byte{'D'}, std::byte{0},
};
constexpr std::uint16_t interface_version = 3;
constexpr std::uint32_t maximum_count = 1u << 24;

class Writer {
public:
	void writeU8(std::uint8_t value) { bytes.push_back(static_cast<std::byte>(value)); }
	void writeU16(std::uint16_t value) { writeU8(static_cast<std::uint8_t>(value)); writeU8(static_cast<std::uint8_t>(value >> 8)); }
	void writeU32(std::uint32_t value) { for (unsigned shift = 0; shift != 32; shift += 8) writeU8(static_cast<std::uint8_t>(value >> shift)); }
	void writeBool(bool value) { writeU8(value ? 1 : 0); }
	void writeString(std::string_view value) {
		writeU32(static_cast<std::uint32_t>(value.size()));
		bytes.insert(bytes.end(), reinterpret_cast<const std::byte*>(value.data()), reinterpret_cast<const std::byte*>(value.data() + value.size()));
	}
	void writeBytes(const std::vector<std::byte>& value) { writeU32(static_cast<std::uint32_t>(value.size())); bytes.insert(bytes.end(), value.begin(), value.end()); }
	[[nodiscard]] bool canRepresent(std::size_t count) const { return count <= maximum_count; }

	std::vector<std::byte> bytes;
};

class Reader {
public:
	explicit Reader(std::span<const std::byte> value) : bytes(value) {}
	[[nodiscard]] bool failed() const { return error.has_value(); }
	void fail(ModuleInterfaceErrorCode code, std::string message) {
		if (!error) error = ModuleInterfaceError{.code = code, .message = std::move(message)};
	}
	bool readU8(std::uint8_t& value) {
		if (position == bytes.size()) { fail(ModuleInterfaceErrorCode::error_truncated, "unexpected end of module interface"); return false; }
		value = std::to_integer<std::uint8_t>(bytes[position++]); return true;
	}
	bool readU16(std::uint16_t& value) { std::uint8_t first{}, second{}; if (!readU8(first) || !readU8(second)) return false; value = static_cast<std::uint16_t>(first | static_cast<std::uint16_t>(second) << 8); return true; }
	bool readU32(std::uint32_t& value) { value = 0; for (unsigned shift = 0; shift != 32; shift += 8) { std::uint8_t part{}; if (!readU8(part)) return false; value |= static_cast<std::uint32_t>(part) << shift; } return true; }
	bool readBool(bool& value) { std::uint8_t raw{}; if (!readU8(raw)) return false; if (raw > 1) { fail(ModuleInterfaceErrorCode::error_invalid_enum, "boolean encoding is not zero or one"); return false; } value = raw != 0; return true; }
	bool readCount(std::uint32_t& value, std::string_view context) { if (!readU32(value)) return false; if (value > maximum_count) { fail(ModuleInterfaceErrorCode::error_invalid_count, std::string(context) + " exceeds the interface limit"); return false; } return true; }
	bool readString(std::string& value) {
		std::uint32_t size{}; if (!readCount(size, "string length")) return false;
		if (size > bytes.size() - position) { fail(ModuleInterfaceErrorCode::error_truncated, "string exceeds module interface"); return false; }
		value.assign(reinterpret_cast<const char*>(bytes.data() + position), size); position += size; return true;
	}
	bool readBytes(std::vector<std::byte>& value) {
		std::uint32_t size{}; if (!readCount(size, "byte sequence length")) return false;
		if (size > bytes.size() - position) { fail(ModuleInterfaceErrorCode::error_truncated, "byte sequence exceeds module interface"); return false; }
		value.assign(bytes.begin() + static_cast<std::ptrdiff_t>(position), bytes.begin() + static_cast<std::ptrdiff_t>(position + size)); position += size; return true;
	}
	[[nodiscard]] bool finished() const { return position == bytes.size(); }
	[[nodiscard]] const std::optional<ModuleInterfaceError>& getError() const { return error; }

private:
	std::span<const std::byte> bytes;
	std::size_t position{};
	std::optional<ModuleInterfaceError> error;
};

bool writeType(Writer& writer, const InterfaceType& type) {
	if (!writer.canRepresent(type.name.size()) || !writer.canRepresent(type.arguments.size())) return false;
	writer.writeU8(static_cast<std::uint8_t>(type.kind)); writer.writeBool(type.constant); writer.writeString(type.name);
	writer.writeBool(type.integer_value.has_value()); if (type.integer_value) writer.writeU32(*type.integer_value);
	writer.writeU32(static_cast<std::uint32_t>(type.arguments.size()));
	for (const InterfaceType& argument : type.arguments) if (!writeType(writer, argument)) return false;
	return true;
}

bool readType(Reader& reader, InterfaceType& type, std::uint16_t version) {
	std::uint8_t kind{}; if (!reader.readU8(kind) || kind > static_cast<std::uint8_t>(InterfaceTypeKind::type_reference)) { if (!reader.failed()) reader.fail(ModuleInterfaceErrorCode::error_invalid_enum, "unknown interface type kind"); return false; }
	type.kind = static_cast<InterfaceTypeKind>(kind);
	if (!reader.readBool(type.constant) || !reader.readString(type.name)) return false;
	if (version >= 3) { bool has_integer{}; if (!reader.readBool(has_integer)) return false; if (has_integer) { std::uint32_t value{}; if (!reader.readU32(value)) return false; type.integer_value = value; } else type.integer_value.reset(); }
	else type.integer_value.reset();
	std::uint32_t count{}; if (!reader.readCount(count, "type argument count")) return false;
	type.arguments.clear(); type.arguments.resize(count);
	for (InterfaceType& argument : type.arguments) if (!readType(reader, argument, version)) return false;
	return true;
}

bool writeAttributes(Writer& writer, const std::vector<InterfaceAttribute>& attributes) {
	if (!writer.canRepresent(attributes.size())) return false;
	writer.writeU32(static_cast<std::uint32_t>(attributes.size()));
	for (const InterfaceAttribute& attribute : attributes) {
		if (!writer.canRepresent(attribute.name.size()) || !writer.canRepresent(attribute.tokens.size())) return false;
		writer.writeString(attribute.name); writer.writeU32(static_cast<std::uint32_t>(attribute.tokens.size()));
		for (const InterfaceAttributeToken& token : attribute.tokens) {
			if (!writer.canRepresent(token.spelling.size())) return false;
			writer.writeU16(token.kind); writer.writeString(token.spelling);
		}
	}
	return true;
}

bool readAttributes(Reader& reader, std::vector<InterfaceAttribute>& attributes) {
	std::uint32_t count{}; if (!reader.readCount(count, "attribute count")) return false;
	attributes.clear(); attributes.resize(count);
	for (InterfaceAttribute& attribute : attributes) {
		if (!reader.readString(attribute.name)) return false;
		std::uint32_t tokens{}; if (!reader.readCount(tokens, "attribute token count")) return false;
		attribute.tokens.resize(tokens);
		for (InterfaceAttributeToken& token : attribute.tokens) {
			if (!reader.readU16(token.kind) || !reader.readString(token.spelling)) return false;
		}
	}
	return true;
}

bool writeDeclaration(Writer& writer, const InterfaceDeclaration& declaration) {
	writer.writeU8(static_cast<std::uint8_t>(declaration.index()));
	return std::visit([&](const auto& value) -> bool {
		using Value = std::decay_t<decltype(value)>;
		if (!writer.canRepresent(value.name.size())) return false;
		writer.writeString(value.name);
		if constexpr (std::is_same_v<Value, InterfaceRecord>) {
			if (!writeAttributes(writer, value.attributes)) return false;
			writer.writeBool(value.base_type.has_value()); if (value.base_type && !writeType(writer, *value.base_type)) return false;
			if (!writer.canRepresent(value.fields.size())) return false; writer.writeU32(static_cast<std::uint32_t>(value.fields.size()));
			for (const InterfaceField& field : value.fields) { if (!writer.canRepresent(field.name.size())) return false; writer.writeString(field.name); if (!writeAttributes(writer, field.attributes) || !writeType(writer, field.type)) return false; }
		} else if constexpr (std::is_same_v<Value, InterfaceTypeAlias>) {
			if (!writeAttributes(writer, value.attributes) || !writeType(writer, value.type)) return false;
		} else if constexpr (std::is_same_v<Value, InterfaceVariable>) {
			if (!writeAttributes(writer, value.attributes) || !writeType(writer, value.type)) return false; writer.writeU8(static_cast<std::uint8_t>(value.storage)); writer.writeBool(value.constant);
		} else {
			if (!writeAttributes(writer, value.attributes) || !writeType(writer, value.return_type) || !writer.canRepresent(value.parameters.size()) || !writer.canRepresent(value.type_only_parameters.size()) || !writer.canRepresent(value.template_parameters.size()) || !writer.canRepresent(value.generic_definition.size())) return false;
			writer.writeU32(static_cast<std::uint32_t>(value.parameters.size()));
			for (const InterfaceParameter& parameter : value.parameters) { if (!writer.canRepresent(parameter.name.size())) return false; writer.writeString(parameter.name); if (!writeAttributes(writer, parameter.attributes) || !writeType(writer, parameter.type)) return false; }
			writer.writeU32(static_cast<std::uint32_t>(value.type_only_parameters.size()));
			for (const InterfaceType& parameter : value.type_only_parameters) if (!writeType(writer, parameter)) return false;
			writer.writeU32(static_cast<std::uint32_t>(value.template_parameters.size())); for (const std::string& parameter : value.template_parameters) { if (!writer.canRepresent(parameter.size())) return false; writer.writeString(parameter); }
			writer.writeBool(value.implicit_emitter); writer.writeBool(value.declaration); writer.writeBytes(value.generic_definition);
		}
		return true;
	}, declaration);
}

bool readDeclaration(Reader& reader, InterfaceDeclaration& declaration, std::uint16_t version) {
	std::uint8_t kind{}; if (!reader.readU8(kind) || kind > 3) { if (!reader.failed()) reader.fail(ModuleInterfaceErrorCode::error_invalid_enum, "unknown interface declaration kind"); return false; }
	if (kind == 0) {
		InterfaceRecord value; bool has_base{}; if (!reader.readString(value.name) || !readAttributes(reader, value.attributes) || !reader.readBool(has_base)) return false; if (has_base) { value.base_type.emplace(); if (!readType(reader, *value.base_type, version)) return false; }
		std::uint32_t fields{}; if (!reader.readCount(fields, "record field count")) return false; value.fields.resize(fields); for (InterfaceField& field : value.fields) if (!reader.readString(field.name) || !readAttributes(reader, field.attributes) || !readType(reader, field.type, version)) return false; declaration = std::move(value);
	} else if (kind == 1) {
		InterfaceTypeAlias value; if (!reader.readString(value.name) || !readAttributes(reader, value.attributes) || !readType(reader, value.type, version)) return false; declaration = std::move(value);
	} else if (kind == 2) {
		InterfaceVariable value; std::uint8_t storage{}; if (!reader.readString(value.name) || !readAttributes(reader, value.attributes) || !readType(reader, value.type, version) || !reader.readU8(storage) || storage > static_cast<std::uint8_t>(InterfaceStorageClass::storage_storage) || !reader.readBool(value.constant)) { if (!reader.failed()) reader.fail(ModuleInterfaceErrorCode::error_invalid_enum, "unknown interface storage class"); return false; } value.storage = static_cast<InterfaceStorageClass>(storage); declaration = std::move(value);
	} else {
		InterfaceFunction value; if (!reader.readString(value.name) || !readAttributes(reader, value.attributes) || !readType(reader, value.return_type, version)) return false;
		std::uint32_t parameters{}; if (!reader.readCount(parameters, "function parameter count")) return false; value.parameters.resize(parameters); for (InterfaceParameter& parameter : value.parameters) if (!reader.readString(parameter.name) || !readAttributes(reader, parameter.attributes) || !readType(reader, parameter.type, version)) return false;
		if (version >= 2) { std::uint32_t type_only_parameters{}; if (!reader.readCount(type_only_parameters, "function type-only parameter count")) return false; value.type_only_parameters.resize(type_only_parameters); for (InterfaceType& parameter : value.type_only_parameters) if (!readType(reader, parameter, version)) return false; }
		std::uint32_t templates{}; if (!reader.readCount(templates, "function template parameter count")) return false; value.template_parameters.resize(templates); for (std::string& parameter : value.template_parameters) if (!reader.readString(parameter)) return false;
		if (!reader.readBool(value.implicit_emitter) || !reader.readBool(value.declaration) || !reader.readBytes(value.generic_definition)) return false; declaration = std::move(value);
	}
	return true;
}

} // namespace

ModuleInterfaceWriteResult ModuleInterfaceWriter::write(const ModuleInterface& interface) const {
	Writer writer;
	if (!writer.canRepresent(interface.units.size()) || (interface.library_name && !writer.canRepresent(interface.library_name->size()))) return {.error = ModuleInterfaceError{.code = ModuleInterfaceErrorCode::error_invalid_count, .message = "module interface exceeds the serialization limit"}};
	writer.bytes.insert(writer.bytes.end(), interface_magic.begin(), interface_magic.end()); writer.writeU16(interface_version); writer.writeBool(interface.library_name.has_value()); if (interface.library_name) writer.writeString(*interface.library_name);
	writer.writeU32(static_cast<std::uint32_t>(interface.units.size()));
	for (const InterfaceUnit& unit : interface.units) {
		if (!writer.canRepresent(unit.import_path.size()) || !writer.canRepresent(unit.imports.size()) || !writer.canRepresent(unit.declarations.size())) return {.error = ModuleInterfaceError{.code = ModuleInterfaceErrorCode::error_invalid_count, .message = "module interface unit exceeds the serialization limit"}};
		writer.writeString(unit.import_path); writer.writeU32(static_cast<std::uint32_t>(unit.imports.size()));
		for (const InterfaceImport& import : unit.imports) { if (!writer.canRepresent(import.name.size())) return {.error = ModuleInterfaceError{.code = ModuleInterfaceErrorCode::error_invalid_count, .message = "module import exceeds the serialization limit"}}; writer.writeU8(static_cast<std::uint8_t>(import.kind)); writer.writeString(import.name); }
		writer.writeU32(static_cast<std::uint32_t>(unit.declarations.size())); for (const InterfaceDeclaration& declaration : unit.declarations) if (!writeDeclaration(writer, declaration)) return {.error = ModuleInterfaceError{.code = ModuleInterfaceErrorCode::error_invalid_count, .message = "interface declaration exceeds the serialization limit"}};
	}
	return {.bytes = std::move(writer.bytes)};
}

ModuleInterfaceReadResult ModuleInterfaceReader::read(std::span<const std::byte> bytes) const {
	Reader reader(bytes);
	for (std::byte expected : interface_magic) { std::uint8_t value{}; if (!reader.readU8(value)) return {.error = reader.getError()}; if (value != std::to_integer<std::uint8_t>(expected)) return {.error = ModuleInterfaceError{.code = ModuleInterfaceErrorCode::error_invalid_magic, .message = "invalid module interface magic"}}; }
	std::uint16_t version{}; if (!reader.readU16(version)) return {.error = reader.getError()}; if (version == 0 || version > interface_version) return {.error = ModuleInterfaceError{.code = ModuleInterfaceErrorCode::error_unsupported_version, .message = "unsupported module interface version"}};
	ModuleInterface result; bool has_library{}; if (!reader.readBool(has_library)) return {.error = reader.getError()}; if (has_library) { result.library_name.emplace(); if (!reader.readString(*result.library_name)) return {.error = reader.getError()}; }
	std::uint32_t units{}; if (!reader.readCount(units, "interface unit count")) return {.error = reader.getError()}; result.units.resize(units);
	for (InterfaceUnit& unit : result.units) {
		if (!reader.readString(unit.import_path)) return {.error = reader.getError()}; std::uint32_t imports{}; if (!reader.readCount(imports, "import count")) return {.error = reader.getError()}; unit.imports.resize(imports);
		for (InterfaceImport& import : unit.imports) { std::uint8_t kind{}; if (!reader.readU8(kind) || kind > static_cast<std::uint8_t>(InterfaceImportKind::import_library) || !reader.readString(import.name)) { if (!reader.failed()) reader.fail(ModuleInterfaceErrorCode::error_invalid_enum, "unknown import kind"); return {.error = reader.getError()}; } import.kind = static_cast<InterfaceImportKind>(kind); }
		std::uint32_t declarations{}; if (!reader.readCount(declarations, "declaration count")) return {.error = reader.getError()}; unit.declarations.resize(declarations); for (InterfaceDeclaration& declaration : unit.declarations) if (!readDeclaration(reader, declaration, version)) return {.error = reader.getError()};
	}
	if (!reader.finished()) return {.error = ModuleInterfaceError{.code = ModuleInterfaceErrorCode::error_trailing_data, .message = "module interface contains trailing data"}};
	return {.interface = std::move(result)};
}

} // namespace rtsl
