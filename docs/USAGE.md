# TerminalC usage

## Command-line interface

```text
terminalc [OPTIONS]
terminalc --eval "EXPRESSION"
terminalc --script FILE.tc
```

| Option | Description |
|--------|-------------|
| `-h`, `--help` | Print help and exit |
| `-V`, `--version` | Print version and exit |
| `-e`, `--eval EXPR` | Evaluate one expression and exit |
| `-f`, `--script FILE` | Run a `.tc` automation script and exit |
| `-p`, `--precision N` | Set decimal precision (2–100000) |
| `--no-color` | Disable ANSI colors |
| `--color` | Force ANSI colors |
| `--format plain\|json\|csv` | Emit human text, structured JSON, or CSV |

### Examples

```sh
terminalc --eval "2^3^2"
terminalc --eval "1/3+2/7"
terminalc --precision 80 --eval "sqrt(2)"
terminalc --precision 80 --eval "nthroot(2,3)"
terminalc --precision 50 --eval "ramanujan_pi()"
terminalc --no-color --eval "fact(20)"
terminalc --format json --precision 30 --eval "to(1/3*km,m)"
terminalc --script examples/engineering_cost.tc
```

Exit codes: `0` success, `1` evaluation/script error, `2` usage / argument error.

`--format json` returns a typed object with exact values preserved as strings;
`--format csv` emits a header and one scalar row (tensor values are flattened
into the final field). These formats are intended for `--eval` pipelines and
do not emit ANSI color. Script command messages may remain human-readable when
the script contains session commands such as `precision` or `help`.

For a native Python-wheel install from a checkout, run `python -m pip install .`.
The wheel contains the launcher plus the CMake-installed executable; source
builds still need a matching C compiler, GMP, and MPFR toolchain.

### `.tc` automation scripts

Scripts execute non-empty lines in order. Blank lines, `# comments`, and
`// comments` are ignored. Assignments persist for later lines, and the same
REPL commands and expressions are available without prompts:

```text
# loss and one gradient-descent update
target = 3
learning_rate = 1/10
weight = 0
gradient = 2 * (weight - target)
next_weight = weight - learning_rate * gradient
(weight - target)^2
(next_weight - target)^2
```

`--script` stops at the first evaluation or command error and returns exit code
`1`, making it suitable for CI checks and shell pipelines. See `examples/` for
complete cost-estimate and gradient-descent scripts.

## Interactive REPL

```text
terminalc
calc> help
calc> precision 20
calc> a = 1/3
calc> a + 2/7
calc> quit
```

### Session commands

| Command | Meaning |
|---------|---------|
| `help` / `?` | REPL overview |
| `help <topic>` / `? <topic>` | Per-function help with **every argument** documented |
| `help topics` | Index of documented topics by category |
| `help functions` / `help catalog` | Complete callable-function list with signatures and aliases |
| `quit` / `exit` / `:q` | Leave |
| `precision` | Show current digit count |
| `precision N` / `digits N` | Set precision |
| `angle radians` / `angle degrees` | Trig mode |
| `vars` | List variables |
| `clear_vars` | Drop user variables |
| `history` / `history clear` | Show or clear persistent command history |
| `!!` / `!N` | Replay the last or numbered history entry |
| Up/Down arrows | Browse history; Left/Right, Home, and End edit the current line |
| `hi` / `bye` | Friendly greeting or farewell easter eggs |
| `color on` / `color off` | Toggle color |
| `advanced on` / `advanced off` | Per-eval time, memory, output size |
| `mode advanced` / `mode normal` | Aliases for advanced on/off |

### Expressions

- Operators: `+ - * / % ^` (power is **right-associative**)
- Compare / logic: `== != < <= > >=`, `and` `or` `not` (also `&&` `||` `!`)
- Booleans: `true` `false`; ternary-style `if(cond, a, b)`
- Grouping: `( ... )`
- Assignment: `name = expression`
- Last result: `ans` or `_`
- Constants: `pi`, `e`, `phi`, `sqrt2`, `ln2`
- Integer base literals: `0xFF` (hex), `0b1010` (binary), `0o77` (octal)

### Functions

Run `help functions` inside TerminalC for the authoritative categorized list.
It includes callable functions, aliases, REPL-only sequence commands, geometry
methods, and signatures. Run `help <name>` for every argument and examples.
The same complete reference is available in docs/FUNCTIONS.md.

| Group | Names |
|-------|--------|
| Core / logic | `if`, `abs`, `min`, `max`, `clamp`, comparisons, boolean operators |
| Trig | `sin` `cos` `tan` `sind` `cosd` `tand`, `asin` `acos` `atan` `atan2` |
| Hyperbolic | `sinh` `cosh` `tanh` `asinh` `acosh` `atanh`, `hypot` |
| Roots / logs | `sqrt` `cbrt` `nthroot` / `nroot` / `root` `ln` `log` `log10` `logb` / `logn` / `log_base` `exp` `log2` `log1p` `expm1` `pow` `erf` `erfc` `gamma` `lgamma` `ramanujan_pi` |
| Statistics / ML | `mean` `median` `quantile` `variance` `stddev` `norm`, `covariance` / `correlation`, `sigmoid` / `logistic` `softplus` `logit` `relu` `sign` |
| Rounding | `abs` `floor` `ceil` `min` `max` `clamp` |
| Combinatorics | `fact` / `factorial` `gcd` `lcm` `ncr` / `choose` |
| Bases | `hex` `bin` `oct` `base(n,b)` / `tobase(n,b)` (`b` = 2…36) |
| Matrices / tensors | `zeros` `ones` `eye` `mat` `shape` `transpose` `det` `inv` `linsolve` `dot`; scalar broadcasting with `+ - * /` |
| Solvers / calculus | `solve` `diff` `integral` |
| Units / geometry | `to` `mag`, unit names, square/rectangle/circle/triangle/cube/etc. |
| Sequences | `fib(n)` list, `fibn(n)` nth (fast), AP/GP/HP, sums, primes |

Resource limits protect interactive sessions from accidental memory or CPU
exhaustion: `fact` and `ncr` accept `n <= 100000`, `fib` lists at most 10000
terms, `fibn` accepts indices up to 10000000, expression nesting is limited to
64 levels, exact power results are capped at approximately 8 million bits, and
numeric solve/diff/integral work is bounded (80 work digits, 2048 solver
iterations, and 200000 integration samples). `nthroot` accepts a positive
integer index up to 1000000.

Base helpers keep the integer value in `ans` but **display** it in the chosen base
(e.g. `hex(255)` → `0xFF`, `bin(10)` → `0b1010`). Combining with other operators
returns to normal decimal display (`hex(255)+1` → `256`).

### Algebra

These are REPL/script commands (also usable in `--script` files), not
expression functions accepted by `--eval`.

```text
solve_quadratic(a, b, c)

p = poly([1, -3, 2])     # highest degree first, exact rationals
degree(p)
evaluate(p, 5)
derivative(p)            # stores p_d
roots(p)                 # linear / quadratic

plot(sin(x), x=-2*pi..2*pi)  # native popup (Windows/macOS): real lines + zoom
plot(y=x, x=0..3)
plot_save(sin(x);cos(x), x=0..2*pi, curves.svg)  # multi-curve SVG export
```

On **Windows**, `plot(...)` opens a zoomable window drawn with **Win32 GDI**;
on **macOS**, it opens a native **Cocoa** window. Both use actual vector
lines — no gnuplot/SDL/Cairo required:

- **High resolution**: ~3 samples per horizontal pixel (2.5k–10k points)
- **Adaptive ticks**: major/minor grid + numeric labels that refine as you zoom
- **Axis labels**: `x` / `y`
- **Legend**: curve swatch + `y = <expression>`
- **Hover readout**: move near the curve to snap a point and show `(x, y)`

The `x=lo..hi` range is the initial horizontal domain and is sampled exactly.
The interactive window can intentionally move outside that domain after zooming
or panning; press `0` to reset it. The y-axis is auto-fitted with a small visual
margin so the curve does not touch the frame.

| Control | Action |
|---------|--------|
| Hover near curve | Snap marker + tooltip with `x` / `y` |
| Mouse wheel | Zoom toward cursor |
| Left-drag | Pan |
| `+` / `-` | Zoom in / out (center) |
| `0` | Reset view |
| `y` | Auto-fit Y to visible samples |
| `a` | Print current view range to the console |
| `Esc` / `q` | Close window |

If stdout is not a TTY (pipes/tests), or `TERMINALC_ASCII_PLOT=1` is set, or the
GUI cannot open, TerminalC falls back to the in-terminal ASCII plot.

`plot_save(expr; expr, x=lo..hi, file.svg)` samples one or more expressions and
writes a standalone SVG with axes, labels, legend, and separate curve colors.
The semicolon separates curves; expressions remain evaluated with the active
arbitrary precision before export.

### Base conversion examples

```text
calc> 0xFF + 1
256
calc> hex(255)
0xFF
calc> bin(10)
0b1010
calc> oct(64)
0o100
calc> base(255, 16)
0xFF
calc> lcm(3, 6)
6
calc> gcd(12, 18)
6
```

Color panels (when enabled) segment quadratic and polynomial output:

- **Cyan** — quadratic frame / plot frame  
- **Magenta** — polynomial frames  
- **Yellow** — section labels  
- **Green** — numeric values  
- **Red** — errors  

## Environment

| Variable | Effect |
|----------|--------|
| `NO_COLOR` | Disable color when set to any non-empty value |
| `TERMINALC_HISTORY_FILE` | Override the REPL history path; set empty to disable persistence |

## Notes

- Integers and pure rationals stay exact until a real function (or decimal literal) promotes to MPFR.
- **Display rules**
  - **Integers** (exact or integer-valued reals like `log(1000)`) print as plain integers — no `3.000…` padding and no precision banner.
  - **`precision N`** applies only to **decimal / float** lines.
  - Non-exact trig values near an integer do not carry across that boundary:
    `sind(89.999999)` displays `0.9999` at precision 4 and `0.9999999999` at
    precision 10.
  - **Rationals** (exact or recovered, e.g. `1/3`, `sin(pi/6)` → `1/2`) show **Exact:** fraction **and** **Decimal:** expansion.
  - Closed forms like `sqrt(2)` show **Nice:** + **Decimal:**.
