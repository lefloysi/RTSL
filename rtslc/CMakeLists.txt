find_package(CLI11 CONFIG QUIET)

if(NOT CLI11_FOUND)
    message(STATUS "CLI11 not found; rtslc will not be built")
    return()
endif()

add_executable("rtslc"
    "src/rtslc.cpp"
)

target_compile_features("rtslc" PRIVATE "cxx_std_23")

target_include_directories("rtslc" PRIVATE
    "${PROJECT_SOURCE_DIR}/rtsl/src"
    "${PROJECT_SOURCE_DIR}/rtsl-sdk/src"
    "${PROJECT_SOURCE_DIR}/rtsl-sdk/include"
)

target_link_libraries("rtslc" PRIVATE
    "rtsl"
    "CLI11::CLI11"
)

set_target_properties("rtslc" PROPERTIES
    CXX_EXTENSIONS OFF
)

install(
    TARGETS "rtslc"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)