#ifndef RTSL_H
#define RTSL_H

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#    if defined(RTSL_BUILD_SHARED)
#        define RTSL_API __declspec(dllexport)
#    elif defined(RTSL_SHARED)
#        define RTSL_API __declspec(dllimport)
#    else
#        define RTSL_API
#    endif
#else
#    define RTSL_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct rtsl_module_t* rtsl_module;
typedef struct rtsl_context_t* rtsl_context;
typedef struct rtsl_linker_t* rtsl_linker;

typedef enum rtsl_result_code {
    RTSL_OK = 0,
    RTSL_ERROR_INVALID_ARGUMENT = 1,
    RTSL_ERROR_COMPILE_FAILED = 2,
    RTSL_ERROR_LINK_FAILED = 3,
    RTSL_ERROR_INTERNAL = 4
} rtsl_result_code;

typedef struct rtsl_result {
    int code;
    const char* text;
} rtsl_result;

typedef struct rtsl_blob {
    const uint8_t* data;
    size_t size;
} rtsl_blob;

typedef enum rtsl_output_kind {
    RTSL_OUTPUT_OBJECT = 1,
    RTSL_OUTPUT_MODULE = 2,
    RTSL_OUTPUT_LIBRARY = 3,
    RTSL_OUTPUT_PROGRAM = 4
} rtsl_output_kind;

typedef enum rtsl_diagnostic_severity {
    RTSL_DIAG_IGNORED = 0,
    RTSL_DIAG_NOTE = 1,
    RTSL_DIAG_WARNING = 2,
    RTSL_DIAG_ERROR = 3,
    RTSL_DIAG_FATAL = 4
} rtsl_diagnostic_severity;

typedef struct rtsl_diagnostic {
    int code;
    rtsl_diagnostic_severity severity;
    const char* source_name;
    size_t offset;
    uint32_t line;
    uint32_t column;
    const char* text;
} rtsl_diagnostic;

typedef enum rtsl_access {
    RTSL_ACCESS_READ_WRITE = 0,
    RTSL_ACCESS_READ_ONLY = 1,
    RTSL_ACCESS_WRITE_ONLY = 2
} rtsl_access;


typedef enum rtsl_uniform_kind {
    RTSL_UNIFORM_KIND_UNIFORM_BUFFER = 0,
    RTSL_UNIFORM_KIND_STORAGE_BUFFER = 1,
    RTSL_UNIFORM_KIND_SAMPLER = 2,
    RTSL_UNIFORM_KIND_IMAGE = 3,
    RTSL_UNIFORM_KIND_SAMPLED_IMAGE = 4,
} rtsl_uniform_kind;

/* Reflected resource binding.
 *
 * Strings are owned by the module. They remain valid until
 * rtslDestroyModule(module). */
typedef struct rtsl_uniform_info {
    const char* qualified_name; /* scope_name + "." + name, or name for the global scope */
    const char* type_name;      /* source resource type, e.g. UniformBuffer or Sampler2D */
    uint32_t group;
    uint32_t member;
    rtsl_access access;
    rtsl_uniform_kind kind;
} rtsl_uniform_info;

typedef enum rtsl_stage_role {
    RTSL_STAGE_ROLE_INPUT = 0,
    RTSL_STAGE_ROLE_VARYING = 1,
    RTSL_STAGE_ROLE_OUTPUT = 2
} rtsl_stage_role;

typedef enum rtsl_interpolation {
    RTSL_INTERP_NONE = 0,
    RTSL_INTERP_SMOOTH = 1,
    RTSL_INTERP_FLAT = 2
} rtsl_interpolation;

typedef enum rtsl_stage_field_placement {
    RTSL_STAGE_FIELD_USER = 0,
    RTSL_STAGE_FIELD_CLIP_POSITION = 1
} rtsl_stage_field_placement;

#define RTSL_NO_LOCATION 0xffffffffu

/* Reflected stage variable. */
typedef struct rtsl_stage_variable {
    rtsl_stage_role role;
    const char* payload_type;       /* stage payload type containing this field */
    const char* name;             /* field name */
    rtsl_interpolation interpolation;
    rtsl_stage_field_placement placement;
    uint32_t location;            /* RTSL_NO_LOCATION when routed through a built-in */
} rtsl_stage_variable;

/* Reflected backend entry point. */
typedef struct rtsl_entry_info {
    const char* name;  /* backend entry name */
    const char* stage; /* authored stage identifier, e.g. vertex or fragment */
} rtsl_entry_info;

RTSL_API rtsl_context rtslCreateContext(void);
RTSL_API rtsl_result rtslCtxGetResult(rtsl_context ctx);
RTSL_API size_t rtslCtxGetDiagnosticCount(rtsl_context ctx);
RTSL_API rtsl_diagnostic rtslCtxGetDiagnostic(rtsl_context ctx, size_t index);
RTSL_API void rtslDestroyContext(rtsl_context ctx);

RTSL_API rtsl_module rtslCompileSource(rtsl_context ctx, const char* source, size_t source_size, const char* source_name);

RTSL_API rtsl_blob rtslModuleGetBytecode(rtsl_module module);
RTSL_API rtsl_output_kind rtslModuleGetKind(rtsl_module module);
RTSL_API void rtslDestroyModule(rtsl_module module);

/* Load an emitted .rtslo/.rtslm/.rtsll/.rtslp artifact.
 * Returns NULL on failure. Read the context result and diagnostics for details. */
RTSL_API rtsl_module rtslLoadModule(rtsl_context ctx, const uint8_t* data, size_t size);
RTSL_API rtsl_module rtslLoadModuleFromBytes(const uint8_t* data, size_t size);

/* Resource reflection. */
RTSL_API size_t rtslModuleGetUniformCount(rtsl_module module);
RTSL_API int rtslModuleGetUniform(rtsl_module module, size_t index, rtsl_uniform_info* out_info);

/* Stage-variable reflection. */
RTSL_API size_t rtslModuleGetStageVariableCount(rtsl_module module);
RTSL_API int rtslModuleGetStageVariable(rtsl_module module, size_t index, rtsl_stage_variable* out_var);

/* Look up the assigned location of a reflected stage variable. */
RTSL_API int rtslModuleGetStageLocation(rtsl_module module, rtsl_stage_role role,
                                         const char* field_name, uint32_t* out_location);

/* Backend entry point reflection. */
RTSL_API size_t rtslModuleGetEntryCount(rtsl_module module);
RTSL_API int rtslModuleGetEntry(rtsl_module module, size_t index, rtsl_entry_info* out_entry);

RTSL_API rtsl_linker rtslCreateLinker(rtsl_context ctx);
RTSL_API int rtslLinkerAddModule(rtsl_linker linker, rtsl_module module);
RTSL_API int rtslLinkerAddBlob(rtsl_linker linker, const uint8_t* data, size_t size);
RTSL_API rtsl_module rtslLinkLibrary(rtsl_linker linker);
RTSL_API rtsl_module rtslLinkProgram(rtsl_linker linker);
RTSL_API void rtslDestroyLinker(rtsl_linker linker);

#ifdef __cplusplus
}
#endif

#endif /* RTSL_H */
