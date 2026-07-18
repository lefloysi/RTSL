# DX12 RTSL/HLSL Integration

## Objective

Add an SDK-only RTSL-to-HLSL transpiler and use it with DXC to make the DX12
graphics-program path consume linked RTSL programs and create DXIL pipelines.

## Architectural rule

RTSL remains the authored language and linked IR remains the backend boundary.
The HLSL transpiler consumes only `RTSL::sdk`; the DX12 backend owns DXC,
root-signature, input-layout, and pipeline-state policy.

## Documentation consulted

- `RTSL/AGENTS.md`
- `RTSL/docs/language.md`
- `RTSL/transpilers/spirv`
- Microsoft `D3D12_SHADER_BYTECODE` documentation
- Microsoft DirectX Shader Compiler documentation

## Impact map

### Confirmed violations

- `rt-dx12` accepts RTSLP bytes but finalization always returns unsupported.
- DX12 pipeline preparation records formats without creating a pipeline state.
- RTSL currently exposes only a SPIR-V transpiler.
- The DX12 target has no DXC integration or stage bytecode storage.

### Suspicious related locations

- Root parameters are currently fixed rather than derived from RTSL resources.
- Vertex attributes now carry RTSL field names, which need stable HLSL semantics.
- Command-buffer uniform binding must match the transpiler's register assignment.
- Raster, blend, depth, and render-target state must be transferred into the PSO.

### Inspected locations ruled out

- `rtGraphicsProgramSource` already provides the correct backend-neutral RTSLP
  boundary.
- The Windows SDK provides `dxcapi.h`, `dxcompiler.dll`, and DXIL validation.
- Direct3D 12 accepts compiled shader blobs through `D3D12_SHADER_BYTECODE`.

## Invariants

### Pre-change

- Vulkan consumes RTSLP through the SPIR-V transpiler.
- DX12 cannot finalize or prepare any RTSL graphics program.

### Post-change

- `RTSL::hlsl` emits deterministic vertex and fragment HLSL from linked IR.
- DX12 compiles emitted HLSL to validated DXIL in memory.
- RTSL reflection owns logical resources; DX12 assigns matching registers and
  root parameters.
- A graphics PSO is created lazily for the active color/depth formats.
- No user-authored or embedded HLSL is introduced into examples.

## Checklist

- [x] Inspect SDK IR, SPIR-V transpiler, DX12 program/pipeline code, and DXC availability.
- [x] Design the HLSL source API and deterministic naming/register rules.
- [x] Implement and package `RTSL::hlsl`.
- [x] Add HLSL transpiler tests covering stages, interfaces, resources, and control flow.
- [x] Integrate DXC and RTSLP loading into `rt-dx12`.
- [x] Implement root-signature reflection and graphics pipeline creation.
- [x] Build and run the triangle through DX12.
- [x] Repeat architectural searches, review the final diff, and run repository verification.

## Verification

- `rtsl-tests`: 645 assertions in 155 test cases passed.
- `rt-dx12`, triangle, textured quads, and voxel renderer built successfully.
- Triangle remained live through DX12 initialization and rendering.
- Textured quads remained live with reflected CBV/SRV/sampler bindings.
- Voxel renderer remained live with both RTSL programs, depth, blending, and
  structured fragment control flow.

## Final repository search

No draft bridge, fixed shader binding limit, Vulkan-only finalization error, or
unimplemented DX12 pipeline path remains. `RTSL::hlsl` is consumed only by the
DX12 backend and package test.

## Blockers

None.

## Continuation notes

Complete.
