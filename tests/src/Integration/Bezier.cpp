#include <rtsl/Frontend/CompilerInstance.hpp>
#include <rtsl/IR/IR.hpp>
#include <rtsl/Serialization/Artifact.hpp>

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <iterator>

TEST_CASE("cubic Bezier tessellation example compiles to a verified linked program artifact") {
	std::ifstream Input(RTSL_BEZIER_FIXTURE, std::ios::binary);
	std::string Source(std::istreambuf_iterator<char>(Input), {});
	REQUIRE_FALSE(Source.empty());

	rtsl::CompilerInvocation Invocation;
	Invocation.setModuleName("bezier");
	Invocation.setInputName(RTSL_BEZIER_FIXTURE);
	Invocation.setInputBuffer(std::move(Source));
	rtsl::CompilerInstance Compiler;
	Compiler.setInvocation(std::move(Invocation));
	auto Compilation = Compiler.compileToLinkedRTIR();
	REQUIRE(Compilation.succeeded());
	REQUIRE(Compilation.Link.Module.entry_points.size() == 4);

	const auto& Control = Compilation.Link.Module.entry_points[0];
	REQUIRE(Control.stage == rtsl::ir::Stage::stage_tessellation_control);
	REQUIRE(Control.attributes.size() == 1);
	REQUIRE(std::get<rtsl::ir::TessellationControlConfiguration>(Control.configuration).output_control_points == 4);
	const auto& Evaluation = Compilation.Link.Module.entry_points[1];
	REQUIRE(Evaluation.stage == rtsl::ir::Stage::stage_tessellation_evaluation);
	const auto& Configuration = std::get<rtsl::ir::TessellationEvaluationConfiguration>(Evaluation.configuration);
	REQUIRE(Configuration.domain == rtsl::ir::TessellationDomain::tessellation_domain_isolines);
	REQUIRE(Configuration.spacing == rtsl::ir::TessellationSpacing::tessellation_spacing_equal);

	bool HasPatchStore{};
	for (const auto& Function : Compilation.Link.Module.functions)
		for (const auto& Block : Function.blocks)
			for (const auto& Instruction : Block.instructions)
				HasPatchStore = HasPatchStore || Instruction.opcode == rtsl::ir::Opcode::opcode_store;
	REQUIRE(HasPatchStore);

	rtsl::Artifact Program{.kind = rtsl::ArtifactKind::artifact_program, .module = std::move(Compilation.Link.Module)};
	REQUIRE(rtsl::ArtifactWriter{}.write(Program));
}
