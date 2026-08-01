#include "parse_internal.h"

#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
/* ctype already used for names / spaces */

void terminalc_fail(TerminalCCalc *c, const char *msg)
{
    if (!c->error) {
        c->error = msg;
        c->error_pos = (c->at && c->text) ? (size_t)(c->at - c->text) : 0;
    }
}

void terminalc_skip(TerminalCCalc *c)
{
    while (isspace((unsigned char)*c->at))
        c->at++;
}

bool terminalc_eat(TerminalCCalc *c, char ch)
{
    terminalc_skip(c);
    if (*c->at == ch) {
        c->at++;
        return true;
    }
    return false;
}

bool terminalc_read_name(TerminalCCalc *c, char out[TERMINALC_NAME_MAX])
{
    terminalc_skip(c);
    if (!isalpha((unsigned char)*c->at) && *c->at != '_')
        return false;
    size_t n = 0;
    while (isalnum((unsigned char)*c->at) || *c->at == '_') {
        if (n + 1 < TERMINALC_NAME_MAX)
            out[n++] = (char)tolower((unsigned char)*c->at);
        c->at++;
    }
    out[n] = '\0';
    return true;
}

static void get_var(TerminalCCalc *c, const char *n, TerminalCValue *out)
{
    if (strcmp(n, "true") == 0) {
        terminalc_value_set_bool(out, true);
        return;
    }
    if (strcmp(n, "false") == 0) {
        terminalc_value_set_bool(out, false);
        return;
    }
    if (strcmp(n, "pi") == 0) {
        out->type = TERMINALC_REAL;
        mpfr_const_pi(out->r, MPFR_RNDN);
        return;
    }
    if (strcmp(n, "e") == 0) {
        out->type = TERMINALC_REAL;
        mpfr_set_ui(out->r, 1, MPFR_RNDN);
        mpfr_exp(out->r, out->r, MPFR_RNDN);
        return;
    }
    if (strcmp(n, "phi") == 0) {
        out->type = TERMINALC_REAL;
        mpfr_sqrt_ui(out->r, 5, MPFR_RNDN);
        mpfr_add_ui(out->r, out->r, 1, MPFR_RNDN);
        mpfr_div_ui(out->r, out->r, 2, MPFR_RNDN);
        return;
    }
    if (strcmp(n, "sqrt2") == 0) {
        out->type = TERMINALC_REAL;
        mpfr_sqrt_ui(out->r, 2, MPFR_RNDN);
        return;
    }
    if (strcmp(n, "ln2") == 0) {
        out->type = TERMINALC_REAL;
        mpfr_const_log2(out->r, MPFR_RNDN);
        return;
    }
    if (strcmp(n, "ans") == 0 || strcmp(n, "_") == 0) {
        terminalc_value_copy(out, &c->ans);
        return;
    }
    if (strcmp(n, "x") == 0) {
        terminalc_value_copy(out, &c->x);
        return;
    }
    if (terminalc_try_unit_constant(n, out, c->bits))
        return;
    for (size_t i = 0; i < c->var_count; i++) {
        if (strcmp(n, c->vars[i].name) == 0) {
            terminalc_value_copy(out, &c->vars[i].value);
            return;
        }
    }
    terminalc_fail(c, "unknown identifier");
    terminalc_value_set_si(out, 0);
}

static void q_pow_ui(mpq_t out, const mpq_t base, unsigned long exp)
{
    mpz_pow_ui(mpq_numref(out), mpq_numref(base), exp);
    mpz_pow_ui(mpq_denref(out), mpq_denref(base), exp);
    mpq_canonicalize(out);
}

static bool is_expr_fn(const char *n)
{
    return strcmp(n, "solve") == 0 || strcmp(n, "diff") == 0 ||
           strcmp(n, "integral") == 0 || strcmp(n, "sum") == 0 ||
           strcmp(n, "sigma") == 0;
}

/* Scan first argument text for solve/diff/integral (comma or ')' at depth 0). */
static bool take_raw_arg(TerminalCCalc *c, char *buf, size_t bufsz)
{
    terminalc_skip(c);
    const char *start = c->at;
    int depth = 0;
    while (*c->at) {
        char ch = *c->at;
        if (ch == '(' || ch == '[')
            depth++;
        else if (ch == ')' || ch == ']') {
            if (depth == 0)
                break;
            depth--;
        } else if (ch == ',' && depth == 0)
            break;
        c->at++;
    }
    size_t n = (size_t)(c->at - start);
    while (n > 0 && isspace((unsigned char)start[n - 1]))
        n--;
    if (n == 0 || n >= bufsz)
        return false;
    memcpy(buf, start, n);
    buf[n] = '\0';
    return true;
}

static TerminalCValue primary(TerminalCCalc *c)
{
    TerminalCValue o;
    terminalc_value_init(&o, c->bits);
    terminalc_skip(c);

    if (terminalc_eat(c, '(')) {
        terminalc_value_clear(&o);
        o = terminalc_expression(c);
        if (!terminalc_eat(c, ')'))
            terminalc_fail(c, "expected ')' after expression");
        /* optional .to(unit) on parenthesized measure */
        if (terminalc_eat(c, '.')) {
            char method[TERMINALC_NAME_MAX];
            if (!terminalc_read_name(c, method)) {
                terminalc_fail(c, "expected method name after '.'");
                return o;
            }
            if (strcmp(method, "to") == 0 && terminalc_eat(c, '(')) {
                TerminalCValue u = terminalc_expression(c);
                if (!terminalc_eat(c, ')'))
                    terminalc_fail(c, "expected ')'");
                TerminalCValue conv;
                terminalc_value_init(&conv, c->bits);
                if (terminalc_units_to(c, &o, &u, &conv)) {
                    terminalc_value_clear(&o);
                    o = conv;
                } else {
                    terminalc_value_clear(&conv);
                }
                terminalc_value_clear(&u);
            } else {
                terminalc_fail(c, "unknown method");
            }
        }
        return o;
    }

    /* Matrix / vector literal: [1, 2; 3, 4] */
    if (terminalc_eat(c, '[')) {
        TerminalCValue *cells = NULL;
        size_t ncells = 0, cap = 0;
        size_t cols0 = 0;
        size_t rows = 0;
        bool first_row = true;
        for (;;) {
            terminalc_skip(c);
            if (*c->at == ']') {
                c->at++;
                break;
            }
            size_t row_cols = 0;
            for (;;) {
                TerminalCValue cell = terminalc_expression(c);
                if (ncells == cap) {
                    cap = cap ? cap * 2 : 16;
                    TerminalCValue *ncv =
                        (TerminalCValue *)realloc(cells, cap * sizeof(TerminalCValue));
                    if (!ncv) {
                        terminalc_fail(c, "out of memory");
                        terminalc_value_clear(&cell);
                        break;
                    }
                    cells = ncv;
                }
                cells[ncells++] = cell;
                row_cols++;
                terminalc_skip(c);
                if (terminalc_eat(c, ','))
                    continue;
                break;
            }
            if (first_row) {
                cols0 = row_cols;
                first_row = false;
            } else if (row_cols != cols0) {
                terminalc_fail(c, "matrix rows must have equal length");
            }
            rows++;
            terminalc_skip(c);
            if (terminalc_eat(c, ';'))
                continue;
            if (terminalc_eat(c, ']'))
                break;
            if (*c->at == ']') {
                c->at++;
                break;
            }
            terminalc_fail(c, "expected ',' ';' or ']' in matrix");
            break;
        }
        if (!c->error && ncells > 0) {
            if (rows == 1 && cols0 == ncells) {
                /* could be 1×n matrix or vector — use vector if single row
                 * without semicolon; we count rows from ; so 1 row = vector */
                size_t sh[1] = {cols0};
                if (terminalc_tensor_create(c, &o, 1, sh)) {
                    for (size_t i = 0; i < cols0; i++)
                        terminalc_value_copy(&o.ten->data[i], &cells[i]);
                }
            } else {
                size_t sh[2] = {rows, cols0};
                if (terminalc_tensor_create(c, &o, 2, sh)) {
                    for (size_t i = 0; i < ncells; i++)
                        terminalc_value_copy(&o.ten->data[i], &cells[i]);
                }
            }
        }
        for (size_t i = 0; i < ncells; i++)
            terminalc_value_clear(&cells[i]);
        free(cells);
        return o;
    }

    /* Base-prefixed integers: 0xFF, 0b1010, 0o77 (and uppercase X/B/O). */
    if (c->at[0] == '0' && c->at[1] != '\0') {
        char pfx = (char)tolower((unsigned char)c->at[1]);
        int base = 0;
        if (pfx == 'x')
            base = 16;
        else if (pfx == 'b')
            base = 2;
        else if (pfx == 'o')
            base = 8;
        if (base != 0) {
            const char *digits = c->at + 2;
            const char *p = digits;
            while (*p) {
                unsigned char ch = (unsigned char)*p;
                int ok = 0;
                if (base == 16)
                    ok = isxdigit(ch);
                else if (base == 8)
                    ok = (ch >= '0' && ch <= '7');
                else
                    ok = (ch == '0' || ch == '1');
                if (!ok)
                    break;
                p++;
            }
            if (p == digits) {
                terminalc_fail(c, "expected digits after base prefix");
                return o;
            }
            size_t n = (size_t)(p - digits);
            if (n >= 256) {
                terminalc_fail(c, "number literal too long");
                return o;
            }
            char buf[256];
            memcpy(buf, digits, n);
            buf[n] = '\0';
            if (mpz_set_str(mpq_numref(o.q), buf, base) != 0) {
                terminalc_fail(c, "invalid base-prefixed integer");
                return o;
            }
            mpz_set_ui(mpq_denref(o.q), 1);
            o.type = TERMINALC_RAT;
            c->at = p;
            return o;
        }
    }

    if (isdigit((unsigned char)*c->at) || *c->at == '.') {
        const char *s = c->at;
        bool real = false;
        while (isdigit((unsigned char)*c->at))
            c->at++;
        if (*c->at == '.') {
            real = true;
            c->at++;
            while (isdigit((unsigned char)*c->at))
                c->at++;
        }
        if (*c->at == 'e' || *c->at == 'E') {
            real = true;
            c->at++;
            if (*c->at == '+' || *c->at == '-')
                c->at++;
            const char *edigits = c->at;
            while (isdigit((unsigned char)*c->at))
                c->at++;
            if (edigits == c->at)
                terminalc_fail(c, "invalid exponent");
        }
        char buf[256];
        size_t n = (size_t)(c->at - s);
        if (n >= sizeof buf) {
            terminalc_fail(c, "number literal too long");
            return o;
        }
        memcpy(buf, s, n);
        buf[n] = '\0';
        if (real) {
            o.type = TERMINALC_REAL;
            if (mpfr_set_str(o.r, buf, 10, MPFR_RNDN) != 0)
                terminalc_fail(c, "invalid number");
        } else if (mpq_set_str(o.q, buf, 10) == 0) {
            o.type = TERMINALC_RAT;
            mpq_canonicalize(o.q);
        } else {
            terminalc_fail(c, "invalid integer");
        }
        return o;
    }

    char name[TERMINALC_NAME_MAX];
    if (terminalc_read_name(c, name)) {
        /* module.method(args) — mensuration etc. */
        if (terminalc_eat(c, '.')) {
            char method[TERMINALC_NAME_MAX];
            if (!terminalc_read_name(c, method)) {
                terminalc_fail(c, "expected method name");
                return o;
            }
            if (!terminalc_eat(c, '(')) {
                terminalc_fail(c, "expected '(' after method");
                return o;
            }
            TerminalCValue a[TERMINALC_MAX_ARGS];
            size_t n = 0;
            if (!terminalc_eat(c, ')')) {
                do {
                    if (n == TERMINALC_MAX_ARGS) {
                        terminalc_fail(c, "too many arguments");
                        break;
                    }
                    a[n++] = terminalc_expression(c);
                } while (terminalc_eat(c, ','));
                if (!terminalc_eat(c, ')'))
                    terminalc_fail(c, "expected ')'");
            }
            terminalc_value_clear(&o);
            return terminalc_module_call(c, name, method, a, n);
        }

        if (terminalc_eat(c, '(')) {
            /* solve/diff/integral: first arg is raw expression text */
            if (is_expr_fn(name)) {
                char expr_buf[TERMINALC_LINE_MAX];
                if (!take_raw_arg(c, expr_buf, sizeof expr_buf)) {
                    terminalc_fail(c, "expected expression argument");
                    return o;
                }
                TerminalCValue a[TERMINALC_MAX_ARGS];
                size_t n = 0;
                if (terminalc_eat(c, ',')) {
                    do {
                        if (n == TERMINALC_MAX_ARGS)
                            break;
                        a[n++] = terminalc_expression(c);
                    } while (terminalc_eat(c, ','));
                }
                if (!terminalc_eat(c, ')'))
                    terminalc_fail(c, "expected ')'");
                terminalc_value_clear(&o);
                return terminalc_call_expr_fn(c, name, expr_buf, a, n);
            }

            TerminalCValue a[TERMINALC_MAX_ARGS];
            size_t n = 0;
            if (!terminalc_eat(c, ')')) {
                do {
                    if (n == TERMINALC_MAX_ARGS) {
                        terminalc_fail(c, "too many function arguments");
                        break;
                    }
                    a[n++] = terminalc_expression(c);
                } while (terminalc_eat(c, ','));
                if (!terminalc_eat(c, ')'))
                    terminalc_fail(c, "expected ')' after function argument");
            }
            terminalc_value_clear(&o);
            return terminalc_call(c, name, a, n);
        }
        get_var(c, name, &o);
        return o;
    }

    terminalc_fail(c, "expected a number, identifier, matrix, or '('");
    return o;
}

/* Keyword match (case-insensitive); must not steal prefixes of identifiers. */
static bool eat_kw(TerminalCCalc *c, const char *kw)
{
    terminalc_skip(c);
    const char *p = c->at;
    size_t i = 0;
    for (; kw[i]; i++) {
        unsigned char ch = (unsigned char)p[i];
        if (!ch || tolower(ch) != (unsigned char)kw[i])
            return false;
    }
    if (isalnum((unsigned char)p[i]) || p[i] == '_')
        return false;
    c->at += i;
    return true;
}

static TerminalCValue unary(TerminalCCalc *c);
static TerminalCValue unary_impl(TerminalCCalc *c);

static TerminalCValue unary(TerminalCCalc *c)
{
    if (c->parse_depth >= TERMINALC_PARSE_MAX_DEPTH) {
        TerminalCValue o;
        terminalc_value_init(&o, c->bits);
        terminalc_fail(c, "expression nesting is too deep");
        return o;
    }
    c->parse_depth++;
    TerminalCValue o = unary_impl(c);
    c->parse_depth--;
    return o;
}

static TerminalCValue unary_impl(TerminalCCalc *c)
{
    if (terminalc_eat(c, '+'))
        return unary(c);
    if (terminalc_eat(c, '-')) {
        TerminalCValue v = unary(c);
        if (v.type == TERMINALC_BOOL) {
            /* numeric negation: true→-1, false→0 */
            long n = v.b ? -1 : 0;
            terminalc_value_set_si(&v, n);
            return v;
        }
        if (v.type == TERMINALC_RAT)
            mpq_neg(v.q, v.q);
        else
            mpfr_neg(v.r, v.r, MPFR_RNDN);
        return v;
    }
    if (terminalc_eat(c, '!') || eat_kw(c, "not")) {
        TerminalCValue v = unary(c);
        bool t = terminalc_is_truthy(&v);
        terminalc_value_clear(&v);
        TerminalCValue o;
        terminalc_value_init(&o, c->bits);
        terminalc_value_set_bool(&o, !t);
        return o;
    }
    return primary(c);
}

static TerminalCValue power(TerminalCCalc *c)
{
    TerminalCValue a = unary(c);
    if (terminalc_eat(c, '^')) {
        c->out_base = 0;
        TerminalCValue b;
        if (c->parse_depth >= TERMINALC_PARSE_MAX_DEPTH) {
            terminalc_value_init(&b, c->bits);
            terminalc_fail(c, "expression nesting is too deep");
        } else {
            c->parse_depth++;
            b = power(c);
            c->parse_depth--;
        }
        TerminalCValue o;
        terminalc_value_init(&o, c->bits);
        if (a.type == TERMINALC_RAT && terminalc_is_integer(&b) &&
            mpz_fits_slong_p(mpq_numref(b.q))) {
            o.type = TERMINALC_RAT;
            long e = mpz_get_si(mpq_numref(b.q));
            unsigned long mag = e < 0 ? (unsigned long)(-(e + 1)) + 1UL
                                      : (unsigned long)e;
            size_t num_bits = mpz_sizeinbase(mpq_numref(a.q), 2);
            size_t den_bits = mpz_sizeinbase(mpq_denref(a.q), 2);
            size_t base_bits = num_bits > den_bits ? num_bits : den_bits;
            if (e < 0 && mpq_sgn(a.q) == 0) {
                terminalc_fail(c, "zero cannot be raised to a negative power");
            } else if (mag != 0 &&
                       base_bits > TERMINALC_EXACT_RESULT_MAX_BITS / mag) {
                terminalc_fail(c, "exact power result is too large");
            } else {
                q_pow_ui(o.q, a.q, mag);
                if (e < 0)
                    mpq_inv(o.q, o.q);
            }
        } else {
            o.type = TERMINALC_REAL;
            mpfr_t x, y;
            mpfr_init2(x, c->bits);
            mpfr_init2(y, c->bits);
            terminalc_as_real(c, x, &a);
            terminalc_as_real(c, y, &b);
            mpfr_pow(o.r, x, y, MPFR_RNDN);
            mpfr_clear(x);
            mpfr_clear(y);
        }
        terminalc_value_clear(&a);
        terminalc_value_clear(&b);
        return o;
    }
    return a;
}

static TerminalCValue term(TerminalCCalc *c)
{
    TerminalCValue a = power(c);
    for (;;) {
        char op = 0;
        if (terminalc_eat(c, '*'))
            op = '*';
        else if (terminalc_eat(c, '/'))
            op = '/';
        else if (terminalc_eat(c, '%'))
            op = '%';
        else
            break;

        c->out_base = 0;
        TerminalCValue b = power(c);
        if (op == '*' && a.type == TERMINALC_TENSOR && b.type == TERMINALC_TENSOR) {
            TerminalCValue r;
            terminalc_value_init(&r, c->bits);
            if (terminalc_linalg_matmul(c, &a, &b, &r)) {
                terminalc_value_clear(&a);
                a = r;
            } else {
                terminalc_value_clear(&r);
            }
            terminalc_value_clear(&b);
            continue;
        }
        if ((a.type == TERMINALC_TENSOR && terminalc_is_scalar(&b)) ||
            (terminalc_is_scalar(&a) && b.type == TERMINALC_TENSOR)) {
            TerminalCValue r;
            terminalc_value_init(&r, c->bits);
            bool ok = op != '%' && terminalc_tensor_ew_muldiv(c, &a, &b, op, &r);
            if (op == '%')
                terminalc_fail(c, "modulo is not defined for tensors");
            if (ok) {
                terminalc_value_clear(&a);
                a = r;
            } else {
                terminalc_value_clear(&r);
            }
            terminalc_value_clear(&b);
            continue;
        }
        if (op == '*' && terminalc_is_scalar(&a) && b.type == TERMINALC_MEASURE) {
            TerminalCValue r;
            terminalc_value_init(&r, c->bits);
            if (terminalc_units_mul(c, &a, &b, &r)) {
                terminalc_value_clear(&a);
                a = r;
            } else {
                terminalc_value_clear(&r);
            }
            terminalc_value_clear(&b);
            continue;
        }
        if (op == '*' && a.type == TERMINALC_MEASURE && terminalc_is_scalar(&b)) {
            TerminalCValue r;
            terminalc_value_init(&r, c->bits);
            if (terminalc_units_mul(c, &b, &a, &r)) {
                terminalc_value_clear(&a);
                a = r;
            } else {
                terminalc_value_clear(&r);
            }
            terminalc_value_clear(&b);
            continue;
        }
        /* length / time -> speed, e.g. (5*m)/s or 5*m/s */
        if (op == '/' && a.type == TERMINALC_MEASURE && b.type == TERMINALC_MEASURE) {
            TerminalCValue r;
            terminalc_value_init(&r, c->bits);
            if (terminalc_units_div(c, &a, &b, &r)) {
                terminalc_value_clear(&a);
                a = r;
            } else {
                terminalc_value_clear(&r);
            }
            terminalc_value_clear(&b);
            continue;
        }
        if (a.type == TERMINALC_RAT && b.type == TERMINALC_RAT && op != '%') {
            if (op == '*') {
                mpq_mul(a.q, a.q, b.q);
            } else {
                if (mpq_sgn(b.q) == 0)
                    terminalc_fail(c, "division by zero");
                else
                    mpq_div(a.q, a.q, b.q);
            }
        } else if (a.type != TERMINALC_TENSOR && b.type != TERMINALC_TENSOR &&
                   a.type != TERMINALC_MEASURE && b.type != TERMINALC_MEASURE) {
            mpfr_t x, y;
            mpfr_init2(x, c->bits);
            mpfr_init2(y, c->bits);
            terminalc_as_real(c, x, &a);
            terminalc_as_real(c, y, &b);
            a.type = TERMINALC_REAL;
            if ((op == '/' || op == '%') && mpfr_zero_p(y))
                terminalc_fail(c, op == '/' ? "division by zero" : "modulo by zero");
            else if (op == '*')
                mpfr_mul(a.r, x, y, MPFR_RNDN);
            else if (op == '/')
                mpfr_div(a.r, x, y, MPFR_RNDN);
            else
                mpfr_fmod(a.r, x, y, MPFR_RNDN);
            mpfr_clear(x);
            mpfr_clear(y);
        } else {
            terminalc_fail(c, "invalid operands for * / %");
        }
        terminalc_value_clear(&b);
    }
    return a;
}

static TerminalCValue additive(TerminalCCalc *c)
{
    TerminalCValue a = term(c);
    for (;;) {
        char op = 0;
        if (terminalc_eat(c, '+'))
            op = '+';
        else if (terminalc_eat(c, '-'))
            op = '-';
        else
            break;

        c->out_base = 0;
        TerminalCValue b = term(c);
        if (a.type == TERMINALC_TENSOR && b.type == TERMINALC_TENSOR) {
            TerminalCValue r;
            terminalc_value_init(&r, c->bits);
            if (terminalc_tensor_ew_add(c, &a, &b, op == '-', &r)) {
                terminalc_value_clear(&a);
                a = r;
            } else {
                terminalc_value_clear(&r);
            }
            terminalc_value_clear(&b);
            continue;
        }
        if ((a.type == TERMINALC_TENSOR && terminalc_is_scalar(&b)) ||
            (terminalc_is_scalar(&a) && b.type == TERMINALC_TENSOR)) {
            TerminalCValue r;
            terminalc_value_init(&r, c->bits);
            if (terminalc_tensor_ew_add(c, &a, &b, op == '-', &r)) {
                terminalc_value_clear(&a);
                a = r;
            } else {
                terminalc_value_clear(&r);
            }
            terminalc_value_clear(&b);
            continue;
        }
        if (a.type == TERMINALC_RAT && b.type == TERMINALC_RAT) {
            if (op == '+')
                mpq_add(a.q, a.q, b.q);
            else
                mpq_sub(a.q, a.q, b.q);
        } else if (terminalc_is_scalar(&a) && terminalc_is_scalar(&b) &&
                   a.type != TERMINALC_MEASURE && b.type != TERMINALC_MEASURE) {
            mpfr_t x, y;
            mpfr_init2(x, c->bits);
            mpfr_init2(y, c->bits);
            terminalc_as_real(c, x, &a);
            terminalc_as_real(c, y, &b);
            a.type = TERMINALC_REAL;
            if (op == '+')
                mpfr_add(a.r, x, y, MPFR_RNDN);
            else
                mpfr_sub(a.r, x, y, MPFR_RNDN);
            mpfr_clear(x);
            mpfr_clear(y);
        } else {
            terminalc_fail(c, "invalid operands for + / -");
        }
        terminalc_value_clear(&b);
    }
    return a;
}

/* Compare two values; returns -1/0/1 like memcmp, or sets error. */
static int value_cmp(TerminalCCalc *c, const TerminalCValue *a, const TerminalCValue *b)
{
    if (a->type == TERMINALC_BOOL && b->type == TERMINALC_BOOL) {
        if (a->b == b->b)
            return 0;
        return a->b ? 1 : -1;
    }
    if ((a->type == TERMINALC_RAT || a->type == TERMINALC_BOOL) &&
        (b->type == TERMINALC_RAT || b->type == TERMINALC_BOOL)) {
        mpq_t qa, qb;
        mpq_init(qa);
        mpq_init(qb);
        if (a->type == TERMINALC_BOOL)
            mpq_set_si(qa, a->b ? 1 : 0, 1);
        else
            mpq_set(qa, a->q);
        if (b->type == TERMINALC_BOOL)
            mpq_set_si(qb, b->b ? 1 : 0, 1);
        else
            mpq_set(qb, b->q);
        int r = mpq_cmp(qa, qb);
        mpq_clear(qa);
        mpq_clear(qb);
        return r;
    }
    mpfr_t x, y;
    mpfr_init2(x, c->bits);
    mpfr_init2(y, c->bits);
    terminalc_as_real(c, x, a);
    terminalc_as_real(c, y, b);
    int r = mpfr_cmp(x, y);
    mpfr_clear(x);
    mpfr_clear(y);
    return r;
}

static TerminalCValue comparison(TerminalCCalc *c)
{
    TerminalCValue a = additive(c);
    terminalc_skip(c);
    const char *p = c->at;
    int kind = 0; /* 1== 2!= 3< 4<= 5> 6>= */
    if (p[0] == '=' && p[1] == '=') {
        kind = 1;
        c->at += 2;
    } else if (p[0] == '!' && p[1] == '=') {
        kind = 2;
        c->at += 2;
    } else if (p[0] == '<' && p[1] == '=') {
        kind = 4;
        c->at += 2;
    } else if (p[0] == '>' && p[1] == '=') {
        kind = 6;
        c->at += 2;
    } else if (p[0] == '<') {
        kind = 3;
        c->at += 1;
    } else if (p[0] == '>') {
        kind = 5;
        c->at += 1;
    } else {
        return a;
    }

    c->out_base = 0;
    TerminalCValue b = additive(c);
    int cmp = value_cmp(c, &a, &b);
    bool res = false;
    switch (kind) {
    case 1:
        res = (cmp == 0);
        break;
    case 2:
        res = (cmp != 0);
        break;
    case 3:
        res = (cmp < 0);
        break;
    case 4:
        res = (cmp <= 0);
        break;
    case 5:
        res = (cmp > 0);
        break;
    case 6:
        res = (cmp >= 0);
        break;
    default:
        break;
    }
    terminalc_value_clear(&a);
    terminalc_value_clear(&b);
    TerminalCValue o;
    terminalc_value_init(&o, c->bits);
    terminalc_value_set_bool(&o, res);
    return o;
}

static bool eat_and_op(TerminalCCalc *c)
{
    if (eat_kw(c, "and"))
        return true;
    terminalc_skip(c);
    if (c->at[0] == '&' && c->at[1] == '&') {
        c->at += 2;
        return true;
    }
    return false;
}

static bool eat_or_op(TerminalCCalc *c)
{
    if (eat_kw(c, "or"))
        return true;
    terminalc_skip(c);
    if (c->at[0] == '|' && c->at[1] == '|') {
        c->at += 2;
        return true;
    }
    return false;
}

static TerminalCValue and_expr(TerminalCCalc *c)
{
    TerminalCValue a = comparison(c);
    while (eat_and_op(c)) {
        c->out_base = 0;
        TerminalCValue b = comparison(c);
        bool res = terminalc_is_truthy(&a) && terminalc_is_truthy(&b);
        terminalc_value_clear(&a);
        terminalc_value_clear(&b);
        terminalc_value_init(&a, c->bits);
        terminalc_value_set_bool(&a, res);
    }
    return a;
}

static TerminalCValue or_expr(TerminalCCalc *c)
{
    TerminalCValue a = and_expr(c);
    while (eat_or_op(c)) {
        c->out_base = 0;
        TerminalCValue b = and_expr(c);
        bool res = terminalc_is_truthy(&a) || terminalc_is_truthy(&b);
        terminalc_value_clear(&a);
        terminalc_value_clear(&b);
        terminalc_value_init(&a, c->bits);
        terminalc_value_set_bool(&a, res);
    }
    return a;
}

TerminalCValue terminalc_expression(TerminalCCalc *c)
{
    return or_expr(c);
}

static bool eval_impl(TerminalCCalc *c, const char *line, TerminalCValue *out)
{
    c->text = line;
    c->at = line;
    c->error = NULL;
    c->error_pos = 0;
    c->parse_depth = 0;
    c->out_base = 0;
    *out = terminalc_expression(c);
    terminalc_skip(c);
    if (!c->error && *c->at)
        terminalc_fail(c, "unexpected input");
    return !c->error;
}

bool terminalc_eval_nested(TerminalCCalc *c, const char *line,
                           TerminalCValue *out)
{
    const char *saved_text = c->text;
    const char *saved_at = c->at;
    const char *saved_error = c->error;
    size_t saved_error_pos = c->error_pos;
    unsigned saved_depth = c->parse_depth;
    int saved_out_base = c->out_base;

    bool ok = eval_impl(c, line, out);
    const char *nested_error = c->error;

    c->text = saved_text;
    c->at = saved_at;
    c->parse_depth = saved_depth;
    c->out_base = saved_out_base;
    if (saved_error) {
        c->error = saved_error;
        c->error_pos = saved_error_pos;
    } else if (!ok) {
        c->error = nested_error;
        c->error_pos = (saved_text && saved_at)
                           ? (size_t)(saved_at - saved_text)
                           : 0;
    } else {
        c->error = NULL;
        c->error_pos = saved_error_pos;
    }
    return ok;
}

bool terminalc_eval(TerminalCCalc *c, const char *line, TerminalCValue *out)
{
    if (!c || !out)
        return false;
    free(c->owned_text);
    c->owned_text = NULL;
    if (!line) {
        terminalc_value_init(out, c->bits);
        c->text = "";
        c->at = c->text;
        c->error = "input is null";
        c->error_pos = 0;
        return false;
    }
    return eval_impl(c, line, out);
}

bool terminalc_eval_n(TerminalCCalc *c, const char *s, size_t n, TerminalCValue *out)
{
    if (!c || !out)
        return false;
    free(c->owned_text);
    c->owned_text = NULL;
    if ((!s && n != 0) || n == SIZE_MAX) {
        terminalc_value_init(out, c->bits);
        c->text = "";
        c->at = c->text;
        c->error = !s ? "input is null" : "input is too large";
        c->error_pos = 0;
        return false;
    }
    char *buf = (char *)malloc(n + 1);
    if (!buf) {
        terminalc_value_init(out, c->bits);
        c->text = "";
        c->at = c->text;
        c->error = "out of memory";
        c->error_pos = 0;
        return false;
    }
    if (n != 0)
        memcpy(buf, s, n);
    buf[n] = '\0';
    c->owned_text = buf;
    return eval_impl(c, buf, out);
}
