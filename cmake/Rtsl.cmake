set(RTSL_CMAKE_MODULE_DIR "${CMAKE_CURRENT_LIST_DIR}" CACHE INTERNAL "RTSL CMake helper directory")

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

    set(_rtsl_source_include_dirs)
    foreach(source IN LISTS RTSL_SOURCES)
        get_filename_component(source_directory "${source}" DIRECTORY)
        if(source_directory)
            get_filename_component(source_directory "${source_directory}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
        else()
            set(source_directory "${CMAKE_CURRENT_SOURCE_DIR}")
        endif()
        list(APPEND _rtsl_source_include_dirs "${source_directory}")
    endforeach()
    list(REMOVE_DUPLICATES _rtsl_source_include_dirs)
    set(_rtsl_source_include_args)
    foreach(dir IN LISTS _rtsl_source_include_dirs)
        list(APPEND _rtsl_source_include_args -I "${dir}")
    endforeach()

    set(_rtsl_object_dir "${RTSL_OUTPUT_DIR}/objects")
    file(MAKE_DIRECTORY "${_rtsl_object_dir}")

    set(_rtsl_objects)
    foreach(source IN LISTS RTSL_SOURCES)
        get_filename_component(source_name "${source}" NAME_WE)
        get_filename_component(source_path "${source}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
        string(SHA256 source_hash "${source_path}")
        string(SUBSTRING "${source_hash}" 0 16 source_id)
        set(object_path "${_rtsl_object_dir}/${source_name}-${source_id}.rtslo")

        add_custom_command(
            OUTPUT "${object_path}"
            COMMAND "${_rtsl_compiler}" compile "${source}" -o "${object_path}"
                ${_rtsl_source_include_args}
                ${_rtsl_include_args}
            DEPENDS ${RTSL_SOURCES} ${RTSL_DEPENDS}
            VERBATIM
            COMMENT "RTSL compile ${source_name}"
        )
        list(APPEND _rtsl_objects "${object_path}")
    endforeach()

    add_custom_command(
        OUTPUT "${RTSL_OUTPUT}"
        COMMAND "${_rtsl_compiler}" link-program ${_rtsl_objects} -o "${RTSL_OUTPUT}"
        DEPENDS ${_rtsl_objects} ${RTSL_SOURCES} ${RTSL_DEPENDS}
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
            -P "${RTSL_CMAKE_MODULE_DIR}/RtslEmbed.cmake"
        DEPENDS ${_rtsl_inputs} "${RTSL_CMAKE_MODULE_DIR}/RtslEmbed.cmake"
        VERBATIM
        COMMENT "RTSL embed -> ${target_name}"
    )
    set_source_files_properties("${RTSL_OUTPUT}" PROPERTIES
        GENERATED TRUE
        CXX_SCAN_FOR_MODULES OFF
    )

    if(TARGET rtsl-sdk)
        target_link_libraries("${target_name}" PRIVATE rtsl-sdk)
    elseif(TARGET RTSL::sdk)
        target_link_libraries("${target_name}" PRIVATE RTSL::sdk)
    else()
        message(FATAL_ERROR "RTSL SDK target is unavailable for '${target_name}'")
    endif()
    set_target_properties("${target_name}" PROPERTIES CXX_SCAN_FOR_MODULES OFF)
    target_sources("${target_name}" PRIVATE "${RTSL_OUTPUT}")
endfunction()
