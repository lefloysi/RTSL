#ifndef RTSL_AST_DECL_HPP
#define RTSL_AST_DECL_HPP

#include <rtsl/AST/Attr.hpp>
#include <rtsl/AST/Stmt.hpp>

namespace rtsl {

enum class DeclKind : std::uint8_t {
	decl_translation_unit,
	decl_import,
	decl_record,
	decl_field,
	decl_variable,
	decl_parameter,
	decl_function,
	decl_type_alias,
};

enum class StorageClass : std::uint8_t { storage_ordinary, storage_uniform, storage_storage };

class Decl;

class DeclContext {
public:
	void addDecl(Decl* Declaration);
	[[nodiscard]] Decl* declsBegin() const { return FirstDecl; }
private:
	Decl* FirstDecl{};
	Decl* LastDecl{};
};

class Decl {
public:
	[[nodiscard]] DeclKind getKind() const { return Kind; }
	[[nodiscard]] SourceLocation getLocation() const { return Location; }
	[[nodiscard]] Decl* getNextDeclInContext() const { return NextDecl; }
	[[nodiscard]] DeclContext* getDeclContext() const { return Context; }
	[[nodiscard]] Attr* getAttrs() const { return Attributes; }
	void setAttrs(Attr* Value) { Attributes = Value; }
protected:
	Decl(DeclKind Kind, DeclContext* Context, SourceLocation Location) : Kind(Kind), Context(Context), Location(Location) {}
private:
	DeclKind Kind;
	DeclContext* Context;
	SourceLocation Location;
	Decl* NextDecl{};
	Attr* Attributes{};
	friend class DeclContext;
};

class NamedDecl : public Decl {
public:
	[[nodiscard]] IdentifierInfo* getIdentifier() const { return Name; }
protected:
	NamedDecl(DeclKind Kind, DeclContext* Context, SourceLocation Location, IdentifierInfo* Name)
		: Decl(Kind, Context, Location), Name(Name) {}
private:
	IdentifierInfo* Name;
};

class ValueDecl : public NamedDecl {
public:
	[[nodiscard]] QualType getType() const { return DeclarationType; }
protected:
	ValueDecl(DeclKind Kind, DeclContext* Context, SourceLocation Location, IdentifierInfo* Name, QualType Type)
		: NamedDecl(Kind, Context, Location, Name), DeclarationType(Type) {}
private:
	QualType DeclarationType;
};

class VarDecl : public ValueDecl {
public:
	VarDecl(DeclKind Kind, DeclContext* Context, SourceLocation Location, IdentifierInfo* Name, QualType Type,
		StorageClass Storage, bool Constant, bool Internal, bool Exported)
		: ValueDecl(Kind, Context, Location, Name, Type), Storage(Storage), Constant(Constant), Internal(Internal), Exported(Exported) {}
	void setInit(Expr* Value) { Init = Value; }
	[[nodiscard]] Expr* getInit() const { return Init; }
	[[nodiscard]] StorageClass getStorageClass() const { return Storage; }
	[[nodiscard]] bool isConstant() const { return Constant; }
	[[nodiscard]] bool hasInternalLinkage() const { return Internal; }
	[[nodiscard]] bool isExported() const { return Exported; }
private:
	StorageClass Storage;
	bool Constant;
	bool Internal;
	bool Exported;
	Expr* Init{};
};

class ParmVarDecl final : public VarDecl {
public:
	ParmVarDecl(DeclContext* Context, SourceLocation Location, IdentifierInfo* Name, QualType Type)
		: VarDecl(DeclKind::decl_parameter, Context, Location, Name, Type, StorageClass::storage_ordinary, false, false, false) {}
};

class FieldDecl final : public ValueDecl {
public:
	FieldDecl(DeclContext* Context, SourceLocation Location, IdentifierInfo* Name, QualType Type)
		: ValueDecl(DeclKind::decl_field, Context, Location, Name, Type) {}
};

class RecordDecl final : public NamedDecl, public DeclContext {
public:
	RecordDecl(DeclContext* Context, SourceLocation Location, IdentifierInfo* Name, bool Complete, bool Internal, bool Exported,
		QualType BaseType = {}, bool BuiltinPosition = false)
		: NamedDecl(DeclKind::decl_record, Context, Location, Name), BaseType(BaseType), Complete(Complete),
		  Internal(Internal), Exported(Exported), BuiltinPosition(BuiltinPosition) {}
	[[nodiscard]] bool isCompleteDefinition() const { return Complete; }
	[[nodiscard]] bool hasInternalLinkage() const { return Internal; }
	[[nodiscard]] bool isExported() const { return Exported; }
	[[nodiscard]] QualType getBaseType() const { return BaseType; }
	[[nodiscard]] bool isBuiltinPosition() const { return BuiltinPosition; }
	void setBaseType(QualType Value) { BaseType = Value; }
private:
	QualType BaseType;
	bool Complete;
	bool Internal;
	bool Exported;
	bool BuiltinPosition;
};

class TypeAliasDecl final : public NamedDecl {
public:
	TypeAliasDecl(DeclContext* Context, SourceLocation Location, IdentifierInfo* Name, QualType Type,
		bool Internal, bool Exported)
		: NamedDecl(DeclKind::decl_type_alias, Context, Location, Name), AliasedType(Type), Internal(Internal), Exported(Exported) {}
	[[nodiscard]] QualType getAliasedType() const { return AliasedType; }
	[[nodiscard]] bool hasInternalLinkage() const { return Internal; }
	[[nodiscard]] bool isExported() const { return Exported; }
private:
	QualType AliasedType;
	bool Internal;
	bool Exported;
};

class ImportDecl final : public Decl {
public:
	enum class Kind : std::uint8_t { import_file, import_library };
	ImportDecl(DeclContext* Context, SourceLocation Location, const char* ModuleData, unsigned ModuleLength, Kind ImportKind)
		: Decl(DeclKind::decl_import, Context, Location), ModuleData(ModuleData), ModuleLength(ModuleLength), ImportKind(ImportKind) {}
	[[nodiscard]] std::string_view getModuleName() const { return {ModuleData, ModuleLength}; }
	[[nodiscard]] Kind getImportKind() const { return ImportKind; }
private:
	const char* ModuleData;
	unsigned ModuleLength;
	Kind ImportKind;
};

struct ParameterContract {
	unsigned ParameterIndex{};
	IdentifierInfo* const* MemberPath{};
	unsigned MemberPathLength{};
	IdentifierInfo* Contract{};
};

class FunctionDecl final : public ValueDecl, public DeclContext {
public:
	FunctionDecl(DeclContext* Context, SourceLocation Location, IdentifierInfo* Name, QualType ReturnType,
		ParmVarDecl** Parameters, unsigned ParameterCount, const ParameterContract* ParameterContracts,
		unsigned ParameterContractCount, IdentifierInfo* const* TemplateParameters, unsigned TemplateParameterCount,
		const QualType* TypeOnlyParameters, unsigned TypeOnlyParameterCount, Expr* BaseInitializer, bool ImplicitEmitter, bool Internal, bool Exported)
		: ValueDecl(DeclKind::decl_function, Context, Location, Name, ReturnType), Parameters(Parameters),
		  ParameterCount(ParameterCount), ParameterContracts(ParameterContracts), ParameterContractCount(ParameterContractCount),
		  TemplateParameters(TemplateParameters), TemplateParameterCount(TemplateParameterCount), TypeOnlyParameters(TypeOnlyParameters), TypeOnlyParameterCount(TypeOnlyParameterCount), BaseInitializer(BaseInitializer),
		  ImplicitEmitter(ImplicitEmitter), Internal(Internal), Exported(Exported) {}
	void setBody(CompoundStmt* Value) { Body = Value; }
	void setBaseInitializer(Expr* Value) { BaseInitializer = Value; }
	[[nodiscard]] CompoundStmt* getBody() const { return Body; }
	[[nodiscard]] Expr* getBaseInitializer() const { return BaseInitializer; }
	[[nodiscard]] ParmVarDecl* const* parameters() const { return Parameters; }
	[[nodiscard]] unsigned getNumParams() const { return ParameterCount; }
	[[nodiscard]] const ParameterContract* parameterContracts() const { return ParameterContracts; }
	[[nodiscard]] unsigned getNumParameterContracts() const { return ParameterContractCount; }
	[[nodiscard]] IdentifierInfo* const* templateParameters() const { return TemplateParameters; }
	[[nodiscard]] unsigned getNumTemplateParameters() const { return TemplateParameterCount; }
	[[nodiscard]] bool isFunctionTemplate() const { return TemplateParameterCount != 0; }
	[[nodiscard]] const QualType* typeOnlyParameters() const { return TypeOnlyParameters; }
	[[nodiscard]] unsigned getNumTypeOnlyParameters() const { return TypeOnlyParameterCount; }
	[[nodiscard]] bool hasImplicitEmitter() const { return ImplicitEmitter; }
	[[nodiscard]] bool hasInternalLinkage() const { return Internal; }
	[[nodiscard]] bool isExported() const { return Exported; }
private:
	ParmVarDecl** Parameters;
	unsigned ParameterCount;
	const ParameterContract* ParameterContracts;
	unsigned ParameterContractCount;
	IdentifierInfo* const* TemplateParameters;
	unsigned TemplateParameterCount;
	const QualType* TypeOnlyParameters;
	unsigned TypeOnlyParameterCount;
	CompoundStmt* Body{};
	Expr* BaseInitializer{};
	bool ImplicitEmitter;
	bool Internal;
	bool Exported;
};

class TranslationUnitDecl final : public Decl, public DeclContext {
public:
	TranslationUnitDecl() : Decl(DeclKind::decl_translation_unit, nullptr, {}) {}
};

}

#endif
