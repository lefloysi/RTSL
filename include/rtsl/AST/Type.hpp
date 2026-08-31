#ifndef RTSL_AST_TYPE_HPP
#define RTSL_AST_TYPE_HPP

#include <rtsl/Basic/IdentifierTable.hpp>

#include <cstdint>

namespace rtsl {

enum class TypeClass : std::uint8_t {
	type_builtin,
	type_named,
	type_template_parameter,
	type_template_specialization,
	type_pointer,
	type_reference,
};

class alignas(8) Type {
public:
	[[nodiscard]] TypeClass getTypeClass() const { return Class; }
protected:
	explicit Type(TypeClass Class) : Class(Class) {}
private:
	TypeClass Class;
};

static_assert(alignof(Type) >= 2);

class QualType {
public:
	QualType() = default;
	explicit QualType(const Type* TypePointer, bool Constant = false)
		: Value(reinterpret_cast<std::uintptr_t>(TypePointer) | (Constant ? 1 : 0)) {}
	[[nodiscard]] const Type* getTypePtr() const { return reinterpret_cast<const Type*>(Value & ~std::uintptr_t{1}); }
	[[nodiscard]] bool isConstQualified() const { return (Value & 1) != 0; }
	[[nodiscard]] explicit operator bool() const { return getTypePtr() != nullptr; }
private:
	std::uintptr_t Value{};
};

enum class BuiltinTypeKind : std::uint8_t {
	builtin_void,
	builtin_bool,
	builtin_i32,
	builtin_u32,
	builtin_usize,
	builtin_f32,
};

class BuiltinType final : public Type {
public:
	explicit BuiltinType(BuiltinTypeKind Kind) : Type(TypeClass::type_builtin), Kind(Kind) {}
	[[nodiscard]] BuiltinTypeKind getKind() const { return Kind; }
private:
	BuiltinTypeKind Kind;
};

class NamedType final : public Type {
public:
	explicit NamedType(IdentifierInfo* Name) : Type(TypeClass::type_named), Name(Name) {}
	[[nodiscard]] IdentifierInfo* getName() const { return Name; }
private:
	IdentifierInfo* Name;
};

class TemplateParameterType final : public Type {
public:
	explicit TemplateParameterType(IdentifierInfo* Name) : Type(TypeClass::type_template_parameter), Name(Name) {}
	[[nodiscard]] IdentifierInfo* getName() const { return Name; }
private:
	IdentifierInfo* Name;
};

class TemplateSpecializationType final : public Type {
public:
	TemplateSpecializationType(IdentifierInfo* Name, const QualType* Arguments, unsigned ArgumentCount)
		: Type(TypeClass::type_template_specialization), Name(Name), Arguments(Arguments), ArgumentCount(ArgumentCount) {}
	[[nodiscard]] IdentifierInfo* getName() const { return Name; }
	[[nodiscard]] const QualType* arguments() const { return Arguments; }
	[[nodiscard]] unsigned getArgumentCount() const { return ArgumentCount; }
private:
	IdentifierInfo* Name;
	const QualType* Arguments;
	unsigned ArgumentCount;
};

class PointerType final : public Type {
public:
	explicit PointerType(QualType Pointee) : Type(TypeClass::type_pointer), Pointee(Pointee) {}
	[[nodiscard]] QualType getPointeeType() const { return Pointee; }
private:
	QualType Pointee;
};

class ReferenceType final : public Type {
public:
	explicit ReferenceType(QualType Pointee) : Type(TypeClass::type_reference), Pointee(Pointee) {}
	[[nodiscard]] QualType getPointeeType() const { return Pointee; }
private:
	QualType Pointee;
};

}

#endif
