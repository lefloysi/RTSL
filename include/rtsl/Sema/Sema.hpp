#ifndef RTSL_SEMA_SEMA_HPP
#define RTSL_SEMA_SEMA_HPP

#include <rtsl/AST/ASTContext.hpp>
#include <rtsl/Basic/Diagnostic.hpp>
#include <rtsl/Sema/DeclSpec.hpp>
#include <rtsl/Sema/ParsedAttr.hpp>

#include <unordered_map>
#include <vector>

namespace rtsl {

class Sema {
public:
	Sema(ASTContext& Context, DiagnosticsEngine& Diagnostics, IdentifierTable& Identifiers);
	[[nodiscard]] QualType actOnType(const ParsedType& Type);
	bool isTypeName(const IdentifierInfo* name) const;
	void setParsingCore(bool value) { ParsingCore = value; }
	RecordDecl* actOnStartRecord(DeclContext* Context, IdentifierInfo* Name, SourceLocation Location, bool Complete,
		bool Internal, bool Exported, const ParsedAttributes& Attributes,
		const std::vector<ParsedTemplateParameter>& TemplateParameters = {});
	FieldDecl* actOnField(RecordDecl* Record, const Declarator& D, const ParsedAttributes& Attributes);
	VarDecl* actOnVariable(DeclContext* Context, const DeclSpec& DS, const Declarator& D, Expr* Init,
		const ParsedAttributes& Attributes);
	VarDecl* actOnLocalVariable(const DeclSpec& DS, const Declarator& D, Expr* Init);
	ParmVarDecl* actOnParameter(DeclContext* Context, const Declarator& D, const ParsedAttributes& Attributes);
	void pushTemplateParameters(const std::vector<ParsedTemplateParameter>& Parameters);
	void popTemplateParameters(const std::vector<ParsedTemplateParameter>& Parameters);
	FunctionDecl* actOnFunction(DeclContext* Context, const DeclSpec& DS, const Declarator& D,
		const std::vector<ParmVarDecl*>& Parameters,
		const std::vector<ParsedParameterContract>& ParameterContracts, Expr* BaseInitializer,
		const ParsedAttributes& Attributes, const std::vector<ParsedTemplateParameter>& TemplateParameters,
		const std::vector<ParsedType>& TypeOnlyParameters);
	[[nodiscard]] bool isFunctionName(IdentifierInfo* Name) const;
	Expr* actOnTemplateIdentifierExpr(IdentifierInfo* Name, const std::vector<ParsedType>& Arguments, SourceLocation Location);
	TypeAliasDecl* actOnTypeAlias(DeclContext* Context, IdentifierInfo* Name, SourceLocation Location,
		const DeclSpec& DS, const ParsedType& Type, const ParsedAttributes& Attributes);
	ImportDecl* actOnImport(DeclContext* Context, const Token& ModuleToken);
	ImportDecl* actOnLibraryImport(DeclContext* Context, const Token& LibraryToken);
	void actOnFinishFunctionBody(FunctionDecl* Function, CompoundStmt* Body);
	void actOnStartFunctionBody(FunctionDecl* Function);
	void actOnStartFunctionSignature(const std::vector<ParmVarDecl*>& Parameters);
	void actOnFinishFunctionSignature();
	void enterScope();
	void exitScope();
	CompoundStmt* actOnCompoundStmt(const std::vector<Stmt*>& Statements);
	Stmt* actOnReturnStmt(Expr* Value);
	Expr* actOnIdentifierExpr(IdentifierInfo* Name, SourceLocation Location);
	Expr* actOnCurrentEmitterExpr(SourceLocation Location);
	Expr* actOnMemberExpr(Expr* Base, IdentifierInfo* Member, SourceLocation Location);
	Expr* actOnSubscriptExpr(Expr* Base, const std::vector<Expr*>& Indices, SourceLocation Location);
	Expr* actOnCallExpr(Expr* Callee, const std::vector<Expr*>& Arguments, SourceLocation Location);
	Expr* actOnUnaryExpr(tok::TokenKind Opcode, Expr* Operand);
	Expr* actOnBinaryExpr(tok::TokenKind Opcode, Expr* Left, Expr* Right);
	Attr* processAttributes(const ParsedAttributes& Attributes);

	[[nodiscard]] ASTContext& getASTContext() { return Context; }
	[[nodiscard]] IdentifierTable& getIdentifierTable() { return Identifiers; }

private:
	void installStandardLibrary(IdentifierTable& Identifiers);
	void applyContextualType(Expr* Expression, QualType Type);
	void bindLocal(IdentifierInfo* Name, ValueDecl* Declaration);
	[[nodiscard]] ValueDecl* lookupValue(IdentifierInfo* Name) const;
	void addRecordFieldsToFunctionScope(RecordDecl* Record);
	[[nodiscard]] RecordDecl* recordForType(QualType ValueType) const;
	[[nodiscard]] FieldDecl* lookupField(RecordDecl* Record, IdentifierInfo* Name) const;
	[[nodiscard]] FieldDecl* lookupField(QualType ValueType, IdentifierInfo* Name) const;
	[[nodiscard]] QualType lookupFieldType(QualType ValueType, IdentifierInfo* Name) const;
	[[nodiscard]] FunctionDecl* lookupMemberFunction(RecordDecl* Record, IdentifierInfo* Name) const;
	[[nodiscard]] FunctionDecl* resolveMemberFunction(QualType Receiver, IdentifierInfo* Name);
	[[nodiscard]] QualType substituteRecordType(QualType Type, const RecordDecl* Owner, QualType Receiver) const;
	FunctionDecl* instantiateRecordMemberFunction(FunctionDecl* Pattern, RecordDecl* Owner, QualType Receiver);
	[[nodiscard]] QualType substituteType(QualType Type, const FunctionDecl* Pattern, const std::vector<TemplateArgument>& Arguments) const;
	FunctionDecl* instantiateFunction(FunctionDecl* Pattern, const std::vector<TemplateArgument>& Arguments);
	ASTContext& Context;
	DiagnosticsEngine& Diagnostics;
	IdentifierTable& Identifiers;
	std::unordered_map<IdentifierInfo*, QualType> Types;
	std::unordered_map<IdentifierInfo*, ValueDecl*> Values;
	std::unordered_map<IdentifierInfo*, std::vector<FunctionDecl*>> Functions;
	std::unordered_map<IdentifierInfo*, RecordDecl*> Records;
	std::unordered_map<IdentifierInfo*, QualType> TemplateValueParameters;
	std::unordered_map<IdentifierInfo*, FunctionDecl*> Constructors;
	std::vector<std::unordered_map<IdentifierInfo*, ValueDecl*>> LocalScopes;
	IdentifierInfo* BufferTemplate{};
	IdentifierInfo* ReturnEmitter{};
	FunctionDecl* CurrentFunction{};
	bool ParsingCore{};
};

}

#endif
