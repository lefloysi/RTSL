#ifndef RTSL_AST_STMT_HPP
#define RTSL_AST_STMT_HPP

#include <rtsl/AST/Type.hpp>
#include <rtsl/Lex/Token.hpp>

#include <cstdint>

namespace rtsl {

class ValueDecl;
class VarDecl;
class FunctionDecl;

enum class StmtClass : std::uint8_t {
	stmt_compound,
	stmt_decl,
	stmt_if,
	stmt_return,
	stmt_barrier,
	expr_emitter,
	expr_decl_ref,
	expr_type,
	expr_integer,
	expr_floating,
	expr_string,
	expr_unary,
	expr_binary,
	expr_member,
	expr_subscript,
	expr_call,
	expr_construct,
};

class Stmt {
public:
	[[nodiscard]] StmtClass getStmtClass() const { return Class; }
protected:
	explicit Stmt(StmtClass Class) : Class(Class) {}
private:
	StmtClass Class;
};

class Expr : public Stmt {
public:
	[[nodiscard]] QualType getType() const { return ExpressionType; }
	void setType(QualType Type) { ExpressionType = Type; }
protected:
	explicit Expr(StmtClass Class) : Stmt(Class) {}
private:
	QualType ExpressionType;
};

class TypeExpr final : public Expr {
public:
	explicit TypeExpr(QualType Type) : Expr(StmtClass::expr_type) { setType(Type); }
};

class DeclRefExpr final : public Expr {
public:
	explicit DeclRefExpr(ValueDecl* Declaration) : Expr(StmtClass::expr_decl_ref), Declaration(Declaration) {}
	[[nodiscard]] ValueDecl* getDecl() const { return Declaration; }
	void setDecl(ValueDecl* Value) { Declaration = Value; }
private:
	ValueDecl* Declaration;
};

class EmitterExpr final : public Expr {
public:
	EmitterExpr(FunctionDecl* Function, QualType ValueType) : Expr(StmtClass::expr_emitter), Function(Function) {
		setType(ValueType);
	}
	[[nodiscard]] FunctionDecl* getFunction() const { return Function; }
private:
	FunctionDecl* Function;
};

class IntegerLiteral final : public Expr {
public:
	explicit IntegerLiteral(std::uint64_t Value) : Expr(StmtClass::expr_integer), Value(Value) {}
	[[nodiscard]] std::uint64_t getValue() const { return Value; }
private:
	std::uint64_t Value;
};

class FloatingLiteral final : public Expr {
public:
	explicit FloatingLiteral(double Value) : Expr(StmtClass::expr_floating), Value(Value) {}
	[[nodiscard]] double getValue() const { return Value; }
private:
	double Value;
};

class UnaryExpr final : public Expr {
public:
	UnaryExpr(tok::TokenKind Opcode, Expr* Operand) : Expr(StmtClass::expr_unary), Opcode(Opcode), Operand(Operand) {}
	[[nodiscard]] tok::TokenKind getOpcode() const { return Opcode; }
	[[nodiscard]] Expr* getOperand() const { return Operand; }
private:
	tok::TokenKind Opcode;
	Expr* Operand;
};

class BinaryExpr final : public Expr {
public:
	BinaryExpr(tok::TokenKind Opcode, Expr* Left, Expr* Right)
		: Expr(StmtClass::expr_binary), Opcode(Opcode), Left(Left), Right(Right) {}
	[[nodiscard]] tok::TokenKind getOpcode() const { return Opcode; }
	[[nodiscard]] Expr* getLeft() const { return Left; }
	[[nodiscard]] Expr* getRight() const { return Right; }
private:
	tok::TokenKind Opcode;
	Expr* Left;
	Expr* Right;
};

class PostfixExpr final : public Expr {
public:
	PostfixExpr(StmtClass Class, Expr* Base, IdentifierInfo* Member, Expr** Arguments, unsigned ArgumentCount)
		: Expr(Class), Base(Base), Member(Member), Arguments(Arguments), ArgumentCount(ArgumentCount) {}
	[[nodiscard]] Expr* getBase() const { return Base; }
	[[nodiscard]] IdentifierInfo* getMember() const { return Member; }
	[[nodiscard]] Expr* const* arguments() const { return Arguments; }
	[[nodiscard]] unsigned getArgumentCount() const { return ArgumentCount; }
private:
	Expr* Base;
	IdentifierInfo* Member;
	Expr** Arguments;
	unsigned ArgumentCount;
};

class ConstructExpr final : public Expr {
public:
	ConstructExpr(QualType Type, Expr** Arguments, unsigned ArgumentCount)
		: Expr(StmtClass::expr_construct), Arguments(Arguments), ArgumentCount(ArgumentCount) { setType(Type); }
	[[nodiscard]] Expr* const* arguments() const { return Arguments; }
	[[nodiscard]] unsigned getArgumentCount() const { return ArgumentCount; }
private:
	Expr** Arguments;
	unsigned ArgumentCount;
};

class CompoundStmt final : public Stmt {
public:
	CompoundStmt(Stmt** Body, unsigned Size) : Stmt(StmtClass::stmt_compound), Body(Body), Size(Size) {}
	[[nodiscard]] Stmt* const* body() const { return Body; }
	[[nodiscard]] unsigned size() const { return Size; }
private:
	Stmt** Body;
	unsigned Size;
};

class DeclStmt final : public Stmt {
public:
	explicit DeclStmt(VarDecl* Declaration) : Stmt(StmtClass::stmt_decl), Declaration(Declaration) {}
	[[nodiscard]] VarDecl* getDecl() const { return Declaration; }
private:
	VarDecl* Declaration;
};

class IfStmt final : public Stmt {
public:
	IfStmt(Expr* Condition, Stmt* Then, Stmt* Else) : Stmt(StmtClass::stmt_if), Condition(Condition), Then(Then), Else(Else) {}
	[[nodiscard]] Expr* getCondition() const { return Condition; }
	[[nodiscard]] Stmt* getThen() const { return Then; }
	[[nodiscard]] Stmt* getElse() const { return Else; }
private:
	Expr* Condition;
	Stmt* Then;
	Stmt* Else;
};

class ValueStmt final : public Stmt {
public:
	ValueStmt(StmtClass Class, Expr* Value) : Stmt(Class), Value(Value) {}
	[[nodiscard]] Expr* getValue() const { return Value; }
private:
	Expr* Value;
};

class BarrierStmt final : public Stmt {
public:
	explicit BarrierStmt(IdentifierInfo* Name) : Stmt(StmtClass::stmt_barrier), Name(Name) {}
	[[nodiscard]] IdentifierInfo* getName() const { return Name; }
private:
	IdentifierInfo* Name;
};

}

#endif
