# CMake

`cmake/Rtsl.cmake` provides `rtsl_add_program` for compiling, linking, and
optionally embedding shader programs during the normal build.

## Basic Use

```cmake
list(APPEND CMAKE_MODULE_PATH "/path/to/RTSL/cmake")
include(Rtsl)

add_library(my_backend SHARED backend.cpp)
target_link_libraries(my_backend PRIVATE RTSL::sdk RTSL::spirv)

rtsl_add_program(my_backend
    RTSLC rtslc
    SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/world.rtsl"
    INCLUDE_DIRS
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/include"
)
```

`rtslc` is a build-time tool. It is not linked into `my_backend`.

## Signature

```cmake
rtsl_add_program(<target>
    RTSLC <rtslc-target-or-path>
    SOURCES <files...>
    [OUTPUT_DIR <dir>]
    [INCLUDE_DIRS <dirs...>]
    [DEPENDS <deps...>]
    [EMBED_NAME <c-symbol>]
    [EMBED]
)
```

- `RTSLC`: compiler executable or target used by custom build commands
- `SOURCES`: `.rtsl` sources to compile and link
- `OUTPUT_DIR`: artifact output directory
- `INCLUDE_DIRS`: import paths passed to `rtslc compile`
- `DEPENDS`: extra build dependencies
- `EMBED`: generate C++ storage for each linked `.rtslp`
- `EMBED_NAME`: C symbol for a single embedded program

The helper creates `<target>-rtsl` and makes the host target depend on it.

## Embedding

```cmake
rtsl_add_program(my_backend
    RTSLC rtslc
    SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/shaders/world.rtsl"
    EMBED_NAME game_world_shader
    EMBED
)
```

The generated source includes the SDK and defines an SDK-owned byte view:

```cpp
#include <rtsl/sdk.hpp>
#include <rtsl/spirv.hpp>

extern "C" const rtsl::ProgramBytes game_world_shader;

auto program = rtsl::load_program(game_world_shader);
if (!program) {
    report(program.error().message);
    return;
}

auto vertex = rtsl::spirv::transpile(*program, rtsl::Stage::vertex);
```

No compiler header or compiler library is required by this runtime code.
Without `EMBED_NAME`, a source named `world.rtsl` produces `world_rtslp`.
