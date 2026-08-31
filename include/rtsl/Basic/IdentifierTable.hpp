#ifndef RTSL_BASIC_IDENTIFIER_TABLE_HPP
#define RTSL_BASIC_IDENTIFIER_TABLE_HPP

#include <rtsl/Basic/TokenKinds.hpp>

#include <memory_resource>
#include <string_view>
#include <unordered_map>

namespace rtsl {

struct IdentifierTableEntry;

class IdentifierInfo {
public:
	[[nodiscard]] std::string_view getName() const;
	[[nodiscard]] tok::TokenKind getTokenID() const { return TokenID; }

private:
	IdentifierTableEntry* Entry{};
	tok::TokenKind TokenID{tok::identifier};
	friend class IdentifierTable;
};

struct IdentifierTableEntry {
	std::string_view Name;
	IdentifierInfo Info;
};

class IdentifierTable {
public:
	IdentifierTable();
	IdentifierInfo& get(std::string_view Name);
	IdentifierInfo& get(std::string_view Name, tok::TokenKind TokenID);

private:
	std::pmr::monotonic_buffer_resource Arena;
	std::unordered_map<std::string_view, IdentifierTableEntry*> Identifiers;
};

}

#endif
