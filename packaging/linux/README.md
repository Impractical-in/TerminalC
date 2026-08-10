# Linux packages

On Debian/Ubuntu, install `cmake`, `ninja-build`, `libgmp-dev`, and
`libmpfr-dev`, then build and package:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
cpack --config build/CPackConfig.cmake -G DEB
cpack --config build/CPackConfig.cmake -G TGZ
```

The DEB generator enables shared-library dependency discovery. The TGZ is the
portable fallback for distributions that do not use Debian packages.
