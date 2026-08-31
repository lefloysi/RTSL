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
	RecordDecl* actOnStartRecord(DeclContext* Context, IdentifierInfo* Name, SourceLocation Location, bool Complete,
		bool Internal, bool Exported, const ParsedAttributes& Attributes);
	FieldDecl* actOnField(RecordDecl* Record, const Declarator& D, const ParsedAttributes& Attributes);
	VarDecl* actOnVariable(DeclContext* Context, const DeclSpec& DS, const Declarator& D, Expr* Init,
		const ParsedAttributes& Attributes);
	VarDecl* actOnLocalVariable(const DeclSpec& DS, const Declarator& D, Expr* Init);
	ParmVarDecl* actOnParameter(DeclContext* Context, const Declarator& D, const ParsedAttributes& Attributes);
	void pushTemplateParameters(const std::vector<IdentifierInfo*>& Parameters);
	void popTemplateParameters(const std::vector<IdentifierInfo*>& Parameters);
	FunctionDecl* actOnFunction(DeclContext* Context, const DeclSpec& DS, const Declarator& D,
		const std::vector<ParmVarDecl*>& Parameters,
		const std::vector<ParsedParameterContract>& ParameterContracts, Expr* BaseInitializer,
		const ParsedAttributes& Attributes, const std::vector<IdentifierInfo*>& TemplateParameters);
	TypeAliasDecl* actOnTypeAlias(DeclContext* Context, IdentifierInfo* Name, SourceLocation Location,
		const ParsedType& Type, const ParsedAttributes& Attributes);
	ImportDecl* actOnImport(DeclContext* Context, const Token& ModuleToken);
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
	Expr* actOnCallExpr(Expr* Callee, const std::vector<Expr*>& Arguments, SourceLocation Location);
	Expr* actOnUnaryExpr(tok::TokenKind Opcode, Expr* Operand);
	Expr* actOnBinaryExpr(tok::TokenKind Opcode, Expr* Left, Expr* Right);
	Attr* processAttributes(const ParsedAttributes& Attributes);

	[[nodiscard]] ASTContext& getASTContext() { return Context; }

private:
	void installStandardLibrary(IdentifierTable& Identifiers);
	void applyContextualType(Expr* Expression, QualType Type);
	void bindLocal(IdentifierInfo* Name, ValueDecl* Declaration);
	[[nodiscard]] ValueDecl* lookupValue(IdentifierInfo* Name) const;
	void addRecordFieldsToFunctionScope(RecordDecl* Record);
	[[nodiscard]] RecordDecl* recordForType(QualType ValueType) const;
	[[nodiscard]] FieldDecl* lookupField(RecordDecl* Record, IdentifierInfo* Name) const;
	ASTContext& Context;
	DiagnosticsEngine& Diagnostics;
	std::unordered_map<IdentifierInfo*, QualType> Types;
	std::unordered_map<IdentifierInfo*, ValueDecl*> Values;
	std::unordered_map<IdentifierInfo*, RecordDecl*> Records;
	std::unordered_map<IdentifierInfo*, FunctionDecl*> Constructors;
	std::vector<std::unordered_map<IdentifierInfo*, ValueDecl*>> LocalScopes;
	IdentifierInfo* BufferTemplate{};
	IdentifierInfo* PositionType{};
	IdentifierInfo* ReturnEmitter{};
	FunctionDecl* SampleIntrinsic{};
	FunctionDecl* CurrentFunction{};
};

}

#endif
