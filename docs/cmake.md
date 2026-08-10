# CMake

`cmake/Rtsl.cmake` provides small build helpers for RTSL artifacts. The API is
split deliberately:

- `rtsl_add_program` defines one linked `.rtslp` program from one or more RTSL
  sources.
- `rtsl_embed_program` embeds one or more previously defined programs into a C++
  target.

`rtslc` is a build-time tool only. It is discovered from `RTSL_COMPILER`, the
in-tree `rtslc` target, or `PATH`.

## Basic Use

```cmake
list(APPEND CMAKE_MODULE_PATH "/path/to/RTSL/cmake")
include(Rtsl)

add_library(my_backend SHARED backend.cpp)
target_link_libraries(my_backend PRIVATE RTSL::sdk)

rtsl_add_program(world_shader
    SYMBOL world_rtslp
    SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/world.rtsl"
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/materials.rtsl"
    INCLUDE_DIRS
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/include"
)

rtsl_embed_program(my_backend
    PROGRAMS world_shader
)
```

## Program Signature

```cmake
rtsl_add_program(<program-target>
    SOURCES <files...>
    [OUTPUT <file.rtslp>]
    [OUTPUT_DIR <dir>]
    [SYMBOL <c-symbol>]
    [INCLUDE_DIRS <dirs...>]
    [DEPENDS <deps...>]
)
```

- `SOURCES`: `.rtsl` sources compiled to objects and linked into one program
- `OUTPUT`: final `.rtslp` path
- `OUTPUT_DIR`: artifact directory, used when `OUTPUT` is omitted
- `SYMBOL`: C symbol used if the program is later embedded
- `INCLUDE_DIRS`: import paths passed to `rtslc compile`
- `DEPENDS`: extra build dependencies

## Embedding Signature

```cmake
rtsl_embed_program(<target>
    PROGRAMS <program-targets...>
    [OUTPUT <embed.cpp>]
)
```

The generated source includes the SDK's public program header and defines
SDK-owned byte views:

```cpp
#include <rtsl/program.hpp>

extern "C" const rtsl::ProgramBytes world_rtslp;

auto program = rtsl::load_program(world_rtslp);
if (!program) {
    report(program.error().message);
    return;
}

```

No compiler header or compiler library is required by runtime code. RTSL does
not provide installed `RTSL::hlsl` or `RTSL::spirv` targets; its HLSL and
SPIR-V transpiler sources are compiled directly by the RTSL test targets and
Rutile backend targets that need them.
