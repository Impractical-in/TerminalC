#include "terminalc.h"
#include "color.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

TerminalCPolynomial *terminalc_find_poly(TerminalCCalc *c, const char *name)
{
    for (size_t i = 0; i < c->poly_count; i++) {
        if (strcmp(c->polys[i].name, name) == 0)
            return &c->polys[i];
    }
    return NULL;
}

static void free_poly_contents(TerminalCPolynomial *p)
{
    if (p->coeff) {
        for (size_t i = 0; i < p->n; i++)
            mpq_clear(p->coeff[i]);
        free(p->coeff);
        p->coeff = NULL;
    }
    p->n = 0;
}

bool terminalc_quadratic(TerminalCCalc *c, const char *args)
{
    TerminalCValue v[3];
    const char *p = args;

    for (int i = 0; i < 3; i++) {
        const char *q = p;
        while (*q && *q != ',')
            q++;
        if (!terminalc_eval_n(c, p, (size_t)(q - p), &v[i]))
            return false;
        if (i < 2) {
            if (*q != ',') {
                c->error = "solve_quadratic requires three arguments";
                c->error_pos = 0;
                for (int j = 0; j <= i; j++)
                    terminalc_value_clear(&v[j]);
                return false;
            }
            p = q + 1;
        } else if (*q) {
            c->error = "solve_quadratic requires three arguments";
            c->error_pos = 0;
            for (int j = 0; j <= i; j++)
                terminalc_value_clear(&v[j]);
            return false;
        }
    }

    mpfr_t a, b, cc, d, s, q, x1, x2, r;
    mpfr_inits2(c->bits, a, b, cc, d, s, q, x1, x2, r, (mpfr_ptr)0);
    terminalc_as_real(c, a, &v[0]);
    terminalc_as_real(c, b, &v[1]);
    terminalc_as_real(c, cc, &v[2]);

    printf("%s+-- Quadratic Solver -------------------------+%s\n", C_B_CYN, C_RST);
    printf("%s|%s  %sax^2 + bx + c = 0%s\n", C_B_CYN, C_RST, C_DIM, C_RST);

    if (mpfr_zero_p(a)) {
        if (mpfr_zero_p(b)) {
            printf("%s|%s  %s%s%s\n", C_B_CYN, C_RST, C_B_YEL,
                   mpfr_zero_p(cc) ? "Infinitely many solutions." : "No solution.",
                   C_RST);
        } else {
            mpfr_neg(x1, cc, MPFR_RNDN);
            mpfr_div(x1, x1, b, MPFR_RNDN);
            printf("%s|%s  %sLinear root:%s  x = %s", C_B_CYN, C_RST, C_B_YEL, C_RST,
                   C_B_GRN);
            mpfr_printf("%.*RNf", c->digits, x1);
            printf("%s\n", C_RST);
        }
    } else {
        mpfr_mul(d, b, b, MPFR_RNDN);
        mpfr_mul(q, a, cc, MPFR_RNDN);
        mpfr_mul_ui(q, q, 4, MPFR_RNDN);
        mpfr_sub(d, d, q, MPFR_RNDN);

        printf("%s|%s  %sDiscriminant D%s = %s", C_B_CYN, C_RST, C_B_YEL, C_RST,
               C_B_GRN);
        mpfr_printf("%.*RNf", c->digits, d);
        printf("%s\n", C_RST);

        if (mpfr_sgn(d) < 0) {
            mpfr_neg(s, d, MPFR_RNDN);
            mpfr_sqrt(s, s, MPFR_RNDN);
            mpfr_neg(x1, b, MPFR_RNDN);
            mpfr_mul_ui(q, a, 2, MPFR_RNDN);
            mpfr_div(x1, x1, q, MPFR_RNDN);
            mpfr_div(s, s, q, MPFR_RNDN);
            printf("%s|%s  %sComplex roots:%s\n", C_B_CYN, C_RST, C_B_MAG, C_RST);
            printf("%s|%s    x1 = %s", C_B_CYN, C_RST, C_B_GRN);
            mpfr_printf("%.*RNf", c->digits, x1);
            printf("%s + %s", C_RST, C_B_GRN);
            mpfr_printf("%.*RNf", c->digits, s);
            printf("%si%s\n", C_RST, C_RST);
            printf("%s|%s    x2 = %s", C_B_CYN, C_RST, C_B_GRN);
            mpfr_printf("%.*RNf", c->digits, x1);
            printf("%s - %s", C_RST, C_B_GRN);
            mpfr_printf("%.*RNf", c->digits, s);
            printf("%si%s\n", C_RST, C_RST);
        } else {
            /* Stable formula: q = -0.5 * (b + sign(b)*sqrt(D)); x1=q/a; x2=c/q */
            mpfr_sqrt(s, d, MPFR_RNDN);
            mpfr_set(q, b, MPFR_RNDN);
            if (mpfr_sgn(b) >= 0)
                mpfr_add(q, q, s, MPFR_RNDN);
            else
                mpfr_sub(q, q, s, MPFR_RNDN);
            mpfr_neg(q, q, MPFR_RNDN);
            mpfr_div_ui(q, q, 2, MPFR_RNDN);
            mpfr_div(x1, q, a, MPFR_RNDN);
            if (mpfr_zero_p(q))
                mpfr_set(x2, x1, MPFR_RNDN);
            else
                mpfr_div(x2, cc, q, MPFR_RNDN);

            printf("%s|%s  %sReal roots:%s\n", C_B_CYN, C_RST, C_B_YEL, C_RST);
            printf("%s|%s    x1 = %s", C_B_CYN, C_RST, C_B_GRN);
            mpfr_printf("%.*RNf", c->digits, x1);
            printf("%s\n", C_RST);
            printf("%s|%s    x2 = %s", C_B_CYN, C_RST, C_B_GRN);
            mpfr_printf("%.*RNf", c->digits, x2);
            printf("%s\n", C_RST);

            mpfr_mul(r, a, x1, MPFR_RNDN);
            mpfr_add(r, r, b, MPFR_RNDN);
            mpfr_mul(r, r, x1, MPFR_RNDN);
            mpfr_add(r, r, cc, MPFR_RNDN);
            printf("%s|%s  %sResidual f(x1)%s = %s", C_B_CYN, C_RST, C_DIM, C_RST,
                   C_MAG);
            mpfr_printf("%.*RNe", 8, r);
            printf("%s\n", C_RST);

            mpfr_mul(r, a, x2, MPFR_RNDN);
            mpfr_add(r, r, b, MPFR_RNDN);
            mpfr_mul(r, r, x2, MPFR_RNDN);
            mpfr_add(r, r, cc, MPFR_RNDN);
            printf("%s|%s  %sResidual f(x2)%s = %s", C_B_CYN, C_RST, C_DIM, C_RST,
                   C_MAG);
            mpfr_printf("%.*RNe", 8, r);
            printf("%s\n", C_RST);
        }
    }

    printf("%s+-------------------------------------------+%s\n", C_B_CYN, C_RST);

    for (int i = 0; i < 3; i++)
        terminalc_value_clear(&v[i]);
    mpfr_clears(a, b, cc, d, s, q, x1, x2, r, (mpfr_ptr)0);
    return true;
}

bool terminalc_store_poly(TerminalCCalc *c, const char *name, const char *body)
{
    TerminalCPolynomial *p = terminalc_find_poly(c, name);
    if (!p) {
        if (c->poly_count == TERMINALC_VAR_MAX) {
            c->error = "polynomial limit reached";
            c->error_pos = 0;
            return false;
        }
        p = &c->polys[c->poly_count++];
        memset(p, 0, sizeof *p);
        size_t i = 0;
        for (; i + 1 < TERMINALC_NAME_MAX && name[i]; i++)
            p->name[i] = name[i];
        p->name[i] = '\0';
    } else {
        free_poly_contents(p);
    }

    size_t n = 1;
    for (const char *t = body; *t; t++) {
        if (*t == ',')
            n++;
    }

    p->coeff = (mpq_t *)calloc(n, sizeof(mpq_t));
    if (!p->coeff) {
        c->error = "out of memory";
        c->error_pos = 0;
        return false;
    }
    p->n = n;
    for (size_t i = 0; i < n; i++)
        mpq_init(p->coeff[i]);

    const char *s = body;
    for (size_t i = 0; i < n; i++) {
        const char *q = strchr(s, ',');
        TerminalCValue v;
        size_t len = q ? (size_t)(q - s) : strlen(s);
        if (!terminalc_eval_n(c, s, len, &v) || v.type != TERMINALC_RAT) {
            if (!c->error)
                c->error = "polynomial coefficients must be exact rationals";
            terminalc_value_clear(&v);
            free_poly_contents(p);
            return false;
        }
        mpq_set(p->coeff[i], v.q);
        terminalc_value_clear(&v);
        if (q)
            s = q + 1;
    }

    printf("%s+-- Polynomial Stored ---------------------+%s\n", C_B_MAG, C_RST);
    printf("%s|%s  name   %s%s%s\n", C_B_MAG, C_RST, C_B_CYN, p->name, C_RST);
    printf("%s|%s  degree %s%zu%s\n", C_B_MAG, C_RST, C_B_GRN, n - 1, C_RST);
    printf("%s|%s  coeffs %s", C_B_MAG, C_RST, C_YEL);
    for (size_t i = 0; i < n; i++) {
        char *text = mpq_get_str(NULL, 10, p->coeff[i]);
        if (text) {
            fputs(text, stdout);
            free(text);
        }
        if (i + 1 < n)
            fputs(", ", stdout);
    }
    printf("%s\n", C_RST);
    printf("%s+-------------------------------------------+%s\n", C_B_MAG, C_RST);
    return true;
}

void terminalc_poly_eval(TerminalCCalc *c, const TerminalCPolynomial *p, const TerminalCValue *x, mpfr_t out)
{
    mpfr_t t;
    mpfr_init2(t, c->bits);
    mpfr_set_q(out, p->coeff[0], MPFR_RNDN);
    for (size_t i = 1; i < p->n; i++) {
        terminalc_as_real(c, t, x);
        mpfr_mul(out, out, t, MPFR_RNDN);
        mpfr_set_q(t, p->coeff[i], MPFR_RNDN);
        mpfr_add(out, out, t, MPFR_RNDN);
    }
    mpfr_clear(t);
}

bool terminalc_poly_derivative(TerminalCCalc *c, const char *src_name, const char *dst_name)
{
    TerminalCPolynomial *src = terminalc_find_poly(c, src_name);
    if (!src || src->n == 0) {
        c->error = "unknown polynomial";
        c->error_pos = 0;
        return false;
    }

    /* Build exact derivative coefficients: n*a_n, (n-1)*a_{n-1}, ... */
    size_t deg = src->n - 1;
    if (deg == 0) {
        /* constant → zero polynomial */
        return terminalc_store_poly(c, dst_name, "0");
    }

    char *body = NULL;
    size_t cap = 0, len = 0;
    for (size_t i = 0; i < deg; i++) {
        size_t power = deg - i;
        mpq_t term, scale;
        mpq_init(term);
        mpq_init(scale);
        mpq_set(term, src->coeff[i]);
        mpq_set_ui(scale, (unsigned long)power, 1);
        mpq_mul(term, term, scale);
        mpq_clear(scale);
        char *s = mpq_get_str(NULL, 10, term);
        mpq_clear(term);
        if (!s) {
            free(body);
            c->error = "out of memory";
            return false;
        }
        size_t sl = strlen(s);
        size_t need = len + sl + 2;
        if (need > cap) {
            cap = need * 2 + 32;
            char *nb = (char *)realloc(body, cap);
            if (!nb) {
                free(s);
                free(body);
                c->error = "out of memory";
                return false;
            }
            body = nb;
        }
        memcpy(body + len, s, sl);
        len += sl;
        free(s);
        if (i + 1 < deg)
            body[len++] = ',';
    }
    if (body)
        body[len] = '\0';
    else {
        c->error = "out of memory";
        return false;
    }

    bool ok = terminalc_store_poly(c, dst_name, body);
    free(body);
    return ok;
}
