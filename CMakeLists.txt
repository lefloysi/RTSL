cmake_minimum_required(VERSION 3.28)
project(RTSL VERSION 0.0.1 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

add_subdirectory("rtsl-sdk")
add_subdirectory("rtsl")
add_subdirectory("rtslc")

set(RTSL_PACKAGE_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/RTSL")

install(
    EXPORT "RTSLTargets"
    FILE "RTSLTargets.cmake"
    NAMESPACE "RTSL::"
    DESTINATION "${RTSL_PACKAGE_DIR}"
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RTSLConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/RTSLConfig.cmake"
    INSTALL_DESTINATION "${RTSL_PACKAGE_DIR}"
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/RTSLConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY "SameMajorVersion"
)

install(
    FILES
        "${CMAKE_CURRENT_BINARY_DIR}/RTSLConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/RTSLConfigVersion.cmake"
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Rtsl.cmake"
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RtslEmbed.cmake"
    DESTINATION "${RTSL_PACKAGE_DIR}"
)
