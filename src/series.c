/**
 * Series / progressions / primes for TerminalC.
 *
 * AP  arithmetic:  a, a+d, a+2d, ...
 * GP  geometric:   a, ar, ar^2, ...
 * HP  harmonic:    1/a, 1/(a+d), ...  (reciprocals of an AP)
 * SP  school sums: 1+..+n, 1^2+..+n^2, 1^3+..+n^3  (sumi/sumsq/sumcu)
 * Primes: isprime, primes, primes_to, nextprime, prevprime
 * Summation: sumrange(lo,hi), sum(expr, lo, hi) with free x
 */
#include "terminalc.h"
#include "parse_internal.h"

#include <math.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

enum { SERIES_LIST_MAX = 10000, PRIME_LIST_MAX = 10000, SUM_ITER_MAX = 1000000 };

static bool get_ulong(const TerminalCValue *v, unsigned long *out)
{
    if (!terminalc_is_integer(v) || mpz_sgn(mpq_numref(v->q)) < 0 ||
        !mpz_fits_ulong_p(mpq_numref(v->q)))
        return false;
    *out = mpz_get_ui(mpq_numref(v->q));
    return true;
}

static bool exact_power_allowed(TerminalCCalc *c, long base,
                                unsigned long exponent)
{
    mpz_t z;
    mpz_init_set_si(z, base);
    size_t base_bits = mpz_sizeinbase(z, 2);
    mpz_clear(z);
    if (exponent != 0 &&
        base_bits > TERMINALC_EXACT_RESULT_MAX_BITS / exponent) {
        terminalc_fail(c, "exact power result is too large");
        return false;
    }
    return true;
}

static bool get_slong(const TerminalCValue *v, long *out)
{
    if (!terminalc_is_integer(v) || !mpz_fits_slong_p(mpq_numref(v->q)))
        return false;
    *out = mpz_get_si(mpq_numref(v->q));
    return true;
}

static void set_mpz_result(TerminalCValue *o, mpz_t z)
{
    o->type = TERMINALC_RAT;
    mpz_set(mpq_numref(o->q), z);
    mpz_set_ui(mpq_denref(o->q), 1);
}

static void mpz_add_slong(mpz_t z, long value)
{
    mpz_t t;
    mpz_init_set_si(t, value);
    mpz_add(z, z, t);
    mpz_clear(t);
}

static bool make_vector_si(TerminalCCalc *c, TerminalCValue *out, size_t n,
                           void (*fill)(mpz_t, size_t, void *), void *ctx)
{
    if (n == 0 || n > SERIES_LIST_MAX) {
        terminalc_fail(c, "list length out of range (1..10000)");
        return false;
    }
    size_t sh[1] = {n};
    if (!terminalc_tensor_create(c, out, 1, sh))
        return false;
    mpz_t z;
    mpz_init(z);
    for (size_t i = 0; i < n; i++) {
        fill(z, i, ctx);
        out->ten->data[i].type = TERMINALC_RAT;
        mpz_set(mpq_numref(out->ten->data[i].q), z);
        mpz_set_ui(mpq_denref(out->ten->data[i].q), 1);
    }
    mpz_clear(z);
    return true;
}

/* ---- AP ---- */
/* ap_term(a, d, n)  T_n = a + (n-1)*d , n >= 1 */
/* ap_sum(a, d, n)   S_n = n/2 * (2a + (n-1)d) */
/* ap_list(a, d, n)  first n terms as vector */

typedef struct {
    long a, d;
} ApCtx;

static void ap_fill(mpz_t z, size_t i, void *ctx)
{
    ApCtx *p = (ApCtx *)ctx;
    mpz_set_ui(z, (unsigned long)i);
    mpz_mul_si(z, z, p->d);
    mpz_add_slong(z, p->a);
}

static bool ap_term(TerminalCCalc *c, TerminalCValue *args, size_t count,
                    TerminalCValue *o)
{
    long a, d, n;
    if (count != 3 || !get_slong(&args[0], &a) || !get_slong(&args[1], &d) ||
        !get_slong(&args[2], &n) || n < 1) {
        terminalc_fail(c, "ap_term(a, d, n) needs integers, n >= 1");
        return false;
    }
    mpz_t z;
    mpz_init_set_si(z, n - 1);
    mpz_mul_si(z, z, d);
    mpz_add_slong(z, a);
    set_mpz_result(o, z);
    mpz_clear(z);
    return true;
}

static bool ap_sum(TerminalCCalc *c, TerminalCValue *args, size_t count,
                   TerminalCValue *o)
{
    long a, d, n;
    if (count != 3 || !get_slong(&args[0], &a) || !get_slong(&args[1], &d) ||
        !get_slong(&args[2], &n) || n < 1) {
        terminalc_fail(c, "ap_sum(a, d, n) needs integers, n >= 1");
        return false;
    }
    /* S = n/2 * (2a + (n-1)d) exact */
    mpz_t num, term;
    mpz_inits(num, term, NULL);
    mpz_set_si(num, a);
    mpz_mul_ui(num, num, 2);
    mpz_set_si(term, n - 1);
    mpz_mul_si(term, term, d);
    mpz_add(num, num, term);
    mpz_mul_si(num, num, n);
    o->type = TERMINALC_RAT;
    mpz_set(mpq_numref(o->q), num);
    mpz_set_ui(mpq_denref(o->q), 2);
    mpq_canonicalize(o->q);
    mpz_clears(num, term, NULL);
    return true;
}

static bool ap_list(TerminalCCalc *c, TerminalCValue *args, size_t count,
                    TerminalCValue *o)
{
    long a, d, n;
    if (count != 3 || !get_slong(&args[0], &a) || !get_slong(&args[1], &d) ||
        !get_slong(&args[2], &n) || n < 1) {
        terminalc_fail(c, "ap_list(a, d, n) needs integers, n >= 1");
        return false;
    }
    ApCtx ctx = {a, d};
    return make_vector_si(c, o, (size_t)n, ap_fill, &ctx);
}

/* ---- GP ---- */
/* gp_term(a, r, n)  a * r^(n-1) */
/* gp_sum(a, r, n)   a*(r^n-1)/(r-1) or n*a if r=1 */
/* gp_list(a, r, n) */

typedef struct {
    long a, r;
} GpCtx;

static void gp_fill(mpz_t z, size_t i, void *ctx)
{
    GpCtx *p = (GpCtx *)ctx;
    mpz_set_si(z, p->a);
    if (i == 0)
        return;
    mpz_t rp, base;
    mpz_inits(rp, base, NULL);
    mpz_set_si(base, p->r);
    mpz_pow_ui(rp, base, (unsigned long)i);
    mpz_mul(z, z, rp);
    mpz_clears(rp, base, NULL);
}

static bool gp_term(TerminalCCalc *c, TerminalCValue *args, size_t count,
                    TerminalCValue *o)
{
    long a, r, n;
    if (count != 3 || !get_slong(&args[0], &a) || !get_slong(&args[1], &r) ||
        !get_slong(&args[2], &n) || n < 1) {
        terminalc_fail(c, "gp_term(a, r, n) needs integers, n >= 1");
        return false;
    }
    mpz_t z, rp, base;
    mpz_inits(z, rp, base, NULL);
    mpz_set_si(z, a);
    if (n > 1) {
        unsigned long e = (unsigned long)(n - 1);
        if (!exact_power_allowed(c, r, e)) {
            mpz_clears(z, rp, base, NULL);
            return false;
        }
        mpz_set_si(base, r);
        mpz_pow_ui(rp, base, e);
        mpz_mul(z, z, rp);
    }
    set_mpz_result(o, z);
    mpz_clears(z, rp, base, NULL);
    return true;
}

static bool gp_sum(TerminalCCalc *c, TerminalCValue *args, size_t count,
                   TerminalCValue *o)
{
    long a, r, n;
    if (count != 3 || !get_slong(&args[0], &a) || !get_slong(&args[1], &r) ||
        !get_slong(&args[2], &n) || n < 1) {
        terminalc_fail(c, "gp_sum(a, r, n) needs integers, n >= 1");
        return false;
    }
    if (r == 1) {
        mpz_t z;
        mpz_init_set_si(z, a);
        mpz_mul_si(z, z, n);
        set_mpz_result(o, z);
        mpz_clear(z);
        return true;
    }
    /* a * (r^n - 1) / (r - 1) */
    if (!exact_power_allowed(c, r, (unsigned long)n))
        return false;
    mpz_t rn, num, den, base;
    mpz_inits(rn, num, den, base, NULL);
    mpz_set_si(base, r);
    mpz_pow_ui(rn, base, (unsigned long)n);
    mpz_sub_ui(num, rn, 1);
    mpz_mul_si(num, num, a);
    mpz_set_si(den, r);
    mpz_sub_ui(den, den, 1);
    o->type = TERMINALC_RAT;
    mpz_set(mpq_numref(o->q), num);
    mpz_set(mpq_denref(o->q), den);
    mpq_canonicalize(o->q);
    mpz_clears(rn, num, den, base, NULL);
    return true;
}

static bool gp_list(TerminalCCalc *c, TerminalCValue *args, size_t count,
                    TerminalCValue *o)
{
    long a, r, n;
    if (count != 3 || !get_slong(&args[0], &a) || !get_slong(&args[1], &r) ||
        !get_slong(&args[2], &n) || n < 1) {
        terminalc_fail(c, "gp_list(a, r, n) needs integers, n >= 1");
        return false;
    }
    GpCtx ctx = {a, r};
    return make_vector_si(c, o, (size_t)n, gp_fill, &ctx);
}

/* ---- HP: harmonics of an AP ---- */
/* hp_term(a, d, n) = 1 / (a + (n-1)d) */
/* hp_list(a, d, n) */
/* hp_sum approximated numerically for n terms (no closed form generally) */

static bool hp_term(TerminalCCalc *c, TerminalCValue *args, size_t count,
                    TerminalCValue *o)
{
    long a, d, n;
    if (count != 3 || !get_slong(&args[0], &a) || !get_slong(&args[1], &d) ||
        !get_slong(&args[2], &n) || n < 1) {
        terminalc_fail(c, "hp_term(a, d, n) needs integers, n >= 1");
        return false;
    }
    mpz_t den;
    mpz_init_set_si(den, n - 1);
    mpz_mul_si(den, den, d);
    mpz_add_slong(den, a);
    if (mpz_sgn(den) == 0) {
        mpz_clear(den);
        terminalc_fail(c, "hp_term: zero denominator");
        return false;
    }
    o->type = TERMINALC_RAT;
    mpz_set_ui(mpq_numref(o->q), 1);
    mpz_set(mpq_denref(o->q), den);
    mpq_canonicalize(o->q);
    mpz_clear(den);
    return true;
}

static bool hp_list(TerminalCCalc *c, TerminalCValue *args, size_t count,
                    TerminalCValue *o)
{
    long a, d, n;
    if (count != 3 || !get_slong(&args[0], &a) || !get_slong(&args[1], &d) ||
        !get_slong(&args[2], &n) || n < 1) {
        terminalc_fail(c, "hp_list(a, d, n) needs integers, n >= 1");
        return false;
    }
    if ((size_t)n > SERIES_LIST_MAX) {
        terminalc_fail(c, "list length out of range");
        return false;
    }
    size_t sh[1] = {(size_t)n};
    if (!terminalc_tensor_create(c, o, 1, sh))
        return false;
    mpz_t den;
    mpz_init(den);
    for (long i = 0; i < n; i++) {
        mpz_set_si(den, i);
        mpz_mul_si(den, den, d);
        mpz_add_slong(den, a);
        if (mpz_sgn(den) == 0) {
            mpz_clear(den);
            terminalc_fail(c, "hp_list: zero denominator");
            return false;
        }
        o->ten->data[i].type = TERMINALC_RAT;
        mpz_set_ui(mpq_numref(o->ten->data[i].q), 1);
        mpz_set(mpq_denref(o->ten->data[i].q), den);
        mpq_canonicalize(o->ten->data[i].q);
    }
    mpz_clear(den);
    return true;
}

static bool hp_sum(TerminalCCalc *c, TerminalCValue *args, size_t count,
                   TerminalCValue *o)
{
    long a, d, n;
    if (count != 3 || !get_slong(&args[0], &a) || !get_slong(&args[1], &d) ||
        !get_slong(&args[2], &n) || n < 1) {
        terminalc_fail(c, "hp_sum(a, d, n) needs integers, n >= 1");
        return false;
    }
    mpq_t s, t;
    mpq_inits(s, t, NULL);
    mpq_set_ui(s, 0, 1);
    mpz_t den;
    mpz_init(den);
    for (long i = 0; i < n; i++) {
        mpz_set_si(den, i);
        mpz_mul_si(den, den, d);
        mpz_add_slong(den, a);
        if (mpz_sgn(den) == 0) {
            mpz_clear(den);
            mpq_clears(s, t, NULL);
            terminalc_fail(c, "hp_sum: zero denominator");
            return false;
        }
        mpz_set_ui(mpq_numref(t), 1);
        mpz_set(mpq_denref(t), den);
        mpq_canonicalize(t);
        mpq_add(s, s, t);
    }
    o->type = TERMINALC_RAT;
    mpq_set(o->q, s);
    mpz_clear(den);
    mpq_clears(s, t, NULL);
    return true;
}

/* ---- SP: school summation formulas (Sum of first n naturals / powers) ---- */
/* sumi(n) = n(n+1)/2 */
/* sumsq(n) = n(n+1)(2n+1)/6 */
/* sumcu(n) = [n(n+1)/2]^2 */
/* sumrange(lo, hi) inclusive */

static bool sumi(TerminalCCalc *c, TerminalCValue *args, size_t count,
                 TerminalCValue *o)
{
    long n;
    if (count != 1 || !get_slong(&args[0], &n) || n < 0) {
        terminalc_fail(c, "sumi(n) needs integer n >= 0");
        return false;
    }
    mpz_t z;
    mpz_init_set_si(z, n);
    mpz_add_ui(mpq_denref(o->q), z, 1);
    mpz_mul(mpq_numref(o->q), z, mpq_denref(o->q));
    mpz_set_ui(mpq_denref(o->q), 2);
    mpq_canonicalize(o->q);
    o->type = TERMINALC_RAT;
    mpz_clear(z);
    return true;
}

static bool sumsq(TerminalCCalc *c, TerminalCValue *args, size_t count,
                  TerminalCValue *o)
{
    long n;
    if (count != 1 || !get_slong(&args[0], &n) || n < 0) {
        terminalc_fail(c, "sumsq(n) needs integer n >= 0");
        return false;
    }
    /* n(n+1)(2n+1)/6 — use mpz for safety */
    mpz_t z;
    mpz_init(z);
    mpz_t t;
    mpz_init(t);
    mpz_set_si(z, n);
    mpz_add_ui(t, z, 1);
    mpz_mul(z, z, t);
    mpz_set_si(t, n);
    mpz_mul_ui(t, t, 2);
    mpz_add_ui(t, t, 1);
    mpz_mul(z, z, t);
    o->type = TERMINALC_RAT;
    mpz_set(mpq_numref(o->q), z);
    mpz_set_ui(mpq_denref(o->q), 6);
    mpq_canonicalize(o->q);
    mpz_clears(z, t, NULL);
    return true;
}

static bool sumcu(TerminalCCalc *c, TerminalCValue *args, size_t count,
                  TerminalCValue *o)
{
    long n;
    if (count != 1 || !get_slong(&args[0], &n) || n < 0) {
        terminalc_fail(c, "sumcu(n) needs integer n >= 0");
        return false;
    }
    /* [n(n+1)/2]^2 */
    mpz_t z;
    mpz_init(z);
    mpz_t t;
    mpz_init(t);
    mpz_set_si(z, n);
    mpz_add_ui(t, z, 1);
    mpz_mul(z, z, t);
    mpz_fdiv_q_ui(z, z, 2);
    mpz_mul(z, z, z);
    set_mpz_result(o, z);
    mpz_clears(z, t, NULL);
    return true;
}

static bool sumrange(TerminalCCalc *c, TerminalCValue *args, size_t count,
                     TerminalCValue *o)
{
    long lo, hi;
    if (count != 2 || !get_slong(&args[0], &lo) || !get_slong(&args[1], &hi)) {
        terminalc_fail(c, "sumrange(lo, hi) needs integers");
        return false;
    }
    if (hi < lo) {
        long t = lo;
        lo = hi;
        hi = t;
    }
    mpz_t term_count, endpoints;
    mpz_inits(term_count, endpoints, NULL);
    mpz_set_si(term_count, hi);
    mpz_set_si(endpoints, lo);
    mpz_sub(term_count, term_count, endpoints);
    mpz_add_ui(term_count, term_count, 1);
    mpz_set_si(endpoints, lo);
    mpz_add_slong(endpoints, hi);
    mpz_mul(mpq_numref(o->q), term_count, endpoints);
    mpz_set_ui(mpq_denref(o->q), 2);
    mpq_canonicalize(o->q);
    o->type = TERMINALC_RAT;
    mpz_clears(term_count, endpoints, NULL);
    return true;
}

/* sum(expr, lo, hi) with free x — integer steps */
bool terminalc_series_sum_expr(TerminalCCalc *c, const char *expr, long lo,
                               long hi, TerminalCValue *out)
{
    if (hi < lo) {
        long t = lo;
        lo = hi;
        hi = t;
    }
    mpz_t steps, lo_value;
    mpz_init_set_si(steps, hi);
    mpz_init_set_si(lo_value, lo);
    mpz_sub(steps, steps, lo_value);
    mpz_add_ui(steps, steps, 1);
    if (mpz_sgn(steps) < 1 || mpz_cmp_ui(steps, SUM_ITER_MAX) > 0) {
        mpz_clears(steps, lo_value, NULL);
        terminalc_fail(c, "sum: range too large (max 1000000 terms)");
        return false;
    }
    mpz_clears(steps, lo_value, NULL);
    mpfr_t s, y;
    mpfr_init2(s, c->bits);
    mpfr_init2(y, c->bits);
    mpfr_set_ui(s, 0, MPFR_RNDN);
    TerminalCValue saved_x;
    terminalc_value_init(&saved_x, c->bits);
    terminalc_value_copy(&saved_x, &c->x);
    for (long k = lo;; k++) {
        if (terminalc_interrupt_requested()) {
            terminalc_fail(c, "calculation interrupted by Ctrl-C");
            terminalc_value_copy(&c->x, &saved_x);
            terminalc_value_clear(&saved_x);
            mpfr_clear(s);
            mpfr_clear(y);
            return false;
        }
        c->x.type = TERMINALC_RAT;
        mpq_set_si(c->x.q, k, 1);
        TerminalCValue v;
        if (!terminalc_eval_nested(c, expr, &v)) {
            terminalc_value_clear(&v);
            terminalc_value_copy(&c->x, &saved_x);
            terminalc_value_clear(&saved_x);
            mpfr_clear(s);
            mpfr_clear(y);
            return false;
        }
        terminalc_as_real(c, y, &v);
        mpfr_add(s, s, y, MPFR_RNDN);
        terminalc_value_clear(&v);
        if (k == hi)
            break;
    }
    out->type = TERMINALC_REAL;
    mpfr_set(out->r, s, MPFR_RNDN);
    terminalc_value_copy(&c->x, &saved_x);
    terminalc_value_clear(&saved_x);
    mpfr_clear(s);
    mpfr_clear(y);
    return true;
}

/* ---- Primes ---- */

static bool is_prime_ul(unsigned long n)
{
    if (n < 2)
        return false;
    if (n < 4)
        return true;
    if (n % 2 == 0 || n % 3 == 0)
        return false;
    for (unsigned long i = 5; i <= n / i; i += 6) {
        if (n % i == 0 || n % (i + 2) == 0)
            return false;
    }
    return true;
}

static bool isprime_fn(TerminalCCalc *c, TerminalCValue *args, size_t count,
                       TerminalCValue *o)
{
    long n;
    if (count != 1 || !get_slong(&args[0], &n)) {
        terminalc_fail(c, "isprime(n) needs an integer");
        return false;
    }
#if LONG_MAX > 4000000000LL
    if (n > 4000000000L) {
        terminalc_fail(c, "isprime: n must be at most 4000000000");
        return false;
    }
#endif
    terminalc_value_set_bool(o, n > 1 && is_prime_ul((unsigned long)n));
    return true;
}

static bool nextprime_fn(TerminalCCalc *c, TerminalCValue *args, size_t count,
                         TerminalCValue *o)
{
    long n;
    if (count != 1 || !get_slong(&args[0], &n)) {
        terminalc_fail(c, "nextprime(n) needs an integer");
        return false;
    }
    if (n < 2) {
        terminalc_value_set_si(o, 2);
        return true;
    }
    if ((unsigned long)n >= 4000000000UL) {
        terminalc_fail(c, "nextprime: n must be below 4000000000");
        return false;
    }
    unsigned long p = (unsigned long)n + 1;
    while (!is_prime_ul(p)) {
        p++;
        if (p > 4000000000UL) {
            terminalc_fail(c, "nextprime: overflow");
            return false;
        }
    }
    o->type = TERMINALC_RAT;
    mpz_set_ui(mpq_numref(o->q), p);
    mpz_set_ui(mpq_denref(o->q), 1);
    return true;
}

static bool prevprime_fn(TerminalCCalc *c, TerminalCValue *args, size_t count,
                         TerminalCValue *o)
{
    long n;
    if (count != 1 || !get_slong(&args[0], &n)) {
        terminalc_fail(c, "prevprime(n) needs an integer");
        return false;
    }
    if (n <= 2) {
        terminalc_fail(c, "prevprime: no prime < 2");
        return false;
    }
    unsigned long p = (unsigned long)n - 1;
    while (p >= 2 && !is_prime_ul(p))
        p--;
    if (p < 2) {
        terminalc_fail(c, "prevprime: none");
        return false;
    }
    terminalc_value_set_si(o, (long)p);
    return true;
}

/* primes(n) = first n primes as vector */
static bool primes_fn(TerminalCCalc *c, TerminalCValue *args, size_t count,
                      TerminalCValue *o)
{
    unsigned long n;
    if (count != 1 || !get_ulong(&args[0], &n) || n < 1 || n > PRIME_LIST_MAX) {
        terminalc_fail(c, "primes(n): n in 1..10000");
        return false;
    }
    size_t sh[1] = {n};
    if (!terminalc_tensor_create(c, o, 1, sh))
        return false;
    unsigned long p = 2;
    for (unsigned long i = 0; i < n; i++) {
        while (!is_prime_ul(p))
            p++;
        o->ten->data[i].type = TERMINALC_RAT;
        mpz_set_ui(mpq_numref(o->ten->data[i].q), p);
        mpz_set_ui(mpq_denref(o->ten->data[i].q), 1);
        p++;
    }
    return true;
}

/* primes_to(limit) = all primes <= limit */
static bool primes_to_fn(TerminalCCalc *c, TerminalCValue *args, size_t count,
                         TerminalCValue *o)
{
    long lim;
    if (count != 1 || !get_slong(&args[0], &lim) || lim < 2) {
        terminalc_fail(c, "primes_to(limit) needs integer limit >= 2");
        return false;
    }
    if (lim > 2000000) {
        terminalc_fail(c, "primes_to: limit max 2000000");
        return false;
    }
    /* sieve */
    size_t L = (size_t)lim + 1;
    unsigned char *sieve = (unsigned char *)calloc(L, 1);
    if (!sieve) {
        terminalc_fail(c, "out of memory");
        return false;
    }
    size_t cnt = 0;
    for (size_t i = 2; i < L; i++) {
        if (!sieve[i]) {
            cnt++;
            if (i * i < L)
                for (size_t j = i * i; j < L; j += i)
                    sieve[j] = 1;
        }
    }
    size_t sh[1] = {cnt};
    if (!terminalc_tensor_create(c, o, 1, sh)) {
        free(sieve);
        return false;
    }
    size_t k = 0;
    for (size_t i = 2; i < L; i++) {
        if (!sieve[i]) {
            o->ten->data[k].type = TERMINALC_RAT;
            mpz_set_ui(mpq_numref(o->ten->data[k].q), (unsigned long)i);
            mpz_set_ui(mpq_denref(o->ten->data[k].q), 1);
            k++;
        }
    }
    free(sieve);
    return true;
}

/* Dispatch from functions.c */
bool terminalc_series_call(TerminalCCalc *c, const char *n, TerminalCValue *args,
                           size_t count, TerminalCValue *o)
{
    if (strcmp(n, "ap_term") == 0 || strcmp(n, "ap_nth") == 0)
        return ap_term(c, args, count, o);
    if (strcmp(n, "ap_sum") == 0)
        return ap_sum(c, args, count, o);
    if (strcmp(n, "ap_list") == 0)
        return ap_list(c, args, count, o);
    if (strcmp(n, "gp_term") == 0 || strcmp(n, "gp_nth") == 0)
        return gp_term(c, args, count, o);
    if (strcmp(n, "gp_sum") == 0)
        return gp_sum(c, args, count, o);
    if (strcmp(n, "gp_list") == 0)
        return gp_list(c, args, count, o);
    if (strcmp(n, "hp_term") == 0 || strcmp(n, "hp_nth") == 0)
        return hp_term(c, args, count, o);
    if (strcmp(n, "hp_list") == 0)
        return hp_list(c, args, count, o);
    if (strcmp(n, "hp_sum") == 0)
        return hp_sum(c, args, count, o);
    if (strcmp(n, "sumi") == 0 || strcmp(n, "sp_sum") == 0)
        return sumi(c, args, count, o);
    if (strcmp(n, "sumsq") == 0 || strcmp(n, "sp_sumsq") == 0)
        return sumsq(c, args, count, o);
    if (strcmp(n, "sumcu") == 0 || strcmp(n, "sp_sumcu") == 0)
        return sumcu(c, args, count, o);
    if (strcmp(n, "sumrange") == 0)
        return sumrange(c, args, count, o);
    if (strcmp(n, "isprime") == 0)
        return isprime_fn(c, args, count, o);
    if (strcmp(n, "nextprime") == 0)
        return nextprime_fn(c, args, count, o);
    if (strcmp(n, "prevprime") == 0)
        return prevprime_fn(c, args, count, o);
    if (strcmp(n, "primes") == 0)
        return primes_fn(c, args, count, o);
    if (strcmp(n, "primes_to") == 0 || strcmp(n, "primes_upto") == 0)
        return primes_to_fn(c, args, count, o);
    return false; /* not handled */
}
