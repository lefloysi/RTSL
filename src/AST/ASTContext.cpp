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
	return QualType(create<NamedType>(Name));
}

QualType ASTContext::getTemplateParameterType(IdentifierInfo* Name) {
	return QualType(create<TemplateParameterType>(Name));
}

QualType ASTContext::getTemplateSpecializationType(IdentifierInfo* Name, const std::vector<QualType>& Arguments) {
	return QualType(create<TemplateSpecializationType>(Name, copyArray(Arguments), static_cast<unsigned>(Arguments.size())));
}

QualType ASTContext::getPointerType(QualType Pointee) {
	return QualType(create<PointerType>(Pointee));
}

QualType ASTContext::getReferenceType(QualType Pointee) {
	return QualType(create<ReferenceType>(Pointee));
}

void DeclContext::addDecl(Decl* Declaration) {
	if (LastDecl) LastDecl->NextDecl = Declaration;
	else FirstDecl = Declaration;
	LastDecl = Declaration;
}

}
