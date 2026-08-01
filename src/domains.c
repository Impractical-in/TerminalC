/**
 * Solvers, calculus, units, mensuration.
 */
#include "terminalc.h"
#include "parse_internal.h"
#include "color.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* ========== Units ========== */

typedef enum {
    UD_NONE = 0,
    UD_LENGTH,
    UD_MASS,
    UD_TIME,
    UD_AREA,
    UD_VOLUME,
    UD_TEMP,
    UD_SPEED /* length/time */
} UnitDim;

typedef struct {
    const char *name;
    const char *display; /* pretty print, e.g. "m/s"; NULL => use name */
    UnitDim dim;
    const char *to_si_q; /* exact decimal/rational scale to SI */
    const char *t_off_q; /* exact affine temperature offset */
    int temp_kind;  /* 0=none, 1=C, 2=F, 3=K */
} UnitDef;

static const UnitDef g_units[] = {
    {"m", NULL, UD_LENGTH, "1", "0", 0},
    {"meter", NULL, UD_LENGTH, "1", "0", 0},
    {"metre", NULL, UD_LENGTH, "1", "0", 0},
    {"km", NULL, UD_LENGTH, "1000", "0", 0},
    {"cm", NULL, UD_LENGTH, "1/100", "0", 0},
    {"mm", NULL, UD_LENGTH, "1/1000", "0", 0},
    {"um", NULL, UD_LENGTH, "1/1000000", "0", 0},
    {"nm", NULL, UD_LENGTH, "1/1000000000", "0", 0},
    {"ft", NULL, UD_LENGTH, "381/1250", "0", 0},
    {"in", NULL, UD_LENGTH, "127/5000", "0", 0},
    {"yd", NULL, UD_LENGTH, "1143/1250", "0", 0},
    {"mile", NULL, UD_LENGTH, "201168/125", "0", 0},
    {"kg", NULL, UD_MASS, "1", "0", 0},
    {"g", NULL, UD_MASS, "1/1000", "0", 0},
    {"mg", NULL, UD_MASS, "1/1000000", "0", 0},
    {"ug", NULL, UD_MASS, "1/1000000000", "0", 0},
    {"tonne", NULL, UD_MASS, "1000", "0", 0},
    {"t", NULL, UD_MASS, "1000", "0", 0},
    {"lb", NULL, UD_MASS, "45359237/100000000", "0", 0},
    {"oz", NULL, UD_MASS, "226796185/8000000000", "0", 0},
    {"s", NULL, UD_TIME, "1", "0", 0},
    {"sec", NULL, UD_TIME, "1", "0", 0},
    {"ms", NULL, UD_TIME, "1/1000", "0", 0},
    {"us", NULL, UD_TIME, "1/1000000", "0", 0},
    {"min", NULL, UD_TIME, "60", "0", 0},
    {"hour", NULL, UD_TIME, "3600", "0", 0},
    {"hr", NULL, UD_TIME, "3600", "0", 0},
    {"day", NULL, UD_TIME, "86400", "0", 0},
    {"m2", NULL, UD_AREA, "1", "0", 0},
    {"km2", NULL, UD_AREA, "1000000", "0", 0},
    {"hectare", NULL, UD_AREA, "10000", "0", 0},
    {"ha", NULL, UD_AREA, "10000", "0", 0},
    {"acre", NULL, UD_AREA, "40468564224/10000000", "0", 0},
    {"ft2", NULL, UD_AREA, "9290304/100000000", "0", 0},
    {"m3", NULL, UD_VOLUME, "1", "0", 0},
    {"l", NULL, UD_VOLUME, "1/1000", "0", 0},
    {"liter", NULL, UD_VOLUME, "1/1000", "0", 0},
    {"ml", NULL, UD_VOLUME, "1/1000000", "0", 0},
    /* temperature (affine) */
    {"celsius", "C", UD_TEMP, "1", "27315/100", 1},
    {"c", "C", UD_TEMP, "1", "27315/100", 1},
    {"degc", "C", UD_TEMP, "1", "27315/100", 1},
    {"fahrenheit", "F", UD_TEMP, "5/9", "45967/100", 2},
    {"f", "F", UD_TEMP, "5/9", "45967/100", 2},
    {"degf", "F", UD_TEMP, "5/9", "45967/100", 2},
    {"kelvin", "K", UD_TEMP, "1", "0", 3},
    {"k", "K", UD_TEMP, "1", "0", 3},
    /* speed: SI base m/s */
    {"mps", "m/s", UD_SPEED, "1", "0", 0},
    {"m_s", "m/s", UD_SPEED, "1", "0", 0},
    {"kmps", "km/s", UD_SPEED, "1000", "0", 0},
    {"km_s", "km/s", UD_SPEED, "1000", "0", 0},
    {"kmh", "km/h", UD_SPEED, "5/18", "0", 0},
    {"kmph", "km/h", UD_SPEED, "5/18", "0", 0},
    {"km_h", "km/h", UD_SPEED, "5/18", "0", 0},
    {"mph", "mph", UD_SPEED, "2794/6250", "0", 0},
    {"fps", "ft/s", UD_SPEED, "381/1250", "0", 0},
    {"ft_s", "ft/s", UD_SPEED, "381/1250", "0", 0},
    /* dimensionless prefixes */
    {"kilo", NULL, UD_NONE, "1000", "0", 0},
    {"mega", NULL, UD_NONE, "1000000", "0", 0},
    {"milli", NULL, UD_NONE, "1/1000", "0", 0},
    {"micro", NULL, UD_NONE, "1/1000000", "0", 0},
    {"nano", NULL, UD_NONE, "1/1000000000", "0", 0},
    {"centi", NULL, UD_NONE, "1/100", "0", 0},
    {"hecto", NULL, UD_NONE, "100", "0", 0},
};

static int unit_count(void)
{
    return (int)(sizeof g_units / sizeof g_units[0]);
}

static int find_unit(const char *name)
{
    for (int i = 0; i < unit_count(); i++) {
        if (strcmp(g_units[i].name, name) == 0)
            return i;
    }
    return -1;
}

static bool valid_measure_unit(const TerminalCValue *v)
{
    return v->type == TERMINALC_MEASURE && v->unit_id >= 0 &&
           v->unit_id < unit_count();
}

bool terminalc_try_unit_constant(const char *name, TerminalCValue *out, mpfr_prec_t bits)
{
    int id = find_unit(name);
    if (id < 0)
        return false;
    terminalc_value_init(out, bits);
    out->type = TERMINALC_MEASURE;
    out->unit_id = id;
    mpq_set_ui(out->q, 1, 1);
    mpfr_set_ui(out->r, 1, MPFR_RNDN);
    return true;
}

static bool mag_as_mpfr(const TerminalCCalc *c, mpfr_t out,
                        const TerminalCValue *v)
{
    if (v->type == TERMINALC_MEASURE) {
        if (!mpfr_nan_p(v->r) && !mpfr_zero_p(v->r))
            mpfr_set(out, v->r, MPFR_RNDN);
        else if (mpq_sgn(v->q) != 0)
            mpfr_set_q(out, v->q, MPFR_RNDN);
        else if (!mpfr_nan_p(v->r))
            mpfr_set(out, v->r, MPFR_RNDN);
        else
            mpfr_set_ui(out, 0, MPFR_RNDN);
    } else {
        terminalc_as_real(c, out, v);
    }
    return !mpfr_nan_p(out) && !mpfr_inf_p(out);
}

static bool unit_factor(const char *which, mpfr_t out)
{
    mpq_t q;
    mpq_init(q);
    bool ok = mpq_set_str(q, which, 10) == 0;
    if (ok) {
        mpq_canonicalize(q);
        mpfr_set_q(out, q, MPFR_RNDN);
    }
    mpq_clear(q);
    return ok;
}

static bool unit_scale(const UnitDef *u, mpfr_t out)
{
    return unit_factor(u->to_si_q, out);
}

static bool unit_offset(const UnitDef *u, mpfr_t out)
{
    return unit_factor(u->t_off_q, out);
}

static void set_measure_mpfr(TerminalCCalc *c, TerminalCValue *out,
                             int unit_id, mpfr_srcptr mag)
{
    terminalc_value_clear(out);
    terminalc_value_init(out, c->bits);
    out->type = TERMINALC_MEASURE;
    out->unit_id = unit_id;
    mpfr_set(out->r, mag, MPFR_RNDN);
    mpq_set_ui(out->q, 0, 1);
    if (mpfr_integer_p(mag)) {
        mpz_t z;
        mpz_init(z);
        mpfr_get_z(z, mag, MPFR_RNDN);
        mpq_set_z(out->q, z);
        mpz_clear(z);
    }
}

bool terminalc_units_mul(TerminalCCalc *c, const TerminalCValue *num,
                         const TerminalCValue *unit, TerminalCValue *out)
{
    if (!valid_measure_unit(unit)) {
        terminalc_fail(c, "expected a unit");
        return false;
    }
    if (!terminalc_is_scalar(num)) {
        terminalc_fail(c, "unit scale must be scalar");
        return false;
    }
    mpfr_t x, u, m;
    mpfr_inits2(c->bits, x, u, m, (mpfr_ptr)0);
    if (!mag_as_mpfr(c, x, num) || !mag_as_mpfr(c, u, unit)) {
        mpfr_clears(x, u, m, (mpfr_ptr)0);
        terminalc_fail(c, "invalid unit magnitude");
        return false;
    }
    mpfr_mul(m, x, u, MPFR_RNDN);
    if (mpfr_nan_p(m) || mpfr_inf_p(m)) {
        mpfr_clears(x, u, m, (mpfr_ptr)0);
        terminalc_fail(c, "invalid unit magnitude");
        return false;
    }
    set_measure_mpfr(c, out, unit->unit_id, m);
    mpfr_clears(x, u, m, (mpfr_ptr)0);
    return true;
}

bool terminalc_units_to(TerminalCCalc *c, const TerminalCValue *qty, const TerminalCValue *target_unit,
                 TerminalCValue *out)
{
    if (!valid_measure_unit(qty) || !valid_measure_unit(target_unit)) {
        terminalc_fail(c, "to(qty, unit) needs a measure and a unit");
        return false;
    }
    const UnitDef *from = &g_units[qty->unit_id];
    const UnitDef *to = &g_units[target_unit->unit_id];
    if (from->dim != to->dim) {
        terminalc_fail(c, "incompatible units");
        return false;
    }
    mpfr_t x, y, target_mag, factor_from, factor_to, offset, k;
    mpfr_inits2(c->bits, x, y, target_mag, factor_from, factor_to, offset, k,
                (mpfr_ptr)0);
    if (!mag_as_mpfr(c, x, qty) || !mag_as_mpfr(c, target_mag, target_unit) ||
        mpfr_cmp_ui(target_mag, 1) != 0 || !unit_scale(from, factor_from) ||
        !unit_scale(to, factor_to) || !unit_offset(from, offset)) {
        mpfr_clears(x, y, target_mag, factor_from, factor_to, offset, k,
                    (mpfr_ptr)0);
        terminalc_fail(c, "target unit must be an unscaled unit");
        return false;
    }
    if (from->dim == UD_TEMP) {
        if (from->temp_kind == 1)
            mpfr_add(k, x, offset, MPFR_RNDN);
        else if (from->temp_kind == 2) {
            mpfr_add(k, x, offset, MPFR_RNDN);
            mpfr_mul(k, k, factor_from, MPFR_RNDN);
        } else
            mpfr_set(k, x, MPFR_RNDN);
        if (to->temp_kind == 1) {
            if (!unit_offset(to, offset))
                goto unit_error;
            mpfr_sub(y, k, offset, MPFR_RNDN);
        } else if (to->temp_kind == 2) {
            if (!unit_offset(to, offset))
                goto unit_error;
            mpfr_div(y, k, factor_to, MPFR_RNDN);
            mpfr_sub(y, y, offset, MPFR_RNDN);
        } else
            mpfr_set(y, k, MPFR_RNDN);
    } else {
        mpfr_mul(y, x, factor_from, MPFR_RNDN);
        mpfr_div(y, y, factor_to, MPFR_RNDN);
    }
    if (mpfr_nan_p(y) || mpfr_inf_p(y))
        goto unit_error;
    set_measure_mpfr(c, out, target_unit->unit_id, y);
    mpfr_clears(x, y, target_mag, factor_from, factor_to, offset, k,
                (mpfr_ptr)0);
    return true;

unit_error:
    mpfr_clears(x, y, target_mag, factor_from, factor_to, offset, k,
                (mpfr_ptr)0);
    terminalc_fail(c, "invalid unit conversion");
    return false;
}

/*
 * Measure / measure:
 *   length / time  -> speed (m/s SI, unit mps)
 *   same dim       -> dimensionless ratio (as plain real)
 */
bool terminalc_units_div(TerminalCCalc *c, const TerminalCValue *a,
                         const TerminalCValue *b, TerminalCValue *out)
{
    if (!valid_measure_unit(a) || !valid_measure_unit(b)) {
        terminalc_fail(c, "unit division needs two measures");
        return false;
    }
    const UnitDef *ua = &g_units[a->unit_id];
    const UnitDef *ub = &g_units[b->unit_id];
    mpfr_t ma, mb, fa, fb, si, t;
    mpfr_inits2(c->bits, ma, mb, fa, fb, si, t, (mpfr_ptr)0);
    if (!mag_as_mpfr(c, ma, a) || !mag_as_mpfr(c, mb, b) ||
        !unit_scale(ua, fa) || !unit_scale(ub, fb) || mpfr_zero_p(mb)) {
        mpfr_clears(ma, mb, fa, fb, si, t, (mpfr_ptr)0);
        terminalc_fail(c, "division by zero unit magnitude");
        return false;
    }

    if (ua->dim == UD_LENGTH && ub->dim == UD_TIME) {
        /* result SI m/s */
        mpfr_mul(si, ma, fa, MPFR_RNDN);
        mpfr_mul(t, mb, fb, MPFR_RNDN);
        mpfr_div(si, si, t, MPFR_RNDN);
        int id = find_unit("mps");
        if (id < 0) {
            terminalc_fail(c, "internal: mps unit missing");
            mpfr_clears(ma, mb, fa, fb, si, t, (mpfr_ptr)0);
            return false;
        }
        set_measure_mpfr(c, out, id, si); /* mps to_si = 1 */
        mpfr_clears(ma, mb, fa, fb, si, t, (mpfr_ptr)0);
        return true;
    }
    if (ua->dim == ub->dim && ua->dim != UD_TEMP && ua->dim != UD_NONE) {
        /* cancel: pure number */
        mpfr_mul(si, ma, fa, MPFR_RNDN);
        mpfr_mul(t, mb, fb, MPFR_RNDN);
        mpfr_div(si, si, t, MPFR_RNDN);
        terminalc_value_clear(out);
        terminalc_value_init(out, c->bits);
        out->type = TERMINALC_REAL;
        mpfr_set(out->r, si, MPFR_RNDN);
        mpfr_clears(ma, mb, fa, fb, si, t, (mpfr_ptr)0);
        return true;
    }
    mpfr_clears(ma, mb, fa, fb, si, t, (mpfr_ptr)0);
    terminalc_fail(c, "incompatible units for division (try length/time for speed)");
    return false;
}

/* exported for value.c */
const char *terminalc_measure_unit_name(int unit_id)
{
    if (unit_id < 0 || unit_id >= unit_count())
        return "";
    return g_units[unit_id].display ? g_units[unit_id].display : g_units[unit_id].name;
}

void terminalc_print_measure(const TerminalCCalc *c, const TerminalCValue *v)
{
    const UnitDef *u = &g_units[v->unit_id];
    const char *label = u->display ? u->display : u->name;
    printf("%s", C_B_GRN);
    mpfr_t t;
    mpfr_init2(t, c->bits);
    if (!mpfr_nan_p(v->r) && (!mpfr_zero_p(v->r) || mpq_sgn(v->q) == 0))
        mpfr_set(t, v->r, MPFR_RNDN);
    else
        mpfr_set_q(t, v->q, MPFR_RNDN);
    if (mpfr_nan_p(t))
        mpfr_set_ui(t, 0, MPFR_RNDN);
    if (mpfr_integer_p(t))
        mpfr_printf("%.0Rf", t);
    else
        mpfr_printf("%.*Rg", c->digits, t);
    mpfr_clear(t);
    printf(" %s%s%s\n", C_B_CYN, label, C_RST);
}

/* ========== Numeric solve / calculus ========== */

/*
 * Numeric domains deliberately use MPFR throughout their iteration.  The
 * parser and value layer are already arbitrary precision; converting every
 * sample to double here used to throw away that work.  The algorithms use a
 * bounded working tolerance (80 decimal digits) so a request for a very high
 * display precision cannot accidentally turn a short REPL command into an
 * unbounded computation.
 */
enum {
    TERMINALC_NUMERIC_WORK_DIGITS = 80,
    TERMINALC_NUMERIC_MAX_ITER = 2048,
    TERMINALC_INTEGRAL_MAX_EVALS = 200000
};

static int numeric_work_digits(const TerminalCCalc *c)
{
    return c->digits < TERMINALC_NUMERIC_WORK_DIGITS
               ? c->digits
               : TERMINALC_NUMERIC_WORK_DIGITS;
}

static mpfr_prec_t numeric_work_bits(const TerminalCCalc *c)
{
    int digits = numeric_work_digits(c);
    mpfr_prec_t bits = (mpfr_prec_t)(digits * 3.3219280948873623) +
                       TERMINALC_GUARD_BITS;
    return bits < c->bits ? bits : c->bits;
}

static bool finite_mpfr(mpfr_srcptr x)
{
    return !mpfr_nan_p(x) && !mpfr_inf_p(x);
}

static void numeric_tolerance(const TerminalCCalc *c, mpfr_t out)
{
    mpfr_set_si(out, -numeric_work_digits(c), MPFR_RNDN);
    mpfr_exp10(out, out, MPFR_RNDN);
}

static bool numeric_value(TerminalCCalc *c, const TerminalCValue *v,
                          mpfr_t out)
{
    if (!terminalc_is_scalar(v)) {
        terminalc_fail(c, "numeric functions need scalar values");
        return false;
    }
    terminalc_as_real(c, out, v);
    if (!finite_mpfr(out)) {
        terminalc_fail(c, "numeric argument is not finite");
        return false;
    }
    return true;
}

static void restore_x(TerminalCCalc *c, TerminalCValue *saved)
{
    terminalc_value_copy(&c->x, saved);
    terminalc_value_clear(saved);
}

static bool eval_at_mpfr(TerminalCCalc *c, const char *expr, mpfr_srcptr x,
                         mpfr_t y)
{
    TerminalCValue saved_x;
    terminalc_value_init(&saved_x, c->bits);
    terminalc_value_copy(&saved_x, &c->x);

    /* Keep nested parser/function allocations bounded even when the caller
     * requested the maximum display precision.  The destination `y` remains
     * at the caller's precision, so this only limits intermediate work. */
    mpfr_prec_t saved_bits = c->bits;
    c->bits = numeric_work_bits(c);
    c->x.type = TERMINALC_REAL;
    mpfr_set(c->x.r, x, MPFR_RNDN);

    TerminalCValue v;
    bool ok = terminalc_eval_nested(c, expr, &v);
    if (ok && terminalc_is_scalar(&v)) {
        terminalc_as_real(c, y, &v);
        ok = finite_mpfr(y);
        if (!ok)
            terminalc_fail(c, "numeric expression returned a non-finite value");
    } else if (ok) {
        terminalc_fail(c, "numeric expression must return a scalar");
        ok = false;
    }
    terminalc_value_clear(&v);
    c->bits = saved_bits;
    restore_x(c, &saved_x);
    return ok;
}

static void numeric_step(const TerminalCCalc *c, mpfr_srcptr x, mpfr_t h)
{
    /* Balance five-point O(h^4) truncation against MPFR round-off. */
    int exponent = (numeric_work_digits(c) + 16) / 5;
    if (exponent < 6)
        exponent = 6;
    mpfr_set_ui(h, 10, MPFR_RNDN);
    mpfr_pow_si(h, h, -(long)exponent, MPFR_RNDN);

    mpfr_t scale;
    mpfr_init2(scale, numeric_work_bits(c));
    mpfr_abs(scale, x, MPFR_RNDN);
    if (mpfr_cmp_ui(scale, 1) < 0)
        mpfr_set_ui(scale, 1, MPFR_RNDN);
    mpfr_mul(h, h, scale, MPFR_RNDN);
    mpfr_clear(scale);
}

static bool derivative_at_mpfr(TerminalCCalc *c, const char *expr,
                               mpfr_srcptr x, mpfr_t out)
{
    mpfr_t h, xm2, xm, xp, xp2, ym2, ym, yp, yp2, t;
    mpfr_inits2(numeric_work_bits(c), h, xm2, xm, xp, xp2, ym2, ym, yp, yp2, t,
                (mpfr_ptr)0);
    numeric_step(c, x, h);

    bool separated = false;
    for (int i = 0; i < 20; i++) {
        mpfr_sub(xm2, x, h, MPFR_RNDN);
        mpfr_sub(xm2, xm2, h, MPFR_RNDN);
        mpfr_sub(xm, x, h, MPFR_RNDN);
        mpfr_add(xp, x, h, MPFR_RNDN);
        mpfr_add(xp2, xp, h, MPFR_RNDN);
        if (mpfr_cmp(xm, x) != 0 && mpfr_cmp(xp, x) != 0) {
            separated = true;
            break;
        }
        mpfr_mul_ui(h, h, 10, MPFR_RNDN);
    }
    if (!separated) {
        mpfr_clears(h, xm2, xm, xp, xp2, ym2, ym, yp, yp2, t,
                    (mpfr_ptr)0);
        terminalc_fail(c, "diff step is below the current precision");
        return false;
    }

    bool ok = eval_at_mpfr(c, expr, xm2, ym2) &&
              eval_at_mpfr(c, expr, xm, ym) &&
              eval_at_mpfr(c, expr, xp, yp) &&
              eval_at_mpfr(c, expr, xp2, yp2);
    if (ok) {
        /* Five-point central stencil: O(h^4) truncation error. */
        mpfr_sub(t, yp, ym, MPFR_RNDN);
        mpfr_mul_si(t, t, 8, MPFR_RNDN);
        mpfr_sub(out, t, yp2, MPFR_RNDN);
        mpfr_add(out, out, ym2, MPFR_RNDN);
        mpfr_mul_ui(t, h, 12, MPFR_RNDN);
        mpfr_div(out, out, t, MPFR_RNDN);
    } else if (!c->error) {
        terminalc_fail(c, "diff: cannot evaluate expression");
    }
    mpfr_clears(h, xm2, xm, xp, xp2, ym2, ym, yp, yp2, t,
                (mpfr_ptr)0);
    return ok;
}

bool terminalc_numeric_diff(TerminalCCalc *c, const char *expr,
                            const TerminalCValue *x0, TerminalCValue *out)
{
    mpfr_t x, d;
    mpfr_inits2(numeric_work_bits(c), x, d, (mpfr_ptr)0);
    bool ok = numeric_value(c, x0, x) && derivative_at_mpfr(c, expr, x, d);
    if (ok) {
        out->type = TERMINALC_REAL;
        mpfr_set(out->r, d, MPFR_RNDN);
    }
    mpfr_clears(x, d, (mpfr_ptr)0);
    return ok;
}

typedef struct {
    TerminalCCalc *calc;
    const char *expr;
    size_t evaluations;
} IntegralContext;

static bool integral_eval(IntegralContext *ctx, mpfr_srcptr x, mpfr_t y)
{
    if (ctx->evaluations++ >= TERMINALC_INTEGRAL_MAX_EVALS) {
        terminalc_fail(ctx->calc, "integral evaluation limit reached");
        return false;
    }
    return eval_at_mpfr(ctx->calc, ctx->expr, x, y);
}

/*
 * Tanh-sinh quadrature converges exponentially for smooth functions and
 * avoids evaluating endpoints.  That makes it substantially more useful at
 * high MPFR precision than a fixed-width double Simpson pass, while still
 * handling integrable endpoint singularities gracefully.
 */
static bool tanh_sinh_level(IntegralContext *ctx, mpfr_srcptr a,
                            mpfr_srcptr b, mpfr_srcptr h,
                            mpfr_srcptr tolerance, mpfr_t out)
{
    mpfr_t half, mid, pi, t, sinh_t, cosh_t, u, cosh_u, den, weight;
    mpfr_t xp, xm, x_plus, x_minus, fp, fm, term, sum, abs_term;
    mpfr_inits2(numeric_work_bits(ctx->calc), half, mid, pi, t, sinh_t, cosh_t, u,
                cosh_u, den, weight, xp, xm, x_plus, x_minus, fp, fm, term,
                sum, abs_term, (mpfr_ptr)0);
    mpfr_sub(half, b, a, MPFR_RNDN);
    mpfr_div_ui(half, half, 2, MPFR_RNDN);
    mpfr_add(mid, a, b, MPFR_RNDN);
    mpfr_div_ui(mid, mid, 2, MPFR_RNDN);
    mpfr_const_pi(pi, MPFR_RNDN);
    mpfr_set_zero(sum, 0);

    int quiet_tail = 0;
    const unsigned long max_k = 10000;
    bool ok = true;
    for (unsigned long k = 0; k <= max_k; k++) {
        mpfr_set_ui(t, k, MPFR_RNDN);
        mpfr_mul(t, t, h, MPFR_RNDN);
        mpfr_sinh(sinh_t, t, MPFR_RNDN);
        mpfr_cosh(cosh_t, t, MPFR_RNDN);
        mpfr_mul(u, pi, sinh_t, MPFR_RNDN);
        mpfr_div_ui(u, u, 2, MPFR_RNDN);
        mpfr_tanh(xp, u, MPFR_RNDN);
        mpfr_cosh(cosh_u, u, MPFR_RNDN);
        mpfr_mul(den, cosh_u, cosh_u, MPFR_RNDN);
        mpfr_div(weight, cosh_t, den, MPFR_RNDN);
        mpfr_mul(weight, weight, pi, MPFR_RNDN);
        mpfr_div_ui(weight, weight, 2, MPFR_RNDN);
        mpfr_mul(weight, weight, h, MPFR_RNDN);
        mpfr_mul(weight, weight, half, MPFR_RNDN);
        if (mpfr_zero_p(weight))
            break;

        if (k == 0) {
            ok = integral_eval(ctx, mid, fp);
            if (ok)
                mpfr_mul(term, weight, fp, MPFR_RNDN);
        } else {
            mpfr_mul(x_plus, half, xp, MPFR_RNDN);
            mpfr_add(x_plus, mid, x_plus, MPFR_RNDN);
            mpfr_neg(xm, xp, MPFR_RNDN);
            mpfr_mul(x_minus, half, xm, MPFR_RNDN);
            mpfr_add(x_minus, mid, x_minus, MPFR_RNDN);
            bool plus_endpoint = mpfr_cmp_ui(xp, 1) == 0;
            bool minus_endpoint = mpfr_cmp_si(xm, -1) == 0;
            ok = plus_endpoint || integral_eval(ctx, x_plus, fp);
            if (ok)
                ok = minus_endpoint || integral_eval(ctx, x_minus, fm);
            if (ok) {
                mpfr_set_zero(term, 0);
                if (!plus_endpoint)
                    mpfr_add(term, term, fp, MPFR_RNDN);
                if (!minus_endpoint)
                    mpfr_add(term, term, fm, MPFR_RNDN);
                mpfr_mul(term, term, weight, MPFR_RNDN);
            }
        }
        if (!ok)
            break;
        mpfr_add(sum, sum, term, MPFR_RNDN);
        mpfr_abs(abs_term, term, MPFR_RNDN);
        if (k > 8 && mpfr_cmp(abs_term, tolerance) <= 0)
            quiet_tail++;
        else
            quiet_tail = 0;
        if (quiet_tail >= 8)
            break;
    }
    if (ok)
        mpfr_set(out, sum, MPFR_RNDN);
    mpfr_clears(half, mid, pi, t, sinh_t, cosh_t, u, cosh_u, den, weight,
                xp, xm, x_plus, x_minus, fp, fm, term, sum, abs_term,
                (mpfr_ptr)0);
    return ok;
}

bool terminalc_numeric_integral(TerminalCCalc *c, const char *expr,
                                const TerminalCValue *a_value,
                                const TerminalCValue *b_value,
                                TerminalCValue *out)
{
    mpfr_t a, b, h, tolerance, previous, current, delta;
    mpfr_inits2(numeric_work_bits(c), a, b, h, tolerance, previous, current, delta,
                (mpfr_ptr)0);
    bool ok = numeric_value(c, a_value, a) && numeric_value(c, b_value, b);
    if (!ok) {
        mpfr_clears(a, b, h, tolerance, previous, current, delta,
                    (mpfr_ptr)0);
        return false;
    }
    if (mpfr_cmp(a, b) == 0) {
        out->type = TERMINALC_REAL;
        mpfr_set_zero(out->r, 0);
        mpfr_clears(a, b, h, tolerance, previous, current, delta,
                    (mpfr_ptr)0);
        return true;
    }

    bool reverse = mpfr_cmp(a, b) > 0;
    if (reverse)
        mpfr_swap(a, b);
    numeric_tolerance(c, tolerance);
    mpfr_set_ui(h, 1, MPFR_RNDN);
    bool converged = false;
    bool have_previous = false;
    IntegralContext ctx = {c, expr, 0};
    int max_levels = numeric_work_digits(c) / 4 + 8;
    if (max_levels > 18)
        max_levels = 18;
    for (int level = 0; level < max_levels; level++) {
        if (!tanh_sinh_level(&ctx, a, b, h, tolerance, current)) {
            ok = false;
            break;
        }
        if (have_previous) {
            mpfr_sub(delta, current, previous, MPFR_RNDN);
            mpfr_abs(delta, delta, MPFR_RNDN);
            if (mpfr_cmp(delta, tolerance) <= 0) {
                converged = true;
                break;
            }
        }
        mpfr_set(previous, current, MPFR_RNDN);
        have_previous = true;
        mpfr_div_ui(h, h, 2, MPFR_RNDN);
    }
    if (ok && !converged) {
        terminalc_fail(c, "integral did not converge at current precision");
        ok = false;
    }
    if (ok) {
        if (reverse)
            mpfr_neg(current, current, MPFR_RNDN);
        out->type = TERMINALC_REAL;
        mpfr_set(out->r, current, MPFR_RNDN);
    }
    mpfr_clears(a, b, h, tolerance, previous, current, delta,
                (mpfr_ptr)0);
    return ok;
}

bool terminalc_numeric_solve(TerminalCCalc *c, const char *expr,
                             const TerminalCValue *a_value,
                             const TerminalCValue *b_value, bool bracket,
                             TerminalCValue *out)
{
    mpfr_t a, b, fa, fb, m, fm, tolerance, width, x, fx, dfx, step, next;
    mpfr_inits2(numeric_work_bits(c), a, b, fa, fb, m, fm, tolerance, width, x, fx, dfx,
                step, next, (mpfr_ptr)0);
    bool ok = numeric_value(c, a_value, a);
    if (bracket)
        ok = ok && numeric_value(c, b_value, b);
    if (!ok) {
        mpfr_clears(a, b, fa, fb, m, fm, tolerance, width, x, fx, dfx,
                    step, next, (mpfr_ptr)0);
        return false;
    }
    numeric_tolerance(c, tolerance);
    int max_iter = numeric_work_digits(c) * 4 + 32;
    if (max_iter > TERMINALC_NUMERIC_MAX_ITER)
        max_iter = TERMINALC_NUMERIC_MAX_ITER;

    if (bracket) {
        if (mpfr_cmp(a, b) > 0)
            mpfr_swap(a, b);
        if (!eval_at_mpfr(c, expr, a, fa) || !eval_at_mpfr(c, expr, b, fb)) {
            if (!c->error)
                terminalc_fail(c, "solve: cannot evaluate");
            ok = false;
        } else if (mpfr_zero_p(fa)) {
            out->type = TERMINALC_REAL;
            mpfr_set(out->r, a, MPFR_RNDN);
        } else if (mpfr_zero_p(fb)) {
            out->type = TERMINALC_REAL;
            mpfr_set(out->r, b, MPFR_RNDN);
        } else if (mpfr_sgn(fa) == mpfr_sgn(fb)) {
            terminalc_fail(c, "solve: no sign change on interval");
            ok = false;
        } else {
            ok = false;
            for (int i = 0; i < max_iter; i++) {
                mpfr_add(m, a, b, MPFR_RNDN);
                mpfr_div_ui(m, m, 2, MPFR_RNDN);
                if (!eval_at_mpfr(c, expr, m, fm))
                    break;
                mpfr_sub(width, b, a, MPFR_RNDN);
                mpfr_abs(width, width, MPFR_RNDN);
                if (mpfr_zero_p(fm) || mpfr_cmp(width, tolerance) <= 0) {
                    out->type = TERMINALC_REAL;
                    mpfr_set(out->r, m, MPFR_RNDN);
                    ok = true;
                    break;
                }
                if (mpfr_sgn(fa) == mpfr_sgn(fm)) {
                    mpfr_set(a, m, MPFR_RNDN);
                    mpfr_set(fa, fm, MPFR_RNDN);
                } else {
                    mpfr_set(b, m, MPFR_RNDN);
                    mpfr_set(fb, fm, MPFR_RNDN);
                }
            }
            if (!ok && !c->error)
                terminalc_fail(c, "solve: did not converge");
        }
    } else {
        mpfr_set(x, a, MPFR_RNDN);
        ok = false;
        for (int i = 0; i < max_iter; i++) {
            if (!eval_at_mpfr(c, expr, x, fx))
                break;
            mpfr_abs(width, fx, MPFR_RNDN);
            if (mpfr_cmp(width, tolerance) <= 0) {
                out->type = TERMINALC_REAL;
                mpfr_set(out->r, x, MPFR_RNDN);
                ok = true;
                break;
            }
            if (!derivative_at_mpfr(c, expr, x, dfx))
                break;
            mpfr_abs(width, dfx, MPFR_RNDN);
            if (mpfr_zero_p(width) || mpfr_cmp(width, tolerance) <= 0) {
                terminalc_fail(c, "solve: derivative too small");
                break;
            }
            mpfr_div(step, fx, dfx, MPFR_RNDN);
            mpfr_sub(next, x, step, MPFR_RNDN);
            if (!finite_mpfr(next)) {
                terminalc_fail(c, "solve: iteration left the finite range");
                break;
            }
            mpfr_sub(width, next, x, MPFR_RNDN);
            mpfr_abs(width, width, MPFR_RNDN);
            mpfr_set(x, next, MPFR_RNDN);
            if (mpfr_cmp(width, tolerance) <= 0) {
                out->type = TERMINALC_REAL;
                mpfr_set(out->r, x, MPFR_RNDN);
                ok = true;
                break;
            }
        }
        if (!ok && !c->error)
            terminalc_fail(c, "solve: did not converge");
    }
    mpfr_clears(a, b, fa, fb, m, fm, tolerance, width, x, fx, dfx, step,
                next, (mpfr_ptr)0);
    return ok;
}

/* ========== Mensuration ========== */

static bool geometry_arg(TerminalCCalc *c, TerminalCValue *args, size_t i,
                         mpfr_t out)
{
    if (!terminalc_is_scalar(&args[i]) ||
        (args[i].type == TERMINALC_MEASURE &&
         !valid_measure_unit(&args[i])) ||
        !mag_as_mpfr(c, out, &args[i])) {
        terminalc_fail(c, "geometry arguments must be finite scalar lengths");
        return false;
    }
    if (mpfr_sgn(out) < 0) {
        terminalc_fail(c, "geometry lengths must be non-negative");
        return false;
    }
    return true;
}

static bool geometry_result(TerminalCCalc *c, TerminalCValue *out,
                            mpfr_srcptr value)
{
    if (!finite_mpfr(value)) {
        terminalc_fail(c, "geometry result is not finite");
        return false;
    }
    if (mpfr_integer_p(value)) {
        mpz_t z;
        mpz_init(z);
        mpfr_get_z(z, value, MPFR_RNDN);
        out->type = TERMINALC_RAT;
        mpq_set_z(out->q, z);
        mpz_clear(z);
    } else {
        out->type = TERMINALC_REAL;
        mpfr_set(out->r, value, MPFR_RNDN);
    }
    return true;
}

TerminalCValue terminalc_module_call(TerminalCCalc *c, const char *module, const char *method,
                       TerminalCValue *args, size_t count)
{
    TerminalCValue o;
    terminalc_value_init(&o, c->bits);

    mpfr_t a, b, d, e, s, t, pi;
    mpfr_inits2(c->bits, a, b, d, e, s, t, pi, (mpfr_ptr)0);

    /* measure.to(unit) */
    if (strcmp(method, "to") == 0 && count == 1) {
        /* module is not used — this is called as wrong path; handled elsewhere */
    }

    if (strcmp(module, "square") == 0) {
        if (strcmp(method, "area") == 0 && count == 1) {
            if (geometry_arg(c, args, 0, a)) {
                mpfr_mul(t, a, a, MPFR_RNDN);
                (void)geometry_result(c, &o, t);
            }
        } else if (strcmp(method, "perimeter") == 0 && count == 1) {
            if (geometry_arg(c, args, 0, a)) {
                mpfr_mul_ui(t, a, 4, MPFR_RNDN);
                (void)geometry_result(c, &o, t);
            }
        } else {
            terminalc_fail(c, "unknown square method (try area, perimeter)");
        }
    } else if (strcmp(module, "rectangle") == 0) {
        if (strcmp(method, "area") == 0 && count == 2) {
            if (geometry_arg(c, args, 0, a) && geometry_arg(c, args, 1, b)) {
                mpfr_mul(t, a, b, MPFR_RNDN);
                (void)geometry_result(c, &o, t);
            }
        } else if (strcmp(method, "perimeter") == 0 && count == 2) {
            if (geometry_arg(c, args, 0, a) && geometry_arg(c, args, 1, b)) {
                mpfr_add(t, a, b, MPFR_RNDN);
                mpfr_mul_ui(t, t, 2, MPFR_RNDN);
                (void)geometry_result(c, &o, t);
            }
        } else {
            terminalc_fail(c, "unknown rectangle method");
        }
    } else if (strcmp(module, "circle") == 0) {
        if (strcmp(method, "area") == 0 && count == 1) {
            if (geometry_arg(c, args, 0, a)) {
                mpfr_const_pi(pi, MPFR_RNDN);
                mpfr_mul(t, a, a, MPFR_RNDN);
                mpfr_mul(t, t, pi, MPFR_RNDN);
                (void)geometry_result(c, &o, t);
            }
        } else if (strcmp(method, "circumference") == 0 && count == 1) {
            if (geometry_arg(c, args, 0, a)) {
                mpfr_const_pi(pi, MPFR_RNDN);
                mpfr_mul(t, a, pi, MPFR_RNDN);
                mpfr_mul_ui(t, t, 2, MPFR_RNDN);
                (void)geometry_result(c, &o, t);
            }
        } else if (strcmp(method, "diameter") == 0 && count == 1) {
            if (geometry_arg(c, args, 0, a)) {
                mpfr_mul_ui(t, a, 2, MPFR_RNDN);
                (void)geometry_result(c, &o, t);
            }
        } else {
            terminalc_fail(c, "unknown circle method");
        }
    } else if (strcmp(module, "triangle") == 0) {
        if (strcmp(method, "area") == 0 && count == 2) {
            if (geometry_arg(c, args, 0, a) && geometry_arg(c, args, 1, b)) {
                mpfr_mul(t, a, b, MPFR_RNDN);
                mpfr_div_ui(t, t, 2, MPFR_RNDN);
                (void)geometry_result(c, &o, t);
            }
        } else if (strcmp(method, "area_heron") == 0 && count == 3) {
            if (geometry_arg(c, args, 0, a) && geometry_arg(c, args, 1, b) &&
                geometry_arg(c, args, 2, d)) {
                mpfr_add(s, a, b, MPFR_RNDN);
                mpfr_add(s, s, d, MPFR_RNDN);
                mpfr_div_ui(s, s, 2, MPFR_RNDN);
                mpfr_sub(t, s, a, MPFR_RNDN);
                mpfr_mul(t, t, s, MPFR_RNDN);
                mpfr_sub(e, s, b, MPFR_RNDN);
                mpfr_mul(t, t, e, MPFR_RNDN);
                mpfr_sub(e, s, d, MPFR_RNDN);
                mpfr_mul(t, t, e, MPFR_RNDN);
                if (mpfr_sgn(t) < 0)
                    terminalc_fail(c, "triangle sides do not form a valid triangle");
                else {
                    mpfr_sqrt(t, t, MPFR_RNDN);
                    (void)geometry_result(c, &o, t);
                }
            }
        } else {
            terminalc_fail(c, "unknown triangle method");
        }
    } else if (strcmp(module, "cube") == 0) {
        if (strcmp(method, "volume") == 0 && count == 1) {
            if (geometry_arg(c, args, 0, a)) {
                mpfr_mul(t, a, a, MPFR_RNDN);
                mpfr_mul(t, t, a, MPFR_RNDN);
                (void)geometry_result(c, &o, t);
            }
        } else if (strcmp(method, "surface") == 0 && count == 1) {
            if (geometry_arg(c, args, 0, a)) {
                mpfr_mul(t, a, a, MPFR_RNDN);
                mpfr_mul_ui(t, t, 6, MPFR_RNDN);
                (void)geometry_result(c, &o, t);
            }
        } else {
            terminalc_fail(c, "unknown cube method");
        }
    } else if (strcmp(module, "cuboid") == 0) {
        if (strcmp(method, "volume") == 0 && count == 3) {
            if (geometry_arg(c, args, 0, a) && geometry_arg(c, args, 1, b) &&
                geometry_arg(c, args, 2, d)) {
                mpfr_mul(t, a, b, MPFR_RNDN);
                mpfr_mul(t, t, d, MPFR_RNDN);
                (void)geometry_result(c, &o, t);
            }
        } else if (strcmp(method, "surface") == 0 && count == 3) {
            if (geometry_arg(c, args, 0, a) && geometry_arg(c, args, 1, b) &&
                geometry_arg(c, args, 2, d)) {
                mpfr_mul(t, a, b, MPFR_RNDN);
                mpfr_mul(e, a, d, MPFR_RNDN);
                mpfr_add(t, t, e, MPFR_RNDN);
                mpfr_mul(e, b, d, MPFR_RNDN);
                mpfr_add(t, t, e, MPFR_RNDN);
                mpfr_mul_ui(t, t, 2, MPFR_RNDN);
                (void)geometry_result(c, &o, t);
            }
        } else {
            terminalc_fail(c, "unknown cuboid method");
        }
    } else if (strcmp(module, "sphere") == 0) {
        if (strcmp(method, "volume") == 0 && count == 1) {
            if (geometry_arg(c, args, 0, a)) {
                mpfr_const_pi(pi, MPFR_RNDN);
                mpfr_mul(t, a, a, MPFR_RNDN);
                mpfr_mul(t, t, a, MPFR_RNDN);
                mpfr_mul(t, t, pi, MPFR_RNDN);
                mpfr_mul_ui(t, t, 4, MPFR_RNDN);
                mpfr_div_ui(t, t, 3, MPFR_RNDN);
                (void)geometry_result(c, &o, t);
            }
        } else if (strcmp(method, "surface") == 0 && count == 1) {
            if (geometry_arg(c, args, 0, a)) {
                mpfr_const_pi(pi, MPFR_RNDN);
                mpfr_mul(t, a, a, MPFR_RNDN);
                mpfr_mul(t, t, pi, MPFR_RNDN);
                mpfr_mul_ui(t, t, 4, MPFR_RNDN);
                (void)geometry_result(c, &o, t);
            }
        } else {
            terminalc_fail(c, "unknown sphere method");
        }
    } else if (strcmp(module, "cylinder") == 0) {
        if (strcmp(method, "volume") == 0 && count == 2) {
            if (geometry_arg(c, args, 0, a) && geometry_arg(c, args, 1, b)) {
                mpfr_const_pi(pi, MPFR_RNDN);
                mpfr_mul(t, a, a, MPFR_RNDN);
                mpfr_mul(t, t, b, MPFR_RNDN);
                mpfr_mul(t, t, pi, MPFR_RNDN);
                (void)geometry_result(c, &o, t);
            }
        } else {
            terminalc_fail(c, "unknown cylinder method");
        }
    } else if (strcmp(module, "cone") == 0) {
        if (strcmp(method, "volume") == 0 && count == 2) {
            if (geometry_arg(c, args, 0, a) && geometry_arg(c, args, 1, b)) {
                mpfr_const_pi(pi, MPFR_RNDN);
                mpfr_mul(t, a, a, MPFR_RNDN);
                mpfr_mul(t, t, b, MPFR_RNDN);
                mpfr_mul(t, t, pi, MPFR_RNDN);
                mpfr_div_ui(t, t, 3, MPFR_RNDN);
                (void)geometry_result(c, &o, t);
            }
        } else {
            terminalc_fail(c, "unknown cone method");
        }
    } else {
        terminalc_fail(c, "unknown module");
    }

    mpfr_clears(a, b, d, e, s, t, pi, (mpfr_ptr)0);
    for (size_t i = 0; i < count; i++)
        terminalc_value_clear(&args[i]);
    return o;
}
