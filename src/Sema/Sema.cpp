#include <rtsl/Sema/Sema.hpp>

#include <algorithm>
#include <string>

namespace rtsl {
namespace {

void rebindDeclarationReferences(Expr* Expression, const std::unordered_map<ValueDecl*, ValueDecl*>& Rebindings) {
	if (!Expression) return;
	switch (Expression->getStmtClass()) {
	case StmtClass::expr_decl_ref: {
		auto Reference = static_cast<DeclRefExpr*>(Expression);
		if (auto Position = Rebindings.find(Reference->getDecl()); Position != Rebindings.end()) Reference->setDecl(Position->second);
		return;
	}
	case StmtClass::expr_unary:
		rebindDeclarationReferences(static_cast<UnaryExpr*>(Expression)->getOperand(), Rebindings);
		return;
	case StmtClass::expr_binary: {
		auto Binary = static_cast<BinaryExpr*>(Expression);
		rebindDeclarationReferences(Binary->getLeft(), Rebindings);
		rebindDeclarationReferences(Binary->getRight(), Rebindings);
		return;
	}
	case StmtClass::expr_member:
	case StmtClass::expr_subscript:
	case StmtClass::expr_call: {
		auto Postfix = static_cast<PostfixExpr*>(Expression);
		rebindDeclarationReferences(Postfix->getBase(), Rebindings);
		for (unsigned Index = 0; Index < Postfix->getArgumentCount(); ++Index)
			rebindDeclarationReferences(Postfix->arguments()[Index], Rebindings);
		return;
	}
	case StmtClass::expr_construct: {
		auto Construct = static_cast<ConstructExpr*>(Expression);
		for (unsigned Index = 0; Index < Construct->getArgumentCount(); ++Index)
			rebindDeclarationReferences(Construct->arguments()[Index], Rebindings);
		return;
	}
	default:
		return;
	}
}

} // namespace

Sema::Sema(ASTContext& Context, DiagnosticsEngine& Diagnostics, IdentifierTable& Identifiers)
	: Context(Context), Diagnostics(Diagnostics), Identifiers(Identifiers) {
	installStandardLibrary(Identifiers);
}

void Sema::installStandardLibrary(IdentifierTable& Identifiers) {
	Types[&Identifiers.get("__void")] = Context.getBuiltinType(BuiltinTypeKind::builtin_void);
	Types[&Identifiers.get("__bool")] = Context.getBuiltinType(BuiltinTypeKind::builtin_bool);
	Types[&Identifiers.get("__i32")] = Context.getBuiltinType(BuiltinTypeKind::builtin_i32);
	Types[&Identifiers.get("__u32")] = Context.getBuiltinType(BuiltinTypeKind::builtin_u32);
	Types[&Identifiers.get("__usize")] = Context.getBuiltinType(BuiltinTypeKind::builtin_usize);
	Types[&Identifiers.get("__f32")] = Context.getBuiltinType(BuiltinTypeKind::builtin_f32);
	for (auto Name : {"__vec2", "__vec3", "__vec4", "__mat2", "__mat3", "__mat4"}) {
		auto& II = Identifiers.get(Name);
		Types[&II] = Context.getNamedType(&II);
	}
	BufferTemplate = &Identifiers.get("buffer");
	ReturnEmitter = &Identifiers.get("__return");
}

QualType Sema::actOnType(const ParsedType& Parsed) {
	QualType Result;
	if (!Parsed.Name) return Result;
	if (!Parsed.Arguments.empty()) {
		std::vector<QualType> Arguments;
		std::vector<std::optional<std::uint32_t>> IntegerArguments;
		std::vector<IdentifierInfo*> IntegerParameters;
		for (const auto& Argument : Parsed.Arguments) {
			const bool ValueParameter = !Argument.IntegerValue && Argument.Name && TemplateValueParameters.contains(Argument.Name);
			Arguments.push_back(ValueParameter ? TemplateValueParameters.at(Argument.Name) : actOnType(Argument));
			IntegerArguments.push_back(Argument.IntegerValue);
			IntegerParameters.push_back(ValueParameter ? Argument.Name : nullptr);
		}
		if (!Types.contains(Parsed.Name))
			Diagnostics.report(DiagnosticLevel::diagnostic_error, {Parsed.Location, Parsed.Location}, "unknown type name");
		if (Parsed.Name == BufferTemplate && Arguments.size() == 2) {
			auto First = Arguments[0].getTypePtr();
			auto Second = Arguments[1].getTypePtr();
			bool FirstVoid = First && First->getTypeClass() == TypeClass::type_builtin &&
				static_cast<const BuiltinType*>(First)->getKind() == BuiltinTypeKind::builtin_void;
			bool SecondVoid = Second && Second->getTypeClass() == TypeClass::type_builtin &&
				static_cast<const BuiltinType*>(Second)->getKind() == BuiltinTypeKind::builtin_void;
			if (FirstVoid && SecondVoid) Diagnostics.report(DiagnosticLevel::diagnostic_error, {Parsed.Location, Parsed.Location},
				"buffer<void, void> has neither header nor repeated storage");
		}
		Result = Context.getTemplateSpecializationType(Parsed.Name, Arguments, IntegerArguments, IntegerParameters);
	} else if (auto Position = Types.find(Parsed.Name); Position != Types.end()) {
		Result = Position->second;
	} else {
		Diagnostics.report(DiagnosticLevel::diagnostic_error, {Parsed.Location, Parsed.Location}, "unknown type name");
		Result = Context.getNamedType(Parsed.Name);
	}
	if (Parsed.Pointer) Result = Context.getPointerType(Result);
	if (Parsed.Reference) Result = Context.getReferenceType(Result);
	if (Parsed.Constant) Result = QualType(Result.getTypePtr(), true);
	return Result;
}

bool Sema::isTypeName(const IdentifierInfo* Name) const {
	return Name != nullptr && Types.contains(const_cast<IdentifierInfo*>(Name));
}

RecordDecl* Sema::recordForType(QualType ValueType) const {
	const Type* TypePointer = ValueType.getTypePtr();
	if (TypePointer && TypePointer->getTypeClass() == TypeClass::type_reference)
		TypePointer = static_cast<const ReferenceType*>(TypePointer)->getPointeeType().getTypePtr();
	if (!TypePointer) return nullptr;
	IdentifierInfo* Name{};
	if (TypePointer->getTypeClass() == TypeClass::type_named)
		Name = static_cast<const NamedType*>(TypePointer)->getName();
	else if (TypePointer->getTypeClass() == TypeClass::type_template_specialization)
		Name = static_cast<const TemplateSpecializationType*>(TypePointer)->getName();
	else return nullptr;
	auto Position = Records.find(Name);
	return Position == Records.end() ? nullptr : Position->second;
}

FieldDecl* Sema::lookupField(RecordDecl* Record, IdentifierInfo* Name) const {
	for (Decl* Declaration = Record->declsBegin(); Declaration; Declaration = Declaration->getNextDeclInContext()) {
		if (Declaration->getKind() != DeclKind::decl_field) continue;
		auto Field = static_cast<FieldDecl*>(Declaration);
		if (Field->getIdentifier() != Name) continue;
		return Field;
	}
	auto Base = recordForType(Record->getBaseType());
	return Base ? lookupField(Base, Name) : nullptr;
}

Attr* Sema::processAttributes(const ParsedAttributes& Parsed) {
	Attr* First{};
	Attr* Last{};
	for (const auto& Attribute : Parsed.attributes()) {
		std::vector<AttrToken> Tokens;
		for (const auto& ParsedToken : Attribute.Tokens) {
			AttrToken Value{};
			Value.Kind = ParsedToken.getKind();
			if (ParsedToken.getKind() == tok::identifier ||
				(ParsedToken.getKind() >= tok::kw_import && ParsedToken.getKind() <= tok::kw_typename))
				Value.Identifier = ParsedToken.getIdentifierInfo();
			if (ParsedToken.getKind() == tok::numeric_literal || ParsedToken.getKind() == tok::string_literal) {
				Value.LiteralLength = ParsedToken.getLength();
				Value.LiteralData = ParsedToken.getLiteralData();
			}
			Tokens.push_back(Value);
		}
		auto Result = Context.create<Attr>(Attribute.Name, Context.copyArray(Tokens), static_cast<unsigned>(Tokens.size()), Attribute.Range);
		if (Last) Last->setNextAttr(Result); else First = Result;
		Last = Result;
	}
	return First;
}

RecordDecl* Sema::actOnStartRecord(DeclContext* DeclContext, IdentifierInfo* Name, SourceLocation Location,
	bool Complete, bool Internal, bool Exported, const ParsedAttributes& Attributes) {
	if (auto Existing = Records.find(Name); Existing != Records.end()) {
		Diagnostics.report(DiagnosticLevel::diagnostic_error, {Location, Location}, "redefinition of record type");
		return Existing->second;
	}
	auto Result = Context.create<RecordDecl>(DeclContext, Location, Name, Complete, Internal, Exported);
	Result->setImplicit(ParsingCore);
	if (ParsingCore && Name && Name->getName() == "Position") Result->setBuiltinPosition();
	Result->setAttrs(processAttributes(Attributes));
	DeclContext->addDecl(Result);
	Types[Name] = Context.getNamedType(Name);
	Records[Name] = Result;
	return Result;
}

FieldDecl* Sema::actOnField(RecordDecl* Record, const Declarator& D, const ParsedAttributes& Attributes) {
	auto Result = Context.create<FieldDecl>(Record, D.Location, D.Name, actOnType(D.Type));
	Result->setAttrs(processAttributes(Attributes));
	Record->addDecl(Result);
	return Result;
}

VarDecl* Sema::actOnVariable(DeclContext* DeclContext, const DeclSpec& DS, const Declarator& D, Expr* Init,
	const ParsedAttributes& Attributes) {
	auto Result = Context.create<VarDecl>(DeclKind::decl_variable, DeclContext, D.Location, D.Name, actOnType(D.Type),
		DS.Storage, DS.Constant, DS.Internal, DS.Exported);
	Result->setImplicit(ParsingCore);
	if (Init) {
		if (RecordDecl* Record = recordForType(Result->getType()); Record && !Record->isCompleteDefinition())
			Diagnostics.report(DiagnosticLevel::diagnostic_error, {D.Location, D.Location}, "an incomplete record variable cannot be initialized");
	}
	applyContextualType(Init, Result->getType());
	if (Init && (!Init->getType() || Init->getType().getTypePtr() != Result->getType().getTypePtr()))
		Diagnostics.report(DiagnosticLevel::diagnostic_error, {D.Location, D.Location}, "variable initializer type does not match declared type");
	Result->setInit(Init);
	Result->setAttrs(processAttributes(Attributes));
	DeclContext->addDecl(Result);
	Values[D.Name] = Result;
	return Result;
}

VarDecl* Sema::actOnLocalVariable(const DeclSpec& DS, const Declarator& D, Expr* Init) {
	if (!CurrentFunction) {
		Diagnostics.report(DiagnosticLevel::diagnostic_error, {D.Location, D.Location},
			"a local variable declaration is only valid inside a function");
		return nullptr;
	}
	auto Result = Context.create<VarDecl>(DeclKind::decl_variable, CurrentFunction, D.Location, D.Name, actOnType(D.Type),
		StorageClass::storage_ordinary, DS.Constant, false, false);
	applyContextualType(Init, Result->getType());
	if (Init && (!Init->getType() || Init->getType().getTypePtr() != Result->getType().getTypePtr()))
		Diagnostics.report(DiagnosticLevel::diagnostic_error, {D.Location, D.Location}, "variable initializer type does not match declared type");
	Result->setInit(Init);
	bindLocal(D.Name, Result);
	CurrentFunction->addDecl(Result);
	return Result;
}

void Sema::applyContextualType(Expr* Expression, QualType Type) {
	if (!Expression || !Type) return;
	const rtsl::Type* ValueType = Type.getTypePtr();
	const bool IntegerContext = ValueType && ValueType->getTypeClass() == TypeClass::type_builtin &&
		(static_cast<const BuiltinType*>(ValueType)->getKind() == BuiltinTypeKind::builtin_i32 ||
			static_cast<const BuiltinType*>(ValueType)->getKind() == BuiltinTypeKind::builtin_u32 ||
			static_cast<const BuiltinType*>(ValueType)->getKind() == BuiltinTypeKind::builtin_usize);
	if (!IntegerContext) return;
	switch (Expression->getStmtClass()) {
	case StmtClass::expr_integer:
		if (!Expression->getType()) Expression->setType(Type);
		break;
	case StmtClass::expr_unary:
		applyContextualType(static_cast<UnaryExpr*>(Expression)->getOperand(), Type);
		if (!Expression->getType()) Expression->setType(Type);
		break;
	case StmtClass::expr_binary: {
		auto Binary = static_cast<BinaryExpr*>(Expression);
		applyContextualType(Binary->getLeft(), Type);
		applyContextualType(Binary->getRight(), Type);
		if (!Expression->getType()) Expression->setType(Type);
		break;
	}
	default:
		break;
	}
}

void Sema::bindLocal(IdentifierInfo* Name, ValueDecl* Declaration) {
	if (LocalScopes.empty()) return;
	auto& Scope = LocalScopes.back();
	if (Scope.contains(Name)) {
		Diagnostics.report(DiagnosticLevel::diagnostic_error, {}, "redefinition of local identifier");
		return;
	}
	Scope[Name] = Declaration;
}

ValueDecl* Sema::lookupValue(IdentifierInfo* Name) const {
	for (auto Scope = LocalScopes.rbegin(); Scope != LocalScopes.rend(); ++Scope)
		if (auto Position = Scope->find(Name); Position != Scope->end()) return Position->second;
	auto Position = Values.find(Name);
	return Position == Values.end() ? nullptr : Position->second;
}

ParmVarDecl* Sema::actOnParameter(DeclContext* DeclContext, const Declarator& D, const ParsedAttributes& Attributes) {
	auto Result = Context.create<ParmVarDecl>(DeclContext, D.Location, D.Name, actOnType(D.Type));
	Result->setAttrs(processAttributes(Attributes));
	return Result;
}

void Sema::pushTemplateParameters(const std::vector<ParsedTemplateParameter>& Parameters) {
	for (const auto& Parameter : Parameters) {
		if (!Parameter.IsType) {
			TemplateValueParameters.emplace(Parameter.Name, actOnType(Parameter.ValueType));
			continue;
		}
		if (Types.contains(Parameter.Name)) {
			Diagnostics.report(DiagnosticLevel::diagnostic_error, {}, "template parameter shadows an existing type");
			continue;
		}
		Types.emplace(Parameter.Name, Context.getTemplateParameterType(Parameter.Name));
	}
}

void Sema::popTemplateParameters(const std::vector<ParsedTemplateParameter>& Parameters) {
	for (const auto& Parameter : Parameters) {
		if (!Parameter.IsType) { TemplateValueParameters.erase(Parameter.Name); continue; }
		auto Position = Types.find(Parameter.Name);
		if (Position != Types.end() && Position->second.getTypePtr()->getTypeClass() == TypeClass::type_template_parameter)
			Types.erase(Position);
	}
}

FunctionDecl* Sema::actOnFunction(DeclContext* LocalContext, const DeclSpec& DS, const Declarator& D,
	const std::vector<ParmVarDecl*>& Parameters, const std::vector<ParsedParameterContract>& ParsedContracts,
	Expr* BaseInitializer, const ParsedAttributes& Attributes, const std::vector<ParsedTemplateParameter>& TemplateParameters,
	const std::vector<ParsedType>& ParsedTypeOnlyParameters) {
	DeclContext* FunctionContext = LocalContext;
	if (D.EnclosingName) {
		auto Enclosing = Records.find(D.EnclosingName);
		if (Enclosing == Records.end()) {
			Diagnostics.report(DiagnosticLevel::diagnostic_error, {D.EnclosingLocation, D.EnclosingLocation},
				"qualified function owner does not name a declared record");
			return nullptr;
		}
		FunctionContext = Enclosing->second;
	}
	RecordDecl* Owner{};
	for (const auto& [Name, Record] : Records)
		if (static_cast<DeclContext*>(Record) == FunctionContext) { Owner = Record; break; }
	const bool HasImplicitObject = Owner && D.Name && D.Name != Owner->getIdentifier();
	std::vector<ParmVarDecl*> EffectiveParameters = Parameters;
	if (HasImplicitObject) EffectiveParameters.insert(EffectiveParameters.begin(), Context.create<ParmVarDecl>(FunctionContext, D.Location,
		&Identifiers.get("__self"), Context.getNamedType(Owner->getIdentifier())));
	std::vector<ParameterContract> Contracts;
	for (const ParsedParameterContract& Parsed : ParsedContracts) {
		if (Parsed.ParameterIndex >= Parameters.size()) {
			Diagnostics.report(DiagnosticLevel::diagnostic_error, {}, "parameter contract refers to an invalid parameter");
			continue;
		}
		QualType Current = Parameters[Parsed.ParameterIndex]->getType();
		bool ValidPath = true;
		for (IdentifierInfo* Member : Parsed.MemberPath) {
			auto Record = recordForType(Current);
			auto Field = Record ? lookupField(Record, Member) : nullptr;
			if (Field) {
				Current = Field->getType();
				continue;
			}
			Diagnostics.report(DiagnosticLevel::diagnostic_error, {}, "parameter contract refers to an unknown member");
			ValidPath = false;
			break;
		}
		if (!ValidPath || !Parsed.Contract) continue;
		Contracts.push_back({
			.ParameterIndex = Parsed.ParameterIndex,
			.MemberPath = Context.copyPointerArray(Parsed.MemberPath),
			.MemberPathLength = static_cast<unsigned>(Parsed.MemberPath.size()),
			.Contract = Parsed.Contract,
		});
	}
	std::vector<QualType> TypeOnlyParameters;
	for (const ParsedType& Type : ParsedTypeOnlyParameters) TypeOnlyParameters.push_back(actOnType(Type));
	const QualType ReturnType = actOnType(D.Type);
	std::vector<TemplateParameter> TemplateParameterValues;
	for (const auto& Parameter : TemplateParameters) TemplateParameterValues.push_back({.Name = Parameter.Name,
		.ValueType = Parameter.IsType ? QualType{} : actOnType(Parameter.ValueType), .IsType = Parameter.IsType, .Constraint = Parameter.Constraint});
	std::vector<TemplateArgument> TemplateArgumentValues;
	for (const auto& Argument : D.TemplateArguments)
		TemplateArgumentValues.push_back({.Type = actOnType(Argument), .IntegerValue = Argument.IntegerValue});
	if (D.EnclosingName) {
		for (Decl* Declaration = FunctionContext->declsBegin(); Declaration; Declaration = Declaration->getNextDeclInContext()) {
			if (Declaration->getKind() != DeclKind::decl_function) continue;
			auto* Existing = static_cast<FunctionDecl*>(Declaration);
			if (Existing->getIdentifier() != D.Name) continue;
			bool Matches = Existing->getType().getTypePtr() == ReturnType.getTypePtr() &&
				Existing->getNumParams() == EffectiveParameters.size() &&
				Existing->getNumTypeOnlyParameters() == TypeOnlyParameters.size() &&
				Existing->getNumTemplateParameters() == TemplateParameters.size() &&
				Existing->getNumTemplateArguments() == TemplateArgumentValues.size();
			for (unsigned Index = 0; Matches && Index < Existing->getNumTemplateArguments(); ++Index)
				Matches = Existing->templateArguments()[Index].Type == TemplateArgumentValues[Index].Type &&
					Existing->templateArguments()[Index].IntegerValue == TemplateArgumentValues[Index].IntegerValue;
			for (unsigned Index = 0; Matches && Index < Existing->getNumParams(); ++Index)
				Matches = Existing->parameters()[Index]->getIdentifier() == EffectiveParameters[Index]->getIdentifier() &&
					Existing->parameters()[Index]->getType().getTypePtr() == EffectiveParameters[Index]->getType().getTypePtr();
			for (unsigned Index = 0; Matches && Index < Existing->getNumTypeOnlyParameters(); ++Index)
				Matches = Existing->typeOnlyParameters()[Index].getTypePtr() == TypeOnlyParameters[Index].getTypePtr();
			if (!Matches) {
				Diagnostics.report(DiagnosticLevel::diagnostic_error, {D.Location, D.Location},
					"out-of-line function definition does not match its declaration");
				return nullptr;
			}
			if (Existing->getBody()) {
				Diagnostics.report(DiagnosticLevel::diagnostic_error, {D.Location, D.Location}, "redefinition of function");
				return nullptr;
			}
			std::unordered_map<ValueDecl*, ValueDecl*> Rebindings;
			for (unsigned Index = 0; Index < Existing->getNumParams(); ++Index)
				if (!HasImplicitObject || Index != 0)
					Rebindings.emplace(Parameters[HasImplicitObject ? Index - 1 : Index], Existing->parameters()[Index]);
			rebindDeclarationReferences(BaseInitializer, Rebindings);
			Existing->setBaseInitializer(BaseInitializer);
			return Existing;
		}
		Diagnostics.report(DiagnosticLevel::diagnostic_error, {D.Location, D.Location},
			"out-of-line function definition does not name a declared member");
		return nullptr;
	}
	if (!D.TemplateArguments.empty()) {
		for (Decl* Declaration = FunctionContext->declsBegin(); Declaration; Declaration = Declaration->getNextDeclInContext()) {
			if (Declaration->getKind() != DeclKind::decl_function) continue;
			auto* Existing = static_cast<FunctionDecl*>(Declaration);
			if (Existing->getIdentifier() != D.Name || Existing->getType() != ReturnType ||
				Existing->getNumParams() != EffectiveParameters.size() || Existing->getNumTemplateArguments() != TemplateArgumentValues.size()) continue;
			bool Matches = true;
			for (unsigned Index = 0; Matches && Index < Existing->getNumParams(); ++Index)
				Matches = Existing->parameters()[Index]->getType() == EffectiveParameters[Index]->getType();
			for (unsigned Index = 0; Matches && Index < Existing->getNumTemplateArguments(); ++Index)
				Matches = Existing->templateArguments()[Index].Type == TemplateArgumentValues[Index].Type &&
					Existing->templateArguments()[Index].IntegerValue == TemplateArgumentValues[Index].IntegerValue;
			if (!Matches) continue;
			if (Existing->getBody()) {
				Diagnostics.report(DiagnosticLevel::diagnostic_error, {D.Location, D.Location}, "redefinition of function template specialization");
				return nullptr;
			}
			return Existing;
		}
	}
	auto Result = Context.create<FunctionDecl>(FunctionContext, D.Location, D.Name, ReturnType,
		Context.copyPointerArray(EffectiveParameters), static_cast<unsigned>(EffectiveParameters.size()), Context.copyArray(Contracts),
		static_cast<unsigned>(Contracts.size()), Context.copyArray(TemplateParameterValues),
		static_cast<unsigned>(TemplateParameterValues.size()), Context.copyArray(TemplateArgumentValues),
		static_cast<unsigned>(TemplateArgumentValues.size()), Context.copyArray(TypeOnlyParameters),
		static_cast<unsigned>(TypeOnlyParameters.size()), BaseInitializer, D.Emits, DS.Internal, DS.Exported);
	Result->setImplicit(ParsingCore);
	Result->setImplicitObject(HasImplicitObject);
	Result->setAttrs(processAttributes(Attributes));
	FunctionContext->addDecl(Result);
	Functions[D.Name].push_back(Result);
	if (!Values.contains(D.Name)) Values[D.Name] = Result;
	for (const auto& [Name, Record] : Records)
		if (static_cast<DeclContext*>(Record) == FunctionContext && Name == D.Name) Constructors[D.Name] = Result;
	for (auto Parameter : EffectiveParameters) Result->addDecl(Parameter);
	return Result;
}

TypeAliasDecl* Sema::actOnTypeAlias(DeclContext* DeclContext, IdentifierInfo* Name, SourceLocation Location,
	const DeclSpec& DS, const ParsedType& Type, const ParsedAttributes& Attributes) {
	auto AliasedType = actOnType(Type);
	auto Result = Context.create<TypeAliasDecl>(DeclContext, Location, Name, AliasedType, DS.Internal, DS.Exported);
	Result->setImplicit(ParsingCore);
	Result->setAttrs(processAttributes(Attributes));
	DeclContext->addDecl(Result);
	Types[Name] = AliasedType;
	return Result;
}

ImportDecl* Sema::actOnImport(DeclContext* DeclContext, const Token& ModuleToken) {
	auto Data = ModuleToken.getLiteralData();
	unsigned Length = ModuleToken.getLength();
	if (Length >= 2) { ++Data; Length -= 2; }
	auto Result = Context.create<ImportDecl>(DeclContext, ModuleToken.getLocation(), Data, Length, ImportDecl::Kind::import_file);
	DeclContext->addDecl(Result);
	return Result;
}

ImportDecl* Sema::actOnLibraryImport(DeclContext* DeclContext, const Token& LibraryToken) {
	auto Name = LibraryToken.getIdentifierInfo()->getName();
	auto Result = Context.create<ImportDecl>(DeclContext, LibraryToken.getLocation(), Name.data(),
		static_cast<unsigned>(Name.size()), ImportDecl::Kind::import_library);
	DeclContext->addDecl(Result);
	return Result;
}

void Sema::addRecordFieldsToFunctionScope(RecordDecl* Record) {
	if (RecordDecl* Base = recordForType(Record->getBaseType())) addRecordFieldsToFunctionScope(Base);
	for (Decl* Declaration = Record->declsBegin(); Declaration; Declaration = Declaration->getNextDeclInContext()) {
		if (Declaration->getKind() != DeclKind::decl_field) continue;
		auto Field = static_cast<FieldDecl*>(Declaration);
		bindLocal(Field->getIdentifier(), Field);
	}
}

void Sema::actOnStartFunctionBody(FunctionDecl* Function) {
	CurrentFunction = Function;
	enterScope();
	for (unsigned Index = 0; Index < Function->getNumParams(); ++Index)
		bindLocal(Function->parameters()[Index]->getIdentifier(), Function->parameters()[Index]);
	for (const auto& [Name, Record] : Records)
		if (static_cast<DeclContext*>(Record) == Function->getDeclContext()) {
			addRecordFieldsToFunctionScope(Record);
			break;
		}
}

void Sema::actOnStartFunctionSignature(const std::vector<ParmVarDecl*>& Parameters) {
	enterScope();
	for (ParmVarDecl* Parameter : Parameters) bindLocal(Parameter->getIdentifier(), Parameter);
}

void Sema::actOnFinishFunctionSignature() { exitScope(); }

void Sema::actOnFinishFunctionBody(FunctionDecl* Function, CompoundStmt* Body) {
	Function->setBody(Body);
	if (CurrentFunction == Function) {
		exitScope();
		CurrentFunction = nullptr;
	}
}

void Sema::enterScope() { LocalScopes.emplace_back(); }

void Sema::exitScope() {
	if (!LocalScopes.empty()) LocalScopes.pop_back();
}

CompoundStmt* Sema::actOnCompoundStmt(const std::vector<Stmt*>& Statements) {
	return Context.create<CompoundStmt>(Context.copyPointerArray(Statements), static_cast<unsigned>(Statements.size()));
}

Stmt* Sema::actOnReturnStmt(Expr* Value) {
	if (!CurrentFunction) {
		Diagnostics.report(DiagnosticLevel::diagnostic_error, {}, "return statement is only valid inside a function");
		return Context.create<ValueStmt>(StmtClass::stmt_return, Value);
	}
	QualType ReturnType = CurrentFunction->getType();
	applyContextualType(Value, ReturnType);
	if (Value && Value->getType() && Value->getType().getTypePtr() != ReturnType.getTypePtr())
		Diagnostics.report(DiagnosticLevel::diagnostic_error, {}, "return expression type does not match function return type");
	return Context.create<ValueStmt>(StmtClass::stmt_return, Value);
}

Expr* Sema::actOnIdentifierExpr(IdentifierInfo* Name, SourceLocation Location) {
	if (Name == ReturnEmitter) return actOnCurrentEmitterExpr(Location);
	if (ValueDecl* Declaration = lookupValue(Name)) {
		auto Result = Context.create<DeclRefExpr>(Declaration);
		Result->setType(Declaration->getType());
		return Result;
	}
	if (auto TypePosition = Types.find(Name); TypePosition != Types.end()) return Context.create<TypeExpr>(TypePosition->second);
	Diagnostics.report(DiagnosticLevel::diagnostic_error, {Location, Location}, "use of undeclared identifier");
	return nullptr;
}

Expr* Sema::actOnCurrentEmitterExpr(SourceLocation Location) {
	if (!CurrentFunction) {
		Diagnostics.report(DiagnosticLevel::diagnostic_error, {Location, Location},
			"the return emitter is only available inside a function body");
		return nullptr;
	}
	const Type* ReturnType = CurrentFunction->getType().getTypePtr();
	if (!ReturnType || ReturnType->getTypeClass() != TypeClass::type_template_specialization ||
		static_cast<const TemplateSpecializationType*>(ReturnType)->getName()->getName() != "triangle_strip") {
		Diagnostics.report(DiagnosticLevel::diagnostic_error, {Location, Location}, "the return emitter requires a geometry primitive return type");
		return nullptr;
	}
	return Context.create<EmitterExpr>(CurrentFunction, CurrentFunction->getType());
}

FunctionDecl* Sema::lookupMemberFunction(RecordDecl* Record, IdentifierInfo* Name) const {
	for (Decl* Declaration = Record->declsBegin(); Declaration; Declaration = Declaration->getNextDeclInContext()) {
		if (Declaration->getKind() != DeclKind::decl_function) continue;
		auto* Function = static_cast<FunctionDecl*>(Declaration);
		if (Function->getIdentifier() == Name) return Function;
	}
	auto Base = recordForType(Record->getBaseType());
	return Base ? lookupMemberFunction(Base, Name) : nullptr;
}

bool Sema::isFunctionName(IdentifierInfo* Name) const {
	return Name != nullptr && Functions.contains(Name);
}

Expr* Sema::actOnTemplateIdentifierExpr(IdentifierInfo* Name, const std::vector<ParsedType>& Arguments, SourceLocation Location) {
	auto Position = Functions.find(Name);
	if (Position == Functions.end()) {
		Diagnostics.report(DiagnosticLevel::diagnostic_error, {Location, Location}, "use of undeclared function template");
		return nullptr;
	}
	std::vector<TemplateArgument> Resolved;
	for (const auto& Argument : Arguments) Resolved.push_back({.Type = actOnType(Argument), .IntegerValue = Argument.IntegerValue});
	for (FunctionDecl* Function : Position->second) {
		if (Function->getNumTemplateArguments() != Resolved.size()) continue;
		bool Matches = true;
		for (unsigned Index = 0; Index < Function->getNumTemplateArguments(); ++Index)
			Matches = Matches && Function->templateArguments()[Index].Type == Resolved[Index].Type &&
				Function->templateArguments()[Index].IntegerValue == Resolved[Index].IntegerValue;
		if (!Matches) continue;
		auto Result = Context.create<DeclRefExpr>(Function);
		Result->setType(Function->getType());
		return Result;
	}
	for (FunctionDecl* Function : Position->second) {
		if (Function->getNumTemplateParameters() != Resolved.size()) continue;
		bool Allowed = true;
		for (unsigned Index = 0; Index < Function->getNumTemplateParameters(); ++Index) {
			const auto& Parameter = Function->templateParameters()[Index];
			Allowed = Allowed && (!Parameter.Constraint || *Parameter.Constraint);
			Allowed = Allowed && (Parameter.IsType ? !Resolved[Index].IntegerValue : Resolved[Index].IntegerValue.has_value());
		}
		if (!Allowed) continue;
		Function = instantiateFunction(Function, Resolved);
		auto Result = Context.create<DeclRefExpr>(Function);
		Result->setType(Function->getType());
		return Result;
	}
	Diagnostics.report(DiagnosticLevel::diagnostic_error, {Location, Location}, "function template specialization is not declared");
	return nullptr;
}

QualType Sema::substituteType(QualType Type, const FunctionDecl* Pattern, const std::vector<TemplateArgument>& Arguments) const {
	if (!Type) return {};
	const rtsl::Type* Value = Type.getTypePtr();
	if (Value->getTypeClass() == TypeClass::type_template_parameter) {
		auto* Name = static_cast<const TemplateParameterType*>(Value)->getName();
		for (unsigned Index = 0; Index < Pattern->getNumTemplateParameters(); ++Index)
			if (Pattern->templateParameters()[Index].IsType && Pattern->templateParameters()[Index].Name == Name)
				return Arguments[Index].Type;
	}
	if (Value->getTypeClass() == TypeClass::type_reference) return Context.getReferenceType(substituteType(static_cast<const ReferenceType*>(Value)->getPointeeType(), Pattern, Arguments));
	if (Value->getTypeClass() == TypeClass::type_pointer) return Context.getPointerType(substituteType(static_cast<const PointerType*>(Value)->getPointeeType(), Pattern, Arguments));
	return Type;
}

FunctionDecl* Sema::instantiateFunction(FunctionDecl* Pattern, const std::vector<TemplateArgument>& Arguments) {
	for (FunctionDecl* Candidate : Functions[Pattern->getIdentifier()]) {
		if (Candidate->getTemplatePattern() != Pattern || Candidate->getNumTemplateArguments() != Arguments.size()) continue;
		bool Same = true;
		for (unsigned Index = 0; Index < Arguments.size(); ++Index) Same = Same && Candidate->templateArguments()[Index].Type == Arguments[Index].Type && Candidate->templateArguments()[Index].IntegerValue == Arguments[Index].IntegerValue;
		if (Same) return Candidate;
	}
	std::vector<ParmVarDecl*> Parameters;
	for (unsigned Index = 0; Index < Pattern->getNumParams(); ++Index) {
		auto* Source = Pattern->parameters()[Index];
		Parameters.push_back(Context.create<ParmVarDecl>(nullptr, Source->getLocation(), Source->getIdentifier(), substituteType(Source->getType(), Pattern, Arguments)));
	}
	auto* Result = Context.create<FunctionDecl>(Pattern->getDeclContext(), Pattern->getLocation(), Pattern->getIdentifier(),
		substituteType(Pattern->getType(), Pattern, Arguments), Context.copyPointerArray(Parameters), static_cast<unsigned>(Parameters.size()),
		nullptr, 0, nullptr, 0, Context.copyArray(Arguments), static_cast<unsigned>(Arguments.size()), nullptr, 0,
		nullptr, Pattern->hasImplicitEmitter(), Pattern->hasInternalLinkage(), Pattern->isExported());
	Result->setImplicit(Pattern->isImplicit());
	Result->setBody(Pattern->getBody()); Result->setTemplatePattern(Pattern); Pattern->getDeclContext()->addDecl(Result);
	Functions[Pattern->getIdentifier()].push_back(Result);
	return Result;
}

Expr* Sema::actOnMemberExpr(Expr* Base, IdentifierInfo* Member, SourceLocation Location) {
	if (!Base) return nullptr;
	const Type* BaseType = Base->getType().getTypePtr();
	if (BaseType && BaseType->getTypeClass() == TypeClass::type_reference)
		BaseType = static_cast<const ReferenceType*>(BaseType)->getPointeeType().getTypePtr();
	if (BaseType && BaseType->getTypeClass() == TypeClass::type_template_specialization && Member) {
		const auto* Patch = static_cast<const TemplateSpecializationType*>(BaseType);
		const std::string_view Name = Patch->getName()->getName();
		if ((Name == "patch" || Name == "triangle_patch" || Name == "quad_patch" || Name == "isoline_patch" || Name == "triangle") &&
			Patch->getArgumentCount() != 0) {
			auto Result = Context.create<PostfixExpr>(StmtClass::expr_member, Base, Member, nullptr, 0);
			if (Member->getName() == "current") Result->setType(Patch->arguments()[0]);
			else if (Member->getName() == "outer" || Member->getName() == "coordinate")
				Result->setType(Context.getBuiltinType(BuiltinTypeKind::builtin_f32));
			else Result = nullptr;
			if (Result) return Result;
		}
	}
	if (BaseType && BaseType->getTypeClass() == TypeClass::type_named && Member) {
		auto Name = static_cast<const NamedType*>(BaseType)->getName()->getName();
		if (Name.starts_with("__")) Name.remove_prefix(2);
		auto Component = Member->getName();
		if ((Name == "vec2" || Name == "vec3" || Name == "vec4") && !Component.empty() && Component.size() <= 4 &&
			std::ranges::all_of(Component, [](char Character) {
				return Character == 'x' || Character == 'y' || Character == 'z' || Character == 'w';
			}) &&
			std::ranges::all_of(Component, [&](char Character) {
				const unsigned Index = Character == 'x' ? 0 : Character == 'y' ? 1 : Character == 'z' ? 2 : 3;
				return Index < static_cast<unsigned>(Name.back() - '0');
			})) {
			auto Result = Context.create<PostfixExpr>(StmtClass::expr_member, Base, Member, nullptr, 0);
			if (Component.size() == 1) Result->setType(Context.getBuiltinType(BuiltinTypeKind::builtin_f32));
			else for (const auto& [VectorName, VectorType] : Types)
				if (VectorName->getName() == std::string("vec") + std::to_string(Component.size())) {
					Result->setType(VectorType);
					break;
				}
			return Result;
		}
	}
	auto Record = recordForType(Base->getType());
	if (!Record) {
		Diagnostics.report(DiagnosticLevel::diagnostic_error, {Location, Location}, "member access requires a structure value");
		return nullptr;
	}
	auto Field = lookupField(Record, Member);
	if (Field) {
		auto Result = Context.create<PostfixExpr>(StmtClass::expr_member, Base, Member, nullptr, 0);
		Result->setType(Field->getType());
		return Result;
	}
	if (auto* Function = lookupMemberFunction(Record, Member)) {
		auto Result = Context.create<PostfixExpr>(StmtClass::expr_member, Base, Member, nullptr, 0);
		Result->setType(Function->getType());
		return Result;
	}
	Diagnostics.report(DiagnosticLevel::diagnostic_error, {Location, Location}, "structure has no such member");
	return nullptr;
}

Expr* Sema::actOnSubscriptExpr(Expr* Base, Expr* Index, SourceLocation Location) {
	if (!Base || !Index) return nullptr;
	if (Base->getStmtClass() == StmtClass::expr_member) {
		auto Member = static_cast<PostfixExpr*>(Base);
		if (Member->getMember() && Member->getMember()->getName() == "outer") {
			applyContextualType(Index, Context.getBuiltinType(BuiltinTypeKind::builtin_u32));
			auto Result = Context.create<PostfixExpr>(StmtClass::expr_subscript, Base, nullptr,
				Context.copyPointerArray(std::vector<Expr*>{Index}), 1);
			Result->setType(Context.getBuiltinType(BuiltinTypeKind::builtin_f32));
			return Result;
		}
	}
	const Type* BaseType = Base->getType().getTypePtr();
	if (BaseType && BaseType->getTypeClass() == TypeClass::type_reference)
		BaseType = static_cast<const ReferenceType*>(BaseType)->getPointeeType().getTypePtr();
	if (BaseType && BaseType->getTypeClass() == TypeClass::type_template_specialization) {
		const auto* Patch = static_cast<const TemplateSpecializationType*>(BaseType);
		const std::string_view Name = Patch->getName()->getName();
		if ((Name == "patch" || Name == "triangle_patch" || Name == "quad_patch" || Name == "isoline_patch" || Name == "triangle") &&
			Patch->getArgumentCount() != 0) {
			applyContextualType(Index, Context.getBuiltinType(BuiltinTypeKind::builtin_u32));
			auto Result = Context.create<PostfixExpr>(StmtClass::expr_subscript, Base, nullptr,
				Context.copyPointerArray(std::vector<Expr*>{Index}), 1);
			Result->setType(Patch->arguments()[0]);
			return Result;
		}
	}
	Diagnostics.report(DiagnosticLevel::diagnostic_error, {Location, Location}, "subscript requires a patch or primitive value");
	return nullptr;
}

Expr* Sema::actOnCallExpr(Expr* Callee, const std::vector<Expr*>& Arguments, SourceLocation Location) {
	if (!Callee) return nullptr;
	if (Callee->getStmtClass() == StmtClass::expr_member) {
		auto* Member = static_cast<PostfixExpr*>(Callee);
		auto* Record = recordForType(Member->getBase()->getType());
		auto* Function = Record && Member->getMember() ? lookupMemberFunction(Record, Member->getMember()) : nullptr;
		if (!Function) {
			Diagnostics.report(DiagnosticLevel::diagnostic_error, {Location, Location}, "member expression is not callable");
			return nullptr;
		}
		if (Function->getNumParams() != Arguments.size() + 1) {
			Diagnostics.report(DiagnosticLevel::diagnostic_error, {Location, Location}, "member function call has the wrong number of arguments");
			return nullptr;
		}
		RecordDecl* Owner{};
		for (const auto& [Name, Candidate] : Records)
			if (static_cast<DeclContext*>(Candidate) == Function->getDeclContext()) { Owner = Candidate; break; }
		if (!Owner) return nullptr;
		Expr* Receiver = Member->getBase();
		if (Receiver->getType() != Context.getNamedType(Owner->getIdentifier())) {
			std::vector<Expr*> Fields;
			for (Decl* Declaration = Owner->declsBegin(); Declaration; Declaration = Declaration->getNextDeclInContext())
				if (Declaration->getKind() == DeclKind::decl_field) {
					auto* Field = static_cast<FieldDecl*>(Declaration);
					auto* Value = actOnMemberExpr(Member->getBase(), Field->getIdentifier(), Location);
					if (!Value) return nullptr;
					Fields.push_back(Value);
				}
			Receiver = Context.create<ConstructExpr>(Context.getNamedType(Owner->getIdentifier()),
				Context.copyPointerArray(Fields), static_cast<unsigned>(Fields.size()));
		}
		std::vector<Expr*> ExpandedArguments{Receiver};
		ExpandedArguments.insert(ExpandedArguments.end(), Arguments.begin(), Arguments.end());
		auto* Target = Context.create<DeclRefExpr>(Function);
		Target->setType(Function->getType());
		return actOnCallExpr(Target, ExpandedArguments, Location);
	}
	if (Callee->getStmtClass() == StmtClass::expr_type) {
		auto Type = Callee->getType();
		if (Type.getTypePtr() && Type.getTypePtr()->getTypeClass() == TypeClass::type_named) {
			auto Name = static_cast<const NamedType*>(Type.getTypePtr())->getName();
			if (auto Constructor = Constructors.find(Name); Constructor != Constructors.end()) {
				auto Target = Context.create<DeclRefExpr>(Constructor->second);
				Target->setType(Constructor->second->getType());
				auto Result = Context.create<PostfixExpr>(StmtClass::expr_call, Target, nullptr,
					Context.copyPointerArray(Arguments), static_cast<unsigned>(Arguments.size()));
				Result->setType(Constructor->second->getType());
				return Result;
			}
		}
		return Context.create<ConstructExpr>(Type, Context.copyPointerArray(Arguments), static_cast<unsigned>(Arguments.size()));
	}
	if (Callee->getStmtClass() == StmtClass::expr_decl_ref &&
		static_cast<DeclRefExpr*>(Callee)->getDecl()->getKind() == DeclKind::decl_function) {
		auto Function = static_cast<FunctionDecl*>(static_cast<DeclRefExpr*>(Callee)->getDecl());
		if (Function->isFunctionTemplate()) {
			if (Function->getNumTemplateParameters() != 1 || Function->getNumParams() != Arguments.size()) {
				Diagnostics.report(DiagnosticLevel::diagnostic_error, {Location, Location}, "function template arguments cannot be deduced"); return nullptr;
			}
			std::vector<TemplateArgument> Deduced(1);
			bool Found = false;
			for (unsigned Index = 0; Index < Function->getNumParams(); ++Index) {
				auto Type = Function->parameters()[Index]->getType().getTypePtr();
				if (!Type || Type->getTypeClass() != TypeClass::type_template_parameter) continue;
				Deduced[0].Type = Arguments[Index]->getType(); Found = true;
			}
			if (!Found) { Diagnostics.report(DiagnosticLevel::diagnostic_error, {Location, Location}, "function template arguments cannot be deduced"); return nullptr; }
			Function = instantiateFunction(Function, Deduced);
			static_cast<DeclRefExpr*>(Callee)->setDecl(Function); Callee->setType(Function->getType());
		}
		if (Function->getNumParams() != Arguments.size()) {
			Diagnostics.report(DiagnosticLevel::diagnostic_error, {Location, Location}, "function call has the wrong number of arguments");
			return nullptr;
		}
		for (unsigned Index = 0; Index < Function->getNumParams(); ++Index) {
			applyContextualType(Arguments[Index], Function->parameters()[Index]->getType());
			if (Arguments[Index] && Arguments[Index]->getType() &&
				Arguments[Index]->getType().getTypePtr() != Function->parameters()[Index]->getType().getTypePtr())
				Diagnostics.report(DiagnosticLevel::diagnostic_error, {Location, Location}, "function argument type does not match parameter type");
		}
		auto Result = Context.create<PostfixExpr>(StmtClass::expr_call, Callee, nullptr,
			Context.copyPointerArray(Arguments), static_cast<unsigned>(Arguments.size()));
		Result->setType(static_cast<DeclRefExpr*>(Callee)->getDecl()->getType());
		return Result;
	}
	Diagnostics.report(DiagnosticLevel::diagnostic_error, {Location, Location}, "expression is not callable");
	return nullptr;
}

Expr* Sema::actOnUnaryExpr(tok::TokenKind Opcode, Expr* Operand) {
	if (!Operand) return nullptr;
	auto Result = Context.create<UnaryExpr>(Opcode, Operand);
	if (Opcode == tok::exclaim) Result->setType(Context.getBuiltinType(BuiltinTypeKind::builtin_bool));
	else Result->setType(Operand->getType());
	return Result;
}

Expr* Sema::actOnBinaryExpr(tok::TokenKind Opcode, Expr* Left, Expr* Right) {
	if (!Left || !Right) return nullptr;
	if (Opcode == tok::equal && Left->getStmtClass() == StmtClass::expr_member) {
		auto Member = static_cast<PostfixExpr*>(Left);
		if (Member->getMember() && Member->getMember()->getName() == "outer") {
			Diagnostics.report(DiagnosticLevel::diagnostic_error, {}, "tessellation outer levels require an index");
			return nullptr;
		}
	}
	if (Opcode == tok::lessminus) {
		if (Left->getStmtClass() != StmtClass::expr_emitter) {
			Diagnostics.report(DiagnosticLevel::diagnostic_error, {}, "left operand of '<-' is not an emitter");
			return nullptr;
		}
		auto Owner = recordForType(Left->getType());
		if (!Owner) { Diagnostics.report(DiagnosticLevel::diagnostic_error, {}, "left operand of '<-' has no operator members"); return nullptr; }
		FunctionDecl* ExactPattern{};
		FunctionDecl* DeducedPattern{};
		for (Decl* Declaration = Owner->declsBegin(); Declaration; Declaration = Declaration->getNextDeclInContext()) {
			if (Declaration->getKind() != DeclKind::decl_function) continue;
			auto* Candidate = static_cast<FunctionDecl*>(Declaration);
			if (Candidate->getIdentifier() != &Identifiers.get("operator<-")) continue;
			if (Candidate->getNumParams() != 2) continue;
			const Type* Second = Candidate->parameters()[1]->getType().getTypePtr();
			if (!Second) continue;
			if (Second->getTypeClass() == TypeClass::type_template_parameter) {
				auto* Primitive = Left->getType().getTypePtr() &&
					Left->getType().getTypePtr()->getTypeClass() == TypeClass::type_template_specialization
					? static_cast<const TemplateSpecializationType*>(Left->getType().getTypePtr()) : nullptr;
				if (Primitive && Primitive->getArgumentCount() != 0 &&
					Primitive->arguments()[0].getTypePtr() == Right->getType().getTypePtr()) DeducedPattern = Candidate;
			} else if (Second == Right->getType().getTypePtr()) {
				ExactPattern = Candidate;
			}
		}
		FunctionDecl* Pattern = ExactPattern ? ExactPattern : DeducedPattern;
		if (!Pattern) { Diagnostics.report(DiagnosticLevel::diagnostic_error, {}, "no '<-' overload accepts this emitted value"); return nullptr; }
		std::vector<ParmVarDecl*> Parameters;
		Parameters.push_back(Context.create<ParmVarDecl>(nullptr, SourceLocation{},
			&Identifiers.get("value"), Left->getType()));
		Parameters.push_back(Context.create<ParmVarDecl>(nullptr, SourceLocation{}, &Identifiers.get("emitted"), Right->getType()));
		auto* Function = Context.create<FunctionDecl>(Owner, SourceLocation{}, Pattern->getIdentifier(), Left->getType(),
			Context.copyPointerArray(Parameters), 2, nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0, nullptr, false, false, false);
		Function->setImplicit(Pattern->isImplicit());
		Owner->addDecl(Function); Functions[Function->getIdentifier()].push_back(Function);
		Function->addDecl(Parameters[0]); Function->addDecl(Parameters[1]);
		auto* Callee = Context.create<DeclRefExpr>(Function); Callee->setType(Function->getType());
		auto* Result = Context.create<PostfixExpr>(StmtClass::expr_call, Callee, nullptr,
			Context.copyPointerArray(std::vector<Expr*>{Left, Right}), 2);
		Result->setType(Function->getType()); return Result;
	}
	if (Opcode == tok::equal) {
		applyContextualType(Right, Left->getType());
		if (Right->getType() && Left->getType() && Right->getType().getTypePtr() != Left->getType().getTypePtr()) {
			Diagnostics.report(DiagnosticLevel::diagnostic_error, {}, "assignment value type does not match destination type");
			return nullptr;
		}
	} else if (!Left->getType() && Right->getType()) {
		applyContextualType(Left, Right->getType());
	} else if (Left->getType() && !Right->getType()) {
		applyContextualType(Right, Left->getType());
	}
	auto Result = Context.create<BinaryExpr>(Opcode, Left, Right);
	switch (Opcode) {
	case tok::equalequal:
	case tok::exclaimequal:
	case tok::less:
	case tok::lessequal:
	case tok::greater:
	case tok::greaterequal:
	case tok::ampamp:
	case tok::pipepipe:
		Result->setType(Context.getBuiltinType(BuiltinTypeKind::builtin_bool));
		break;
	default:
		Result->setType(Left->getType());
		if (Opcode == tok::star) {
			const Type* LeftType = Left->getType().getTypePtr();
			const Type* RightType = Right->getType().getTypePtr();
				auto isNamed = [](const Type* Type, std::string_view Name) {
					return Type && Type->getTypeClass() == TypeClass::type_named &&
					static_cast<const NamedType*>(Type)->getName()->getName() == Name;
			};
			auto isVector = [&](const Type* Type) { return isNamed(Type, "vec2") || isNamed(Type, "vec3") || isNamed(Type, "vec4"); };
			auto isMatrix = [&](const Type* Type) { return isNamed(Type, "mat2") || isNamed(Type, "mat3") || isNamed(Type, "mat4"); };
			if ((isMatrix(LeftType) && isVector(RightType)) ||
				(LeftType && LeftType->getTypeClass() == TypeClass::type_builtin && isVector(RightType)))
				Result->setType(Right->getType());
		}
		break;
	}
	return Result;
}

}
