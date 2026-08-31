#ifndef RTSL_LEX_LEXER_HPP
#define RTSL_LEX_LEXER_HPP

#include <rtsl/Basic/Diagnostic.hpp>
#include <rtsl/Basic/IdentifierTable.hpp>
#include <rtsl/Basic/SourceManager.hpp>
#include <rtsl/Lex/Token.hpp>

namespace rtsl {

class Lexer {
public:
	Lexer(FileID File, SourceManager& Sources, IdentifierTable& Identifiers, DiagnosticsEngine& Diagnostics);
	void lex(Token& Result);

private:
	void skipWhitespaceAndComments();
	void lexIdentifier(Token& Result, unsigned Start);
	void lexNumber(Token& Result, unsigned Start);
	void lexString(Token& Result, unsigned Start);
	void lexPunctuator(Token& Result, unsigned Start);
	void formToken(Token& Result, tok::TokenKind Kind, unsigned Start, unsigned End);

	FileID File;
	SourceManager& Sources;
	IdentifierTable& Identifiers;
	DiagnosticsEngine& Diagnostics;
	std::string_view Buffer;
	unsigned Cursor{};
};

}

#endif
