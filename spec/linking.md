# RTSL Linking Specification

The linker consumes `.rtslo` objects and `.rtsll` libraries.

## Library Linking

Library linking emits `.rtsll`.

The linker merges inputs, remaps ids, and resolves cross-input calls when the
callee is available. Unresolved calls may remain in libraries.

Library linking rejects duplicate exported function identities and stale
imported interfaces.

## Program Linking

Program linking emits `.rtslp`.

Program linking rejects:

- no inputs
- no stage entries
- unresolved calls
- duplicate exported function identities
- stale imported interfaces
- missing vertex stage in a graphics program
- missing fragment stage in a graphics program
- duplicate vertex stage entries
- duplicate fragment stage entries

Graphics programs contain exactly one `vertex` entry and one `fragment` entry.

Backend entry names:

- `vertex` -> `vert`
- `fragment` -> `frag`

## Imports

Imported export records carry interface identity data. A link fails when an
available export has a different interface identity than an imported record.

## Open Questions

- The exported interface hash algorithm is not specified.
- Cross-module inlining termination and failure behavior are not specified.
- Link ordering for duplicate private symbols is not specified.
