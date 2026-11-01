#include <rtsl/Serialization/Artifact.hpp>

#include <algorithm>
#include <cstdint>
#include <span>

extern "C" int rtsl_test_artifact_parity(const std::uint8_t* data, std::uint64_t size) {
	const auto bytes = std::as_bytes(std::span{data, static_cast<std::size_t>(size)});
	const auto decoded = rtsl::ArtifactReader{}.read(bytes);
	if (!decoded) return 1;
	const auto& artifact = *decoded.artifact;
	const auto& module = artifact.module;
	if (artifact.kind != rtsl::ArtifactKind::artifact_program || module.name.value() != 1 ||
		module.types.size() != 4 || module.symbols.size() != 5 || module.functions.size() != 1 ||
		module.entry_points.size() != 5 || module.resources.size() != 1 ||
		module.uniforms.size() != 1 || module.storage_objects.size() != 1) return 2;
	const auto& function = module.functions.front();
	if (!function.implicit || !function.implicit_emitter || function.parameters.size() != 1 ||
		!function.parameters.front().builtin || function.blocks.size() != 2 ||
		function.blocks.front().instructions.size() != 2 || !function.blocks.front().terminator ||
		function.blocks.front().terminator->successors.front().arguments.size() != 1 ||
		function.blocks.back().arguments.size() != 1 || module.types[2].members.size() != 1 ||
		module.types[2].builtin_members.size() != 1 || module.types[3].parameter_types.size() != 1 ||
		!module.resources.front().binding || !module.uniforms.front().offset ||
		!module.uniforms.front().size || !module.uniforms.front().alignment ||
		!module.storage_objects.front().initializer) return 3;
	const auto encoded = rtsl::ArtifactWriter{}.write(artifact);
	if (!encoded) return 4;
	if (!std::ranges::equal(bytes, encoded.bytes)) return 5;
	return 0;
}
