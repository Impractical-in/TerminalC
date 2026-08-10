# macOS installer

The native CPack `productbuild` generator creates an unsigned `.pkg` from the
CMake install tree. Preview builds can be produced with:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$(brew --prefix gmp);$(brew --prefix mpfr)"
cmake --build build --parallel
cpack --config build/CPackConfig.cmake -G productbuild
```

Distribution builds should sign the package with `productsign` and notarize it
with Apple's `notarytool`. Those credentials belong in protected release
secrets, not in pull-request CI.
