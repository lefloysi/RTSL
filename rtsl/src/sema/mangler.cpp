#include "sema/mangler.hpp"

#include <format>
#include <vector>

namespace rtsl {

static std::vector<std::string_view> split_qualified_name(std::string_view name) {
	std::vector<std::string_view> parts;
	while (!name.empty()) {
		const auto scope = name.find("::");
		if (scope == std::string_view::npos) {
			parts.push_back(name);
			break;
		}
		parts.push_back(name.substr(0, scope));
		name.remove_prefix(scope + 2);
	}
	return parts;
}

static std::string encode_source_name(std::string_view name) {
	return std::format("{}{}", name.size(), name);
}

static std::string encode_name_part(std::string_view name) {
	if (name.starts_with("~")) {
		return "D1";
	}
	return encode_source_name(name);
}

static std::string encode_type(std::string_view type) {
	if (type.ends_with("&")) {
		type.remove_suffix(1);
		return "R" + encode_type(type);
	}
	if (type.starts_with("const ")) {
		type.remove_prefix(6);
		return "K" + encode_type(type);
	}
	if (type == "void") return "v";
	if (type == "bool") return "b";
	if (type == "f32") return "f";
	if (type == "f64") return "d";
	if (type == "i32") return "i";
	if (type == "u32") return "j";
	return encode_source_name(type);
}

std::string mangle_rtsl(std::string_view name, std::string_view stage, std::span<const std::string_view> parameter_types) {
	std::string out;
	if (!stage.empty()) {
		out = "_ZN4rtsl5stage";
		out += encode_source_name(stage);
		for (const auto part : split_qualified_name(name)) out += encode_name_part(part);
		out += "E";
	} else {
		out = "_Z";
		const auto parts = split_qualified_name(name);
		if (parts.size() > 1) out += "N";
		for (const auto part : parts) {
			out += encode_name_part(part);
		}
		if (parts.size() > 1) out += "E";
	}

	if (parameter_types.empty()) {
		out += "v";
	} else {
		for (const auto parameter_type : parameter_types) {
			out += encode_type(parameter_type);
		}
	}
	return out;
}

} // namespace rtsl
