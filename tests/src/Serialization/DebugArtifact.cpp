#include <rtsl/Serialization/DebugArtifact.hpp>

#include <catch2/catch_test_macros.hpp>

namespace {

rtsl::debug::Artifact makeArtifact() {
	using namespace rtsl::debug;
	Artifact artifact;
	artifact.compilation_identity = "rtsl test compilation";
	artifact.compiler_identity = "rtslc test";
	artifact.target_identity = "rtir";
	artifact.source_files.push_back({
		.logical_name = "shaders/test.rtsl",
		.text = "vertex main() {}\n",
		.line_starts = {0, 17},
	});
	const SourceRange declaration{.file = 0, .begin = 0, .end = 16};
	artifact.source_entities.push_back({.kind = SourceEntityKind::declaration, .id = 1, .range = declaration});
	artifact.types.push_back({.id = 1, .name = "void", .spelling = "void", .kind = 0, .declaration = declaration});
	artifact.symbols.push_back({.id = 1, .name = "main", .type = 1, .declaration = declaration});
	artifact.functions.push_back({.id = 1, .symbol = 1, .return_type = 1, .declaration = declaration});
	artifact.ir_instructions.push_back({.function = 1, .block = 0, .instruction = 0, .opcode = 0, .result_symbol = 0, .source = declaration});
	artifact.entry_points.push_back({.function = 1, .name = "main", .stage = 0, .declaration = declaration});
	return artifact;
}

} // namespace

TEST_CASE("RTSL debug artifacts round trip deterministically") {
	const rtsl::debug::Artifact input = makeArtifact();
	const rtsl::debug::WriteResult first = rtsl::debug::ArtifactWriter{}.write(input);
	REQUIRE(first);

	const rtsl::debug::ReadResult decoded = rtsl::debug::ArtifactReader{}.read(first.bytes);
	REQUIRE(decoded);
	REQUIRE(decoded.artifact->source_files.size() == 1);
	REQUIRE(decoded.artifact->source_files[0].logical_name == "shaders/test.rtsl");
	REQUIRE(decoded.artifact->ir_instructions[0].instruction == 0);

	const rtsl::debug::WriteResult second = rtsl::debug::ArtifactWriter{}.write(*decoded.artifact);
	REQUIRE(second);
	REQUIRE(second.bytes == first.bytes);
}

TEST_CASE("RTSL debug artifact reader rejects malformed input") {
	const rtsl::debug::WriteResult encoded = rtsl::debug::ArtifactWriter{}.write(makeArtifact());
	REQUIRE(encoded);

	auto bad_magic = encoded.bytes;
	bad_magic[0] = std::byte{0};
	const auto bad_magic_result = rtsl::debug::ArtifactReader{}.read(bad_magic);
	REQUIRE_FALSE(bad_magic_result);
	REQUIRE(bad_magic_result.error->code == rtsl::debug::ErrorCode::invalid_magic);

	const auto truncated_result = rtsl::debug::ArtifactReader{}.read(
		std::span{encoded.bytes}.first(encoded.bytes.size() - 1));
	REQUIRE_FALSE(truncated_result);

	rtsl::debug::Artifact invalid = makeArtifact();
	invalid.symbols[0].declaration.file = 7;
	const auto invalid_result = rtsl::debug::ArtifactWriter{}.write(invalid);
	REQUIRE_FALSE(invalid_result);
	REQUIRE(invalid_result.error->code == rtsl::debug::ErrorCode::invalid_range);
}
