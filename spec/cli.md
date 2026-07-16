# rtslc CLI Specification

`rtslc` is the RTSL command-line compiler and linker.

## Global Behavior

The driver requires exactly one subcommand. With no arguments it prints help
and exits with failure.

`--version` prints `rtslc 0.1`.

`-v` or `--verbose` prints extra diagnostics.

## compile

```text
rtslc compile input.rtsl -o output.rtslo [-I dir ...]
```

`-o, --output` is required. `-I, --include-dir` may be repeated.

Exports also produce a `.rtslm` module interface next to the object.

## link-program

```text
rtslc link-program input.rtslo [input.rtslo ...] -o output.rtslp
```

Links object or library inputs into a program artifact.

## link-library

```text
rtslc link-library input.rtslo [input.rtslo ...] -o output.rtsll
```

Links object or library inputs into a library artifact. Exports also produce a
`.rtslm` module interface next to the library.

## dump

```text
rtslc dump input
```

Prints textual RTIR for an artifact.

## Exit Status

The driver exits with `0` on success and nonzero on invalid arguments, failed
input reads, compile failures, link failures, artifact read failures, or output
write failures.

## Open Questions

- Output path defaults are not specified.
- Machine-readable diagnostics and dependency-file output are not specified.
- Help text is not a stable v0.1 contract.
