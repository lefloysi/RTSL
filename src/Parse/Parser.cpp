#include <rtsl/Parse/Parser.hpp>

#include <charconv>
#include <system_error>

namespace rtsl {

Parser::Parser(Preprocessor& PP, Sema& Actions, DiagnosticsEngine& Diagnostics)
	: PP(PP), Actions(Actions), Diagnostics(Diagnostics) {
	consumeToken();
}

void Parser::consumeToken() {
	if (HasLookahead) {
		Tok = Lookahead;
		HasLookahead = false;
		return;
	}
	PP.lex(Tok);
}

const Token& Parser::peekToken() {
	if (!HasLookahead) {
		PP.lex(Lookahead);
		HasLookahead = true;
	}
	return Lookahead;
}

bool Parser::consumeIf(tok::TokenKind Kind) {
	if (Tok.isNot(Kind)) return false;
	consumeToken();
	return true;
}

bool Parser::expectAndConsume(tok::TokenKind Kind, std::string_view Message) {
	if (consumeIf(Kind)) return true;
	Diagnostics.report(DiagnosticLevel::diagnostic_error, {Tok.getLocation(), Tok.getLocation()}, Message);
	return false;
}

ParsedAttributes Parser::parseAttributes() {
	ParsedAttributes Result;
	while (Tok.is(tok::at)) {
		SourceLocation Begin = Tok.getLocation();
		consumeToken();
		ParsedAttr Attribute;
		if (Tok.is(tok::identifier)) {
			Attribute.Name = Tok.getIdentifierInfo();
			consumeToken();
		} else {
			Diagnostics.report(DiagnosticLevel::diagnostic_error, {Begin, Tok.getLocation()}, "expected attribute name");
		}
		expectAndConsume(tok::colon, "expected ':' after attribute name");
		if (Tok.is(tok::l_paren) || Tok.is(tok::l_square) || Tok.is(tok::l_brace)) {
			tok::TokenKind Open = Tok.getKind();
			tok::TokenKind Close = Open == tok::l_paren ? tok::r_paren : Open == tok::l_square ? tok::r_square : tok::r_brace;
			unsigned Depth = 0;
			do {
				if (Tok.is(Open)) ++Depth;
				if (Tok.is(Close)) --Depth;
				Attribute.Tokens.push_back(Tok);
				consumeToken();
			} while (Tok.isNot(tok::eof) && Depth != 0);
		} else if (Tok.isNot(tok::eof)) {
			Attribute.Tokens.push_back(Tok);
			consumeToken();
		}
		Attribute.Range = {Begin, Attribute.Tokens.empty() ? Begin : Attribute.Tokens.back().getLocation()};
		Result.add(std::move(Attribute));
	}
	return Result;
}

void Parser::parseTranslationUnit() {
	while (Tok.isNot(tok::eof)) {
		if (consumeIf(tok::semi)) continue;
		const SourceLocation Begin = Tok.getLocation();
		auto Attributes = parseAttributes();
		parseExternalDeclaration(Attributes);
		if (Tok.isNot(tok::eof) && Tok.getLocation().getRawEncoding() == Begin.getRawEncoding()) {
			Diagnostics.report(DiagnosticLevel::diagnostic_error, {Tok.getLocation(), Tok.getLocation()},
				"unable to recover from malformed top-level declaration");
			consumeToken();
		}
	}
}

void Parser::parseExternalDeclaration(ParsedAttributes& Attributes) {
	auto Context = Actions.getASTContext().getTranslationUnitDecl();
	DeclSpec DS;
	bool ReadingSpecifiers = true;
	while (ReadingSpecifiers) {
		switch (Tok.getKind()) {
		case tok::kw_export: DS.Exported = true; consumeToken(); break;
		case tok::kw_static: DS.Internal = true; consumeToken(); break;
		case tok::kw_const: DS.Constant = true; consumeToken(); break;
		default: ReadingSpecifiers = false; break;
		}
	}
	switch (Tok.getKind()) {
	case tok::kw_import:
		consumeToken();
		if (Tok.is(tok::string_literal)) { Actions.actOnImport(Context, Tok); consumeToken(); }
		else if (consumeIf(tok::less)) {
			if (Tok.is(tok::identifier)) { Actions.actOnLibraryImport(Context, Tok); consumeToken(); }
			else Diagnostics.report(DiagnosticLevel::diagnostic_error, {Tok.getLocation(), Tok.getLocation()}, "expected library name after '<'");
			expectAndConsume(tok::greater, "expected '>' after library name");
		} else Diagnostics.report(DiagnosticLevel::diagnostic_error, {Tok.getLocation(), Tok.getLocation()}, "expected import path or library name");
		expectAndConsume(tok::semi, "expected ';' after import");
		return;
	case tok::kw_struct:
		parseRecord(Context, DS, Attributes);
		return;
	case tok::kw_fn: {
		parseFunction(Context, DS, Attributes);
		return;
	}
	case tok::kw_template:
		parseFunctionTemplate(Context, DS, Attributes);
		return;
	case tok::kw_using:
		parseTypeAlias(Context, DS, Attributes);
		return;
	default: {
		parseDeclSpec(DS);
		if (!DS.Type.Name) {
			Diagnostics.report(DiagnosticLevel::diagnostic_error, {Tok.getLocation(), Tok.getLocation()}, "expected declaration");
			synchronizeDeclaration();
			return;
		}
		parseVariable(Context, DS, Attributes);
	}
	}
}

bool Parser::parseTemplateParameterList(std::vector<IdentifierInfo*>& Parameters) {
	consumeToken();
	if (!expectAndConsume(tok::less, "expected '<' after 'template'")) return false;
	while (Tok.isNot(tok::greater) && Tok.isNot(tok::eof)) {
		if (!consumeIf(tok::kw_typename)) {
			Diagnostics.report(DiagnosticLevel::diagnostic_error, {Tok.getLocation(), Tok.getLocation()},
				"expected 'typename' in template parameter list");
			return false;
		}
		if (Tok.isNot(tok::identifier)) {
			Diagnostics.report(DiagnosticLevel::diagnostic_error, {Tok.getLocation(), Tok.getLocation()},
				"expected template parameter name");
			return false;
		}
		Parameters.push_back(Tok.getIdentifierInfo());
		consumeToken();
		if (!consumeIf(tok::comma)) break;
	}
	return expectAndConsume(tok::greater, "expected '>' after template parameter list");
}

void Parser::parseFunctionTemplate(DeclContext* Context, DeclSpec& DS, ParsedAttributes& Attributes) {
	std::vector<IdentifierInfo*> Parameters;
	if (!parseTemplateParameterList(Parameters)) {
		synchronizeDeclaration();
		return;
	}
	if (Tok.isNot(tok::kw_fn)) {
		Diagnostics.report(DiagnosticLevel::diagnostic_error, {Tok.getLocation(), Tok.getLocation()},
			"a template declaration must declare a function");
		synchronizeDeclaration();
		return;
	}
	Actions.pushTemplateParameters(Parameters);
	parseFunction(Context, DS, Attributes, Parameters);
	Actions.popTemplateParameters(Parameters);
}

void Parser::parseTypeAlias(DeclContext* Context, const DeclSpec& DS, ParsedAttributes& Attributes) {
	SourceLocation Location = Tok.getLocation();
	consumeToken();
	IdentifierInfo* Name{};
	if (Tok.is(tok::identifier)) { Name = Tok.getIdentifierInfo(); consumeToken(); }
	else Diagnostics.report(DiagnosticLevel::diagnostic_error, {Location, Tok.getLocation()}, "expected alias name");
	expectAndConsume(tok::equal, "expected '=' in type alias");
	auto Type = parseType();
	Actions.actOnTypeAlias(Context, Name, Location, DS, Type, Attributes);
	expectAndConsume(tok::semi, "expected ';' after type alias");
}

void Parser::parseDeclSpec(DeclSpec& DS) {
	bool Continue = true;
	while (Continue) {
		switch (Tok.getKind()) {
		case tok::kw_export: DS.Exported = true; consumeToken(); break;
		case tok::kw_static: DS.Internal = true; consumeToken(); break;
		case tok::kw_const: DS.Constant = true; consumeToken(); break;
		case tok::kw_uniform: DS.Storage = StorageClass::storage_uniform; consumeToken(); break;
		case tok::kw_storage: DS.Storage = StorageClass::storage_storage; consumeToken(); break;
		case tok::kw_var: DS.HasVar = true; consumeToken(); break;
		default: Continue = false; break;
		}
	}
	DS.Type = parseType();
	DS.Type.Constant = DS.Constant;
}

ParsedType Parser::parseType() {
	ParsedType Result;
	Result.Constant = consumeIf(tok::kw_const);
	if (Tok.isNot(tok::identifier)) return Result;
	Result.Location = Tok.getLocation();
	Result.Name = Tok.getIdentifierInfo();
	consumeToken();
	if (consumeIf(tok::less)) {
		while (Tok.isNot(tok::greater) && Tok.isNot(tok::eof)) {
			if (Tok.is(tok::numeric_literal)) {
				ParsedType Value;
				Value.Location = Tok.getLocation();
				Value.Name = &PP.getIdentifierTable().get("usize");
				std::uint32_t IntegerValue{};
				auto [Position, Error] = std::from_chars(Tok.getLiteralData(), Tok.getLiteralData() + Tok.getLength(), IntegerValue);
				if (Error != std::errc{} || Position != Tok.getLiteralData() + Tok.getLength())
					Diagnostics.report(DiagnosticLevel::diagnostic_error, {Tok.getLocation(), Tok.getLocation()}, "expected an unsigned integer template argument");
				else Value.IntegerValue = IntegerValue;
				Result.Arguments.push_back(Value);
				consumeToken();
			} else {
				Result.Arguments.push_back(parseType());
			}
			if (!consumeIf(tok::comma)) break;
		}
		expectAndConsume(tok::greater, "expected '>' after template arguments");
	}
	while (Tok.is(tok::coloncolon)) {
		Diagnostics.report(DiagnosticLevel::diagnostic_error, {Tok.getLocation(), Tok.getLocation()},
			"qualified type names are not supported");
		consumeToken();
		if (Tok.is(tok::identifier)) consumeToken();
		else break;
	}
	Result.Pointer = consumeIf(tok::star);
	Result.Reference = consumeIf(tok::amp);
	return Result;
}

Declarator Parser::parseDeclarator(ParsedType Type) {
	Declarator Result;
	Result.Type = std::move(Type);
	Result.Location = Tok.getLocation();
	if (Tok.is(tok::identifier)) {
		Result.Name = Tok.getIdentifierInfo();
		consumeToken();
	} else {
		Diagnostics.report(DiagnosticLevel::diagnostic_error, {Tok.getLocation(), Tok.getLocation()}, "expected declarator name");
	}
	return Result;
}

void Parser::parseRecord(DeclContext* Context, DeclSpec& DS, ParsedAttributes& Attributes) {
	SourceLocation Location = Tok.getLocation();
	consumeToken();
	IdentifierInfo* Name{};
	if (Tok.is(tok::identifier)) { Name = Tok.getIdentifierInfo(); consumeToken(); }
	else Diagnostics.report(DiagnosticLevel::diagnostic_error, {Location, Tok.getLocation()}, "expected structure name");
	QualType BaseType;
	if (consumeIf(tok::colon)) {
		auto Base = parseType();
		if (!Base.Name) {
			Diagnostics.report(DiagnosticLevel::diagnostic_error, {Tok.getLocation(), Tok.getLocation()},
				"expected base structure name");
		} else {
			BaseType = Actions.actOnType(Base);
		}
		if (consumeIf(tok::comma)) {
			Diagnostics.report(DiagnosticLevel::diagnostic_error, {Tok.getLocation(), Tok.getLocation()},
				"a structure can have only one direct base");
			while (Tok.isNot(tok::l_brace) && Tok.isNot(tok::semi) && Tok.isNot(tok::eof)) consumeToken();
		}
	}
	bool Complete = Tok.is(tok::l_brace);
	auto Record = Actions.actOnStartRecord(Context, Name, Location, Complete, DS.Internal, DS.Exported, Attributes);
	Record->setBaseType(BaseType);
	if (!Complete) { expectAndConsume(tok::semi, "expected ';' after structure declaration"); return; }
	consumeToken();
	while (Tok.isNot(tok::r_brace) && Tok.isNot(tok::eof)) {
		if (consumeIf(tok::semi)) continue;
		auto FieldAttributes = parseAttributes();
		if (Tok.is(tok::kw_fn)) {
			DeclSpec FunctionSpec;
			parseFunction(Record, FunctionSpec, FieldAttributes);
			continue;
		}
		if (Tok.is(tok::kw_template)) {
			DeclSpec FunctionSpec;
			parseFunctionTemplate(Record, FunctionSpec, FieldAttributes);
			continue;
		}
		DeclSpec DS;
		parseDeclSpec(DS);
		if (!DS.Type.Name) { synchronizeDeclaration(); continue; }
		auto D = parseDeclarator(DS.Type);
		Actions.actOnField(Record, D, FieldAttributes);
		expectAndConsume(tok::semi, "expected ';' after field");
	}
	expectAndConsume(tok::r_brace, "expected '}' after structure");
	consumeIf(tok::semi);
}

void Parser::parseVariable(DeclContext* Context, DeclSpec& DS, ParsedAttributes& Attributes) {
	auto D = parseDeclarator(DS.Type);
	Expr* Init{};
	if (consumeIf(tok::colonequal)) Init = parseExpression();
	Actions.actOnVariable(Context, DS, D, Init, Attributes);
	expectAndConsume(tok::semi, "expected ';' after variable declaration");
}

void Parser::parseFunction(DeclContext* Context, DeclSpec& DS, ParsedAttributes& Attributes,
	const std::vector<IdentifierInfo*>& TemplateParameters) {
	SourceLocation Location = Tok.getLocation();
	consumeToken();
	Declarator D;
	D.Location = Location;
	if (Tok.is(tok::identifier)) {
		auto* FirstName = Tok.getIdentifierInfo();
		D.EnclosingLocation = Tok.getLocation();
		consumeToken();
		if (consumeIf(tok::coloncolon)) {
			D.EnclosingName = FirstName;
			if (Tok.is(tok::identifier)) { D.Name = Tok.getIdentifierInfo(); consumeToken(); }
			else Diagnostics.report(DiagnosticLevel::diagnostic_error, {Location, Tok.getLocation()}, "expected function name after '::'");
		} else D.Name = FirstName;
	}
	else Diagnostics.report(DiagnosticLevel::diagnostic_error, {Location, Tok.getLocation()}, "expected function name");
	expectAndConsume(tok::l_paren, "expected '(' after function name");
	std::vector<ParmVarDecl*> Parameters;
	std::vector<ParsedType> TypeOnlyParameters;
	std::vector<ParsedParameterContract> ParameterContracts;
	while (Tok.isNot(tok::r_paren) && Tok.isNot(tok::eof)) {
		auto ParameterAttributes = parseAttributes();
		auto Type = parseType();
		if (!Type.Name) {
			Diagnostics.report(DiagnosticLevel::diagnostic_error, {Tok.getLocation(), Tok.getLocation()},
				"expected parameter declaration");
			break;
		}
		if (Tok.isNot(tok::identifier) && Type.Name && Type.Name->getName() == "tessellation") {
			TypeOnlyParameters.push_back(std::move(Type));
			if (!consumeIf(tok::comma)) break;
			continue;
		}
		auto ParameterDeclarator = parseDeclarator(Type);
		if (!ParameterDeclarator.Name) break;
		Parameters.push_back(Actions.actOnParameter(nullptr, ParameterDeclarator, ParameterAttributes));
		if (Tok.is(tok::l_brace)) {
			std::vector<IdentifierInfo*> Path;
			parseParameterContracts(static_cast<unsigned>(Parameters.size() - 1), Path, ParameterContracts);
		}
		if (!consumeIf(tok::comma)) break;
	}
	const bool ClosedParameters = expectAndConsume(tok::r_paren, "expected ')' after parameters");
	if (!ClosedParameters) {
		while (Tok.isNot(tok::l_brace) && Tok.isNot(tok::semi) && Tok.isNot(tok::eof)) consumeToken();
		if (Tok.is(tok::l_brace)) skipBalanced(tok::l_brace, tok::r_brace);
		else consumeIf(tok::semi);
		return;
	}
	D.Emits = consumeIf(tok::kw_emit);
	if (consumeIf(tok::arrow)) D.Type = parseType();
	else if ((Context != Actions.getASTContext().getTranslationUnitDecl() &&
		static_cast<RecordDecl*>(Context)->getIdentifier() == D.Name) ||
		(D.EnclosingName && D.EnclosingName == D.Name))
		D.Type.Name = D.EnclosingName ? D.EnclosingName : static_cast<RecordDecl*>(Context)->getIdentifier();
	else D.Type.Name = &PP.getIdentifierTable().get("void");
	Expr* BaseInitializer{};
	if (consumeIf(tok::colon)) {
		Actions.actOnStartFunctionSignature(Parameters);
		if ((Context == Actions.getASTContext().getTranslationUnitDecl() ||
			static_cast<RecordDecl*>(Context)->getIdentifier() != D.Name) &&
			(!D.EnclosingName || D.EnclosingName != D.Name)) {
			Diagnostics.report(DiagnosticLevel::diagnostic_error, {Tok.getLocation(), Tok.getLocation()},
				"a base initializer is only valid on a structure constructor");
		}
		BaseInitializer = parseExpression();
		Actions.actOnFinishFunctionSignature();
	}
	auto Function = Actions.actOnFunction(Context, DS, D, Parameters, ParameterContracts, BaseInitializer, Attributes,
		TemplateParameters, TypeOnlyParameters);
	if (!Function) {
		if (consumeIf(tok::semi)) return;
		if (Tok.is(tok::l_brace)) skipBalanced(tok::l_brace, tok::r_brace);
		else synchronizeDeclaration();
		return;
	}
	if (consumeIf(tok::semi)) return;
	if (Tok.is(tok::l_brace)) {
		Actions.actOnStartFunctionBody(Function);
		Actions.actOnFinishFunctionBody(Function, parseCompoundStatement());
	}
	else {
		Diagnostics.report(DiagnosticLevel::diagnostic_error, {Tok.getLocation(), Tok.getLocation()},
			"expected function body or ';'");
		synchronizeDeclaration();
	}
}

void Parser::parseParameterContracts(unsigned ParameterIndex, std::vector<IdentifierInfo*>& Path,
	std::vector<ParsedParameterContract>& Contracts) {
	expectAndConsume(tok::l_brace, "expected '{' in parameter contract pattern");
	while (Tok.isNot(tok::r_brace) && Tok.isNot(tok::eof)) {
		consumeIf(tok::period);
		if (Tok.isNot(tok::identifier)) {
			Diagnostics.report(DiagnosticLevel::diagnostic_error, {Tok.getLocation(), Tok.getLocation()},
				"expected member name in parameter contract pattern");
			break;
		}
		Path.push_back(Tok.getIdentifierInfo());
		consumeToken();
		if (Tok.is(tok::l_brace)) parseParameterContracts(ParameterIndex, Path, Contracts);
		else if (consumeIf(tok::colon)) {
			ParsedParameterContract Contract{.ParameterIndex = ParameterIndex, .MemberPath = Path};
			if (Tok.is(tok::identifier)) { Contract.Contract = Tok.getIdentifierInfo(); consumeToken(); }
			else Diagnostics.report(DiagnosticLevel::diagnostic_error, {Tok.getLocation(), Tok.getLocation()},
				"expected contract name");
			Contracts.push_back(Contract);
		}
		Path.pop_back();
		if (consumeIf(tok::comma)) continue;
		if (Tok.is(tok::r_brace) || Tok.is(tok::eof)) break;
		Diagnostics.report(DiagnosticLevel::diagnostic_error, {Tok.getLocation(), Tok.getLocation()},
			"expected ',' or '}' after parameter contract");
		while (Tok.isNot(tok::r_brace) && Tok.isNot(tok::eof)) consumeToken();
		break;
	}
	expectAndConsume(tok::r_brace, "expected '}' after parameter contract pattern");
}

CompoundStmt* Parser::parseCompoundStatement() {
	expectAndConsume(tok::l_brace, "expected '{'");
	Actions.enterScope();
	std::vector<Stmt*> Statements;
	while (Tok.isNot(tok::r_brace) && Tok.isNot(tok::eof)) {
		if (auto Statement = parseStatement()) Statements.push_back(Statement);
	}
	expectAndConsume(tok::r_brace, "expected '}'");
	Actions.exitScope();
	return Actions.actOnCompoundStmt(Statements);
}

Stmt* Parser::parseStatement() {
	auto& Context = Actions.getASTContext();
	if (Tok.is(tok::kw_var)) return parseLocalDeclaration();
	if (Tok.is(tok::kw_if)) return parseIfStatement();
	if (Tok.is(tok::kw_return)) {
		consumeToken();
		Expr* Value = Tok.is(tok::semi) ? nullptr : parseExpression();
		expectAndConsume(tok::semi, "expected ';' after statement");
		return Actions.actOnReturnStmt(Value);
	}
	if (Tok.is(tok::kw_emit)) {
		const SourceLocation Location = Tok.getLocation();
		consumeToken();
		Expr* Value = Tok.is(tok::semi) ? nullptr : parseExpression();
		if (!Value) Diagnostics.report(DiagnosticLevel::diagnostic_error,
			{Location, Tok.getLocation()}, "expected value after 'emit'");
		expectAndConsume(tok::semi, "expected ';' after emit statement");
		return Actions.actOnBinaryExpr(tok::lessminus, Actions.actOnCurrentEmitterExpr(Location), Value);
	}
	if (Tok.is(tok::identifier) && peekToken().is(tok::colon)) {
		IdentifierInfo* Name = Tok.getIdentifierInfo();
		consumeToken();
		consumeToken();
		return Context.create<BarrierStmt>(Name);
	}
	if (Tok.is(tok::identifier) || Tok.is(tok::numeric_literal) || Tok.is(tok::l_paren) ||
		Tok.is(tok::minus) || Tok.is(tok::exclaim)) {
		Expr* Expression = parseExpression();
		expectAndConsume(tok::semi, "expected ';' after expression");
		return Expression;
	}
	if (Tok.is(tok::l_brace)) return parseCompoundStatement();
	Diagnostics.report(DiagnosticLevel::diagnostic_error, {Tok.getLocation(), Tok.getLocation()},
		"unexpected token in statement");
	consumeToken();
	return nullptr;
}

Stmt* Parser::parseLocalDeclaration() {
	DeclSpec DS;
	parseDeclSpec(DS);
	if (!DS.HasVar || !DS.Type.Name) {
		Diagnostics.report(DiagnosticLevel::diagnostic_error, {Tok.getLocation(), Tok.getLocation()},
			"expected type after 'var'");
		synchronizeDeclaration();
		return nullptr;
	}
	auto D = parseDeclarator(DS.Type);
	Expr* Init{};
	if (consumeIf(tok::equal)) Init = parseExpression();
	expectAndConsume(tok::semi, "expected ';' after local variable declaration");
	auto Variable = Actions.actOnLocalVariable(DS, D, Init);
	return Variable ? Actions.getASTContext().create<DeclStmt>(Variable) : nullptr;
}

Stmt* Parser::parseIfStatement() {
	consumeToken();
	expectAndConsume(tok::l_paren, "expected '(' after 'if'");
	Expr* Condition = parseExpression();
	expectAndConsume(tok::r_paren, "expected ')' after if condition");
	Stmt* Then = parseStatement();
	Stmt* Else{};
	if (consumeIf(tok::kw_else)) Else = parseStatement();
	return Actions.getASTContext().create<IfStmt>(Condition, Then, Else);
}

Expr* Parser::parseExpression(unsigned MinimumPrecedence) {
	Expr* Left = parseUnaryExpression();
	while (Left) {
		unsigned Precedence = getBinaryPrecedence(Tok.getKind());
		if (Precedence == 0 || Precedence < MinimumPrecedence) break;
		auto Opcode = Tok.getKind();
		consumeToken();
		const bool RightAssociative = Opcode == tok::equal || Opcode == tok::lessminus;
		Expr* Right = parseExpression(Precedence + (RightAssociative ? 0 : 1));
		Left = Actions.actOnBinaryExpr(Opcode, Left, Right);
	}
	return Left;
}

Expr* Parser::parseUnaryExpression() {
	if (Tok.is(tok::minus) || Tok.is(tok::exclaim)) {
		auto Opcode = Tok.getKind();
		consumeToken();
		return Actions.actOnUnaryExpr(Opcode, parseUnaryExpression());
	}
	return parsePrimaryExpression();
}

Expr* Parser::parsePrimaryExpression() {
	auto& Context = Actions.getASTContext();
	Expr* Result{};
	if (Tok.is(tok::identifier)) {
		Result = Actions.actOnIdentifierExpr(Tok.getIdentifierInfo(), Tok.getLocation());
		consumeToken();
	} else if (Tok.is(tok::numeric_literal)) {
		const SourceLocation Location = Tok.getLocation();
		auto Data = Tok.getLiteralData();
		auto Length = Tok.getLength();
		std::string_view Spelling(Data, Length);
		bool Floating = Spelling.find('.') != std::string_view::npos;
		std::uint64_t IntegerValue{};
		double FloatingValue{};
		const auto ParseResult = Floating
			? std::from_chars(Data, Data + Length, FloatingValue)
			: std::from_chars(Data, Data + Length, IntegerValue);
		consumeToken();
		if (ParseResult.ec != std::errc{} || ParseResult.ptr != Data + Length) {
			Diagnostics.report(DiagnosticLevel::diagnostic_error, {Location, Location},
				"invalid numeric literal");
			return nullptr;
		}
		if (Floating) {
			Result = Context.create<FloatingLiteral>(FloatingValue);
			Result->setType(Context.getBuiltinType(BuiltinTypeKind::builtin_f32));
		} else {
			Result = Context.create<IntegerLiteral>(IntegerValue);
		}
	} else if (consumeIf(tok::l_paren)) {
		Result = parseExpression();
		expectAndConsume(tok::r_paren, "expected ')'");
	} else {
		return nullptr;
	}
	for (;;) {
		if (consumeIf(tok::period)) {
			IdentifierInfo* Member{};
			if (Tok.is(tok::identifier)) { Member = Tok.getIdentifierInfo(); consumeToken(); }
			else Diagnostics.report(DiagnosticLevel::diagnostic_error, {Tok.getLocation(), Tok.getLocation()}, "expected member name");
			Result = Actions.actOnMemberExpr(Result, Member, Tok.getLocation());
			continue;
		}
		if (consumeIf(tok::l_square)) {
			Expr* Index = parseExpression();
			expectAndConsume(tok::r_square, "expected ']' after index");
			Result = Actions.actOnSubscriptExpr(Result, Index, Tok.getLocation());
			continue;
		}
		if (consumeIf(tok::l_paren)) {
			std::vector<Expr*> Arguments;
			while (Tok.isNot(tok::r_paren) && Tok.isNot(tok::eof)) {
				Arguments.push_back(parseExpression());
				if (!consumeIf(tok::comma)) break;
			}
			expectAndConsume(tok::r_paren, "expected ')' after arguments");
			Result = Actions.actOnCallExpr(Result, Arguments, Tok.getLocation());
			continue;
		}
		break;
	}
	return Result;
}

unsigned Parser::getBinaryPrecedence(tok::TokenKind Kind) const {
	switch (Kind) {
	case tok::equal: case tok::lessminus: return 1;
	case tok::pipepipe: return 2;
	case tok::ampamp: return 3;
	case tok::equalequal: case tok::exclaimequal: return 4;
	case tok::less: case tok::lessequal: case tok::greater: case tok::greaterequal: return 5;
	case tok::plus: case tok::minus: return 6;
	case tok::star: case tok::slash: case tok::percent: return 7;
	default: return 0;
	}
}

void Parser::skipBalanced(tok::TokenKind Open, tok::TokenKind Close) {
	unsigned Depth = 0;
	do {
		if (Tok.is(Open)) ++Depth;
		if (Tok.is(Close)) --Depth;
		consumeToken();
	} while (Tok.isNot(tok::eof) && Depth != 0);
}

void Parser::synchronizeDeclaration() {
	while (Tok.isNot(tok::semi) && Tok.isNot(tok::r_brace) && Tok.isNot(tok::eof)) consumeToken();
	consumeIf(tok::semi);
}

}
