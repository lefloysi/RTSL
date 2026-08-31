#ifndef RTSL_BASIC_TOKEN_KINDS_HPP
#define RTSL_BASIC_TOKEN_KINDS_HPP

#include <string_view>

namespace rtsl::tok {

enum TokenKind : unsigned short {
#define TOKEN(Name) Name,
#include <rtsl/Basic/TokenKinds.def>
};

[[nodiscard]] std::string_view getTokenName(TokenKind Kind);
[[nodiscard]] std::string_view getPunctuatorSpelling(TokenKind Kind);
[[nodiscard]] bool isPunctuator(TokenKind Kind);

}

#endif
