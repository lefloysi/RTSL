# CMake Integration

`cmake/Rtsl.cmake` provides `rtsl_add_program`, a helper for building RTSL
program artifacts from a host CMake target.

```cmake
list(APPEND CMAKE_MODULE_PATH "/path/to/RTSL/cmake")
include(Rtsl)

add_executable(game main.cpp)

rtsl_add_program(game
    RTSLC rtslc
    SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/world.rtsl"
    INCLUDE_DIRS
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/include"
    EMBED
)
```

## Signature

```cmake
rtsl_add_program(<target>
    RTSLC <rtslc-target-or-path>
    SOURCES <files...>
    [OUTPUT_DIR <dir>]
    [INCLUDE_DIRS <dirs...>]
    [DEPENDS <deps...>]
    [NAMESPACE <cpp-namespace>]
    [EMBED]
)
```

Required arguments:

- `RTSLC`: compiler executable or CMake target used for `compile` and
  `link-program` commands.
- `SOURCES`: one or more `.rtsl` source files.

Optional arguments:

- `OUTPUT_DIR`: output directory. Defaults to
  `${CMAKE_CURRENT_BINARY_DIR}/rtsl/<target>`.
- `INCLUDE_DIRS`: additional import search paths passed as repeated `-I`
  arguments to `rtslc compile`.
- `DEPENDS`: extra build dependencies for the generated commands.
- `NAMESPACE`: C++ namespace for embedded byte arrays. Defaults to `<target>`.
- `EMBED`: generate a C++ source file containing linked `.rtslp` bytes and add
  it to the host target.

## What It Builds

For each source, the helper:

1. Compiles `<name>.rtsl` to `<OUTPUT_DIR>/<name>.rtslo`.
2. Lets `rtslc compile` emit `<name>.rtslm` when the source exports symbols.
3. Links that object to `<OUTPUT_DIR>/<name>.rtslp`.
4. Adds a `<target>-rtsl` custom target and makes `<target>` depend on it.

Imports between sources in the same call are ordered automatically. External
imports use the `-I` search paths.

With `EMBED`, the helper writes `<target>_rtsl_embed.cpp` in `OUTPUT_DIR`
and adds it to the host target. The generated source defines arrays named
`<source_name>_rtslp` plus `<source_name>_rtslp_size` in the selected
namespace. Host code that uses those symbols should declare them where needed.

## Limits

- The helper links each source into its own `.rtslp`.
- The configured compiler must support the `rtslc` command surface documented
  in [rtslc](rtslc.md).
- There is no installed CMake package config documented in this repository.
