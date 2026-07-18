# Voxel Renderer RTSL Migration

## Objective

Move the terrain and water graphics programs in `examples/05-voxel-engine`
from embedded GLSL strings to linked RTSL programs consumed by `rt-vk13`,
including the terrain shader's masked pixel edges and clean GPU teardown.

## Architectural rule

The example embeds `.rtslp` artifacts at build time and passes them through
`rtGraphicsProgramSource`; runtime shader translation remains owned by the
Vulkan backend through `RTSL::sdk` and `RTSL::spirv`.

## Documentation consulted

- `RTSL/AGENTS.md`
- `RTSL/docs/language.md`
- `RTSL/docs/cmake.md`
- `RTSL/docs/artifacts-sdk.md`
- `examples/00-triangle`
- `examples/01-textured-quads`

## Impact map

### Confirmed violations

- The voxel example uses removed split vertex/fragment shader entry points.
- Terrain and water shaders are inline GLSL instead of RTSL artifacts.
- The example is disabled in `examples/CMakeLists.txt`.
- Its vertex attributes still use numeric locations despite the current named
  vertex-attribute ABI.
- The example requests the stale `RT_VALIDATION` layer name instead of the
  deployed `RT_VALIDATION_LAYER` module.
- `rtsl_add_program(... EMBED)` loses every artifact after the first when a
  target supplies multiple sources because its `-D` list argument is unquoted.
- Semantic analysis parses inline buffer layouts but represents their value as
  an opaque `struct`, making authored member access fail.
- IR type registration emits anonymous layout structs without retaining their
  member metadata, so member extraction silently preserves the whole struct.
- The initial RTSL terrain shader reduced edge and corner masks to a whole-face
  brightness approximation instead of evaluating the masked 8x8 border pixels.
- The voxel example destroys GPU resources without waiting for its final queue
  submission, leaving its depth allocation live during allocator teardown.
- The example destroys and recreates the depth texture and view handles on
  every resize even though `rtTextureData` replaces a texture's active image
  node and the existing view can be rebound.
- Vulkan texture image nodes are released without first being retired. The
  shared resource finalizer requires both `zombie` and a zero reference count,
  so parent-owned image nodes could reach zero without destroying their VMA
  allocation.

### Suspicious related locations

- Terrain's GLSL outline effect depends on integer bit operations and math
  intrinsics outside the released RTSL v0.1 standard library.
- Water's GLSL wave and highlight effects depend on trigonometric and power
  intrinsics outside that same surface.
- The two programs must retain independent uniform resources and blend state.

### Inspected locations ruled out

- The current Vulkan backend already supports multiple independent RTSL
  graphics programs and reflects their uniform buffers by name.
- Both programs use the same CPU vertex layout and need no runtime ABI changes.
- Water animation can remain an example-owned model transform without adding
  backend-specific language behavior.

## Invariants

### Pre-change

- Terrain and water share the seven-field `Vertex` buffer layout.
- Terrain is opaque, depth-tested, and back-face culled.
- Water is depth-tested, back-face culled, and alpha blended.

### Post-change

- Each program is a complete RTSL vertex/fragment artifact.
- Both artifacts consume the existing named CPU vertex layout.
- Terrain AO and directional face shading remain visible.
- Terrain edge and corner masks darken the same outer 8x8 pixel cells as the
  original shader.
- Water remains animated and alpha blended.
- No GLSL source or split graphics-shader API remains in the voxel example.

## Checklist

- [x] Inspect the example, runtime ABI, Vulkan program path, RTSL language,
      artifact embedding, and applicable instructions.
- [x] Define terrain and water RTSL stage/resource boundaries.
- [x] Migrate program creation and vertex attribute names.
- [x] Re-enable the example target.
- [x] Fix multi-artifact embedding at the shared CMake helper boundary.
- [x] Preserve inline-layout field identity through semantic member lookup and
      add compiler regression coverage.
- [x] Compile and link both RTSL artifacts.
- [x] Build the voxel executable and affected Vulkan backend.
- [x] Run relevant RTSL tests and runtime smoke verification.
- [x] Repeat architectural searches and review the final diff.
- [x] Restore masked terrain edge and corner pixels in RTSL.
- [x] Wait for final GPU work before destroying voxel resources.
- [x] Keep depth texture and view handles stable across resize; replace texture
      data and rebind the view.
- [x] Remove single-use transform wrappers and the redundant water-uniform
      alias; upload `glm::mat4` values directly.
- [x] Retire Vulkan texture image nodes when their owning texture releases
      them, including allocation-failure cleanup paths.
- [x] Rebuild, validate SPIR-V, and repeat runtime shutdown verification.

## Verification

- Built `rt-vk13` and `rutile-05-voxel-engine`; terrain and water RTSL
  artifacts compiled, embedded as independent symbols, and linked.
- Ran the standalone RTSL suite: all 6 tests passed, including SPIR-V
  validation coverage for inline uniform-layout member access.
- Ran the Vulkan voxel executable for 10 seconds. It remained alive until the
  smoke harness stopped it, with empty stdout and stderr.
- Rebuilt the corrected terrain shader and Vulkan backend under isolated output
  names because the user's old voxel process held the normal executable and
  backend DLL open.
- Vulkan validation initially caught an invalid early-return lowering shape;
  the shader now uses one final return and validates without SPIR-V errors.
- The isolated corrected run exited successfully with no VMA live-allocation
  report or assertion after texture nodes were retired before release.
- The final stable-handle build remained alive for 10 seconds, accepted a
  normal window close, returned zero, and emitted no validation or allocator
  diagnostics. The subsequent direct-matrix cleanup compiled successfully.
- `git diff --check` passed for both the framework and nested RTSL worktrees.

## Final repository search

- No GLSL markers, embedded shader strings, split graphics-shader calls, or
  temporary smoke instrumentation remain in `examples/05-voxel-engine`.
- The scene uniform block is authored as an inline layout struct.
- Terrain evaluates all four edge bits and four corner bits against the same
  outer 1/8 UV cells as the original 8x8 GLSL pixel mask.
- No temporary test output name remains in source or CMake configuration.

## Blockers

None.

## Continuation notes

DONE. The voxel example now consumes embedded RTSL programs through the Vulkan
backend's RTSL-to-SPIR-V path.
