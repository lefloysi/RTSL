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
	Types[&Identifiers.get("void")] = Context.getBuiltinType(BuiltinTypeKind::builtin_void);
	Types[&Identifiers.get("bool")] = Context.getBuiltinType(BuiltinTypeKind::builtin_bool);
	Types[&Identifiers.get("i32")] = Context.getBuiltinType(BuiltinTypeKind::builtin_i32);
	Types[&Identifiers.get("u32")] = Context.getBuiltinType(BuiltinTypeKind::builtin_u32);
	Types[&Identifiers.get("usize")] = Context.getBuiltinType(BuiltinTypeKind::builtin_usize);
	Types[&Identifiers.get("f32")] = Context.getBuiltinType(BuiltinTypeKind::builtin_f32);
	for (auto Name : {"vec2", "vec3", "vec4", "mat2", "mat3", "mat4", "triangle", "triangle_strip", "patch",
		"triangle_patch", "quad_patch", "isoline_patch", "tessellation", "equal", "fractional_even", "fractional_odd",
		"cw", "ccw"}) {
		auto& II = Identifiers.get(Name);
		Types[&II] = Context.getNamedType(&II);
	}
	for (auto Name : {"buffer", "image_1d", "image_2d", "image_3d", "texture_1d", "texture_2d", "texture_3d", "sampler"}) {
		auto& II = Identifiers.get(Name);
		Types[&II] = Context.getNamedType(&II);
	}
	BufferTemplate = &Identifiers.get("buffer");
	PositionType = &Identifiers.get("Position");
	ReturnEmitter = &Identifiers.get("__return");
	Types[PositionType] = Context.getNamedType(PositionType);
	auto PositionRecord = Context.create<RecordDecl>(Context.getTranslationUnitDecl(), SourceLocation{}, PositionType,
		true, false, false, QualType{}, true);
	auto PositionMember = &Identifiers.get("position");
	auto PositionField = Context.create<FieldDecl>(PositionRecord, SourceLocation{}, PositionMember,
		Types[&Identifiers.get("vec4")]);
	PositionRecord->addDecl(PositionField);
	Context.getTranslationUnitDecl()->addDecl(PositionRecord);
	Records[PositionType] = PositionRecord;
	IdentifierInfo* SampleName = &Identifiers.get("sample");
	SampleIntrinsic = Context.create<FunctionDecl>(Context.getTranslationUnitDecl(), SourceLocation{}, SampleName,
		Types[&Identifiers.get("vec4")], nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0, nullptr, false, false, false);
	Values[SampleName] = SampleIntrinsic;
}

QualType Sema::actOnType(const ParsedType& Parsed) {
	QualType Result;
	if (!Parsed.Name) return Result;
	if (!Parsed.Arguments.empty()) {
		std::vector<QualType> Arguments;
		std::vector<std::optional<std::uint32_t>> IntegerArguments;
		for (const auto& Argument : Parsed.Arguments) {
			Arguments.push_back(actOnType(Argument));
			IntegerArguments.push_back(Argument.IntegerValue);
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
		Result = Context.getTemplateSpecializationType(Parsed.Name, Arguments, IntegerArguments);
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
	if (!TypePointer || TypePointer->getTypeClass() != TypeClass::type_named) return nullptr;
	auto Position = Records.find(static_cast<const NamedType*>(TypePointer)->getName());
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
	if (Name == PositionType) Diagnostics.report(DiagnosticLevel::diagnostic_error, {Location, Location},
		"Position is provided by the RTSL standard library and cannot be redeclared");
	auto Result = Context.create<RecordDecl>(DeclContext, Location, Name, Complete, Internal, Exported);
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

void Sema::pushTemplateParameters(const std::vector<IdentifierInfo*>& Parameters) {
	for (IdentifierInfo* Parameter : Parameters) {
		if (Types.contains(Parameter)) {
			Diagnostics.report(DiagnosticLevel::diagnostic_error, {}, "template parameter shadows an existing type");
			continue;
		}
		Types.emplace(Parameter, Context.getTemplateParameterType(Parameter));
	}
}

void Sema::popTemplateParameters(const std::vector<IdentifierInfo*>& Parameters) {
	for (IdentifierInfo* Parameter : Parameters) {
		auto Position = Types.find(Parameter);
		if (Position != Types.end() && Position->second.getTypePtr()->getTypeClass() == TypeClass::type_template_parameter)
			Types.erase(Position);
	}
}

FunctionDecl* Sema::actOnFunction(DeclContext* LocalContext, const DeclSpec& DS, const Declarator& D,
	const std::vector<ParmVarDecl*>& Parameters, const std::vector<ParsedParameterContract>& ParsedContracts,
	Expr* BaseInitializer, const ParsedAttributes& Attributes, const std::vector<IdentifierInfo*>& TemplateParameters,
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
	bool GeometryEntry = false;
	for (const ParsedAttr& Attribute : Attributes.attributes()) {
		if (!Attribute.Name || Attribute.Name->getName() != "stage" || Attribute.Tokens.empty()) continue;
		GeometryEntry = Attribute.Tokens.front().getIdentifierInfo() && Attribute.Tokens.front().getIdentifierInfo()->getName() == "geometry";
	}
	const bool GeometryEmitter = GeometryEntry && D.Type.Name && D.Type.Name->getName() == "triangle_strip";
	const QualType ReturnType = actOnType(D.Type);
	if (D.EnclosingName) {
		for (Decl* Declaration = FunctionContext->declsBegin(); Declaration; Declaration = Declaration->getNextDeclInContext()) {
			if (Declaration->getKind() != DeclKind::decl_function) continue;
			auto* Existing = static_cast<FunctionDecl*>(Declaration);
			if (Existing->getIdentifier() != D.Name) continue;
			bool Matches = Existing->getType().getTypePtr() == ReturnType.getTypePtr() &&
				Existing->getNumParams() == Parameters.size() &&
				Existing->getNumTypeOnlyParameters() == TypeOnlyParameters.size() &&
				Existing->getNumTemplateParameters() == TemplateParameters.size();
			for (unsigned Index = 0; Matches && Index < Existing->getNumParams(); ++Index)
				Matches = Existing->parameters()[Index]->getIdentifier() == Parameters[Index]->getIdentifier() &&
					Existing->parameters()[Index]->getType().getTypePtr() == Parameters[Index]->getType().getTypePtr();
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
				Rebindings.emplace(Parameters[Index], Existing->parameters()[Index]);
			rebindDeclarationReferences(BaseInitializer, Rebindings);
			Existing->setBaseInitializer(BaseInitializer);
			return Existing;
		}
		Diagnostics.report(DiagnosticLevel::diagnostic_error, {D.Location, D.Location},
			"out-of-line function definition does not name a declared member");
		return nullptr;
	}
	auto Result = Context.create<FunctionDecl>(FunctionContext, D.Location, D.Name, ReturnType,
		Context.copyPointerArray(Parameters), static_cast<unsigned>(Parameters.size()), Context.copyArray(Contracts),
		static_cast<unsigned>(Contracts.size()), Context.copyPointerArray(TemplateParameters),
		static_cast<unsigned>(TemplateParameters.size()), Context.copyArray(TypeOnlyParameters),
		static_cast<unsigned>(TypeOnlyParameters.size()), BaseInitializer, D.Emits || GeometryEmitter, DS.Internal, DS.Exported);
	Result->setAttrs(processAttributes(Attributes));
	FunctionContext->addDecl(Result);
	Values[D.Name] = Result;
	for (const auto& [Name, Record] : Records)
		if (static_cast<DeclContext*>(Record) == FunctionContext && Name == D.Name) Constructors[D.Name] = Result;
	for (auto Parameter : Parameters) Result->addDecl(Parameter);
	return Result;
}

TypeAliasDecl* Sema::actOnTypeAlias(DeclContext* DeclContext, IdentifierInfo* Name, SourceLocation Location,
	const DeclSpec& DS, const ParsedType& Type, const ParsedAttributes& Attributes) {
	auto AliasedType = actOnType(Type);
	auto Result = Context.create<TypeAliasDecl>(DeclContext, Location, Name, AliasedType, DS.Internal, DS.Exported);
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
		if (static_cast<DeclContext*>(Record) == Function->getDeclContext() && Record->getIdentifier() == Function->getIdentifier()) {
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
	QualType ValueType;
	if (!CurrentFunction->hasImplicitEmitter()) {
		ValueType = CurrentFunction->getType();
		const Type* ReturnType = ValueType.getTypePtr();
		if (ReturnType && ReturnType->getTypeClass() == TypeClass::type_template_specialization) {
			const auto* Specialization = static_cast<const TemplateSpecializationType*>(ReturnType);
			if (Specialization->getArgumentCount() != 0) ValueType = Specialization->arguments()[0];
		}
	}
	return Context.create<EmitterExpr>(CurrentFunction, ValueType);
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
		if (Function == SampleIntrinsic) {
			if (Arguments.size() != 2) {
				Diagnostics.report(DiagnosticLevel::diagnostic_error, {Location, Location}, "sample requires a texture and coordinates");
				return nullptr;
			}
			auto Result = Context.create<PostfixExpr>(StmtClass::expr_call, Callee, nullptr,
				Context.copyPointerArray(Arguments), static_cast<unsigned>(Arguments.size()));
			Result->setType(Function->getType());
			return Result;
		}
		if (Function->isFunctionTemplate()) {
			Diagnostics.report(DiagnosticLevel::diagnostic_error, {Location, Location},
				"function template instantiation is not implemented");
			return nullptr;
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
		if (Left->getType() && Left->getType().getTypePtr() != Right->getType().getTypePtr()) {
			Diagnostics.report(DiagnosticLevel::diagnostic_error, {}, "emitted value has the wrong type");
			return nullptr;
		}
		auto Result = Context.create<BinaryExpr>(Opcode, Left, Right);
		Result->setType(Context.getBuiltinType(BuiltinTypeKind::builtin_void));
		return Result;
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
