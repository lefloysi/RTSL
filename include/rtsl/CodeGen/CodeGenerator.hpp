#ifndef RTSL_CODEGEN_CODE_GENERATOR_HPP
#define RTSL_CODEGEN_CODE_GENERATOR_HPP

#include <rtsl/AST/ASTContext.hpp>
#include <rtsl/IR/Builder.hpp>

#include <optional>
#include <rtsl/IR/Verifier.hpp>

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace rtsl {

struct CodeGenDiagnostic {
	std::string Message;
};

struct CodeGenResult {
	ir::Module Module;
	std::vector<CodeGenDiagnostic> Diagnostics;
	ir::VerificationResult Verification;
	[[nodiscard]] bool succeeded() const { return Diagnostics.empty() && Verification.valid(); }
};

class CodeGenerator {
public:
	explicit CodeGenerator(std::string_view ModuleName);
	[[nodiscard]] CodeGenResult generate(ASTContext& Context);

private:
	void declareRecords(TranslationUnitDecl* TranslationUnit);
	void declareAliases(TranslationUnitDecl* TranslationUnit);
	void declareGlobals(TranslationUnitDecl* TranslationUnit);
	void declareFunctions(TranslationUnitDecl* TranslationUnit);
	void declareRecordFunctions(RecordDecl* Record);
	void defineFunctions(TranslationUnitDecl* TranslationUnit);
	void defineRecordFunctions(RecordDecl* Record);
	void defineStages(TranslationUnitDecl* TranslationUnit);
	void defineRecordStages(RecordDecl* Record);
	void lowerRecord(RecordDecl* Record);
	void appendRecordMembers(ir::Type& Type, RecordDecl* Record);
	void collectRecordFields(RecordDecl* Record, std::vector<FieldDecl*>& Fields);
	[[nodiscard]] bool lowerRecordConstruction(ConstructExpr* Construct, RecordDecl* Record,
		std::vector<ir::ValueId>& Fields);
	[[nodiscard]] bool lowerConstructorBaseInitializer(FunctionDecl* Function, RecordDecl* Record);
	void appendRecordValueMembers(RecordDecl* Record, ir::ValueId Value, std::vector<ir::ValueId>& Fields);
	[[nodiscard]] bool appendConstructorFields(RecordDecl* Record, std::vector<ir::ValueId>& Fields);
	void lowerGlobal(VarDecl* Variable);
	void lowerFunctionDeclaration(FunctionDecl* Function);
	void lowerFunctionBody(FunctionDecl* Function);
	void lowerStatement(Stmt* Statement);
	void lowerIfStatement(IfStmt* Statement);
	[[nodiscard]] ir::ValueId lowerExpression(Expr* Expression);
	[[nodiscard]] ir::TypeId lowerType(QualType Type);
	[[nodiscard]] ir::TypeId lowerUnqualifiedType(const Type* Type);
	[[nodiscard]] bool hasUnresolvedTemplateParameter(QualType ValueType) const;
	[[nodiscard]] std::string qualifiedName(const NamedDecl* Declaration) const;
	[[nodiscard]] std::string typeName(QualType Type) const;
	[[nodiscard]] RecordDecl* constructorRecord(FunctionDecl* Function) const;
	[[nodiscard]] RecordDecl* recordForType(QualType ValueType) const;
	[[nodiscard]] Attr* findAttribute(const Decl* Declaration, std::string_view Name) const;
	[[nodiscard]] bool lowerStage(FunctionDecl* Function, ir::FunctionId FunctionID, ir::SymbolId Symbol);
	[[nodiscard]] std::vector<std::uint32_t> numericAttribute(Attr* Attribute) const;
	[[nodiscard]] std::optional<std::string_view> identifierAttribute(Attr* Attribute) const;
	[[nodiscard]] ir::Opcode binaryOpcode(tok::TokenKind Kind) const;
	void diagnose(std::string_view Message);

	std::string ModuleName;
	ASTContext* Context{};
	ir::ModuleBuilder Builder;
	std::vector<CodeGenDiagnostic> Diagnostics;
	std::unordered_map<const Type*, ir::TypeId> Types;
	std::unordered_map<IdentifierInfo*, ir::TypeId> NamedTypes;
	std::unordered_map<const ValueDecl*, ir::ValueId> Values;
	std::unordered_map<const ValueDecl*, ir::SymbolId> GlobalSymbols;
	std::unordered_map<const FunctionDecl*, ir::FunctionId> Functions;
	std::unordered_map<std::uint32_t, ir::TypeId> ValueTypes;
	ir::FunctionId CurrentFunction;
	ir::BlockId CurrentBlock;
	ir::ValueId CurrentReturnObject;
	ir::ValueId CurrentImplicitObject;
	FunctionDecl* CurrentASTFunction{};
	RecordDecl* CurrentConstructor{};
	std::unordered_map<const FieldDecl*, ir::ValueId> ConstructorFields;
	unsigned BarrierIndex{};
	unsigned ConditionalDepth{};
};

}

#endif
