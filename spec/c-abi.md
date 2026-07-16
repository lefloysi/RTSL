# RTSL C ABI Specification

The C ABI is declared in `rtsl/include/rtsl.h`.

## Handles

- `rtsl_context`
- `rtsl_module`
- `rtsl_linker`

Handles created by the ABI must be destroyed by the matching destroy function.

## Results And Diagnostics

Result codes:

- `RTSL_OK`
- `RTSL_ERROR_INVALID_ARGUMENT`
- `RTSL_ERROR_COMPILE_FAILED`
- `RTSL_ERROR_LINK_FAILED`
- `RTSL_ERROR_INTERNAL`
- `RTSL_ERROR_ARTIFACT_FAILED`

Diagnostics are queried from the context by count and index.

## Modules

`rtslCompileSource` returns an object module.

`rtslLoadModule` loads emitted artifact bytes through a context.

`rtslLoadModuleFromBytes` loads artifact bytes without a context and returns
null on failure.

`rtslModuleGetBytecode` returns borrowed artifact bytes.

## Reflection

Resource reflection exposes binding name, source resource type, group, member,
access, and uniform kind.

Stage reflection exposes host-visible stage fields. Backend-only inter-stage
fields are serialized in artifacts but not exposed as host reflection.

Entry reflection exposes backend entry name and authored stage identifier.

Returned strings are owned by the module.

## Linker

`rtslCreateLinker` creates a linker attached to a context. Inputs are added as
module handles or raw artifact bytes.

`rtslLinkLibrary` emits a library module. `rtslLinkProgram` emits a program
module.

## Null And Error Behavior

Functions that receive null where a valid pointer is required fail by returning
null, zero, or an empty result according to the function's return type.
Context-taking operations update the context result when possible.

The ABI must not leak C++ exceptions.

## Open Questions

- Thread-safety is not specified.
- Diagnostic lifetime after subsequent operations is not specified.
- ABI version discovery beyond the CLI version string is not specified.
