function(rtsl_add_program target_name)
    set(options EMBED)
    set(oneValueArgs RTSLC OUTPUT_DIR NAMESPACE EMBED_NAME)
    set(multiValueArgs SOURCES DEPENDS INCLUDE_DIRS)
    cmake_parse_arguments(RTSL "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT RTSL_RTSLC)
        message(FATAL_ERROR "rtsl_add_program(${target_name}) requires RTSLC to be set")
    endif()
    if(NOT RTSL_SOURCES)
        message(FATAL_ERROR "rtsl_add_program(${target_name}) requires at least one source")
    endif()
    if(NOT RTSL_OUTPUT_DIR)
        set(RTSL_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/rtsl/${target_name}")
    endif()
    if(RTSL_NAMESPACE)
        message(FATAL_ERROR "rtsl_add_program(${target_name}) NAMESPACE is not supported for embedded symbols; use EMBED_NAME")
    endif()
    list(LENGTH RTSL_SOURCES _rtsl_source_count)
    if(RTSL_EMBED_NAME AND NOT _rtsl_source_count EQUAL 1)
        message(FATAL_ERROR "rtsl_add_program(${target_name}) EMBED_NAME requires exactly one source")
    endif()

    set(_rtsl_include_args)
    foreach(dir IN LISTS RTSL_INCLUDE_DIRS)
        list(APPEND _rtsl_include_args -I "${dir}")
    endforeach()

    set(_rtsl_compiler "${RTSL_RTSLC}")
    set(_rtsl_compiler_dep)
    if(TARGET "${RTSL_RTSLC}")
        set(_rtsl_compiler "$<TARGET_FILE:${RTSL_RTSLC}>")
        list(APPEND _rtsl_compiler_dep "${RTSL_RTSLC}")
    endif()

    file(MAKE_DIRECTORY "${RTSL_OUTPUT_DIR}")

    # First pass: build a map of source basename -> .rtslm output path so we
    # can resolve `import "name";` lines to concrete build-tree paths. Every
    # source contributes a candidate .rtslm sidecar even if it turns out to
    # have no exports (rtslc skips writing in that case -- CMake sees a
    # missing file, but nothing will depend on it either).
    foreach(source IN LISTS RTSL_SOURCES)
        get_filename_component(source_name "${source}" NAME_WE)
        set(_rtsl_module_${source_name} "${RTSL_OUTPUT_DIR}/${source_name}.rtslm")
    endforeach()

    if(RTSL_EMBED)
        set(embed_cpp "${RTSL_OUTPUT_DIR}/${target_name}_rtsl_embed.cpp")
        set(embed_inputs)
        set(embed_byproducts)
        set(embed_commands)
        foreach(source IN LISTS RTSL_SOURCES)
            get_filename_component(source_name "${source}" NAME_WE)
            set(object_path "${RTSL_OUTPUT_DIR}/${source_name}.rtslo")
            set(module_path "${RTSL_OUTPUT_DIR}/${source_name}.rtslm")
            set(program_path "${RTSL_OUTPUT_DIR}/${source_name}.rtslp")
            list(APPEND embed_inputs "${program_path}")
            list(APPEND embed_byproducts "${object_path}" "${module_path}" "${program_path}")
            list(APPEND embed_commands
                COMMAND "${_rtsl_compiler}" compile "${source}" -o "${object_path}"
                    -I "${RTSL_OUTPUT_DIR}"
                    ${_rtsl_include_args}
                COMMAND "${_rtsl_compiler}" link-program "${object_path}" -o "${program_path}"
            )
        endforeach()
        string(JOIN ";" embed_input_list ${embed_inputs})
        add_custom_command(
            OUTPUT "${embed_cpp}"
            BYPRODUCTS ${embed_byproducts}
            ${embed_commands}
            COMMAND "${CMAKE_COMMAND}"
                "-DRTSL_EMBED_INPUTS=${embed_input_list}"
                "-DRTSL_EMBED_OUTPUT=${embed_cpp}"
                "-DRTSL_EMBED_NAME=${RTSL_EMBED_NAME}"
                -P "${CMAKE_CURRENT_FUNCTION_LIST_FILE}"
            DEPENDS ${RTSL_SOURCES} ${_rtsl_compiler_dep} ${RTSL_DEPENDS}
            VERBATIM
            COMMENT "RTSL compile and embed -> ${target_name}"
        )
        target_link_libraries("${target_name}" PRIVATE RTSL::sdk)
        target_sources("${target_name}" PRIVATE "${embed_cpp}")
        return()
    endif()

    set(outputs)
    foreach(source IN LISTS RTSL_SOURCES)
        get_filename_component(source_name "${source}" NAME_WE)
        set(object_path "${RTSL_OUTPUT_DIR}/${source_name}.rtslo")
        set(module_path "${RTSL_OUTPUT_DIR}/${source_name}.rtslm")
        set(program_path "${RTSL_OUTPUT_DIR}/${source_name}.rtslp")

        # Scan the source for import lines so we can wire per-file dependency
        # ordering. Matches `import "foo";`; extension
        # is optional. Only imports that name a sibling source in this call
        # become build-graph edges -- external imports fall through to the -I
        # search path at compile time and don't imply an ordering here.
        set(import_deps)
        if(EXISTS "${source}")
            file(STRINGS "${source}" _rtsl_import_lines REGEX "^[ \t]*(export[ \t]+)?import[ \t]")
            foreach(line IN LISTS _rtsl_import_lines)
                if(line MATCHES "(export[ \t]+)?import[ \t]+\"([^\"]+)\"")
                    set(_imported "${CMAKE_MATCH_2}")
                    get_filename_component(_imported_base "${_imported}" NAME_WE)
                    if(DEFINED _rtsl_module_${_imported_base})
                        list(APPEND import_deps "${_rtsl_module_${_imported_base}}")
                    endif()
                endif()
            endforeach()
        endif()

        # The .rtslm sidecar is written by `rtslc compile` only when the source
        # has exports -- not every source produces one. Listing it in BYPRODUCTS
        # (rather than OUTPUT) tells CMake "may or may not appear; don't fail
        # if missing" while still letting downstream commands depend on it for
        # ordering.
        add_custom_command(
            OUTPUT "${object_path}" "${program_path}"
            BYPRODUCTS "${module_path}"
            COMMAND "${_rtsl_compiler}" compile "${source}" -o "${object_path}"
                -I "${RTSL_OUTPUT_DIR}"
                ${_rtsl_include_args}
            COMMAND "${_rtsl_compiler}" link-program "${object_path}" -o "${program_path}"
            DEPENDS "${source}" ${_rtsl_compiler_dep} ${RTSL_DEPENDS} ${import_deps}
            VERBATIM
            COMMENT "RTSL ${source_name} -> ${target_name}"
        )

        list(APPEND outputs "${program_path}")
    endforeach()

    add_custom_target("${target_name}-rtsl" DEPENDS ${outputs})
    add_dependencies("${target_name}" "${target_name}-rtsl")

endfunction()

if(DEFINED RTSL_EMBED_INPUTS AND DEFINED RTSL_EMBED_OUTPUT)
    file(WRITE "${RTSL_EMBED_OUTPUT}" "#include <rtsl/sdk/program.hpp>\n\n#include <cstdint>\n\n")

    foreach(input_path IN LISTS RTSL_EMBED_INPUTS)
        get_filename_component(input_name "${input_path}" NAME_WE)
        set(symbol_name "${input_name}_rtslp")
        if(DEFINED RTSL_EMBED_NAME AND NOT RTSL_EMBED_NAME STREQUAL "")
            set(symbol_name "${RTSL_EMBED_NAME}")
        endif()
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
