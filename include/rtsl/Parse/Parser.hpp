#ifndef RTSL_PARSE_PARSER_HPP
#define RTSL_PARSE_PARSER_HPP

#include <rtsl/Lex/Preprocessor.hpp>
#include <rtsl/Sema/Sema.hpp>

namespace rtsl {

class Parser {
public:
	Parser(Preprocessor& PP, Sema& Actions, DiagnosticsEngine& Diagnostics);
	void parseTranslationUnit();

private:
	void consumeToken();
	const Token& peekToken();
	bool consumeIf(tok::TokenKind Kind);
	bool expectAndConsume(tok::TokenKind Kind, std::string_view Message);
	ParsedAttributes parseAttributes();
	void parseExternalDeclaration(ParsedAttributes& Attributes);
	bool parseTemplateParameterList(std::vector<ParsedTemplateParameter>& Parameters);
	void parseFunctionTemplate(DeclContext* Context, DeclSpec& DS, ParsedAttributes& Attributes);
	void parseDeclSpec(DeclSpec& DS);
	ParsedType parseType();
	ParsedType parseAnonymousRecordType(SourceLocation Location);
	Declarator parseDeclarator(ParsedType Type);
	void parseRecord(DeclContext* Context, DeclSpec& DS, ParsedAttributes& Attributes,
		const std::vector<ParsedTemplateParameter>& TemplateParameters = {});
	void parseTypeAlias(DeclContext* Context, const DeclSpec& DS, ParsedAttributes& Attributes);
	void parseVariable(DeclContext* Context, DeclSpec& DS, ParsedAttributes& Attributes);
	void parseFunction(DeclContext* Context, DeclSpec& DS, ParsedAttributes& Attributes,
		const std::vector<ParsedTemplateParameter>& TemplateParameters = {});
	ParmVarDecl* parseParameter(FunctionDecl* FunctionContext);
	CompoundStmt* parseCompoundStatement();
	Stmt* parseStatement();
	Stmt* parseLocalDeclaration();
	Stmt* parseIfStatement();
	Expr* parseExpression(unsigned MinimumPrecedence = 0);
	Expr* parseUnaryExpression();
	Expr* parsePrimaryExpression();
	void parseParameterContracts(unsigned ParameterIndex, std::vector<IdentifierInfo*>& Path,
		std::vector<ParsedParameterContract>& Contracts);
	unsigned getBinaryPrecedence(tok::TokenKind Kind) const;
	void skipBalanced(tok::TokenKind Open, tok::TokenKind Close);
	void synchronizeDeclaration();

	Preprocessor& PP;
	Sema& Actions;
	DiagnosticsEngine& Diagnostics;
	Token Tok;
	Token Lookahead;
	bool HasLookahead{};
};

}

#endif
