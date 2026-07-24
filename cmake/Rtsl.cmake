function(rtsl_set_compiler compiler)
    set(RTSL_COMPILER "${compiler}" CACHE STRING "RTSL compiler executable or CMake target")
endfunction()

function(rtsl_get_compiler out_var)
    if(RTSL_COMPILER)
        set(_rtsl_compiler "${RTSL_COMPILER}")
    elseif(TARGET rtslc)
        set(_rtsl_compiler rtslc)
    else()
        find_program(_rtsl_compiler rtslc)
    endif()

    if(NOT _rtsl_compiler)
        message(FATAL_ERROR "Could not find rtslc. Build the rtslc target or set RTSL_COMPILER.")
    endif()

    if(TARGET "${_rtsl_compiler}")
        set(${out_var} "$<TARGET_FILE:${_rtsl_compiler}>" PARENT_SCOPE)
        set(${out_var}_TARGET "${_rtsl_compiler}" PARENT_SCOPE)
    else()
        set(${out_var} "${_rtsl_compiler}" PARENT_SCOPE)
        set(${out_var}_TARGET "" PARENT_SCOPE)
    endif()
endfunction()

function(rtsl_add_program program_name)
    set(oneValueArgs OUTPUT_DIR OUTPUT SYMBOL)
    set(multiValueArgs SOURCES DEPENDS INCLUDE_DIRS)
    cmake_parse_arguments(RTSL "" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT RTSL_SOURCES)
        message(FATAL_ERROR "rtsl_add_program(${program_name}) requires SOURCES")
    endif()
    if(NOT RTSL_OUTPUT_DIR)
        set(RTSL_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/rtsl/${program_name}")
    endif()
    if(NOT RTSL_OUTPUT)
        set(RTSL_OUTPUT "${RTSL_OUTPUT_DIR}/${program_name}.rtslp")
    endif()
    if(NOT RTSL_SYMBOL)
        set(RTSL_SYMBOL "${program_name}_rtslp")
    endif()

    rtsl_get_compiler(_rtsl_compiler)

    set(_rtsl_include_args)
    foreach(dir IN LISTS RTSL_INCLUDE_DIRS)
        list(APPEND _rtsl_include_args -I "${dir}")
    endforeach()

    file(MAKE_DIRECTORY "${RTSL_OUTPUT_DIR}")

    foreach(source IN LISTS RTSL_SOURCES)
        get_filename_component(source_name "${source}" NAME_WE)
        set(_rtsl_module_${source_name} "${RTSL_OUTPUT_DIR}/${source_name}.rtslm")
    endforeach()

    set(_rtsl_objects)
    foreach(source IN LISTS RTSL_SOURCES)
        get_filename_component(source_name "${source}" NAME_WE)
        set(object_path "${RTSL_OUTPUT_DIR}/${source_name}.rtslo")
        set(module_path "${RTSL_OUTPUT_DIR}/${source_name}.rtslm")

        set(import_deps)
        set(module_byproducts)
        if(EXISTS "${source}")
            file(STRINGS "${source}" _rtsl_import_lines REGEX "^[ \t]*(export[ \t]+)?import[ \t]")
            foreach(line IN LISTS _rtsl_import_lines)
                if(line MATCHES "(export[ \t]+)?import[ \t]+\"([^\"]+)\"")
                    get_filename_component(_imported_base "${CMAKE_MATCH_2}" NAME_WE)
                    if(DEFINED _rtsl_module_${_imported_base})
                        list(APPEND import_deps "${_rtsl_module_${_imported_base}}")
                    endif()
                endif()
            endforeach()

            file(STRINGS "${source}" _rtsl_export_lines REGEX "^[ \t]*export[ \t]")
            if(_rtsl_export_lines)
                list(APPEND module_byproducts "${module_path}")
            endif()
        endif()

        add_custom_command(
            OUTPUT "${object_path}"
            BYPRODUCTS ${module_byproducts}
            COMMAND "${_rtsl_compiler}" compile "${source}" -o "${object_path}"
                -I "${RTSL_OUTPUT_DIR}"
                ${_rtsl_include_args}
            DEPENDS "${source}" ${RTSL_DEPENDS} ${import_deps}
            VERBATIM
            COMMENT "RTSL compile ${source_name}"
        )
        list(APPEND _rtsl_objects "${object_path}")
    endforeach()

    add_custom_command(
        OUTPUT "${RTSL_OUTPUT}"
        COMMAND "${_rtsl_compiler}" link-program ${_rtsl_objects} -o "${RTSL_OUTPUT}"
        DEPENDS ${_rtsl_objects} ${RTSL_DEPENDS}
        VERBATIM
        COMMENT "RTSL link ${program_name}"
    )

    add_custom_target("${program_name}" DEPENDS "${RTSL_OUTPUT}")
    if(_rtsl_compiler_TARGET)
        add_dependencies("${program_name}" "${_rtsl_compiler_TARGET}")
    endif()
    set_target_properties("${program_name}" PROPERTIES
        RTSL_PROGRAM_OUTPUT "${RTSL_OUTPUT}"
        RTSL_PROGRAM_SYMBOL "${RTSL_SYMBOL}"
    )
endfunction()

function(rtsl_embed_program target_name)
    set(oneValueArgs OUTPUT)
    set(multiValueArgs PROGRAMS)
    cmake_parse_arguments(RTSL "" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT TARGET "${target_name}")
        message(FATAL_ERROR "rtsl_embed_program target '${target_name}' does not exist")
    endif()
    if(NOT RTSL_PROGRAMS)
        message(FATAL_ERROR "rtsl_embed_program(${target_name}) requires PROGRAMS")
    endif()
    if(NOT RTSL_OUTPUT)
        set(RTSL_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/rtsl/${target_name}_rtsl_embed.cpp")
    endif()
    get_filename_component(_rtsl_embed_output_dir "${RTSL_OUTPUT}" DIRECTORY)
    file(MAKE_DIRECTORY "${_rtsl_embed_output_dir}")

    set(_rtsl_inputs)
    set(_rtsl_symbols)
    foreach(program IN LISTS RTSL_PROGRAMS)
        if(NOT TARGET "${program}")
            message(FATAL_ERROR "rtsl_embed_program(${target_name}) unknown RTSL program '${program}'")
        endif()
        get_target_property(program_output "${program}" RTSL_PROGRAM_OUTPUT)
        get_target_property(program_symbol "${program}" RTSL_PROGRAM_SYMBOL)
        if(NOT program_output)
            message(FATAL_ERROR "'${program}' is not an RTSL program target")
        endif()
        list(APPEND _rtsl_inputs "${program_output}")
        list(APPEND _rtsl_symbols "${program_symbol}")
        add_dependencies("${target_name}" "${program}")
    endforeach()

    string(JOIN ";" _rtsl_input_list ${_rtsl_inputs})
    string(JOIN ";" _rtsl_symbol_list ${_rtsl_symbols})
    string(REPLACE ";" "\\;" _rtsl_input_arg "${_rtsl_input_list}")
    string(REPLACE ";" "\\;" _rtsl_symbol_arg "${_rtsl_symbol_list}")
    add_custom_command(
        OUTPUT "${RTSL_OUTPUT}"
        COMMAND "${CMAKE_COMMAND}"
            "-DRTSL_EMBED_INPUTS=${_rtsl_input_arg}"
            "-DRTSL_EMBED_SYMBOLS=${_rtsl_symbol_arg}"
            "-DRTSL_EMBED_OUTPUT=${RTSL_OUTPUT}"
            -P "${CMAKE_CURRENT_FUNCTION_LIST_FILE}"
        DEPENDS ${_rtsl_inputs}
        VERBATIM
        COMMENT "RTSL embed -> ${target_name}"
    )
    set_source_files_properties("${RTSL_OUTPUT}" PROPERTIES
        GENERATED TRUE
        CXX_SCAN_FOR_MODULES OFF
    )

    target_link_libraries("${target_name}" PRIVATE RTSL::sdk)
    set_target_properties("${target_name}" PROPERTIES CXX_SCAN_FOR_MODULES OFF)
    target_sources("${target_name}" PRIVATE "${RTSL_OUTPUT}")
endfunction()

if(DEFINED RTSL_EMBED_INPUTS AND DEFINED RTSL_EMBED_OUTPUT)
    string(REPLACE "\\;" ";" RTSL_EMBED_INPUTS "${RTSL_EMBED_INPUTS}")
    string(REPLACE "\\;" ";" RTSL_EMBED_SYMBOLS "${RTSL_EMBED_SYMBOLS}")

    file(WRITE "${RTSL_EMBED_OUTPUT}" "#include <rtsl/sdk/program.hpp>\n\n#include <cstdint>\n\n")

    set(_rtsl_index 0)
    foreach(input_path IN LISTS RTSL_EMBED_INPUTS)
        list(GET RTSL_EMBED_SYMBOLS "${_rtsl_index}" symbol_name)
        if(NOT symbol_name)
            get_filename_component(input_name "${input_path}" NAME_WE)
            set(symbol_name "${input_name}_rtslp")
        endif()
        math(EXPR _rtsl_index "${_rtsl_index} + 1")

        file(READ "${input_path}" _hex HEX)
        file(APPEND "${RTSL_EMBED_OUTPUT}" "namespace {\n")
        file(APPEND "${RTSL_EMBED_OUTPUT}" "alignas(16) const std::uint8_t ${symbol_name}_data[] = {\n")

        string(LENGTH "${_hex}" _hex_length)
        math(EXPR _byte_count "${_hex_length} / 2")
        if(_byte_count GREATER 0)
            math(EXPR _last_index "${_byte_count} - 1")
            foreach(index RANGE 0 "${_last_index}")
                math(EXPR _offset "${index} * 2")
                math(EXPR _next_index "${index} + 1")
                math(EXPR _column "${_next_index} % 12")
                string(SUBSTRING "${_hex}" "${_offset}" 2 _byte)
                if(_next_index EQUAL _byte_count)
                    file(APPEND "${RTSL_EMBED_OUTPUT}" "    0x${_byte}\n")
                elseif(_column EQUAL 0)
                    file(APPEND "${RTSL_EMBED_OUTPUT}" "    0x${_byte},\n")
                else()
                    file(APPEND "${RTSL_EMBED_OUTPUT}" "    0x${_byte}, ")
                endif()
            endforeach()
        endif()

        file(APPEND "${RTSL_EMBED_OUTPUT}" "};\n")
        file(APPEND "${RTSL_EMBED_OUTPUT}" "}\n\n")
        file(APPEND "${RTSL_EMBED_OUTPUT}" "extern \"C\" const rtsl::ProgramBytes ${symbol_name} = {\n")
        file(APPEND "${RTSL_EMBED_OUTPUT}" "    ${symbol_name}_data,\n")
        file(APPEND "${RTSL_EMBED_OUTPUT}" "    sizeof(${symbol_name}_data)\n")
        file(APPEND "${RTSL_EMBED_OUTPUT}" "};\n\n")
    endforeach()
endif()
