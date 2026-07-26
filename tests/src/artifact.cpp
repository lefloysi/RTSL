#include "artifact/artifact.hpp"
#include "artifact/linker.hpp"
#include "sema/mangler.hpp"

#include <catch2/catch_test_macros.hpp>
#include <array>

using namespace rtsl;

TEST_CASE("artifact round trips") {
	IRModule module{ .source_name = "test.rtsl" };
	auto bytes = write_artifact(ArtifactKind::program, module);
	Artifact artifact;
	DiagnosticEngine diagnostics;
	REQUIRE(read_artifact(bytes, artifact, &diagnostics));
	REQUIRE_FALSE(diagnostics.has_error());
	REQUIRE(artifact.kind == ArtifactKind::program);
	REQUIRE_FALSE(artifact.bytes.empty());
}

TEST_CASE("artifact stream starts with the artifact identity") {
	IRModule module{ .source_name = "layout.rtsl" };
	const auto bytes = write_artifact(ArtifactKind::object, module);

	Artifact artifact;
	REQUIRE(read_artifact(bytes, artifact));
	REQUIRE(artifact.kind == ArtifactKind::object);
	REQUIRE(artifact.module.source_name == "layout.rtsl");
}

TEST_CASE("artifact reader rejects malformed input") {
	Artifact artifact;
	DiagnosticEngine diagnostics;
	const std::array<u08, 4> bytes{ 'R', 'T', 'S', 'L' };
	REQUIRE_FALSE(read_artifact(bytes, artifact, &diagnostics));
	REQUIRE(diagnostics.has_error());
}

TEST_CASE("artifact round trips imports") {
	IRModule module{ .source_name = "test.rtsl" };
	module.imports = { "shared/math", "core/types" };
	module.imported_exports = { ExportSymbol{ .name = "dot", .kind = "function", .type = "float" } };
	module.exports = { ExportSymbol{ .name = "helper", .kind = "function", .type = "void" } };
	Artifact artifact;
	REQUIRE(read_artifact(write_artifact(ArtifactKind::module, module), artifact));
	REQUIRE(artifact.module.imports.size() == 2);
	REQUIRE(artifact.module.imports[0] == "shared/math");
	REQUIRE(artifact.module.imports[1] == "core/types");
	REQUIRE(artifact.module.exports.size() == 1);
	REQUIRE(artifact.module.exports[0].name == "helper");
	REQUIRE(artifact.module.imported_exports.size() == 1);
	REQUIRE(artifact.module.imported_exports[0].name == "dot");
}

TEST_CASE("module interface emits re-export-only modules") {
	Artifact source{ .kind = ArtifactKind::object };
	source.module.source_name = "forward.rtsl";
	source.module.exports = { ExportSymbol{ .name = "shared", .kind = "function", .type = "void" } };

	const Artifact interface_artifact = extract_module_interface(source);
	REQUIRE(interface_artifact.kind == ArtifactKind::module);
	REQUIRE_FALSE(interface_artifact.bytes.empty());

	Artifact loaded;
	REQUIRE(read_artifact(interface_artifact.bytes, loaded));
	REQUIRE(loaded.module.exports.size() == 1);
	REQUIRE(loaded.module.exports[0].name == "shared");
}

TEST_CASE("linker rejects module interface inputs") {
	Artifact module{ .kind = ArtifactKind::module };
	module.module.source_name = "iface.rtslm";
	module.module.exports = { ExportSymbol{ .name = "shared", .kind = "function", .type = "void" } };
	module.bytes = write_artifact(ArtifactKind::module, module.module);

	DiagnosticEngine diagnostics;
	Linker linker{ diagnostics };
	REQUIRE_FALSE(linker.add_artifact(module));
	REQUIRE(diagnostics.has_error());
}

TEST_CASE("artifact round trips function display names") {
	IRModule module{ .source_name = "names.rtsl" };
	module.functions.push_back(IRFunction{
		.result_id = ID<IRInstruction>{ 1 },
		.kind = IRFunction::Kind::exported,
		.link_name = "_Z6helperv",
		.display_name = "helper",
	});

	Artifact artifact;
	REQUIRE(read_artifact(write_artifact(ArtifactKind::object, module), artifact));
	REQUIRE(artifact.module.functions.size() == 1);
	REQUIRE(artifact.module.functions[0].display_name == "helper");
	REQUIRE(artifact.module.functions[0].link_name == "_Z6helperv");
}

TEST_CASE("artifact round trips struct call signatures") {
	IRModule module{ .source_name = "reflection.rtsl" };
	module.structs.push_back(StructDecl{
		.name = "Material",
		.fields = { StructField{ .type = "vec4", .name = "albedo" } },
		.member_functions = {
			StructMemberFunction{
				.name = "tint",
				.parameters = { ParameterDecl{ .type = "vec4", .name = "color", .is_const = true, .is_reference = true } },
				.return_type = "vec4",
			},
		},
		.constructor_parameters = { ParameterDecl{ .type = "vec4", .name = "albedo" } },
	});
	Artifact artifact;
	REQUIRE(read_artifact(write_artifact(ArtifactKind::object, module), artifact));
	REQUIRE(artifact.module.structs.size() == 1);
	REQUIRE(artifact.module.structs[0].member_functions.size() == 1);
	REQUIRE(artifact.module.structs[0].member_functions[0].parameters[0].is_const);
	REQUIRE(artifact.module.structs[0].member_functions[0].parameters[0].is_reference);
	REQUIRE(artifact.module.structs[0].constructor_parameters.size() == 1);
}

TEST_CASE("artifact round trips normalized backend reflection") {
	IRModule module{ .source_name = "reflection.rtsl" };
	module.resources.push_back(Resource{
		.name = "material.texture",
		.kind = ResourceKind::sampled_texture,
		.image = { .dimension = ImageDimension::two },
		.access = Access::read_only,
		.descriptor = { .set = 2, .binding = 3 },
		.variable = ir::Id{ 7 },
		.value_type = ir::Id{ 4 },
	});
	module.entries.push_back(EntryPoint{
		.name = "shade",
		.stage = Stage::fragment,
		.function = ir::Id{ 9 },
		.input = Interface{
			.value_type = ir::Id{ 5 },
			.elements = {
				InterfaceElement{
					.name = "uv",
					.type = ir::Id{ 2 },
					.member = 0,
					.location = 0,
					.interpolation = Interpolation::smooth,
				},
			},
		},
	});

	Artifact artifact;
	REQUIRE(read_artifact(write_artifact(ArtifactKind::object, module), artifact));
	REQUIRE(artifact.module.resources.size() == 1);
	REQUIRE(artifact.module.resources[0].descriptor.set == 2);
	REQUIRE(artifact.module.resources[0].image.dimension == ImageDimension::two);
	REQUIRE(artifact.module.entries.size() == 1);
	REQUIRE(artifact.module.entries[0].name == "shade");
	REQUIRE(artifact.module.entries[0].input->elements[0].name == "uv");
}

TEST_CASE("mangler emits stable rtsl names") {
	const std::array<std::string_view, 1> parameter_types{ "Point" };
	REQUIRE(mangle_rtsl("Vertex::Vertex", {}, parameter_types) == "_ZN6Vertex6VertexE5Point");
}
