# Windows installer

The CMake install tree is packaged as both a portable ZIP and an NSIS installer.
The MinGW build copies the matching GMP, MPFR, GCC, and pthread runtime DLLs
next to `terminalc.exe`.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DTERMINALC_ENABLE_LTO=ON
cmake --build build --parallel
cpack --config build/CPackConfig.cmake -G NSIS
```

The compiler, GMP, and MPFR packages must come from the same MSYS2 MinGW
prefix. A production workflow should Authenticode-sign the installer and
publish its SHA-256 checksum beside the artifact.
