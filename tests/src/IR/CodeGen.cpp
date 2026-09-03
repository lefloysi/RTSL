#include <rtsl/CodeGen/CodeGenerator.hpp>
#include <rtsl/Frontend/CompilerInstance.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

namespace {

const rtsl::ir::Function& stageFunction(const rtsl::ir::Module& Module, rtsl::ir::Stage Stage) {
	auto Entry = std::ranges::find_if(Module.entry_points, [Stage](const rtsl::ir::EntryPoint& Value) { return Value.stage == Stage; });
	REQUIRE(Entry != Module.entry_points.end());
	auto* Function = Module.findFunction(Entry->function);
	REQUIRE(Function != nullptr);
	return *Function;
}

} // namespace

TEST_CASE("semantic AST lowers to verified RTIR") {
	rtsl::CompilerInvocation Invocation;
	Invocation.setModuleName("shader");
	Invocation.setInputName("shader.rtsl");
	Invocation.setInputBuffer(R"(
uniform u32 frame;
storage u32 counter;
@binding : data
var buffer<u32, u32> data;
@stage : vertex
fn main(u32 value) -> u32 {
	return value;
}
)");
	rtsl::CompilerInstance Compiler;
	Compiler.setInvocation(std::move(Invocation));
	REQUIRE(Compiler.execute());
	rtsl::CodeGenerator Generator("shader");
	auto Result = Generator.generate(*Compiler.getASTContext());
	REQUIRE(Result.succeeded());
	REQUIRE(Result.Module.uniforms.size() == 1);
	REQUIRE(Result.Module.storage_objects.size() == 1);
	REQUIRE(Result.Module.resources.size() == 1);
	REQUIRE(Result.Module.entry_points.size() == 1);
	REQUIRE(Result.Module.entry_points[0].stage == rtsl::ir::Stage::stage_vertex);
}

TEST_CASE("matrix-vector multiplication retains the canonical Position vector type") {
	rtsl::CompilerInvocation Invocation;
	Invocation.setModuleName("matrix-vector");
	Invocation.setInputName("matrix-vector.rtsl");
	Invocation.setInputBuffer(R"(
uniform mat4 mvp;
struct Vertex : Position {
	fn Vertex(vec3 value) {
		position = vec4(value, 1.0);
		position = mvp * position;
	}
}
@stage : vertex
fn main(vec3 value) -> Vertex {
	return Vertex(value);
}
)");
	rtsl::CompilerInstance Compiler;
	Compiler.setInvocation(std::move(Invocation));
	REQUIRE(Compiler.execute());
	rtsl::CodeGenerator Generator("matrix-vector");
	auto Result = Generator.generate(*Compiler.getASTContext());
	for (const auto& Diagnostic : Result.Diagnostics) INFO(Diagnostic.Message);
	for (const auto& Issue : Result.Verification.issues()) INFO(Issue.context << ": " << Issue.message);
	REQUIRE(Result.succeeded());
}

TEST_CASE("storage reads lower to a typed resource load") {
	rtsl::CompilerInvocation Invocation;
	Invocation.setModuleName("storage_read");
	Invocation.setInputName("storage-read.rtsl");
	Invocation.setInputBuffer(R"(
storage u32 counter;
@stage : vertex
fn main(u32 value) -> u32 {
	return counter;
}
)");
	rtsl::CompilerInstance Compiler;
	Compiler.setInvocation(std::move(Invocation));
	auto Compilation = Compiler.compileToLinkedRTIR();
	REQUIRE(Compilation.succeeded());
	REQUIRE(Compilation.Link.Module.storage_objects.size() == 1);
	const auto& Function = stageFunction(Compilation.Link.Module, rtsl::ir::Stage::stage_vertex);
	REQUIRE(std::ranges::any_of(Function.blocks[0].instructions,
		[](const rtsl::ir::Instruction& instruction) { return instruction.opcode == rtsl::ir::Opcode::opcode_resource_load; }));
}

TEST_CASE("compute workgroup size is preserved in backend-neutral metadata") {
	rtsl::CompilerInvocation Invocation;
	Invocation.setInputName("compute.rtsl");
	Invocation.setInputBuffer(R"(
@stage : compute
fn main<8, 4, 2>(usize x, usize y, usize z) {
}
)");
	rtsl::CompilerInstance Compiler;
	Compiler.setInvocation(std::move(Invocation));
	REQUIRE(Compiler.execute());
	rtsl::CodeGenerator Generator("compute");
	auto Result = Generator.generate(*Compiler.getASTContext());
	for (const auto& Diagnostic : Result.Diagnostics) INFO(Diagnostic.Message);
	for (const auto& Issue : Result.Verification.issues()) INFO(Issue.context << ": " << Issue.message);
	REQUIRE(Result.succeeded());
	auto Configuration = std::get<rtsl::ir::ComputeConfiguration>(Result.Module.entry_points[0].configuration);
	REQUIRE(Configuration.workgroup_size == std::array<std::uint32_t, 3>{8, 4, 2});
	const auto& Function = stageFunction(Result.Module, rtsl::ir::Stage::stage_compute);
	REQUIRE(Function.parameters.size() == 3);
	REQUIRE(Function.parameters[0].builtin == rtsl::ir::Builtin::builtin_global_invocation_x);
	REQUIRE(Function.parameters[1].builtin == rtsl::ir::Builtin::builtin_global_invocation_y);
	REQUIRE(Function.parameters[2].builtin == rtsl::ir::Builtin::builtin_global_invocation_z);
}

TEST_CASE("compute resource indexing and image queries lower to resource operations") {
	rtsl::CompilerInvocation Invocation;
	Invocation.setModuleName("compute-resources");
	Invocation.setInputName("compute-resources.rtsl");
	Invocation.setInputBuffer(R"(
struct Header {
	u32 count;
}
var buffer<Header, u32> values;
var image_2d<vec4> output;
@stage : compute
fn main<8, 4, 1>(usize x, usize y, usize z) {
	var u32 count = values.count;
	var u32 value = values[x];
	values[x] = value;
	var vec2<usize> extent = output.size();
	output[x, y] = vec4(0.0, 0.0, 0.0, 1.0);
}
)");
	rtsl::CompilerInstance Compiler;
	Compiler.setInvocation(std::move(Invocation));
	for (const auto& Diagnostic : Compiler.getDiagnostics().diagnostics()) INFO(Diagnostic.Message);
	REQUIRE(Compiler.execute());
	for (const auto& Diagnostic : Compiler.getDiagnostics().diagnostics()) INFO(Diagnostic.Message);
	rtsl::CodeGenerator Generator("compute-resources");
	auto Generated = Generator.generate(*Compiler.getASTContext());
	for (const auto& Diagnostic : Generated.Diagnostics) INFO(Diagnostic.Message);
	for (const auto& Issue : Generated.Verification.issues()) INFO(Issue.context << ": " << Issue.message);
	REQUIRE(Generated.succeeded());
	const auto& Function = stageFunction(Generated.Module, rtsl::ir::Stage::stage_compute);
	REQUIRE(std::ranges::count_if(Function.blocks.front().instructions, [](const rtsl::ir::Instruction& Instruction) {
		return Instruction.opcode == rtsl::ir::Opcode::opcode_resource_load;
	}) >= 2);
	REQUIRE(std::ranges::any_of(Function.blocks.front().instructions, [](const rtsl::ir::Instruction& Instruction) {
		return Instruction.opcode == rtsl::ir::Opcode::opcode_resource_store;
	}));
	REQUIRE(std::ranges::any_of(Function.blocks.front().instructions, [](const rtsl::ir::Instruction& Instruction) {
		return Instruction.opcode == rtsl::ir::Opcode::opcode_resource_query;
	}));
}

TEST_CASE("deduced primary function templates lower as concrete specializations") {
	rtsl::CompilerInvocation Invocation;
	Invocation.setInputName("generic-call.rtsl");
	Invocation.setInputBuffer(R"(
template<typename T>
fn identity(T value) -> T { return value; }
fn main() -> i32 { var i32 value = 1; return identity(value); }
)");
	rtsl::CompilerInstance Compiler;
	Compiler.setInvocation(std::move(Invocation));
	REQUIRE(Compiler.execute());
	rtsl::CodeGenerator Generator("generic-call");
	auto Result = Generator.generate(*Compiler.getASTContext());
	REQUIRE(Result.succeeded());
	REQUIRE(std::ranges::count_if(Result.Module.functions, [](const rtsl::ir::Function& Function) { return !Function.implicit; }) == 2);
	bool HasCall{};
	for (const auto& Function : Result.Module.functions)
		for (const auto& Block : Function.blocks)
			HasCall = HasCall || std::ranges::any_of(Block.instructions,
				[](const rtsl::ir::Instruction& Instruction) { return Instruction.opcode == rtsl::ir::Opcode::opcode_call; });
	REQUIRE(HasCall);
}

TEST_CASE("entry attributes are preserved as backend metadata") {
	rtsl::CompilerInvocation Invocation;
	Invocation.setInputName("stages.rtsl");
	Invocation.setInputBuffer(R"(

struct Vertex {}
@stage : tess_control
fn control<4>() {
}
@stage : tess_eval
fn evaluate(const quad_patch<Vertex>& patch, tessellation<fractional_odd, cw>) -> Vertex {
}
@stage : geometry
fn expand<5>(triangle<Vertex> input) -> triangle_strip<Vertex, 6> {
    emit input[0];
}
)");
	rtsl::CompilerInstance Compiler;
	Compiler.setInvocation(std::move(Invocation));
	REQUIRE(Compiler.execute());
	rtsl::CodeGenerator Generator("stages");
	auto Result = Generator.generate(*Compiler.getASTContext());
	REQUIRE(Result.succeeded());
	REQUIRE(Result.Module.entry_points.size() == 3);
	const auto& Control = std::get<rtsl::ir::TessellationControlConfiguration>(Result.Module.entry_points[0].configuration);
	REQUIRE(Control.output_control_points == 4);
	const auto& Evaluation = std::get<rtsl::ir::TessellationEvaluationConfiguration>(Result.Module.entry_points[1].configuration);
	REQUIRE(Evaluation.domain == rtsl::ir::TessellationDomain::tessellation_domain_quads);
	REQUIRE(Evaluation.spacing == rtsl::ir::TessellationSpacing::tessellation_spacing_fractional_odd);
	REQUIRE(Evaluation.winding == rtsl::ir::Winding::winding_clockwise);
	const auto& Geometry = std::get<rtsl::ir::GeometryConfiguration>(Result.Module.entry_points[2].configuration);
	REQUIRE(Geometry.input == rtsl::ir::PrimitiveTopology::primitive_triangles);
	REQUIRE(Geometry.output == rtsl::ir::PrimitiveTopology::primitive_triangle_strip);
	REQUIRE(Geometry.maximum_vertices == 6);
	REQUIRE(Geometry.invocations == 1);
	const auto Triangle = std::ranges::find_if(Result.Module.types, [&](const rtsl::ir::Type& Type) {
		return Type.kind == rtsl::ir::TypeKind::type_primitive && Result.Module.strings.get(Type.name) == "triangle";
	});
	REQUIRE(Triangle != Result.Module.types.end());
	REQUIRE(Triangle->element_count == 3);
}

TEST_CASE("geometry emit requires the triangle strip vertex type") {
	rtsl::CompilerInvocation Invocation;
	Invocation.setInputName("geometry-emit.rtsl");
	Invocation.setInputBuffer(R"(
struct Vertex {}
@stage : geometry
fn main(triangle<Vertex> input) -> triangle_strip<Vertex, 6> {
	emit vec4(1.0, 0.0, 0.0, 1.0);
}
)");
	rtsl::CompilerInstance Compiler;
	Compiler.setInvocation(std::move(Invocation));
	REQUIRE_FALSE(Compiler.execute());
}

TEST_CASE("an emitter is semantically rejected outside a geometry return type") {
	rtsl::CompilerInvocation Invocation;
	Invocation.setInputName("unrealized-emitter.rtsl");
	Invocation.setInputBuffer(R"(
struct Vertex {}
@stage : vertex
fn vertex(Vertex value) -> Vertex {
	emit value;
}
)");
	rtsl::CompilerInstance Compiler;
	Compiler.setInvocation(std::move(Invocation));
	REQUIRE_FALSE(Compiler.execute());
}

TEST_CASE("tessellation outer levels are indexed") {
	rtsl::CompilerInvocation Invocation;
	Invocation.setInputName("outer-levels.rtsl");
	Invocation.setInputBuffer(R"(
struct Vertex {}
@stage : tess_control
fn main<4>(patch<Vertex>& patch) -> Vertex {
	patch.outer[0] = 1.0;
	return patch.current;
}
)");
	rtsl::CompilerInstance Compiler;
	Compiler.setInvocation(std::move(Invocation));
	REQUIRE(Compiler.execute());
	rtsl::CodeGenerator Generator("outer-levels");
	auto Result = Generator.generate(*Compiler.getASTContext());
	REQUIRE(Result.succeeded());
	const auto& Function = stageFunction(Result.Module, rtsl::ir::Stage::stage_tessellation_control);
	REQUIRE(std::ranges::count(Function.blocks.front().instructions, rtsl::ir::Opcode::opcode_store,
		&rtsl::ir::Instruction::opcode) == 1);

	rtsl::CompilerInvocation InvalidInvocation;
	InvalidInvocation.setInputName("scalar-outer.rtsl");
	InvalidInvocation.setInputBuffer(R"(
struct Vertex {}
@stage : tess_control
fn main(patch<Vertex>& patch) -> Vertex {
	patch.outer = 1.0;
	return patch.current;
}
)");
	rtsl::CompilerInstance InvalidCompiler;
	InvalidCompiler.setInvocation(std::move(InvalidInvocation));
	REQUIRE_FALSE(InvalidCompiler.execute());
}

TEST_CASE("a compute barrier lowers from a named statement label") {
	rtsl::CompilerInvocation Invocation;
	Invocation.setInputName("barrier.rtsl");
	Invocation.setInputBuffer(R"(
@stage : compute
fn main<1, 1, 1>(usize x, usize y, usize z) {
	ready:
	var u32 value = 0;
}
)");
	rtsl::CompilerInstance Compiler;
	Compiler.setInvocation(std::move(Invocation));
	REQUIRE(Compiler.execute());
	rtsl::CodeGenerator Generator("barrier");
	auto Result = Generator.generate(*Compiler.getASTContext());
	REQUIRE(Result.succeeded());
	const auto& Function = stageFunction(Result.Module, rtsl::ir::Stage::stage_compute);
	REQUIRE(std::ranges::any_of(Function.blocks.front().instructions,
		[](const rtsl::ir::Instruction& Instruction) { return Instruction.opcode == rtsl::ir::Opcode::opcode_barrier; }));
}

TEST_CASE("a barrier is rejected outside compute and tessellation control") {
	rtsl::CompilerInvocation Invocation;
	Invocation.setInputName("invalid-barrier.rtsl");
	Invocation.setInputBuffer(R"(
@stage : fragment
fn main() -> f32 {
	ready:
	return 1.0;
}
)");
	rtsl::CompilerInstance Compiler;
	Compiler.setInvocation(std::move(Invocation));
	REQUIRE(Compiler.execute());
	rtsl::CodeGenerator Generator("invalid-barrier");
	auto Result = Generator.generate(*Compiler.getASTContext());
	REQUIRE_FALSE(Result.succeeded());
	REQUIRE(std::ranges::any_of(Result.Diagnostics,
		[](const rtsl::CodeGenDiagnostic& Diagnostic) { return Diagnostic.Message == "barriers are only valid in compute and tessellation-control entry functions"; }));
}

TEST_CASE("emit syntax appends to the geometry return object") {
	rtsl::CompilerInvocation Invocation;
	Invocation.setModuleName("emit");
	Invocation.setInputName("emit.rtsl");
	Invocation.setInputBuffer(R"(
struct Vertex {}
@stage : geometry
fn main(triangle<Vertex> input) -> triangle_strip<Vertex, 2> {
	emit input[0];
	emit input[1];
	emit end;
}
)");
	rtsl::CompilerInstance Compiler;
	Compiler.setInvocation(std::move(Invocation));
	REQUIRE(Compiler.execute());
	rtsl::CodeGenerator Generator("emit");
	auto Result = Generator.generate(*Compiler.getASTContext());
	REQUIRE(Result.succeeded());

	const auto Geometry = std::ranges::find_if(Result.Module.entry_points,
		[](const rtsl::ir::EntryPoint& Entry) { return Entry.stage == rtsl::ir::Stage::stage_geometry; });
	REQUIRE(Geometry != Result.Module.entry_points.end());
	const auto* Function = Result.Module.findFunction(Geometry->function);
	REQUIRE(Function != nullptr);
	REQUIRE(std::ranges::count(Function->blocks[0].instructions, rtsl::ir::Opcode::opcode_call,
		&rtsl::ir::Instruction::opcode) == 3);
}

TEST_CASE("a constructor initializes its direct Position base from a vec3") {
	rtsl::CompilerInvocation Invocation;
	Invocation.setModuleName("constructor-base");
	Invocation.setInputName("constructor-base.rtsl");
	Invocation.setInputBuffer(R"(
struct Point {
	vec3 position;
	vec4 color;
};
struct Vertex : Position {
	vec4 color;
	fn Vertex(Point point);
};
fn Vertex::Vertex(Point point) : Position(point.position) {
	color = point.color;
}
@stage : vertex
fn main(Point point) -> Vertex {
	return Vertex(point);
}
)");
	rtsl::CompilerInstance Compiler;
	Compiler.setInvocation(std::move(Invocation));
	REQUIRE(Compiler.execute());
	rtsl::CodeGenerator Generator("constructor-base");
	auto Result = Generator.generate(*Compiler.getASTContext());
	REQUIRE(Result.succeeded());
}

TEST_CASE("var locals keep typed integer literals and lower control flow to structured RTIR") {
	rtsl::CompilerInvocation Invocation;
	Invocation.setModuleName("expressions");
	Invocation.setInputName("expressions.rtsl");
	Invocation.setInputBuffer(R"(
@stage : fragment
fn main() -> f32 {
	var i32 count = 5;
	var f32 value = -1.0 + 3.0 * 2.0;
	if (value < 0.0 || value >= 8.0) {
		value = value % 2.0;
	}
	return value;
}
)");
	rtsl::CompilerInstance Compiler;
	Compiler.setInvocation(std::move(Invocation));
	REQUIRE(Compiler.execute());
	rtsl::CodeGenerator Generator("expressions");
	auto Result = Generator.generate(*Compiler.getASTContext());
	REQUIRE(Result.succeeded());
	const auto& Function = stageFunction(Result.Module, rtsl::ir::Stage::stage_fragment);
	REQUIRE(Function.blocks.size() == 3);
	REQUIRE(Function.blocks.front().merge.kind == rtsl::ir::MergeKind::merge_selection);
	REQUIRE(std::ranges::any_of(Function.blocks.front().instructions,
		[](const rtsl::ir::Instruction& Instruction) { return Instruction.opcode == rtsl::ir::Opcode::opcode_logical_or; }));
	REQUIRE(std::ranges::any_of(Function.blocks[1].instructions,
		[](const rtsl::ir::Instruction& Instruction) { return Instruction.opcode == rtsl::ir::Opcode::opcode_remainder; }));
	const auto Integer = std::ranges::find_if(Function.blocks.front().instructions,
		[](const rtsl::ir::Instruction& Instruction) { return Instruction.opcode == rtsl::ir::Opcode::opcode_constant_integer; });
	REQUIRE(Integer != Function.blocks.front().instructions.end());
	REQUIRE(Result.Module.findType(Integer->type)->kind == rtsl::ir::TypeKind::type_signed_integer);
	REQUIRE_FALSE(std::ranges::any_of(Function.blocks.front().instructions,
		[](const rtsl::ir::Instruction& Instruction) { return Instruction.opcode == rtsl::ir::Opcode::opcode_convert; }));
}

TEST_CASE("integer literals receive their type from local declarations, assignments, calls, and returns") {
	rtsl::CompilerInvocation Invocation;
	Invocation.setModuleName("integer-context");
	Invocation.setInputName("integer-context.rtsl");
	Invocation.setInputBuffer(R"(
fn increment(i32 value) -> i32 {
	return value + 1;
}

@stage : fragment
fn main() -> i32 {
	var i32 value;
	value = 5;
	return increment(value);
}
)");
	rtsl::CompilerInstance Compiler;
	Compiler.setInvocation(std::move(Invocation));
	REQUIRE(Compiler.execute());
	rtsl::CodeGenerator Generator("integer-context");
	auto Result = Generator.generate(*Compiler.getASTContext());
	REQUIRE(Result.succeeded());
	for (const auto& Function : Result.Module.functions)
		for (const auto& Block : Function.blocks)
			for (const auto& Instruction : Block.instructions)
				if (Instruction.opcode == rtsl::ir::Opcode::opcode_constant_integer)
					REQUIRE(Result.Module.findType(Instruction.type)->kind == rtsl::ir::TypeKind::type_signed_integer);
}

TEST_CASE("function and nested-block locals do not escape their lexical scope") {
	rtsl::CompilerInvocation Invocation;
	Invocation.setInputName("local-scope.rtsl");
	Invocation.setInputBuffer(R"(
fn first() -> i32 {
	var i32 value = 1;
	{
		var i32 nested = value;
	}
	return value;
}

fn second() -> i32 {
	return nested;
}
)");
	rtsl::CompilerInstance Compiler;
	Compiler.setInvocation(std::move(Invocation));
	REQUIRE_FALSE(Compiler.execute());
}

TEST_CASE("texture resources and sample lower to a resource sample operation") {
	rtsl::CompilerInvocation Invocation;
	Invocation.setModuleName("sample");
	Invocation.setInputName("sample.rtsl");
	Invocation.setInputBuffer(R"(
var texture_2d<f32> plasma;
@stage : fragment
fn main(vec2 uv) -> vec4 {
	return sample(plasma, uv);
}
)");
	rtsl::CompilerInstance Compiler;
	Compiler.setInvocation(std::move(Invocation));
	REQUIRE(Compiler.execute());
	rtsl::CodeGenerator Generator("sample");
	auto Result = Generator.generate(*Compiler.getASTContext());
	REQUIRE(Result.succeeded());
	REQUIRE(Result.Module.resources.size() == 1);
	REQUIRE(Result.Module.resources.front().kind == rtsl::ir::ResourceKind::resource_sampled_texture);
	const auto& Function = stageFunction(Result.Module, rtsl::ir::Stage::stage_fragment);
	REQUIRE(std::ranges::any_of(Function.blocks.front().instructions,
		[](const rtsl::ir::Instruction& Instruction) { return Instruction.opcode == rtsl::ir::Opcode::opcode_resource_sample; }));
}

TEST_CASE("an i32 local accepts an integer literal in a fragment entry") {
	rtsl::CompilerInvocation Invocation;
	Invocation.setModuleName("fragment-local");
	Invocation.setInputName("fragment-local.rtsl");
	Invocation.setInputBuffer(R"(
struct Vertex {
	vec4 color;
}

@stage : fragment
fn main(Vertex vertex) -> vec4 {
	var i32 a = 5;
	return vertex.color;
}
)");
	rtsl::CompilerInstance Compiler;
	Compiler.setInvocation(std::move(Invocation));
	REQUIRE(Compiler.execute());
	rtsl::CodeGenerator Generator("fragment-local");
	auto Result = Generator.generate(*Compiler.getASTContext());
	REQUIRE(Result.succeeded());
	const auto& Function = stageFunction(Result.Module, rtsl::ir::Stage::stage_fragment);
	const auto Constant = std::ranges::find_if(Function.blocks.front().instructions,
		[](const rtsl::ir::Instruction& Instruction) { return Instruction.opcode == rtsl::ir::Opcode::opcode_constant_integer; });
	REQUIRE(Constant != Function.blocks.front().instructions.end());
	REQUIRE(Result.Module.findType(Constant->type)->kind == rtsl::ir::TypeKind::type_signed_integer);
	REQUIRE_FALSE(std::ranges::any_of(Function.blocks.front().instructions,
		[](const rtsl::ir::Instruction& Instruction) { return Instruction.opcode == rtsl::ir::Opcode::opcode_convert; }));
}
