#ifndef RTSL_AST_ATTR_HPP
#define RTSL_AST_ATTR_HPP

#include <rtsl/Basic/IdentifierTable.hpp>
#include <rtsl/Lex/Token.hpp>

namespace rtsl {

struct AttrToken {
	tok::TokenKind Kind{tok::unknown};
	IdentifierInfo* Identifier{};
	const char* LiteralData{};
	unsigned LiteralLength{};
};

class Attr {
public:
	Attr(IdentifierInfo* Name, const AttrToken* Tokens, unsigned TokenCount, SourceRange Range)
		: Name(Name), Tokens(Tokens), TokenCount(TokenCount), Range(Range) {}
	[[nodiscard]] IdentifierInfo* getName() const { return Name; }
	[[nodiscard]] const AttrToken* tokens() const { return Tokens; }
	[[nodiscard]] unsigned getTokenCount() const { return TokenCount; }
	[[nodiscard]] Attr* getNextAttr() const { return NextAttr; }
	void setNextAttr(Attr* Next) { NextAttr = Next; }
private:
	IdentifierInfo* Name;
	const AttrToken* Tokens;
	unsigned TokenCount;
	SourceRange Range;
	Attr* NextAttr{};
};

}

#endif
