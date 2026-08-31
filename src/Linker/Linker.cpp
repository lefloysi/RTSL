#include <rtsl/Linker/Linker.hpp>

#include <array>


namespace rtsl {

LinkResult Linker::link(std::string_view ProgramName, std::span<const ir::Module> Modules) {
	Builder = ir::ModuleBuilder(ProgramName);
	Diagnostics.clear();
	FunctionsBySymbol.clear();
	SelectedDefinitions.clear();
	EntriesByStage.clear();
	std::vector<ModuleMaps> Maps(Modules.size());
	for (std::size_t Index = 0; Index < Modules.size(); ++Index) copyTypes(Modules[Index], Maps[Index]);
	for (std::size_t Index = 0; Index < Modules.size(); ++Index) copySymbols(Modules[Index], Maps[Index]);
	for (std::size_t Index = 0; Index < Modules.size(); ++Index) declareFunctions(Modules[Index], Maps[Index]);
	for (std::size_t Index = 0; Index < Modules.size(); ++Index) defineFunctions(Modules[Index], Maps[Index]);
	for (std::size_t Index = 0; Index < Modules.size(); ++Index) copyMetadata(Modules[Index], Maps[Index]);
	synthesizeIdentityVertexStage();
	validateDefinitions();
	validateStageInterfaces();
	auto Module = Builder.takeModule();
	auto Verification = ir::verify(Module);
	return {std::move(Module), std::move(Diagnostics), std::move(Verification)};
}

void Linker::synthesizeIdentityVertexStage() {
	if (EntriesByStage.contains(static_cast<std::uint32_t>(ir::Stage::stage_vertex))) return;
	auto Control = EntriesByStage.find(static_cast<std::uint32_t>(ir::Stage::stage_tessellation_control));
	if (Control == EntriesByStage.end()) return;
	const ir::TypeId InterfaceType = interfaceInput(Control->second);
	if (!InterfaceType) {
		diagnose(LinkDiagnosticCode::link_identity_vertex_unavailable, {},
			"cannot synthesize a vertex stage without a tessellation-control input interface");
		return;
	}
	const auto Symbol = Builder.addSymbol("__rtsl_identity_vertex");
	const std::array Parameters{InterfaceType};
	const auto Function = Builder.addFunction(Symbol, InterfaceType, Parameters);
	const auto Block = Builder.addBlock(Function);
	const auto Input = Builder.module().findFunction(Function)->parameters.front().value;
	ir::Terminator Return{.kind = ir::TerminatorKind::terminator_return_value};
	Return.operands.push_back(Input);
	Builder.setTerminator(Function, Block, std::move(Return));
	ir::EntryPoint Entry{};
	Entry.symbol = Symbol;
	Entry.function = Function;
	Entry.source_name = Builder.module().strings.intern("__rtsl_identity_vertex");
	Entry.stage = ir::Stage::stage_vertex;
	EntriesByStage.emplace(static_cast<std::uint32_t>(Entry.stage), Entry);
	Builder.addEntryPoint(Entry);
}

void Linker::copyTypes(const ir::Module& Module, ModuleMaps& Maps) {
	for (const auto& Source : Module.types) {
		ir::Type Type = Source;
		Type.id = {};
		if (Source.element_type) Type.element_type = Maps.Types[Source.element_type.value()];
		for (auto& Parameter : Type.parameter_types) Parameter = Maps.Types[Parameter.value()];
		for (auto& Member : Type.members) {
			Member.type = Maps.Types[Member.type.value()];
			Member.name = Module.strings.get(Member.name).empty() ? ir::StringId{} :
				Builder.module().strings.intern(Module.strings.get(Member.name));
		}
		Type.name = Module.strings.get(Source.name).empty() ? ir::StringId{} :
			Builder.module().strings.intern(Module.strings.get(Source.name));
		Maps.Types[Source.id.value()] = Builder.internType(std::move(Type));
	}
}

void Linker::copySymbols(const ir::Module& Module, ModuleMaps& Maps) {
	for (const auto& Symbol : Module.symbols)
		Maps.Symbols[Symbol.id.value()] = Builder.addSymbol(Module.strings.get(Symbol.fully_qualified_name), Symbol.exported);
}

void Linker::declareFunctions(const ir::Module& Module, ModuleMaps& Maps) {
	for (const auto& Source : Module.functions) {
		std::vector<ir::TypeId> ParameterTypes;
		std::vector<ir::SymbolId> ParameterSymbols;
		for (const auto& Parameter : Source.parameters) {
			ParameterTypes.push_back(Maps.Types[Parameter.type.value()]);
			ParameterSymbols.push_back(Parameter.symbol ? Maps.Symbols[Parameter.symbol.value()] : ir::SymbolId{});
		}
		auto Symbol = Maps.Symbols[Source.symbol.value()];
		auto SymbolRecord = Builder.module().findSymbol(Symbol);
		std::string Name(Builder.module().strings.get(SymbolRecord->fully_qualified_name));
		auto Existing = FunctionsBySymbol.find(Symbol.value());
		if (Existing == FunctionsBySymbol.end()) {
			auto Function = Builder.addFunction(Symbol, Maps.Types[Source.return_type.value()], ParameterTypes,
				ParameterSymbols, Source.declaration, Source.implicit_emitter);
			FunctionsBySymbol.emplace(Symbol.value(), Function);
			Maps.Functions[Source.id.value()] = Function;
			if (!Source.declaration) SelectedDefinitions.insert(&Source);
			continue;
		}
		auto Target = Builder.module().findFunction(Existing->second);
		if (!sameSignature(*Target, Maps.Types[Source.return_type.value()], ParameterTypes) ||
			Target->implicit_emitter != Source.implicit_emitter)
			diagnose(LinkDiagnosticCode::link_incompatible_declaration, Name, "function declarations have incompatible types");
		Maps.Functions[Source.id.value()] = Existing->second;
		if (!Source.declaration) {
			if (!Target->declaration) diagnose(LinkDiagnosticCode::link_duplicate_definition, Name, "function has multiple definitions");
			else {
				Target->declaration = false;
				SelectedDefinitions.insert(&Source);
			}
		}
	}
}

void Linker::defineFunctions(const ir::Module& Module, ModuleMaps& Maps) {
	for (const auto& Source : Module.functions) {
		if (!SelectedDefinitions.contains(&Source)) continue;
		auto TargetID = Maps.Functions[Source.id.value()];
		auto Target = Builder.module().findFunction(TargetID);
		std::unordered_map<std::uint32_t, ir::ValueId> Values;
		for (std::size_t Index = 0; Index < Source.parameters.size(); ++Index)
			Values[Source.parameters[Index].value.value()] = Target->parameters[Index].value;
		std::unordered_map<std::uint32_t, ir::BlockId> Blocks;
		for (const auto& SourceBlock : Source.blocks) Blocks[SourceBlock.id.value()] = Builder.addBlock(TargetID);
		for (const auto& SourceBlock : Source.blocks) {
			auto TargetBlock = Blocks[SourceBlock.id.value()];
			for (const auto& Argument : SourceBlock.arguments)
				Values[Argument.value.value()] = Builder.addBlockArgument(TargetID, TargetBlock, Maps.Types[Argument.type.value()]);
			for (const auto& Instruction : SourceBlock.instructions) {
				std::vector<ir::ValueId> Operands;
				for (auto Operand : Instruction.operands) Operands.push_back(Values[Operand.value()]);
				auto Immediates = Instruction.immediates;
				if (!Immediates.empty() && (Instruction.opcode == ir::Opcode::opcode_resource_load ||
					Instruction.opcode == ir::Opcode::opcode_resource_store ||
					Instruction.opcode == ir::Opcode::opcode_resource_sample ||
					Instruction.opcode == ir::Opcode::opcode_resource_query))
					Immediates[0] = Maps.Symbols[Immediates[0]].value();
				if (!Immediates.empty() && Instruction.opcode == ir::Opcode::opcode_access)
					Immediates[0] = Builder.module().strings.intern(Module.strings.get(ir::StringId{Immediates[0]})).value();
				auto Result = Builder.appendInstruction(TargetID, TargetBlock, Instruction.opcode,
					Instruction.type ? Maps.Types[Instruction.type.value()] : ir::TypeId{}, Operands, Immediates,
					Instruction.callee ? Maps.Functions[Instruction.callee.value()] : ir::FunctionId{});
				if (Instruction.result) Values[Instruction.result.value()] = Result;
			}
		}
		for (const auto& SourceBlock : Source.blocks) {
			auto TargetBlock = Blocks[SourceBlock.id.value()];
			if (SourceBlock.merge.kind != ir::MergeKind::merge_none) {
				Builder.setMerge(TargetID, TargetBlock, {.kind = SourceBlock.merge.kind,
					.merge_block = Blocks[SourceBlock.merge.merge_block.value()],
					.continue_block = SourceBlock.merge.continue_block ? Blocks[SourceBlock.merge.continue_block.value()] : ir::BlockId{}});
			}
			if (!SourceBlock.terminator) continue;
			ir::Terminator Terminator = *SourceBlock.terminator;
			for (auto& Operand : Terminator.operands) Operand = Values[Operand.value()];
			for (auto& Successor : Terminator.successors) {
				Successor.block = Blocks[Successor.block.value()];
				for (auto& Argument : Successor.arguments) Argument = Values[Argument.value()];
			}
			Builder.setTerminator(TargetID, TargetBlock, std::move(Terminator));
		}
	}
}

void Linker::copyMetadata(const ir::Module& Module, const ModuleMaps& Maps) {
	for (const auto& Entry : Module.entry_points) {
		ir::EntryPoint Target = Entry;
		Target.symbol = Maps.Symbols.at(Entry.symbol.value());
		Target.function = Maps.Functions.at(Entry.function.value());
		Target.source_name = Builder.module().strings.intern(Module.strings.get(Entry.source_name));
		for (auto& Contract : Target.parameter_contracts) {
			for (auto& Member : Contract.member_path)
				Member = Builder.module().strings.intern(Module.strings.get(Member));
			Contract.contract = Builder.module().strings.intern(Module.strings.get(Contract.contract));
		}
		auto Key = static_cast<std::uint32_t>(Target.stage);
		if (EntriesByStage.contains(Key)) {
			diagnose(LinkDiagnosticCode::link_duplicate_stage, {}, "program contains multiple entries for one stage");
			continue;
		}
		EntriesByStage.emplace(Key, Target);
		Builder.addEntryPoint(Target);
	}
	for (const auto& Resource : Module.resources) {
		auto Target = Resource;
		Target.symbol = Maps.Symbols.at(Resource.symbol.value());
		Target.type = Maps.Types.at(Resource.type.value());
		Target.binding.reset();
		for (auto& User : Target.users) User = Maps.Functions.at(User.value());
		Builder.addResource(std::move(Target));
	}
	for (const auto& Uniform : Module.uniforms) {
		auto Target = Uniform;
		Target.symbol = Maps.Symbols.at(Uniform.symbol.value());
		Target.type = Maps.Types.at(Uniform.type.value());
		Target.binding.reset();
		Builder.addUniform(std::move(Target));
	}
	for (const auto& Object : Module.storage_objects) {
		auto Target = Object;
		Target.symbol = Maps.Symbols.at(Object.symbol.value());
		Target.type = Maps.Types.at(Object.type.value());
		Target.binding.reset();
		Target.initializer.reset();
		Builder.addStorageObject(std::move(Target));
	}
}

void Linker::validateDefinitions() {
	for (const auto& Function : Builder.module().functions) {
		if (!Function.declaration) continue;
		auto Symbol = Builder.module().findSymbol(Function.symbol);
		auto Name = Builder.module().strings.get(Symbol->fully_qualified_name);
		diagnose(LinkDiagnosticCode::link_missing_definition, Name, "function declaration has no linked definition");
	}
}

void Linker::validateStageInterfaces() {
	constexpr std::array Stages = {ir::Stage::stage_vertex, ir::Stage::stage_tessellation_control,
		ir::Stage::stage_tessellation_evaluation, ir::Stage::stage_geometry, ir::Stage::stage_fragment};
	const ir::EntryPoint* Previous{};
	for (auto Stage : Stages) {
		auto Position = EntriesByStage.find(static_cast<std::uint32_t>(Stage));
		if (Position == EntriesByStage.end()) continue;
		if (Previous && interfaceOutput(*Previous) != interfaceInput(Position->second))
			diagnose(LinkDiagnosticCode::link_incompatible_stage_interface, {}, "adjacent shader stage interfaces are incompatible");
		Previous = &Position->second;
	}
}

ir::TypeId Linker::interfaceInput(const ir::EntryPoint& Entry) const {
	auto Function = Builder.module().findFunction(Entry.function);
	if (!Function || Function->parameters.empty()) return {};
	return unwrapInterfaceType(Function->parameters[0].type);
}

ir::TypeId Linker::interfaceOutput(const ir::EntryPoint& Entry) const {
	auto Function = Builder.module().findFunction(Entry.function);
	return Function ? unwrapInterfaceType(Function->return_type) : ir::TypeId{};
}

ir::TypeId Linker::unwrapInterfaceType(ir::TypeId TypeID) const {
	auto Type = Builder.module().findType(TypeID);
	if (!Type) return {};
	if (Type->kind == ir::TypeKind::type_pointer) return unwrapInterfaceType(Type->element_type);
	if (Type->kind == ir::TypeKind::type_patch || Type->kind == ir::TypeKind::type_primitive) return Type->element_type;
	return TypeID;
}

bool Linker::sameSignature(const ir::Function& Left, ir::TypeId ReturnType,
	std::span<const ir::TypeId> Parameters) const {
	if (Left.return_type != ReturnType || Left.parameters.size() != Parameters.size()) return false;
	for (std::size_t Index = 0; Index < Parameters.size(); ++Index)
		if (Left.parameters[Index].type != Parameters[Index]) return false;
	return true;
}

void Linker::diagnose(LinkDiagnosticCode Code, std::string_view Symbol, std::string_view Message) {
	Diagnostics.push_back({Code, std::string(Symbol), std::string(Message)});
}

}
