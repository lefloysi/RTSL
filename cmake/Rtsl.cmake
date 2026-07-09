function(rtsl_add_program target_name)
    set(options EMBED)
    set(oneValueArgs RTSLC OUTPUT_DIR NAMESPACE)
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
    if(NOT RTSL_NAMESPACE)
        set(RTSL_NAMESPACE "${target_name}")
    endif()

    set(_rtsl_include_args)
    foreach(dir IN LISTS RTSL_INCLUDE_DIRS)
        list(APPEND _rtsl_include_args -I "${dir}")
    endforeach()

    file(MAKE_DIRECTORY "${RTSL_OUTPUT_DIR}")

    # First pass: build a map of source basename -> .rtslm output path so we
    # can resolve `import <name>;` lines to concrete build-tree paths. Every
    # source contributes a candidate .rtslm sidecar even if it turns out to
    # have no exports (rtslc skips writing in that case — CMake sees a
    # missing file, but nothing will depend on it either).
    set(_rtsl_module_paths)
    foreach(source IN LISTS RTSL_SOURCES)
        get_filename_component(source_name "${source}" NAME_WE)
        set(_rtsl_module_${source_name} "${RTSL_OUTPUT_DIR}/${source_name}.rtslm")
        list(APPEND _rtsl_module_paths "${_rtsl_module_${source_name}}")
    endforeach()

    set(outputs)
    foreach(source IN LISTS RTSL_SOURCES)
        get_filename_component(source_name "${source}" NAME_WE)
        set(object_path "${RTSL_OUTPUT_DIR}/${source_name}.rtslo")
        set(module_path "${RTSL_OUTPUT_DIR}/${source_name}.rtslm")
        set(program_path "${RTSL_OUTPUT_DIR}/${source_name}.rtslp")

        # Scan the source for import lines so we can wire per-file dependency
        # ordering. Matches `import <foo.rtsl>;` and `import "foo";`; extension
        # is optional. Only imports that name a sibling source in this call
        # become build-graph edges — external imports fall through to the -I
        # search path at compile time and don't imply an ordering here.
        set(import_deps)
        if(EXISTS "${source}")
            file(STRINGS "${source}" _rtsl_import_lines REGEX "^[ \t]*(export[ \t]+)?import[ \t]")
            foreach(line IN LISTS _rtsl_import_lines)
                if(line MATCHES "(export[ \t]+)?import[ \t]+[<\"]([^>\"]+)[>\"]")
                    set(_imported "${CMAKE_MATCH_2}")
                    get_filename_component(_imported_base "${_imported}" NAME_WE)
                    if(DEFINED _rtsl_module_${_imported_base})
                        list(APPEND import_deps "${_rtsl_module_${_imported_base}}")
                    endif()
                endif()
            endforeach()
        endif()

        # The .rtslm sidecar is written by `rtslc compile` only when the source
        # has exports — not every source produces one. Listing it in BYPRODUCTS
        # (rather than OUTPUT) tells CMake "may or may not appear; don't fail
        # if missing" while still letting downstream commands depend on it for
        # ordering.
        add_custom_command(
            OUTPUT "${object_path}" "${program_path}"
            BYPRODUCTS "${module_path}"
            COMMAND "$<TARGET_FILE:rtslc>" compile "${source}" -o "${object_path}"
                -I "${RTSL_OUTPUT_DIR}"
                ${_rtsl_include_args}
            COMMAND "$<TARGET_FILE:rtslc>" link-program "${object_path}" -o "${program_path}"
            DEPENDS "${source}" rtslc ${RTSL_DEPENDS} ${import_deps}
            VERBATIM
            COMMENT "RTSL ${source_name} -> ${target_name}"
        )

        list(APPEND outputs "${program_path}")
    endforeach()

    add_custom_target("${target_name}-rtsl" DEPENDS ${outputs})
    add_dependencies("${target_name}" "${target_name}-rtsl")

    if(RTSL_EMBED)
        set(embed_cpp "${RTSL_OUTPUT_DIR}/${target_name}_rtsl_embed.cpp")
        string(JOIN ";" embed_inputs ${outputs})
        add_custom_command(
            OUTPUT "${embed_cpp}"
            COMMAND "${CMAKE_COMMAND}"
                -DRTSL_EMBED_INPUTS=${embed_inputs}
                -DRTSL_EMBED_OUTPUT=${embed_cpp}
                -DRTSL_EMBED_HEADER_NAME=rtsl_embed.hpp
                -DRTSL_EMBED_NAMESPACE=${RTSL_NAMESPACE}
                -P "${CMAKE_CURRENT_FUNCTION_LIST_FILE}"
            DEPENDS ${outputs}
            VERBATIM
            COMMENT "Embedding RTSL programs for ${target_name}"
        )
        target_sources("${target_name}" PRIVATE "${embed_cpp}")
        target_include_directories("${target_name}" PRIVATE "${RTSL_OUTPUT_DIR}")
    endif()
endfunction()

if(DEFINED RTSL_EMBED_INPUTS AND DEFINED RTSL_EMBED_OUTPUT AND DEFINED RTSL_EMBED_HEADER_NAME AND DEFINED RTSL_EMBED_NAMESPACE)
    file(WRITE "${RTSL_EMBED_OUTPUT}" "#include <cstddef>\n#include <cstdint>\n#include \"${RTSL_EMBED_HEADER_NAME}\"\n\n")

    foreach(input_path IN LISTS RTSL_EMBED_INPUTS)
        get_filename_component(input_name "${input_path}" NAME_WE)
        file(READ "${input_path}" _hex HEX)

        file(APPEND "${RTSL_EMBED_OUTPUT}" "namespace ${RTSL_EMBED_NAMESPACE} {\n")
        file(APPEND "${RTSL_EMBED_OUTPUT}" "alignas(16) const std::uint8_t ${input_name}_rtslp[] = {\n")

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
        file(APPEND "${RTSL_EMBED_OUTPUT}" "const std::size_t ${input_name}_rtslp_size = sizeof(${input_name}_rtslp);\n")
        file(APPEND "${RTSL_EMBED_OUTPUT}" "}\n\n")
    endforeach()
endif()
