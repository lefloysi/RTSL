#include <rtsl/Basic/TokenKinds.hpp>

namespace rtsl::tok {

std::string_view getTokenName(TokenKind Kind) {
	switch (Kind) {
#define TOKEN(Name) case Name: return #Name;
#include <rtsl/Basic/TokenKinds.def>
	}
	return "unknown";
}

std::string_view getPunctuatorSpelling(TokenKind Kind) {
	switch (Kind) {
#define PUNCTUATOR(Name, Spelling) case Name: return Spelling;
#include <rtsl/Basic/TokenKinds.def>
	default: return {};
	}
}

bool isPunctuator(TokenKind Kind) {
	switch (Kind) {
#define PUNCTUATOR(Name, Spelling) case Name: return true;
#include <rtsl/Basic/TokenKinds.def>
	default: return false;
	}
}

}
