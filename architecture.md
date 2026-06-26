# RTSL Architecture Overview

This file is the short architecture entry point. The detailed architecture is
split across the specification documents in `docs/`.

RTSL source compiles into backend-neutral binary artifacts:

- `rtslo`: compiled object file for one source file
- `rtsll`: linked library file without required entry points
- `rtslm`: module/interface file emitted for export-bearing objects and libraries
- `rtslp`: final linked program consumed by Rutile backends

The high-level pipeline is:

```text
RTSL source -> compiler -> rtslo + optional rtslm
rtslo/rtsll -> linker -> rtsll + optional rtslm, or rtslp
rtslp -> Rutile backend -> SPIR-V, HLSL, MSL, or another target
```

Detailed documents:

- [Compiler architecture](docs/compiler-architecture.md)
- [Language semantics](docs/language.md)
- [Artifact formats](docs/artifacts.md)
- [RTIR](docs/rtir.md)
- [Linking](docs/linking.md)
- [Backend contract](docs/backend-contract.md)
