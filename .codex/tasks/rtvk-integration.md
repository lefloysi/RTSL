# RTSL SPIR-V And rt-vk13 Integration

## Objective

Integrate the runtime-facing RTSL SDK and SPIR-V transpiler into Rutile's
Vulkan backend, while cleaning the reviewed SPIR-V emitter code.

## Architectural rule

`rt-vk13` consumes linked `.rtslp` bytes through `RTSL::sdk` and
`RTSL::spirv`. It must not link the RTSL compiler, reconstruct frontend
semantics, or let C++ exceptions cross its C implementation boundary.

## Documentation consulted

- `RTSL/AGENTS.md`
- `RTSL/README.md`
- `RTSL/docs/artifacts-sdk.md`
- `RTSL/docs/cmake.md`
- `RTSL/docs/language.md`
- `ARCHITECTURE.md`
- `bindings/c/include/rutile.h`

## Impact map

### Confirmed violations

- `rt-vk13` links the compiler target `rtsl` instead of `RTSL::spirv`.
- `rtvk_graphics_program_finalize` is an empty shader-compilation stub.
- The existing Vulkan reflection structures are disconnected from the new
  SDK `Program::resources()` model.
- The SPIR-V emitter uses an unnecessary optional for an exhaustive
  decoration mapping, mixes direct opcode mappings into custom lowering,
  uses unwanted suffixed member names, and contains unbraced control bodies.
- Example embedding still requests the removed `NAMESPACE` contract.

### Suspicious related locations

- Graphics descriptor layout creation only supports descriptor set zero.
- Uniform location names use fixed backend storage and must not truncate SDK
  resource names silently.
- Finalization must clean up partial Vulkan objects on every failure path.
- Existing examples still contain pre-v0.1 RTSL syntax.

### Inspected locations ruled out

- The public Rutile ABI already defines `rtGraphicsProgramSource` as linked
  RTSL program bytes; no ABI change is required.
- Shader modules only need to live through pipeline creation, but retaining
  them until reset matches the existing backend ownership model.
- `rtsl::Program` owns and validates its artifact data, so the C bridge may
  expose borrowed resource names while its opaque translation object lives.

## Pre-change invariants

- Graphics program source bytes are copied and owned by the Vulkan program.
- Pipeline creation is deferred until a framebuffer format is known.
- Uniform locations are program-owned and invalidated by reset/destruction.

## Post-change invariants

- Finalize validates `.rtslp`, transpiles vertex and fragment stages, creates
  both Vulkan shader modules, reflects supported set-zero resources, and
  creates the pipeline layout.
- Any failure leaves no partial shader modules, reflection handles, uniform
  locations, descriptor layout, or pipeline layout behind.
- The runtime backend links only the SDK/SPIR-V runtime surface.
- C++ exceptions remain inside the bridge.
- Direct RTIR-to-SPIR-V opcode mappings are backend-owned data; custom
  lowerings remain explicit code.
- Every touched control-flow body uses braces.

## Checklist

- [x] Inspect SDK, transpiler, Vulkan graphics program, descriptor binding,
      public ABI, CMake, examples, and error paths.
- [x] Clean the reviewed SPIR-V emitter structure and style.
- [x] Add a narrow C-compatible C++ transpilation/reflection bridge.
- [x] Implement transactional Vulkan graphics-program finalization.
- [x] Replace compiler linkage with `RTSL::spirv`.
- [x] Update affected embed consumers and v0.1 shader syntax as required.
- [x] Build RTSL tests and the Vulkan backend.
- [x] Run relevant tests/smoke coverage.
- [x] Repeat architectural searches and review the final diff.

## Verification

- `cmake --build out/build/x64-Debug --target rt-vk13 -- -j1`: passed.
- `cmake --build out/build/x64-Debug --target
  rutile-00-triangle-rtsl rutile-01-textured-quads-rtsl -- -j1`: passed.
- `cmake --build RTSL/out/build/x64-Debug --config Debug --target
  rtslc rtsl-tests rtsl-sdk-tests -- -j1`: passed.
- `ctest --test-dir RTSL/out/build/x64-Debug -C Debug
  --output-on-failure`: all 6 tests passed.
- Building the complete example executables also selects the legacy DX12
  backend, which still includes the removed compiler header `rtsl.h`. This is
  a separate DX12 migration; the shader compilation/embed targets pass.

## Final repository search

- No legacy Vulkan reflection structures or empty finalization stub remain.
- `rt-vk13` links `RTSL::spirv`; it does not link the compiler target.
- The emitter no longer reads the removed generic operand/literal fields.
- The reviewed emitter members no longer use trailing underscores.
- Touched control-flow bodies use braces.
- Final diff and whitespace checks completed.

## Blockers

None.

## Continuation notes

DONE.
