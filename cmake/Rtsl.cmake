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

    file(MAKE_DIRECTORY "${RTSL_OUTPUT_DIR}")

    set(outputs)
    foreach(source IN LISTS RTSL_SOURCES)
        get_filename_component(source_name "${source}" NAME_WE)
        set(object_path "${RTSL_OUTPUT_DIR}/${source_name}.rtslo")
        set(program_path "${RTSL_OUTPUT_DIR}/${source_name}.rtslp")

        add_custom_command(
            OUTPUT "${object_path}" "${program_path}"
            COMMAND "${RTSLC}" compile "${source}" -o "${object_path}"
            COMMAND "${RTSLC}" link-program "${object_path}" -o "${program_path}"
            DEPENDS "${source}" "${RTSLC}" ${RTSL_DEPENDS}
            VERBATIM
            COMMENT "RTSL ${source_name} -> ${target_name}"
        )

        list(APPEND outputs "${program_path}")
    endforeach()

    add_custom_target("${target_name}-rtsl" DEPENDS ${outputs})
    add_dependencies("${target_name}" "${target_name}-rtsl")

    if(RTSL_EMBED)
        set(embed_cpp "${RTSL_OUTPUT_DIR}/${target_name}_rtsl_embed.cpp")
        add_custom_command(
            OUTPUT "${embed_cpp}"
            COMMAND "${CMAKE_COMMAND}"
                -DRTSL_EMBED_INPUTS="$<JOIN:${outputs},;>"
                -DRTSL_EMBED_OUTPUT="${embed_cpp}"
                -DRTSL_EMBED_NAMESPACE="${RTSL_NAMESPACE}"
                -P "${CMAKE_CURRENT_LIST_DIR}/RtslEmbed.cmake"
            DEPENDS ${outputs}
            VERBATIM
            COMMENT "Embedding RTSL programs for ${target_name}"
        )
        add_library("${target_name}-rtsl-embed" OBJECT "${embed_cpp}")
        target_link_libraries("${target_name}-rtsl-embed" PRIVATE rtsl)
        add_dependencies("${target_name}-rtsl-embed" "${target_name}-rtsl")
        target_sources("${target_name}" PRIVATE $<TARGET_OBJECTS:${target_name}-rtsl-embed>)
    endif()
endfunction()
