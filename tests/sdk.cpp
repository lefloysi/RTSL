#include "rtsl/sdk.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>

TEST_CASE("SDK reports malformed program bytes without compiler state") {
	const std::array bytes{
		std::byte{ 'R' },
		std::byte{ 'T' },
		std::byte{ 'S' },
		std::byte{ 'L' },
	};
	const auto program = rtsl::load_program(bytes);
	REQUIRE_FALSE(program.has_value());
	REQUIRE(program.error().code == rtsl::LoadErrorCode::malformed_artifact);
	REQUIRE_FALSE(program.error().message.empty());
}

TEST_CASE("SDK ProgramBytes is a non-owning load view") {
	const std::array<std::uint8_t, 4> bytes{ 'R', 'T', 'S', 'L' };
	const rtsl::ProgramBytes view{ .data = bytes.data(), .size = bytes.size() };
	REQUIRE(view.view().size() == bytes.size());
	REQUIRE_FALSE(rtsl::load_program(view).has_value());
}
