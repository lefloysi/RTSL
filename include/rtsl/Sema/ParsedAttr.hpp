#ifndef RTSL_SEMA_PARSED_ATTR_HPP
#define RTSL_SEMA_PARSED_ATTR_HPP

#include <rtsl/Lex/Token.hpp>

#include <vector>

namespace rtsl {

struct ParsedAttr {
	IdentifierInfo* Name{};
	SourceRange Range;
	std::vector<Token> Tokens;
};

class ParsedAttributes {
public:
	void add(ParsedAttr Attribute) { Attributes.push_back(std::move(Attribute)); }
	[[nodiscard]] const std::vector<ParsedAttr>& attributes() const { return Attributes; }
	[[nodiscard]] bool empty() const { return Attributes.empty(); }
private:
	std::vector<ParsedAttr> Attributes;
};

}

#endif
