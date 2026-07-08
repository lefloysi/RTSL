#include "frontend/preprocessor.hpp"

#include <cctype>
#include <unordered_set>
#include <vector>

namespace rtsl {

namespace {

enum class DirectiveKind {
	Unknown,
#define RTSL_PP_DIRECTIVE(name, spelling) name,
#include "frontend/directives.def"
};

DirectiveKind directive_kind(std::string_view name) {
#define RTSL_PP_DIRECTIVE(kind, spelling) \
	if (name == spelling) return DirectiveKind::kind;
#include "frontend/directives.def"
	return DirectiveKind::Unknown;
}

std::string_view trim(std::string_view text) {
	const auto is_space = [](char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; };
	for (; !text.empty() && is_space(text.front()); text.remove_prefix(1)) {}
	for (; !text.empty() && is_space(text.back()); text.remove_suffix(1)) {}
	return text;
}

// Consume the leading `[A-Za-z0-9_]+` run — a preprocessor identifier.
// Empty result means the head wasn't an identifier character.
std::string_view take_identifier(std::string_view& text) {
	std::size_t length = 0;
	for (; length < text.size(); ++length) {
		const char c = text[length];
		if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') break;
	}
	const auto identifier = text.substr(0, length);
	text.remove_prefix(length);
	return identifier;
}

// Conditional-inclusion state. One frame per open #if{def,ndef}; a line is
// emitted only when every frame is active.
class ConditionStack {
  public:
	[[nodiscard]] bool active() const { return frames_.empty() || frames_.back().active; }

	void push(bool condition) {
		frames_.push_back(Frame{ .active = active() && condition, .parent_active = active() });
	}

	void flip() {
		if (frames_.empty()) return;
		frames_.back().active = frames_.back().parent_active && !frames_.back().active;
	}

	void pop() {
		if (!frames_.empty()) frames_.pop_back();
	}

  private:
	struct Frame {
		bool active;        // this region is emitted
		bool parent_active; // the enclosing region is emitted (for #else)
	};
	std::vector<Frame> frames_;
};

} // namespace

std::string preprocess_source(std::string_view source, std::span<const std::string> defines) {
	std::unordered_set<std::string> defined(defines.begin(), defines.end());
	ConditionStack conditions;

	// Split into lines up front; the emit loop below indexes them so the
	// original line boundaries (including a missing final newline) survive.
	std::vector<std::string_view> lines;
	for (std::size_t begin = 0; begin <= source.size();) {
		const auto end = source.find('\n', begin);
		if (end == std::string_view::npos) {
			lines.push_back(source.substr(begin));
			break;
		}
		lines.push_back(source.substr(begin, end - begin));
		begin = end + 1;
	}
	const bool ends_with_newline = source.ends_with('\n');
	if (ends_with_newline) lines.pop_back(); // drop the empty tail after the final '\n'

	std::string out;
	out.reserve(source.size());
	for (std::size_t i = 0; i < lines.size(); ++i) {
		const std::string_view line = lines[i];
		std::string_view trimmed = trim(line);

		if (!trimmed.starts_with('#')) {
			if (conditions.active()) {
				out.append(line);
				if (i + 1 < lines.size() || ends_with_newline) out.push_back('\n');
			}
			continue;
		}

		trimmed.remove_prefix(1);
		trimmed = trim(trimmed);
		std::string_view args = trimmed;
		const auto name = take_identifier(args);
		args = trim(args);

		switch (directive_kind(name)) {
		case DirectiveKind::Define:
			if (conditions.active()) {
				if (const auto symbol = take_identifier(args); !symbol.empty()) {
					defined.emplace(symbol);
				}
			}
			break;
		case DirectiveKind::Ifdef:
			conditions.push(defined.contains(std::string(take_identifier(args))));
			break;
		case DirectiveKind::Ifndef:
			conditions.push(!defined.contains(std::string(take_identifier(args))));
			break;
		case DirectiveKind::Else:
			conditions.flip();
			break;
		case DirectiveKind::Endif:
			conditions.pop();
			break;
		case DirectiveKind::Unknown:
			break;
		}
	}
	return out;
}

} // namespace rtsl
