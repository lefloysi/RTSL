#ifndef RTSL_LEX_PREPROCESSOR_HPP
#define RTSL_LEX_PREPROCESSOR_HPP

#include <rtsl/Lex/Lexer.hpp>

#include <memory>
#include <vector>

namespace rtsl {

class Preprocessor {
public:
	Preprocessor(SourceManager& Sources, DiagnosticsEngine& Diagnostics)
		: Sources(Sources), Diagnostics(Diagnostics) {}
	void enterSourceFile(FileID File);
	void lex(Token& Result);
	[[nodiscard]] IdentifierTable& getIdentifierTable() { return Identifiers; }

private:
	SourceManager& Sources;
	DiagnosticsEngine& Diagnostics;
	IdentifierTable Identifiers;
	std::vector<std::unique_ptr<Lexer>> LexerStack;
};

}

#endif
