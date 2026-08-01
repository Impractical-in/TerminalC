#include "terminalc.h"
#include "color.h"
#include "parse_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t product(const size_t *shape, int rank)
{
    size_t n = 1;
    for (int i = 0; i < rank; i++) {
        if (shape[i] == 0)
            return 0;
        if (n > TERMINALC_TENSOR_MAX_ELEM / shape[i])
            return (size_t)-1;
        n *= shape[i];
    }
    return n;
}

void terminalc_tensor_free(TerminalCTensor *t)
{
    if (!t)
        return;
    if (t->data) {
        for (size_t i = 0; i < t->n; i++)
            terminalc_value_clear(&t->data[i]);
        free(t->data);
    }
    free(t);
}

bool terminalc_tensor_create(TerminalCCalc *c, TerminalCValue *out, int rank,
                             const size_t *shape)
{
    if (rank < 1 || rank > TERMINALC_TENSOR_MAX_RANK) {
        terminalc_fail(c, "tensor rank must be 1..4");
        return false;
    }
    size_t n = product(shape, rank);
    if (n == 0 || n == (size_t)-1 || n > TERMINALC_TENSOR_MAX_ELEM) {
        terminalc_fail(c, "tensor too large or empty shape");
        return false;
    }
    TerminalCTensor *t = (TerminalCTensor *)calloc(1, sizeof *t);
    if (!t) {
        terminalc_fail(c, "out of memory");
        return false;
    }
    t->rank = rank;
    t->n = n;
    for (int i = 0; i < rank; i++)
        t->shape[i] = shape[i];
    t->data = (TerminalCValue *)calloc(n, sizeof(TerminalCValue));
    if (!t->data) {
        free(t);
        terminalc_fail(c, "out of memory");
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        terminalc_value_init(&t->data[i], c->bits);
        terminalc_value_set_si(&t->data[i], 0);
    }
    terminalc_value_clear(out);
    terminalc_value_init(out, c->bits);
    out->type = TERMINALC_TENSOR;
    out->ten = t;
    return true;
}

bool terminalc_tensor_fill(TerminalCCalc *c, TerminalCValue *out, int rank,
                           const size_t *shape, const TerminalCValue *fill)
{
    if (!terminalc_tensor_create(c, out, rank, shape))
        return false;
    for (size_t i = 0; i < out->ten->n; i++)
        terminalc_value_copy(&out->ten->data[i], fill);
    return true;
}

bool terminalc_tensor_eye(TerminalCCalc *c, TerminalCValue *out, size_t n)
{
    size_t sh[2] = {n, n};
    if (!terminalc_tensor_create(c, out, 2, sh))
        return false;
    for (size_t i = 0; i < n; i++)
        terminalc_value_set_si(&out->ten->data[i * n + i], 1);
    return true;
}

bool terminalc_tensor_from_args(TerminalCCalc *c, TerminalCValue *out, size_t rows,
                                size_t cols, TerminalCValue *args, size_t nargs)
{
    if (rows == 0 || cols == 0 || rows * cols != nargs) {
        terminalc_fail(c, "mat: rows*cols must equal number of entries");
        return false;
    }
    size_t sh[2] = {rows, cols};
    if (!terminalc_tensor_create(c, out, 2, sh))
        return false;
    for (size_t i = 0; i < nargs; i++) {
        if (!terminalc_is_scalar(&args[i])) {
            terminalc_fail(c, "mat: entries must be scalars");
            return false;
        }
        terminalc_value_copy(&out->ten->data[i], &args[i]);
    }
    return true;
}

/* Format one cell into buf; returns string length. */
static int fmt_cell(const TerminalCCalc *c, const TerminalCValue *e, char *buf,
                    size_t bufsz)
{
    if (e->type == TERMINALC_RAT) {
        char *s = mpq_get_str(NULL, 10, e->q);
        if (!s) {
            snprintf(buf, bufsz, "?");
            return 1;
        }
        int n = snprintf(buf, bufsz, "%s", s);
        free(s);
        return n;
    }
    if (e->type == TERMINALC_BOOL)
        return snprintf(buf, bufsz, "%s", e->b ? "true" : "false");
    int digs = c->digits > 8 ? 8 : c->digits;
    if (digs < 2)
        digs = 2;
    return mpfr_snprintf(buf, bufsz, "%.*Rg", digs, e->r);
}

static void print_row_aligned(const TerminalCCalc *c, const TerminalCValue *row,
                              size_t cols, const int *widths, const char *left,
                              const char *right)
{
    printf("%s%s%s", C_DIM, left, C_RST);
    for (size_t j = 0; j < cols; j++) {
        char cell[128];
        fmt_cell(c, &row[j], cell, sizeof cell);
        printf(" %s%*s%s", C_B_GRN, widths[j], cell, C_RST);
    }
    printf(" %s%s%s\n", C_DIM, right, C_RST);
}

void terminalc_print_tensor(const TerminalCCalc *c, const TerminalCTensor *t)
{
    if (!t) {
        puts("(null tensor)");
        return;
    }

    /* Header */
    printf("%s", C_B_CYN);
    if (t->rank == 1)
        printf("%zu-vector", t->shape[0]);
    else if (t->rank == 2)
        printf("%zu x %zu matrix", t->shape[0], t->shape[1]);
    else {
        printf("%dD tensor ", t->rank);
        for (int i = 0; i < t->rank; i++) {
            if (i)
                fputs(" x ", stdout);
            printf("%zu", t->shape[i]);
        }
    }
    printf("%s\n", C_RST);

    if (t->rank == 1) {
        size_t cols = t->n;
        int *widths = (int *)calloc(cols ? cols : 1, sizeof(int));
        if (!widths)
            return;
        for (size_t j = 0; j < cols; j++) {
            char cell[128];
            int w = fmt_cell(c, &t->data[j], cell, sizeof cell);
            if (w > widths[j])
                widths[j] = w;
            if (widths[j] < 1)
                widths[j] = 1;
        }
        print_row_aligned(c, t->data, cols, widths, "[", "]");
        free(widths);
        return;
    }

    if (t->rank == 2) {
        size_t rows = t->shape[0], cols = t->shape[1];
        int *widths = (int *)calloc(cols ? cols : 1, sizeof(int));
        if (!widths)
            return;
        for (size_t j = 0; j < cols; j++) {
            widths[j] = 1;
            for (size_t i = 0; i < rows; i++) {
                char cell[128];
                int w = fmt_cell(c, &t->data[i * cols + j], cell, sizeof cell);
                if (w > widths[j])
                    widths[j] = w;
            }
        }
        /* top border */
        printf("%s+", C_DIM);
        for (size_t j = 0; j < cols; j++) {
            putchar('-');
            for (int k = 0; k < widths[j]; k++)
                putchar('-');
            putchar('-');
            putchar('+');
        }
        printf("%s\n", C_RST);

        for (size_t i = 0; i < rows; i++) {
            printf("%s|%s", C_DIM, C_RST);
            for (size_t j = 0; j < cols; j++) {
                char cell[128];
                fmt_cell(c, &t->data[i * cols + j], cell, sizeof cell);
                printf(" %s%*s%s %s|%s", C_B_GRN, widths[j], cell, C_RST, C_DIM,
                       C_RST);
            }
            putchar('\n');
        }

        printf("%s+", C_DIM);
        for (size_t j = 0; j < cols; j++) {
            putchar('-');
            for (int k = 0; k < widths[j]; k++)
                putchar('-');
            putchar('-');
            putchar('+');
        }
        printf("%s\n", C_RST);
        free(widths);
        return;
    }

    /* higher rank: aligned 2D slices */
    size_t stride = t->shape[t->rank - 2] * t->shape[t->rank - 1];
    size_t slices = t->n / stride;
    size_t rows = t->shape[t->rank - 2];
    size_t cols = t->shape[t->rank - 1];
    int *widths = (int *)calloc(cols ? cols : 1, sizeof(int));
    if (!widths)
        return;
    for (size_t j = 0; j < cols; j++) {
        widths[j] = 1;
        for (size_t s = 0; s < slices && s < 8; s++) {
            for (size_t i = 0; i < rows; i++) {
                char cell[128];
                int w = fmt_cell(c, &t->data[s * stride + i * cols + j], cell,
                                 sizeof cell);
                if (w > widths[j])
                    widths[j] = w;
            }
        }
    }
    for (size_t s = 0; s < slices && s < 8; s++) {
        printf("%sslice %zu%s\n", C_DIM, s + 1, C_RST);
        for (size_t i = 0; i < rows; i++)
            print_row_aligned(c, &t->data[s * stride + i * cols], cols, widths,
                              "|", "|");
    }
    if (slices > 8)
        printf("%s... %zu more slices%s\n", C_DIM, slices - 8, C_RST);
    free(widths);
}
