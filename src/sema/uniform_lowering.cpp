#include "sema/uniform_lowering.hpp"

#include "support/basic_types.hpp"

#include <functional>
#include <string>

namespace rtsl {

struct ResourceBindingSpec {
	std::string_view spelling;
	ResourceBindingKind kind;
};

static constexpr ResourceBindingSpec ResourceBindings[] = {
	{ "UniformBuffer", ResourceBindingKind::uniform_buffer },
	{ "StorageBuffer", ResourceBindingKind::storage_buffer },
	{ "Sampler", ResourceBindingKind::sampler },
	{ "Sampler2D", ResourceBindingKind::sampled_image },
	{ "Image2D", ResourceBindingKind::image },
};

static bool is_identifier_char(char c) {
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

static std::string sanitize_symbol_part(std::string_view part) {
	std::string sanitized;
	sanitized.reserve(part.size());
	for (const char c : part) {
		sanitized.push_back(is_identifier_char(c) ? c : '_');
	}
	return sanitized;
}

static void append_mangled_part(std::string& name, std::string_view part) {
	const auto sanitized = sanitize_symbol_part(part);
	name += std::to_string(sanitized.size());
	name += sanitized;
}

// Concatenate identity fields into one key and hash it once with std::hash.
// The suffix only needs to distinguish two uniforms within a single compile,
// so std::hash's within-process stability is enough.
static u32 stable_uniform_hash(const UniformBinding& uniform) {
	const auto key = uniform.scope_name + '\0' + uniform.name + '\0' + uniform.type + '\0' +
					 std::to_string(uniform.set) + '\0' + std::to_string(uniform.member);
	return static_cast<u32>(std::hash<std::string>{}(key));
}

static std::string hash_suffix(u32 hash) {
	constexpr char Digits[] = "0123456789abcdef";
	std::string out = "_h";
	for (int shift = 28; shift >= 0; shift -= 4) {
		out.push_back(Digits[(hash >> shift) & 0xfu]);
	}
	return out;
}

static bool is_replacement_boundary(std::string_view text, std::size_t pos) {
	if (pos >= text.size()) {
		return true;
	}
	const char c = text[pos];
	return !is_identifier_char(c) && c != ':';
}

static std::string replace_symbol(std::string text, std::string_view from, std::string_view to) {
	std::size_t pos = 0;
	while ((pos = text.find(from, pos)) != std::string::npos) {
		if ((pos != 0 && !is_replacement_boundary(text, pos - 1)) ||
			!is_replacement_boundary(text, pos + from.size())) {
			pos += from.size();
			continue;
		}
		text.replace(pos, from.size(), to);
		pos += to.size();
	}
	return text;
}

bool is_resource_uniform_type(std::string_view type) {
	return is_opaque_resource_binding(resource_binding_kind(type));
}

ResourceBindingKind resource_binding_kind(std::string_view spelling) {
	for (const auto& binding : ResourceBindings) {
		if (binding.spelling == spelling) {
			return binding.kind;
		}
	}
	return ResourceBindingKind::none;
}

bool is_buffer_binding(ResourceBindingKind kind) {
	return kind == ResourceBindingKind::uniform_buffer || kind == ResourceBindingKind::storage_buffer;
}

bool is_opaque_resource_binding(ResourceBindingKind kind) {
	return kind == ResourceBindingKind::sampler ||
		   kind == ResourceBindingKind::sampled_image ||
		   kind == ResourceBindingKind::image;
}

std::string uniform_binding_name(const UniformBinding& uniform) {
	std::string name = "u_";
	if (!uniform.is_anonymous && !uniform.scope_name.empty()) {
		append_mangled_part(name, uniform.scope_name);
		name += "_";
	}
	append_mangled_part(name, uniform.name);
	name += hash_suffix(stable_uniform_hash(uniform));
	return name;
}

std::string uniform_block_name(const UniformBinding& uniform) {
	std::string name = "ub_";
	if (!uniform.is_anonymous && !uniform.scope_name.empty()) {
		append_mangled_part(name, uniform.scope_name);
		name += "_";
	}
	append_mangled_part(name, uniform.name);
	name += hash_suffix(stable_uniform_hash(uniform));
	return name;
}

std::string lower_uniform_references(std::string statement, std::span<const UniformBinding> uniforms) {
	for (const auto& uniform : uniforms) {
		if (uniform.is_anonymous || uniform.scope_name.empty()) {
			const auto lowered_name = is_resource_uniform_type(uniform.type)
										  ? uniform_binding_name(uniform)
										  : uniform_binding_name(uniform) + ".member";
			statement = replace_symbol(std::move(statement), uniform.name, lowered_name);
			continue;
		}
		const auto source_name = uniform.scope_name + "::" + uniform.name;
		const auto lowered_name = is_resource_uniform_type(uniform.type)
									  ? uniform_binding_name(uniform)
									  : uniform_binding_name(uniform) + ".member";
		statement = replace_symbol(std::move(statement), source_name, lowered_name);
	}
	return statement;
}

} // namespace rtsl
