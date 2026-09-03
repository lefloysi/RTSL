#include <rtsl/Frontend/CompilerInstance.hpp>
#include <rtsl/Serialization/Artifact.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>

TEST_CASE("compute resource operations compile to a linked program artifact") {
	rtsl::CompilerInvocation Invocation;
	Invocation.setModuleName("compute-resources");
	Invocation.setInputName(RTSL_COMPUTE_RESOURCE_FIXTURE);
	std::ifstream Input(RTSL_COMPUTE_RESOURCE_FIXTURE, std::ios::binary);
	Invocation.setInputBuffer({std::istreambuf_iterator<char>(Input), {}});
	rtsl::CompilerInstance Compiler;
	Compiler.setInvocation(std::move(Invocation));
	auto Compilation = Compiler.compileToLinkedRTIR();
	REQUIRE(Compilation.succeeded());
	REQUIRE(Compilation.Link.Module.entry_points.size() == 1);
	const auto& Entry = Compilation.Link.Module.entry_points.front();
	REQUIRE(Entry.stage == rtsl::ir::Stage::stage_compute);
	REQUIRE(std::get<rtsl::ir::ComputeConfiguration>(Entry.configuration).workgroup_size == std::array<std::uint32_t, 3>{16, 16, 1});
	const auto* Function = Compilation.Link.Module.findFunction(Entry.function);
	REQUIRE(Function != nullptr);
	REQUIRE(Function->parameters.size() == 3);
	REQUIRE(Function->parameters[0].builtin == rtsl::ir::Builtin::builtin_global_invocation_x);
	REQUIRE(Function->parameters[1].builtin == rtsl::ir::Builtin::builtin_global_invocation_y);
	REQUIRE(Function->parameters[2].builtin == rtsl::ir::Builtin::builtin_global_invocation_z);
	const auto& Instructions = Function->blocks.front().instructions;
	const auto Load = std::ranges::find_if(Instructions, [](const rtsl::ir::Instruction& Instruction) {
		return Instruction.opcode == rtsl::ir::Opcode::opcode_resource_load && Instruction.operands.size() == 1;
	});
	const auto Store = std::ranges::find_if(Instructions, [](const rtsl::ir::Instruction& Instruction) {
		return Instruction.opcode == rtsl::ir::Opcode::opcode_resource_store;
	});
	const auto Query = std::ranges::find_if(Instructions, [](const rtsl::ir::Instruction& Instruction) {
		return Instruction.opcode == rtsl::ir::Opcode::opcode_resource_query;
	});
	REQUIRE(Load != Instructions.end());
	REQUIRE(Store != Instructions.end());
	REQUIRE(Query != Instructions.end());
	REQUIRE(Load->immediates.size() == 1);
	REQUIRE(Store->immediates.size() == 1);
	REQUIRE(Query->immediates.size() == 1);
	REQUIRE(Store->operands.size() == 2);
	REQUIRE(Query->operands.empty());

	rtsl::Artifact Program{.kind = rtsl::ArtifactKind::artifact_program, .module = std::move(Compilation.Link.Module)};
	auto Encoded = rtsl::ArtifactWriter{}.write(Program);
	REQUIRE(Encoded);
	auto Decoded = rtsl::ArtifactReader{}.read(Encoded.bytes);
	REQUIRE(Decoded);
	const auto& DecodedEntry = Decoded.artifact->module.entry_points.front();
	const auto* DecodedFunction = Decoded.artifact->module.findFunction(DecodedEntry.function);
	REQUIRE(DecodedFunction != nullptr);
	REQUIRE(DecodedFunction->parameters[0].builtin == rtsl::ir::Builtin::builtin_global_invocation_x);
	REQUIRE(DecodedFunction->parameters[1].builtin == rtsl::ir::Builtin::builtin_global_invocation_y);
	REQUIRE(DecodedFunction->parameters[2].builtin == rtsl::ir::Builtin::builtin_global_invocation_z);
}
