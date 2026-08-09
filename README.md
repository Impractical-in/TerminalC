# TerminalC

**TerminalC** is a fast, lightweight terminal calculator written in C.  
It uses **GMP** for exact integers and rationals and **MPFR** for correctly
rounded, user-selectable real arithmetic.

```text
terminalc --eval "1/3+2/7"
# Exact: 13/21
```

## Features

- Exact integer / rational arithmetic (`1/3 + 2/7` stays exact)
- MPFR reals with `precision N` (default **4** digits; range 2–100 000)
- Constants, variables, assignments, and `ans` / `_`
- Right-associative exponentiation, scientific notation
- Trigonometry (radians / degrees), inverse/hyperbolic trig, roots (`sqrt`,
  `cbrt`, `nthroot`), logs,
  arbitrary-base logs (`logb`, `logn`), error/gamma special functions,
  `fact`, `gcd`, `lcm`, `ncr`, and Ramanujan's pi series (`ramanujan_pi()`)
- Statistics and ML helpers: `mean`, `median`, `quantile`, `variance`, `stddev`,
  `covariance`, `correlation`, `norm`, `sigmoid`, `logistic`, `softplus`,
  `logit`, `relu`, `sign`, `min`, `max`, `clamp`
- Number bases: literals `0x` / `0b` / `0o`, display helpers `hex` `bin` `oct` `base(n,b)`
- Fibonacci: `fib(n)`, fast `fibn(n)`
- Stable quadratic solver: `solve_quadratic(1, -3, 2)`
- Exact-coefficient polynomials: `p = poly([1, -3, 2])`, `degree`, `evaluate`, `derivative`, `roots`
- Interactive plots: `plot(sin(x), x=-2*pi..2*pi)` — **Windows/macOS native popup** with real
  vector lines, wheel zoom / drag pan (no extra graphics library); ASCII fallback
- SVG plot export: `plot_save(sin(x);cos(x), x=0..2*pi, curves.svg)` for reports and notebooks
- Persistent REPL history with `history`, `!!`, and numbered `!N` replay
- Matrices/tensors (1D–4D): `[1,2;3,4]`, `eye`, `zeros`, scalar broadcasting,
  element-wise unary math,
  `det`, `inv`, `linsolve`
- Numeric `solve` / `diff` / `integral` (free variable `x`)
- Units: `5*km`, `to(qty, unit)`; mensuration: `square.area(4)`, `sphere.volume(r)`, …
- Booleans, `help <topic>`, 715 regression and robustness tests
- Optional ANSI color for algebra / plot segments (auto-disabled for pipes / `NO_COLOR`)
- Machine-readable `--format json|csv` output for CI and data pipelines
- Interactive REPL and `.tc` automation scripts via `--script`

## MVP and use cases

TerminalC is designed as a small, offline, engineer-facing calculation tool:

- **Engineering and cost work**: exact fractions, unit conversion, dimensions,
  geometry, material/labor estimates, and reproducible `.tc` files.
- **Numerical methods and ML learning**: evaluate loss functions, inspect
  gradients, compare a gradient-descent step, solve roots, differentiate and
  integrate expressions, and increase display precision when needed.
- **Data and linear algebra**: vectors, matrices, determinants, inverses,
  linear solves, dot products, tensor shape checks, and batch-friendly output.
- **Research and education**: exact rational arithmetic, arbitrary-precision
  real display, prime/series functions, polynomial exercises, and transparent
  terminal sessions that work offline.
- **Automation**: run a checked-in `.tc` file in CI or from a shell with
  `terminalc --script analysis.tc`; comments, assignments, session commands,
  and expressions execute in order and stop on the first evaluation or command
  error. Algebra/plot commands are script-friendly even though they are not
  `--eval` expression functions.

The MVP deliberately focuses on a dependable terminal engine, exact arithmetic,
portable plots, documented functions, and a self-contained release archive.

## Portable installation

Release ZIP archives contain a native `terminalc` executable, the public header,
examples, license, and full usage documentation. Windows archives also include
the required GMP/MPFR/runtime DLLs, so they can be extracted and run without
MSYS2 or PATH editing. macOS and Linux builds use their platform's shared GMP
and MPFR libraries; the CMake install target and CPack ZIP generator produce the
same layout for local builds.

### macOS installation

TerminalC supports Apple Silicon and Intel macOS builds through Homebrew:

```sh
brew install cmake ninja gmp mpfr
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$(brew --prefix gmp);$(brew --prefix mpfr)"
cmake --build build --parallel
./build/terminalc --version
```

On an interactive Terminal session, `plot(...)` opens a native Cocoa window
with wheel zoom, drag-to-pan, hover readouts, and keyboard shortcuts. Set
`TERMINALC_ASCII_PLOT=1` to force the terminal renderer, which is also used
automatically for pipes and scripts.

## Project layout

```text
TerminalC/
├── include/terminalc.h    Public C API + version
├── src/
│   ├── main.c            CLI (--help, --eval, --script, --precision)
│   ├── color.c           Zero-cost ANSI when disabled
│   ├── value.c           Dual rational/real values
│   ├── calc.c            Session state & precision
│   ├── parse.c           Recursive-descent evaluator
│   ├── functions.c       Built-in math functions
│   ├── algebra.c         Quadratic + polynomials
│   ├── plot.c            Terminal plots
│   └── repl.c            Interactive shell and .tc scripts
├── examples/              Engineering and gradient-descent scripts
├── docs/USAGE.md         Full usage template
├── docs/FUNCTIONS.md     Complete signatures, arguments, examples, limits
├── CMakeLists.txt
└── pyproject.toml        pip/scikit-build-core packaging metadata
```

## Prerequisites / dependencies

Only two libraries (already used by TerminalC):

| Dependency | Role |
|------------|------|
| **GMP** | Exact integers / rationals (`gcd`, `lcm`, base conversion, factorials, …) |
| **MPFR** | Arbitrary-precision reals, trig, roots, logs, plot sampling |
| **Win32 GDI** (Windows only) | Interactive plot window (`gdi32` / `user32` — ship with the OS) |
| **Cocoa** (macOS only) | Interactive plot window (ships with macOS) |

No third-party render software (no gnuplot, SDL, Cairo, Qt). Plots use the
system windowing API for real lines + zoom; other platforms, pipes, and scripts
use the ASCII terminal plot.

**MSYS2 MinGW64 packages:** `mingw-w64-x86_64-gmp`, `mingw-w64-x86_64-mpfr`
(plus a C toolchain / CMake). Make them CMake-discoverable via `CMAKE_PREFIX_PATH`.

### pip / native wheel

The repository is wired for a native wheel through scikit-build-core:

```sh
python -m pip install .
terminalc --eval "sqrt(2)"
```

Building from source still requires a C compiler, CMake, GMP, and MPFR. Release
CI should publish platform-specific wheels with matching native libraries; on
Windows, do not mix a UCRT compiler with MSVCRT-built GMP/MPFR (use one MinGW
runtime family consistently). `TERMINALC_NATIVE` can point the Python shim at a
local executable while developing.

## Build (macOS)

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$(brew --prefix gmp);$(brew --prefix mpfr)"
cmake --build build --parallel
ctest --test-dir build --output-on-failure
cpack --config build/CPackConfig.cmake
```

## Build (MSYS2 MinGW64)

```sh
cmake -S . -B build -G "MinGW Makefiles" \
  -DCMAKE_C_COMPILER=/mingw64/bin/gcc.exe \
  -DCMAKE_RC_COMPILER=/mingw64/bin/windres.exe \
  -DCMAKE_PREFIX_PATH=/mingw64
cmake --build build
./build/terminalc.exe --help
ctest --test-dir build --output-on-failure   # 715 regression/robustness tests
cpack --config build/CPackConfig.cmake       # portable ZIP release archive
```

Regression tests live in **`tests/eval_catalog.tsv`** (bulk `--eval` cases) plus REPL
fixtures. See **[tests/README.md](tests/README.md)** for adding cases without
touching CMake. Do not reuse a build directory configured with a different MinGW
installation; reconfigure it with the matching compiler and GMP/MPFR prefix.

## Quick usage

```sh
terminalc --help
terminalc --version
terminalc --eval "2^3^2"
terminalc --precision 100 --eval "sqrt(2)"
terminalc                      # interactive REPL
terminalc --script examples/engineering_cost.tc
```

```text
calc> precision 20
calc> solve_quadratic(1, -3, 2)
calc> p = poly([1, -3, 2])
calc> evaluate(p, 5)
calc> plot(sin(x), x=-2*pi..2*pi)
```

For a complete categorized API list, run `terminalc` and enter `help functions`.
Use `help <name>` for each function's arguments, examples, aliases, and limits.

See **[docs/USAGE.md](docs/USAGE.md)** for the complete command reference.
See **[docs/FUNCTIONS.md](docs/FUNCTIONS.md)** for the complete function API,
including every argument, example, alias, and safety limit.

## Design notes

- **Light binary**: modular C17, `-O3`, section GC, optional LTO; color is string
  pointers that become empty when disabled (no runtime formatting cost).
- **Exact when possible**: rationals stay `mpq` until a real operation forces promotion.
- **Clean architecture**: public header + focused translation units; single `terminalc` executable.
- **Packaging**: calculation core stays native; `scikit-build-core` is prepared for wheels later.

## License

MIT — see [LICENSE](LICENSE).

Developer / organization metadata is embedded in the binary (Windows file
Properties → Details, and string table) as **Impractical.in** — not shown in
the interactive REPL.
