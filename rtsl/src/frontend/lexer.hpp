#pragma once

#include "support/basic_diagnostics.hpp"
#include "frontend/token.hpp"

#include <vector>

namespace rtsl {

class Lexer {
  public:
	Lexer(SourceManager& source_manager, DiagnosticEngine& diagnostic_engine, u32 source_file_id);

	[[nodiscard]] std::vector<Token> lex();

  private:
	[[nodiscard]] char peek(std::size_t lookahead = 0) const;
	[[nodiscard]] bool at_end(std::size_t lookahead = 0) const;

	void skip_whitespace_and_comments();
	Token lex_identifier_or_keyword();
	Token lex_string();
	Token lex_number();
	Token lex_punctuation();
	Token make_token(TokenKind kind, std::size_t begin, std::size_t end) const;
	void diagnose(std::size_t offset, std::string_view message);

	SourceManager& sources;
	DiagnosticEngine& diagnostics;
	u32 file_id = 0;
	std::string_view input;
	std::size_t cursor = 0;
};

} // namespace rtsl
