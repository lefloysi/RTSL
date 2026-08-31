#ifndef RTSL_FRONTEND_MODULE_INTERFACE_HPP
#define RTSL_FRONTEND_MODULE_INTERFACE_HPP

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace rtsl {

enum class InterfaceImportKind : std::uint8_t {
	import_file,
	import_library,
};

struct InterfaceImport {
	InterfaceImportKind kind{InterfaceImportKind::import_file};
	std::string name;
};

struct InterfaceAttributeToken {
	std::uint16_t kind{};
	std::string spelling;
};

struct InterfaceAttribute {
	std::string name;
	std::vector<InterfaceAttributeToken> tokens;
};

enum class InterfaceTypeKind : std::uint8_t {
	type_void,
	type_bool,
	type_i32,
	type_u32,
	type_usize,
	type_f32,
	type_named,
	type_template_parameter,
	type_template_specialization,
	type_pointer,
	type_reference,
};

struct InterfaceType {
	InterfaceTypeKind kind{InterfaceTypeKind::type_void};
	bool constant{};
	std::string name;
	std::vector<InterfaceType> arguments;
	std::optional<std::uint32_t> integer_value;
};

struct InterfaceField {
	std::string name;
	std::vector<InterfaceAttribute> attributes;
	InterfaceType type;
};

struct InterfaceRecord {
	std::string name;
	std::vector<InterfaceAttribute> attributes;
	std::optional<InterfaceType> base_type;
	std::vector<InterfaceField> fields;
};

struct InterfaceTypeAlias {
	std::string name;
	std::vector<InterfaceAttribute> attributes;
	InterfaceType type;
};

enum class InterfaceStorageClass : std::uint8_t {
	storage_ordinary,
	storage_uniform,
	storage_storage,
};

struct InterfaceVariable {
	std::string name;
	std::vector<InterfaceAttribute> attributes;
	InterfaceType type;
	InterfaceStorageClass storage{InterfaceStorageClass::storage_ordinary};
	bool constant{};
};

struct InterfaceParameter {
	std::string name;
	std::vector<InterfaceAttribute> attributes;
	InterfaceType type;
};

struct InterfaceFunction {
	std::string name;
	std::vector<InterfaceAttribute> attributes;
	InterfaceType return_type;
	std::vector<InterfaceParameter> parameters;
	std::vector<InterfaceType> type_only_parameters;
	std::vector<std::string> template_parameters;
	bool implicit_emitter{};
	bool declaration{};
	// A generic definition is serialized in the interface because importers, not
	// the producing translation unit, instantiate it. Non-template definitions
	// are omitted from an interface.
	std::vector<std::byte> generic_definition;
};

using InterfaceDeclaration = std::variant<InterfaceRecord, InterfaceTypeAlias, InterfaceVariable, InterfaceFunction>;

struct InterfaceUnit {
	// This is the exact source-level name accepted by import "...". It never
	// contains an artifact filename inferred by the frontend.
	std::string import_path;
	std::vector<InterfaceImport> imports;
	std::vector<InterfaceDeclaration> declarations;
};

struct ModuleInterface {
	// Present only when this is the build-provided aggregate selected by
	// import <name>. A one-translation-unit interface leaves this empty.
	std::optional<std::string> library_name;
	std::vector<InterfaceUnit> units;
};

enum class ModuleInterfaceErrorCode : std::uint8_t {
	error_invalid_magic,
	error_unsupported_version,
	error_truncated,
	error_invalid_count,
	error_invalid_enum,
	error_trailing_data,
};

struct ModuleInterfaceError {
	ModuleInterfaceErrorCode code{ModuleInterfaceErrorCode::error_invalid_magic};
	std::string message;
};

struct ModuleInterfaceWriteResult {
	std::vector<std::byte> bytes;
	std::optional<ModuleInterfaceError> error;
	[[nodiscard]] explicit operator bool() const noexcept { return !error.has_value(); }
};

struct ModuleInterfaceReadResult {
	std::optional<ModuleInterface> interface;
	std::optional<ModuleInterfaceError> error;
	[[nodiscard]] explicit operator bool() const noexcept { return interface.has_value(); }
};

class ModuleInterfaceWriter {
public:
	[[nodiscard]] ModuleInterfaceWriteResult write(const ModuleInterface& interface) const;
};

class ModuleInterfaceReader {
public:
	[[nodiscard]] ModuleInterfaceReadResult read(std::span<const std::byte> bytes) const;
};

} // namespace rtsl

#endif
