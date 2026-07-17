#include <rtsl/sdk.hpp>
#include <rtsl/spirv.hpp>

int main() {
	const rtsl::ProgramBytes empty_program{};
	auto program = rtsl::load_program(empty_program);
	if (!program) {
		return 0;
	}

	return rtsl::spirv::transpile(*program, rtsl::Stage::vertex) ? 0 : 1;
}
