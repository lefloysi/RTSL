#include "artifact.hpp"
#include "mangler.hpp"

#include <catch2/catch_test_macros.hpp>

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

TEST_CASE("artifact round trips imports") {
	IRModule module{ .source_name = "test.rtsl" };
	module.imports = { "shared/math", "core/types" };
	module.imported_exports = { ExportSymbol{ .name = "dot", .kind = "function", .type = "float" } };
	module.exports = { ExportSymbol{ .name = "main", .kind = "function", .type = "void" } };
	Artifact artifact;
	REQUIRE(read_artifact(write_artifact(ArtifactKind::module, module), artifact));
	REQUIRE(artifact.module.imports.size() == 2);
	REQUIRE(artifact.module.imports[0] == "shared/math");
	REQUIRE(artifact.module.imports[1] == "core/types");
	REQUIRE(artifact.module.exports.size() == 1);
	REQUIRE(artifact.module.exports[0].name == "main");
	REQUIRE(artifact.module.imported_exports.size() == 1);
	REQUIRE(artifact.module.imported_exports[0].name == "dot");
}

TEST_CASE("mangler emits stable rtsl names") {
	const MangleInput input{
		.name = "Vertex::Vertex",
		.stage = StageKind::none,
		.parameter_types = { "Point" },
	};
	const Mangler mangler;
	REQUIRE(mangler.mangle_rtsl(input) == "_ZN6Vertex6VertexE5Point");
}
