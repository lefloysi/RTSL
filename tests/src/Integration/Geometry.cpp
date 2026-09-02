#include <rtsl/Frontend/CompilerInstance.hpp>
#include <rtsl/IR/IR.hpp>
#include <rtsl/Serialization/Artifact.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <unordered_map>

TEST_CASE("geometry entry compiles to a verified linked program artifact") {
	rtsl::CompilerInvocation Invocation;
	Invocation.setModuleName("geometry");
	Invocation.setInputName("geometry.rtsl");
	Invocation.setInputBuffer(R"(
struct Vertex {
	vec4 color;
}
@stage : vertex
fn shadow(Vertex value) -> Vertex {
	return value;
}
@stage : geometry
fn shadow(triangle<Vertex> input) -> triangle_strip<Vertex, 3> {
	emit input[0];
	emit input[1];
	emit input[2];
}
@stage : fragment
fn shadow(Vertex value) -> vec4 {
	return value.color;
}
)");
	rtsl::CompilerInstance Compiler;
	Compiler.setInvocation(std::move(Invocation));
	auto Compilation = Compiler.compileToLinkedRTIR();
	REQUIRE(Compilation.succeeded());
	REQUIRE(Compilation.Link.Module.entry_points.size() == 3);

	const auto GeometryEntry = std::ranges::find_if(Compilation.Link.Module.entry_points,
		[](const rtsl::ir::EntryPoint& Entry) { return Entry.stage == rtsl::ir::Stage::stage_geometry; });
	REQUIRE(GeometryEntry != Compilation.Link.Module.entry_points.end());
	const auto Configuration = std::get<rtsl::ir::GeometryConfiguration>(GeometryEntry->configuration);
	REQUIRE(Configuration.input == rtsl::ir::PrimitiveTopology::primitive_triangles);
	REQUIRE(Configuration.output == rtsl::ir::PrimitiveTopology::primitive_triangle_strip);
	REQUIRE(Configuration.maximum_vertices == 3);
	REQUIRE(Configuration.invocations == 1);

	const auto* Function = Compilation.Link.Module.findFunction(GeometryEntry->function);
	REQUIRE(Function != nullptr);
	const auto& Instructions = Function->blocks.front().instructions;
	const auto EmptyStrip = std::ranges::find_if(Instructions, [](const rtsl::ir::Instruction& Instruction) {
		return Instruction.opcode == rtsl::ir::Opcode::opcode_construct && Instruction.operands.empty();
	});
	REQUIRE(EmptyStrip != Instructions.end());
	REQUIRE(std::ranges::count(Instructions, rtsl::ir::Opcode::opcode_call,
		&rtsl::ir::Instruction::opcode) == 3);
	REQUIRE(Function->blocks.front().terminator->kind == rtsl::ir::TerminatorKind::terminator_return_value);

	rtsl::Artifact Program{.kind = rtsl::ArtifactKind::artifact_program, .module = std::move(Compilation.Link.Module)};
	auto Encoded = rtsl::ArtifactWriter{}.write(Program);
	REQUIRE(Encoded);
	auto Decoded = rtsl::ArtifactReader{}.read(Encoded.bytes);
	REQUIRE(Decoded);
	REQUIRE(Decoded.artifact->module.entry_points.size() == 3);
}

TEST_CASE("geometry triangle elements retain inherited Position members") {
	rtsl::CompilerInvocation Invocation;
	Invocation.setModuleName("geometry-position");
	Invocation.setInputName("geometry-position.rtsl");
	Invocation.setInputBuffer(R"(
struct Vertex : Position {}
@stage : vertex
fn main(Vertex value) -> Vertex {
	return value;
}
@stage : geometry
fn main(triangle<Vertex> input) -> triangle_strip<Vertex, 3> {
	var vec2 projected = input[0].xy();
	emit input[0];
	emit input[1];
	emit input[2];
}
@stage : fragment
fn main(Vertex value) -> vec4 {
	return vec4(1.0, 1.0, 1.0, 1.0);
}
)");
	rtsl::CompilerInstance Compiler;
	Compiler.setInvocation(std::move(Invocation));
	auto Compilation = Compiler.compileToLinkedRTIR();
	REQUIRE(Compilation.succeeded());
}

TEST_CASE("stage entries are linked in independent function-name groups") {
	rtsl::CompilerInvocation Invocation;
	Invocation.setModuleName("program-groups");
	Invocation.setInputName("program-groups.rtsl");
	Invocation.setInputBuffer(R"(
struct Vertex {
	vec4 color;
}

@stage : vertex
fn main(Vertex value) -> Vertex {
	return value;
}

@stage : fragment
fn main(Vertex value) -> vec4 {
	return value.color;
}
@stage : vertex
fn main_2(Vertex value) -> Vertex {
	return value;
}
@stage : fragment
fn main_2(Vertex value) -> vec4 {
	return value.color;
}
)");
	rtsl::CompilerInstance Compiler;
	Compiler.setInvocation(std::move(Invocation));
	auto Compilation = Compiler.compileToLinkedRTIR();
	REQUIRE(Compilation.succeeded());

	std::unordered_map<std::string, std::uint32_t> StagesByName;
	for (const rtsl::ir::EntryPoint& Entry : Compilation.Link.Module.entry_points)
		++StagesByName[std::string(Compilation.Link.Module.strings.get(Entry.source_name))];
	REQUIRE(StagesByName.size() == 2);
	REQUIRE(StagesByName.at("main") == 2);
	REQUIRE(StagesByName.at("main_2") == 2);
}

TEST_CASE("geometry can return a constructed triangle strip without emit") {
	rtsl::CompilerInvocation Invocation;
	Invocation.setModuleName("geometry-return");
	Invocation.setInputName("geometry-return.rtsl");
	Invocation.setInputBuffer(R"(
struct Vertex {}
@stage : vertex
fn expand(Vertex value) -> Vertex {
	return value;
}
@stage : geometry
fn expand(triangle<Vertex> input) -> triangle_strip<Vertex, 3> {
	return triangle_strip<Vertex, 3>(input[0], input[1], input[2]);
}
@stage : fragment
fn expand(Vertex value) -> vec4 {
	return vec4(1.0, 1.0, 1.0, 1.0);
}
)");
	rtsl::CompilerInstance Compiler;
	Compiler.setInvocation(std::move(Invocation));
	auto Compilation = Compiler.compileToLinkedRTIR();
	REQUIRE(Compilation.succeeded());
	const auto Geometry = std::ranges::find_if(Compilation.Link.Module.entry_points,
		[](const rtsl::ir::EntryPoint& Entry) { return Entry.stage == rtsl::ir::Stage::stage_geometry; });
	REQUIRE(Geometry != Compilation.Link.Module.entry_points.end());
	const auto* Function = Compilation.Link.Module.findFunction(Geometry->function);
	REQUIRE(Function != nullptr);
	REQUIRE(std::ranges::count(Function->blocks.front().instructions, rtsl::ir::Opcode::opcode_construct,
		&rtsl::ir::Instruction::opcode) == 1);
	REQUIRE(std::ranges::count(Function->blocks.front().instructions, rtsl::ir::Opcode::opcode_insert,
		&rtsl::ir::Instruction::opcode) == 0);
}
