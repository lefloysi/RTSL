# RTSL SDK And Documentation Cleanup

## Objective

Move shared artifact and RTIR data into `rtsl-sdk`, remove misleading debug and
record-style artifact surfaces, and replace scattered spec prose with practical
documentation.

## Architectural Rule

`rtsl-sdk` owns stable serialized artifact data and backend/transpiler input
data. `rtsl` owns parsing, diagnostics, semantic analysis, lowering, linking
algorithms, serialization I/O, source spans, and editor services.

## Documentation Consulted

- `README.md`
- `docs/*.md`

## Impact Map

Confirmed violations:

- RTIR model lived in compiler-private `rtsl/src/ir/ir.hpp`.
- `Artifact` lived in compiler-private `rtsl/src/artifact/artifact.hpp`.
- SDK-visible stage tags and struct metadata did not round-trip.
- `write_debug_artifact`, `debug_bytes`, and `.rtsld` extension implied a
  debug artifact format that was not implemented.
- User-facing docs were split across several tiny duplicate markdown files.

Suspicious related locations:

- Parser still owns namespace and alias semantic rewriting. Audited by
  subagent; not changed in this pass because it is a larger AST/sema boundary
  migration.

Inspected and ruled out:

- Backend contract wording already describes transpilers consuming linked
  `.rtslp` artifacts.
- Remaining `byte offset` mentions are layout-rule text, not artifact records.

## Pre-Change Invariants

- Builds and tests passed before SDK split.
- Artifact stream was already direct ordered data, not a section table.

## Post-Change Invariants

- `rtsl-sdk/include/rtsl/ir.hpp` defines the shared RTIR model.
- `rtsl-sdk/include/rtsl/artifact.hpp` defines the shared artifact container.
- `rtsl-sdk/include/rtsl/artifact.hpp` owns RTSL artifact and source suffixes.
- Compiler IR header exposes lowering and verification only.
- Artifact I/O remains compiler-owned.
- Program artifacts with pending call targets are rejected by the reader.
- Stage tags and struct declaration metadata round-trip through artifacts.
- `ID<T>` is the SDK id type. Code uses `ID<IRInstruction>` and
  `ID<std::string>` directly, without aliases or tag types.

## Checklist

- [x] Move shared RTIR model into SDK.
- [x] Move shared artifact container into SDK.
- [x] Preserve opcode encoding order through SDK-owned `ir_ops.def`.
- [x] Remove fake debug artifact API.
- [x] Serialize SDK-visible stage tags and struct metadata.
- [x] Tighten artifact header and program invariants.
- [x] Replace old specs with practical docs under `docs/`.
- [x] Add real command-line, CMake embed, C ABI loading, artifact, and language examples.
- [x] Delete redundant markdown files.
- [x] Add focused artifact round-trip test.
- [x] Move artifact/source suffix facts into SDK.
- [x] Replace IR/String alias ids with direct `ID<T>` use.
- [x] Build `rtsl-tests`.
- [x] Run CTest.
- [x] Search for stale artifact-record/debug-doc terminology.
- [x] Search for stale `IRId`, `StringId`, tag-id, and invalid shader example syntax.
- [x] Replace IR function boolean flags with a single function kind.
- [x] Collapse IR function names to `display_name` and `link_name`.
- [x] Remove public ID arithmetic, stream insertion, and formatter support.
- [x] Remove redundant entry-point mangled name.
- [x] Trim docs and comments that described implementation history instead of contracts.
- [x] Remove duplicate `Artifact` mirror fields; `Artifact::module` is canonical.
- [x] Remove decorative artifact string table; serialize `source_name` directly.
- [x] Move artifact write declaration and SDK-friendly artifact read API into `rtsl-sdk`.
- [x] Fix README minimal shader and normalize docs heading style.
- [x] Remove public SDK stage-role/stage-variable API; RTSL source has no
      input/varying/output declarations.
- [x] Split docs between SDK artifact/RTIR inspection (`rtsl.h`) and compiler
      ABI (`rtslc.h`).
- [ ] Extract duplicated function-call inlining/remap machinery into a shared IR transform helper.

## Verification

- `cmake --build out\build --config Debug --target rtsl-tests`: passed.
- `ctest --test-dir out\build -C Debug --output-on-failure`: passed, 5/5.
- `cmake --build out\build --config Debug --target rtsl-tests`: passed after
  `ID<T>` tightening and SDK suffix move.
- `ctest --test-dir out\build -C Debug --output-on-failure`: passed, 5/5 after
  this cleanup pass.
- `cmake --build out\build --config Debug --target rtsl-tests`: passed after
  IR function and entry-point cleanup.
- `ctest --test-dir out\build -C Debug --output-on-failure`: passed, 5/5 after
  IR function and entry-point cleanup.
- `cmake --build out\build --config Debug --target rtsl-tests`: passed after
  canonical artifact and SDK artifact API cleanup.
- `ctest --test-dir out\build -C Debug --output-on-failure`: passed, 5/5 after
  canonical artifact and SDK artifact API cleanup.
- `cmake --build out\build --config Debug --target rtsl-tests`: passed after
  removing public stage-role/stage-variable SDK API.
- `ctest --test-dir out\build -C Debug --output-on-failure`: passed, 5/5 after
  removing public stage-role/stage-variable SDK API.

## Final Search

`rg` for deleted docs, debug artifact symbols, old nested entry type, artifact
record wording, payload records, section wording, `IRId`, `StringId`, tag-id
spellings, and invalid shader example syntax found no actionable leftovers.
Additional searches for stale function-name fields, entry `mangled_name`, public
ID arithmetic/formatting, deleted `docs/rtir.md`, and migration-history comments
found no actionable leftovers.
Additional search for `rtsl_stage_role`, `RTSL_STAGE_ROLE`,
`rtsl_stage_variable`, and `rtslModuleGetStage*` found no public SDK/doc
leftovers.

## Result

DONE.
