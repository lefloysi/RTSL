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
	source.exports = source.module.exports;

	const Artifact interface_artifact = extract_module_interface(source);
	REQUIRE(interface_artifact.kind == ArtifactKind::module);
	REQUIRE_FALSE(interface_artifact.bytes.empty());

	Artifact loaded;
	REQUIRE(read_artifact(interface_artifact.bytes, loaded));
	REQUIRE(loaded.exports.size() == 1);
	REQUIRE(loaded.exports[0].name == "shared");
}

TEST_CASE("artifact round trips stage interface type names") {
	IRModule module{ .source_name = "stage.rtsl" };
	module.stage_interfaces.push_back(StageInterface{
		.role = StageRole::output,
		.type_name = "Fragment",
		.fields = {
			StageIOField{ .name = "color", .location = 0 },
		},
	});

	Artifact artifact;
	REQUIRE(read_artifact(write_artifact(ArtifactKind::program, module), artifact));
	REQUIRE(artifact.stage_interfaces.size() == 1);
	REQUIRE(artifact.stage_interfaces[0].type_name == "Fragment");
	REQUIRE(artifact.stage_interfaces[0].fields.size() == 1);
	REQUIRE(artifact.stage_interfaces[0].fields[0].name == "color");
}

TEST_CASE("mangler emits stable rtsl names") {
	const std::array<std::string_view, 1> parameter_types{ "Point" };
	REQUIRE(mangle_rtsl("Vertex::Vertex", StageKind::none, parameter_types) == "_ZN6Vertex6VertexE5Point");
}
