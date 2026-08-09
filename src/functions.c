#include "parse_internal.h"

#include <stdlib.h>
#include <string.h>

enum { TERMINALC_COMBINATORIC_MAX_N = 100000 };
enum { TERMINALC_STATS_MAX_VALUES = 100000 };
enum { TERMINALC_NTHROOT_MAX_INDEX = 1000000 };
enum { TERMINALC_RAMANUJAN_PI_MAX_TERMS = 1000000 };
enum { TERMINALC_RAMANUJAN_PI_GUARD_BITS = 64 };

static bool get_dimension(const TerminalCValue *v, size_t *out)
{
    if (!terminalc_is_integer(v) || mpz_sgn(mpq_numref(v->q)) < 0 ||
        !mpz_fits_ulong_p(mpq_numref(v->q)))
        return false;
    *out = (size_t)mpz_get_ui(mpq_numref(v->q));
    return true;
}

static int scalar_cmp(TerminalCCalc *c, const TerminalCValue *a,
                      const TerminalCValue *b)
{
    mpfr_t x, y;
    mpfr_inits2(c->bits, x, y, (mpfr_ptr)0);
    terminalc_as_real(c, x, a);
    terminalc_as_real(c, y, b);
    int result = mpfr_cmp(x, y);
    mpfr_clears(x, y, (mpfr_ptr)0);
    return result;
}

static bool select_extreme(TerminalCCalc *c, TerminalCValue *args,
                           size_t count, bool want_max, TerminalCValue *out)
{
    bool found = false;
    for (size_t i = 0; i < count; i++) {
        size_t cells = (args[i].type == TERMINALC_TENSOR && args[i].ten)
                           ? args[i].ten->n
                           : 1;
        for (size_t j = 0; j < cells; j++) {
            TerminalCValue *candidate = args[i].type == TERMINALC_TENSOR
                                            ? &args[i].ten->data[j]
                                            : &args[i];
            if (!terminalc_is_scalar(candidate))
                return false;
            if (!found) {
                terminalc_value_copy(out, candidate);
                found = true;
            } else {
                int cmp = scalar_cmp(c, candidate, out);
                if ((want_max && cmp > 0) || (!want_max && cmp < 0))
                    terminalc_value_copy(out, candidate);
            }
        }
    }
    return found;
}

static bool aggregate_values(TerminalCCalc *c, TerminalCValue *args,
                             size_t count, mpfr_t sum, mpfr_t sumsq,
                             size_t *value_count)
{
    *value_count = 0;
    mpfr_set_zero(sum, 0);
    mpfr_set_zero(sumsq, 0);
    for (size_t i = 0; i < count; i++) {
        if (args[i].type == TERMINALC_TENSOR && args[i].ten) {
            for (size_t j = 0; j < args[i].ten->n; j++) {
                TerminalCValue *cell = &args[i].ten->data[j];
                if (!terminalc_is_scalar(cell))
                    return false;
                mpfr_t x, x2;
                mpfr_inits2(c->bits, x, x2, (mpfr_ptr)0);
                terminalc_as_real(c, x, cell);
                mpfr_add(sum, sum, x, MPFR_RNDN);
                mpfr_mul(x2, x, x, MPFR_RNDN);
                mpfr_add(sumsq, sumsq, x2, MPFR_RNDN);
                mpfr_clears(x, x2, (mpfr_ptr)0);
                (*value_count)++;
            }
        } else if (terminalc_is_scalar(&args[i])) {
            mpfr_t x, x2;
            mpfr_inits2(c->bits, x, x2, (mpfr_ptr)0);
            terminalc_as_real(c, x, &args[i]);
            mpfr_add(sum, sum, x, MPFR_RNDN);
            mpfr_mul(x2, x, x, MPFR_RNDN);
            mpfr_add(sumsq, sumsq, x2, MPFR_RNDN);
            mpfr_clears(x, x2, (mpfr_ptr)0);
            (*value_count)++;
        } else {
            return false;
        }
    }
    return *value_count > 0;
}

static bool set_aggregate_result(TerminalCCalc *c, const char *name,
                                 TerminalCValue *args, size_t count,
                                 TerminalCValue *out)
{
    mpfr_t sum, sumsq, mean, variance;
    mpfr_inits2(c->bits, sum, sumsq, mean, variance, (mpfr_ptr)0);
    size_t n = 0;
    if (!aggregate_values(c, args, count, sum, sumsq, &n)) {
        mpfr_clears(sum, sumsq, mean, variance, (mpfr_ptr)0);
        terminalc_fail(c, "statistics functions need scalar values or tensors");
        return false;
    }
    mpfr_div_ui(mean, sum, (unsigned long)n, MPFR_RNDN);
    if (strcmp(name, "norm") == 0) {
        out->type = TERMINALC_REAL;
        mpfr_sqrt(out->r, sumsq, MPFR_RNDN);
    } else if (strcmp(name, "mean") == 0) {
        out->type = TERMINALC_REAL;
        mpfr_set(out->r, mean, MPFR_RNDN);
    } else {
        mpfr_div_ui(variance, sumsq, (unsigned long)n, MPFR_RNDN);
        mpfr_mul(sumsq, mean, mean, MPFR_RNDN);
        mpfr_sub(variance, variance, sumsq, MPFR_RNDN);
        if (mpfr_sgn(variance) < 0)
            mpfr_set_zero(variance, 0);
        out->type = TERMINALC_REAL;
        if (strcmp(name, "variance") == 0)
            mpfr_set(out->r, variance, MPFR_RNDN);
        else if (strcmp(name, "stddev") == 0)
            mpfr_sqrt(out->r, variance, MPFR_RNDN);
        else
            mpfr_set(out->r, variance, MPFR_RNDN);
    }
    mpfr_clears(sum, sumsq, mean, variance, (mpfr_ptr)0);
    return true;
}

static bool count_value_items(TerminalCCalc *c, TerminalCValue *args,
                              size_t count, size_t *out)
{
    size_t total = 0;
    for (size_t i = 0; i < count; i++) {
        size_t add = 0;
        if (args[i].type == TERMINALC_TENSOR && args[i].ten)
            add = args[i].ten->n;
        else if (terminalc_is_scalar(&args[i]))
            add = 1;
        else {
            terminalc_fail(c, "statistics functions need scalar values or tensors");
            return false;
        }
        if (add > TERMINALC_STATS_MAX_VALUES - total) {
            terminalc_fail(c, "statistics input is too large");
            return false;
        }
        total += add;
    }
    if (total == 0) {
        terminalc_fail(c, "statistics input cannot be empty");
        return false;
    }
    *out = total;
    return true;
}

static bool collect_value_items(TerminalCCalc *c, TerminalCValue *args,
                                size_t count, mpfr_t **out, size_t *n_out)
{
    size_t n = 0;
    if (!count_value_items(c, args, count, &n))
        return false;
    mpfr_t *values = (mpfr_t *)calloc(n, sizeof *values);
    if (!values) {
        terminalc_fail(c, "statistics input allocation failed");
        return false;
    }
    size_t at = 0;
    for (size_t i = 0; i < count; i++) {
        size_t cells = (args[i].type == TERMINALC_TENSOR && args[i].ten)
                           ? args[i].ten->n
                           : 1;
        for (size_t j = 0; j < cells; j++) {
            const TerminalCValue *v = (args[i].type == TERMINALC_TENSOR)
                                          ? &args[i].ten->data[j]
                                          : &args[i];
            mpfr_init2(values[at], c->bits);
            terminalc_as_real(c, values[at], v);
            if (mpfr_nan_p(values[at]) || mpfr_inf_p(values[at])) {
                for (size_t k = 0; k <= at; k++)
                    mpfr_clear(values[k]);
                free(values);
                terminalc_fail(c, "statistics input must be finite");
                return false;
            }
            at++;
        }
    }
    *out = values;
    *n_out = n;
    return true;
}

static void free_value_items(mpfr_t *values, size_t n)
{
    if (!values)
        return;
    for (size_t i = 0; i < n; i++)
        mpfr_clear(values[i]);
    free(values);
}

static void sort_value_items(mpfr_t *values, size_t n)
{
    for (size_t i = 1; i < n; i++) {
        size_t j = i;
        while (j > 0 && mpfr_cmp(values[j], values[j - 1]) < 0) {
            mpfr_swap(values[j], values[j - 1]);
            j--;
        }
    }
}

static bool set_median_result(TerminalCCalc *c, TerminalCValue *args,
                              size_t count, TerminalCValue *out)
{
    mpfr_t *values = NULL;
    size_t n = 0;
    if (!collect_value_items(c, args, count, &values, &n))
        return false;
    sort_value_items(values, n);
    out->type = TERMINALC_REAL;
    if (n % 2 == 1)
        mpfr_set(out->r, values[n / 2], MPFR_RNDN);
    else {
        mpfr_add(out->r, values[n / 2 - 1], values[n / 2], MPFR_RNDN);
        mpfr_div_ui(out->r, out->r, 2, MPFR_RNDN);
    }
    free_value_items(values, n);
    return true;
}

static bool set_quantile_result(TerminalCCalc *c, TerminalCValue *values_arg,
                                const TerminalCValue *q_arg,
                                TerminalCValue *out)
{
    if (!terminalc_is_scalar(q_arg)) {
        terminalc_fail(c, "quantile probability must be scalar");
        return false;
    }
    mpfr_t q;
    mpfr_init2(q, c->bits);
    terminalc_as_real(c, q, q_arg);
    if (mpfr_nan_p(q) || mpfr_inf_p(q) || mpfr_cmp_ui(q, 0) < 0 ||
        mpfr_cmp_ui(q, 1) > 0) {
        mpfr_clear(q);
        terminalc_fail(c, "quantile probability must be between 0 and 1");
        return false;
    }
    mpfr_t *values = NULL;
    size_t n = 0;
    if (!collect_value_items(c, values_arg, 1, &values, &n)) {
        mpfr_clear(q);
        return false;
    }
    sort_value_items(values, n);
    out->type = TERMINALC_REAL;
    if (n == 1) {
        mpfr_set(out->r, values[0], MPFR_RNDN);
    } else {
        mpfr_t pos, frac, lo, hi;
        mpfr_inits2(c->bits, pos, frac, lo, hi, (mpfr_ptr)0);
        mpfr_set_ui(pos, (unsigned long)(n - 1), MPFR_RNDN);
        mpfr_mul(pos, pos, q, MPFR_RNDN);
        mpfr_floor(pos, pos);
        unsigned long index = mpfr_get_ui(pos, MPFR_RNDN);
        mpfr_set_ui(frac, (unsigned long)(n - 1), MPFR_RNDN);
        mpfr_mul(frac, frac, q, MPFR_RNDN);
        mpfr_sub_ui(frac, frac, index, MPFR_RNDN);
        mpfr_set(lo, values[index], MPFR_RNDN);
        if (index + 1 < n) {
            mpfr_set(hi, values[index + 1], MPFR_RNDN);
            mpfr_sub(hi, hi, lo, MPFR_RNDN);
            mpfr_mul(hi, hi, frac, MPFR_RNDN);
            mpfr_add(out->r, lo, hi, MPFR_RNDN);
        } else {
            mpfr_set(out->r, lo, MPFR_RNDN);
        }
        mpfr_clears(pos, frac, lo, hi, (mpfr_ptr)0);
    }
    free_value_items(values, n);
    mpfr_clear(q);
    return true;
}

static bool set_pair_stat_result(TerminalCCalc *c, const char *name,
                                 const TerminalCValue *left,
                                 const TerminalCValue *right,
                                 TerminalCValue *out)
{
    TerminalCValue largs[1], rargs[1];
    terminalc_value_init(&largs[0], c->bits);
    terminalc_value_init(&rargs[0], c->bits);
    terminalc_value_copy(&largs[0], left);
    terminalc_value_copy(&rargs[0], right);
    mpfr_t *x = NULL, *y = NULL;
    size_t nx = 0, ny = 0;
    bool ok = collect_value_items(c, largs, 1, &x, &nx) &&
              collect_value_items(c, rargs, 1, &y, &ny);
    terminalc_value_clear(&largs[0]);
    terminalc_value_clear(&rargs[0]);
    if (!ok) {
        free_value_items(x, nx);
        free_value_items(y, ny);
        return false;
    }
    if (nx != ny) {
        free_value_items(x, nx);
        free_value_items(y, ny);
        terminalc_fail(c, "paired statistics need equal-length inputs");
        return false;
    }
    mpfr_t mx, my, dx, dy, sum, sx, sy, denom;
    mpfr_inits2(c->bits, mx, my, dx, dy, sum, sx, sy, denom,
                (mpfr_ptr)0);
    mpfr_set_zero(mx, 0);
    mpfr_set_zero(my, 0);
    for (size_t i = 0; i < nx; i++) {
        mpfr_add(mx, mx, x[i], MPFR_RNDN);
        mpfr_add(my, my, y[i], MPFR_RNDN);
    }
    mpfr_div_ui(mx, mx, (unsigned long)nx, MPFR_RNDN);
    mpfr_div_ui(my, my, (unsigned long)nx, MPFR_RNDN);
    mpfr_set_zero(sum, 0);
    mpfr_set_zero(sx, 0);
    mpfr_set_zero(sy, 0);
    for (size_t i = 0; i < nx; i++) {
        mpfr_sub(dx, x[i], mx, MPFR_RNDN);
        mpfr_sub(dy, y[i], my, MPFR_RNDN);
        mpfr_mul(denom, dx, dy, MPFR_RNDN);
        mpfr_add(sum, sum, denom, MPFR_RNDN);
        mpfr_mul(denom, dx, dx, MPFR_RNDN);
        mpfr_add(sx, sx, denom, MPFR_RNDN);
        mpfr_mul(denom, dy, dy, MPFR_RNDN);
        mpfr_add(sy, sy, denom, MPFR_RNDN);
    }
    out->type = TERMINALC_REAL;
    if (strcmp(name, "covariance") == 0 || strcmp(name, "cov") == 0) {
        mpfr_div_ui(out->r, sum, (unsigned long)nx, MPFR_RNDN);
    } else {
        mpfr_mul(denom, sx, sy, MPFR_RNDN);
        mpfr_sqrt(denom, denom, MPFR_RNDN);
        if (mpfr_zero_p(denom)) {
            terminalc_fail(c, "correlation is undefined for constant input");
            out->type = TERMINALC_RAT;
            mpq_set_si(out->q, 0, 1);
            ok = false;
        } else {
            mpfr_div(out->r, sum, denom, MPFR_RNDN);
        }
    }
    mpfr_clears(mx, my, dx, dy, sum, sx, sy, denom, (mpfr_ptr)0);
    free_value_items(x, nx);
    free_value_items(y, ny);
    return ok;
}

/* Floor/ceil with matching mpfr_* function pointer signatures. */
static int terminalc_floor_rnd(mpfr_ptr out, mpfr_srcptr in, mpfr_rnd_t rnd)
{
    (void)rnd;
    return mpfr_floor(out, in);
}

static int terminalc_ceil_rnd(mpfr_ptr out, mpfr_srcptr in, mpfr_rnd_t rnd)
{
    (void)rnd;
    return mpfr_ceil(out, in);
}

static bool tensor_unary_supported(const char *name)
{
    static const char *const names[] = {
        "abs",    "floor",  "ceil",   "sin",    "cos",    "tan",
        "sind",   "cosd",   "tand",   "asin",   "acos",   "atan",
        "sinh",   "cosh",   "tanh",   "asinh",  "acosh",  "atanh",
        "sqrt",   "ln",     "log",    "log10",  "exp",    "log2",
        "log1p",  "expm1",   "cbrt",   "erf",    "erfc",   "gamma",
        "lgamma", "sigmoid", "logistic", "softplus", "logit", "relu",
        "sign"
    };
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
        if (strcmp(name, names[i]) == 0)
            return true;
    }
    return false;
}

static bool is_nthroot_name(const char *name)
{
    return strcmp(name, "nthroot") == 0 || strcmp(name, "nroot") == 0 ||
           strcmp(name, "root") == 0;
}

static void ensure_finite_real(TerminalCCalc *c, const TerminalCValue *v);

/*
 * Ramanujan's rapidly convergent pi series:
 *
 *   1/pi = (2*sqrt(2)/9801) *
 *          sum(k=0..infinity) (4k)!*(1103 + 26390k) /
 *          ((k!)^4 * 396^(4k))
 *
 * Keep the factorial ratio as an MPFR recurrence instead of constructing
 * enormous factorial integers. Each term adds roughly eight decimal digits.
 */
static bool set_ramanujan_pi_result(TerminalCCalc *c, TerminalCValue *out)
{
    const mpfr_prec_t work_bits = c->bits + TERMINALC_RAMANUJAN_PI_GUARD_BITS;
    const mpfr_exp_t target_exp =
        -(mpfr_exp_t)(c->bits + TERMINALC_RAMANUJAN_PI_GUARD_BITS / 2);
    mpfr_t sum, factorial_term, term, ratio, linear, base_pow4;
    mpfr_t prefactor, sqrt2, reciprocal, result;
    mpfr_inits2(work_bits, sum, factorial_term, term, ratio, linear, base_pow4,
                prefactor, sqrt2, reciprocal, result, (mpfr_ptr)0);

    mpfr_set_ui(sum, 1103, MPFR_RNDN); /* k = 0 term */
    mpfr_set_ui(factorial_term, 1, MPFR_RNDN);
    mpfr_set_ui(base_pow4, 396, MPFR_RNDN);
    mpfr_pow_ui(base_pow4, base_pow4, 4, MPFR_RNDN);

    unsigned long max_terms = (unsigned long)(c->bits / 20) + 64;
    if (max_terms > TERMINALC_RAMANUJAN_PI_MAX_TERMS)
        max_terms = TERMINALC_RAMANUJAN_PI_MAX_TERMS;

    bool converged = false;
    for (unsigned long k = 1; k <= max_terms; k++) {
        unsigned long four_k = 4UL * k;
        mpfr_set_ui(ratio, 1, MPFR_RNDN);
        mpfr_mul_ui(ratio, ratio, four_k - 3, MPFR_RNDN);
        mpfr_mul_ui(ratio, ratio, four_k - 2, MPFR_RNDN);
        mpfr_mul_ui(ratio, ratio, four_k - 1, MPFR_RNDN);
        mpfr_mul_ui(ratio, ratio, four_k, MPFR_RNDN);
        mpfr_div_ui(ratio, ratio, k, MPFR_RNDN);
        mpfr_div_ui(ratio, ratio, k, MPFR_RNDN);
        mpfr_div_ui(ratio, ratio, k, MPFR_RNDN);
        mpfr_div_ui(ratio, ratio, k, MPFR_RNDN);
        mpfr_div(ratio, ratio, base_pow4, MPFR_RNDN);
        mpfr_mul(factorial_term, factorial_term, ratio, MPFR_RNDN);

        mpfr_set_ui(linear, k, MPFR_RNDN);
        mpfr_mul_ui(linear, linear, 26390, MPFR_RNDN);
        mpfr_add_ui(linear, linear, 1103, MPFR_RNDN);
        mpfr_mul(term, factorial_term, linear, MPFR_RNDN);
        mpfr_add(sum, sum, term, MPFR_RNDN);

        if (mpfr_zero_p(term) || mpfr_cmp_ui_2exp(term, 1, target_exp) < 0) {
            converged = true;
            break;
        }
    }

    if (!converged) {
        mpfr_clears(sum, factorial_term, term, ratio, linear, base_pow4,
                    prefactor, sqrt2, reciprocal, result, (mpfr_ptr)0);
        terminalc_fail(c, "ramanujan_pi did not converge at this precision");
        return false;
    }

    mpfr_set_ui(prefactor, 2, MPFR_RNDN);
    mpfr_sqrt_ui(sqrt2, 2, MPFR_RNDN);
    mpfr_mul(prefactor, prefactor, sqrt2, MPFR_RNDN);
    mpfr_div_ui(prefactor, prefactor, 9801, MPFR_RNDN);
    mpfr_mul(prefactor, prefactor, sum, MPFR_RNDN);
    mpfr_ui_div(reciprocal, 1, prefactor, MPFR_RNDN);

    out->type = TERMINALC_REAL;
    mpfr_set(result, reciprocal, MPFR_RNDN);
    mpfr_set(out->r, result, MPFR_RNDN);
    mpfr_clears(sum, factorial_term, term, ratio, linear, base_pow4,
                prefactor, sqrt2, reciprocal, result, (mpfr_ptr)0);
    return true;
}

static bool get_nthroot_index(TerminalCCalc *c, const TerminalCValue *value,
                              unsigned long *index)
{
    if (!terminalc_is_integer(value) ||
        mpz_sgn(mpq_numref(value->q)) <= 0 ||
        !mpz_fits_ulong_p(mpq_numref(value->q))) {
        terminalc_fail(c, "nthroot index must be a positive integer");
        return false;
    }
    unsigned long n = mpz_get_ui(mpq_numref(value->q));
    if (n > TERMINALC_NTHROOT_MAX_INDEX) {
        terminalc_fail(c, "nthroot index is too large (maximum is 1000000)");
        return false;
    }
    *index = n;
    return true;
}

static bool set_nthroot_scalar(TerminalCCalc *c, const TerminalCValue *value,
                               unsigned long index, TerminalCValue *out)
{
    if (!terminalc_is_scalar(value)) {
        terminalc_fail(c, "nthroot radicand must be a scalar");
        return false;
    }
    mpfr_t x, magnitude;
    mpfr_inits2(c->bits, x, magnitude, (mpfr_ptr)0);
    terminalc_as_real(c, x, value);
    if (mpfr_nan_p(x) || mpfr_inf_p(x)) {
        mpfr_clears(x, magnitude, (mpfr_ptr)0);
        terminalc_fail(c, "nthroot radicand must be finite");
        return false;
    }
    if (mpfr_sgn(x) < 0 && index % 2 == 0) {
        mpfr_clears(x, magnitude, (mpfr_ptr)0);
        terminalc_fail(c, "even root of a negative value is not real");
        return false;
    }
    out->type = TERMINALC_REAL;
    if (mpfr_zero_p(x)) {
        mpfr_set_zero(out->r, 0);
    } else {
        mpfr_abs(magnitude, x, MPFR_RNDN);
        mpfr_rootn_ui(out->r, magnitude, index, MPFR_RNDN);
        if (mpfr_sgn(x) < 0)
            mpfr_neg(out->r, out->r, MPFR_RNDN);
    }
    mpfr_clears(x, magnitude, (mpfr_ptr)0);
    ensure_finite_real(c, out);
    return !c->error;
}

static bool tensor_nthroot_apply(TerminalCCalc *c, const TerminalCValue *input,
                                 unsigned long index, TerminalCValue *out)
{
    if (!input->ten ||
        !terminalc_tensor_create(c, out, input->ten->rank, input->ten->shape))
        return false;
    for (size_t i = 0; i < input->ten->n; i++) {
        if (!set_nthroot_scalar(c, &input->ten->data[i], index,
                                &out->ten->data[i]))
            return false;
    }
    return true;
}

static bool tensor_unary_apply(TerminalCCalc *c, const char *name,
                               const TerminalCValue *input,
                               TerminalCValue *out)
{
    if (!input->ten || !terminalc_tensor_create(c, out, input->ten->rank,
                                                 input->ten->shape))
        return false;
    for (size_t i = 0; i < input->ten->n; i++) {
        TerminalCValue scalar;
        terminalc_value_init(&scalar, c->bits);
        terminalc_value_copy(&scalar, &input->ten->data[i]);
        TerminalCValue result = terminalc_call(c, name, &scalar, 1);
        if (c->error) {
            terminalc_value_clear(&result);
            return false;
        }
        terminalc_value_copy(&out->ten->data[i], &result);
        terminalc_value_clear(&result);
    }
    return true;
}

static void result_real(TerminalCCalc *c, TerminalCValue *out,
                        int (*fn)(mpfr_ptr, mpfr_srcptr, mpfr_rnd_t),
                        const TerminalCValue *a)
{
    out->type = TERMINALC_REAL;
    mpfr_t t;
    mpfr_init2(t, c->bits);
    terminalc_as_real(c, t, a);
    (void)fn(out->r, t, MPFR_RNDN);
    if (mpfr_nan_p(out->r) || mpfr_inf_p(out->r))
        terminalc_fail(c, "function result is not finite");
    mpfr_clear(t);
}

static void ensure_finite_real(TerminalCCalc *c, const TerminalCValue *v)
{
    if (v->type == TERMINALC_REAL &&
        (mpfr_nan_p(v->r) || mpfr_inf_p(v->r)))
        terminalc_fail(c, "function result is not finite");
}

static void degrees_to_rad(TerminalCCalc *c, mpfr_t out, const TerminalCValue *a)
{
    mpfr_t t, pi;
    mpfr_init2(t, c->bits);
    mpfr_init2(pi, c->bits);
    terminalc_as_real(c, t, a);
    mpfr_const_pi(pi, MPFR_RNDN);
    mpfr_mul(t, t, pi, MPFR_RNDN);
    mpfr_div_ui(t, t, 180, MPFR_RNDN);
    mpfr_set(out, t, MPFR_RNDN);
    mpfr_clear(t);
    mpfr_clear(pi);
}

/* True if arg is an exact integer degree measure (fits in long). */
static bool integer_degrees(const TerminalCValue *a, long *deg_out)
{
    if (!terminalc_is_integer(a))
        return false;
    if (!mpz_fits_slong_p(mpq_numref(a->q)))
        return false;
    *deg_out = mpz_get_si(mpq_numref(a->q));
    return true;
}

static long norm360(long d)
{
    d %= 360;
    if (d < 0)
        d += 360;
    return d;
}

/*
 * Exact common-angle values for integer degrees.
 * Returns: 0=ok, -1=not a table angle, 1=undefined (tan asymptote).
 * kind: 0=sin, 1=cos, 2=tan
 */
static int exact_deg_trig(TerminalCCalc *c, int kind, long deg, TerminalCValue *out)
{
    deg = norm360(deg);

    if (kind == 2 && (deg == 90 || deg == 270)) {
        terminalc_fail(c, "tangent undefined at this angle");
        return 1;
    }

    /* Axis angles → exact rationals (avoids -0.000… from FP). */
    if (kind == 0) { /* sin */
        if (deg == 0 || deg == 180) {
            terminalc_value_set_si(out, 0);
            return 0;
        }
        if (deg == 90) {
            terminalc_value_set_si(out, 1);
            return 0;
        }
        if (deg == 270) {
            terminalc_value_set_si(out, -1);
            return 0;
        }
    } else if (kind == 1) { /* cos */
        if (deg == 90 || deg == 270) {
            terminalc_value_set_si(out, 0);
            return 0;
        }
        if (deg == 0) {
            terminalc_value_set_si(out, 1);
            return 0;
        }
        if (deg == 180) {
            terminalc_value_set_si(out, -1);
            return 0;
        }
    } else { /* tan */
        if (deg == 0 || deg == 180) {
            terminalc_value_set_si(out, 0);
            return 0;
        }
        if (deg == 45 || deg == 225) {
            terminalc_value_set_si(out, 1);
            return 0;
        }
        if (deg == 135 || deg == 315) {
            terminalc_value_set_si(out, -1);
            return 0;
        }
    }

    /* First-quadrant reference + signs */
    long ref;
    int ss = 1, sc = 1;
    if (deg <= 90) {
        ref = deg;
    } else if (deg <= 180) {
        ref = 180 - deg;
        sc = -1;
    } else if (deg <= 270) {
        ref = deg - 180;
        ss = -1;
        sc = -1;
    } else {
        ref = 360 - deg;
        ss = -1;
    }

    if (ref != 30 && ref != 45 && ref != 60)
        return -1;

    mpfr_t s2, s3;
    mpfr_init2(s2, c->bits);
    mpfr_init2(s3, c->bits);
    mpfr_sqrt_ui(s2, 2, MPFR_RNDN);
    mpfr_sqrt_ui(s3, 3, MPFR_RNDN);

    int ts = ((ss < 0) != (sc < 0)) ? -1 : 1;

    if (kind == 2) {
        if (ref == 45) {
            terminalc_value_set_si(out, ts);
        } else if (ref == 30) {
            out->type = TERMINALC_REAL;
            mpfr_set_ui(out->r, 1, MPFR_RNDN);
            mpfr_div(out->r, out->r, s3, MPFR_RNDN);
            if (ts < 0)
                mpfr_neg(out->r, out->r, MPFR_RNDN);
        } else { /* 60 */
            out->type = TERMINALC_REAL;
            mpfr_set(out->r, s3, MPFR_RNDN);
            if (ts < 0)
                mpfr_neg(out->r, out->r, MPFR_RNDN);
        }
        mpfr_clear(s2);
        mpfr_clear(s3);
        return 0;
    }

    out->type = TERMINALC_REAL;
    if (kind == 0) { /* sin */
        if (ref == 30) {
            mpfr_set_ui(out->r, 1, MPFR_RNDN);
            mpfr_div_ui(out->r, out->r, 2, MPFR_RNDN);
        } else if (ref == 45) {
            mpfr_div_ui(out->r, s2, 2, MPFR_RNDN);
        } else {
            mpfr_div_ui(out->r, s3, 2, MPFR_RNDN);
        }
        if (ss < 0)
            mpfr_neg(out->r, out->r, MPFR_RNDN);
    } else { /* cos */
        if (ref == 30) {
            mpfr_div_ui(out->r, s3, 2, MPFR_RNDN);
        } else if (ref == 45) {
            mpfr_div_ui(out->r, s2, 2, MPFR_RNDN);
        } else {
            mpfr_set_ui(out->r, 1, MPFR_RNDN);
            mpfr_div_ui(out->r, out->r, 2, MPFR_RNDN);
        }
        if (sc < 0)
            mpfr_neg(out->r, out->r, MPFR_RNDN);
    }

    mpfr_clear(s2);
    mpfr_clear(s3);
    return 0;
}

/* 1 = exact value stored, 0 = not a table case, -1 = hard error (e.g. tan 90). */
static int try_exact_deg(TerminalCCalc *c, const char *fn, const TerminalCValue *a, TerminalCValue *out)
{
    long deg;
    if (!integer_degrees(a, &deg))
        return 0;
    int kind = -1;
    if (strcmp(fn, "sin") == 0 || strcmp(fn, "sind") == 0)
        kind = 0;
    else if (strcmp(fn, "cos") == 0 || strcmp(fn, "cosd") == 0)
        kind = 1;
    else if (strcmp(fn, "tan") == 0 || strcmp(fn, "tand") == 0)
        kind = 2;
    else
        return 0;
    int rc = exact_deg_trig(c, kind, deg, out);
    if (rc == 0)
        return 1;
    if (rc == 1)
        return -1;
    return 0;
}

TerminalCValue terminalc_call(TerminalCCalc *c, const char *n, TerminalCValue *args, size_t count)
{
    TerminalCValue o;
    terminalc_value_init(&o, c->bits);
    /* Only hex/bin/oct/base keep a non-decimal print base. */
    c->out_base = 0;

    if (count == 1 && args[0].type == TERMINALC_TENSOR &&
        tensor_unary_supported(n)) {
        (void)tensor_unary_apply(c, n, &args[0], &o);
        goto done_clear;
    }

    if (is_nthroot_name(n) && count == 2) {
        unsigned long index = 0;
        if (get_nthroot_index(c, &args[1], &index)) {
            if (args[0].type == TERMINALC_TENSOR && args[0].ten)
                (void)tensor_nthroot_apply(c, &args[0], index, &o);
            else
                (void)set_nthroot_scalar(c, &args[0], index, &o);
        }
        goto done_clear;
    }

    if (strcmp(n, "if") == 0 && count == 3) {
        if (terminalc_is_truthy(&args[0]))
            terminalc_value_copy(&o, &args[1]);
        else
            terminalc_value_copy(&o, &args[2]);
    } else if (strcmp(n, "zeros") == 0 && count >= 1 && count <= 4) {
        size_t sh[4] = {0};
        int rank = (int)count;
        for (size_t i = 0; i < count; i++) {
            if (!get_dimension(&args[i], &sh[i])) {
                terminalc_fail(c, "zeros: dimensions must be non-negative integers");
                goto done_clear;
            }
        }
        TerminalCValue z;
        terminalc_value_init(&z, c->bits);
        terminalc_value_set_si(&z, 0);
        (void)terminalc_tensor_fill(c, &o, rank, sh, &z);
        terminalc_value_clear(&z);
    } else if (strcmp(n, "ones") == 0 && count >= 1 && count <= 4) {
        size_t sh[4] = {0};
        int rank = (int)count;
        for (size_t i = 0; i < count; i++) {
            if (!get_dimension(&args[i], &sh[i])) {
                terminalc_fail(c, "ones: dimensions must be non-negative integers");
                goto done_clear;
            }
        }
        TerminalCValue one;
        terminalc_value_init(&one, c->bits);
        terminalc_value_set_si(&one, 1);
        (void)terminalc_tensor_fill(c, &o, rank, sh, &one);
        terminalc_value_clear(&one);
    } else if (strcmp(n, "eye") == 0 && count == 1 && terminalc_is_integer(&args[0])) {
        size_t dim;
        if (!get_dimension(&args[0], &dim))
            terminalc_fail(c, "eye: dimension must be a non-negative integer");
        else
            (void)terminalc_tensor_eye(c, &o, dim);
    } else if (strcmp(n, "mat") == 0 && count >= 3) {
        size_t rows, cols;
        if (!get_dimension(&args[0], &rows) ||
            !get_dimension(&args[1], &cols)) {
            terminalc_fail(c, "mat: rows and cols must be non-negative integers");
        } else {
            (void)terminalc_tensor_from_args(c, &o, rows, cols, args + 2, count - 2);
        }
    } else if (strcmp(n, "transpose") == 0 && count == 1) {
        (void)terminalc_linalg_transpose(c, &args[0], &o);
    } else if (strcmp(n, "det") == 0 && count == 1) {
        (void)terminalc_linalg_det(c, &args[0], &o);
    } else if (strcmp(n, "inv") == 0 && count == 1) {
        (void)terminalc_linalg_inv(c, &args[0], &o);
    } else if (strcmp(n, "linsolve") == 0 && count == 2) {
        (void)terminalc_linalg_solve(c, &args[0], &args[1], &o);
    } else if (strcmp(n, "dot") == 0 && count == 2) {
        (void)terminalc_linalg_dot(c, &args[0], &args[1], &o);
    } else if (strcmp(n, "shape") == 0 && count == 1) {
        if (args[0].type != TERMINALC_TENSOR || !args[0].ten) {
            terminalc_fail(c, "shape expects a tensor");
        } else {
            size_t r = (size_t)args[0].ten->rank;
            size_t sh[1] = {r};
            if (terminalc_tensor_create(c, &o, 1, sh)) {
                for (size_t i = 0; i < r; i++)
                    terminalc_value_set_si(&o.ten->data[i],
                                    (long)args[0].ten->shape[i]);
            }
        }
    } else if (strcmp(n, "to") == 0 && count == 2) {
        (void)terminalc_units_to(c, &args[0], &args[1], &o);
    } else if (strcmp(n, "mag") == 0 && count == 1) {
        if (args[0].type == TERMINALC_MEASURE) {
            o.type = TERMINALC_REAL;
            terminalc_as_real(c, o.r, &args[0]);
        } else {
            terminalc_value_copy(&o, &args[0]);
        }
    } else if (strcmp(n, "abs") == 0 && count == 1) {
        if (!terminalc_is_scalar(&args[0])) {
            terminalc_fail(c, "abs expects a scalar value");
        } else if (args[0].type == TERMINALC_BOOL) {
            terminalc_value_set_si(&o, args[0].b ? 1 : 0);
        } else if (args[0].type == TERMINALC_RAT) {
            o.type = TERMINALC_RAT;
            mpq_abs(o.q, args[0].q);
        } else if (args[0].type == TERMINALC_MEASURE) {
            o.type = TERMINALC_MEASURE;
            o.unit_id = args[0].unit_id;
            mpq_abs(o.q, args[0].q);
            mpfr_abs(o.r, args[0].r, MPFR_RNDN);
        } else {
            o.type = TERMINALC_REAL;
            mpfr_abs(o.r, args[0].r, MPFR_RNDN);
        }
    } else if ((strcmp(n, "asin") == 0 || strcmp(n, "acos") == 0 ||
                strcmp(n, "atan") == 0 || strcmp(n, "sinh") == 0 ||
                strcmp(n, "cosh") == 0 || strcmp(n, "tanh") == 0 ||
                strcmp(n, "asinh") == 0 || strcmp(n, "acosh") == 0 ||
                strcmp(n, "atanh") == 0 || strcmp(n, "log2") == 0 ||
                strcmp(n, "log1p") == 0 || strcmp(n, "expm1") == 0 ||
                strcmp(n, "cbrt") == 0 || strcmp(n, "erf") == 0 ||
                strcmp(n, "erfc") == 0 || strcmp(n, "gamma") == 0 ||
                strcmp(n, "lgamma") == 0) &&
               count == 1 && terminalc_is_scalar(&args[0])) {
        if (strcmp(n, "asin") == 0)
            result_real(c, &o, mpfr_asin, &args[0]);
        else if (strcmp(n, "acos") == 0)
            result_real(c, &o, mpfr_acos, &args[0]);
        else if (strcmp(n, "atan") == 0)
            result_real(c, &o, mpfr_atan, &args[0]);
        else if (strcmp(n, "sinh") == 0)
            result_real(c, &o, mpfr_sinh, &args[0]);
        else if (strcmp(n, "cosh") == 0)
            result_real(c, &o, mpfr_cosh, &args[0]);
        else if (strcmp(n, "tanh") == 0)
            result_real(c, &o, mpfr_tanh, &args[0]);
        else if (strcmp(n, "asinh") == 0)
            result_real(c, &o, mpfr_asinh, &args[0]);
        else if (strcmp(n, "acosh") == 0)
            result_real(c, &o, mpfr_acosh, &args[0]);
        else if (strcmp(n, "atanh") == 0)
            result_real(c, &o, mpfr_atanh, &args[0]);
        else if (strcmp(n, "log2") == 0)
            result_real(c, &o, mpfr_log2, &args[0]);
        else if (strcmp(n, "log1p") == 0)
            result_real(c, &o, mpfr_log1p, &args[0]);
        else if (strcmp(n, "expm1") == 0)
            result_real(c, &o, mpfr_expm1, &args[0]);
        else if (strcmp(n, "erf") == 0)
            result_real(c, &o, mpfr_erf, &args[0]);
        else if (strcmp(n, "erfc") == 0)
            result_real(c, &o, mpfr_erfc, &args[0]);
        else if (strcmp(n, "gamma") == 0)
            result_real(c, &o, mpfr_gamma, &args[0]);
        else if (strcmp(n, "lgamma") == 0)
            result_real(c, &o, mpfr_lngamma, &args[0]);
        else
            result_real(c, &o, mpfr_cbrt, &args[0]);
    } else if (strcmp(n, "atan2") == 0 && count == 2 &&
               terminalc_is_scalar(&args[0]) && terminalc_is_scalar(&args[1])) {
        mpfr_t y, x;
        mpfr_inits2(c->bits, y, x, (mpfr_ptr)0);
        terminalc_as_real(c, y, &args[0]);
        terminalc_as_real(c, x, &args[1]);
        o.type = TERMINALC_REAL;
        mpfr_atan2(o.r, y, x, MPFR_RNDN);
        mpfr_clears(y, x, (mpfr_ptr)0);
    } else if (strcmp(n, "hypot") == 0 && count == 2 &&
               terminalc_is_scalar(&args[0]) && terminalc_is_scalar(&args[1])) {
        mpfr_t x, y;
        mpfr_inits2(c->bits, x, y, (mpfr_ptr)0);
        terminalc_as_real(c, x, &args[0]);
        terminalc_as_real(c, y, &args[1]);
        o.type = TERMINALC_REAL;
        mpfr_hypot(o.r, x, y, MPFR_RNDN);
        mpfr_clears(x, y, (mpfr_ptr)0);
    } else if (strcmp(n, "pow") == 0 && count == 2 &&
               terminalc_is_scalar(&args[0]) && terminalc_is_scalar(&args[1])) {
        mpfr_t x, y;
        mpfr_inits2(c->bits, x, y, (mpfr_ptr)0);
        terminalc_as_real(c, x, &args[0]);
        terminalc_as_real(c, y, &args[1]);
        o.type = TERMINALC_REAL;
        mpfr_pow(o.r, x, y, MPFR_RNDN);
        ensure_finite_real(c, &o);
        mpfr_clears(x, y, (mpfr_ptr)0);
    } else if ((strcmp(n, "sigmoid") == 0 || strcmp(n, "logistic") == 0) &&
               count == 1 &&
               terminalc_is_scalar(&args[0])) {
        mpfr_t x, t, one;
        mpfr_inits2(c->bits, x, t, one, (mpfr_ptr)0);
        terminalc_as_real(c, x, &args[0]);
        mpfr_set_ui(one, 1, MPFR_RNDN);
        if (mpfr_sgn(x) >= 0) {
            mpfr_neg(t, x, MPFR_RNDN);
            mpfr_exp(t, t, MPFR_RNDN);
            mpfr_add(t, t, one, MPFR_RNDN);
            mpfr_div(o.r, one, t, MPFR_RNDN);
        } else {
            mpfr_exp(t, x, MPFR_RNDN);
            mpfr_add(x, t, one, MPFR_RNDN);
            mpfr_div(o.r, t, x, MPFR_RNDN);
        }
        o.type = TERMINALC_REAL;
        mpfr_clears(x, t, one, (mpfr_ptr)0);
    } else if (strcmp(n, "softplus") == 0 && count == 1 &&
               terminalc_is_scalar(&args[0])) {
        mpfr_t x, t;
        mpfr_inits2(c->bits, x, t, (mpfr_ptr)0);
        terminalc_as_real(c, x, &args[0]);
        if (mpfr_sgn(x) > 0) {
            mpfr_neg(t, x, MPFR_RNDN);
            mpfr_exp(t, t, MPFR_RNDN);
            mpfr_log1p(t, t, MPFR_RNDN);
            mpfr_add(o.r, x, t, MPFR_RNDN);
        } else {
            mpfr_exp(t, x, MPFR_RNDN);
            mpfr_log1p(o.r, t, MPFR_RNDN);
        }
        o.type = TERMINALC_REAL;
        ensure_finite_real(c, &o);
        mpfr_clears(x, t, (mpfr_ptr)0);
    } else if (strcmp(n, "logit") == 0 && count == 1 &&
               terminalc_is_scalar(&args[0])) {
        mpfr_t x, one, t;
        mpfr_inits2(c->bits, x, one, t, (mpfr_ptr)0);
        terminalc_as_real(c, x, &args[0]);
        mpfr_set_ui(one, 1, MPFR_RNDN);
        if (mpfr_cmp_ui(x, 0) <= 0 || mpfr_cmp_ui(x, 1) >= 0) {
            terminalc_fail(c, "logit expects 0 < x < 1");
        } else {
            mpfr_sub(t, one, x, MPFR_RNDN);
            mpfr_div(t, x, t, MPFR_RNDN);
            mpfr_log(o.r, t, MPFR_RNDN);
            o.type = TERMINALC_REAL;
            ensure_finite_real(c, &o);
        }
        mpfr_clears(x, one, t, (mpfr_ptr)0);
    } else if (strcmp(n, "relu") == 0 && count == 1 &&
               terminalc_is_scalar(&args[0])) {
        o.type = TERMINALC_REAL;
        terminalc_as_real(c, o.r, &args[0]);
        if (mpfr_sgn(o.r) < 0)
            mpfr_set_zero(o.r, 0);
    } else if (strcmp(n, "sign") == 0 && count == 1 &&
               terminalc_is_scalar(&args[0])) {
        int sign = 0;
        if (args[0].type == TERMINALC_REAL && mpfr_nan_p(args[0].r))
            terminalc_fail(c, "sign is undefined for NaN");
        else {
            mpfr_t x;
            mpfr_init2(x, c->bits);
            terminalc_as_real(c, x, &args[0]);
            sign = mpfr_sgn(x);
            mpfr_clear(x);
            terminalc_value_set_si(&o, sign);
        }
    } else if (strcmp(n, "median") == 0 && count >= 1) {
        (void)set_median_result(c, args, count, &o);
    } else if ((strcmp(n, "quantile") == 0 || strcmp(n, "percentile") == 0) &&
               count == 2) {
        (void)set_quantile_result(c, &args[0], &args[1], &o);
    } else if ((strcmp(n, "covariance") == 0 || strcmp(n, "cov") == 0 ||
                strcmp(n, "correlation") == 0 || strcmp(n, "corr") == 0) &&
               count == 2) {
        (void)set_pair_stat_result(c, n, &args[0], &args[1], &o);
    } else if ((strcmp(n, "min") == 0 || strcmp(n, "max") == 0) &&
               count >= 1) {
        if (!select_extreme(c, args, count, strcmp(n, "max") == 0, &o))
            terminalc_fail(c, "min/max need scalar arguments");
    } else if (strcmp(n, "clamp") == 0 && count == 3 &&
               terminalc_is_scalar(&args[0]) && terminalc_is_scalar(&args[1]) &&
               terminalc_is_scalar(&args[2])) {
        if (scalar_cmp(c, &args[1], &args[2]) > 0)
            terminalc_fail(c, "clamp lower bound exceeds upper bound");
        else if (scalar_cmp(c, &args[0], &args[1]) < 0)
            terminalc_value_copy(&o, &args[1]);
        else if (scalar_cmp(c, &args[0], &args[2]) > 0)
            terminalc_value_copy(&o, &args[2]);
        else
            terminalc_value_copy(&o, &args[0]);
    } else if ((strcmp(n, "mean") == 0 || strcmp(n, "variance") == 0 ||
                strcmp(n, "stddev") == 0 || strcmp(n, "norm") == 0) &&
               count >= 1) {
        (void)set_aggregate_result(c, n, args, count, &o);
    } else if (strcmp(n, "sin") == 0 && count == 1 &&
               terminalc_is_scalar(&args[0])) {
        int ex = c->degrees ? try_exact_deg(c, "sin", &args[0], &o) : 0;
        if (ex == 0) {
            if (c->degrees) {
                mpfr_t t;
                mpfr_init2(t, c->bits);
                degrees_to_rad(c, t, &args[0]);
                o.type = TERMINALC_REAL;
                o.display_decimal = true;
                mpfr_sin(o.r, t, MPFR_RNDN);
                mpfr_clear(t);
            } else {
                result_real(c, &o, mpfr_sin, &args[0]);
            }
        }
    } else if (strcmp(n, "cos") == 0 && count == 1 &&
               terminalc_is_scalar(&args[0])) {
        int ex = c->degrees ? try_exact_deg(c, "cos", &args[0], &o) : 0;
        if (ex == 0) {
            if (c->degrees) {
                mpfr_t t;
                mpfr_init2(t, c->bits);
                degrees_to_rad(c, t, &args[0]);
                o.type = TERMINALC_REAL;
                o.display_decimal = true;
                mpfr_cos(o.r, t, MPFR_RNDN);
                mpfr_clear(t);
            } else {
                result_real(c, &o, mpfr_cos, &args[0]);
            }
        }
    } else if (strcmp(n, "tan") == 0 && count == 1 &&
               terminalc_is_scalar(&args[0])) {
        int ex = c->degrees ? try_exact_deg(c, "tan", &args[0], &o) : 0;
        if (ex == 0) {
            if (c->degrees) {
                mpfr_t t;
                mpfr_init2(t, c->bits);
                degrees_to_rad(c, t, &args[0]);
                o.type = TERMINALC_REAL;
                o.display_decimal = true;
                mpfr_tan(o.r, t, MPFR_RNDN);
                mpfr_clear(t);
            } else {
                result_real(c, &o, mpfr_tan, &args[0]);
            }
        }
    } else if (strcmp(n, "sqrt") == 0 && count == 1 &&
               terminalc_is_scalar(&args[0])) {
        result_real(c, &o, mpfr_sqrt, &args[0]);
    } else if (strcmp(n, "ln") == 0 && count == 1 &&
               terminalc_is_scalar(&args[0])) {
        result_real(c, &o, mpfr_log, &args[0]);
    } else if ((strcmp(n, "log") == 0 || strcmp(n, "log10") == 0) && count == 1 &&
               terminalc_is_scalar(&args[0])) {
        result_real(c, &o, mpfr_log10, &args[0]);
    } else if ((strcmp(n, "logb") == 0 || strcmp(n, "logn") == 0 ||
                strcmp(n, "log_base") == 0) && count == 2 &&
               terminalc_is_scalar(&args[0]) && terminalc_is_scalar(&args[1])) {
        mpfr_t x, base;
        mpfr_inits2(c->bits, x, base, (mpfr_ptr)0);
        terminalc_as_real(c, x, &args[0]);
        terminalc_as_real(c, base, &args[1]);
        if (mpfr_nan_p(x) || mpfr_inf_p(x) || mpfr_nan_p(base) ||
            mpfr_inf_p(base) || mpfr_cmp_ui(x, 0) <= 0 ||
            mpfr_cmp_ui(base, 0) <= 0 ||
            mpfr_cmp_ui(base, 1) == 0) {
            terminalc_fail(c, "log base expects x > 0, base > 0, base != 1");
        } else {
            o.type = TERMINALC_REAL;
            mpfr_log(o.r, x, MPFR_RNDN);
            mpfr_log(base, base, MPFR_RNDN);
            mpfr_div(o.r, o.r, base, MPFR_RNDN);
            ensure_finite_real(c, &o);
        }
        mpfr_clears(x, base, (mpfr_ptr)0);
    } else if (strcmp(n, "ramanujan_pi") == 0 && count == 0) {
        (void)set_ramanujan_pi_result(c, &o);
    } else if (strcmp(n, "exp") == 0 && count == 1 &&
               terminalc_is_scalar(&args[0])) {
        result_real(c, &o, mpfr_exp, &args[0]);
    } else if (strcmp(n, "floor") == 0 && count == 1 &&
               terminalc_is_scalar(&args[0])) {
        result_real(c, &o, terminalc_floor_rnd, &args[0]);
    } else if (strcmp(n, "ceil") == 0 && count == 1 &&
               terminalc_is_scalar(&args[0])) {
        result_real(c, &o, terminalc_ceil_rnd, &args[0]);
    } else if ((strcmp(n, "sind") == 0 || strcmp(n, "cosd") == 0 ||
                strcmp(n, "tand") == 0) &&
               count == 1 && terminalc_is_scalar(&args[0])) {
        int ex = try_exact_deg(c, n, &args[0], &o);
        if (ex == 0) {
            mpfr_t t;
            mpfr_init2(t, c->bits);
            degrees_to_rad(c, t, &args[0]);
            o.type = TERMINALC_REAL;
            o.display_decimal = true;
            if (strcmp(n, "sind") == 0)
                mpfr_sin(o.r, t, MPFR_RNDN);
            else if (strcmp(n, "cosd") == 0)
                mpfr_cos(o.r, t, MPFR_RNDN);
            else
                mpfr_tan(o.r, t, MPFR_RNDN);
            mpfr_clear(t);
        }
    } else if (strcmp(n, "gcd") == 0 && count == 2 &&
               terminalc_is_integer(&args[0]) && terminalc_is_integer(&args[1])) {
        o.type = TERMINALC_RAT;
        mpz_gcd(mpq_numref(o.q), mpq_numref(args[0].q), mpq_numref(args[1].q));
        mpz_set_ui(mpq_denref(o.q), 1);
    } else if (strcmp(n, "lcm") == 0 && count == 2 &&
               terminalc_is_integer(&args[0]) && terminalc_is_integer(&args[1])) {
        o.type = TERMINALC_RAT;
        mpz_lcm(mpq_numref(o.q), mpq_numref(args[0].q), mpq_numref(args[1].q));
        mpz_set_ui(mpq_denref(o.q), 1);
    } else if ((strcmp(n, "hex") == 0 || strcmp(n, "bin") == 0 ||
                strcmp(n, "oct") == 0) &&
               count == 1 && terminalc_is_integer(&args[0])) {
        o.type = TERMINALC_RAT;
        mpq_set(o.q, args[0].q);
        if (strcmp(n, "hex") == 0)
            c->out_base = 16;
        else if (strcmp(n, "bin") == 0)
            c->out_base = 2;
        else
            c->out_base = 8;
    } else if ((strcmp(n, "base") == 0 || strcmp(n, "tobase") == 0) &&
               count == 2 && terminalc_is_integer(&args[0]) && terminalc_is_integer(&args[1])) {
        if (!mpz_fits_slong_p(mpq_numref(args[1].q))) {
            terminalc_fail(c, "base must be an integer from 2 to 36");
        } else {
            long b = mpz_get_si(mpq_numref(args[1].q));
            if (b < 2 || b > 36) {
                terminalc_fail(c, "base must be an integer from 2 to 36");
            } else {
                o.type = TERMINALC_RAT;
                mpq_set(o.q, args[0].q);
                c->out_base = (int)b;
            }
        }
    } else if ((strcmp(n, "fact") == 0 || strcmp(n, "factorial") == 0) &&
               count == 1 && terminalc_is_integer(&args[0]) &&
               mpz_sgn(mpq_numref(args[0].q)) >= 0) {
        o.type = TERMINALC_RAT;
        if (!mpz_fits_ulong_p(mpq_numref(args[0].q)) ||
            mpz_cmp_ui(mpq_numref(args[0].q), TERMINALC_COMBINATORIC_MAX_N) > 0) {
            terminalc_fail(c, "factorial input is too large (max 100000)");
        } else {
            mpz_fac_ui(mpq_numref(o.q), mpz_get_ui(mpq_numref(args[0].q)));
            mpz_set_ui(mpq_denref(o.q), 1);
        }
    } else if ((strcmp(n, "ncr") == 0 || strcmp(n, "choose") == 0) && count == 2 &&
               terminalc_is_integer(&args[0]) && terminalc_is_integer(&args[1])) {
        o.type = TERMINALC_RAT;
        if (mpz_sgn(mpq_numref(args[0].q)) < 0 ||
            mpz_sgn(mpq_numref(args[1].q)) < 0 ||
            !mpz_fits_ulong_p(mpq_numref(args[0].q)) ||
            !mpz_fits_ulong_p(mpq_numref(args[1].q)) ||
            mpz_cmp_ui(mpq_numref(args[0].q), TERMINALC_COMBINATORIC_MAX_N) > 0) {
            terminalc_fail(c, "ncr needs non-negative k and 0 <= n <= 100000");
        } else {
            mpz_bin_uiui(mpq_numref(o.q),
                         mpz_get_ui(mpq_numref(args[0].q)),
                         mpz_get_ui(mpq_numref(args[1].q)));
            mpz_set_ui(mpq_denref(o.q), 1);
        }
    } else if (terminalc_series_call(c, n, args, count, &o)) {
        /* AP / GP / HP / SP / primes handled in series.c */
    } else {
        terminalc_fail(c, "unknown function or invalid argument count/type");
    }

done_clear:
    ensure_finite_real(c, &o);
    for (size_t i = 0; i < count; i++)
        terminalc_value_clear(&args[i]);
    return o;
}

/* Called from parse for solve/diff/integral with raw expression text. */
TerminalCValue terminalc_call_expr_fn(TerminalCCalc *c, const char *name, const char *expr_text,
                        TerminalCValue *args, size_t count)
{
    TerminalCValue o;
    terminalc_value_init(&o, c->bits);
    c->out_base = 0;

    if (strcmp(name, "diff") == 0 && count == 1 &&
        terminalc_is_scalar(&args[0])) {
        (void)terminalc_numeric_diff(c, expr_text, &args[0], &o);
    } else if (strcmp(name, "integral") == 0 && count == 2 &&
               terminalc_is_scalar(&args[0]) &&
               terminalc_is_scalar(&args[1])) {
        (void)terminalc_numeric_integral(c, expr_text, &args[0], &args[1], &o);
    } else if (strcmp(name, "solve") == 0 && count == 1 &&
               terminalc_is_scalar(&args[0])) {
        (void)terminalc_numeric_solve(c, expr_text, &args[0], &args[0],
                                      false, &o);
    } else if (strcmp(name, "solve") == 0 && count == 2 &&
               terminalc_is_scalar(&args[0]) &&
               terminalc_is_scalar(&args[1])) {
        (void)terminalc_numeric_solve(c, expr_text, &args[0], &args[1],
                                      true, &o);
    } else if ((strcmp(name, "sum") == 0 || strcmp(name, "sigma") == 0) &&
               count == 2) {
        long lo = 0, hi = 0;
        if (args[0].type == TERMINALC_RAT && terminalc_is_integer(&args[0]))
            lo = mpz_get_si(mpq_numref(args[0].q));
        else if (args[0].type == TERMINALC_REAL)
            lo = (long)mpfr_get_si(args[0].r, MPFR_RNDN);
        if (args[1].type == TERMINALC_RAT && terminalc_is_integer(&args[1]))
            hi = mpz_get_si(mpq_numref(args[1].q));
        else if (args[1].type == TERMINALC_REAL)
            hi = (long)mpfr_get_si(args[1].r, MPFR_RNDN);
        (void)terminalc_series_sum_expr(c, expr_text, lo, hi, &o);
    } else {
        terminalc_fail(c, "invalid expr-function call");
    }

    for (size_t i = 0; i < count; i++)
        terminalc_value_clear(&args[i]);
    return o;
}
