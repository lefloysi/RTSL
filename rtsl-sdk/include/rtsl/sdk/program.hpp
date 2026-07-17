#pragma once

#include "rtsl/sdk/ir.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace rtsl {

inline constexpr std::uint16_t artifact_version_major = 1;
inline constexpr std::uint16_t artifact_version_minor = 0;

enum class Stage : std::uint8_t {
	vertex,
	fragment,
};

enum class StageMask : std::uint8_t {
	none = 0,
	vertex = 1 << 0,
	fragment = 1 << 1,
};

[[nodiscard]] constexpr StageMask operator|(StageMask lhs, StageMask rhs) {
	return static_cast<StageMask>(static_cast<std::uint8_t>(lhs) | static_cast<std::uint8_t>(rhs));
}

constexpr StageMask& operator|=(StageMask& lhs, StageMask rhs) {
	lhs = lhs | rhs;
	return lhs;
}

[[nodiscard]] constexpr bool contains(StageMask mask, Stage stage) {
	const StageMask bit = stage == Stage::vertex ? StageMask::vertex : StageMask::fragment;
	return (static_cast<std::uint8_t>(mask) & static_cast<std::uint8_t>(bit)) != 0;
}

enum class ResourceKind : std::uint8_t {
	uniform_buffer,
	storage_buffer,
	sampler,
	sampled_texture,
	storage_image,
};

enum class Access : std::uint8_t {
	read_write,
	read_only,
	write_only,
};

using ImageDimension = ir::ImageDimension;
using ImageShape = ir::ImageShape;

struct DescriptorBinding {
	std::uint32_t set = 0;
	std::uint32_t binding = 0;
};

struct Resource {
	std::string name;
	ResourceKind kind = ResourceKind::uniform_buffer;
	ImageShape image{};
	Access access = Access::read_write;
	DescriptorBinding descriptor{};
	ir::Id variable{};
	ir::Id value_type{};
	StageMask stages = StageMask::none;
};

enum class Builtin : std::uint8_t {
	none,
	position,
};

enum class Interpolation : std::uint8_t {
	default_,
	smooth,
	flat,
};

struct InterfaceElement {
	std::string name;
	ir::Id type{};
	std::optional<std::uint32_t> member;
	std::optional<std::uint32_t> location;
	Builtin builtin = Builtin::none;
	Interpolation interpolation = Interpolation::default_;
};

struct Interface {
	ir::Id value_type{};
	std::optional<ir::Id> value;
	std::vector<InterfaceElement> elements;
};

struct EntryPoint {
	std::string name;
	Stage stage = Stage::vertex;
	ir::Id function{};
	std::optional<Interface> input;
	std::optional<Interface> output;
};

enum class LoadErrorCode : std::uint8_t {
	invalid_argument,
	invalid_magic,
	unsupported_version,
	wrong_artifact_kind,
	malformed_artifact,
	invalid_program,
	allocation_failure,
};

struct LoadError {
	LoadErrorCode code = LoadErrorCode::malformed_artifact;
	std::size_t byte_offset = 0;
	std::string context;
	std::string message;
};

struct ProgramBytes {
	const std::uint8_t* data = nullptr;
	std::size_t size = 0;

	[[nodiscard]] std::span<const std::byte> view() const noexcept {
		return { reinterpret_cast<const std::byte*>(data), size };
	}
};

class Program {
  public:
	Program(Program&&) noexcept;
	Program& operator=(Program&&) noexcept;
	Program(const Program&) = delete;
	Program& operator=(const Program&) = delete;
	~Program();

	[[nodiscard]] std::span<const ir::Type> types() const noexcept;
	[[nodiscard]] std::span<const ir::Constant> constants() const noexcept;
	[[nodiscard]] std::span<const ir::Global> globals() const noexcept;
	[[nodiscard]] std::span<const ir::Function> functions() const noexcept;
	[[nodiscard]] std::span<const ir::Decoration> decorations() const noexcept;
	[[nodiscard]] std::span<const ir::Decoration> decorations(ir::Id target) const noexcept;
	[[nodiscard]] std::span<const Resource> resources() const noexcept;
	[[nodiscard]] std::span<const EntryPoint> entries() const noexcept;
	[[nodiscard]] std::uint32_t id_bound() const noexcept;

	[[nodiscard]] const ir::Type* find_type(ir::Id id) const noexcept;
	[[nodiscard]] const ir::Constant* find_constant(ir::Id id) const noexcept;
	[[nodiscard]] const ir::Global* find_global(ir::Id id) const noexcept;
	[[nodiscard]] const ir::Function* find_function(ir::Id id) const noexcept;
	[[nodiscard]] const EntryPoint* entry(Stage stage) const noexcept;

  private:
	struct Data;
	explicit Program(std::unique_ptr<Data> data);
	std::unique_ptr<Data> data_;

	friend std::expected<Program, LoadError> load_program(std::span<const std::byte> bytes);
};

[[nodiscard]] std::expected<Program, LoadError> load_program(std::span<const std::byte> bytes);

[[nodiscard]] inline std::expected<Program, LoadError> load_program(ProgramBytes bytes) {
	return load_program(bytes.view());
}

} // namespace rtsl
