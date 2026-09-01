#ifndef RTSL_SEMA_DECL_SPEC_HPP
#define RTSL_SEMA_DECL_SPEC_HPP

#include <rtsl/AST/Decl.hpp>

#include <vector>
#include <optional>
#include <cstdint>

namespace rtsl {

struct ParsedType {
	IdentifierInfo* Name{};
	SourceLocation Location;
	std::vector<ParsedType> Arguments;
	std::optional<std::uint32_t> IntegerValue;
	bool Pointer{};
	bool Reference{};
	bool Constant{};
};

struct ParsedParameterContract {
	unsigned ParameterIndex{};
	std::vector<IdentifierInfo*> MemberPath;
	IdentifierInfo* Contract{};
};

class DeclSpec {
public:
	StorageClass Storage{StorageClass::storage_ordinary};
	bool Constant{};
	bool Internal{};
	bool Exported{};
	bool HasVar{};
	ParsedType Type;
};

class Declarator {
public:
	IdentifierInfo* EnclosingName{};
	SourceLocation EnclosingLocation;
	IdentifierInfo* Name{};
	SourceLocation Location;
	ParsedType Type;
	bool Emits{};
};

}

#endif
