#include "terminalc.h"
#include "color.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void terminalc_init(TerminalCCalc *c)
{
    memset(c, 0, sizeof *c);
    c->digits = 4;
    c->bits = (mpfr_prec_t)(c->digits * 3.3219280948873623) + TERMINALC_GUARD_BITS;
    c->degrees = false;
    c->color = g_terminalc_color.enabled;
    c->advanced = false;
    c->output_format = TERMINALC_OUTPUT_PLAIN;
    terminalc_value_init(&c->ans, c->bits);
    terminalc_value_init(&c->x, c->bits);
}

void terminalc_clear(TerminalCCalc *c)
{
    free(c->owned_text);
    c->owned_text = NULL;
    c->text = NULL;
    c->at = NULL;
    terminalc_value_clear(&c->ans);
    terminalc_value_clear(&c->x);
    for (size_t i = 0; i < c->var_count; i++)
        terminalc_value_clear(&c->vars[i].value);
    c->var_count = 0;
    for (size_t i = 0; i < c->poly_count; i++) {
        if (c->polys[i].coeff) {
            for (size_t j = 0; j < c->polys[i].n; j++)
                mpq_clear(c->polys[i].coeff[j]);
            free(c->polys[i].coeff);
            c->polys[i].coeff = NULL;
            c->polys[i].n = 0;
        }
    }
    c->poly_count = 0;
}

bool terminalc_set_precision(TerminalCCalc *c, int digits, bool verbose)
{
    if (digits < 2 || digits > 100000) {
        fprintf(stderr, "%sPrecision must be between 2 and 100000 digits.%s\n",
                C_B_RED, C_RST);
        return false;
    }
    c->digits = digits;
    c->bits = (mpfr_prec_t)(digits * 3.3219280948873623) + TERMINALC_GUARD_BITS;
    mpfr_prec_round(c->ans.r, c->bits, MPFR_RNDN);
    mpfr_prec_round(c->x.r, c->bits, MPFR_RNDN);
    for (size_t i = 0; i < c->var_count; i++)
        mpfr_prec_round(c->vars[i].value.r, c->bits, MPFR_RNDN);
    if (verbose) {
        printf("%sPrecision set to %s%d%s decimal digits.%s\n",
               C_B_CYN, C_B_GRN, digits, C_B_CYN, C_RST);
    }
    return true;
}
