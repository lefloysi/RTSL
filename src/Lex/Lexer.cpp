#include <rtsl/Lex/Lexer.hpp>

#include <cctype>

namespace rtsl {

Lexer::Lexer(FileID File, SourceManager& Sources, IdentifierTable& Identifiers, DiagnosticsEngine& Diagnostics)
	: File(File), Sources(Sources), Identifiers(Identifiers), Diagnostics(Diagnostics), Buffer(Sources.getBuffer(File)) {}

void Lexer::formToken(Token& Result, tok::TokenKind Kind, unsigned Start, unsigned End) {
	Result = {};
	Result.setKind(Kind);
	Result.setLocation(Sources.getLocation(File, Start));
	Result.setLength(End - Start);
}

void Lexer::skipWhitespaceAndComments() {
	for (;;) {
		while (Cursor < Buffer.size() && std::isspace(static_cast<unsigned char>(Buffer[Cursor]))) ++Cursor;
		if (Cursor + 1 < Buffer.size() && Buffer[Cursor] == '/' && Buffer[Cursor + 1] == '/') {
			Cursor += 2;
			while (Cursor < Buffer.size() && Buffer[Cursor] != '\n') ++Cursor;
			continue;
		}
		if (Cursor + 1 < Buffer.size() && Buffer[Cursor] == '/' && Buffer[Cursor + 1] == '*') {
			unsigned Start = Cursor;
			Cursor += 2;
			while (Cursor + 1 < Buffer.size() && !(Buffer[Cursor] == '*' && Buffer[Cursor + 1] == '/')) ++Cursor;
			if (Cursor + 1 >= Buffer.size()) {
				Diagnostics.report(DiagnosticLevel::diagnostic_error,
					{Sources.getLocation(File, Start), Sources.getLocation(File, static_cast<unsigned>(Buffer.size()))},
					"unterminated block comment");
				Cursor = static_cast<unsigned>(Buffer.size());
				return;
			}
			Cursor += 2;
			continue;
		}
		return;
	}
}

void Lexer::lexIdentifier(Token& Result, unsigned Start) {
	while (Cursor < Buffer.size() &&
		(std::isalnum(static_cast<unsigned char>(Buffer[Cursor])) || Buffer[Cursor] == '_')) ++Cursor;
	auto& II = Identifiers.get(Buffer.substr(Start, Cursor - Start));
	formToken(Result, II.getTokenID(), Start, Cursor);
	Result.setIdentifierInfo(&II);
}

void Lexer::lexNumber(Token& Result, unsigned Start) {
	while (Cursor < Buffer.size() &&
		(std::isalnum(static_cast<unsigned char>(Buffer[Cursor])) || Buffer[Cursor] == '.' || Buffer[Cursor] == '_')) ++Cursor;
	formToken(Result, tok::numeric_literal, Start, Cursor);
	Result.setLiteralData(Buffer.data() + Start);
}

void Lexer::lexString(Token& Result, unsigned Start) {
	while (Cursor < Buffer.size() && Buffer[Cursor] != '"') {
		if (Buffer[Cursor] == '\\' && Cursor + 1 < Buffer.size()) Cursor += 2;
		else ++Cursor;
	}
	if (Cursor == Buffer.size()) {
		Diagnostics.report(DiagnosticLevel::diagnostic_error,
			{Sources.getLocation(File, Start), Sources.getLocation(File, Cursor)}, "unterminated string literal");
	} else {
		++Cursor;
	}
	formToken(Result, tok::string_literal, Start, Cursor);
	Result.setLiteralData(Buffer.data() + Start);
}

void Lexer::lexPunctuator(Token& Result, unsigned Start) {
	tok::TokenKind Kind = tok::unknown;
	std::size_t Length{};
	const auto Match = [&](tok::TokenKind Candidate, std::string_view Spelling) {
		if (Spelling.size() > Length && Buffer.substr(Start, Spelling.size()) == Spelling) {
			Kind = Candidate;
			Length = Spelling.size();
		}
	};
#define PUNCTUATOR(Name, Spelling) Match(tok::Name, Spelling);
#include <rtsl/Basic/TokenKinds.def>
	if (Kind != tok::unknown) {
		Cursor = Start + static_cast<unsigned>(Length);
	}
	formToken(Result, Kind, Start, Cursor);
	if (Kind == tok::unknown) {
		Diagnostics.report(DiagnosticLevel::diagnostic_error,
			{Result.getLocation(), Result.getLocation()}, "unknown character");
	}
}

void Lexer::lex(Token& Result) {
	skipWhitespaceAndComments();
	if (Cursor == Buffer.size()) {
		formToken(Result, tok::eof, Cursor, Cursor);
		return;
	}
	unsigned Start = Cursor++;
	char Character = Buffer[Start];
	if (std::isalpha(static_cast<unsigned char>(Character)) || Character == '_') {
		lexIdentifier(Result, Start);
		return;
	}
	if (std::isdigit(static_cast<unsigned char>(Character))) {
		lexNumber(Result, Start);
		return;
	}
	if (Character == '"') {
		lexString(Result, Start);
		return;
	}
	lexPunctuator(Result, Start);
}

}
