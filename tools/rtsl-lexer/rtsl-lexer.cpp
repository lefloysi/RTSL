#include <rtsl/Lex/Lexer.hpp>

#include <string>
#include <vector>

extern "C" {
struct RtslLexicalSpan { unsigned offset; unsigned length; unsigned category; };

enum RtslLexicalCategory : unsigned {
 keyword = 1,
 control_keyword,
 number,
 string,
 op,
 comment,
 attribute,
 punctuation,
};

static void collectComments(std::string_view text, std::vector<RtslLexicalSpan>& result) {
 // The compiler lexer intentionally treats comments as whitespace.  Keep these
 // ranges byte-for-byte identical to Lexer::skipWhitespaceAndComments.
 for (unsigned cursor = 0; cursor + 1 < text.size();) {
  if (text[cursor] == '"') {
   ++cursor;
   while (cursor < text.size() && text[cursor] != '"') {
    if (text[cursor] == '\\' && cursor + 1 < text.size()) cursor += 2;
    else ++cursor;
   }
   if (cursor < text.size()) ++cursor;
   continue;
  }
  if (text[cursor] != '/' || (text[cursor + 1] != '/' && text[cursor + 1] != '*')) { ++cursor; continue; }
  const unsigned start = cursor;
  if (text[cursor + 1] == '/') {
   cursor += 2;
   while (cursor < text.size() && text[cursor] != '\n') ++cursor;
  } else {
   cursor += 2;
   while (cursor + 1 < text.size() && !(text[cursor] == '*' && text[cursor + 1] == '/')) ++cursor;
   cursor = cursor + 1 < text.size() ? cursor + 2 : static_cast<unsigned>(text.size());
  }
  result.push_back({start, cursor - start, comment});
 }
}

static bool startsDeclaration(rtsl::tok::TokenKind kind) {
 using namespace rtsl::tok;
 switch (kind) {
 case kw_import: case kw_struct: case kw_using: case kw_var: case kw_const: case kw_static:
 case kw_export: case kw_uniform: case kw_storage: case kw_fn: case kw_template: return true;
 default: return false;
 }
}

__declspec(dllexport) unsigned rtsl_lexical_spans(const char* text, unsigned textLength, RtslLexicalSpan* spans, unsigned capacity) {
 if (!text || !spans) return 0;
 const std::string source(text, textLength);
 std::vector<RtslLexicalSpan> result;
 collectComments(source, result);
 rtsl::SourceManager sources; rtsl::DiagnosticsEngine diagnostics; rtsl::IdentifierTable identifiers;
 const auto file = sources.createFileID("<editor>", source); rtsl::Lexer lexer(file, sources, identifiers, diagnostics); rtsl::Token token;
 bool inAttribute = false;
 for (;;) {
  lexer.lex(token); if (token.is(rtsl::tok::eof)) break;
  const auto kind = token.getKind();
  const unsigned offset = token.getLocation().getRawEncoding() - 1;
  unsigned category = 0;
  if (kind == rtsl::tok::at) { inAttribute = true; category = attribute; }
  else if (inAttribute && !startsDeclaration(kind)) category = attribute;
  else {
   inAttribute = false;
   if (kind == rtsl::tok::kw_if || kind == rtsl::tok::kw_else || kind == rtsl::tok::kw_return || kind == rtsl::tok::kw_emit) category = control_keyword;
   else if (kind >= rtsl::tok::kw_import && kind <= rtsl::tok::kw_typename) category = keyword;
   else if (kind == rtsl::tok::numeric_literal) category = number;
   else if (kind == rtsl::tok::string_literal) category = string;
   else if (rtsl::tok::isPunctuator(kind)) {
    switch (kind) {
    case rtsl::tok::l_square: case rtsl::tok::r_square: case rtsl::tok::l_paren: case rtsl::tok::r_paren:
    case rtsl::tok::l_brace: case rtsl::tok::r_brace: case rtsl::tok::period: case rtsl::tok::ellipsis:
    case rtsl::tok::comma: case rtsl::tok::colon: case rtsl::tok::coloncolon: case rtsl::tok::semi: category = punctuation; break;
    default: category = op; break;
    }
   }
  }
  result.push_back({offset, token.getLength(), category});
 }
 if (result.size() > capacity) return 0;
 for (unsigned index = 0; index < result.size(); ++index) spans[index] = result[index];
 return static_cast<unsigned>(result.size());
}
}
