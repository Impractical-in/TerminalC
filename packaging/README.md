# TerminalC packaging roadmap

TerminalC keeps one native CMake install surface and produces platform-native
artifacts from it. The project intentionally does not mark a stable release in
this beta cycle; the commands below are the repeatable packaging path for
preview builds.

## Artifact matrix

| Platform | Native artifact | Build command | Distribution target |
|---|---|---|---|
| macOS | `ZIP`, unsigned `.pkg` | `cpack -G ZIP` / `cpack -G productbuild` | GitHub Releases, Homebrew tap |
| Windows | `ZIP`, NSIS `.exe` | `cpack -G ZIP` / `cpack -G NSIS` | GitHub Releases, installer download |
| Linux | `.deb`, `.tar.gz` | `cpack -G DEB` / `cpack -G TGZ` | GitHub Releases, Debian/Ubuntu, tarball |
| Python | platform wheel + source distribution | `python -m build` | PyPI (`pip install terminalc`) |

The CPack packages include the installed documentation and examples. Windows
MinGW packages include the GMP, MPFR, GCC, and pthread runtime DLLs. macOS
packages are unsigned until a release job is given an Apple Developer signing
identity and notarization credentials.

## Local builds

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$(brew --prefix gmp);$(brew --prefix mpfr)"
cmake --build build --parallel
cpack --config build/CPackConfig.cmake -G ZIP
```

On Linux, use the system GMP/MPFR development packages and select `DEB` or
`TGZ`. On Windows, use the same MSYS2 MinGW prefix for the compiler, GMP, and
MPFR, then run CPack from that environment. The CMake configure step rejects
mixed MinGW runtime prefixes because those packages can corrupt the heap.

## pip / PyPI

The Python package is a thin launcher around the native executable. The wheel
build uses `TERMINALC_PYTHON_WHEEL=ON`, which installs the executable into a
private package directory so it cannot collide with the console-script shim.
Build a wheel or sdist with:

```sh
python -m pip install --upgrade build
python -m build
```

The release workflow should publish separately built wheels for macOS arm64,
macOS x86_64, Linux x86_64, and Windows x86_64, plus one source distribution.
PyPI publishing remains credential-gated and is not performed from ordinary
pull requests.

## Homebrew

The formula template is in `packaging/homebrew/terminalc.rb`. A tap release
should update its version, source URL, and checksum from the GitHub release,
then run `brew audit`, `brew install --build-from-source`, and the formula test.

## CI/release ownership

Pull requests build and test the native project. The package workflow is
manual/tag-driven so credentials, signing, notarization, and registry uploads
never run on untrusted pull-request code. The remaining release checklist is:

1. Build the three native package families and upload checksums.
2. Build and smoke-test the platform wheels.
3. Sign/notarize macOS artifacts and sign the Windows installer.
4. Publish the Homebrew formula update and PyPI artifacts.
5. Add package-manager install tests to the release workflow.
