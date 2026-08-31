#include <rtsl/Basic/IdentifierTable.hpp>

#include <cstring>

namespace rtsl {

std::string_view IdentifierInfo::getName() const { return Entry ? Entry->Name : std::string_view{}; }

IdentifierTable::IdentifierTable() {
#define KEYWORD(Name, Flags) get(#Name, tok::kw_##Name);
#include <rtsl/Basic/TokenKinds.def>
}

IdentifierInfo& IdentifierTable::get(std::string_view Name) {
	if (auto Position = Identifiers.find(Name); Position != Identifiers.end()) return Position->second->Info;
	auto Storage = static_cast<char*>(Arena.allocate(Name.size(), alignof(char)));
	std::memcpy(Storage, Name.data(), Name.size());
	std::string_view StableName(Storage, Name.size());
	auto Entry = static_cast<IdentifierTableEntry*>(Arena.allocate(sizeof(IdentifierTableEntry), alignof(IdentifierTableEntry)));
	::new (Entry) IdentifierTableEntry{StableName, {}};
	Entry->Info.Entry = Entry;
	Identifiers.emplace(StableName, Entry);
	return Entry->Info;
}

IdentifierInfo& IdentifierTable::get(std::string_view Name, tok::TokenKind TokenID) {
	auto& Result = get(Name);
	Result.TokenID = TokenID;
	return Result;
}

}
