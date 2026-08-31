#ifndef RTSL_LEX_TOKEN_HPP
#define RTSL_LEX_TOKEN_HPP

#include <rtsl/Basic/IdentifierTable.hpp>
#include <rtsl/Basic/SourceLocation.hpp>

namespace rtsl {

class Token {
public:
	[[nodiscard]] tok::TokenKind getKind() const { return Kind; }
	[[nodiscard]] bool is(tok::TokenKind K) const { return Kind == K; }
	[[nodiscard]] bool isNot(tok::TokenKind K) const { return Kind != K; }
	[[nodiscard]] SourceLocation getLocation() const { return Location; }
	[[nodiscard]] unsigned getLength() const { return Length; }
	[[nodiscard]] IdentifierInfo* getIdentifierInfo() const { return static_cast<IdentifierInfo*>(PtrData); }
	[[nodiscard]] const char* getLiteralData() const { return static_cast<const char*>(PtrData); }
	void setKind(tok::TokenKind K) { Kind = K; }
	void setLocation(SourceLocation L) { Location = L; }
	void setLength(unsigned L) { Length = L; }
	void setIdentifierInfo(IdentifierInfo* II) { PtrData = II; }
	void setLiteralData(const char* Data) { PtrData = const_cast<char*>(Data); }

private:
	SourceLocation Location;
	unsigned Length{};
	void* PtrData{};
	tok::TokenKind Kind{tok::unknown};
};

}

#endif
