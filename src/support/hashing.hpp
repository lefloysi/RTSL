#pragma once

// Transparent hashing/comparison for string-keyed hash containers.
//
// A plain `unordered_map<std::string, V>` copies every `std::string_view`
// lookup key into a `std::string` just to compute the hash. `TransparentString`
// carries the `is_transparent` tag pair the standard requires so lookups
// against `string_view` (and `const char*`) hit the container without any
// allocation. Every string-keyed map/set in the compiler uses this.

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace rtsl {

struct TransparentStringHash {
	using is_transparent = void;

	[[nodiscard]] std::size_t operator()(std::string_view value) const noexcept {
		return std::hash<std::string_view>{}(value);
	}
	[[nodiscard]] std::size_t operator()(const std::string& value) const noexcept {
		return std::hash<std::string_view>{}(value);
	}
	[[nodiscard]] std::size_t operator()(const char* value) const noexcept {
		return std::hash<std::string_view>{}(value);
	}
};

struct TransparentStringEqual {
	using is_transparent = void;
	[[nodiscard]] bool operator()(std::string_view a, std::string_view b) const noexcept { return a == b; }
};

template <typename Value>
using StringMap = std::unordered_map<std::string, Value, TransparentStringHash, TransparentStringEqual>;

using StringSet = std::unordered_set<std::string, TransparentStringHash, TransparentStringEqual>;

} // namespace rtsl
