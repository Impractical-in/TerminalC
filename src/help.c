/**
 * Per-topic help: help <name>
 * Documents signatures, every argument, examples, and the implemented API.
 */
#include "terminalc.h"
#include "color.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    const char *key;       /* primary lookup name (lowercase) */
    const char *aliases;   /* space-separated alternate keys, or NULL */
    const char *category;  /* for listing */
    int available;         /* retained for compatibility with older entries */
    const char *signature;
    const char *summary;
    /* One argument per line: "name - description" */
    const char *args;
    const char *examples;
    const char *notes; /* optional extra / see-also */
} TerminalCHelpEntry;

/*
 * Keep entries roughly alphabetical within categories.
 * Keep entries grouped so help topics remains easy to scan.
 */
static const TerminalCHelpEntry g_help[] = {
    /* ---- session / meta ---- */
    {
        "help", "?", "session", 1,
        "help [topic]",
        "Show general help, or detailed help for one topic.",
        "topic - optional name (function, module, or category)",
        "  help\n  help hex\n  help topics\n  help square",
        "Use: help topics   to list every known topic."
    },
    {
        "topics", "list commands", "session", 1,
        "help topics",
        "List all documented help topics grouped by category.",
        NULL,
        "  help topics",
        NULL
    },
    {
        "functions", "function catalog callable", "session", 1,
        "help functions\nhelp catalog\nhelp list",
        "List every callable function, alias, and signature grouped by use case.",
        NULL,
        "  help functions\n  help catalog",
        "Use help <name> for argument descriptions and examples."
    },
    {
        "precision", "digits", "session", 1,
        "precision [N]\ndigits N",
        "Show or set decimal digits used for non-integer display (default 4).",
        "N - integer from 2 to 100000 (decimal places for reals)",
        "  precision\n  precision 20\n  digits 8",
        "Integers print without padding. Exact fractions show Exact + Decimal."
    },
    {
        "angle", NULL, "session", 1,
        "angle radians|degrees",
        "Set trigonometric mode for sin/cos/tan (and friends).",
        "radians - default; arguments in radians\n"
        "degrees - sin(90) uses degree measure when mode is degrees",
        "  angle degrees\n  sin(90)\n  angle radians",
        "sind/cosd/tand always take degrees."
    },
    {
        "vars", "clear_vars", "session", 1,
        "vars\nclear_vars",
        "List or clear user-defined variables.",
        NULL,
        "  a = 1/3\n  vars\n  clear_vars",
        NULL
    },
    {
        "color", NULL, "session", 1,
        "color on|off",
        "Toggle ANSI color in the REPL.",
        NULL,
        "  color off\n  color on",
        "Also: --no-color, --color, NO_COLOR env."
    },
    {
        "advanced", "mode", "session", 1,
        "advanced on|off\nmode advanced|normal\nterminalc --advanced",
        "Show per-evaluation performance stats after each result.",
        NULL,
        "  advanced on\n  fact(20)\n  advanced off",
        "Reports:\n"
        "  time         - wall-clock duration of the evaluation\n"
        "  object       - full result value in memory (struct shell + content)\n"
        "  payload      - variable content only (integer/real limbs, tensor data)\n"
        "  output       - estimated size of the printed result\n"
        "  working set  - process memory on Windows (with delta)\n"
        "Small integers have a nearly fixed object size; payload grows with big ints."
    },
    {
        "quit", "exit :q", "session", 1,
        "quit | exit | :q",
        "Leave the interactive REPL.",
        NULL,
        "  quit",
        NULL
    },

    /* ---- booleans ---- */
    {
        "true", "false bool boolean", "boolean", 1,
        "true\nfalse",
        "Boolean literals.",
        NULL,
        "  true\n  false\n  true and false",
        "Also: == != < <= > >=  and or not  && || !"
    },
    {
        "if", NULL, "boolean", 1,
        "if(cond, then_val, else_val)",
        "Pick then_val if cond is truthy, else else_val.",
        "cond - any value (0/false = false; non-zero/true = true)\n"
        "then_val - result when cond is true\n"
        "else_val - result when cond is false",
        "  if(1 < 2, 10, 20)\n  if(false, 10, 0)",
        NULL
    },
    {
        "and", "or not && || !", "boolean", 1,
        "a and b\na or b\nnot a\n!a",
        "Boolean connectives (also && || !).",
        "a, b - any values; converted with truthiness",
        "  true and 1\n  0 or 5\n  not false",
        "Comparisons: == != < <= > >="
    },

    /* ---- bases ---- */
    {
        "hex", NULL, "bases", 1,
        "hex(n)",
        "Display integer n in hexadecimal (value still stored as n).",
        "n - integer (exact rational with denominator 1)",
        "  hex(255)     -> 0xFF\n  hex(0b1111)\n  -hex(16)",
        "Input literals: 0xFF  0b1010  0o77"
    },
    {
        "bin", NULL, "bases", 1,
        "bin(n)",
        "Display integer n in binary.",
        "n - integer",
        "  bin(10)      -> 0b1010",
        "See also: hex, oct, base"
    },
    {
        "oct", NULL, "bases", 1,
        "oct(n)",
        "Display integer n in octal.",
        "n - integer",
        "  oct(64)      -> 0o100",
        NULL
    },
    {
        "base", "tobase", "bases", 1,
        "base(n, b)\ntobase(n, b)",
        "Display integer n in radix b (2-36).",
        "n - integer to display\n"
        "b - radix, integer from 2 to 36",
        "  base(255, 16)\n  base(42, 36)",
        "Prefixes 0x/0b/0o used when b is 16/2/8."
    },

    /* ---- elementary ---- */
    {
        "sin", "cos tan", "trig", 1,
        "sin(x)  cos(x)  tan(x)",
        "Trigonometric functions (radians or degrees - see angle).",
        "x - angle (real or rational)",
        "  sin(pi/2)\n  angle degrees\n  sin(90)",
        "Exact values for common degree angles when possible."
    },
    {
        "sind", "cosd tand", "trig", 1,
        "sind(x)  cosd(x)  tand(x)",
        "Trigonometry with x always in degrees.",
        "x - angle in degrees",
        "  sind(30)\n  cosd(60)",
        NULL
    },
    {
        "inverse_trig", "asin acos atan atan2", "trig", 1,
        "asin(x)\nacos(x)\natan(x)\natan2(y, x)",
        "Inverse trigonometric functions (radians).",
        "x, y - real values; atan2 uses y first and x second",
        "  asin(1)\n  atan2(1, 1)",
        "Use sind/cosd/tand for degree-based forward trigonometry."
    },
    {
        "hyperbolic", "sinh cosh tanh asinh acosh atanh", "trig", 1,
        "sinh(x)\ncosh(x)\ntanh(x)\nasinh(x)\nacosh(x)\natanh(x)",
        "Hyperbolic and inverse-hyperbolic functions.",
        "x - real argument (domain restrictions apply)",
        "  tanh(0)\n  asinh(1)",
        NULL
    },
    {
        "sqrt", NULL, "roots_logs", 1,
        "sqrt(x)",
        "Principal square root.",
        "x - non-negative real (for real results)",
        "  sqrt(2)\n  sqrt(9)",
        NULL
    },
    {
        "nthroot", "nroot root", "roots_logs", 1,
        "nthroot(x, n) | nroot(x, n) | root(x, n)",
        "Real n-th root; tensors are evaluated element-wise.",
        "x - finite real scalar or tensor; n - positive integer index.\n"
        "Negative x requires an odd n.",
        "  nthroot(16, 4)\n  nthroot(-8, 3)\n  nthroot([1,8,27], 3)",
        NULL
    },
    {
        "ln", "log log10 exp", "roots_logs", 1,
        "ln(x)\nlog(x) | log10(x)\nexp(x)",
        "Natural log, base-10 log, exponential.",
        "x - positive for logs; any real for exp",
        "  ln(e)\n  log(1000)\n  exp(1)",
        "log and log10 are aliases (base 10)."
    },
    {
        "log_extra", "log2 log1p expm1 cbrt pow logb logn log_base erf erfc gamma lgamma", "roots_logs", 1,
        "log2(x)\nlog1p(x)\nexpm1(x)\ncbrt(x)\npow(x, y)\n"
        "logb(x, base) | logn(x, base) | log_base(x, base)\n"
        "erf(x)\nerfc(x)\ngamma(x)\nlgamma(x)",
        "Additional logarithm, base-log, special-function, cube-root, and power helpers.",
        "x, y - real values; pow(x,y) computes x raised to y\n"
        "base - positive base other than 1 for logb/logn/log_base",
        "  log2(8)\n  logb(8, 2)\n  logn(1000, 10)\n  erf(1)\n  gamma(5)\n  pow(2, 10)",
        NULL
    },
    {
        "ramanujan_pi", NULL, "roots_logs", 1,
        "ramanujan_pi()",
        "Compute pi with Ramanujan's rapidly convergent series.",
        "none - this function takes no arguments",
        "  ramanujan_pi()\n  precision 30\n  ramanujan_pi()",
        "Formula (Ramanujan, 1914):\n"
        "  1/pi = (2*sqrt(2)/9801) * sum(k=0..infinity,\n"
        "    (4k)!*(1103 + 26390*k) / ((k!)^4 * 396^(4k)))\n"
        "The implementation sums this series directly with MPFR until the\n"
        "active precision is reached; each term contributes about eight\n"
        "decimal digits. Named in tribute to Srinivasa Ramanujan\n"
        "(1887-1920), whose work transformed analysis and number theory."
    },
    {
        "abs", "floor ceil", "rounding", 1,
        "abs(x)\nfloor(x)\nceil(x)",
        "Absolute value; floor / ceiling.",
        "x - number (or bool for abs -> 0/1)",
        "  abs(-3)\n  floor(2.7)\n  ceil(2.1)",
        NULL
    },
    {
        "range", "min max clamp", "rounding", 1,
        "min(x1, ..., xn)\nmax(x1, ..., xn)\nclamp(x, lo, hi)",
        "Choose extrema or constrain a scalar to an inclusive interval.",
        "x values - scalar arguments\nlo, hi - lower and upper bounds for clamp",
        "  min(4, 2, 9)\n  max([1, 5, 3])\n  clamp(12, 0, 10)",
        NULL
    },
    {
        "statistics", "mean variance stddev norm median quantile percentile covariance cov correlation corr", "statistics", 1,
        "mean(values...)\nvariance(values...)\nstddev(values...)\nnorm(values...)\n"
        "median(values...)\nquantile(values, probability) | percentile(values, probability)\n"
        "covariance(x, y) | cov(x, y)\ncorrelation(x, y) | corr(x, y)",
        "Statistics over scalar arguments or tensor elements.",
        "values - one or more scalar values and/or tensors\n"
        "probability - real value from 0 to 1\nx, y - equal-length scalar/tensor samples",
        "  mean(1, 2, 3)\n  median([1,5,2])\n  quantile([1,2,3,4], 0.75)\n"
        "  covariance([1,2,3],[2,4,6])\n  correlation([1,2,3],[2,4,6])",
        "variance and covariance are population statistics; quantile uses linear interpolation."
    },
    {
        "ml", "sigmoid logistic softplus logit relu sign", "statistics", 1,
        "sigmoid(x)\nlogistic(x)\nsoftplus(x)\nlogit(x)\nrelu(x)\nsign(x)",
        "Common machine-learning activation, transform, and sign functions.",
        "x - scalar value",
        "  sigmoid(0)\n  softplus(0)\n  logit(1/2)\n  relu(-2)\n  sign(-7)",
        "Use these with assignments and .tc scripts for transparent model calculations."
    },
    {
        "fact", "factorial", "combinatorics", 1,
        "fact(n)\nfactorial(n)",
        "Factorial n! for non-negative integer n (maximum 100000).",
        "n - integer >= 0 (size-limited)",
        "  fact(5)\n  fact(12)",
        NULL
    },
    {
        "gcd", NULL, "combinatorics", 1,
        "gcd(a, b)",
        "Greatest common divisor of two integers.",
        "a - integer\nb - integer",
        "  gcd(12, 18)\n  gcd(-15, 25)",
        NULL
    },
    {
        "lcm", NULL, "combinatorics", 1,
        "lcm(a, b)",
        "Least common multiple of two integers.",
        "a - integer\nb - integer",
        "  lcm(3, 6)\n  lcm(12, 18)",
        NULL
    },
    {
        "ncr", "choose", "combinatorics", 1,
        "ncr(n, k)\nchoose(n, k)",
        "Binomial coefficient C(n, k), with n at most 100000.",
        "n - non-negative integer\nk - non-negative integer",
        "  ncr(10, 3)\n  choose(5, 2)",
        NULL
    },
    {
        "fib", "fibn", "sequences", 1,
        "fib(n)\nfibn(n)",
        "Fibonacci: list first n terms, or nth term (fast).",
        "n - non-negative integer (term count / index)",
        "  fib(10)\n  fibn(20)",
        "REPL commands (not general expressions). Limits: fib 10000 terms; fibn index 10000000."
    },

    /* ---- algebra / plot (existing) ---- */
    {
        "solve_quadratic", "quadratic", "algebra", 1,
        "solve_quadratic(a, b, c)\nquadratic(a, b, c)",
        "Solve a*x^2 + b*x + c = 0 (stable formula).",
        "a - quadratic coefficient (!= 0 for true quadratic)\n"
        "b - linear coefficient\n"
        "c - constant term",
        "  solve_quadratic(1, -3, 2)",
        NULL
    },
    {
        "poly", "degree evaluate derivative roots", "algebra", 1,
        "p = poly([a_n, ..., a_0])\ndegree(p)\nevaluate(p, x)\nderivative(p)\nroots(p)",
        "Exact-coefficient polynomials (highest degree first).",
        "coeffs - list of rationals, highest power first\n"
        "p - stored polynomial name\n"
        "x - evaluation point",
        "  p = poly([1, -3, 2])\n  evaluate(p, 5)\n  derivative(p)\n  roots(p)",
        "roots: linear/quadratic only for now."
    },
    {
        "plot", NULL, "plot", 1,
        "plot(expr, x=lo..hi)",
        "Plot y = expr for free variable x (native window on Windows/macOS).",
        "expr - expression in x (also y=... form)\n"
        "lo - range start (expression)\n"
        "hi - range end (must be > lo)",
        "  plot(sin(x), x=-pi..pi)\n  plot(y=x^2, x=-2..2)",
        "Hover curve for (x,y). Wheel zoom, drag pan, Esc close.\n"
        "The x=lo..hi interval is sampled exactly; press 0 to reset after pan/zoom.\n"
        "The y view has a small auto-fit margin. TERMINALC_ASCII_PLOT=1 forces terminal ASCII."
    },

    /* ---- constants ---- */
    {
        "pi", "e phi sqrt2 ln2 ans", "constants", 1,
        "pi  e  phi  sqrt2  ln2  ans  _  x",
        "Built-in constants and special names.",
        NULL,
        "  pi\n  e\n  ans",
        "x is the free plot/solve variable. ans/_ last result."
    },

    /* ---- matrices and tensors ---- */
    {
        "matrix", "mat zeros ones eye", "matrix", 1,
        "v = [1, 2, 3]\nA = [1, 2; 3, 4]\nmat(rows, cols, ...)\nzeros(...)\neye(n)",
        "Vectors and matrices (1D-4D tensors).",
        "rows, cols - dimensions\n"
        "data - row-major entries\n"
        "shape args - for zeros/ones/tensor up to 4D",
        "  A = [1, 2; 3, 4]\n  eye(3)\n  zeros(2, 2, 2)",
        "Operations: * (matrix multiplication), scalar-broadcast + - * /,\n"
        "det, inv, linsolve, transpose, dot, and shape. Tensor modulo and\n"
        "indexing are not supported."
    },
    {
        "det", "inv transpose linsolve dot", "matrix", 1,
        "det(A)  inv(A)  transpose(A)  linsolve(A, b)  dot(u, v)",
        "Linear-algebra helpers.",
        "A - square matrix (det/inv)\n"
        "b - right-hand side vector/matrix\n"
        "u, v - vectors",
        "  det([1,2;3,4])\n  linsolve(A, b)",
        "See: help matrix"
    },

    /* ---- solvers / calculus ---- */
    {
        "solve", NULL, "solver", 1,
        "solve(expr, guess)\nsolve(expr, lo, hi)",
        "Numeric root finder for free variable x.",
        "expr - expression in x (e.g. x^2-2)\n"
        "guess - Newton start\n"
        "lo, hi - bracket for Brent (sign change)",
        "  solve(x^2 - 2, 1)\n  solve(x^2 - 2, 0, 2)",
        "See also: solve_quadratic (available), linsolve (matrix topic)."
    },
    {
        "diff", "derivative differential", "calculus", 1,
        "diff(expr, x0)",
        "Numeric derivative of expr(x) at x0.",
        "expr - expression in free variable x\n"
        "x0 - point of evaluation",
        "  diff(sin(x), 0)\n  diff(x^3, 2)",
        "Central differences; uses current precision."
    },
    {
        "integral", "integrate", "calculus", 1,
        "integral(expr, a, b)",
        "Numeric definite integral of expr(x) from a to b.",
        "expr - integrand in free variable x\n"
        "a - lower limit\n"
        "b - upper limit",
        "  integral(sin(x), 0, pi)\n  integral(x^2, 0, 1)",
        NULL
    },

    /* ---- units ---- */
    {
        "units", "to mag mps kmh", "units", 1,
        "5 * m\n"
        "10 * m / s\n"
        "to(qty, unit)\n"
        "mag(qty)",
        "Dimensioned quantities, speed (m/s), and temperature.",
        "qty - measure (number * unit)\n"
        "unit - target unit (m, km, mps, kmh, celsius, ...)",
        "  to(5 * km, m)\n"
        "  10 * m / s\n"
        "  to(10 * m / s, kmps)\n"
        "  to(36 * kmh, mps)\n"
        "  to(100 * celsius, fahrenheit)",
        "Speed: mps (m/s), kmps (km/s), kmh (km/h), mph, fps\n"
        "Also: (v)*m/s via length/time division\n"
        "Temp: celsius/c/degc, fahrenheit/f/degf, kelvin/k"
    },
    {
        "m", "km cm mm kg g mg s mps kmh mph", "units", 1,
        "number * unit_name",
        "SI and common units as named constants.",
        "Multiply a number by a unit name to build a measure.",
        "  3 * m\n"
        "  10 * m / s\n"
        "  60 * mph\n"
        "  to(1 * km, m)",
        "help units for the full catalog."
    },

    /* ---- mensuration modules ---- */
    {
        "square", NULL, "mensuration", 1,
        "square.area(side)\nsquare.perimeter(side)",
        "Square geometry helpers.",
        "side - side length (number or measure)",
        "  square.area(4)\n  square.perimeter(4)\n  square.area(4 * m)",
        "Pattern: module.method(args). See help mensuration."
    },
    {
        "rectangle", NULL, "mensuration", 1,
        "rectangle.area(length, width)\nrectangle.perimeter(length, width)",
        "Rectangle helpers.",
        "length - first side\nwidth - second side",
        "  rectangle.area(3, 5)",
        NULL
    },
    {
        "circle", NULL, "mensuration", 1,
        "circle.area(r)\ncircle.circumference(r)\ncircle.diameter(r)",
        "Circle helpers.",
        "r - radius",
        "  circle.area(1)\n  circle.circumference(2)",
        NULL
    },
    {
        "triangle", NULL, "mensuration", 1,
        "triangle.area(base, height)\ntriangle.area_heron(a, b, c)",
        "Triangle helpers.",
        "base, height - for 1/2*b*h\n"
        "a, b, c - side lengths for Heron",
        "  triangle.area(6, 4)\n  triangle.area_heron(3, 4, 5)",
        NULL
    },
    {
        "cube", "cuboid sphere cylinder cone", "mensuration", 1,
        "cube.volume(a)  cube.surface(a)\nsphere.volume(r)  sphere.surface(r)\n...",
        "3D solid helpers.",
        "a - cube edge\nr - radius\nh - height where needed",
        "  cube.volume(2)\n  sphere.volume(1)\n  cylinder.volume(r, h)",
        "See help mensuration."
    },
    {
        "ap", "ap_term ap_sum ap_list ap_nth", "series", 1,
        "ap_term(a, d, n)\nap_sum(a, d, n)\nap_list(a, d, n)",
        "Arithmetic progression (AP): a, a+d, a+2d, ...",
        "a - first term\n"
        "d - common difference\n"
        "n - term index / count (n >= 1, 1-based)",
        "  ap_term(2, 3, 5)     # 2+3*4 = 14\n"
        "  ap_sum(2, 3, 5)      # sum of 5 terms\n"
        "  ap_list(2, 3, 5)     # [2,5,8,11,14]",
        "Alias: ap_nth = ap_term"
    },
    {
        "gp", "gp_term gp_sum gp_list gp_nth", "series", 1,
        "gp_term(a, r, n)\ngp_sum(a, r, n)\ngp_list(a, r, n)",
        "Geometric progression (GP): a, ar, ar^2, ...",
        "a - first term\n"
        "r - common ratio\n"
        "n - term index / count (n >= 1)",
        "  gp_term(3, 2, 4)     # 3*2^3 = 24\n"
        "  gp_sum(3, 2, 4)\n"
        "  gp_list(3, 2, 4)",
        "Alias: gp_nth = gp_term"
    },
    {
        "hp", "hp_term hp_sum hp_list", "series", 1,
        "hp_term(a, d, n)\nhp_sum(a, d, n)\nhp_list(a, d, n)",
        "Harmonic progression (HP): reciprocals of an AP.",
        "a, d - underlying AP first term and difference\n"
        "n - term count / index",
        "  hp_term(2, 3, 1)     # 1/2\n"
        "  hp_list(2, 3, 3)",
        NULL
    },
    {
        "sumi", "sp_sum sumsq sumcu sp_sumsq sp_sumcu sumrange", "series", 1,
        "sumi(n) | sp_sum(n)\nsumsq(n)\nsumcu(n)\nsumrange(lo, hi)",
        "School summation formulas (SP-style natural number series).",
        "n - non-negative integer\n"
        "lo, hi - inclusive integer bounds for sumrange",
        "  sumi(100)           # 1+..+100 = 5050\n"
        "  sumsq(10)           # 1^2+..+10^2\n"
        "  sumcu(5)            # 1^3+..+5^3\n"
        "  sumrange(1, 100)",
        "sp_sum is an alias of sumi."
    },
    {
        "sum", "sigma", "series", 1,
        "sum(expr, lo, hi)\nsigma(expr, lo, hi)",
        "Sum expr over integer x from lo to hi (inclusive).",
        "expr - expression in free variable x\n"
        "lo - start (integer)\n"
        "hi - end (integer)",
        "  sum(x, 1, 10)\n"
        "  sum(x^2, 1, 5)\n"
        "  sigma(2*x+1, 0, 4)",
        "Also: sumrange(lo,hi) for plain integer sums."
    },
    {
        "primes", "isprime nextprime prevprime primes_to primes_upto", "series", 1,
        "isprime(n)\nprimes(n)\nprimes_to(limit)\nnextprime(n)\nprevprime(n)",
        "Prime tests and prime lists.",
        "n - count or integer argument\n"
        "limit - upper bound (primes_to: all primes <= limit)",
        "  isprime(17)\n"
        "  primes(10)            # first 10 primes as vector\n"
        "  primes_to(30)         # primes <= 30\n"
        "  nextprime(14)\n"
        "  prevprime(14)",
        "Limits: primes n<=100000, primes_to limit<=2000000."
    },
    {
        "mensuration", "geometry", "mensuration", 1,
        "module.method(args)",
        "Geometry library (square, circle, sphere, ...).",
        "module - shape name (square, circle, cube, ...)\n"
        "method - area, perimeter, volume, surface, ...\n"
        "args - lengths (numbers; compatible measures where supported)",
        "  square.area(4)\n  circle.area(1)\n  help square",
        "Each shape has its own help topic."
    },
};

static void lower_copy(char *dst, size_t dstsz, const char *src)
{
    size_t i = 0;
    for (; src[i] && i + 1 < dstsz; i++)
        dst[i] = (char)tolower((unsigned char)src[i]);
    dst[i] = '\0';
}

static bool key_match(const TerminalCHelpEntry *e, const char *topic)
{
    if (strcmp(e->key, topic) == 0)
        return true;
    if (!e->aliases)
        return false;
    /* aliases: space-separated tokens */
    const char *p = e->aliases;
    while (*p) {
        while (*p == ' ')
            p++;
        if (!*p)
            break;
        const char *start = p;
        while (*p && *p != ' ')
            p++;
        size_t n = (size_t)(p - start);
        if (n == strlen(topic) && strncmp(start, topic, n) == 0)
            return true;
    }
    return false;
}

static const TerminalCHelpEntry *find_help(const char *topic)
{
    for (size_t i = 0; i < sizeof g_help / sizeof g_help[0]; i++) {
        if (key_match(&g_help[i], topic))
            return &g_help[i];
    }
    return NULL;
}

typedef struct {
    const char *category;
    const char *signatures;
} TerminalCFunctionGroup;

/* This is the user-facing callable surface. Keep it synchronized with the
 * dispatchers in functions.c, domains.c, series.c, algebra.c, and plot.c. */
static const TerminalCFunctionGroup g_function_groups[] = {
    {"Core and logic",
     "if(cond, then_value, else_value)\n"
     "abs(x)\n"
     "floor(x)\n"
     "ceil(x)\n"
     "min(x1, ..., xn)\n"
     "max(x1, ..., xn)\n"
     "clamp(x, lo, hi)\n"
     "true | false (literals)\n"
     "and | or | not | ! | && | || (operators)"},
    {"Trigonometry and logarithms",
     "sin(x)\ncos(x)\ntan(x)\n"
     "sind(x)\ncosd(x)\ntand(x)\n"
     "asin(x)\nacos(x)\natan(x)\natan2(y, x)\n"
     "sinh(x)\ncosh(x)\ntanh(x)\nasinh(x)\nacosh(x)\natanh(x)\n"
     "hypot(x, y)\n"
     "sqrt(x)\nnthroot(x, n) | nroot(x, n) | root(x, n)\n"
     "ln(x)\nlog(x) | log10(x)\nlogb(x, base) | logn(x, base)\nexp(x)\n"
     "erf(x)\nerfc(x)\ngamma(x)\nlgamma(x)\nramanujan_pi()"},
    {"Number theory and combinatorics",
     "gcd(a, b)\nlcm(a, b)\nfact(n) | factorial(n)\nncr(n, k) | choose(n, k)\n"
     "isprime(n)\nnextprime(n)\nprevprime(n)"},
    {"Bases and integer display",
     "hex(n)\nbin(n)\noct(n)\nbase(n, radix) | tobase(n, radix)"},
    {"Sequences and summation",
     "fib(n) [REPL command]\nfibn(n) [REPL command]\n"
     "ap_term(a, d, n) | ap_nth(a, d, n)\nap_sum(a, d, n)\nap_list(a, d, n)\n"
     "gp_term(a, r, n) | gp_nth(a, r, n)\ngp_sum(a, r, n)\ngp_list(a, r, n)\n"
     "hp_term(a, d, n) | hp_nth(a, d, n)\nhp_sum(a, d, n)\nhp_list(a, d, n)\n"
     "sumi(n) | sp_sum(n)\nsumsq(n) | sp_sumsq(n)\nsumcu(n) | sp_sumcu(n)\n"
     "sumrange(lo, hi)\nsum(expr, lo, hi) | sigma(expr, lo, hi)\n"
     "primes(count)\nprimes_to(limit) | primes_upto(limit)"},
    {"Matrices and tensors",
     "zeros(d1, ..., d4)\nones(d1, ..., d4)\neye(n)\n"
     "mat(rows, cols, values...)\nshape(tensor)\ntranspose(A)\n"
     "det(A)\ninv(A)\nlinsolve(A, b)\ndot(u, v)\n"
     "tensor + - * / scalar broadcast"},
    {"Numerical solvers and calculus",
     "solve(expr, guess) | solve(expr, lo, hi)\n"
     "diff(expr, x0)\nintegral(expr, lo, hi)"},
    {"Statistics and machine learning",
     "mean(values...)\nvariance(values...)\nstddev(values...)\nnorm(values...)\n"
     "median(values...)\nquantile(values, probability) | percentile(values, probability)\n"
     "covariance(x, y) | cov(x, y)\ncorrelation(x, y) | corr(x, y)\n"
     "sigmoid(x) | logistic(x)\nsoftplus(x)\nlogit(x)\nrelu(x)\nsign(x)\n"
     "log1p(x)\nexpm1(x)\nlog2(x)\ncbrt(x)\npow(x, y)"},
    {"Units and measurements",
     "to(quantity, unit)\nmag(quantity)\nnumber * unit_name\n"
     "length/time -> speed conversion"},
    {"Geometry and mensuration",
     "square.area(side)\nsquare.perimeter(side)\n"
     "rectangle.area(length, width)\nrectangle.perimeter(length, width)\n"
     "circle.area(radius)\ncircle.circumference(radius)\ncircle.diameter(radius)\n"
     "triangle.area(base, height)\ntriangle.area_heron(a, b, c)\n"
     "cube.volume(edge)\ncube.surface(edge)\n"
     "cuboid.volume(l, w, h)\ncuboid.surface(l, w, h)\n"
     "sphere.volume(radius)\nsphere.surface(radius)\n"
     "cylinder.volume(radius, height)\ncone.volume(radius, height)"},
    {"Algebra and plotting",
     "solve_quadratic(a, b, c) | quadratic(a, b, c) [REPL/script]\n"
     "poly([a_n, ..., a_0]) [REPL/script]\n"
     "degree(p) | evaluate(p, x) | derivative(p) | roots(p) [REPL/script]\n"
     "plot(expr, x=lo..hi) [REPL/script]"}
};

static void print_lines(const char *text, const char *prefix)
{
    const char *p = text;
    while (p && *p) {
        const char *nl = strchr(p, '\n');
        size_t n = nl ? (size_t)(nl - p) : strlen(p);
        printf("%s%.*s%s\n", prefix, (int)n, p, C_RST);
        if (!nl)
            break;
        p = nl + 1;
    }
}

static void print_function_catalog(void)
{
    printf("%sTerminalC function catalog%s\n", C_B_CYN, C_RST);
    printf("Use %shelp <name>%s for arguments, examples, and limits.\n\n",
           C_B_YEL, C_RST);
    for (size_t i = 0; i < sizeof g_function_groups / sizeof g_function_groups[0]; i++) {
        printf("%s[%s]%s\n", C_B_YEL, g_function_groups[i].category, C_RST);
        print_lines(g_function_groups[i].signatures, "  ");
        putchar('\n');
    }
    printf("%sConstants%s: pi, e, phi, sqrt2, ln2, ans, _, x\n", C_B_YEL, C_RST);
    printf("%sUnit names%s: use %shelp units%s for the complete list.\n",
           C_B_YEL, C_RST, C_B_CYN, C_RST);
}

static void print_unit_catalog(void)
{
    printf("\n%sUnit names%s\n", C_B_CYN, C_RST);
    printf("  Length: m, meter, metre, km, cm, mm, um, nm, ft, in, yd, mile\n");
    printf("  Mass: kg, g, mg, ug, tonne, t, lb, oz\n");
    printf("  Time: s, sec, ms, us, min, hour, hr, day\n");
    printf("  Area: m2, km2, hectare, ha, acre, ft2\n");
    printf("  Volume: m3, l, liter, ml\n");
    printf("  Temperature: celsius, c, degc, fahrenheit, f, degf, kelvin, k\n");
    printf("  Speed: mps, m_s, kmps, km_s, kmh, kmph, km_h, mph, fps, ft_s\n");
    printf("  Prefixes: kilo, mega, milli, micro, nano, centi, hecto\n");
}

static const char *display_summary(const TerminalCHelpEntry *e)
{
    if (strcmp(e->key, "matrix") == 0)
        return "Vectors and matrices (1D-4D tensors).";
    if (strcmp(e->key, "det") == 0)
        return "Linear-algebra helpers.";
    if (strcmp(e->key, "solve") == 0)
        return "Numeric root finder for free variable x.";
    if (strcmp(e->key, "diff") == 0)
        return "Numeric derivative of expr(x) at x0.";
    if (strcmp(e->key, "integral") == 0)
        return "Numeric definite integral of expr(x) from a to b.";
    if (strcmp(e->key, "square") == 0)
        return "Square geometry helpers.";
    if (strcmp(e->key, "rectangle") == 0)
        return "Rectangle helpers.";
    if (strcmp(e->key, "circle") == 0)
        return "Circle helpers.";
    if (strcmp(e->key, "triangle") == 0)
        return "Triangle helpers.";
    if (strcmp(e->key, "cube") == 0)
        return "3D solid helpers.";
    if (strcmp(e->key, "mensuration") == 0)
        return "Geometry library (square, circle, sphere, and 3D solids).";
    return e->summary;
}

static const char *display_notes(const TerminalCHelpEntry *e)
{
    if (strcmp(e->key, "matrix") == 0)
        return "Operations: matrix multiplication, transpose, det, inv, linsolve, dot, and shape.";
    if (strcmp(e->key, "solve") == 0)
        return "MPFR-backed iteration (bounded to 80 work digits); bracketed calls use bisection and one-guess calls use Newton. See help diff and help integral.";
    if (strcmp(e->key, "diff") == 0)
        return "MPFR five-point central differences with a precision-aware step; work is bounded to 80 digits.";
    if (strcmp(e->key, "integral") == 0)
        return "MPFR tanh-sinh quadrature avoids endpoint sampling; evaluations and work precision are bounded.";
    return e->notes;
}

static void print_entry(const TerminalCHelpEntry *e)
{
    printf("%s", C_B_CYN);
    fputs(e->signature, stdout);
    printf("%s\n", C_RST);

    if (!e->available)
        printf("%s[planned - not implemented yet]%s\n", C_B_YEL, C_RST);

    printf("%s\n", display_summary(e));

    if (e->args && e->args[0]) {
        printf("\n%sArguments%s\n", C_B_CYN, C_RST);
        /* print each line with a bullet */
        const char *p = e->args;
        while (*p) {
            const char *nl = strchr(p, '\n');
            size_t n = nl ? (size_t)(nl - p) : strlen(p);
            printf("  %s-%s %.*s\n", C_B_GRN, C_RST, (int)n, p);
            if (!nl)
                break;
            p = nl + 1;
        }
    }

    if (e->examples && e->examples[0]) {
        printf("\n%sExamples%s\n", C_B_CYN, C_RST);
        const char *p = e->examples;
        while (*p) {
            const char *nl = strchr(p, '\n');
            size_t n = nl ? (size_t)(nl - p) : strlen(p);
            printf("  %s%.*s%s\n", C_DIM, (int)n, p, C_RST);
            if (!nl)
                break;
            p = nl + 1;
        }
    }

    const char *notes = display_notes(e);
    if (notes && notes[0]) {
        printf("\n%sNotes%s\n  %s\n", C_B_CYN, C_RST, notes);
    }

    if (e->aliases && e->aliases[0])
        printf("\n%sAliases:%s %s\n", C_DIM, C_RST, e->aliases);
    printf("%sCategory:%s %s\n", C_DIM, C_RST, e->category);
}

static void print_topic_index(void)
{
    printf("%sHelp topics%s (use: help <name>)\n\n", C_B_CYN, C_RST);

    const char *last_cat = "";
    for (size_t i = 0; i < sizeof g_help / sizeof g_help[0]; i++) {
        const TerminalCHelpEntry *e = &g_help[i];
        if (strcmp(e->category, last_cat) != 0) {
            last_cat = e->category;
            printf("%s[%s]%s\n", C_B_YEL, last_cat, C_RST);
        }
        printf("  %s%-18s%s %s\n", C_B_GRN, e->key, C_RST,
               display_summary(e));
    }
    printf("\n%sTip:%s help hex   |   help square   |   help diff   |   help topics\n",
           C_DIM, C_RST);
}

bool terminalc_help_topic(const char *topic_raw)
{
    if (!topic_raw || !topic_raw[0])
        return false;

    char topic[64];
    lower_copy(topic, sizeof topic, topic_raw);

    if (strcmp(topic, "functions") == 0 || strcmp(topic, "function") == 0 ||
        strcmp(topic, "catalog") == 0 || strcmp(topic, "list") == 0 ||
        strcmp(topic, "available") == 0) {
        print_function_catalog();
        return true;
    }

    if (strcmp(topic, "all") == 0) {
        print_topic_index();
        putchar('\n');
        print_function_catalog();
        return true;
    }

    if (strcmp(topic, "topics") == 0 || strcmp(topic, "index") == 0) {
        print_topic_index();
        return true;
    }

    const TerminalCHelpEntry *e = find_help(topic);
    if (!e) {
        fprintf(stderr, "%sUnknown help topic '%s'.%s Try: %shelp topics%s\n",
                C_B_RED, topic, C_RST, C_B_CYN, C_RST);
        /* suggest prefix matches */
        int shown = 0;
        for (size_t i = 0; i < sizeof g_help / sizeof g_help[0]; i++) {
            if (strncmp(g_help[i].key, topic, strlen(topic)) == 0) {
                if (shown == 0)
                    fprintf(stderr, "%sDid you mean:%s\n", C_DIM, C_RST);
                fprintf(stderr, "  %s\n", g_help[i].key);
                if (++shown >= 8)
                    break;
            }
        }
        return false;
    }

    print_entry(e);
    if (strcmp(e->key, "units") == 0 || strcmp(e->key, "m") == 0)
        print_unit_catalog();
    return true;
}
