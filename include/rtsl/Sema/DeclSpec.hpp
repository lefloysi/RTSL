#ifndef RTSL_SEMA_DECL_SPEC_HPP
#define RTSL_SEMA_DECL_SPEC_HPP

#include <rtsl/AST/Decl.hpp>

#include <vector>

namespace rtsl {

struct ParsedType {
	IdentifierInfo* Name{};
	SourceLocation Location;
	std::vector<ParsedType> Arguments;
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
	IdentifierInfo* Name{};
	SourceLocation Location;
	ParsedType Type;
	bool Emits{};
};

}

#endif
