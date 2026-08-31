#ifndef RTSL_AST_AST_CONTEXT_HPP
#define RTSL_AST_AST_CONTEXT_HPP

#include <rtsl/AST/Decl.hpp>

#include <memory_resource>
#include <map>
#include <new>
#include <utility>
#include <vector>

namespace rtsl {

class ASTContext {
public:
	ASTContext();
	template <typename T, typename... Args>
	T* create(Args&&... Arguments) {
		void* Memory = Arena.allocate(sizeof(T), alignof(T));
		return ::new (Memory) T(std::forward<Args>(Arguments)...);
	}
	template <typename T>
	T** copyPointerArray(const std::vector<T*>& Values) {
		if (Values.empty()) return nullptr;
		auto Result = static_cast<T**>(Arena.allocate(sizeof(T*) * Values.size(), alignof(T*)));
		for (std::size_t Index = 0; Index < Values.size(); ++Index) Result[Index] = Values[Index];
		return Result;
	}
	template <typename T>
	const T* copyArray(const std::vector<T>& Values) {
		if (Values.empty()) return nullptr;
		auto Result = static_cast<T*>(Arena.allocate(sizeof(T) * Values.size(), alignof(T)));
		for (std::size_t Index = 0; Index < Values.size(); ++Index) ::new (Result + Index) T(Values[Index]);
		return Result;
	}
	[[nodiscard]] TranslationUnitDecl* getTranslationUnitDecl() const { return TranslationUnit; }
	[[nodiscard]] QualType getBuiltinType(BuiltinTypeKind Kind) const;
	[[nodiscard]] QualType getNamedType(IdentifierInfo* Name);
	[[nodiscard]] QualType getTemplateParameterType(IdentifierInfo* Name);
	[[nodiscard]] QualType getTemplateSpecializationType(IdentifierInfo* Name, const std::vector<QualType>& Arguments);
	[[nodiscard]] QualType getPointerType(QualType Pointee);
	[[nodiscard]] QualType getReferenceType(QualType Pointee);
private:
	struct TemplateSpecializationKey {
		IdentifierInfo* Name{};
		std::vector<QualType> Arguments;
		[[nodiscard]] bool operator<(const TemplateSpecializationKey& Other) const {
			return Name != Other.Name ? Name < Other.Name : Arguments < Other.Arguments;
		}
	};
	std::pmr::monotonic_buffer_resource Arena;
	TranslationUnitDecl* TranslationUnit;
	BuiltinType* Builtins[6]{};
	std::map<IdentifierInfo*, NamedType*> NamedTypes;
	std::map<IdentifierInfo*, TemplateParameterType*> TemplateParameterTypes;
	std::map<QualType, PointerType*> PointerTypes;
	std::map<QualType, ReferenceType*> ReferenceTypes;
	std::map<TemplateSpecializationKey, TemplateSpecializationType*> TemplateSpecializationTypes;
};

}

#endif
