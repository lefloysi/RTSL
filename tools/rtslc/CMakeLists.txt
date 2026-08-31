find_package(CLI11 CONFIG REQUIRED)

add_executable(rtslc main.cpp)
target_link_libraries(rtslc PRIVATE RTSL::rtsl CLI11::CLI11)
