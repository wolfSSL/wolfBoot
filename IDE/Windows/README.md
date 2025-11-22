# wolfboot for Windows

A variety of Windows-based solutions exist. Here are notes for a no-IDE build on Windows from a DOS prompt.

See also:

- [VS Code](../VSCode/README.md)
- [CMake docs](../../CMake.md)
- [Compile docs](../../compile.md)
- [Windows docs](../../Windows.md)
- [Other docs](../../README.md)

# Example

See the [`[WOLFBOOT_ROOT]/tools/scripts/cmake_test.bat`](../../tools/scripts/cmake_test.bat) using cmake:

```dos
rmdir /s /q build-stm32l4

cmake --preset stm32l4
cmake --build --preset stm32l4
```

## Troubleshooting

#### cannot access the file

This error is typically caused by anti-virus software locking a file during build.

Consider excluding the build directory or executable from anti-virus scan.

```test
build-stm32l4\bin-assemble.exe - The process cannot access the file because it is being used by another process.
```
