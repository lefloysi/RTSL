// Native RTSL LSP transport.  Parsing and semantic diagnostics are delegated to
// the RTSL frontend; this program deliberately contains no RTSL grammar.
#include <rtsl/Frontend/CompilerInstance.hpp>
#include <rtsl/Lex/Lexer.hpp>
#include <rtsl/Lex/Preprocessor.hpp>

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {
std::string escape(std::string_view text) { std::string r; for (char c : text) { if (c == '\\' || c == '"') r += '\\'; if (c == '\n') r += "\\n"; else if (c != '\r') r += c; } return r; }
std::string readFile(const std::string& path) { std::ifstream f(path, std::ios::binary); return {std::istreambuf_iterator<char>(f), {}}; }
std::string uriToPath(std::string u) { const std::string p = "file:///"; if (u.rfind(p, 0) == 0) u.erase(0, p.size()); for (char& c : u) if (c == '/') c = '\\'; return u; }
unsigned lineOf(std::string_view s, unsigned offset) { unsigned n = 0; for (unsigned i=0;i<offset && i<s.size();++i) if(s[i]=='\n') ++n; return n; }
unsigned columnOf(std::string_view s, unsigned offset) { unsigned i=offset; while(i && s[i-1]!='\n') --i; return offset-i; }
int semanticType(rtsl::tok::TokenKind k) { using namespace rtsl::tok; if(k>=kw_import && k<=kw_typename) return 0; if(k==numeric_literal) return 3; if(k==string_literal) return 4; if(isPunctuator(k)) return 5; return 1; }
std::string lexTokens(const std::string& name, const std::string& text) {
 rtsl::SourceManager sm; rtsl::DiagnosticsEngine d; rtsl::IdentifierTable ids; auto file=sm.createFileID(name,text); rtsl::Lexer lx(file,sm,ids,d); rtsl::Token t; unsigned pl=0,pc=0; std::ostringstream out; bool first=true;
 for(;;){ lx.lex(t); if(t.is(rtsl::tok::eof)) break; auto loc=t.getLocation().getRawEncoding()-1; unsigned l=lineOf(text,loc), c=columnOf(text,loc); if(!first) out<<','; first=false; out<<(l-pl)<<','<<(l==pl?c-pc:c)<<','<<t.getLength()<<','<<semanticType(t.getKind())<<",0"; pl=l;pc=c; }
 return out.str();
}
std::string diagnostics(const std::string& name, const std::string& text) {
 rtsl::CompilerInvocation inv; inv.setInputName(name); inv.setInputBuffer(text); rtsl::CompilerInstance ci; ci.setInvocation(std::move(inv)); ci.execute(); std::ostringstream out; bool first=true;
 for(const auto& x:ci.getDiagnostics().diagnostics()){ auto p=ci.getSourceManager().getPresumedLoc(x.Range.Begin); if(!first)out<<','; first=false; out<<"{\"range\":{\"start\":{\"line\":"<<(p.Line?p.Line-1:0)<<",\"character\":"<<(p.Column?p.Column-1:0)<<"},\"end\":{\"line\":"<<(p.Line?p.Line-1:0)<<",\"character\":"<<(p.Column?p.Column-1:0)<<"}},\"severity\":"<<(x.Level==rtsl::DiagnosticLevel::diagnostic_error?1:2)<<",\"source\":\"rtsl\",\"message\":\""<<escape(x.Message)<<"\"}"; }
 return out.str();
}
void send(const std::string& body){ std::cout<<"Content-Length: "<<body.size()<<"\r\n\r\n"<<body<<std::flush; }
std::string field(const std::string& j,const std::string& key){ auto p=j.find("\""+key+"\""); if(p==std::string::npos)return{}; p=j.find(':',p); p=j.find('"',p); if(p==std::string::npos)return{}; auto e=j.find('"',p+1); return e==std::string::npos?std::string{}:j.substr(p+1,e-p-1); }
}
int main(int argc,char** argv) {
 if(argc==3 && std::string(argv[1])=="--check"){ auto s=readFile(argv[2]); std::cout<<diagnostics(argv[2],s)<<'\n'; return 0; }
 std::string openUri, openText, header, body;
 while(std::getline(std::cin,header)) { if(header.rfind("Content-Length:",0)!=0) continue; auto n=std::stoul(header.substr(15)); std::getline(std::cin,header); body.assign(n,'\0'); std::cin.read(body.data(),n); auto method=field(body,"method"), id=field(body,"id");
  if(method=="initialize") send("{\"jsonrpc\":\"2.0\",\"id\":"+id+",\"result\":{\"capabilities\":{\"textDocumentSync\":1,\"semanticTokensProvider\":{\"legend\":{\"tokenTypes\":[\"keyword\",\"variable\",\"type\",\"number\",\"string\",\"operator\"],\"tokenModifiers\":[]},\"full\":true},\"completionProvider\":{\"triggerCharacters\":[\".\",\"<\",\":\"]},\"hoverProvider\":true,\"definitionProvider\":true,\"referencesProvider\":true,\"documentSymbolProvider\":true,\"signatureHelpProvider\":{\"triggerCharacters\":[\"(\",\",\"]}}}}" );
  else if(method=="textDocument/didOpen" || method=="textDocument/didChange") { openUri=field(body,"uri"); openText=field(body,"text"); auto path=uriToPath(openUri); send("{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/publishDiagnostics\",\"params\":{\"uri\":\""+escape(openUri)+"\",\"diagnostics\":["+diagnostics(path,openText)+"]}}"); }
  else if(method=="textDocument/semanticTokens/full") send("{\"jsonrpc\":\"2.0\",\"id\":"+id+",\"result\":{\"data\":["+lexTokens(uriToPath(openUri),openText)+"]}}");
  else if(!id.empty()) send("{\"jsonrpc\":\"2.0\",\"id\":"+id+",\"result\":null}");
 }
}
