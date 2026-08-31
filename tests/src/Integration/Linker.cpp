#include <rtsl/Linker/Linker.hpp>

#include <catch2/catch_test_macros.hpp>


namespace rtsl::tests {

ir::Module makeSingleEntryModule(std::string_view ModuleName, std::string_view FunctionName, ir::Stage Stage,
	bool FloatingParameter, bool Declaration = false) {
	ir::ModuleBuilder Builder(ModuleName);
	ir::Type UnsignedType;
	UnsignedType.kind = ir::TypeKind::type_unsigned_integer;
	UnsignedType.bit_width = 32;
	auto Unsigned = Builder.internType(UnsignedType);
	ir::Type FloatingType;
	FloatingType.kind = ir::TypeKind::type_floating;
	FloatingType.bit_width = 32;
	auto Floating = Builder.internType(FloatingType);
	auto Symbol = Builder.addSymbol(FunctionName);
	auto ParameterType = FloatingParameter ? Floating : Unsigned;
	auto Function = Builder.addFunction(Symbol, Unsigned, std::span(&ParameterType, 1), {}, Declaration);
	if (!Declaration) {
		auto Block = Builder.addBlock(Function);
		auto Parameter = Builder.module().findFunction(Function)->parameters[0].value;
		ir::Terminator Return{.kind = ir::TerminatorKind::terminator_return_value};
		Return.operands.push_back(Parameter);
		Builder.setTerminator(Function, Block, std::move(Return));
	}
	Builder.addEntryPoint({.symbol = Symbol, .function = Function, .stage = Stage});
	return Builder.takeModule();
}

ir::Module makeTessellationControlModule() {
	ir::ModuleBuilder Builder("tess");
	ir::Type ScalarType;
	ScalarType.kind = ir::TypeKind::type_unsigned_integer;
	ScalarType.bit_width = 32;
	auto Scalar = Builder.internType(ScalarType);
	ir::Type PatchType;
	PatchType.kind = ir::TypeKind::type_patch;
	PatchType.element_type = Scalar;
	auto Patch = Builder.internType(PatchType);
	ir::Type ReferenceType;
	ReferenceType.kind = ir::TypeKind::type_pointer;
	ReferenceType.element_type = Patch;
	auto Reference = Builder.internType(ReferenceType);
	auto Symbol = Builder.addSymbol("tess::control");
	auto Function = Builder.addFunction(Symbol, Scalar, std::span(&Reference, 1));
	auto Block = Builder.addBlock(Function);
	std::uint32_t One = 1;
	auto Value = Builder.appendInstruction(Function, Block, ir::Opcode::opcode_constant_integer, Scalar, {}, std::span(&One, 1));
	ir::Terminator Return{.kind = ir::TerminatorKind::terminator_return_value};
	Return.operands.push_back(Value);
	Builder.setTerminator(Function, Block, std::move(Return));
	Builder.addEntryPoint({.symbol = Symbol, .function = Function, .stage = ir::Stage::stage_tessellation_control,
		.configuration = ir::TessellationControlConfiguration{.output_control_points = 1}});
	return Builder.takeModule();
}

}

TEST_CASE("linker synthesizes an exact identity vertex stage for tessellation") {
	auto Module = rtsl::tests::makeTessellationControlModule();
	rtsl::Linker Linker;
	auto Result = Linker.link("program", std::span(&Module, 1));
	REQUIRE(Result.succeeded());
	REQUIRE(Result.Module.entry_points.size() == 2);
	bool HasVertex = false;
	for (const auto& Entry : Result.Module.entry_points)
		HasVertex |= Entry.stage == rtsl::ir::Stage::stage_vertex;
	REQUIRE(HasVertex);
}

TEST_CASE("linker diagnoses a missing function definition") {
	rtsl::ir::ModuleBuilder Builder("library");
	rtsl::ir::Type VoidType;
	VoidType.kind = rtsl::ir::TypeKind::type_void;
	auto Void = Builder.internType(VoidType);
	auto Symbol = Builder.addSymbol("library::missing");
	(void)Builder.addFunction(Symbol, Void, {}, {}, true);
	auto Module = Builder.takeModule();
	rtsl::Linker Linker;
	auto Result = Linker.link("program", std::span(&Module, 1));
	REQUIRE_FALSE(Result.succeeded());
	REQUIRE(Result.Diagnostics[0].Code == rtsl::LinkDiagnosticCode::link_missing_definition);
}

TEST_CASE("linker preserves exported symbols") {
	rtsl::ir::ModuleBuilder Builder("library");
	rtsl::ir::Type VoidType;
	VoidType.kind = rtsl::ir::TypeKind::type_void;
	auto Void = Builder.internType(VoidType);
	auto Symbol = Builder.addSymbol("library::exported", true);
	auto Function = Builder.addFunction(Symbol, Void, {});
	auto Block = Builder.addBlock(Function);
	Builder.setTerminator(Function, Block, {.kind = rtsl::ir::TerminatorKind::terminator_return});
	auto Module = Builder.takeModule();

	rtsl::Linker Linker;
	auto Result = Linker.link("program", std::span(&Module, 1));
	REQUIRE(Result.succeeded());
	REQUIRE(Result.Module.symbols.size() == 1);
	REQUIRE(Result.Module.symbols[0].exported);
}

TEST_CASE("linker rejects incompatible adjacent stage interfaces") {
	auto Vertex = rtsl::tests::makeSingleEntryModule("vertex", "vertex::main", rtsl::ir::Stage::stage_vertex, false);
	auto Fragment = rtsl::tests::makeSingleEntryModule("fragment", "fragment::main", rtsl::ir::Stage::stage_fragment, true);
	std::array Modules{std::move(Vertex), std::move(Fragment)};
	rtsl::Linker Linker;
	auto Result = Linker.link("program", Modules);
	REQUIRE_FALSE(Result.succeeded());
	bool Found = false;
	for (const auto& Diagnostic : Result.Diagnostics)
		Found |= Diagnostic.Code == rtsl::LinkDiagnosticCode::link_incompatible_stage_interface;
	REQUIRE(Found);
}

TEST_CASE("linker rejects duplicate canonical definitions") {
	auto First = rtsl::tests::makeSingleEntryModule("first", "shared::main", rtsl::ir::Stage::stage_vertex, false);
	auto Second = rtsl::tests::makeSingleEntryModule("second", "shared::main", rtsl::ir::Stage::stage_vertex, false);
	std::array Modules{std::move(First), std::move(Second)};
	rtsl::Linker Linker;
	auto Result = Linker.link("program", Modules);
	REQUIRE_FALSE(Result.succeeded());
	bool Found = false;
	for (const auto& Diagnostic : Result.Diagnostics)
		Found |= Diagnostic.Code == rtsl::LinkDiagnosticCode::link_duplicate_definition;
	REQUIRE(Found);
}
