#pragma once

// The single file-I/O and byte/text conversion surface used across the
// compiler. Everywhere else uses `std::filesystem::path` for filesystem
// arguments, `std::vector<u08>` / `std::span<const u08>` for byte buffers,
// and `std::string` / `std::string_view` for text — the conversions between
// those live here so nobody else reinterpret_casts or wraps strings in path
// constructors ad hoc.

#include "support/basic_types.hpp"

#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rtsl {

// Read the whole file as bytes. Empty on any failure (missing / unreadable /
// truly empty file) — callers that need to distinguish should stat first.
[[nodiscard]] std::vector<u08> read_file(const std::filesystem::path& path);

// Write bytes wholesale, truncating any existing file. Returns false on any
// I/O error.
[[nodiscard]] bool write_file(const std::filesystem::path& path, std::span<const u08> bytes);

// View bytes as text without copying. Bytes are always a valid byte sequence;
// interpreting them as characters is a no-op cast the file format guarantees.
[[nodiscard]] inline std::string_view as_text(std::span<const u08> bytes) {
	return { reinterpret_cast<const char*>(bytes.data()), bytes.size() };
}

// Copy the file's path with `extension` swapped in.
[[nodiscard]] inline std::filesystem::path with_extension(std::filesystem::path path, std::string_view extension) {
	path.replace_extension(extension);
	return path;
}

} // namespace rtsl
