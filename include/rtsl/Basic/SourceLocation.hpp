#ifndef RTSL_BASIC_SOURCE_LOCATION_HPP
#define RTSL_BASIC_SOURCE_LOCATION_HPP

#include <cstdint>

namespace rtsl {

class SourceLocation {
public:
	constexpr SourceLocation() = default;
	[[nodiscard]] static constexpr SourceLocation getFromRawEncoding(std::uint32_t Value) {
		SourceLocation Result;
		Result.Value = Value;
		return Result;
	}
	[[nodiscard]] constexpr std::uint32_t getRawEncoding() const { return Value; }
	[[nodiscard]] constexpr bool isValid() const { return Value != 0; }

private:
	std::uint32_t Value{};
};

struct SourceRange {
	SourceLocation Begin;
	SourceLocation End;
};

using FileID = std::uint32_t;

}

#endif
