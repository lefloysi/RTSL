#include "format_files.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <random>
#include <string>

#if defined(_WIN32)
#include <Windows.h>
#else
#include <cstdlib>
#include <unistd.h>
#endif

struct FormatFiles {
	std::filesystem::path directory;
	std::string command;
	std::string output;

	~FormatFiles() {
		std::error_code error;
		if (!directory.empty()) {
			std::filesystem::remove_all(directory, error);
		}
	}
};

std::string rtsl_format_path(const std::filesystem::path& path) {
	const auto bytes = path.u8string();
	return { reinterpret_cast<const char*>(bytes.data()), bytes.size() };
}

std::string rtsl_format_quote(const std::string& value) {
#if defined(_WIN32)
	// cmd expands these even inside double quotes. Refuse them rather than
	// allowing document paths or configured executable names to become commands.
	if (value.find_first_of("\"%!\r\n") != std::string::npos) {
		return {};
	}
	return "\"" + value + "\"";
#else
	std::string quoted = "'";
	for (const char character : value) {
		quoted += character == '\'' ? "'\\''" : std::string(1, character);
	}
	return quoted + "'";
#endif
}

std::filesystem::path rtsl_format_executable(const char* executable) {
#if defined(_WIN32)
	const auto candidate = std::filesystem::u8path(executable).wstring();
	std::wstring path(32768, L'\0');
	const auto size = SearchPathW(nullptr, candidate.c_str(), L".exe", static_cast<DWORD>(path.size()), path.data(), nullptr);
	if (!size || size >= path.size()) {
		return {};
	}
	path.resize(size);
	return path;
#else
	const std::filesystem::path candidate(executable);
	if (candidate.has_parent_path()) {
		return access(executable, X_OK) ? std::filesystem::path{} : candidate;
	}
	const char* environment = std::getenv("PATH");
	if (!environment) {
		return {};
	}
	std::string paths(environment);
	std::size_t begin = 0;
	while (true) {
		const auto end = paths.find(':', begin);
		const auto directory = paths.substr(begin, end == std::string::npos ? end : end - begin);
		const auto path = (directory.empty() ? std::filesystem::path(".") : std::filesystem::path(directory)) / candidate;
		if (!access(path.c_str(), X_OK)) {
			return std::filesystem::absolute(path);
		}
		if (end == std::string::npos) {
			return {};
		}
		begin = end + 1;
	}
#endif
}

FormatFiles* rtsl_format_files_create(const char* text, std::size_t size) {
	try {
		auto files = std::make_unique<FormatFiles>();
#if defined(_WIN32)
		std::random_device random;
		for (unsigned int attempt = 0; attempt < 64; ++attempt) {
			auto directory = std::filesystem::temp_directory_path() / ("rtsl-format-" + std::to_string(random()) + "-" + std::to_string(random()));
			if (std::filesystem::create_directory(directory)) {
				files->directory = std::move(directory);
				break;
			}
		}
#else
		std::string pattern = rtsl_format_path(std::filesystem::temp_directory_path() / "rtsl-format-XXXXXX");
		if (mkdtemp(pattern.data())) {
			files->directory = pattern;
		}
#endif
		if (files->directory.empty()) {
			return nullptr;
		}
		std::ofstream input(files->directory / "input", std::ios::binary);
		input.write(text, static_cast<std::streamsize>(size));
		input.close();
		if (!input) {
			return nullptr;
		}
		return files.release();
	}
	catch (...) {
		return nullptr;
	}
}

const char* rtsl_format_files_command(FormatFiles* storage, const char* executable, const char* assumed) {
	try {
		auto& files = *storage;
		const auto resolved = rtsl_format_executable(executable);
		if (resolved.empty()) {
			return nullptr;
		}
		const auto program = rtsl_format_quote(rtsl_format_path(resolved));
		const auto filename = rtsl_format_quote(std::string("-assume-filename=") + assumed);
		const auto input = rtsl_format_quote(rtsl_format_path(files.directory / "input"));
		const auto output = rtsl_format_quote(rtsl_format_path(files.directory / "output"));
		if (program.empty() || filename.empty() || input.empty() || output.empty()) {
			return nullptr;
		}
		files.command = program + " -style=file -fallback-style=LLVM " + filename + " < " + input + " > " + output;
#if defined(_WIN32)
		files.command = "\"" + files.command + " 2>NUL\"";
#else
		files.command += " 2>/dev/null";
#endif
		return files.command.c_str();
	}
	catch (...) {
		return nullptr;
	}
}

const char* rtsl_format_files_output(FormatFiles* storage, std::size_t* size) {
	try {
		auto& files = *storage;
		std::ifstream input(files.directory / "output", std::ios::binary);
		if (!input) {
			return nullptr;
		}
		files.output.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
		if (input.bad()) {
			return nullptr;
		}
		*size = files.output.size();
		return files.output.c_str();
	}
	catch (...) {
		return nullptr;
	}
}

void rtsl_format_files_destroy(FormatFiles* files) {
	delete files;
}
