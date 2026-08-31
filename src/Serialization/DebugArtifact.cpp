#include <rtsl/Serialization/DebugArtifact.hpp>

#include <array>
#include <limits>
#include <new>
#include <string_view>
#include <utility>

namespace rtsl::debug {
namespace {
constexpr std::array<std::byte, 8> magic{std::byte{'R'}, std::byte{'T'}, std::byte{'S'}, std::byte{'L'}, std::byte{'D'}, std::byte{'B'}, std::byte{'G'}, std::byte{0}};
constexpr std::uint32_t max_records = 1u << 24;
constexpr std::uint32_t max_string_bytes = 1u << 28;

class Writer {
public:
	void u8(std::uint8_t v) { bytes.push_back(static_cast<std::byte>(v)); }
	void u16(std::uint16_t v) { u8(static_cast<std::uint8_t>(v)); u8(static_cast<std::uint8_t>(v >> 8)); }
	void u32(std::uint32_t v) { for (unsigned s = 0; s < 32; s += 8) u8(static_cast<std::uint8_t>(v >> s)); }
	void string(const std::string& v) { u32(static_cast<std::uint32_t>(v.size())); for (unsigned char c : v) u8(c); }
	template <typename T, typename F> void vector(const std::vector<T>& values, F write) { u32(static_cast<std::uint32_t>(values.size())); for (const T& value : values) write(value); }
	std::vector<std::byte> bytes;
};

class Reader {
public:
	explicit Reader(std::span<const std::byte> data, Error& out) : data(data), out(out) {}
	bool failed() const { return error; }
	void fail(ErrorCode code, std::string message) { if (!error) { error = true; out = Error{code, pos, std::move(message)}; } }
	bool u8(std::uint8_t& v) { if (pos == data.size()) { fail(ErrorCode::truncated, "unexpected end of .rtsld"); return false; } v = std::to_integer<std::uint8_t>(data[pos++]); return true; }
	bool u16(std::uint16_t& v) { std::uint8_t a{}, b{}; if (!u8(a) || !u8(b)) return false; v = static_cast<std::uint16_t>(a | (static_cast<std::uint16_t>(b) << 8)); return true; }
	bool u32(std::uint32_t& v) { v = 0; for (unsigned s = 0; s < 32; s += 8) { std::uint8_t b{}; if (!u8(b)) return false; v |= static_cast<std::uint32_t>(b) << s; } return true; }
	bool string(std::string& v) { std::uint32_t n{}; if (!count(n, "string byte count") || n > max_string_bytes) return false; if (n > data.size() - pos) { fail(ErrorCode::truncated, "string exceeds artifact"); return false; } v.assign(reinterpret_cast<const char*>(data.data() + pos), n); pos += n; return true; }
	bool count(std::uint32_t& n, std::string_view what) { if (!u32(n)) return false; if (n > max_records) { fail(ErrorCode::invalid_count, std::string(what) + " exceeds limit"); return false; } return true; }
	template <typename T, typename F> bool vector(std::vector<T>& values, std::string_view what, F read) { std::uint32_t n{}; if (!count(n, what)) return false; values.clear(); values.reserve(n); for (std::uint32_t i = 0; i < n; ++i) { T value{}; if (!read(value)) return false; values.push_back(std::move(value)); } return true; }
	bool finish() { if (pos != data.size()) { fail(ErrorCode::invalid_trailing_data, "trailing bytes in .rtsld"); return false; } return true; }
private: std::span<const std::byte> data; Error& out; std::size_t pos{}; bool error{};
};

void range(Writer& w, const SourceRange& v) { w.u32(v.file); w.u32(v.begin); w.u32(v.end); }
bool range(Reader& r, SourceRange& v) { return r.u32(v.file) && r.u32(v.begin) && r.u32(v.end); }
bool validRange(const SourceRange& v, const Artifact& a) { return v.file < a.source_files.size() && v.begin <= v.end && v.end <= a.source_files[v.file].text.size(); }
void sourceFile(Writer& w, const SourceFile& v) { w.string(v.logical_name); for (auto b : v.content_hash) w.u8(std::to_integer<std::uint8_t>(b)); w.string(v.text); w.vector(v.line_starts, [&w](std::uint32_t x) { w.u32(x); }); }
bool sourceFile(Reader& r, SourceFile& v) { if (!r.string(v.logical_name)) return false; for (auto& b : v.content_hash) { std::uint8_t x{}; if (!r.u8(x)) return false; b = static_cast<std::byte>(x); } return r.string(v.text) && r.vector(v.line_starts, "line starts", [&r](auto& x) { return r.u32(x); }); }
void entity(Writer& w, const SourceEntity& v) { w.u32(static_cast<std::uint32_t>(v.kind)); w.u32(v.id); range(w, v.range); }
bool entity(Reader& r, SourceEntity& v) { std::uint32_t k{}; if (!r.u32(k) || k > static_cast<std::uint32_t>(SourceEntityKind::lowered_instruction)) { r.fail(ErrorCode::invalid_enum, "invalid source entity kind"); return false; } v.kind = static_cast<SourceEntityKind>(k); return r.u32(v.id) && range(r, v.range); }
void symbol(Writer& w, const Symbol& v) { w.u32(v.id); w.string(v.name); w.u32(v.type); range(w, v.declaration); }
bool symbol(Reader& r, Symbol& v) { return r.u32(v.id) && r.string(v.name) && r.u32(v.type) && range(r, v.declaration); }
void type(Writer& w, const Type& v) { w.u32(v.id); w.string(v.name); w.string(v.spelling); w.u32(v.kind); range(w, v.declaration); }
bool type(Reader& r, Type& v) { return r.u32(v.id) && r.string(v.name) && r.string(v.spelling) && r.u32(v.kind) && range(r, v.declaration); }
void function(Writer& w, const Function& v) { w.u32(v.id); w.u32(v.symbol); w.u32(v.return_type); w.vector(v.parameter_symbols, [&w](auto x) { w.u32(x); }); range(w, v.declaration); }
bool function(Reader& r, Function& v) { return r.u32(v.id) && r.u32(v.symbol) && r.u32(v.return_type) && r.vector(v.parameter_symbols, "function parameters", [&r](auto& x) { return r.u32(x); }) && range(r, v.declaration); }
void templ(Writer& w, const Template& v) { w.u32(v.id); w.u32(v.origin_symbol); w.u32(v.specialized_symbol); w.vector(v.arguments, [&w](const auto& x) { w.string(x); }); w.vector(v.instantiation_chain, [&w](auto x) { w.u32(x); }); range(w, v.origin); range(w, v.instantiation); }
bool templ(Reader& r, Template& v) { return r.u32(v.id) && r.u32(v.origin_symbol) && r.u32(v.specialized_symbol) && r.vector(v.arguments, "template arguments", [&r](auto& x) { return r.string(x); }) && r.vector(v.instantiation_chain, "template chain", [&r](auto& x) { return r.u32(x); }) && range(r, v.origin) && range(r, v.instantiation); }
void instruction(Writer& w, const IRInstructionMapping& v) { w.u32(v.function); w.u32(v.block); w.u32(v.instruction); w.u32(v.opcode); w.u32(v.result_symbol); range(w, v.source); }
bool instruction(Reader& r, IRInstructionMapping& v) { return r.u32(v.function) && r.u32(v.block) && r.u32(v.instruction) && r.u32(v.opcode) && r.u32(v.result_symbol) && range(r, v.source); }
void interfaceVar(Writer& w, const InterfaceVariable& v) { w.u32(v.symbol); w.u32(v.type); w.u32(v.location); w.u32(v.direction); range(w, v.declaration); }
bool interfaceVar(Reader& r, InterfaceVariable& v) { return r.u32(v.symbol) && r.u32(v.type) && r.u32(v.location) && r.u32(v.direction) && range(r, v.declaration); }
void binding(Writer& w, const ResourceBinding& v) { w.u32(v.symbol); w.u32(v.type); w.u32(v.descriptor_set); w.u32(v.binding); w.u32(v.kind); range(w, v.declaration); }
bool binding(Reader& r, ResourceBinding& v) { return r.u32(v.symbol) && r.u32(v.type) && r.u32(v.descriptor_set) && r.u32(v.binding) && r.u32(v.kind) && range(r, v.declaration); }
void entry(Writer& w, const EntryPoint& v) { w.u32(v.function); w.string(v.name); w.u32(v.stage); w.vector(v.interfaces, [&w](const auto& x) { interfaceVar(w, x); }); w.vector(v.resources, [&w](const auto& x) { binding(w, x); }); range(w, v.declaration); }
bool entry(Reader& r, EntryPoint& v) { return r.u32(v.function) && r.string(v.name) && r.u32(v.stage) && r.vector(v.interfaces, "interfaces", [&r](auto& x) { return interfaceVar(r, x); }) && r.vector(v.resources, "resources", [&r](auto& x) { return binding(r, x); }) && range(r, v.declaration); }
bool validate(const Artifact& a, Error& error) { auto check = [&](const SourceRange& v, const char* what) { if (!validRange(v, a)) { error = {ErrorCode::invalid_range, 0, std::string(what) + " has an invalid source range"}; return false; } return true; }; for (const auto& v : a.source_entities) if (!check(v.range, "source entity")) return false; for (const auto& v : a.symbols) if (!check(v.declaration, "symbol")) return false; for (const auto& v : a.types) if (!check(v.declaration, "type")) return false; for (const auto& v : a.functions) if (!check(v.declaration, "function")) return false; for (const auto& v : a.templates) if (!check(v.origin, "template origin") || !check(v.instantiation, "template instantiation")) return false; for (const auto& v : a.ir_instructions) if (!check(v.source, "instruction")) return false; for (const auto& v : a.entry_points) { if (!check(v.declaration, "entry point")) return false; for (const auto& x : v.interfaces) if (!check(x.declaration, "interface")) return false; for (const auto& x : v.resources) if (!check(x.declaration, "resource")) return false; } return true; }
}

WriteResult ArtifactWriter::write(const Artifact& artifact) const {
	try { Error error{}; if (!validate(artifact, error)) return {.error = std::move(error)}; Writer w; for (auto b : magic) w.u8(std::to_integer<std::uint8_t>(b)); w.u16(artifact_version_major); w.u16(artifact_version_minor); w.string(artifact.compilation_identity); w.string(artifact.compiler_identity); w.string(artifact.target_identity); w.vector(artifact.source_files, [&w](const auto& x) { sourceFile(w, x); }); w.vector(artifact.source_entities, [&w](const auto& x) { entity(w, x); }); w.vector(artifact.symbols, [&w](const auto& x) { symbol(w, x); }); w.vector(artifact.types, [&w](const auto& x) { type(w, x); }); w.vector(artifact.functions, [&w](const auto& x) { function(w, x); }); w.vector(artifact.templates, [&w](const auto& x) { templ(w, x); }); w.vector(artifact.ir_instructions, [&w](const auto& x) { instruction(w, x); }); w.vector(artifact.entry_points, [&w](const auto& x) { entry(w, x); }); return {.bytes = std::move(w.bytes)}; } catch (const std::bad_alloc&) { return {.error = Error{ErrorCode::allocation_failure, 0, "allocation failed while writing .rtsld"}}; }
}

ReadResult ArtifactReader::read(std::span<const std::byte> bytes) const {
	try { Error error{}; Reader r(bytes, error); for (auto expected : magic) { std::uint8_t actual{}; if (!r.u8(actual)) return {.error = std::move(error)}; if (actual != std::to_integer<std::uint8_t>(expected)) return {.error = Error{ErrorCode::invalid_magic, 0, "not an RTSL debug artifact"}}; } std::uint16_t major{}, minor{}; if (!r.u16(major) || !r.u16(minor)) return {.error = std::move(error)}; if (major != artifact_version_major || minor > artifact_version_minor) return {.error = Error{ErrorCode::unsupported_version, 8, "unsupported .rtsld version"}}; Artifact a; if (!r.string(a.compilation_identity) || !r.string(a.compiler_identity) || !r.string(a.target_identity) || !r.vector(a.source_files, "source files", [&r](auto& x) { return sourceFile(r, x); }) || !r.vector(a.source_entities, "source entities", [&r](auto& x) { return entity(r, x); }) || !r.vector(a.symbols, "symbols", [&r](auto& x) { return symbol(r, x); }) || !r.vector(a.types, "types", [&r](auto& x) { return type(r, x); }) || !r.vector(a.functions, "functions", [&r](auto& x) { return function(r, x); }) || !r.vector(a.templates, "templates", [&r](auto& x) { return templ(r, x); }) || !r.vector(a.ir_instructions, "IR instructions", [&r](auto& x) { return instruction(r, x); }) || !r.vector(a.entry_points, "entry points", [&r](auto& x) { return entry(r, x); }) || !r.finish()) return {.error = std::move(error)}; if (!validate(a, error)) return {.error = std::move(error)}; return {.artifact = std::move(a)}; } catch (const std::bad_alloc&) { return {.error = Error{ErrorCode::allocation_failure, 0, "allocation failed while reading .rtsld"}}; }
}
} // namespace rtsl::debug
