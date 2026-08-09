# TerminalC tests

## Philosophy

- **Catalog-first:** most regressions are pure `--eval` cases in `eval_catalog.tsv`.
- **Hundreds of cases** are cheap to maintain as TSV rows, not as hand-written CMake.
- **REPL fixtures** cover interactive-only features (`help`, `precision`, `poly`, …).
- Every new feature should add rows (or a fixture) **in the same PR**.

## Layout

| File | Role |
|------|------|
| `eval_catalog.tsv` | Bulk eval cases (name, expr, regex, options) |
| `register_catalog.cmake` | Loads TSV → one CTest per row |
| `run_eval_case.cmake` | Runs a single eval case |
| `run_repl_expect.cmake` | REPL stdin fixture + substring expect |
| `run_plot_export.cmake` | SVG export smoke test |
| `*.input` | REPL session scripts |
| `README.md` | This file |

## Catalog format (`eval_catalog.tsv`)

TAB-separated columns:

```text
name<TAB>expr<TAB>expect_regex<TAB>options
```

- `#` comments and blank lines ignored  
- **options** (comma-separated, optional):
  - `p=50` — `--precision 50`
  - `fail` — expect exit code 1
  - `err` — match regex against **stderr** (for parse/eval errors)

Examples:

```text
add_2_3	2+3	^5$
sqrt2_hi	sqrt(2)	1\.41421356237309504880	p=50
err_div0	1/0	division by zero	fail,err
```

## Running

```sh
cmake -S . -B build -G "MinGW Makefiles"
cmake --build build
ctest --test-dir build --output-on-failure
# filter catalog only:
ctest --test-dir build -R '^cat_' --output-on-failure
```

## Gated validation harness

`validate.py` runs the full exhaustive CTest suite, deterministic numerical
accuracy checks at several precisions, bounded large-computation stress cases,
complex FFT/matrix/calculus/unit scenarios, and the sanitizer suite when a
sanitizer build is available. It writes per-stage logs plus `summary.json` and
`summary.md` into a timestamped local directory:

```sh
python3 tests/validate.py --build-dir build --sanitizer-dir build-sanitize
```

Use `--build` to rebuild existing build trees first. A missing sanitizer build
is a validation failure unless `--skip-sanitizers` is explicitly supplied; a
skipped run is reported as partial rather than good-to-go.

Validation reports are local artifacts and are ignored by Git. To archive one
privately, compress then encrypt it with an approved `age` recipient. The
recipient is supplied at runtime and no decryption key is stored in the repo:

```sh
python3 tests/archive_validation.py validation-results/<timestamp> \
  --recipient "$TERMINALC_VALIDATION_AGE_RECIPIENT"
```

Do not commit plaintext reports or private keys. Encrypted archives should
only be added deliberately to a private repository or private storage after
the recipient has been verified.

## Adding tests (checklist)

1. Prefer a **catalog row** for anything expressible as `--eval`.
2. Use a **REPL `.input`** when the feature is command-only (`help`, `angle`, assignments + multi-step).
3. Name cases `area_behavior_detail` (stable CTest names: `cat_<name>`).
4. Anchors: `^3$` for exact integer lines; unanchored substring for multi-line Exact/Decimal.
5. After changing display rules, run full `ctest` and fix catalog regexes intentionally.

## Coverage goals

| Area | Catalog | REPL |
|------|---------|------|
| Arithmetic / precedence | yes | |
| Booleans / compare / if | yes | session |
| Bases / hex / bin / oct | yes | |
| Combinatorics | yes | |
| Trig / logs / roots | yes | |
| Display (int vs fraction) | yes | |
| Errors (div0, unknown) | yes (`fail,err`) | |
| Help topics | | fixtures |
| Plot / poly / quadratic | | fixtures |
| Matrices / units / … | add when implemented | |

## Regenerating bulk arithmetic tables

Optional: re-run a generator script or extend the TSV by hand.  
Do **not** delete curated edge cases when regenerating grids.
