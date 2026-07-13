#pragma once

#include "support/basic_source_manager.hpp"
#include "support/basic_types.hpp"

#include <string_view>

namespace rtsl {

// Token kinds are generated from frontend/tokens.def: reserved words first,
// then punctuation.
enum class TokenKind : u16 {
	invalid,
	end_of_file,
	identifier,
	string_literal,
	integer_literal,
	float_literal,

#define RTSL_KEYWORD(name, spelling) kw_##name,
#define RTSL_PUNCTUATOR(name, spelling) name,
#include "frontend/tokens.def"
};

struct Token {
	TokenKind kind = TokenKind::invalid;
	std::string_view text{};
	SourceSpan span{};
};

[[nodiscard]] std::string_view token_spelling(TokenKind kind);
[[nodiscard]] TokenKind keyword_kind(std::string_view text);

} // namespace rtsl
