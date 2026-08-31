#ifndef RTSL_BASIC_SOURCE_MANAGER_HPP
#define RTSL_BASIC_SOURCE_MANAGER_HPP

#include <rtsl/Basic/SourceLocation.hpp>

#include <deque>
#include <string>
#include <string_view>

namespace rtsl {

struct PresumedLoc {
	std::string_view Filename;
	std::uint32_t Line{};
	std::uint32_t Column{};

	[[nodiscard]] constexpr bool isValid() const { return !Filename.empty() && Line != 0 && Column != 0; }
};

class SourceManager {
public:
	FileID createFileID(std::string_view Name, std::string_view Buffer);
	[[nodiscard]] std::string_view getBuffer(FileID File) const;
	[[nodiscard]] std::string_view getName(FileID File) const;
	[[nodiscard]] SourceLocation getLocation(FileID File, std::uint32_t Offset) const;
	[[nodiscard]] PresumedLoc getPresumedLoc(SourceLocation Location) const;

private:
	struct FileInfo {
		std::string Name;
		std::string Buffer;
		std::uint32_t Base{};
	};
	std::deque<FileInfo> Files;
	std::uint32_t NextBase{1};
};

}

#endif
