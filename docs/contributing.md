# Development Notes

Keep changes aligned across implementation, specs, and tests.

## Workflow

1. Identify the behavior being changed.
2. Change the owning compiler layer.
3. Add or update focused tests.
4. Update `spec/` for public behavior changes.
5. Run the relevant build and CTest commands.

## Verification

```powershell
cmake -S . -B out/build
cmake --build out/build --config Debug
ctest --test-dir out/build -C Debug --output-on-failure
```

For focused work:

```powershell
cmake --build out/build --config Debug --target rtslc
cmake --build out/build --config Debug --target rtsl-tests
```
