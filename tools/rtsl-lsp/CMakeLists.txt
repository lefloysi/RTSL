add_executable(rtsl-lsp main.cpp)
target_link_libraries(rtsl-lsp PRIVATE RTSL::rtsl)
target_compile_features(rtsl-lsp PRIVATE cxx_std_20)
