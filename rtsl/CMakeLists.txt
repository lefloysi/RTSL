add_library("rtsl" STATIC
    "src/support/basic_diagnostics.cpp"
    "src/support/basic_source_manager.cpp"
    "src/support/io.cpp"
    "src/frontend/preprocessor.cpp"
    "src/frontend/lexer.cpp"
    "src/frontend/parser.cpp"
    "src/sema/sema.cpp"
    "src/sema/mangler.cpp"
    "src/sema/uniform_lowering.cpp"
    "src/ir/ir.cpp"
    "src/ir/text_rtir.cpp"
    "src/artifact/artifact.cpp"
    "src/artifact/linker.cpp"
    "src/driver/compiler.cpp"
    "src/api/language_service.cpp"
    "src/api/rtsl.cpp"
)

target_compile_features("rtsl" PUBLIC "cxx_std_23")

target_include_directories("rtsl"
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${PROJECT_SOURCE_DIR}/rtsl-sdk/src"
)

target_link_libraries("rtsl" PRIVATE "rtsl-sdk")

set_target_properties("rtsl" PROPERTIES
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
)

install(
    TARGETS "rtsl"
    EXPORT "RTSLTargets"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)

install(
    DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)