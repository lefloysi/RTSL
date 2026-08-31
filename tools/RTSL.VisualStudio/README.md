# RTSL.VisualStudio

Visual Studio editor integration for `.rtsl` source files and build-produced `.rtslm` module interfaces.

The VSIX is intentionally a transport and editor-integration layer. `rtsl-lsp` is native C++ and calls the RTSL lexer and `CompilerInstance`; it does not maintain an independent parser or Sema. Build integration must provide the exact quoted logical imports and named library interfaces through `CompilerInvocation`, matching the compiler model. Source import paths never infer an extension or scan include directories.

## Build

Build `rtsl-lsp` using the repository CMake preset, then place `rtsl-lsp.exe` next to the VSIX assembly before packaging. The project references the installed Visual Studio 17 SDK packages and is intentionally named without a product-year suffix.

Current compiler frontend limitation: `CompilerInstance::validateImports` validates build-provided import names but does not deserialize/import their declarations into Sema. Therefore cross-module completion, definition, and template instantiation cannot be claimed until that compiler capability is implemented; the language client will expose it automatically once the frontend query API does.
