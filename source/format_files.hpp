#pragma once

#include <cstddef>

struct FormatFiles;

extern "C" FormatFiles* rtsl_format_files_create(const char* text, std::size_t size);
extern "C" const char* rtsl_format_files_command(FormatFiles* files, const char* executable, const char* assumed);
extern "C" const char* rtsl_format_files_output(FormatFiles* files, std::size_t* size);
extern "C" void rtsl_format_files_destroy(FormatFiles* files);
