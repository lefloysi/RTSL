#include <rtsl/Frontend/CompilerInstance.hpp>
#include <rtsl/Serialization/Artifact.hpp>

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <iterator>
#include <unordered_map>

TEST_CASE("triangle example compiles to a verified linked program artifact") {
	std::ifstream Input(RTSL_TRIANGLE_FIXTURE, std::ios::binary);
	std::string Source(std::istreambuf_iterator<char>(Input), {});
	REQUIRE_FALSE(Source.empty());

	rtsl::CompilerInvocation Invocation;
	Invocation.setModuleName("triangle");
	Invocation.setInputName(RTSL_TRIANGLE_FIXTURE);
	Invocation.setInputBuffer(Source);
	rtsl::CompilerInstance Compiler;
	Compiler.setInvocation(Invocation);
	auto Compilation = Compiler.compileToLinkedRTIR();
	REQUIRE(Compilation.succeeded());
	REQUIRE(Compilation.Link.Module.entry_points.size() == 2);
	REQUIRE(Compilation.Link.Module.functions.size() == 3);
	const rtsl::ir::Type* VertexType{};
	for (const auto& Type : Compilation.Link.Module.types) {
		if (Type.kind != rtsl::ir::TypeKind::type_structure) continue;
		if (Compilation.Link.Module.strings.get(Type.name) != "Vertex") continue;
		VertexType = &Type;
		break;
	}
	REQUIRE(VertexType != nullptr);
	REQUIRE(VertexType->members.size() == 2);
	REQUIRE(Compilation.Link.Module.strings.get(VertexType->members[0].name) == "position");
	REQUIRE(Compilation.Link.Module.strings.get(VertexType->members[1].name) == "color");
	const auto& FragmentEntry = Compilation.Link.Module.entry_points[1];
	REQUIRE(FragmentEntry.attributes.size() == 1);
	REQUIRE(Compilation.Link.Module.strings.get(FragmentEntry.attributes[0].name) == "stage");
	REQUIRE(FragmentEntry.attributes[0].tokens.size() == 1);
	REQUIRE(Compilation.Link.Module.strings.get(FragmentEntry.attributes[0].tokens[0]) == "fragment");
	REQUIRE(FragmentEntry.parameter_contracts.size() == 1);
	REQUIRE(FragmentEntry.parameter_contracts[0].parameter_index == 0);
	REQUIRE(FragmentEntry.parameter_contracts[0].member_path.size() == 1);
	REQUIRE(Compilation.Link.Module.strings.get(FragmentEntry.parameter_contracts[0].member_path[0]) == "color");
	REQUIRE(Compilation.Link.Module.strings.get(FragmentEntry.parameter_contracts[0].contract) == "flat");

	const rtsl::ir::EntryPoint* VertexEntry{};
	for (const auto& Entry : Compilation.Link.Module.entry_points)
		if (Entry.stage == rtsl::ir::Stage::stage_vertex) VertexEntry = &Entry;
	REQUIRE(VertexEntry != nullptr);
	const rtsl::ir::Function* VertexFunction = Compilation.Link.Module.findFunction(VertexEntry->function);
	REQUIRE(VertexFunction != nullptr);
	std::unordered_map<std::uint32_t, rtsl::ir::TypeId> ValueTypes;
	for (const auto& Parameter : VertexFunction->parameters) ValueTypes.emplace(Parameter.value.value(), Parameter.type);
	const rtsl::ir::Instruction* VertexConstruction{};
	const rtsl::ir::Instruction* VertexCall{};
	const rtsl::ir::Function* Constructor{};
	for (const auto& Function : Compilation.Link.Module.functions) {
		const auto* Symbol = Compilation.Link.Module.findSymbol(Function.symbol);
		if (Symbol && Compilation.Link.Module.strings.get(Symbol->fully_qualified_name) == "Vertex::Vertex(Point)")
			Constructor = &Function;
	}
	REQUIRE(Constructor != nullptr);
	for (const auto& Parameter : Constructor->parameters) ValueTypes.emplace(Parameter.value.value(), Parameter.type);
	for (const auto& Block : Constructor->blocks)
		for (const auto& Instruction : Block.instructions)
			if (Instruction.result) {
				ValueTypes.emplace(Instruction.result.value(), Instruction.type);
				if (Instruction.opcode == rtsl::ir::Opcode::opcode_construct && Instruction.type == VertexType->id)
				VertexConstruction = &Instruction;
			}
	for (const auto& Block : VertexFunction->blocks)
		for (const auto& Instruction : Block.instructions) {
			if (Instruction.result) ValueTypes.emplace(Instruction.result.value(), Instruction.type);
			if (Instruction.opcode == rtsl::ir::Opcode::opcode_call && Instruction.callee == Constructor->id)
				VertexCall = &Instruction;
		}
	REQUIRE(VertexConstruction != nullptr);
	REQUIRE(VertexCall != nullptr);
	REQUIRE(VertexConstruction->operands.size() == VertexType->members.size());
	for (std::size_t Index = 0; Index < VertexConstruction->operands.size(); ++Index)
		REQUIRE(ValueTypes.at(VertexConstruction->operands[Index].value()) == VertexType->members[Index].type);

	rtsl::Artifact Program{.kind = rtsl::ArtifactKind::artifact_program, .module = std::move(Compilation.Link.Module)};
	auto Encoded = rtsl::ArtifactWriter{}.write(Program);
	REQUIRE(Encoded);
	auto Decoded = rtsl::ArtifactReader{}.read(Encoded.bytes);
	REQUIRE(Decoded);
	REQUIRE(Decoded.artifact->kind == rtsl::ArtifactKind::artifact_program);
	REQUIRE(Decoded.artifact->module.entry_points[1].parameter_contracts.size() == 1);
}
