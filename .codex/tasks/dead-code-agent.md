# Dead Code / Stale Artifact Cleanup

## Objective

Find old code, unreachable helpers, stale examples, generated leftovers, or files no longer wired into the build. Make safe cleanup edits only when evidence is strong, without touching documentation/spec files or public v0.1 API behavior.

## Architectural Rule

Cleanup must preserve the compiler ownership boundaries and the documented v0.1 surface. This pass is limited to code/build/tests/examples and generated artifacts; docs/spec are read-only by user direction.

## Documentation Consulted

- `AGENTS.md`
- `CMakeLists.txt`
- `cmake/Rtsl.cmake`
- `.gitignore`

Docs/spec files were not edited.

## Impact Map

### Confirmed Violations

- `workspace/rtslc.exe`: tracked copied executable, also ignored by `.gitignore` as a stray tool binary. No source/build references require it.
- `cmake/Rtsl.cmake`: `_rtsl_module_paths` is populated but never read.

### Suspicious Related Locations

- `tools/Rtsl.LanguageServer/bin/`, `tools/Rtsl.LanguageServer/obj/`, Visual Studio `bin/`, `obj/`, `.vs/`, and `UpgradeLog*.htm`: ignored generated output exists locally but is untracked, so it was left untouched.
- `workspace/default.rtsl`: tracked sample source, not enough evidence to remove.
- `tests/shaders/graphics_program.rtsl`: untracked but referenced by `CMakeLists.txt` and smoke tests, so not stale.

### Inspected And Ruled Out

- `CMakeLists.txt`: all listed C++ source and test files exist.
- `tests/*.cpp`: wired into `rtsl-tests`.
- `rtsl/src`, `rtsl/include`, `rtsl-sdk/include`, `rtslc/src`: files are referenced by build sources or public headers.
- `tools/Rtsl.VisualStudio/*.csproj` and `tools/Rtsl.LanguageServer/*.csproj`: project files reference their current source files.

## Pre-change Invariants

- Public headers stay intact.
- Parser, semantic analysis, lowering, backend-neutral IR, and artifact behavior are not changed.
- Smoke shader remains available for CMake tests.
- Docs/spec remain untouched.

## Checklist

- [x] Inspect build files and source/test layout.
- [x] Search TODO/FIXME/stale/generated markers.
- [x] Identify tracked generated leftovers separately from ignored local output.
- [x] Remove only strongly evidenced stale artifacts/build leftovers.
- [x] Run repository-defined build/test listing or relevant compile check.
- [x] Repeat repository search after implementation.
- [x] Review diff for accidental docs/spec edits and special-case logic.

## Verification

- `cmake --build out\build --config Debug`: failed after reaching compilation due to an existing/concurrent `rtsl/src/ir/ir.cpp(2212,41)` aggregate initialization error in `PendingStage`. No cleanup-edited source was implicated.
- `ctest --test-dir out\build -C Debug -N`: passed test discovery; listed `rtsl-tests`, `rtslc-smoke-prepare`, `rtslc-smoke-compile`, `rtslc-smoke-link`, and `rtslc-smoke-dump`.

## Final Searches

- `rg -n "_rtsl_module_paths|workspace/rtslc.exe|rtslc\.exe" .gitignore CMakeLists.txt cmake tools workspace rtslc rtsl tests`: only `.gitignore:369:workspace/rtslc.exe` remains.
- `git diff -- cmake/Rtsl.cmake .codex/tasks/dead-code-agent.md workspace/rtslc.exe`: reviewed cleanup diff. Note: `cmake/Rtsl.cmake` already had concurrent edits unrelated to this cleanup.

## Result

DONE. Removed one tracked generated binary from the working tree and one unused CMake local variable. Left ignored generated local output alone because it is untracked.
