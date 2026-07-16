# C ABI

The public C ABI is declared in `rtsl/include/rtsl.h`.

## Handles

- `rtsl_context`
- `rtsl_module`
- `rtsl_linker`

Destroy handles with their matching destroy functions. Borrowed strings and
byte spans remain valid until the owning module or context is destroyed.

## Operations

```c
rtsl_context ctx = rtslCreateContext();
rtsl_module object = rtslCompileSource(ctx, source, source_size, "shader.rtsl");

rtsl_linker linker = rtslCreateLinker(ctx);
rtslLinkerAddModule(linker, object);
rtsl_module program = rtslLinkProgram(linker);
```

Load existing artifact bytes with `rtslLoadModule`. Borrow serialized bytes
with `rtslModuleGetBytecode`.

## Diagnostics

After a failed context operation, query:

- `rtslCtxGetResult`
- `rtslCtxGetDiagnosticCount`
- `rtslCtxGetDiagnostic`

## Reflection

Reflection exposes artifact kind, resources, host-visible stage fields, and
linked entry points.

The ABI contract is specified in [../spec/c-abi.md](../spec/c-abi.md).
