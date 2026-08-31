#include <rtsl/CodeGen/CodeGenerator.hpp>

#include <bit>
#include <charconv>

namespace rtsl {

CodeGenerator::CodeGenerator(std::string_view ModuleName) : ModuleName(ModuleName), Builder(ModuleName) {}

CodeGenResult CodeGenerator::generate(ASTContext& Context) {
	this->Context = &Context;
	auto TranslationUnit = Context.getTranslationUnitDecl();
	declareRecords(TranslationUnit);
	declareAliases(TranslationUnit);
	declareGlobals(TranslationUnit);
	declareFunctions(TranslationUnit);
	defineFunctions(TranslationUnit);
	defineStages(TranslationUnit);
	auto Module = Builder.takeModule();
	auto Verification = ir::verify(Module);
	return {std::move(Module), std::move(Diagnostics), std::move(Verification)};
}

void CodeGenerator::declareRecords(TranslationUnitDecl* TranslationUnit) {
	for (auto Declaration = TranslationUnit->declsBegin(); Declaration; Declaration = Declaration->getNextDeclInContext())
		if (Declaration->getKind() == DeclKind::decl_record) lowerRecord(static_cast<RecordDecl*>(Declaration));
}

void CodeGenerator::declareAliases(TranslationUnitDecl* TranslationUnit) {
	for (auto Declaration = TranslationUnit->declsBegin(); Declaration; Declaration = Declaration->getNextDeclInContext()) {
		if (Declaration->getKind() != DeclKind::decl_type_alias) continue;
		auto Alias = static_cast<TypeAliasDecl*>(Declaration);
		NamedTypes[Alias->getIdentifier()] = lowerType(Alias->getAliasedType());
	}
}

void CodeGenerator::declareGlobals(TranslationUnitDecl* TranslationUnit) {
	for (auto Declaration = TranslationUnit->declsBegin(); Declaration; Declaration = Declaration->getNextDeclInContext())
		if (Declaration->getKind() == DeclKind::decl_variable) lowerGlobal(static_cast<VarDecl*>(Declaration));
}

void CodeGenerator::declareFunctions(TranslationUnitDecl* TranslationUnit) {
	for (auto Declaration = TranslationUnit->declsBegin(); Declaration; Declaration = Declaration->getNextDeclInContext()) {
		if (Declaration->getKind() == DeclKind::decl_record) declareRecordFunctions(static_cast<RecordDecl*>(Declaration));
		else if (Declaration->getKind() == DeclKind::decl_function && !static_cast<FunctionDecl*>(Declaration)->isFunctionTemplate())
			lowerFunctionDeclaration(static_cast<FunctionDecl*>(Declaration));
	}
}

void CodeGenerator::declareRecordFunctions(RecordDecl* Record) {
	for (auto Declaration = Record->declsBegin(); Declaration; Declaration = Declaration->getNextDeclInContext())
		if (Declaration->getKind() == DeclKind::decl_function && !static_cast<FunctionDecl*>(Declaration)->isFunctionTemplate())
			lowerFunctionDeclaration(static_cast<FunctionDecl*>(Declaration));
}

void CodeGenerator::defineFunctions(TranslationUnitDecl* TranslationUnit) {
	for (auto Declaration = TranslationUnit->declsBegin(); Declaration; Declaration = Declaration->getNextDeclInContext()) {
		if (Declaration->getKind() == DeclKind::decl_record) defineRecordFunctions(static_cast<RecordDecl*>(Declaration));
		else if (Declaration->getKind() == DeclKind::decl_function && !static_cast<FunctionDecl*>(Declaration)->isFunctionTemplate() && static_cast<FunctionDecl*>(Declaration)->getBody())
			lowerFunctionBody(static_cast<FunctionDecl*>(Declaration));
	}
}

void CodeGenerator::defineRecordFunctions(RecordDecl* Record) {
	for (auto Declaration = Record->declsBegin(); Declaration; Declaration = Declaration->getNextDeclInContext())
		if (Declaration->getKind() == DeclKind::decl_function && !static_cast<FunctionDecl*>(Declaration)->isFunctionTemplate() && static_cast<FunctionDecl*>(Declaration)->getBody())
			lowerFunctionBody(static_cast<FunctionDecl*>(Declaration));
}

void CodeGenerator::defineStages(TranslationUnitDecl* TranslationUnit) {
	for (auto Declaration = TranslationUnit->declsBegin(); Declaration; Declaration = Declaration->getNextDeclInContext()) {
		if (Declaration->getKind() == DeclKind::decl_record) defineRecordStages(static_cast<RecordDecl*>(Declaration));
		else if (Declaration->getKind() == DeclKind::decl_function && !static_cast<FunctionDecl*>(Declaration)->isFunctionTemplate()) {
			auto Function = static_cast<FunctionDecl*>(Declaration);
			(void)lowerStage(Function, Functions[Function], Builder.module().findFunction(Functions[Function])->symbol);
		}
	}
}

void CodeGenerator::defineRecordStages(RecordDecl* Record) {
	for (auto Declaration = Record->declsBegin(); Declaration; Declaration = Declaration->getNextDeclInContext()) {
		if (Declaration->getKind() != DeclKind::decl_function || static_cast<FunctionDecl*>(Declaration)->isFunctionTemplate()) continue;
		auto Function = static_cast<FunctionDecl*>(Declaration);
		(void)lowerStage(Function, Functions[Function], Builder.module().findFunction(Functions[Function])->symbol);
	}
}

void CodeGenerator::lowerRecord(RecordDecl* Record) {
	ir::Type Type;
	Type.kind = ir::TypeKind::type_structure;
	Type.name = Builder.module().strings.intern(qualifiedName(Record));
	appendRecordMembers(Type, Record);
	NamedTypes[Record->getIdentifier()] = Builder.internType(std::move(Type));
}

void CodeGenerator::appendRecordMembers(ir::Type& Type, RecordDecl* Record) {
	if (auto Base = recordForType(Record->getBaseType())) appendRecordMembers(Type, Base);
	for (auto Declaration = Record->declsBegin(); Declaration; Declaration = Declaration->getNextDeclInContext()) {
		if (Declaration->getKind() != DeclKind::decl_field) continue;
		auto Field = static_cast<FieldDecl*>(Declaration);
		Type.members.push_back({Builder.module().strings.intern(Field->getIdentifier()->getName()), lowerType(Field->getType()), {}, {}});
	}
}

void CodeGenerator::collectRecordFields(RecordDecl* Record, std::vector<FieldDecl*>& Fields) {
	if (auto Base = recordForType(Record->getBaseType())) collectRecordFields(Base, Fields);
	for (auto Declaration = Record->declsBegin(); Declaration; Declaration = Declaration->getNextDeclInContext())
		if (Declaration->getKind() == DeclKind::decl_field) Fields.push_back(static_cast<FieldDecl*>(Declaration));
}

bool CodeGenerator::lowerRecordConstruction(ConstructExpr* Construct, RecordDecl* Record,
	std::vector<ir::ValueId>& Fields) {
	unsigned ArgumentIndex{};
	if (RecordDecl* Base = recordForType(Record->getBaseType())) {
		if (Construct->getArgumentCount() == 0) {
			diagnose("record construction is missing its direct base value");
			return false;
		}
		Expr* BaseExpression = Construct->arguments()[ArgumentIndex++];
		if (BaseExpression->getType().getTypePtr() != Record->getBaseType().getTypePtr()) {
			diagnose("record construction requires its direct base value first");
			return false;
		}
		ir::ValueId BaseValue = lowerExpression(BaseExpression);
		if (!BaseValue) return false;
		appendRecordValueMembers(Base, BaseValue, Fields);
	}
	for (Decl* Declaration = Record->declsBegin(); Declaration; Declaration = Declaration->getNextDeclInContext()) {
		if (Declaration->getKind() != DeclKind::decl_field) continue;
		if (ArgumentIndex == Construct->getArgumentCount()) {
			diagnose("record construction does not initialize every direct field");
			return false;
		}
		Expr* Argument = Construct->arguments()[ArgumentIndex++];
		auto Field = static_cast<FieldDecl*>(Declaration);
		const ir::TypeId FieldType = lowerType(Field->getType());
		ir::ValueId Value = lowerExpression(Argument);
		if (Record->getIdentifier()->getName() == "Position" && Field->getIdentifier()->getName() == "position" &&
			Construct->getArgumentCount() == 1 && Value) {
			auto ArgumentType = ValueTypes[Value.value()];
			auto TargetType = lowerType(Field->getType());
			auto ArgumentIRType = Builder.module().findType(ArgumentType);
			auto TargetIRType = Builder.module().findType(TargetType);
			if (ArgumentIRType && TargetIRType && ArgumentIRType->kind == ir::TypeKind::type_vector &&
				ArgumentIRType->element_count == 3 && TargetIRType->kind == ir::TypeKind::type_vector &&
				TargetIRType->element_count == 4 && ArgumentIRType->element_type == TargetIRType->element_type) {
				float One = 1.0f;
				std::uint32_t Word = std::bit_cast<std::uint32_t>(One);
					auto Scalar = Builder.appendInstruction(CurrentFunction, CurrentBlock, ir::Opcode::opcode_constant_floating,
						TargetIRType->element_type, std::span<const ir::ValueId>{}, std::span<const std::uint32_t>(&Word, 1));
				ValueTypes[Scalar.value()] = TargetIRType->element_type;
				ir::ValueId Components[] = {Value, Scalar};
				Value = Builder.appendInstruction(CurrentFunction, CurrentBlock, ir::Opcode::opcode_construct, TargetType, Components);
				ValueTypes[Value.value()] = TargetType;
			}
		}
		if (!Value || ValueTypes[Value.value()] != FieldType) {
			diagnose("record construction argument type does not match its field");
			return false;
		}
		Fields.push_back(Value);
	}
	if (ArgumentIndex != Construct->getArgumentCount()) {
		diagnose("record construction has too many arguments");
		return false;
	}
	return true;
}

bool CodeGenerator::lowerConstructorBaseInitializer(FunctionDecl* Function, RecordDecl* Record) {
	Expr* Initializer = Function->getBaseInitializer();
	if (!Initializer) return true;
	RecordDecl* Base = recordForType(Record->getBaseType());
	if (!Base) {
		diagnose("constructor initializes a base but the structure has no base");
		return false;
	}
	if (Initializer->getType().getTypePtr() != Record->getBaseType().getTypePtr()) {
		diagnose("constructor base initializer does not construct the direct base");
		return false;
	}
	ir::ValueId BaseValue = lowerExpression(Initializer);
	if (!BaseValue) return false;
	std::vector<ir::ValueId> Values;
	appendRecordValueMembers(Base, BaseValue, Values);
	std::vector<FieldDecl*> Fields;
	collectRecordFields(Base, Fields);
	if (Fields.size() != Values.size()) {
		diagnose("constructor base initializer has an invalid field layout");
		return false;
	}
	for (std::size_t Index = 0; Index < Fields.size(); ++Index) ConstructorFields[Fields[Index]] = Values[Index];
	return true;
}

void CodeGenerator::appendRecordValueMembers(RecordDecl* Record, ir::ValueId Value,
	std::vector<ir::ValueId>& Fields) {
	if (RecordDecl* Base = recordForType(Record->getBaseType())) appendRecordValueMembers(Base, Value, Fields);
	for (Decl* Declaration = Record->declsBegin(); Declaration; Declaration = Declaration->getNextDeclInContext()) {
		if (Declaration->getKind() != DeclKind::decl_field) continue;
		auto Field = static_cast<FieldDecl*>(Declaration);
		const ir::TypeId FieldType = lowerType(Field->getType());
		const std::uint32_t Member = Builder.module().strings.intern(Field->getIdentifier()->getName()).value();
		const ir::ValueId Extracted = Builder.appendInstruction(CurrentFunction, CurrentBlock,
			ir::Opcode::opcode_access, FieldType, std::span(&Value, 1), std::span(&Member, 1));
		ValueTypes[Extracted.value()] = FieldType;
		Fields.push_back(Extracted);
	}
}

bool CodeGenerator::appendConstructorFields(RecordDecl* Record, std::vector<ir::ValueId>& Fields) {
	if (RecordDecl* Base = recordForType(Record->getBaseType())) {
		if (!appendConstructorFields(Base, Fields)) return false;
	}
	for (Decl* Declaration = Record->declsBegin(); Declaration; Declaration = Declaration->getNextDeclInContext()) {
		if (Declaration->getKind() != DeclKind::decl_field) continue;
		auto Field = static_cast<FieldDecl*>(Declaration);
		auto Position = ConstructorFields.find(Field);
		if (Position == ConstructorFields.end()) {
			diagnose("constructor does not initialize every field");
			return false;
		}
		Fields.push_back(Position->second);
	}
	return true;
}

void CodeGenerator::lowerGlobal(VarDecl* Variable) {
	auto Symbol = Builder.addSymbol(qualifiedName(Variable));
	auto IRType = lowerType(Variable->getType());
	GlobalSymbols[Variable] = Symbol;
	if (Variable->getStorageClass() == StorageClass::storage_uniform) {
		Builder.addUniform({.symbol = Symbol, .type = IRType});
		return;
	}
	if (Variable->getStorageClass() == StorageClass::storage_storage) {
		Builder.addStorageObject({.symbol = Symbol, .type = IRType, .address_space = ir::AddressSpace::address_space_storage});
		return;
	}
	const Type* ASTType = Variable->getType().getTypePtr();
	if (ASTType && ASTType->getTypeClass() == TypeClass::type_template_specialization) {
		auto Specialization = static_cast<const TemplateSpecializationType*>(ASTType);
		auto Name = Specialization->getName()->getName();
		ir::ResourceKind Kind{};
		bool Resource = true;
		if (Name == "buffer") Kind = ir::ResourceKind::resource_storage_buffer;
		else if (Name == "texture_1d" || Name == "texture_2d" || Name == "texture_3d") Kind = ir::ResourceKind::resource_sampled_texture;
		else if (Name == "image_1d" || Name == "image_2d" || Name == "image_3d") Kind = ir::ResourceKind::resource_storage_texture;
		else Resource = false;
		if (Resource) {
			Builder.addResource({.symbol = Symbol, .kind = Kind, .type = IRType,
				.access = Kind == ir::ResourceKind::resource_sampled_texture ? ir::Access::access_read_only : ir::Access::access_read_write});
			return;
		}
	}
	Builder.addStorageObject({.symbol = Symbol, .type = IRType, .address_space = ir::AddressSpace::address_space_private});
}

void CodeGenerator::lowerFunctionDeclaration(FunctionDecl* Function) {
	auto Symbol = Builder.addSymbol(qualifiedName(Function));
	std::vector<ir::TypeId> ParameterTypes;
	std::vector<ir::SymbolId> ParameterSymbols;
	for (unsigned Index = 0; Index < Function->getNumParams(); ++Index) {
		auto Parameter = Function->parameters()[Index];
		ParameterTypes.push_back(lowerType(Parameter->getType()));
		ParameterSymbols.push_back(Builder.addSymbol(qualifiedName(Function) + "::" + std::string(Parameter->getIdentifier()->getName())));
	}
	auto FunctionID = Builder.addFunction(Symbol, lowerType(Function->getType()), ParameterTypes, ParameterSymbols,
		Function->getBody() == nullptr, Function->hasImplicitEmitter());
	Functions[Function] = FunctionID;
	auto IRFunction = Builder.module().findFunction(FunctionID);
	for (unsigned Index = 0; Index < Function->getNumParams(); ++Index) {
		Values[Function->parameters()[Index]] = IRFunction->parameters[Index].value;
		ValueTypes[IRFunction->parameters[Index].value.value()] = IRFunction->parameters[Index].type;
	}
}

void CodeGenerator::lowerFunctionBody(FunctionDecl* Function) {
	CurrentFunction = Functions[Function];
	CurrentASTFunction = Function;
	CurrentBlock = Builder.addBlock(CurrentFunction);
	Values.clear();
	auto IRFunction = Builder.module().findFunction(CurrentFunction);
	for (unsigned Index = 0; Index < Function->getNumParams(); ++Index) {
		auto Value = IRFunction->parameters[Index].value;
		Values[Function->parameters()[Index]] = Value;
		ValueTypes[Value.value()] = IRFunction->parameters[Index].type;
	}
	BarrierIndex = 0;
	ConditionalDepth = 0;
	CurrentConstructor = constructorRecord(Function);
	ConstructorFields.clear();
	if (CurrentConstructor && !lowerConstructorBaseInitializer(Function, CurrentConstructor)) {
		CurrentConstructor = nullptr;
		return;
	}
	lowerStatement(Function->getBody());
	IRFunction = Builder.module().findFunction(CurrentFunction);
	auto Block = Builder.module().findBlock(*IRFunction, CurrentBlock);
	if (!Block->terminator && CurrentConstructor) {
		std::vector<ir::ValueId> Fields;
		if (!appendConstructorFields(CurrentConstructor, Fields)) {
			CurrentConstructor = nullptr;
			return;
		}
		auto ResultType = lowerType(Function->getType());
		auto Result = Builder.appendInstruction(CurrentFunction, CurrentBlock, ir::Opcode::opcode_construct, ResultType, Fields);
		ValueTypes[Result.value()] = ResultType;
		ir::Terminator Terminator{.kind = ir::TerminatorKind::terminator_return_value};
		Terminator.operands.push_back(Result);
		Builder.setTerminator(CurrentFunction, CurrentBlock, std::move(Terminator));
	} else if (!Block->terminator) Builder.setTerminator(CurrentFunction, CurrentBlock,
		{.kind = ir::TerminatorKind::terminator_return});
	CurrentConstructor = nullptr;
}

void CodeGenerator::lowerStatement(Stmt* Statement) {
	if (!Statement) return;
	switch (Statement->getStmtClass()) {
	case StmtClass::stmt_compound: {
		auto Compound = static_cast<CompoundStmt*>(Statement);
		for (unsigned Index = 0; Index < Compound->size(); ++Index) lowerStatement(Compound->body()[Index]);
		break;
	}
	case StmtClass::stmt_decl: {
		auto Variable = static_cast<DeclStmt*>(Statement)->getDecl();
		if (!Variable->getInit()) {
			Values[Variable] = {};
			break;
		}
		auto Value = lowerExpression(Variable->getInit());
		if (!Value) {
			diagnose("local variable has no lowerable initializer");
			break;
		}
		if (ValueTypes[Value.value()] != lowerType(Variable->getType())) {
			diagnose("local variable initializer type does not match its declared type");
			break;
		}
		Values[Variable] = Value;
		break;
	}
	case StmtClass::stmt_if:
		lowerIfStatement(static_cast<IfStmt*>(Statement));
		break;
	case StmtClass::stmt_return: {
		auto Value = static_cast<ValueStmt*>(Statement)->getValue();
		ir::Terminator Terminator;
		if (Value) {
			Terminator.kind = ir::TerminatorKind::terminator_return_value;
			Terminator.operands.push_back(lowerExpression(Value));
		} else Terminator.kind = ir::TerminatorKind::terminator_return;
		Builder.setTerminator(CurrentFunction, CurrentBlock, std::move(Terminator));
		break;
	}
	case StmtClass::stmt_barrier: {
		auto Attribute = CurrentASTFunction ? findAttribute(CurrentASTFunction, "stage") : nullptr;
		const std::string_view Stage = Attribute && Attribute->getTokenCount() && Attribute->tokens()[0].Identifier
			? Attribute->tokens()[0].Identifier->getName() : std::string_view{};
		if (Stage != "compute" && Stage != "tess_control") {
			diagnose("barriers are only valid in compute and tessellation-control entry functions");
			break;
		}
		if (ConditionalDepth) {
			diagnose("barriers cannot be conditionally reached");
			break;
		}
		std::uint32_t ID = ++BarrierIndex;
		(void)Builder.appendInstruction(CurrentFunction, CurrentBlock, ir::Opcode::opcode_barrier, {}, {}, std::span(&ID, 1));
		break;
	}
	default:
		(void)lowerExpression(static_cast<Expr*>(Statement));
		break;
	}
}

void CodeGenerator::lowerIfStatement(IfStmt* Statement) {
	auto Condition = lowerExpression(Statement->getCondition());
	if (!Condition) {
		diagnose("if condition is not lowerable");
		return;
	}
	const auto Before = Values;
	const auto ThenBlock = Builder.addBlock(CurrentFunction);
	const auto ElseBlock = Statement->getElse() ? Builder.addBlock(CurrentFunction) : ir::BlockId{};
	const auto MergeBlock = Builder.addBlock(CurrentFunction);
	std::vector<const ValueDecl*> Declarations;
	std::vector<ir::ValueId> BeforeValues;
	for (const auto& [Declaration, Value] : Before) {
		Declarations.push_back(Declaration);
		BeforeValues.push_back(Value);
		auto MergeValue = Builder.addBlockArgument(CurrentFunction, MergeBlock, ValueTypes[Value.value()]);
		ValueTypes[MergeValue.value()] = ValueTypes[Value.value()];
	}
	Builder.setMerge(CurrentFunction, CurrentBlock, {.kind = ir::MergeKind::merge_selection, .merge_block = MergeBlock});
	ir::Terminator Branch{.kind = ir::TerminatorKind::terminator_conditional_branch};
	Branch.operands.push_back(Condition);
	Branch.successors.push_back({.block = ThenBlock});
	Branch.successors.push_back({.block = ElseBlock ? ElseBlock : MergeBlock, .arguments = ElseBlock ? std::vector<ir::ValueId>{} : BeforeValues});
	Builder.setTerminator(CurrentFunction, CurrentBlock, std::move(Branch));

	CurrentBlock = ThenBlock;
	Values = Before;
	++ConditionalDepth;
	lowerStatement(Statement->getThen());
	--ConditionalDepth;
	if (!Builder.module().findBlock(*Builder.module().findFunction(CurrentFunction), CurrentBlock)->terminator) {
		std::vector<ir::ValueId> Arguments;
		for (const ValueDecl* Declaration : Declarations) Arguments.push_back(Values[Declaration]);
		Builder.setTerminator(CurrentFunction, CurrentBlock,
			{.kind = ir::TerminatorKind::terminator_branch, .successors = {{.block = MergeBlock, .arguments = std::move(Arguments)}}});
	}

	if (ElseBlock) {
		CurrentBlock = ElseBlock;
		Values = Before;
		++ConditionalDepth;
		lowerStatement(Statement->getElse());
		--ConditionalDepth;
		if (!Builder.module().findBlock(*Builder.module().findFunction(CurrentFunction), CurrentBlock)->terminator) {
			std::vector<ir::ValueId> Arguments;
			for (const ValueDecl* Declaration : Declarations) Arguments.push_back(Values[Declaration]);
			Builder.setTerminator(CurrentFunction, CurrentBlock,
				{.kind = ir::TerminatorKind::terminator_branch, .successors = {{.block = MergeBlock, .arguments = std::move(Arguments)}}});
		}
	}

	CurrentBlock = MergeBlock;
	Values.clear();
	auto Merge = Builder.module().findBlock(*Builder.module().findFunction(CurrentFunction), MergeBlock);
	for (std::size_t Index = 0; Index < Declarations.size(); ++Index) Values[Declarations[Index]] = Merge->arguments[Index].value;
}

ir::ValueId CodeGenerator::lowerExpression(Expr* Expression) {
	if (!Expression) return {};
	switch (Expression->getStmtClass()) {
	case StmtClass::expr_decl_ref: {
		auto Declaration = static_cast<DeclRefExpr*>(Expression)->getDecl();
		if (auto Position = Values.find(Declaration); Position != Values.end()) {
			if (!Position->second) diagnose("use of an uninitialized local variable");
			return Position->second;
		}
		auto Symbol = GlobalSymbols.find(Declaration);
		if (Symbol == GlobalSymbols.end()) { diagnose("unlowered declaration reference"); return {}; }
		std::uint32_t Immediate = Symbol->second.value();
		auto Type = lowerType(Declaration->getType());
		auto Value = Builder.appendInstruction(CurrentFunction, CurrentBlock, ir::Opcode::opcode_resource_load, Type, {}, std::span(&Immediate, 1));
		ValueTypes[Value.value()] = Type;
		return Value;
	}
	case StmtClass::expr_integer: {
		if (!Expression->getType()) {
			diagnose("integer literal requires a type context");
			return {};
		}
		auto Type = lowerType(Expression->getType());
		std::uint64_t Number = static_cast<IntegerLiteral*>(Expression)->getValue();
		std::uint32_t Words[] = {static_cast<std::uint32_t>(Number), static_cast<std::uint32_t>(Number >> 32)};
		auto Value = Builder.appendInstruction(CurrentFunction, CurrentBlock, ir::Opcode::opcode_constant_integer, Type, {}, Words);
		ValueTypes[Value.value()] = Type;
		return Value;
	}
	case StmtClass::expr_floating: {
		auto Type = lowerUnqualifiedType(Context->getBuiltinType(BuiltinTypeKind::builtin_f32).getTypePtr());
		float Number = static_cast<float>(static_cast<FloatingLiteral*>(Expression)->getValue());
		std::uint32_t Word = std::bit_cast<std::uint32_t>(Number);
		auto Value = Builder.appendInstruction(CurrentFunction, CurrentBlock, ir::Opcode::opcode_constant_floating, Type, {}, std::span(&Word, 1));
		ValueTypes[Value.value()] = Type;
		return Value;
	}
	case StmtClass::expr_unary: {
		auto Unary = static_cast<UnaryExpr*>(Expression);
		auto Operand = lowerExpression(Unary->getOperand());
		if (!Operand) return {};
		auto Opcode = Unary->getOpcode() == tok::minus ? ir::Opcode::opcode_negate :
			Unary->getOpcode() == tok::exclaim ? ir::Opcode::opcode_logical_not : ir::Opcode::opcode_undef;
		if (Opcode == ir::Opcode::opcode_undef) {
			diagnose("unary operator is not supported by RTIR lowering");
			return {};
		}
		auto Type = ValueTypes.at(Operand.value());
		auto Value = Builder.appendInstruction(CurrentFunction, CurrentBlock, Opcode, Type, std::span(&Operand, 1));
		ValueTypes[Value.value()] = Type;
		return Value;
	}
	case StmtClass::expr_binary: {
		auto Binary = static_cast<BinaryExpr*>(Expression);
		if (Binary->getOpcode() == tok::lessminus) {
			auto Value = lowerExpression(Binary->getRight());
			(void)Builder.appendInstruction(CurrentFunction, CurrentBlock, ir::Opcode::opcode_emit, {}, std::span(&Value, 1));
			return {};
		}
		if (Binary->getOpcode() == tok::equal && Binary->getLeft()->getStmtClass() == StmtClass::expr_decl_ref) {
			auto Declaration = static_cast<DeclRefExpr*>(Binary->getLeft())->getDecl();
			if (Declaration->getKind() == DeclKind::decl_field && CurrentConstructor) {
				auto Value = lowerExpression(Binary->getRight());
				ConstructorFields[static_cast<FieldDecl*>(Declaration)] = Value;
				return Value;
			}
			if (auto Position = Values.find(Declaration); Position != Values.end()) {
				auto Value = lowerExpression(Binary->getRight());
				if (Value) Values[Declaration] = Value;
				return Value;
			}
		}
		ir::ValueId Operands[] = {lowerExpression(Binary->getLeft()), lowerExpression(Binary->getRight())};
		if (!Operands[0] || !Operands[1]) return {};
		auto Opcode = binaryOpcode(Binary->getOpcode());
		if (Opcode == ir::Opcode::opcode_undef) {
			diagnose("binary operator is not supported by RTIR lowering");
			return {};
		}
		auto Type = lowerType(Expression->getType());
		auto Value = Builder.appendInstruction(CurrentFunction, CurrentBlock, Opcode, Type, Operands);
		ValueTypes[Value.value()] = Type;
		return Value;
	}
	case StmtClass::expr_construct: {
		auto Construct = static_cast<ConstructExpr*>(Expression);
		std::vector<ir::ValueId> Arguments;
		if (RecordDecl* Record = recordForType(Construct->getType())) {
			if (!lowerRecordConstruction(Construct, Record, Arguments)) return {};
		} else {
			for (unsigned Index = 0; Index < Construct->getArgumentCount(); ++Index)
				Arguments.push_back(lowerExpression(Construct->arguments()[Index]));
		}
		for (ir::ValueId Argument : Arguments) if (!Argument) return {};
		auto Type = lowerType(Construct->getType());
		auto Value = Builder.appendInstruction(CurrentFunction, CurrentBlock, ir::Opcode::opcode_construct, Type, Arguments);
		ValueTypes[Value.value()] = Type;
		return Value;
	}
	case StmtClass::expr_call: {
		auto Call = static_cast<PostfixExpr*>(Expression);
		if (Call->getBase()->getStmtClass() != StmtClass::expr_decl_ref) { diagnose("indirect calls are not lowered yet"); return {}; }
		auto Declaration = static_cast<DeclRefExpr*>(Call->getBase())->getDecl();
		if (Declaration->getKind() != DeclKind::decl_function) { diagnose("call target is not a function"); return {}; }
		if (Declaration->getIdentifier()->getName() == "sample") {
			if (Call->getArgumentCount() != 2 || Call->arguments()[0]->getStmtClass() != StmtClass::expr_decl_ref) {
				diagnose("sample requires a global texture resource and coordinates");
				return {};
			}
			auto Resource = static_cast<DeclRefExpr*>(Call->arguments()[0])->getDecl();
			auto Symbol = GlobalSymbols.find(Resource);
			const Type* ResourceType = Resource->getType().getTypePtr();
			const bool SampledTexture = ResourceType && ResourceType->getTypeClass() == TypeClass::type_template_specialization &&
				static_cast<const TemplateSpecializationType*>(ResourceType)->getName()->getName() == "texture_2d";
			if (Symbol == GlobalSymbols.end() || !SampledTexture) {
				diagnose("sample target is not a global texture resource");
				return {};
			}
			auto Coordinates = lowerExpression(Call->arguments()[1]);
			std::uint32_t ResourceSymbol = Symbol->second.value();
			auto Type = lowerType(Declaration->getType());
			auto Value = Builder.appendInstruction(CurrentFunction, CurrentBlock, ir::Opcode::opcode_resource_sample,
				Type, std::span(&Coordinates, 1), std::span(&ResourceSymbol, 1));
			ValueTypes[Value.value()] = Type;
			return Value;
		}
		std::vector<ir::ValueId> Arguments;
		for (unsigned Index = 0; Index < Call->getArgumentCount(); ++Index) Arguments.push_back(lowerExpression(Call->arguments()[Index]));
		for (ir::ValueId Argument : Arguments) if (!Argument) return {};
		auto Function = static_cast<FunctionDecl*>(Declaration);
		if (auto Position = Functions.find(Function); Position == Functions.end()) {
			diagnose("function call cannot be lowered before template instantiation");
			return {};
		}
		auto Type = lowerType(Function->getType());
		auto Value = Builder.appendInstruction(CurrentFunction, CurrentBlock, ir::Opcode::opcode_call, Type, Arguments, {}, Functions.at(Function));
		ValueTypes[Value.value()] = Type;
		return Value;
	}
	case StmtClass::expr_member:
	case StmtClass::expr_subscript: {
		auto Access = static_cast<PostfixExpr*>(Expression);
		std::vector<ir::ValueId> Operands{lowerExpression(Access->getBase())};
		if (Access->getArgumentCount()) Operands.push_back(lowerExpression(Access->arguments()[0]));
		for (ir::ValueId Operand : Operands) if (!Operand) return {};
		std::vector<std::uint32_t> Immediates;
		if (Access->getMember()) Immediates.push_back(Builder.module().strings.intern(Access->getMember()->getName()).value());
		auto Type = lowerType(Expression->getType());
		auto Value = Builder.appendInstruction(CurrentFunction, CurrentBlock, ir::Opcode::opcode_access, Type, Operands, Immediates);
		ValueTypes[Value.value()] = Type;
		return Value;
	}
	case StmtClass::expr_emitter:
		diagnose("an emitter can only be used as the left operand of '<-'");
		return {};
	default:
		diagnose("expression kind is not implemented by RTIR lowering");
		return {};
	}
}

ir::TypeId CodeGenerator::lowerType(QualType Type) {
	if (!Type) return {};
	return lowerUnqualifiedType(Type.getTypePtr());
}

ir::TypeId CodeGenerator::lowerUnqualifiedType(const Type* ASTType) {
	if (auto Position = Types.find(ASTType); Position != Types.end()) return Position->second;
	ir::Type Type;
	switch (ASTType->getTypeClass()) {
	case TypeClass::type_builtin: {
		auto Builtin = static_cast<const BuiltinType*>(ASTType);
		switch (Builtin->getKind()) {
		case BuiltinTypeKind::builtin_void: Type.kind = ir::TypeKind::type_void; break;
		case BuiltinTypeKind::builtin_bool: Type.kind = ir::TypeKind::type_boolean; Type.bit_width = 1; break;
		case BuiltinTypeKind::builtin_i32: Type.kind = ir::TypeKind::type_signed_integer; Type.bit_width = 32; break;
		case BuiltinTypeKind::builtin_u32: case BuiltinTypeKind::builtin_usize:
			Type.kind = ir::TypeKind::type_unsigned_integer; Type.bit_width = 32; break;
		case BuiltinTypeKind::builtin_f32: Type.kind = ir::TypeKind::type_floating; Type.bit_width = 32; break;
		}
		break;
	}
	case TypeClass::type_named: {
		auto Named = static_cast<const NamedType*>(ASTType);
		if (auto Position = NamedTypes.find(Named->getName()); Position != NamedTypes.end()) return Position->second;
		auto Name = Named->getName()->getName();
		if (Name == "vec2" || Name == "vec3" || Name == "vec4") {
			Type.kind = ir::TypeKind::type_vector;
			Type.element_count = static_cast<std::uint32_t>(Name.back() - '0');
			Type.element_type = lowerUnqualifiedType(Context->getBuiltinType(BuiltinTypeKind::builtin_f32).getTypePtr());
		} else if (Name == "mat2" || Name == "mat3" || Name == "mat4") {
			Type.kind = ir::TypeKind::type_matrix;
			Type.element_count = static_cast<std::uint32_t>(Name.back() - '0');
			Type.element_type = lowerUnqualifiedType(Context->getBuiltinType(BuiltinTypeKind::builtin_f32).getTypePtr());
		} else {
			Type.kind = ir::TypeKind::type_structure;
			Type.name = Builder.module().strings.intern(Name);
		}
		break;
	}
	case TypeClass::type_template_specialization: {
		auto Specialization = static_cast<const TemplateSpecializationType*>(ASTType);
		auto Name = Specialization->getName()->getName();
		Type.kind = Name == "patch" ? ir::TypeKind::type_patch :
			(Name == "triangle" || Name == "triangle_strip") ? ir::TypeKind::type_primitive : ir::TypeKind::type_structure;
		Type.name = Builder.module().strings.intern(Name);
		if (Specialization->getArgumentCount()) Type.element_type = lowerType(Specialization->arguments()[0]);
		Type.element_count = Specialization->getArgumentCount() > 1 ? 1 : 0;
		break;
	}
	case TypeClass::type_pointer:
	case TypeClass::type_reference: {
		QualType Pointee = ASTType->getTypeClass() == TypeClass::type_pointer ?
			static_cast<const PointerType*>(ASTType)->getPointeeType() : static_cast<const ReferenceType*>(ASTType)->getPointeeType();
		Type.kind = ir::TypeKind::type_pointer;
		Type.element_type = lowerType(Pointee);
		Type.address_space = ir::AddressSpace::address_space_function;
		break;
	}
	}
	auto Result = Builder.internType(std::move(Type));
	Types[ASTType] = Result;
	return Result;
}

std::string CodeGenerator::qualifiedName(const NamedDecl* Declaration) const {
	std::string Result;
	if (Declaration->getDeclContext() != Context->getTranslationUnitDecl()) {
		for (auto Candidate = Context->getTranslationUnitDecl()->declsBegin(); Candidate; Candidate = Candidate->getNextDeclInContext()) {
			if (Candidate->getKind() != DeclKind::decl_record) continue;
			auto Record = static_cast<RecordDecl*>(Candidate);
			if (static_cast<DeclContext*>(Record) == Declaration->getDeclContext()) {
				Result += std::string(Record->getIdentifier()->getName()) + "::";
				break;
			}
		}
	}
	Result += Declaration->getIdentifier()->getName();
	if (Declaration->getKind() == DeclKind::decl_function) {
		auto Function = static_cast<const FunctionDecl*>(Declaration);
		Result += "(";
		for (unsigned Index = 0; Index < Function->getNumParams(); ++Index) {
			if (Index) Result += ",";
			Result += typeName(Function->parameters()[Index]->getType());
		}
		Result += ")";
	}
	return Result;
}

std::string CodeGenerator::typeName(QualType Type) const {
	if (!Type) return "?";
	const auto* Value = Type.getTypePtr();
	switch (Value->getTypeClass()) {
	case TypeClass::type_builtin: return std::to_string(static_cast<unsigned>(static_cast<const BuiltinType*>(Value)->getKind()));
	case TypeClass::type_named: return std::string(static_cast<const NamedType*>(Value)->getName()->getName());
	case TypeClass::type_pointer: return typeName(static_cast<const PointerType*>(Value)->getPointeeType()) + "*";
	case TypeClass::type_reference: return typeName(static_cast<const ReferenceType*>(Value)->getPointeeType()) + "&";
	default: return std::to_string(reinterpret_cast<std::uintptr_t>(Value));
	}
}

RecordDecl* CodeGenerator::constructorRecord(FunctionDecl* Function) const {
	for (auto Declaration = Context->getTranslationUnitDecl()->declsBegin(); Declaration; Declaration = Declaration->getNextDeclInContext()) {
		if (Declaration->getKind() != DeclKind::decl_record) continue;
		auto Record = static_cast<RecordDecl*>(Declaration);
		if (static_cast<DeclContext*>(Record) == Function->getDeclContext() && Record->getIdentifier() == Function->getIdentifier()) return Record;
	}
	return nullptr;
}

RecordDecl* CodeGenerator::recordForType(QualType ValueType) const {
	const Type* TypePointer = ValueType.getTypePtr();
	if (TypePointer && TypePointer->getTypeClass() == TypeClass::type_reference)
		TypePointer = static_cast<const ReferenceType*>(TypePointer)->getPointeeType().getTypePtr();
	if (!TypePointer || TypePointer->getTypeClass() != TypeClass::type_named) return nullptr;
	auto Name = static_cast<const NamedType*>(TypePointer)->getName();
	for (auto Declaration = Context->getTranslationUnitDecl()->declsBegin(); Declaration;
		Declaration = Declaration->getNextDeclInContext()) {
		if (Declaration->getKind() != DeclKind::decl_record) continue;
		auto Record = static_cast<RecordDecl*>(Declaration);
		if (Record->getIdentifier() == Name) return Record;
	}
	return nullptr;
}

Attr* CodeGenerator::findAttribute(const Decl* Declaration, std::string_view Name) const {
	for (auto Attribute = Declaration->getAttrs(); Attribute; Attribute = Attribute->getNextAttr())
		if (Attribute->getName()->getName() == Name) return Attribute;
	return nullptr;
}

bool CodeGenerator::lowerStage(FunctionDecl* Function, ir::FunctionId FunctionID, ir::SymbolId Symbol) {
	auto Attribute = findAttribute(Function, "stage");
	if (!Attribute || Attribute->getTokenCount() == 0 || !Attribute->tokens()[0].Identifier) return false;
	auto Name = Attribute->tokens()[0].Identifier->getName();
	ir::EntryPoint Entry{};
	Entry.symbol = Symbol;
	Entry.function = FunctionID;
	Entry.source_name = Builder.module().strings.intern(Function->getIdentifier()->getName());
	Entry.stage = ir::Stage::stage_vertex;
	Entry.configuration = std::monostate{};
	if (Name == "vertex") {
		Entry.stage = ir::Stage::stage_vertex;
		Entry.configuration.emplace<std::monostate>();
	}
	else if (Name == "tess_control") {
		Entry.stage = ir::Stage::stage_tessellation_control;
		Entry.configuration = ir::TessellationControlConfiguration{.output_control_points = 1};
	} else if (Name == "tess_eval") {
		Entry.stage = ir::Stage::stage_tessellation_evaluation;
		Entry.configuration = ir::TessellationEvaluationConfiguration{};
	} else if (Name == "geometry") {
		Entry.stage = ir::Stage::stage_geometry;
		Entry.configuration = ir::GeometryConfiguration{.maximum_vertices = 1};
	} else if (Name == "fragment") {
		Entry.stage = ir::Stage::stage_fragment;
		Entry.configuration.emplace<std::monostate>();
	}
	else if (Name == "compute") {
		Entry.stage = ir::Stage::stage_compute;
		ir::ComputeConfiguration Configuration;
		auto Values = numericAttribute(findAttribute(Function, "workgroup_size"));
		for (unsigned Index = 0; Index < Values.size() && Index < 3; ++Index) Configuration.workgroup_size[Index] = Values[Index];
		Entry.configuration = Configuration;
	} else return false;
	for (unsigned Index = 0; Index < Function->getNumParameterContracts(); ++Index) {
		auto& Contract = Function->parameterContracts()[Index];
		ir::InterfaceContract Lowered{
			.parameter_index = Contract.ParameterIndex,
			.contract = Builder.module().strings.intern(Contract.Contract->getName()),
		};
		for (unsigned PathIndex = 0; PathIndex < Contract.MemberPathLength; ++PathIndex)
			Lowered.member_path.push_back(Builder.module().strings.intern(Contract.MemberPath[PathIndex]->getName()));
		Entry.parameter_contracts.push_back(Lowered);
	}
	Builder.addEntryPoint(Entry);
	return true;
}

std::vector<std::uint32_t> CodeGenerator::numericAttribute(Attr* Attribute) const {
	std::vector<std::uint32_t> Result;
	if (!Attribute) return Result;
	for (unsigned Index = 0; Index < Attribute->getTokenCount(); ++Index) {
		auto& Token = Attribute->tokens()[Index];
		if (Token.Kind != tok::numeric_literal) continue;
		std::uint32_t Value{};
		std::from_chars(Token.LiteralData, Token.LiteralData + Token.LiteralLength, Value);
		Result.push_back(Value);
	}
	return Result;
}

ir::Opcode CodeGenerator::binaryOpcode(tok::TokenKind Kind) const {
	switch (Kind) {
	case tok::plus: return ir::Opcode::opcode_add;
	case tok::minus: return ir::Opcode::opcode_subtract;
	case tok::star: return ir::Opcode::opcode_multiply;
	case tok::slash: return ir::Opcode::opcode_divide;
	case tok::percent: return ir::Opcode::opcode_remainder;
	case tok::equalequal: return ir::Opcode::opcode_compare_equal;
	case tok::exclaimequal: return ir::Opcode::opcode_compare_not_equal;
	case tok::less: return ir::Opcode::opcode_compare_less;
	case tok::lessequal: return ir::Opcode::opcode_compare_less_equal;
	case tok::greater: return ir::Opcode::opcode_compare_greater;
	case tok::greaterequal: return ir::Opcode::opcode_compare_greater_equal;
	case tok::ampamp: return ir::Opcode::opcode_logical_and;
	case tok::pipepipe: return ir::Opcode::opcode_logical_or;
	default: return ir::Opcode::opcode_undef;
	}
}

void CodeGenerator::diagnose(std::string_view Message) { Diagnostics.push_back({std::string(Message)}); }

}
