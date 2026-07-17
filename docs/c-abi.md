# Compiler C ABI

`rtsl/include/rtslc.h` is the compiler and linker ABI. It is not the backend
SDK and exposes no reflection or transpiler interface.

## Compile And Link From Memory

```c
rtsl_context ctx = rtslCreateContext();

rtsl_module object =
    rtslCompileSource(ctx, source, source_size, "shader.rtsl");

rtsl_linker linker = rtslCreateLinker(ctx);
rtslLinkerAddModule(linker, object);

rtsl_module program = rtslLinkProgram(linker);
rtsl_blob bytes = rtslModuleGetBytecode(program);
```

`rtsl_blob` is a borrowed compiler-output byte span. It remains valid until its
owning compiler module is destroyed.

Objects and libraries can be loaded back into a compiler context or linker:

```c
rtsl_module artifact = rtslLoadModule(ctx, bytes.data, bytes.size);
rtslLinkerAddBlob(linker, bytes.data, bytes.size);
```

Backend code does not use these handles. It loads final `.rtslp` bytes with the
C++ SDK described in [Artifacts and SDK](artifacts-sdk.md).

## Diagnostics

After a failed compiler or linker operation, query its context:

```c
rtsl_result result = rtslCtxGetResult(ctx);
size_t count = rtslCtxGetDiagnosticCount(ctx);

for (size_t i = 0; i < count; ++i) {
    rtsl_diagnostic diagnostic = rtslCtxGetDiagnostic(ctx, i);
}
```

## Lifetime

Destroy handles with their matching functions:

```c
rtslDestroyModule(program);
rtslDestroyModule(object);
rtslDestroyLinker(linker);
rtslDestroyContext(ctx);
```

ABI functions translate exceptions into status and diagnostics; exceptions do
not cross the C boundary.
