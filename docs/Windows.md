# Windows Barebones

A Windows (no Visual Studio installed), perhaps for VS Code or DOS command-prompt only setup.




```dos
winget install -e --id LLVM.LLVM
```

Expect an output like:

```text
Found LLVM [LLVM.LLVM] Version 21.1.3
This application is licensed to you by its owner.
Microsoft is not responsible for, nor does it grant any licenses to, third-party packages.
Downloading https://github.com/llvm/llvm-project/releases/download/llvmorg-21.1.3/LLVM-21.1.3-win64.exe
  ██████████████████████████████   356 MB /  356 MB
Successfully verified installer hash
Starting package install...
Successfully installed
```

Expected install directory is

```
C:\Program Files\LLVM\bin
```

Add a `HOST_CC` to the `configurePresets` - `cacheVariables` as is done in the [CMakeUserPresets.json.sample](../cmake/preset-examples/CMakeUserPresets.json.sample):

```
  "configurePresets": [
    {
      "cacheVariables": {
        "HOST_CC": "C:/Program Files/LLVM/bin/clang.exe"
      }
```
