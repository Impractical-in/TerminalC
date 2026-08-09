# TerminalC function reference

This is the complete callable API for TerminalC 1.0.0-beta.1. The interactive
catalog (help functions) is kept in sync with this reference. Use
help <topic> inside the program for the same information in a compact,
searchable form.

## Value conventions

- Integer and rational literals stay exact: 1/3 is an exact rational.
- Functions that require a real value promote an exact value to the current
  MPFR precision. Set it with precision N or --precision N.
- Tensor literals use commas for a row and semicolons for rows:
  [1,2,3], [1,2;3,4]. Tensor elements must be scalar.
- x is the free variable for solve, diff, integral, sum, and plot.
- A unit is written by multiplying a number by a unit name, for example
  5*km. Use to(quantity, target_unit) to convert it.

## Core, logic, and selection

| Signature | Arguments | Example |
|---|---|---|
| if(cond, then_value, else_value) | cond is truthy when non-zero/non-false; the selected value is returned. | if(2>1, 10, 20) -> 10 |
| abs(x) | Scalar x; booleans become 0 or 1. | abs(-7) -> 7 |
| floor(x) / ceil(x) | Scalar x; round toward negative/positive infinity. | floor(-1.2) -> -2 |
| min(x1, ..., xn) / max(x1, ..., xn) | One or more scalar arguments and/or tensors; all elements participate. | max([1,5,3]) -> 5 |
| clamp(x, lo, hi) | Scalar x; inclusive bounds with lo <= hi. | clamp(12, 0, 10) -> 10 |

Boolean literals are true and false. Operators are ==, !=, <, <=, >, >=,
and, or, not and aliases &&, ||, !.

Unary numeric and activation functions accept either a scalar or a tensor. A
tensor input is mapped element-by-element while preserving its shape, for
example `relu([-2, 0, 3])` or `sqrt([1, 4])`.

## Trigonometry and logarithms

| Signature | Arguments | Example |
|---|---|---|
| sin(x), cos(x), tan(x) | Angle x in radians by default; angle degrees changes the REPL mode. | sin(pi/2) |
| sind(x), cosd(x), tand(x) | Angle x always in degrees. | sind(30) -> 1/2 |
| asin(x), acos(x), atan(x) | Scalar x; inverse functions return radians. | asin(1) |
| atan2(y, x) | Scalar coordinates; returns the quadrant-aware angle in radians. | atan2(1,1) |
| sinh(x), cosh(x), tanh(x) | Scalar hyperbolic argument. | tanh(0) -> 0 |
| asinh(x), acosh(x), atanh(x) | Scalar inverse-hyperbolic argument; mathematical domain rules apply. | asinh(1) |
| hypot(x, y) | Two scalars; stable sqrt(x*x + y*y). | hypot(3,4) -> 5 |
| sqrt(x) | Non-negative scalar for a real result. | sqrt(2) |
| nthroot(x, n) / nroot(...) / root(...) | Real n-th root; n is a positive integer. Negative x requires odd n. Tensors are element-wise with a scalar n. | nthroot(16,4) -> 2 |
| ln(x) | Natural logarithm; x > 0. | ln(e) |
| log(x) / log10(x) | Base-10 logarithm; x > 0. | log(1000) -> 3 |
| log2(x) | Base-2 logarithm; x > 0. | log2(8) -> 3 |
| log1p(x) | Computes ln(1+x) accurately near zero. | log1p(0.1) |
| exp(x) | Natural exponential. | exp(1) |
| expm1(x) | Computes exp(x)-1 accurately near zero. | expm1(0) -> 0 |
| cbrt(x) | Real cube root. | cbrt(27) -> 3 |
| pow(x, y) | Real power x^y; use ^ for ordinary expression powers. | pow(2,10) -> 1024 |
| logb(x, base), logn(x, base), log_base(x, base) | Logarithm of positive x in a positive base other than 1. | logb(8,2) -> 3 |
| erf(x), erfc(x) | Error function and complementary error function. | erf(0) -> 0 |
| gamma(x) | Gamma function; poles are rejected as non-finite. | gamma(5) -> 24 |
| lgamma(x) | Natural logarithm of the absolute gamma function. | lgamma(1) -> 0 |
| ramanujan_pi() | No arguments; computes pi from Ramanujan's rapidly convergent series. | ramanujan_pi() |

`ramanujan_pi()` evaluates Ramanujan's 1914 series directly:

```text
1/pi = (2*sqrt(2)/9801) * sum(k=0..infinity,
  (4k)!*(1103 + 26390*k) / ((k!)^4 * 396^(4k)))
```

The MPFR implementation stops when the next term is below the active
precision. Each term contributes about eight decimal digits. This function is
named in tribute to Srinivasa Ramanujan (1887-1920), whose work transformed
analysis and number theory.

## Statistics and machine learning helpers

Each statistics function accepts one or more scalar arguments and/or tensors.
variance is population variance (1/n), and norm is the Euclidean L2 norm.

| Signature | Arguments | Example |
|---|---|---|
| mean(values...) | Values to average. | mean([1,2,3]) -> 2 |
| variance(values...) | Values whose population variance is required. | variance([1,2,3]) -> 2/3 |
| stddev(values...) | Square root of population variance. | stddev([1,2,3]) |
| norm(values...) | L2 norm of all values. | norm([3,4]) -> 5 |
| median(values...) | Middle value after sorting all supplied values. | median([1,5,2]) -> 2 |
| quantile(values, probability) / percentile(...) | Linear-interpolated quantile with probability in [0,1]. | quantile([1,2,3,4],0.75) -> 3.25 |
| covariance(x, y) / cov(...) | Population covariance for equal-length samples. | covariance([1,2,3],[2,4,6]) |
| correlation(x, y) / corr(...) | Pearson correlation for equal-length non-constant samples. | correlation([1,2,3],[2,4,6]) -> 1 |
| sigmoid(x) | Scalar activation 1/(1+exp(-x)), evaluated stably for large absolute x. | sigmoid(0) -> 1/2 |
| logistic(x) | Alias for sigmoid(x). | logistic(0) -> 1/2 |
| softplus(x) | Stable log(1+exp(x)) transform. | softplus(0) |
| logit(x) | Inverse sigmoid, defined for 0 < x < 1. | logit(1/2) -> 0 |
| relu(x) | Scalar rectifier max(0,x). | relu(-2) -> 0 |
| sign(x) | Scalar sign: -1, 0, or 1. | sign(-7) -> -1 |

## Number theory and combinatorics

| Signature | Arguments | Example / limit |
|---|---|---|
| gcd(a, b) | Two exact integers. | gcd(12,18) -> 6 |
| lcm(a, b) | Two exact integers. | lcm(3,6) -> 6 |
| fact(n) / factorial(n) | Non-negative exact integer n. | fact(5) -> 120; n <= 100000 |
| ncr(n, k) / choose(n, k) | Non-negative exact integers with 0 <= k <= n. | choose(5,2) -> 10; n <= 100000 |
| isprime(n) | Exact integer primality test. | isprime(17) -> true |
| nextprime(n) | Next prime strictly above integer n. | nextprime(14) -> 17 |
| prevprime(n) | Previous prime strictly below integer n. | prevprime(14) -> 13 |

## Bases

| Signature | Arguments | Example |
|---|---|---|
| hex(n) | Exact integer; display the result in base 16. | hex(255) -> 0xFF |
| bin(n) | Exact integer; display in base 2. | bin(10) -> 0b1010 |
| oct(n) | Exact integer; display in base 8. | oct(64) -> 0o100 |
| base(n, radix) / tobase(n, radix) | Exact integer n; radix is an integer from 2 through 36. | base(255,16) -> 0xFF |

The numeric value remains in ans; only display changes. Combining the result
with another operator returns to normal decimal display.

## Matrices and tensors

| Signature | Arguments | Example |
|---|---|---|
| zeros(d1, ..., d4) | One to four non-negative integer dimensions. | zeros(2,3) |
| ones(d1, ..., d4) | One to four non-negative integer dimensions. | ones(2,2) |
| eye(n) | Non-negative integer square dimension. | eye(3) |
| mat(rows, cols, values...) | Non-negative integer dimensions followed by row-major scalar values. | mat(2,2,1,2,3,4) |
| shape(tensor) | A tensor; returns its dimension vector. | shape(zeros(2,3)) |
| transpose(A) | A rank-1 vector or rank-2 matrix. | transpose([1,2;3,4]) |
| det(A) | Square rank-2 matrix. | det([1,2;3,4]) -> -2 |
| inv(A) | Non-singular square rank-2 matrix. | inv(eye(2)) |
| linsolve(A, b) | Square matrix A and compatible vector/matrix b. | linsolve(eye(2),[5;7]) |
| dot(u, v) | Compatible vectors or matrices. | dot([1,2],[3,4]) -> 11 |

Matrix multiplication is supported by * for compatible matrices. When one
operand is a scalar, `+`, `-`, `*`, and `/` broadcast it across every tensor
cell while preserving the tensor shape: `[1,2] * 2`, `2 / [1,2]`, and
`[1,2;3,4] + 1` are all valid. Tensor-tensor `*` remains matrix
multiplication; tensor modulo is intentionally rejected. Resource limits
reject negative dimensions, excessive tensor cells, and incompatible shapes
with an error instead of allocating unbounded memory.

## Sequences, sums, and primes

For AP/GP/HP, a is the first term, d is the difference (or r the ratio), and
n is a one-based term index or list count.

| Signature | Arguments | Example |
|---|---|---|
| ap_term(a,d,n) / ap_nth(a,d,n) | Arithmetic progression term a + (n-1)d. | ap_term(2,3,5) -> 14 |
| ap_sum(a,d,n) | Sum of the first n AP terms. | ap_sum(2,3,5) -> 40 |
| ap_list(a,d,n) | Tensor containing the first n AP terms. | ap_list(2,3,3) |
| gp_term(a,r,n) / gp_nth(a,r,n) | Geometric progression term a*r^(n-1). | gp_term(3,2,4) -> 24 |
| gp_sum(a,r,n) | Sum of the first n GP terms. | gp_sum(3,2,4) -> 45 |
| gp_list(a,r,n) | Tensor containing the first n GP terms. | gp_list(3,2,3) |
| hp_term(a,d,n) / hp_nth(a,d,n) | Harmonic progression term 1/(a+(n-1)d). | hp_term(2,3,1) -> 1/2 |
| hp_sum(a,d,n) / hp_list(a,d,n) | Harmonic sum or first n terms. | hp_list(2,3,3) |
| sumi(n) / sp_sum(n) | Sum 1 + ... + n. | sumi(100) -> 5050 |
| sumsq(n) / sp_sumsq(n) | Sum of squares 1^2 + ... + n^2. | sumsq(10) -> 385 |
| sumcu(n) / sp_sumcu(n) | Sum of cubes 1^3 + ... + n^3. | sumcu(5) -> 225 |
| sumrange(lo, hi) | Inclusive integer sum from lo through hi. | sumrange(1,10) -> 55 |
| sum(expr, lo, hi) / sigma(expr, lo, hi) | Evaluate expression in free variable x for each integer in the inclusive range. | sum(x^2,1,5) -> 55 |
| primes(count) | Tensor of the first count primes. | primes(5) |
| primes_to(limit) / primes_upto(limit) | Tensor of all primes <= limit. | primes_to(10) |

fib(n) and fibn(n) are REPL commands (not general expression functions):
fib(n) prints the first n terms, and fibn(n) prints the nth term. Limits are
10,000 list terms and index 10,000,000.

## Solvers and calculus

These functions parse the first argument as an expression in the free variable
x. Root finding, differentiation, and integration evaluate through MPFR-backed
arithmetic. Work is capped at 80 decimal digits and bounded iterations/evaluations
so a very large display precision cannot cause an unbounded computation. The
integrator uses endpoint-safe tanh-sinh quadrature; increase precision for the
final display when you need more digits.

| Signature | Arguments | Example |
|---|---|---|
| solve(expr, guess) | Expression and Newton starting point. | solve(x^2-2,1) |
| solve(expr, lo, hi) | Expression and bracket endpoints; requires a sign-changing bracket. | solve(x^2-2,0,2) |
| diff(expr, x0) | Expression and point x0; central numerical derivative. | diff(x^3,2) -> 12 |
| integral(expr, a, b) | Expression and definite integration bounds. | integral(x,0,1) -> 1/2 |

## Units and measurements

| Signature | Arguments | Example |
|---|---|---|
| to(quantity, unit) | A measure and a compatible target unit. | to(5*km,m) -> 5000 m |
| mag(quantity) | A measure; returns its numeric magnitude. | mag(5*km) |
| number * unit_name | Build a dimensioned quantity. | 36*kmh |

Common names:

- Length: m, meter, metre, km, cm, mm, um, nm, ft, in, yd, mile
- Mass: kg, g, mg, ug, tonne, t, lb, oz
- Time: s, sec, ms, us, min, hour, hr, day
- Area: m2, km2, hectare, ha, acre, ft2
- Volume: m3, l, liter, ml
- Temperature: celsius, c, degc, fahrenheit, f, degf, kelvin, k
- Speed: mps, m_s, kmps, km_s, kmh, kmph, km_h, mph, fps, ft_s

help units prints the complete runtime unit catalog.

The second argument to `to` must be a bare unit such as `m` or `fahrenheit`,
not a scaled quantity such as `3*cm`.

Conversion factors and temperature offsets are stored as exact rational
constants and applied with MPFR at the active precision. For example,
`--precision 30 --eval "to(1/3*km,m)"` preserves the recurring decimal rather
than first rounding the magnitude through a C `double`.

## Geometry and mensuration

All arguments are scalar lengths (or compatible measures). The current
mensuration layer returns a scalar/real result and uses the standard formulas.
Geometry formulas use MPFR at the active precision, including π and Heron's
formula. Inputs are still interpreted as scalar magnitudes; unit dimensions
are not attached to the returned area/volume value.

| Signature | Example |
|---|---|
| square.area(side), square.perimeter(side) | square.area(4) -> 16 |
| rectangle.area(length,width), rectangle.perimeter(length,width) | rectangle.area(3,5) -> 15 |
| circle.area(radius), circle.circumference(radius), circle.diameter(radius) | circle.area(1) |
| triangle.area(base,height), triangle.area_heron(a,b,c) | triangle.area_heron(3,4,5) -> 6 |
| cube.volume(edge), cube.surface(edge) | cube.volume(2) -> 8 |
| cuboid.volume(l,w,h), cuboid.surface(l,w,h) | cuboid.volume(2,3,4) |
| sphere.volume(radius), sphere.surface(radius) | sphere.volume(1) |
| cylinder.volume(radius,height) | cylinder.volume(2,5) |
| cone.volume(radius,height) | cone.volume(2,5) |

## Algebra and plotting

The algebra and plotting entries below are REPL/script commands. They are
available in `.tc` automation files, but are not scalar expression functions
for `--eval`; use the interactive form shown in each example.

| Signature | Arguments / example |
|---|---|
| solve_quadratic(a,b,c) / quadratic(a,b,c) | Solve a*x^2+b*x+c=0; solve_quadratic(1,-3,2). |
| p = poly([a_n,...,a_0]) | Store exact rational coefficients, highest power first. |
| degree(p) | Return polynomial degree. |
| evaluate(p,x) | Evaluate a stored polynomial at x. |
| derivative(p) | Store the derivative as p_d. |
| roots(p) | Display linear or quadratic polynomial roots. |
| plot(expr, x=lo..hi) | Plot free-variable expression; for example plot(sin(x),x=-pi..pi). |

On Windows, plots use a Win32 GDI window; on macOS, they use a native Cocoa
window. Both provide zoom, pan, hover readout, and auto-fit. Pipes, tests,
unsupported desktop builds, and TERMINALC_ASCII_PLOT=1 use the terminal
ASCII renderer.

## Session-only commands

- precision [N] / digits N: show or set display precision, 2 <= N <= 100000.
- angle radians|degrees: choose the mode for sin, cos, and tan.
- vars, clear_vars: inspect or clear user variables.
- color on|off: toggle ANSI colors.
- advanced on|off: show time, memory, and output-size metrics.
- help topics, help functions, help units: browse this API.

.tc scripts run these commands and expressions sequentially with
terminalc --script FILE.tc; blank lines and # or // comments are ignored.

## Safety limits

Expression nesting is limited to 64 levels. Tensor dimensions/cells, exact
power growth, factorial/binomial inputs, prime generation, and Fibonacci output
are bounded so malformed or accidental inputs fail with a readable error
instead of exhausting the process.
