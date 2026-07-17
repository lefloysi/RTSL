# Command Line

`rtslc` compiles source files, links artifacts, and dumps textual RTIR.

## Compile

```powershell
rtslc compile shader.rtsl -o shader.rtslo
```

This reads one source file and writes one object file. If the source exports
declarations, the compiler also writes a `.rtslm` module interface next to the
object.

Use `-I` to add import search directories:

```powershell
rtslc compile material.rtsl -I shaders -I generated/shaders -o material.rtslo
```

Imports are written as quoted paths in source:

```rtsl
import "shared/math";
```

Compile exported dependencies first so their `.rtslm` files are available:

```powershell
rtslc compile shaders/shared/math.rtsl -o build/shaders/shared/math.rtslo
rtslc compile shaders/world.rtsl -I build/shaders -o build/shaders/world.rtslo
rtslc link-program build/shaders/world.rtslo build/shaders/shared/math.rtslo -o build/shaders/world.rtslp
```

## Link Program

```powershell
rtslc link-program shader.rtslo -o shader.rtslp
```

This links objects and libraries into a program artifact. A Rutile backend
loads the `.rtslp` file through `RTSL::sdk`. Artifact format 1.0 is not
compatible with older program files; rebuild them with the current compiler.

## Link Library

```powershell
rtslc link-library lighting.rtslo material.rtslo -o shading.rtsll
```

This links objects and libraries into a reusable library artifact. Libraries can
be used by later `link-program` or `link-library` calls.

## Dump

```powershell
rtslc dump shader.rtslp
```

This prints textual RTIR. It is mainly useful when debugging compiler output or
backend input.
