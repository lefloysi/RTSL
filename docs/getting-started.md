# Getting Started

## Requirements

- CMake 3.20 or newer
- C++23 compiler
- CLI11 for `rtslc`
- Catch2 for `rtsl-tests`

## Build

```powershell
cmake -S . -B out/build
cmake --build out/build --config Debug
ctest --test-dir out/build -C Debug --output-on-failure
```

## First Commands

```powershell
rtslc compile shader.rtsl -o out/shader.rtslo
rtslc link-program out/shader.rtslo -o out/shader.rtslp
rtslc dump out/shader.rtslp
```

Use `-I` to add import search paths:

```powershell
rtslc compile material.rtsl -I out/shaders -o out/material.rtslo
```

## Artifact Types

- `.rtslo`: compiled object
- `.rtslm`: module interface for imports
- `.rtsll`: linked library
- `.rtslp`: linked program for Rutile backends
