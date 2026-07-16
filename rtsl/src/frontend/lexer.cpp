#include "frontend/lexer.hpp"

#include <cctype>

namespace rtsl {

bool is_identifier_start(char c) {
	return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
}

bool is_identifier_continue(char c) {
	return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

std::string_view token_spelling(TokenKind kind) {
	// Spelling table generated from tokens.def — the same database the enum
	// came from. The compiler collapses the switch to a jump table.
	switch (kind) {
#define RTSL_KEYWORD(name, spelling) \
	case TokenKind::kw_##name: return spelling;
#define RTSL_PUNCTUATOR(name, spelling) \
	case TokenKind::name: return spelling;
#include "frontend/tokens.def"
	default: return "";
	}
}

TokenKind keyword_kind(std::string_view text) {
#define RTSL_KEYWORD(name, spelling) \
	if (text == spelling) return TokenKind::kw_##name;
#include "frontend/tokens.def"
	return TokenKind::identifier;
}

Lexer::Lexer(SourceManager& source_manager, DiagnosticEngine& diagnostic_engine, u32 source_file_id)
	: sources(source_manager),
	  diagnostics(diagnostic_engine),
	  file_id(source_file_id),
	  input(source_manager.buffer(source_file_id)) {}

std::vector<Token> Lexer::lex() {
	std::vector<Token> tokens;

	while (true) {
		skip_whitespace_and_comments();
		if (at_end()) {
			tokens.push_back(make_token(TokenKind::end_of_file, cursor, cursor));
			break;
		}

		const char c = peek();
		if (is_identifier_start(c)) {
			tokens.push_back(lex_identifier_or_keyword());
		} else if (c == '"') {
			tokens.push_back(lex_string());
		} else if (std::isdigit(static_cast<unsigned char>(c))) {
			tokens.push_back(lex_number());
		} else {
			tokens.push_back(lex_punctuation());
		}
	}

	return tokens;
}

char Lexer::peek(std::size_t lookahead) const {
	return at_end(lookahead) ? '\0' : input[cursor + lookahead];
}

bool Lexer::at_end(std::size_t lookahead) const {
	return cursor + lookahead >= input.size();
}

void Lexer::skip_whitespace_and_comments() {
	bool consumed = true;
	while (consumed && !at_end()) {
		consumed = false;
		while (!at_end() && std::isspace(static_cast<unsigned char>(peek()))) {
			++cursor;
			consumed = true;
		}

		if (peek() == '/' && peek(1) == '/') {
			cursor += 2;
			while (!at_end() && peek() != '\n') {
				++cursor;
			}
			consumed = true;
		} else if (peek() == '/' && peek(1) == '*') {
			const auto comment_begin = cursor;
			cursor += 2;
			while (!at_end() && !(peek() == '*' && peek(1) == '/')) {
				++cursor;
			}
			if (at_end()) {
				diagnose(comment_begin, "unterminated block comment");
				return;
			}
			cursor += 2;
			consumed = true;
		}
	}
}

Token Lexer::lex_identifier_or_keyword() {
	const auto begin = cursor;
	++cursor;
	while (!at_end() && is_identifier_continue(peek())) {
		++cursor;
	}
	const auto text = input.substr(begin, cursor - begin);
	return make_token(keyword_kind(text), begin, cursor);
}

Token Lexer::lex_number() {
	const auto begin = cursor;
	while (!at_end() && std::isdigit(static_cast<unsigned char>(peek()))) {
		++cursor;
	}

	bool is_float = false;
	if (peek() == '.' && std::isdigit(static_cast<unsigned char>(peek(1)))) {
		is_float = true;
		++cursor;
		while (!at_end() && std::isdigit(static_cast<unsigned char>(peek()))) {
			++cursor;
		}
	}

	return make_token(is_float ? TokenKind::float_literal : TokenKind::integer_literal, begin, cursor);
}

Token Lexer::lex_string() {
	const auto begin = cursor;
	++cursor;
	while (!at_end() && peek() != '"') {
		if (peek() == '\\' && !at_end(1)) {
			cursor += 2;
			continue;
		}
		++cursor;
	}
	if (!at_end()) {
		++cursor;
	}
	return make_token(TokenKind::string_literal, begin, cursor);
}

Token Lexer::lex_punctuation() {
	const auto begin = cursor;
	const std::string_view rest = input.substr(cursor);

	// Punctuator table from tokens.def. Entries are ordered longest-prefix
	// first there, so taking the first match implements maximal munch for
	// spellings of any length. Anything unmatched is an invalid character.
#define RTSL_PUNCTUATOR(name, spelling)                    \
	if (rest.starts_with(spelling)) {                      \
		cursor += std::string_view(spelling).size();      \
		return make_token(TokenKind::name, begin, cursor); \
	}
#include "frontend/tokens.def"

	diagnose(begin, "invalid character in source");
	++cursor;
	return make_token(TokenKind::invalid, begin, cursor);
}

Token Lexer::make_token(TokenKind kind, std::size_t begin, std::size_t end) const {
	return {
		kind,
		input.substr(begin, end - begin),
		SourceSpan{ sources.location_at(file_id, begin), end - begin },
	};
}

void Lexer::diagnose(std::size_t offset, std::string_view message) {
	diagnostics.report(DiagnosticCode::lexer_invalid_character, DiagnosticSeverity::error, sources.location_at(file_id, offset), sources.name(file_id), message);
}

} // namespace rtsl
