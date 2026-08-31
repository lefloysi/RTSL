#include <rtsl/IR/Builder.hpp>
#include <rtsl/Serialization/Artifact.hpp>

#include <catch2/catch_test_macros.hpp>

namespace rtsl::test {

Artifact makeArtifact() {
	rtsl::ir::ModuleBuilder builder("serialization-test");
	rtsl::ir::Type void_type;
	void_type.kind = rtsl::ir::TypeKind::type_void;
	const rtsl::ir::TypeId void_id = builder.internType(void_type);
	const rtsl::ir::SymbolId symbol = builder.addSymbol("serialization-test::main", true);
	const rtsl::ir::FunctionId function = builder.addFunction(symbol, void_id, {}, {}, false, true);
	const rtsl::ir::BlockId block = builder.addBlock(function);
	rtsl::ir::Terminator terminator;
	terminator.kind = rtsl::ir::TerminatorKind::terminator_return;
	builder.setTerminator(function, block, std::move(terminator));
	builder.addEntryPoint(rtsl::ir::EntryPoint{
		.symbol = symbol,
		.function = function,
		.stage = rtsl::ir::Stage::stage_compute,
		.configuration = rtsl::ir::ComputeConfiguration{.workgroup_size = {8, 4, 1}},
		.attributes = {{.name = builder.module().strings.intern("invocations"), .tokens = {builder.module().strings.intern("4")}}},
	});
	return Artifact{
		.kind = ArtifactKind::artifact_program,
		.module = builder.takeModule(),
	};
}

std::uint64_t readU64(const std::vector<std::byte>& bytes, std::size_t offset) {
	std::uint64_t value{};
	for (unsigned shift = 0; shift != 64; shift += 8) value |= static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(bytes[offset++])) << shift;
	return value;
}

} // namespace rtsl::test

TEST_CASE("RTIR artifacts round trip deterministically") {
	const rtsl::WriteResult first = rtsl::ArtifactWriter{}.write(rtsl::test::makeArtifact());
	REQUIRE(first);

	rtsl::ReadResult read = rtsl::ArtifactReader{}.read(first.bytes);
	REQUIRE(read);
	REQUIRE(read.artifact->kind == rtsl::ArtifactKind::artifact_program);
	REQUIRE(read.artifact->module.strings.get(read.artifact->module.name) == "serialization-test");
	REQUIRE(read.artifact->module.functions[0].implicit_emitter);
	REQUIRE(read.artifact->module.symbols[0].exported);
	REQUIRE(read.artifact->module.strings.get(read.artifact->module.entry_points[0].attributes[0].name) == "invocations");
	REQUIRE(read.artifact->module.strings.get(read.artifact->module.entry_points[0].attributes[0].tokens[0]) == "4");

	const rtsl::WriteResult second = rtsl::ArtifactWriter{}.write(*read.artifact);
	REQUIRE(second);
	REQUIRE(second.bytes == first.bytes);
}

TEST_CASE("RTIR artifacts preserve an interned empty string") {
	rtsl::Artifact artifact = rtsl::test::makeArtifact();
	const rtsl::ir::StringId empty = artifact.module.strings.intern("");
	REQUIRE(empty);

	const rtsl::WriteResult encoded = rtsl::ArtifactWriter{}.write(artifact);
	REQUIRE(encoded);

	const rtsl::ReadResult decoded = rtsl::ArtifactReader{}.read(encoded.bytes);
	REQUIRE(decoded);
	REQUIRE(decoded.artifact->module.strings.get(empty).empty());
}

TEST_CASE("RTIR artifact reader rejects corrupt headers and section ranges") {
	const rtsl::WriteResult encoded = rtsl::ArtifactWriter{}.write(rtsl::test::makeArtifact());
	REQUIRE(encoded);

	std::vector<std::byte> bad_magic = encoded.bytes;
	bad_magic.front() = std::byte{0};
	const rtsl::ReadResult magic_result = rtsl::ArtifactReader{}.read(bad_magic);
	REQUIRE_FALSE(magic_result);
	REQUIRE(magic_result.error->code == rtsl::ErrorCode::error_invalid_magic);

	std::vector<std::byte> bad_range = encoded.bytes;
	for (std::size_t index = 32; index != 40; ++index) bad_range[index] = std::byte{0};
	const rtsl::ReadResult range_result = rtsl::ArtifactReader{}.read(bad_range);
	REQUIRE_FALSE(range_result);
	REQUIRE(range_result.error->code == rtsl::ErrorCode::error_invalid_directory);

	const rtsl::ReadResult truncated_result = rtsl::ArtifactReader{}.read(std::span{encoded.bytes}.first(encoded.bytes.size() - 1));
	REQUIRE_FALSE(truncated_result);

	std::vector<std::byte> bad_string_reference = encoded.bytes;
	const std::size_t module_section_offset = static_cast<std::size_t>(rtsl::test::readU64(bad_string_reference, 56));
	for (std::size_t index = module_section_offset; index != module_section_offset + 4; ++index) bad_string_reference[index] = std::byte{0xff};
	const rtsl::ReadResult reference_result = rtsl::ArtifactReader{}.read(bad_string_reference);
	REQUIRE_FALSE(reference_result);
	REQUIRE(reference_result.error->code == rtsl::ErrorCode::error_invalid_reference);
}
