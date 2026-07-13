#include "support/io.hpp"

#include <iterator>
#include <fstream>

namespace rtsl {

std::vector<u08> read_file(const std::filesystem::path& path) {
	std::ifstream input(path, std::ios::binary);
	if (!input) return {};
	return { std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
}

bool write_file(const std::filesystem::path& path, std::span<const u08> bytes) {
	std::ofstream output(path, std::ios::binary);
	if (!output) return false;
	output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
	return output.good();
}

} // namespace rtsl
