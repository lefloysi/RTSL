#include <rtsl/AST/ASTContext.hpp>

namespace rtsl {

ASTContext::ASTContext() {
	TranslationUnit = create<TranslationUnitDecl>();
	for (unsigned Index = 0; Index < 6; ++Index) Builtins[Index] = create<BuiltinType>(static_cast<BuiltinTypeKind>(Index));
}

QualType ASTContext::getBuiltinType(BuiltinTypeKind Kind) const {
	return QualType(Builtins[static_cast<unsigned>(Kind)]);
}

QualType ASTContext::getNamedType(IdentifierInfo* Name) {
	auto [Position, Inserted] = NamedTypes.try_emplace(Name, nullptr);
	if (Inserted) Position->second = create<NamedType>(Name);
	return QualType(Position->second);
}

QualType ASTContext::getTemplateParameterType(IdentifierInfo* Name) {
	auto [Position, Inserted] = TemplateParameterTypes.try_emplace(Name, nullptr);
	if (Inserted) Position->second = create<TemplateParameterType>(Name);
	return QualType(Position->second);
}

QualType ASTContext::getTemplateSpecializationType(IdentifierInfo* Name, const std::vector<QualType>& Arguments) {
	TemplateSpecializationKey Key{.Name = Name, .Arguments = Arguments};
	auto [Position, Inserted] = TemplateSpecializationTypes.try_emplace(std::move(Key), nullptr);
	if (Inserted) Position->second = create<TemplateSpecializationType>(Name, copyArray(Arguments), static_cast<unsigned>(Arguments.size()));
	return QualType(Position->second);
}

QualType ASTContext::getPointerType(QualType Pointee) {
	auto [Position, Inserted] = PointerTypes.try_emplace(Pointee, nullptr);
	if (Inserted) Position->second = create<PointerType>(Pointee);
	return QualType(Position->second);
}

QualType ASTContext::getReferenceType(QualType Pointee) {
	auto [Position, Inserted] = ReferenceTypes.try_emplace(Pointee, nullptr);
	if (Inserted) Position->second = create<ReferenceType>(Pointee);
	return QualType(Position->second);
}

void DeclContext::addDecl(Decl* Declaration) {
	if (LastDecl) LastDecl->NextDecl = Declaration;
	else FirstDecl = Declaration;
	LastDecl = Declaration;
}

}
