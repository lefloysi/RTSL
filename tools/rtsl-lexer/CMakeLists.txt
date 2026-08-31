add_library(rtsl-lexer SHARED
 rtsl-lexer.cpp
 ../../src/Basic/Diagnostic.cpp
 ../../src/Basic/IdentifierTable.cpp
 ../../src/Basic/SourceManager.cpp
 ../../src/Basic/TokenKinds.cpp
 ../../src/Lex/Lexer.cpp
)
target_compile_features(rtsl-lexer PRIVATE cxx_std_20)
target_include_directories(rtsl-lexer PRIVATE ../../include)
