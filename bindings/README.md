# RTSL Bindings

The supported v0.1 host API is the C ABI in `bindings/c/include/rtsl.h`.
It is usable from C and C++ and is the stable boundary for tools that embed the
compiler.

The C ABI provides:

- compiler contexts and diagnostics
- source compilation to artifact bytes
- artifact loading
- library and program linking
- uniform reflection
- stage-variable reflection
- entry reflection

The ABI owns returned strings and byte blobs through the `rtsl_module` or
`rtsl_context` that produced them. Destroy the owning handle after the data is
no longer needed.

Other language bindings can wrap the C ABI later; they are not part of v0.1.
