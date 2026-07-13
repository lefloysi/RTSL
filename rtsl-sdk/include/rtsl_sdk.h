#ifndef RTSL_SDK_H
#define RTSL_SDK_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RTSL_SDK_ARTIFACT_MAGIC 0x4c535452u
#define RTSL_SDK_ARTIFACT_VERSION_MAJOR 0u
#define RTSL_SDK_ARTIFACT_VERSION_MINOR 7u
#define RTSL_SDK_ARTIFACT_HEADER_SIZE 48u
#define RTSL_SDK_PAYLOAD_RECORD_SIZE 32u

#define RTSL_SDK_ARTIFACT_OBJECT 1u
#define RTSL_SDK_ARTIFACT_MODULE 2u
#define RTSL_SDK_ARTIFACT_LIBRARY 3u
#define RTSL_SDK_ARTIFACT_PROGRAM 4u

typedef struct rtsl_sdk_artifact_header {
    unsigned magic;
    unsigned version_major;
    unsigned version_minor;
    unsigned kind;
    unsigned endian;
    unsigned header_size;
    unsigned payload_count;
    size_t payload_record_offset;
    size_t file_size;
} rtsl_sdk_artifact_header;

typedef struct rtsl_sdk_artifact_view {
    rtsl_sdk_artifact_header header;
    const uint8_t* bytes;
    size_t size;
} rtsl_sdk_artifact_view;

typedef struct rtsl_sdk_result {
    int ok;
    const char* error;
} rtsl_sdk_result;

rtsl_sdk_result rtslSdkReadArtifactHeader(const uint8_t* bytes, size_t size, rtsl_sdk_artifact_view* out_view);

#ifdef __cplusplus
}
#endif

#endif
