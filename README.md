RTSL is the shader language used by RutileGraphics. Its lexer, parser, retained AST,
semantic analysis, templates, IR construction, linker, artifact reader/writer and
command-line tools are written in Rutile under `source/rtsl`.

Build from the Outposts workspace root:

```
kiln build rtslc --profile release
kiln build rtsl-lsp --profile release
kiln build rtsl-lexer --profile release
kiln test rtsl --profile release
```

`rtslc main.rtsl --source helper.rtsl --module example --emit-program -o example.rtslp`
compiles the supplied sources as one program. `import <std>;` loads the RTSL standard
library from `std/std.rtsl` beside the compiler or language server. These are ordinary,
editable RTSL sources; Kiln and the Visual Studio extension deploy the directory with
their tools. Imports resolve supplied paths;
`--emit-object` and `--emit-module` emit linkable binary artifacts. `--library` or
`--object` adds an existing binary input. Omitting output checks source syntax and
semantics without emitting an artifact.

`--emit-interface library.rtsli` writes exported source declarations and generic
function definitions. A consumer supplies `--interface library.rtsli` and uses
`import <library>;` for the name given with `--module library`. Supply the binary
object separately with `--library` when exported functions have compiled bodies.
The retained version 3 interface format diagnoses exported generic records, whose
metadata it cannot represent.

Declarations and blocks end with semicolons, and attributes use Rutile syntax:

```
[[stage(compute)]]
[[workgroup_size(8, 8, 1)]]
fn lighting(usize x, usize y, usize z) {
    // Shader body.
};
```

Stage names are `vertex`, `fragment`, `geometry`, `tess_control`, `tess_eval` and
`compute`. Tessellation control uses an explicit function overload such as
`fn main<4>(patch<ControlPoint>& curve) -> ControlPoint`. The count is part of the
function's identity. Ordinary calls use `main<4>(curve)`; other values require their
own explicit overloads. This declaration does not instantiate a template. Parameter
interpolation is written before its type, for example
`[[smooth(uv)]] [[flat(normal)]] Vertex vertex`. Geometry emission uses
`emit vertex;` and `emit end;`, equivalent to `__return <- vertex;` and
`__return <- end;`. The `<-` operator is resolved on the result stream; the
`End` overload ends the current strip. An emitting function keeps its
`() emit -> Return` signature suffix.

The language server uses standard Content-Length framed JSON-RPC over stdin/stdout.
It supplies diagnostics, semantic tokens, completion, hover, definitions,
references, symbols, signatures and formatting. Formatting uses clang-format;
`RTSL_FORMATTER` selects its executable. `rtsl-lexer` preserves the editor's native
lexical span ABI and delegates tokenization to the same Rutile lexer.

C++ remains only at the graphics runtime boundary: `include/rtsl/IR`,
`include/rtsl/Serialization` and their three native implementation files read and
verify emitted artifacts for existing backends. Kiln builds these as
`rtsl-runtime`, with the native graphics runtime selected for each profile;
the compiler target contains Rutile sources and loads the separate RTSL standard library.
CMake builds that reader library;
CMake shader consumers must set `RTSLC_EXECUTABLE` to a Kiln-built compiler.

The Visual Studio extension packages the Kiln-built tools. Its
`RtslBuildDirectory`, `RtslLanguageServerPath` and `RtslLexerPath` MSBuild properties
can select a different build location.
