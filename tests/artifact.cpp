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
	source.exports = source.module.exports;

	const Artifact interface_artifact = extract_module_interface(source);
	REQUIRE(interface_artifact.kind == ArtifactKind::module);
	REQUIRE_FALSE(interface_artifact.bytes.empty());

	Artifact loaded;
	REQUIRE(read_artifact(interface_artifact.bytes, loaded));
	REQUIRE(loaded.exports.size() == 1);
	REQUIRE(loaded.exports[0].name == "shared");
}

TEST_CASE("linker rejects module interface inputs") {
	Artifact module{ .kind = ArtifactKind::module };
	module.module.source_name = "iface.rtslm";
	module.module.exports = { ExportSymbol{ .name = "shared", .kind = "function", .type = "void" } };
	module.exports = module.module.exports;
	module.bytes = write_artifact(ArtifactKind::module, module.module);

	DiagnosticEngine diagnostics;
	Linker linker{ diagnostics };
	REQUIRE_FALSE(linker.add_artifact(module));
	REQUIRE(diagnostics.has_error());
}

TEST_CASE("artifact round trips function display names") {
	IRModule module{ .source_name = "names.rtsl" };
	module.functions.push_back(IRFunction{
		.result_id = 1,
		.exported = true,
		.source_name = "_Z6helperv",
		.display_name_text = "helper",
	});

	Artifact artifact;
	REQUIRE(read_artifact(write_artifact(ArtifactKind::object, module), artifact));
	REQUIRE(artifact.module.functions.size() == 1);
	REQUIRE(artifact.module.functions[0].display_name.value < artifact.strings.size());
	REQUIRE(artifact.strings[artifact.module.functions[0].display_name.value] == "helper");
	REQUIRE(artifact.module.functions[0].display_name_text == "helper");
	REQUIRE(artifact.module.functions[0].mangled_name.value < artifact.strings.size());
	REQUIRE(artifact.strings[artifact.module.functions[0].mangled_name.value] == "_Z6helperv");
	REQUIRE(artifact.module.functions[0].source_name == "_Z6helperv");
}

TEST_CASE("mangler emits stable rtsl names") {
	const std::array<std::string_view, 1> parameter_types{ "Point" };
	REQUIRE(mangle_rtsl("Vertex::Vertex", {}, parameter_types) == "_ZN6Vertex6VertexE5Point");
}
