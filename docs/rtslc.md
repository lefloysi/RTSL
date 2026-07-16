# rtslc

Build the CLI:

```powershell
cmake --build out/build --config Debug --target rtslc
```

## compile

```powershell
rtslc compile input.rtsl -o output.rtslo [-I dir ...] [-v]
```

Compiles one source file. Exported declarations also produce a `.rtslm` module
interface next to the object.

## link-program

```powershell
rtslc link-program input.rtslo [more.rtslo ...] -o output.rtslp [-v]
```

Links objects and libraries into a final program.

## link-library

```powershell
rtslc link-library input.rtslo [more.rtslo ...] -o output.rtsll [-v]
```

Links objects and libraries into a reusable library. Exports also produce a
`.rtslm` module interface.

## dump

```powershell
rtslc dump input.rtslo
rtslc dump input.rtsll
rtslc dump input.rtslp
```

Prints textual RTIR for inspection.

## Failures

`rtslc` returns nonzero for invalid arguments, failed reads or writes,
compilation errors, link errors, and artifact decode errors.
