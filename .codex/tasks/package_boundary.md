# Package boundary task

## Objective

Install and export the runtime-facing RTSL SDK and SPIR-V transpiler so a
standalone backend can use `find_package(RTSL CONFIG)` and link `RTSL::sdk`
plus `RTSL::spirv` without receiving or depending on the compiler target.

## Architectural rule

The installed runtime package exports only the SDK and transpiler. The
compiler remains a build-time producer and is not part of the backend package
surface. `RTSL::spirv` depends on `RTSL::sdk` and Khronos SPIR-V headers, never
on the compiler.

## Documentation consulted

- `AGENTS.md`
- Root `CMakeLists.txt`
- `rtsl-spirv/CMakeLists.txt`
- `vcpkg.json`

## Impact map

### Confirmed violations

- Neither public library had install rules.
- No package config or exported targets existed.
- A clean standalone backend could not discover installed `RTSL::sdk` or
  `RTSL::spirv`.

### Suspicious related locations

- Static-library private dependency on `SPIRV-Headers::SPIRV-Headers` may be
  represented as a link-only exported dependency and therefore must be found
  by the package config.
- Public target output names and export names must remain clean.
- The CMake target is named `RTSL::sdk`, but the final C++ API is rooted in
  `rtsl`/`rtsl::ir`; the smoke consumer must not use the removed `rtsl::sdk`
  namespace.

### Inspected locations ruled out

- `vcpkg.json` already declares `spirv-headers`.
- Public include directories already distinguish build and install trees.
- `RTSL::spirv` already links publicly to `RTSL::sdk` and has no compiler link.

## Invariants

### Pre-change

- Build-tree aliases exist, but there is no installed consumer surface.

### Post-change

- `find_package(RTSL CONFIG REQUIRED)` defines exactly the runtime-facing
  imported targets `RTSL::sdk` and `RTSL::spirv` from this export.
- Installing the runtime package does not install/export `rtsl` or `rtslc`.
- Installed headers and libraries are relocatable.
- A separate consumer configures, compiles, and links against the install tree.

## Checklist

- [x] Inspect target/dependency graph and current install state.
- [x] Add concise install/export/package config rules.
- [x] Add a standalone package consumer smoke project.
- [x] Configure, build, install, configure consumer, and build consumer.
- [x] Search exported package and source for compiler leakage.
- [x] Review final diff and record verification.

## Verification

- `cmake -S . -B out\\build`: passed.
- `cmake --build out\\build --config Debug --target rtsl-sdk rtsl-spirv`:
  passed.
- `cmake --install out\\build --config Debug --prefix
  out\\package-boundary-install`: passed; installed only the two runtime
  libraries, public headers, and RTSL package files.
- Standalone consumer configure against the install tree: passed. Its CMake
  guard confirmed no compiler target was exported.
- `cmake --build out\\package-consumer-build --config Debug`: passed after the
  public API migration to `rtsl`/`rtsl::ir`.
- `out\\package-consumer-build\\Debug\\rtsl-package-consumer.exe`: exited 0.
- `git diff --check` on package-owned files: passed.

## Final repository search

- Installed `RTSLTargets.cmake` defines only `RTSL::sdk` and `RTSL::spirv`.
- `RTSL::spirv` links `RTSL::sdk` and the link-only Khronos headers target.
- No installed package file references `rtsl`, `rtslc`, or
  `RTSL::compiler`.
- No package smoke source uses the removed `rtsl::sdk` C++ namespace.

## Blockers

None.

## Continuation notes

Done. No compiler, SDK, or transpiler implementation source was touched.
