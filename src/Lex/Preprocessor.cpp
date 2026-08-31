#include <rtsl/Lex/Preprocessor.hpp>

namespace rtsl {

void Preprocessor::enterSourceFile(FileID File) {
	LexerStack.push_back(std::make_unique<Lexer>(File, Sources, Identifiers, Diagnostics));
}

void Preprocessor::lex(Token& Result) {
	while (!LexerStack.empty()) {
		LexerStack.back()->lex(Result);
		if (Result.isNot(tok::eof) || LexerStack.size() == 1) return;
		LexerStack.pop_back();
	}
	Result = {};
	Result.setKind(tok::eof);
}

}
