#ifndef RTSL_IR_VERIFIER_HPP
#define RTSL_IR_VERIFIER_HPP

#include <rtsl/IR/IR.hpp>

#include <string>
#include <vector>

namespace rtsl::ir {

enum class VerificationCode : std::uint16_t {
	verification_duplicate_id,
	verification_unknown_type,
	verification_unknown_symbol,
	verification_unknown_function,
	verification_unknown_block,
	verification_unknown_value,
	verification_type_mismatch,
	verification_invalid_instruction,
	verification_missing_terminator,
	verification_invalid_terminator,
	verification_invalid_successor_arguments,
	verification_invalid_structured_merge,
	verification_invalid_stage_configuration,
	verification_invalid_metadata,
};

struct VerificationIssue {
	VerificationCode code{VerificationCode::verification_invalid_metadata};
	std::string context;
	std::string message;
};

class VerificationResult {
public:
	[[nodiscard]] bool valid() const noexcept { return Issues.empty(); }
	[[nodiscard]] const std::vector<VerificationIssue>& issues() const noexcept { return Issues; }
	void add(VerificationCode code, std::string context, std::string message);

private:
	std::vector<VerificationIssue> Issues;
};

[[nodiscard]] VerificationResult verify(const Module& module);

} // namespace rtsl::ir

#endif
