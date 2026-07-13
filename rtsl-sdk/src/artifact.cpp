#include "rtsl_sdk.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace {

template <typename T>
T read_little_endian(const std::uint8_t* bytes) {
	T value{};
	std::memcpy(&value, bytes, sizeof(T));
	if constexpr (std::endian::native == std::endian::big) {
		value = std::byteswap(value);
	}
	return value;
}

} // namespace

rtsl_sdk_result rtslSdkReadArtifactHeader(const std::uint8_t* bytes, std::size_t size, rtsl_sdk_artifact_view* out_view) {
	if (!bytes || !out_view) {
		return rtsl_sdk_result{ 0, "invalid argument" };
	}
	if (size < RTSL_SDK_ARTIFACT_HEADER_SIZE) {
		return rtsl_sdk_result{ 0, "artifact is smaller than the header" };
	}

	rtsl_sdk_artifact_header header{
		.magic = read_little_endian<std::uint32_t>(bytes),
		.version_major = read_little_endian<std::uint16_t>(bytes + 4),
		.version_minor = read_little_endian<std::uint16_t>(bytes + 6),
		.kind = read_little_endian<std::uint16_t>(bytes + 8),
		.endian = bytes[10],
		.header_size = read_little_endian<std::uint32_t>(bytes + 16),
		.payload_count = read_little_endian<std::uint32_t>(bytes + 20),
		.payload_record_offset = read_little_endian<std::uint64_t>(bytes + 24),
		.file_size = read_little_endian<std::uint64_t>(bytes + 32),
	};

	if (header.magic != RTSL_SDK_ARTIFACT_MAGIC) {
		return rtsl_sdk_result{ 0, "invalid RTSL artifact magic" };
	}
	if (header.version_major != RTSL_SDK_ARTIFACT_VERSION_MAJOR) {
		return rtsl_sdk_result{ 0, "unsupported RTSL artifact version" };
	}
	if (header.kind < RTSL_SDK_ARTIFACT_OBJECT || header.kind > RTSL_SDK_ARTIFACT_PROGRAM) {
		return rtsl_sdk_result{ 0, "invalid RTSL artifact kind" };
	}
	if (header.endian != 1 ||
		header.header_size != RTSL_SDK_ARTIFACT_HEADER_SIZE ||
		header.payload_record_offset != RTSL_SDK_ARTIFACT_HEADER_SIZE ||
		header.file_size != size) {
		return rtsl_sdk_result{ 0, "invalid RTSL artifact header" };
	}

	out_view->header = header;
	out_view->bytes = bytes;
	out_view->size = size;
	return rtsl_sdk_result{ 1, "ok" };
}
