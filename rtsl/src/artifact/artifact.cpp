#include "artifact/artifact.hpp"

#include <artifact_codec.hpp>

#include <utility>

namespace rtsl {

std::vector<u08> write_artifact(ArtifactKind kind, const IRModule& module) {
	auto result = codec::encode_artifact(kind, module);
	if (!result) return {};
	return std::move(*result);
}

bool read_artifact(std::span<const u08> data, Artifact& artifact, DiagnosticEngine* diagnostics) {
	auto result = codec::decode_artifact(data);
	if (result) {
		artifact = std::move(*result);
		return true;
	}
	if (diagnostics) {
		std::string message = result.error().context;
		if (!message.empty()) message += ": ";
		message += result.error().message;
		diagnostics->report(DiagnosticCode::artifact_error, DiagnosticSeverity::error,
			{}, "<artifact>", std::move(message));
	}
	return false;
}

} // namespace rtsl
