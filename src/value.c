#include "terminalc.h"
#include "color.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* domains.c */
void terminalc_print_measure(const TerminalCCalc *c, const TerminalCValue *v);

void terminalc_value_init(TerminalCValue *v, mpfr_prec_t bits)
{
    v->type = TERMINALC_RAT;
    v->b = false;
    v->display_decimal = false;
    v->ten = NULL;
    v->unit_id = -1;
    mpq_init(v->q);
    mpfr_init2(v->r, bits);
}

void terminalc_value_clear(TerminalCValue *v)
{
    if (v->type == TERMINALC_TENSOR && v->ten) {
        terminalc_tensor_free(v->ten);
        v->ten = NULL;
    }
    mpq_clear(v->q);
    mpfr_clear(v->r);
    v->b = false;
    v->display_decimal = false;
    v->unit_id = -1;
    v->type = TERMINALC_RAT;
}

void terminalc_value_set_q(TerminalCValue *v, const mpq_t q)
{
    if (v->type == TERMINALC_TENSOR && v->ten) {
        terminalc_tensor_free(v->ten);
        v->ten = NULL;
    }
    v->type = TERMINALC_RAT;
    v->b = false;
    v->display_decimal = false;
    v->unit_id = -1;
    mpq_set(v->q, q);
    mpq_canonicalize(v->q);
}

void terminalc_value_set_si(TerminalCValue *v, long n)
{
    if (v->type == TERMINALC_TENSOR && v->ten) {
        terminalc_tensor_free(v->ten);
        v->ten = NULL;
    }
    v->type = TERMINALC_RAT;
    v->b = false;
    v->display_decimal = false;
    v->unit_id = -1;
    mpq_set_si(v->q, n, 1);
}

void terminalc_value_set_r(TerminalCValue *v, const mpfr_t r)
{
    if (v->type == TERMINALC_TENSOR && v->ten) {
        terminalc_tensor_free(v->ten);
        v->ten = NULL;
    }
    v->type = TERMINALC_REAL;
    v->b = false;
    v->display_decimal = false;
    v->unit_id = -1;
    mpfr_set(v->r, r, MPFR_RNDN);
}

void terminalc_value_set_bool(TerminalCValue *v, bool b)
{
    if (v->type == TERMINALC_TENSOR && v->ten) {
        terminalc_tensor_free(v->ten);
        v->ten = NULL;
    }
    v->type = TERMINALC_BOOL;
    v->b = b;
    v->display_decimal = false;
    v->unit_id = -1;
}

void terminalc_value_copy(TerminalCValue *dst, const TerminalCValue *src)
{
    if (src->type == TERMINALC_RAT)
        terminalc_value_set_q(dst, src->q);
    else if (src->type == TERMINALC_REAL) {
        terminalc_value_set_r(dst, src->r);
        dst->display_decimal = src->display_decimal;
    } else if (src->type == TERMINALC_BOOL)
        terminalc_value_set_bool(dst, src->b);
    else if (src->type == TERMINALC_MEASURE) {
        if (dst->type == TERMINALC_TENSOR && dst->ten) {
            terminalc_tensor_free(dst->ten);
            dst->ten = NULL;
        }
        dst->type = TERMINALC_MEASURE;
        dst->unit_id = src->unit_id;
        dst->b = false;
        dst->display_decimal = false;
        mpq_set(dst->q, src->q);
        mpfr_set(dst->r, src->r, MPFR_RNDN);
    } else if (src->type == TERMINALC_TENSOR && src->ten) {
        size_t sh[TERMINALC_TENSOR_MAX_RANK] = {0};
        for (int i = 0; i < src->ten->rank; i++)
            sh[i] = src->ten->shape[i];
        /* shallow recreate */
        TerminalCCalc fake;
        memset(&fake, 0, sizeof fake);
        fake.bits = mpfr_get_prec(dst->r);
        if (!terminalc_tensor_create(&fake, dst, src->ten->rank, sh)) {
            terminalc_value_set_si(dst, 0);
            return;
        }
        for (size_t i = 0; i < src->ten->n; i++)
            terminalc_value_copy(&dst->ten->data[i], &src->ten->data[i]);
    } else {
        terminalc_value_set_si(dst, 0);
    }
}

void terminalc_as_real(const TerminalCCalc *c, mpfr_t out, const TerminalCValue *v)
{
    (void)c;
    if (v->type == TERMINALC_REAL || v->type == TERMINALC_MEASURE) {
        if (v->type == TERMINALC_MEASURE && mpfr_zero_p(v->r) && mpq_sgn(v->q) != 0)
            mpfr_set_q(out, v->q, MPFR_RNDN);
        else if (v->type == TERMINALC_MEASURE && !mpfr_zero_p(v->r))
            mpfr_set(out, v->r, MPFR_RNDN);
        else if (v->type == TERMINALC_REAL)
            mpfr_set(out, v->r, MPFR_RNDN);
        else
            mpfr_set_q(out, v->q, MPFR_RNDN);
    } else if (v->type == TERMINALC_BOOL)
        mpfr_set_si(out, v->b ? 1 : 0, MPFR_RNDN);
    else if (v->type == TERMINALC_RAT)
        mpfr_set_q(out, v->q, MPFR_RNDN);
    else
        mpfr_set_ui(out, 0, MPFR_RNDN);
}

bool terminalc_is_integer(const TerminalCValue *v)
{
    return v->type == TERMINALC_RAT && mpz_cmp_ui(mpq_denref(v->q), 1) == 0;
}

bool terminalc_is_scalar(const TerminalCValue *v)
{
    return v->type == TERMINALC_RAT || v->type == TERMINALC_REAL || v->type == TERMINALC_BOOL ||
           v->type == TERMINALC_MEASURE;
}

bool terminalc_is_truthy(const TerminalCValue *v)
{
    if (v->type == TERMINALC_BOOL)
        return v->b;
    if (v->type == TERMINALC_RAT)
        return mpq_sgn(v->q) != 0;
    if (v->type == TERMINALC_REAL)
        return !mpfr_zero_p(v->r) && !mpfr_nan_p(v->r);
    if (v->type == TERMINALC_MEASURE)
        return mpq_sgn(v->q) != 0 || !mpfr_zero_p(v->r);
    return false;
}

static void print_q(const mpq_t q)
{
    char *text = mpq_get_str(NULL, 10, q);
    if (text) {
        fputs(text, stdout);
        free(text);
    }
}

/* True if |x| rounds to 0 under fixed-point display with `digits` places. */
static bool rounds_to_display_zero(const mpfr_t x, int digits)
{
    if (mpfr_zero_p(x) || mpfr_nan_p(x) || mpfr_inf_p(x))
        return mpfr_zero_p(x);

    mpfr_t ax, thr;
    mpfr_init2(ax, mpfr_get_prec(x));
    mpfr_init2(thr, mpfr_get_prec(x));
    mpfr_abs(ax, x, MPFR_RNDN);
    /* threshold = 0.5 * 10^(-digits) */
    mpfr_set_si(thr, -digits, MPFR_RNDN);
    mpfr_exp10(thr, thr, MPFR_RNDN);
    mpfr_div_ui(thr, thr, 2, MPFR_RNDN);
    int cmp = mpfr_cmp(ax, thr);
    mpfr_clear(ax);
    mpfr_clear(thr);
    return cmp < 0;
}

/* Integers (and exact 0) never use fixed-point padding from `precision`. */
static void print_plain_zero(void)
{
    printf("%s0%s\n", C_B_GRN, C_RST);
}

static void print_plain_integer_str(const char *s)
{
    printf("%s%s%s\n", C_B_GRN, s, C_RST);
}

static void print_plain_mpz(const mpz_t z)
{
    char *text = mpz_get_str(NULL, 10, z);
    if (text) {
        print_plain_integer_str(text);
        free(text);
    } else {
        print_plain_zero();
    }
}

static void print_decimal_real(const TerminalCCalc *c, const mpfr_t r)
{
    /* True zeros stay "0" — precision does not invent 0.000… */
    if (mpfr_zero_p(r) || rounds_to_display_zero(r, c->digits)) {
        print_plain_zero();
        return;
    }
    mpfr_printf("%s%.*RNf%s\n", C_B_GRN, c->digits, r, C_RST);
}

/* True if s is an optional sign + decimal digits only (no slash / radical). */
static bool is_plain_integer_str(const char *s)
{
    if (!s || !*s)
        return false;
    if (*s == '+' || *s == '-')
        s++;
    if (!*s)
        return false;
    for (const char *p = s; *p; p++) {
        if (!isdigit((unsigned char)*p))
            return false;
    }
    return true;
}

/* Tolerance for display-level nice-form matching: half a unit in the last digit. */
static void set_match_tol(mpfr_t tol, int digits, mpfr_prec_t bits)
{
    (void)bits;
    int d = digits > 0 ? digits : 1;
    if (d > 40)
        d = 40; /* avoid pathological false matches at huge precision */
    mpfr_set_si(tol, -d, MPFR_RNDN);
    mpfr_exp10(tol, tol, MPFR_RNDN);
    mpfr_div_ui(tol, tol, 2, MPFR_RNDN);
}

/* Tighter tolerance for recovering a genuine rational or closed form. */
static void set_recovery_tol(mpfr_t tol, int digits, mpfr_prec_t bits)
{
    (void)bits;
    int d = digits + 4;
    if (d < 1)
        d = 1;
    if (d > 40)
        d = 40;
    mpfr_set_si(tol, -d, MPFR_RNDN);
    mpfr_exp10(tol, tol, MPFR_RNDN);
    mpfr_div_ui(tol, tol, 2, MPFR_RNDN);
}

static bool nearly_equal(const mpfr_t a, const mpfr_t b, const mpfr_t tol)
{
    mpfr_t d;
    mpfr_init2(d, mpfr_get_prec(a));
    mpfr_sub(d, a, b, MPFR_RNDN);
    mpfr_abs(d, d, MPFR_RNDN);
    int ok = mpfr_cmp(d, tol) <= 0;
    mpfr_clear(d);
    return ok != 0;
}

/*
 * If x is an integer within the current display tolerance, write it to z_out.
 * Used so log(1000) → 3 (not 3.000… with precision padding).
 */
static bool real_as_integer(const TerminalCCalc *c, const mpfr_t x, mpz_t z_out)
{
    if (mpfr_nan_p(x) || mpfr_inf_p(x))
        return false;
    if (mpfr_zero_p(x) || rounds_to_display_zero(x, c->digits)) {
        mpz_set_ui(z_out, 0);
        return true;
    }

    mpfr_t nearest, tol;
    mpfr_init2(nearest, c->bits);
    mpfr_init2(tol, c->bits);
    mpfr_round(nearest, x);
    set_match_tol(tol, c->digits, c->bits);
    bool ok = nearly_equal(x, nearest, tol);
    if (ok)
        mpfr_get_z(z_out, nearest, MPFR_RNDN);
    mpfr_clear(nearest);
    mpfr_clear(tol);
    return ok;
}

/* Recover p/q via continued fractions; max |denominator| = max_den. */
static bool rat_from_real(const mpfr_t x, mpq_t out, unsigned long max_den,
                         const mpfr_t tol)
{
    if (mpfr_nan_p(x) || mpfr_inf_p(x))
        return false;

    mpfr_t v, inv, af;
    mpfr_init2(v, mpfr_get_prec(x));
    mpfr_init2(inv, mpfr_get_prec(x));
    mpfr_init2(af, mpfr_get_prec(x));
    mpfr_set(v, x, MPFR_RNDN);

    mpz_t a, h0, h1, k0, k1, h, k, t;
    mpz_inits(a, h0, h1, k0, k1, h, k, t, NULL);
    mpz_set_ui(h0, 0);
    mpz_set_ui(h1, 1);
    mpz_set_ui(k0, 1);
    mpz_set_ui(k1, 0);

    bool found = false;
    for (int i = 0; i < 64; i++) {
        mpfr_floor(af, v);
        if (!mpfr_fits_slong_p(af, MPFR_RNDN))
            break;
        long al = mpfr_get_si(af, MPFR_RNDN);
        mpz_set_si(a, al);

        /* h = a*h1 + h0; k = a*k1 + k0 */
        mpz_mul(h, a, h1);
        mpz_add(h, h, h0);
        mpz_mul(k, a, k1);
        mpz_add(k, k, k0);

        if (mpz_cmp_ui(k, max_den) > 0)
            break;

        mpz_set(h0, h1);
        mpz_set(k0, k1);
        mpz_set(h1, h);
        mpz_set(k1, k);

        if (mpz_sgn(k) != 0) {
            mpq_set_num(out, h);
            mpq_set_den(out, k);
            mpq_canonicalize(out);

            mpfr_t cand;
            mpfr_init2(cand, mpfr_get_prec(x));
            mpfr_set_q(cand, out, MPFR_RNDN);
            if (nearly_equal(x, cand, tol)) {
                found = true;
                mpfr_clear(cand);
                break;
            }
            mpfr_clear(cand);
        }

        mpfr_sub(v, v, af, MPFR_RNDN);
        if (mpfr_zero_p(v)) {
            found = true;
            break;
        }
        mpfr_ui_div(inv, 1, v, MPFR_RNDN);
        mpfr_set(v, inv, MPFR_RNDN);
    }

    mpz_clears(a, h0, h1, k0, k1, h, k, t, NULL);
    mpfr_clear(v);
    mpfr_clear(inv);
    mpfr_clear(af);
    return found;
}

static bool is_square_free_ok(unsigned n)
{
    /* Skip perfect squares; allow other small n. */
    for (unsigned i = 2; i * i <= n; i++) {
        if (n % (i * i) == 0)
            return false;
    }
    return n >= 2;
}

/*
 * Try forms:
 *   p/q
 *   p*sqrt(n)/q
 *   p/(q*sqrt(n))
 * Write ASCII math into buf (e.g. "1/2", "sqrt(2)/2", "1/sqrt(2)").
 */
static bool try_nice_form(const TerminalCCalc *c, const mpfr_t x, char *buf, size_t buflen)
{
    if (buflen < 8)
        return false;
    if (mpfr_nan_p(x) || mpfr_inf_p(x))
        return false;

    mpfr_t tol, cand, s, tmp;
    mpfr_init2(tol, c->bits);
    mpfr_init2(cand, c->bits);
    mpfr_init2(s, c->bits);
    mpfr_init2(tmp, c->bits);
    set_recovery_tol(tol, c->digits, c->bits);

    /* Exact / display zero */
    if (rounds_to_display_zero(x, c->digits) || mpfr_zero_p(x)) {
        snprintf(buf, buflen, "0");
        mpfr_clears(tol, cand, s, tmp, (mpfr_ptr)0);
        return true;
    }

    /* ±1 */
    mpfr_set_si(cand, 1, MPFR_RNDN);
    if (mpfr_integer_p(x) && mpfr_cmp_ui(x, 1) == 0) {
        snprintf(buf, buflen, "1");
        mpfr_clears(tol, cand, s, tmp, (mpfr_ptr)0);
        return true;
    }
    mpfr_set_si(cand, -1, MPFR_RNDN);
    if (mpfr_integer_p(x) && mpfr_cmp_si(x, -1) == 0) {
        snprintf(buf, buflen, "-1");
        mpfr_clears(tol, cand, s, tmp, (mpfr_ptr)0);
        return true;
    }

    /* Rational p/q with small denominator */
    mpq_t q;
    mpq_init(q);
    if (rat_from_real(x, q, 10000UL, tol)) {
        if (mpz_cmp_ui(mpq_denref(q), 1) != 0 || mpfr_integer_p(x)) {
            char *t = mpq_get_str(NULL, 10, q);
            if (t) {
                snprintf(buf, buflen, "%s", t);
                free(t);
                mpq_clear(q);
                mpfr_clears(tol, cand, s, tmp, (mpfr_ptr)0);
                return true;
            }
        }
    }
    mpq_clear(q);

    /* Algebraic: a*sqrt(n)/b  and  a/(b*sqrt(n)) for small integers */
    int sign = mpfr_sgn(x) < 0 ? -1 : 1;
    mpfr_abs(tmp, x, MPFR_RNDN);

    /* Prefer a*sqrt(n)/b (e.g. sqrt(2)/2) over a/(b*sqrt(n)) (e.g. 1/sqrt(2)). */
    for (unsigned n = 2; n <= 32; n++) {
        if (!is_square_free_ok(n))
            continue;
        mpfr_sqrt_ui(s, n, MPFR_RNDN);
        for (unsigned b = 1; b <= 24; b++) {
            for (int a = 1; a <= 24; a++) {
                mpfr_mul_ui(cand, s, (unsigned long)a, MPFR_RNDN);
                mpfr_div_ui(cand, cand, b, MPFR_RNDN);
                if (nearly_equal(tmp, cand, tol)) {
                    if (a == 1 && b == 1)
                        snprintf(buf, buflen, "%ssqrt(%u)", sign < 0 ? "-" : "", n);
                    else if (a == 1)
                        snprintf(buf, buflen, "%ssqrt(%u)/%u", sign < 0 ? "-" : "", n,
                                 b);
                    else if (b == 1)
                        snprintf(buf, buflen, "%s%d*sqrt(%u)", sign < 0 ? "-" : "", a,
                                 n);
                    else
                        snprintf(buf, buflen, "%s%d*sqrt(%u)/%u", sign < 0 ? "-" : "",
                                 a, n, b);
                    mpfr_clears(tol, cand, s, tmp, (mpfr_ptr)0);
                    return true;
                }
            }
        }
    }
    for (unsigned n = 2; n <= 32; n++) {
        if (!is_square_free_ok(n))
            continue;
        mpfr_sqrt_ui(s, n, MPFR_RNDN);
        for (unsigned b = 1; b <= 24; b++) {
            for (int a = 1; a <= 24; a++) {
                mpfr_mul_ui(cand, s, b, MPFR_RNDN);
                mpfr_ui_div(cand, (unsigned long)a, cand, MPFR_RNDN);
                if (nearly_equal(tmp, cand, tol)) {
                    if (a == 1 && b == 1)
                        snprintf(buf, buflen, "%s1/sqrt(%u)", sign < 0 ? "-" : "", n);
                    else if (b == 1)
                        snprintf(buf, buflen, "%s%d/sqrt(%u)", sign < 0 ? "-" : "", a,
                                 n);
                    else if (a == 1)
                        snprintf(buf, buflen, "%s1/(%u*sqrt(%u))",
                                 sign < 0 ? "-" : "", b, n);
                    else
                        snprintf(buf, buflen, "%s%d/(%u*sqrt(%u))",
                                 sign < 0 ? "-" : "", a, b, n);
                    mpfr_clears(tol, cand, s, tmp, (mpfr_ptr)0);
                    return true;
                }
            }
        }
    }

    mpfr_clears(tol, cand, s, tmp, (mpfr_ptr)0);
    return false;
}

/* Print integer in base 2–36 with a conventional prefix when applicable. */
static void print_integer_base(const TerminalCCalc *c, const mpz_t z, int base)
{
    mpz_t abs;
    mpz_init(abs);
    mpz_abs(abs, z);
    char *text = mpz_get_str(NULL, base, abs);
    mpz_clear(abs);
    if (!text) {
        puts("?");
        return;
    }
    /* Uppercase A–Z for bases > 10 (hex etc.). */
    for (char *p = text; *p; p++) {
        if (*p >= 'a' && *p <= 'z')
            *p = (char)(*p - 'a' + 'A');
    }
    printf("%s", C_B_GRN);
    if (mpz_sgn(z) < 0)
        putchar('-');
    if (base == 16)
        fputs("0x", stdout);
    else if (base == 2)
        fputs("0b", stdout);
    else if (base == 8)
        fputs("0o", stdout);
    else if (base != 10)
        printf("(base %d) ", base);
    fputs(text, stdout);
    printf("%s\n", C_RST);
    free(text);
    (void)c;
}

static void print_fraction_and_decimal(const TerminalCCalc *c, const char *exact_label,
                                       const char *exact_text, const mpfr_t r)
{
    printf("%s%s:%s  %s%s%s\n", C_B_CYN, exact_label, C_RST, C_B_GRN, exact_text,
           C_RST);
    printf("%sDecimal:%s ", C_DIM, C_RST);
    print_decimal_real(c, r);
    printf("%s[%d digits, round-to-nearest]%s\n", C_DIM, c->digits, C_RST);
}

static void print_json_string(const char *text)
{
    putchar('"');
    if (text) {
        for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
            switch (*p) {
            case '"': fputs("\\\"", stdout); break;
            case '\\': fputs("\\\\", stdout); break;
            case '\b': fputs("\\b", stdout); break;
            case '\f': fputs("\\f", stdout); break;
            case '\n': fputs("\\n", stdout); break;
            case '\r': fputs("\\r", stdout); break;
            case '\t': fputs("\\t", stdout); break;
            default:
                if (*p < 0x20)
                    printf("\\u%04x", (unsigned)*p);
                else
                    putchar(*p);
                break;
            }
        }
    }
    putchar('"');
}

static char *mpfr_text(const TerminalCCalc *c, const mpfr_t value)
{
    char *text = NULL;
    if (mpfr_asprintf(&text, "%.*Rg", c->digits, value) < 0)
        return NULL;
    return text;
}

static char *mpq_text(const mpq_t value)
{
    return mpq_get_str(NULL, 10, value);
}

static char *copy_text(const char *text)
{
    size_t n = text ? strlen(text) : 0;
    char *copy = (char *)malloc(n + 1);
    if (!copy)
        return NULL;
    if (n)
        memcpy(copy, text, n);
    copy[n] = '\0';
    return copy;
}

static void json_scalar(const TerminalCCalc *c, const TerminalCValue *v)
{
    if (v->type == TERMINALC_BOOL) {
        printf("{\"type\":\"boolean\",\"value\":%s}",
               v->b ? "true" : "false");
        return;
    }

    mpfr_t decimal;
    mpfr_init2(decimal, c->bits);
    char *exact = NULL;
    char *dec = NULL;
    const char *unit = NULL;
    if (v->type == TERMINALC_RAT) {
        exact = mpq_text(v->q);
        mpfr_set_q(decimal, v->q, MPFR_RNDN);
    } else if (v->type == TERMINALC_MEASURE) {
        if (mpfr_zero_p(v->r) && mpq_sgn(v->q) != 0)
            mpfr_set_q(decimal, v->q, MPFR_RNDN);
        else
            mpfr_set(decimal, v->r, MPFR_RNDN);
        if (mpfr_zero_p(v->r) || mpq_sgn(v->q) != 0)
            exact = mpq_text(v->q);
        unit = terminalc_measure_unit_name(v->unit_id);
    } else {
        mpfr_set(decimal, v->r, MPFR_RNDN);
    }
    dec = mpfr_text(c, decimal);

    printf("{\"type\":");
    if (v->type == TERMINALC_RAT)
        print_json_string("rational");
    else if (v->type == TERMINALC_MEASURE)
        print_json_string("measure");
    else
        print_json_string("real");
    if (exact) {
        fputs(",\"exact\":", stdout);
        print_json_string(exact);
    }
    fputs(",\"decimal\":", stdout);
    print_json_string(dec ? dec : "");
    if (unit) {
        fputs(",\"unit\":", stdout);
        print_json_string(unit);
    }
    putchar('}');
    free(exact);
    free(dec);
    mpfr_clear(decimal);
}

static void json_value(const TerminalCCalc *c, const TerminalCValue *v)
{
    if (v->type != TERMINALC_TENSOR || !v->ten) {
        json_scalar(c, v);
        return;
    }
    printf("{\"type\":\"tensor\",\"rank\":%d,\"shape\":[", v->ten->rank);
    for (int i = 0; i < v->ten->rank; i++) {
        if (i)
            putchar(',');
        printf("%zu", v->ten->shape[i]);
    }
    fputs("],\"values\":[", stdout);
    for (size_t i = 0; i < v->ten->n; i++) {
        if (i)
            putchar(',');
        json_scalar(c, &v->ten->data[i]);
    }
    fputs("]}", stdout);
}

static void csv_field(const char *text)
{
    bool quote = false;
    if (text) {
        for (const char *p = text; *p; p++) {
            if (*p == ',' || *p == '"' || *p == '\r' || *p == '\n' || *p == ';') {
                quote = true;
                break;
            }
        }
    }
    if (!quote) {
        fputs(text ? text : "", stdout);
        return;
    }
    putchar('"');
    for (const char *p = text; p && *p; p++) {
        if (*p == '"')
            putchar('"');
        putchar(*p);
    }
    putchar('"');
}

static char *csv_tensor_cell(const TerminalCCalc *c, const TerminalCValue *v)
{
    if (v->type == TERMINALC_RAT)
        return mpq_text(v->q);
    if (v->type == TERMINALC_REAL)
        return mpfr_text(c, v->r);
    if (v->type == TERMINALC_BOOL)
        return copy_text(v->b ? "true" : "false");
    if (v->type != TERMINALC_MEASURE)
        return copy_text("?");

    char *number = NULL;
    if (mpq_sgn(v->q) != 0)
        number = mpq_text(v->q);
    else {
        mpfr_t magnitude;
        mpfr_init2(magnitude, c->bits);
        mpfr_set(magnitude, v->r, MPFR_RNDN);
        number = mpfr_text(c, magnitude);
        mpfr_clear(magnitude);
    }
    const char *unit = terminalc_measure_unit_name(v->unit_id);
    if (!number || !unit || !*unit)
        return number;
    size_t n = strlen(number) + strlen(unit) + 2;
    char *text = (char *)malloc(n);
    if (text)
        snprintf(text, n, "%s %s", number, unit);
    free(number);
    return text;
}

static void csv_value(const TerminalCCalc *c, const TerminalCValue *v)
{
    puts("type,exact,decimal,unit,shape,values");
    if (v->type == TERMINALC_TENSOR && v->ten) {
        csv_field("tensor");
        fputs(",,,,", stdout);
        char shape[128];
        size_t used = 0;
        shape[0] = '\0';
        for (int i = 0; i < v->ten->rank; i++) {
            int written = snprintf(shape + used, sizeof shape - used, "%s%zu",
                                   i ? "x" : "", v->ten->shape[i]);
            if (written < 0 || (size_t)written >= sizeof shape - used)
                break;
            used += (size_t)written;
        }
        csv_field(shape);
        putchar(',');
        putchar('"');
        for (size_t i = 0; i < v->ten->n; i++) {
            if (i)
                putchar(';');
            char *text = csv_tensor_cell(c, &v->ten->data[i]);
            if (text) {
                for (char *p = text; *p; p++) {
                    if (*p == '"')
                        putchar('"');
                    putchar(*p);
                }
                free(text);
            }
        }
        puts("\"");
        return;
    }

    const char *type = "real";
    char *exact = NULL;
    char *decimal = NULL;
    const char *unit = "";
    mpfr_t temp;
    mpfr_init2(temp, c->bits);
    if (v->type == TERMINALC_BOOL) {
        type = "boolean";
        decimal = copy_text(v->b ? "true" : "false");
    } else if (v->type == TERMINALC_RAT) {
        type = "rational";
        exact = mpq_text(v->q);
        mpfr_set_q(temp, v->q, MPFR_RNDN);
        decimal = mpfr_text(c, temp);
    } else if (v->type == TERMINALC_MEASURE) {
        type = "measure";
        if (mpfr_zero_p(v->r) || mpq_sgn(v->q) != 0)
            exact = mpq_text(v->q);
        if (mpfr_zero_p(v->r) && mpq_sgn(v->q) != 0)
            mpfr_set_q(temp, v->q, MPFR_RNDN);
        else
            mpfr_set(temp, v->r, MPFR_RNDN);
        decimal = mpfr_text(c, temp);
        unit = terminalc_measure_unit_name(v->unit_id);
    } else {
        decimal = mpfr_text(c, v->r);
    }
    csv_field(type); putchar(','); csv_field(exact); putchar(',');
    csv_field(decimal); putchar(','); csv_field(unit); puts(",,");
    free(exact);
    free(decimal);
    mpfr_clear(temp);
}

void terminalc_print_value(TerminalCCalc *c, const TerminalCValue *v)
{
    if (c->output_format == TERMINALC_OUTPUT_JSON) {
        json_value(c, v);
        putchar('\n');
        return;
    }
    if (c->output_format == TERMINALC_OUTPUT_CSV) {
        csv_value(c, v);
        return;
    }
    if (v->type == TERMINALC_BOOL) {
        printf("%s%s%s\n", C_B_YEL, v->b ? "true" : "false", C_RST);
        (void)c;
        return;
    }
    if (v->type == TERMINALC_TENSOR) {
        terminalc_print_tensor(c, v->ten);
        return;
    }
    if (v->type == TERMINALC_MEASURE) {
        terminalc_print_measure(c, v);
        return;
    }
    if (v->type == TERMINALC_RAT) {
        if (mpz_cmp_ui(mpq_denref(v->q), 1) == 0) {
            /* Exact integer — never apply decimal precision padding. */
            if (c->out_base >= 2 && c->out_base <= 36) {
                int base = c->out_base;
                c->out_base = 0;
                print_integer_base(c, mpq_numref(v->q), base);
                return;
            }
            print_plain_mpz(mpq_numref(v->q));
        } else {
            /* Exact rational: always show fraction + decimal expansion. */
            char *frac = mpq_get_str(NULL, 10, v->q);
            mpfr_t r;
            mpfr_init2(r, c->bits);
            mpfr_set_q(r, v->q, MPFR_RNDN);
            if (frac) {
                print_fraction_and_decimal(c, "Exact", frac, r);
                free(frac);
            } else {
                printf("%sExact:%s  %s", C_B_CYN, C_RST, C_B_GRN);
                print_q(v->q);
                printf("%s\n", C_RST);
                printf("%sDecimal:%s ", C_DIM, C_RST);
                print_decimal_real(c, r);
                printf("%s[%d digits, round-to-nearest]%s\n", C_DIM, c->digits,
                       C_RST);
            }
            mpfr_clear(r);
        }
        return;
    }

    /* REAL — prefer integer / fraction / radical; precision only for decimals. */

    if (v->display_decimal) {
        print_decimal_real(c, v->r);
        printf("%s[%d digits, round-to-nearest]%s\n", C_DIM, c->digits, C_RST);
        return;
    }

    /* 1) Integer-valued reals (e.g. log(1000) → 3). */
    {
        mpz_t zi;
        mpz_init(zi);
        if (real_as_integer(c, v->r, zi)) {
            print_plain_mpz(zi);
            mpz_clear(zi);
            return;
        }
        mpz_clear(zi);
    }

    /* 2) Rational recovery (fraction + decimal). */
    {
        mpfr_t tol;
        mpfr_init2(tol, c->bits);
        set_recovery_tol(tol, c->digits, c->bits);
        mpq_t q;
        mpq_init(q);
        /* Allow larger denominators when more digits are requested. */
        unsigned long max_den = 10000UL;
        if (c->digits > 10) {
            unsigned long extra = 1;
            for (int i = 0; i < c->digits - 10 && i < 8; i++)
                extra *= 10UL;
            if (extra > 1 && max_den < 100000000UL / extra)
                max_den *= extra;
            if (max_den > 100000000UL)
                max_den = 100000000UL;
        }
        if (rat_from_real(v->r, q, max_den, tol)) {
            if (mpz_cmp_ui(mpq_denref(q), 1) == 0) {
                if (mpfr_integer_p(v->r)) {
                    print_plain_mpz(mpq_numref(q));
                    mpq_clear(q);
                    mpfr_clear(tol);
                    return;
                }
            } else {
                /* Higher-precision check: Exact only if still matches tighter tol */
                mpfr_t tight, cand;
                mpfr_init2(tight, c->bits);
                mpfr_init2(cand, c->bits);
                set_match_tol(tight, c->digits + 8 > 40 ? 40 : c->digits + 8,
                              c->bits);
                mpfr_set_q(cand, q, MPFR_RNDN);
                const char *lab =
                    nearly_equal(v->r, cand, tight) ? "Exact" : "Approx";
                char *frac = mpq_get_str(NULL, 10, q);
                if (frac) {
                    print_fraction_and_decimal(c, lab, frac, v->r);
                    free(frac);
                }
                mpfr_clear(tight);
                mpfr_clear(cand);
                mpq_clear(q);
                mpfr_clear(tol);
                return;
            }
        }
        mpq_clear(q);
        mpfr_clear(tol);
    }

    /* 3) Radical / other nice closed forms. */
    char nice[96];
    if (try_nice_form(c, v->r, nice, sizeof nice)) {
        if (is_plain_integer_str(nice)) {
            print_plain_integer_str(nice);
            return;
        }
        /* Fraction string from try_nice_form (e.g. "1/2") or radical. */
        bool looks_closed = (strchr(nice, '/') != NULL) ||
                            (strstr(nice, "sqrt") != NULL) ||
                            (strchr(nice, '*') != NULL);
        if (looks_closed) {
            const char *label =
                (strchr(nice, '/') != NULL && !strstr(nice, "sqrt")) ? "Exact"
                                                                     : "Nice";
            print_fraction_and_decimal(c, label, nice, v->r);
            return;
        }
    }

    /* 4) Generic real: precision applies here only. */
    print_decimal_real(c, v->r);
    printf("%s[%d digits, round-to-nearest]%s\n", C_DIM, c->digits, C_RST);
}

void terminalc_print_error(const TerminalCCalc *c)
{
    fprintf(stderr, "%sParse/evaluation error at column %zu:%s %s%s%s\n",
            C_B_RED, c->error_pos + 1, C_RST, C_RED,
            c->error ? c->error : "unknown error", C_RST);
    fprintf(stderr, "%s\n", c->text ? c->text : "");
    for (size_t i = 0; i < c->error_pos; i++)
        fputc(' ', stderr);
    fprintf(stderr, "%s^%s\n", C_B_RED, C_RST);
}
