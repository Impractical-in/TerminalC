#include "terminalc.h"
#include "parse_internal.h"

#include <stdlib.h>
#include <string.h>

/* Get matrix dimensions; returns false if not rank-2. */
static bool mat_dims(const TerminalCValue *v, size_t *rows, size_t *cols)
{
    if (v->type != TERMINALC_TENSOR || !v->ten || v->ten->rank != 2)
        return false;
    *rows = v->ten->shape[0];
    *cols = v->ten->shape[1];
    return true;
}

static void cell_to_real(const TerminalCCalc *c, mpfr_t out, const TerminalCValue *e)
{
    terminalc_as_real(c, out, e);
}

bool terminalc_linalg_transpose(TerminalCCalc *c, const TerminalCValue *a, TerminalCValue *out)
{
    size_t r, col;
    if (!mat_dims(a, &r, &col)) {
        if (a->type == TERMINALC_TENSOR && a->ten && a->ten->rank == 1) {
            size_t n = a->ten->shape[0];
            size_t sh[2] = {n, 1};
            if (!terminalc_tensor_create(c, out, 2, sh))
                return false;
            for (size_t i = 0; i < n; i++)
                terminalc_value_copy(&out->ten->data[i], &a->ten->data[i]);
            return true;
        }
        terminalc_fail(c, "transpose expects a matrix or vector");
        return false;
    }
    size_t sh[2] = {col, r};
    if (!terminalc_tensor_create(c, out, 2, sh))
        return false;
    for (size_t i = 0; i < r; i++)
        for (size_t j = 0; j < col; j++)
            terminalc_value_copy(&out->ten->data[j * r + i],
                          &a->ten->data[i * col + j]);
    return true;
}

bool terminalc_linalg_matmul(TerminalCCalc *c, const TerminalCValue *a, const TerminalCValue *b, TerminalCValue *out)
{
    size_t ar, ac, br, bc;
    if (!mat_dims(a, &ar, &ac) || !mat_dims(b, &br, &bc)) {
        terminalc_fail(c, "matrix multiply expects 2D matrices");
        return false;
    }
    if (ac != br) {
        terminalc_fail(c, "matrix multiply: incompatible shapes");
        return false;
    }
    size_t sh[2] = {ar, bc};
    if (!terminalc_tensor_create(c, out, 2, sh))
        return false;

    mpfr_t sum, x, y, t;
    mpfr_inits2(c->bits, sum, x, y, t, (mpfr_ptr)0);
    for (size_t i = 0; i < ar; i++) {
        for (size_t j = 0; j < bc; j++) {
            mpfr_set_ui(sum, 0, MPFR_RNDN);
            for (size_t k = 0; k < ac; k++) {
                cell_to_real(c, x, &a->ten->data[i * ac + k]);
                cell_to_real(c, y, &b->ten->data[k * bc + j]);
                mpfr_mul(t, x, y, MPFR_RNDN);
                mpfr_add(sum, sum, t, MPFR_RNDN);
            }
            out->ten->data[i * bc + j].type = TERMINALC_REAL;
            mpfr_set(out->ten->data[i * bc + j].r, sum, MPFR_RNDN);
        }
    }
    mpfr_clears(sum, x, y, t, (mpfr_ptr)0);
    return true;
}

static bool exact_scalar(const TerminalCValue *v)
{
    return v->type == TERMINALC_RAT || v->type == TERMINALC_BOOL;
}

static void scalar_to_q(mpq_t out, const TerminalCValue *v)
{
    if (v->type == TERMINALC_BOOL)
        mpq_set_si(out, v->b ? 1 : 0, 1);
    else
        mpq_set(out, v->q);
}

/* Apply one scalar binary operation to a tensor cell. Exact rational inputs
 * stay exact; all other numeric combinations use the calculator's MPFR
 * precision. The caller has already ruled out measures. */
static bool tensor_cell_binary(TerminalCCalc *c, const TerminalCValue *left,
                               const TerminalCValue *right, char op,
                               TerminalCValue *out)
{
    if (exact_scalar(left) && exact_scalar(right)) {
        mpq_t lq, rq;
        mpq_inits(lq, rq, (mpq_ptr)0);
        scalar_to_q(lq, left);
        scalar_to_q(rq, right);
        if (op == '/' && mpq_sgn(rq) == 0) {
            mpq_clears(lq, rq, (mpq_ptr)0);
            terminalc_fail(c, "division by zero");
            return false;
        }
        out->type = TERMINALC_RAT;
        if (op == '+')
            mpq_add(out->q, lq, rq);
        else if (op == '-')
            mpq_sub(out->q, lq, rq);
        else if (op == '*')
            mpq_mul(out->q, lq, rq);
        else
            mpq_div(out->q, lq, rq);
        mpq_clears(lq, rq, (mpq_ptr)0);
        return true;
    }

    mpfr_t x, y;
    mpfr_inits2(c->bits, x, y, (mpfr_ptr)0);
    terminalc_as_real(c, x, left);
    terminalc_as_real(c, y, right);
    if (op == '/' && mpfr_zero_p(y)) {
        mpfr_clears(x, y, (mpfr_ptr)0);
        terminalc_fail(c, "division by zero");
        return false;
    }
    out->type = TERMINALC_REAL;
    if (op == '+')
        mpfr_add(out->r, x, y, MPFR_RNDN);
    else if (op == '-')
        mpfr_sub(out->r, x, y, MPFR_RNDN);
    else if (op == '*')
        mpfr_mul(out->r, x, y, MPFR_RNDN);
    else
        mpfr_div(out->r, x, y, MPFR_RNDN);
    mpfr_clears(x, y, (mpfr_ptr)0);
    return true;
}

static bool tensor_binary(TerminalCCalc *c, const TerminalCValue *a,
                          const TerminalCValue *b, char op,
                          TerminalCValue *out)
{
    bool at = a->type == TERMINALC_TENSOR && a->ten;
    bool bt = b->type == TERMINALC_TENSOR && b->ten;
    bool as = terminalc_is_scalar(a) && a->type != TERMINALC_MEASURE;
    bool bs = terminalc_is_scalar(b) && b->type != TERMINALC_MEASURE;
    if (!((at && bt) || (at && bs) || (as && bt))) {
        terminalc_fail(c, "element-wise ops need tensors and numeric scalars");
        return false;
    }

    const TerminalCTensor *shape = at ? a->ten : b->ten;
    if (at && bt) {
        if (a->ten->rank != b->ten->rank || a->ten->n != b->ten->n) {
            terminalc_fail(c, "tensor shape mismatch");
            return false;
        }
        for (int i = 0; i < a->ten->rank; i++) {
            if (a->ten->shape[i] != b->ten->shape[i]) {
                terminalc_fail(c, "tensor shape mismatch");
                return false;
            }
        }
    }
    if (!terminalc_tensor_create(c, out, shape->rank, shape->shape))
        return false;

    for (size_t i = 0; i < shape->n; i++) {
        const TerminalCValue *left = at ? &a->ten->data[i] : a;
        const TerminalCValue *right = bt ? &b->ten->data[i] : b;
        if (!tensor_cell_binary(c, left, right, op, &out->ten->data[i]))
            return false;
    }
    return true;
}

/* Element-wise add/sub supports same-shape tensors and scalar broadcasting. */
bool terminalc_tensor_ew_add(TerminalCCalc *c, const TerminalCValue *a,
                             const TerminalCValue *b, bool sub,
                             TerminalCValue *out)
{
    return tensor_binary(c, a, b, sub ? '-' : '+', out);
}

/* Element-wise multiply/divide supports scalar broadcasting. Tensor*tensor
 * remains matrix multiplication and is intentionally handled by the parser. */
bool terminalc_tensor_ew_muldiv(TerminalCCalc *c, const TerminalCValue *a,
                                const TerminalCValue *b, char op,
                                TerminalCValue *out)
{
    return tensor_binary(c, a, b, op, out);
}

bool terminalc_linalg_det(TerminalCCalc *c, const TerminalCValue *a, TerminalCValue *out)
{
    size_t n, cols;
    if (!mat_dims(a, &n, &cols) || n != cols) {
        terminalc_fail(c, "det expects a square matrix");
        return false;
    }
    if (n == 0) {
        terminalc_fail(c, "empty matrix");
        return false;
    }
    /* Gaussian elimination on a copy (doubles via mpfr) */
    mpfr_t *m = (mpfr_t *)malloc(n * n * sizeof(mpfr_t));
    if (!m) {
        terminalc_fail(c, "out of memory");
        return false;
    }
    for (size_t i = 0; i < n * n; i++) {
        mpfr_init2(m[i], c->bits);
        cell_to_real(c, m[i], &a->ten->data[i]);
    }
    int sign = 1;
    mpfr_t det, t, piv;
    mpfr_inits2(c->bits, det, t, piv, (mpfr_ptr)0);
    mpfr_set_ui(det, 1, MPFR_RNDN);

    for (size_t k = 0; k < n; k++) {
        /* pivot */
        size_t piv_i = k;
        mpfr_abs(piv, m[k * n + k], MPFR_RNDN);
        for (size_t i = k + 1; i < n; i++) {
            mpfr_abs(t, m[i * n + k], MPFR_RNDN);
            if (mpfr_cmp(t, piv) > 0) {
                mpfr_set(piv, t, MPFR_RNDN);
                piv_i = i;
            }
        }
        if (mpfr_zero_p(m[piv_i * n + k])) {
            terminalc_value_set_si(out, 0);
            goto done;
        }
        if (piv_i != k) {
            for (size_t j = 0; j < n; j++)
                mpfr_swap(m[k * n + j], m[piv_i * n + j]);
            sign = -sign;
        }
        mpfr_mul(det, det, m[k * n + k], MPFR_RNDN);
        for (size_t i = k + 1; i < n; i++) {
            mpfr_div(t, m[i * n + k], m[k * n + k], MPFR_RNDN);
            for (size_t j = k; j < n; j++) {
                mpfr_mul(piv, t, m[k * n + j], MPFR_RNDN);
                mpfr_sub(m[i * n + j], m[i * n + j], piv, MPFR_RNDN);
            }
        }
    }
    if (sign < 0)
        mpfr_neg(det, det, MPFR_RNDN);
    out->type = TERMINALC_REAL;
    mpfr_set(out->r, det, MPFR_RNDN);

done:
    mpfr_clears(det, t, piv, (mpfr_ptr)0);
    for (size_t i = 0; i < n * n; i++)
        mpfr_clear(m[i]);
    free(m);
    return !c->error;
}

bool terminalc_linalg_inv(TerminalCCalc *c, const TerminalCValue *a, TerminalCValue *out)
{
    size_t n, cols;
    if (!mat_dims(a, &n, &cols) || n != cols) {
        terminalc_fail(c, "inv expects a square matrix");
        return false;
    }
    /* Gauss-Jordan on [A|I] */
    size_t w = 2 * n;
    mpfr_t *m = (mpfr_t *)malloc(n * w * sizeof(mpfr_t));
    if (!m) {
        terminalc_fail(c, "out of memory");
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        for (size_t j = 0; j < n; j++) {
            mpfr_init2(m[i * w + j], c->bits);
            cell_to_real(c, m[i * w + j], &a->ten->data[i * n + j]);
        }
        for (size_t j = 0; j < n; j++) {
            mpfr_init2(m[i * w + n + j], c->bits);
            mpfr_set_ui(m[i * w + n + j], i == j ? 1 : 0, MPFR_RNDN);
        }
    }
    mpfr_t t, f;
    mpfr_init2(t, c->bits);
    mpfr_init2(f, c->bits);
    bool ok = true;
    for (size_t k = 0; k < n; k++) {
        size_t piv_i = k;
        mpfr_abs(t, m[k * w + k], MPFR_RNDN);
        for (size_t i = k + 1; i < n; i++) {
            mpfr_abs(f, m[i * w + k], MPFR_RNDN);
            if (mpfr_cmp(f, t) > 0) {
                mpfr_set(t, f, MPFR_RNDN);
                piv_i = i;
            }
        }
        if (mpfr_zero_p(m[piv_i * w + k])) {
            terminalc_fail(c, "matrix is singular");
            ok = false;
            break;
        }
        if (piv_i != k) {
            for (size_t j = 0; j < w; j++)
                mpfr_swap(m[k * w + j], m[piv_i * w + j]);
        }
        mpfr_set(f, m[k * w + k], MPFR_RNDN);
        for (size_t j = 0; j < w; j++)
            mpfr_div(m[k * w + j], m[k * w + j], f, MPFR_RNDN);
        for (size_t i = 0; i < n; i++) {
            if (i == k)
                continue;
            mpfr_set(f, m[i * w + k], MPFR_RNDN);
            for (size_t j = 0; j < w; j++) {
                mpfr_mul(t, f, m[k * w + j], MPFR_RNDN);
                mpfr_sub(m[i * w + j], m[i * w + j], t, MPFR_RNDN);
            }
        }
    }
    if (ok) {
        size_t sh[2] = {n, n};
        if (!terminalc_tensor_create(c, out, 2, sh))
            ok = false;
        else {
            for (size_t i = 0; i < n; i++)
                for (size_t j = 0; j < n; j++) {
                    out->ten->data[i * n + j].type = TERMINALC_REAL;
                    mpfr_set(out->ten->data[i * n + j].r, m[i * w + n + j],
                             MPFR_RNDN);
                }
        }
    }
    mpfr_clear(t);
    mpfr_clear(f);
    for (size_t i = 0; i < n * w; i++)
        mpfr_clear(m[i]);
    free(m);
    return ok;
}

bool terminalc_linalg_solve(TerminalCCalc *c, const TerminalCValue *a, const TerminalCValue *b, TerminalCValue *out)
{
    /* Solve A x = b via inv(A)*b for modest n */
    TerminalCValue inv;
    terminalc_value_init(&inv, c->bits);
    if (!terminalc_linalg_inv(c, a, &inv)) {
        terminalc_value_clear(&inv);
        return false;
    }
    /* b as column matrix */
    TerminalCValue bm;
    terminalc_value_init(&bm, c->bits);
    if (b->type == TERMINALC_TENSOR && b->ten && b->ten->rank == 1) {
        size_t n = b->ten->shape[0];
        size_t sh[2] = {n, 1};
        if (!terminalc_tensor_create(c, &bm, 2, sh)) {
            terminalc_value_clear(&inv);
            return false;
        }
        for (size_t i = 0; i < n; i++)
            terminalc_value_copy(&bm.ten->data[i], &b->ten->data[i]);
    } else if (b->type == TERMINALC_TENSOR && b->ten && b->ten->rank == 2) {
        terminalc_value_copy(&bm, b);
    } else {
        terminalc_fail(c, "linsolve: b must be a vector or matrix");
        terminalc_value_clear(&inv);
        terminalc_value_clear(&bm);
        return false;
    }
    bool ok = terminalc_linalg_matmul(c, &inv, &bm, out);
    terminalc_value_clear(&inv);
    terminalc_value_clear(&bm);
    return ok;
}

bool terminalc_linalg_dot(TerminalCCalc *c, const TerminalCValue *a, const TerminalCValue *b, TerminalCValue *out)
{
    if (a->type != TERMINALC_TENSOR || b->type != TERMINALC_TENSOR || !a->ten || !b->ten) {
        terminalc_fail(c, "dot expects vectors");
        return false;
    }
    if (a->ten->rank != 1 || b->ten->rank != 1 || a->ten->n != b->ten->n) {
        terminalc_fail(c, "dot: vector length mismatch");
        return false;
    }
    mpfr_t sum, x, y, t;
    mpfr_inits2(c->bits, sum, x, y, t, (mpfr_ptr)0);
    mpfr_set_ui(sum, 0, MPFR_RNDN);
    for (size_t i = 0; i < a->ten->n; i++) {
        cell_to_real(c, x, &a->ten->data[i]);
        cell_to_real(c, y, &b->ten->data[i]);
        mpfr_mul(t, x, y, MPFR_RNDN);
        mpfr_add(sum, sum, t, MPFR_RNDN);
    }
    out->type = TERMINALC_REAL;
    mpfr_set(out->r, sum, MPFR_RNDN);
    mpfr_clears(sum, x, y, t, (mpfr_ptr)0);
    return true;
}
