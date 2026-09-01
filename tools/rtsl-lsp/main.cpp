// Native RTSL LSP transport.  Parsing and semantic diagnostics are delegated to
// the RTSL frontend; this program deliberately contains no RTSL grammar.
#include <rtsl/Frontend/CompilerInstance.hpp>
#include <rtsl/Lex/Lexer.hpp>
#include <rtsl/Lex/Preprocessor.hpp>
#include <rtsl/Sema/Sema.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

namespace {
std::string escape(std::string_view text) { std::string r; for (char c : text) { if (c == '\\' || c == '"') r += '\\'; if (c == '\n') r += "\\n"; else if (c != '\r') r += c; } return r; }
std::string readFile(const std::string& path) { std::ifstream f(path, std::ios::binary); return {std::istreambuf_iterator<char>(f), {}}; }
std::string uriToPath(std::string u) { const std::string p = "file:///"; if (u.rfind(p, 0) == 0) u.erase(0, p.size()); for (char& c : u) if (c == '/') c = '\\'; return u; }
unsigned lineOf(std::string_view s, unsigned offset) { unsigned n = 0; for (unsigned i=0;i<offset && i<s.size();++i) if(s[i]=='\n') ++n; return n; }
unsigned columnOf(std::string_view s, unsigned offset) { unsigned i=offset; while(i && s[i-1]!='\n') --i; return offset-i; }
enum SemanticType : unsigned { keyword, variable, type, function, number, string, op, comment, decorator, name_space, punctuation, control_keyword };
struct LexedToken { rtsl::tok::TokenKind kind; unsigned offset; unsigned length; std::string_view spelling; };
struct SemanticSpan { unsigned offset; unsigned length; SemanticType type; };

bool isPunctuation(rtsl::tok::TokenKind kind) {
 using namespace rtsl::tok;
 switch (kind) {
 case l_square: case r_square: case l_paren: case r_paren: case l_brace: case r_brace:
 case period: case ellipsis: case comma: case colon: case coloncolon: case semi: case at: return true;
 default: return false;
 }
}

void addSpan(std::vector<SemanticSpan>& spans, unsigned offset, unsigned length, SemanticType type) {
 if (length != 0) spans.push_back({offset, length, type});
}

void collectComments(std::string_view text, std::vector<SemanticSpan>& spans) {
 // Keep comment boundaries identical to Lexer::skipWhitespaceAndComments.
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
   if (cursor + 1 < text.size()) cursor += 2;
   else cursor = static_cast<unsigned>(text.size());
  }
  addSpan(spans, start, cursor - start, comment);
 }
}

void appendEncodedSpan(std::ostringstream& out, bool& first, unsigned& previousLine, unsigned& previousColumn,
 std::string_view text, const SemanticSpan& span) {
 unsigned offset = span.offset;
 unsigned remaining = span.length;
 while (remaining != 0) {
  const unsigned line = lineOf(text, offset);
  const unsigned column = columnOf(text, offset);
  unsigned length = 0;
  while (length < remaining && offset + length < text.size() && text[offset + length] != '\n') ++length;
  if (length == 0) { ++offset; --remaining; continue; }
  if (!first) out << ','; first = false;
  out << (line - previousLine) << ',' << (line == previousLine ? column - previousColumn : column) << ','
      << length << ',' << static_cast<unsigned>(span.type) << ",0";
  previousLine = line; previousColumn = column;
  offset += length; remaining -= length;
 }
}

std::vector<LexedToken> lexSource(const std::string& name, const std::string& text) {
 rtsl::SourceManager sm; rtsl::DiagnosticsEngine d; rtsl::IdentifierTable ids; auto file=sm.createFileID(name,text); rtsl::Lexer lx(file,sm,ids,d); rtsl::Token t;
 std::vector<LexedToken> tokens;
 for (;;) { lx.lex(t); if (t.is(rtsl::tok::eof)) break; const unsigned offset = t.getLocation().getRawEncoding() - 1;
  tokens.push_back({t.getKind(), offset, t.getLength(), std::string_view(text).substr(offset, t.getLength())}); }
 return tokens;
}

std::vector<SemanticSpan> classifyTokens(const std::string& name, const std::string& text, const std::vector<LexedToken>& tokens) {
 rtsl::CompilerInvocation invocation; invocation.setInputName(name); invocation.setInputBuffer(text);
 rtsl::CompilerInstance compiler; compiler.setInvocation(std::move(invocation)); [[maybe_unused]] const bool parsed = compiler.execute();
 const auto* sema = compiler.getSema();
 auto* compilerIdentifiers = compiler.getPreprocessor() ? &compiler.getPreprocessor()->getIdentifierTable() : nullptr;
 std::unordered_set<std::string_view> knownTypes;
 std::vector<SemanticSpan> spans;
 collectComments(text, spans);
 unsigned attributeLine = static_cast<unsigned>(-1);
 for (unsigned index = 0; index < tokens.size(); ++index) {
  const auto& token = tokens[index];
  const auto previous = index == 0 ? rtsl::tok::unknown : tokens[index - 1].kind;
  SemanticType classification = variable;
  using namespace rtsl::tok;
  const unsigned tokenLine = lineOf(text, token.offset);
  if (token.kind == at) { attributeLine = tokenLine; classification = decorator; }
  else if (tokenLine == attributeLine) classification = decorator;
  else if (token.kind == kw_if || token.kind == kw_else || token.kind == kw_return || token.kind == kw_emit) classification = control_keyword;
  else if (token.kind >= kw_import && token.kind <= kw_typename) classification = keyword;
  else if (token.kind == numeric_literal) classification = number;
  else if (token.kind == string_literal) classification = string;
  else if (isPunctuation(token.kind)) classification = punctuation;
  else if (isPunctuator(token.kind)) classification = op;
  else if (previous == at) classification = decorator;
  else if ((previous == kw_import ||
   (previous == less && index > 1 && tokens[index - 2].kind == kw_import)) && token.kind == identifier) classification = name_space;
  else if (previous == coloncolon && token.kind == identifier) classification = function;
  else if (token.kind == identifier && sema && compilerIdentifiers &&
   sema->isTypeName(&compilerIdentifiers->get(token.spelling))) classification = type;
  else if (knownTypes.contains(token.spelling)) classification = type;
  else if (previous == kw_fn && token.kind == identifier) classification = function;
  else if (token.kind == identifier && index + 1 < tokens.size() && tokens[index + 1].kind == l_paren) classification = function;
  if ((previous == kw_struct || previous == kw_using || previous == kw_typename) && token.kind == identifier) {
   knownTypes.insert(token.spelling); classification = type;
  }
  addSpan(spans, token.offset, token.length, classification);
 }
 std::sort(spans.begin(), spans.end(), [](const SemanticSpan& left, const SemanticSpan& right) { return left.offset < right.offset; });
 return spans;
}

std::string lexTokens(const std::string& name, const std::string& text) {
 const auto tokens = lexSource(name, text);
 const auto spans = classifyTokens(name, text, tokens);
 unsigned pl = 0, pc = 0; std::ostringstream out; bool first = true;
 for (const auto& span : spans) {
  // The editor's synchronous lexical classifier owns stable lexical colors.
  // Do not let batched semantic tokens replace control-flow, comments, or
  // attributes after the user has typed them.
  if (span.type == control_keyword || span.type == comment || span.type == decorator || span.type == punctuation) continue;
  appendEncodedSpan(out, first, pl, pc, text, span);
 }
 return out.str();
}

const char* semanticName(SemanticType semanticType) {
 switch (semanticType) {
 case keyword: return "keyword"; case variable: return "identifier"; case type: return "type";
 case function: return "function"; case number: return "number"; case string: return "string";
 case op: return "operator"; case comment: return "comment"; case decorator: return "attribute";
 case name_space: return "import"; case punctuation: return "punctuation";
 }
 return "token";
}

unsigned offsetAt(std::string_view text, unsigned line, unsigned column) {
 unsigned offset = 0;
 while (line != 0 && offset < text.size()) { if (text[offset++] == '\n') --line; }
 return std::min<unsigned>(offset + column, static_cast<unsigned>(text.size()));
}

std::string typeName(rtsl::QualType qualified) {
 const auto* type = qualified.getTypePtr();
 if (!type) return "<invalid>";
 std::string result = qualified.isConstQualified() ? "const " : "";
 using namespace rtsl;
 switch (type->getTypeClass()) {
 case TypeClass::type_builtin:
  switch (static_cast<const BuiltinType*>(type)->getKind()) {
  case BuiltinTypeKind::builtin_void: return result + "void"; case BuiltinTypeKind::builtin_bool: return result + "bool";
  case BuiltinTypeKind::builtin_i32: return result + "i32"; case BuiltinTypeKind::builtin_u32: return result + "u32";
  case BuiltinTypeKind::builtin_usize: return result + "usize"; case BuiltinTypeKind::builtin_f32: return result + "f32";
  }
  break;
 case TypeClass::type_named: return result + std::string(static_cast<const NamedType*>(type)->getName()->getName());
 case TypeClass::type_template_parameter: return result + std::string(static_cast<const TemplateParameterType*>(type)->getName()->getName());
 case TypeClass::type_template_specialization: {
  const auto* specialization = static_cast<const TemplateSpecializationType*>(type);
  result += specialization->getName()->getName(); result += "<";
  for (unsigned index = 0; index < specialization->getArgumentCount(); ++index) { if (index != 0) result += ", "; result += typeName(specialization->arguments()[index]); }
  return result + ">";
 }
 case TypeClass::type_pointer: return result + typeName(static_cast<const PointerType*>(type)->getPointeeType()) + "*";
 case TypeClass::type_reference: return result + typeName(static_cast<const ReferenceType*>(type)->getPointeeType()) + "&";
 }
 return result + "<invalid>";
}

std::string functionInformation(const std::string& name, const std::string& text, std::string_view spelling) {
 rtsl::CompilerInvocation invocation; invocation.setInputName(name); invocation.setInputBuffer(text);
 rtsl::CompilerInstance compiler; compiler.setInvocation(std::move(invocation)); [[maybe_unused]] const bool parsed = compiler.execute();
 const auto* context = compiler.getASTContext();
 if (!context) return {};
 for (auto* declaration = context->getTranslationUnitDecl()->declsBegin(); declaration; declaration = declaration->getNextDeclInContext()) {
  if (declaration->getKind() != rtsl::DeclKind::decl_function) continue;
  const auto* function = static_cast<const rtsl::FunctionDecl*>(declaration);
  if (function->getIdentifier()->getName() != spelling) continue;
  std::string result = "fn " + std::string(spelling) + "(";
  for (unsigned index = 0; index < function->getNumParams(); ++index) {
   if (index != 0) result += ", "; const auto* parameter = function->parameters()[index];
   result += typeName(parameter->getType()) + " " + std::string(parameter->getIdentifier()->getName());
  }
  return result + ") -> " + typeName(function->getType());
 }
 return {};
}

std::string hover(const std::string& name, const std::string& text, unsigned line, unsigned column) {
 const auto tokens = lexSource(name, text);
 const auto spans = classifyTokens(name, text, tokens);
 const unsigned offset = offsetAt(text, line, column);
 for (const auto& span : spans) {
  if (offset < span.offset || offset >= span.offset + span.length) continue;
  const auto spelling = std::string_view(text).substr(span.offset, span.length);
  const auto information = span.type == function ? functionInformation(name, text, spelling) : std::string{};
  const auto value = information.empty() ? std::string(semanticName(span.type)) + ": " + std::string(spelling) : information;
  return "{\"contents\":{\"kind\":\"plaintext\",\"value\":\"" + escape(value) + "\"}}";
 }
 return "null";
}
std::string diagnostics(const std::string& name, const std::string& text) {
 rtsl::CompilerInvocation inv; inv.setInputName(name); inv.setInputBuffer(text); rtsl::CompilerInstance ci; ci.setInvocation(std::move(inv)); [[maybe_unused]] const bool parsed = ci.execute(); std::ostringstream out; bool first=true;
 for(const auto& x:ci.getDiagnostics().diagnostics()){ auto p=ci.getSourceManager().getPresumedLoc(x.Range.Begin); if(!first)out<<','; first=false; out<<"{\"range\":{\"start\":{\"line\":"<<(p.Line?p.Line-1:0)<<",\"character\":"<<(p.Column?p.Column-1:0)<<"},\"end\":{\"line\":"<<(p.Line?p.Line-1:0)<<",\"character\":"<<(p.Column?p.Column-1:0)<<"}},\"severity\":"<<(x.Level==rtsl::DiagnosticLevel::diagnostic_error?1:2)<<",\"source\":\"rtsl\",\"message\":\""<<escape(x.Message)<<"\"}"; }
 return out.str();
}
void send(const std::string& body){ std::cout<<"Content-Length: "<<body.size()<<"\n\n"<<body<<std::flush; }
std::string stringField(const std::string& json, const std::string& key) {
 auto cursor = json.find("\"" + key + "\""); if (cursor == std::string::npos) return {};
 cursor = json.find(':', cursor); if (cursor == std::string::npos) return {};
 while (++cursor < json.size() && std::isspace(static_cast<unsigned char>(json[cursor]))) {}
 if (cursor == json.size() || json[cursor] != '"') return {};
 std::string result;
 for (++cursor; cursor < json.size() && json[cursor] != '"'; ++cursor) {
  if (json[cursor] != '\\' || ++cursor == json.size()) { result += json[cursor]; continue; }
  switch (json[cursor]) { case 'n': result += '\n'; break; case 'r': result += '\r'; break; case 't': result += '\t'; break;
  case '"': result += '"'; break; case '\\': result += '\\'; break; default: result += json[cursor]; break; }
 }
 return result;
}
std::string rawField(const std::string& json, const std::string& key) {
 auto cursor = json.find("\"" + key + "\""); if (cursor == std::string::npos) return {};
 cursor = json.find(':', cursor); if (cursor == std::string::npos) return {};
 while (++cursor < json.size() && std::isspace(static_cast<unsigned char>(json[cursor]))) {}
 const auto end = json.find_first_of(",}", cursor);
 return json.substr(cursor, end == std::string::npos ? std::string::npos : end - cursor);
}
}
int main(int argc,char** argv) {
 if(argc==3 && std::string(argv[1])=="--check"){ auto s=readFile(argv[2]); std::cout<<diagnostics(argv[2],s)<<'\n'; return 0; }
 std::string openUri, openText, header, body;
 while(std::getline(std::cin,header)) { if(header.rfind("Content-Length:",0)!=0) continue; auto n=std::stoul(header.substr(15)); std::getline(std::cin,header); body.assign(n,'\0'); std::cin.read(body.data(),n); auto method=stringField(body,"method"), id=rawField(body,"id");
  if(method=="initialize") send("{\"jsonrpc\":\"2.0\",\"id\":"+id+",\"result\":{\"capabilities\":{\"textDocumentSync\":1,\"semanticTokensProvider\":{\"legend\":{\"tokenTypes\":[\"keyword\",\"variable\",\"type\",\"cppFunction\",\"number\",\"string\",\"operator\",\"comment\",\"cppMacro\",\"namespace\",\"rtsl.punctuation\",\"cppEnumerator\"],\"tokenModifiers\":[]},\"full\":true},\"completionProvider\":{\"triggerCharacters\":[\".\",\"<\",\":\"]},\"hoverProvider\":true,\"definitionProvider\":true,\"referencesProvider\":true,\"documentSymbolProvider\":true,\"signatureHelpProvider\":{\"triggerCharacters\":[\"(\",\",\"]}}}}" );
  else if(method=="textDocument/didOpen" || method=="textDocument/didChange") { openUri=stringField(body,"uri"); openText=stringField(body,"text"); auto path=uriToPath(openUri); send("{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/publishDiagnostics\",\"params\":{\"uri\":\""+escape(openUri)+"\",\"diagnostics\":["+diagnostics(path,openText)+"]}}"); }
  else if(method=="textDocument/semanticTokens/full") send("{\"jsonrpc\":\"2.0\",\"id\":"+id+",\"result\":{\"data\":["+lexTokens(uriToPath(openUri),openText)+"]}}");
  else if(method=="textDocument/hover") {
   const auto line = rawField(body, "line"), character = rawField(body, "character");
   const auto result = line.empty() || character.empty() ? "null" : hover(uriToPath(openUri), openText, std::stoul(line), std::stoul(character));
   send("{\"jsonrpc\":\"2.0\",\"id\":"+id+",\"result\":"+result+"}");
  }
  else if(!id.empty()) send("{\"jsonrpc\":\"2.0\",\"id\":"+id+",\"result\":null}");
 }
}
