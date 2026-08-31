#include <rtsl/Basic/SourceManager.hpp>

#include <stdexcept>

namespace rtsl {

FileID SourceManager::createFileID(std::string_view Name, std::string_view Buffer) {
	Files.push_back({std::string(Name), std::string(Buffer), NextBase});
	NextBase += static_cast<std::uint32_t>(Buffer.size()) + 1;
	return static_cast<FileID>(Files.size());
}

std::string_view SourceManager::getBuffer(FileID File) const {
	if (File == 0 || File > Files.size()) throw std::out_of_range("invalid RTSL FileID");
	return Files[File - 1].Buffer;
}

std::string_view SourceManager::getName(FileID File) const {
	if (File == 0 || File > Files.size()) throw std::out_of_range("invalid RTSL FileID");
	return Files[File - 1].Name;
}

SourceLocation SourceManager::getLocation(FileID File, std::uint32_t Offset) const {
	if (File == 0 || File > Files.size()) return {};
	return SourceLocation::getFromRawEncoding(Files[File - 1].Base + Offset);
}

PresumedLoc SourceManager::getPresumedLoc(SourceLocation Location) const {
	if (!Location.isValid()) return {};
	const std::uint32_t RawLocation = Location.getRawEncoding();
	for (const FileInfo& File : Files) {
		const std::uint32_t End = File.Base + static_cast<std::uint32_t>(File.Buffer.size());
		if (RawLocation < File.Base || RawLocation > End) continue;

		const std::uint32_t Offset = RawLocation - File.Base;
		std::uint32_t Line = 1;
		std::uint32_t Column = 1;
		for (std::uint32_t Index = 0; Index < Offset; ++Index) {
			if (File.Buffer[Index] == '\n') {
				++Line;
				Column = 1;
			} else {
				++Column;
			}
		}
		return {File.Name, Line, Column};
	}
	return {};
}

}
