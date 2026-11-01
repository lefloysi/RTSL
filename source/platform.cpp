#include <filesystem>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <Windows.h>
#include <io.h>
#include <fcntl.h>
#else
#include <unistd.h>
#endif

std::string rtsl_find_executable_directory() {
#if defined(_WIN32)
	std::vector<wchar_t> buffer(1024);
	while (true) {
		const auto size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
		if (!size) {
			return {};
		}
		if (size < buffer.size()) {
			const auto directory = std::filesystem::path(std::wstring(buffer.data(), size)).parent_path().u8string();
			return { reinterpret_cast<const char*>(directory.data()), directory.size() };
		}
		buffer.resize(buffer.size() * 2);
	}
#else
	std::vector<char> buffer(1024);
	while (true) {
		const auto size = readlink("/proc/self/exe", buffer.data(), buffer.size());
		if (size < 0) {
			return {};
		}
		if (static_cast<std::size_t>(size) < buffer.size()) {
			return std::filesystem::path(std::string(buffer.data(), size)).parent_path().string();
		}
		buffer.resize(buffer.size() * 2);
	}
#endif
}

extern const std::string rtsl_executable_directory_storage = rtsl_find_executable_directory();

extern "C" const char* rtsl_executable_directory() {
	return rtsl_executable_directory_storage.c_str();
}

extern "C" int rtsl_read(int file, void* bytes, unsigned int count) {
#if defined(_WIN32)
	return _read(file, bytes, count);
#else
	return static_cast<int>(read(file, bytes, count));
#endif
}

extern "C" int rtsl_write(int file, const void* bytes, unsigned int count) {
#if defined(_WIN32)
	return _write(file, bytes, count);
#else
	return static_cast<int>(write(file, bytes, count));
#endif
}

extern "C" void rtsl_binary_stdio() {
#if defined(_WIN32)
	_setmode(0, _O_BINARY);
	_setmode(1, _O_BINARY);
#endif
}
