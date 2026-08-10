#include "terminalc.h"
#include "parse_internal.h"

#include <stdlib.h>
#include <string.h>

/* Complex vectors are represented as a 2 x N tensor: row 0 is real and row 1
 * is imaginary. This keeps the value model exact for ordinary calculator
 * expressions while still making FFT results composable with tensor helpers. */

static bool is_power_of_two(size_t n)
{
    return n != 0 && (n & (n - 1)) == 0;
}

static void series_free(mpfr_t *real, mpfr_t *imag, size_t n)
{
    if (real) {
        for (size_t i = 0; i < n; i++)
            mpfr_clear(real[i]);
        free(real);
    }
    if (imag) {
        for (size_t i = 0; i < n; i++)
            mpfr_clear(imag[i]);
        free(imag);
    }
}

static bool series_alloc(TerminalCCalc *c, size_t n, mpfr_t **real,
                         mpfr_t **imag)
{
    *real = (mpfr_t *)calloc(n, sizeof **real);
    *imag = (mpfr_t *)calloc(n, sizeof **imag);
    if (!*real || !*imag) {
        free(*real);
        free(*imag);
        *real = NULL;
        *imag = NULL;
        terminalc_fail(c, "FFT allocation failed");
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        mpfr_init2((*real)[i], c->bits);
        mpfr_init2((*imag)[i], c->bits);
        mpfr_set_zero((*real)[i], 0);
        mpfr_set_zero((*imag)[i], 0);
    }
    return true;
}

static bool vector_size(TerminalCCalc *c, const TerminalCValue *v, size_t *n)
{
    if (v->type != TERMINALC_TENSOR || !v->ten || v->ten->rank != 1) {
        terminalc_fail(c, "FFT expects a one-dimensional vector");
        return false;
    }
    if (v->ten->n == 0 || v->ten->n > TERMINALC_FFT_MAX_SAMPLES) {
        terminalc_fail(c, "FFT vector length must be between 1 and 4096");
        return false;
    }
    *n = v->ten->n;
    return true;
}

static bool fill_real_vector(TerminalCCalc *c, const TerminalCValue *v,
                             mpfr_t *real, mpfr_t *imag, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        const TerminalCValue *cell = &v->ten->data[i];
        if (!terminalc_is_scalar(cell)) {
            terminalc_fail(c, "FFT vector elements must be scalar values");
            return false;
        }
        terminalc_as_real(c, real[i], cell);
        mpfr_set_zero(imag[i], 0);
    }
    return true;
}

static bool fill_complex_tensor(TerminalCCalc *c, const TerminalCValue *v,
                                mpfr_t *real, mpfr_t *imag, size_t *n)
{
    if (v->type != TERMINALC_TENSOR || !v->ten || v->ten->rank != 2 ||
        v->ten->shape[0] != 2) {
        terminalc_fail(c, "IFFT expects a 2 x N complex tensor from fft()");
        return false;
    }
    *n = v->ten->shape[1];
    if (*n == 0 || *n > TERMINALC_FFT_MAX_SAMPLES) {
        terminalc_fail(c, "IFFT vector length must be between 1 and 4096");
        return false;
    }
    for (size_t i = 0; i < *n; i++) {
        const TerminalCValue *r = &v->ten->data[i];
        const TerminalCValue *im = &v->ten->data[*n + i];
        if (!terminalc_is_scalar(r) || !terminalc_is_scalar(im)) {
            terminalc_fail(c, "IFFT complex tensor cells must be scalar values");
            return false;
        }
        terminalc_as_real(c, real[i], r);
        terminalc_as_real(c, imag[i], im);
    }
    return true;
}

static void complex_swap(mpfr_t *a, mpfr_t *b)
{
    mpfr_swap(*a, *b);
}

static void fft_radix2(TerminalCCalc *c, mpfr_t *real, mpfr_t *imag,
                       size_t n, bool inverse)
{
    for (size_t i = 1, j = 0; i < n; i++) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j) {
            complex_swap(&real[i], &real[j]);
            complex_swap(&imag[i], &imag[j]);
        }
    }

    mpfr_t pi, angle, wlen_real, wlen_imag, w_real, w_imag;
    mpfr_t tr, ti, next_real, next_imag;
    mpfr_inits2(c->bits, pi, angle, wlen_real, wlen_imag, w_real, w_imag,
                tr, ti, next_real, next_imag, (mpfr_ptr)0);
    mpfr_const_pi(pi, MPFR_RNDN);
    for (size_t len = 2; len <= n; len <<= 1) {
        if (terminalc_interrupt_requested())
            return;
        mpfr_mul_ui(angle, pi, 2, MPFR_RNDN);
        mpfr_div_ui(angle, angle, (unsigned long)len, MPFR_RNDN);
        if (!inverse)
            mpfr_neg(angle, angle, MPFR_RNDN);
        mpfr_cos(wlen_real, angle, MPFR_RNDN);
        mpfr_sin(wlen_imag, angle, MPFR_RNDN);
        size_t half = len / 2;
        for (size_t i = 0; i < n; i += len) {
            mpfr_set_ui(w_real, 1, MPFR_RNDN);
            mpfr_set_zero(w_imag, 0);
            for (size_t j = 0; j < half; j++) {
                if (terminalc_interrupt_requested())
                    return;
                mpfr_mul(next_real, w_real, real[i + j + half], MPFR_RNDN);
                mpfr_mul(tr, w_imag, imag[i + j + half], MPFR_RNDN);
                mpfr_sub(tr, next_real, tr, MPFR_RNDN);
                mpfr_mul(next_imag, w_real, imag[i + j + half], MPFR_RNDN);
                mpfr_mul(ti, w_imag, real[i + j + half], MPFR_RNDN);
                mpfr_add(ti, next_imag, ti, MPFR_RNDN);
                mpfr_add(next_real, real[i + j], tr, MPFR_RNDN);
                mpfr_add(next_imag, imag[i + j], ti, MPFR_RNDN);
                mpfr_sub(real[i + j + half], real[i + j], tr, MPFR_RNDN);
                mpfr_sub(imag[i + j + half], imag[i + j], ti, MPFR_RNDN);
                mpfr_set(real[i + j], next_real, MPFR_RNDN);
                mpfr_set(imag[i + j], next_imag, MPFR_RNDN);

                mpfr_mul(tr, w_real, wlen_real, MPFR_RNDN);
                mpfr_mul(ti, w_imag, wlen_imag, MPFR_RNDN);
                mpfr_sub(next_real, tr, ti, MPFR_RNDN);
                mpfr_mul(tr, w_real, wlen_imag, MPFR_RNDN);
                mpfr_mul(ti, w_imag, wlen_real, MPFR_RNDN);
                mpfr_add(next_imag, tr, ti, MPFR_RNDN);
                mpfr_set(w_real, next_real, MPFR_RNDN);
                mpfr_set(w_imag, next_imag, MPFR_RNDN);
            }
        }
        if (len == n)
            break;
    }
    if (inverse) {
        for (size_t i = 0; i < n; i++) {
            mpfr_div_ui(real[i], real[i], (unsigned long)n, MPFR_RNDN);
            mpfr_div_ui(imag[i], imag[i], (unsigned long)n, MPFR_RNDN);
        }
    }
    mpfr_clears(pi, angle, wlen_real, wlen_imag, w_real, w_imag, tr, ti,
                next_real, next_imag, (mpfr_ptr)0);
}

static void dft_direct(TerminalCCalc *c, mpfr_t *real, mpfr_t *imag,
                       size_t n, bool inverse)
{
    mpfr_t *out_real = NULL, *out_imag = NULL;
    if (!series_alloc(c, n, &out_real, &out_imag))
        return;
    mpfr_t pi, angle, sum_real, sum_imag, cr, sr, tr, ti;
    mpfr_inits2(c->bits, pi, angle, sum_real, sum_imag, cr, sr, tr, ti,
                (mpfr_ptr)0);
    mpfr_const_pi(pi, MPFR_RNDN);
    for (size_t k = 0; k < n; k++) {
        if (terminalc_interrupt_requested())
            break;
        mpfr_set_zero(sum_real, 0);
        mpfr_set_zero(sum_imag, 0);
        for (size_t j = 0; j < n; j++) {
            if (terminalc_interrupt_requested())
                break;
            mpfr_mul_ui(angle, pi, 2 * (unsigned long)(j * k), MPFR_RNDN);
            mpfr_div_ui(angle, angle, (unsigned long)n, MPFR_RNDN);
            if (!inverse)
                mpfr_neg(angle, angle, MPFR_RNDN);
            mpfr_cos(cr, angle, MPFR_RNDN);
            mpfr_sin(sr, angle, MPFR_RNDN);
            mpfr_mul(tr, cr, real[j], MPFR_RNDN);
            mpfr_mul(ti, sr, imag[j], MPFR_RNDN);
            mpfr_sub(tr, tr, ti, MPFR_RNDN);
            mpfr_mul(ti, cr, imag[j], MPFR_RNDN);
            mpfr_mul(cr, sr, real[j], MPFR_RNDN);
            mpfr_add(ti, ti, cr, MPFR_RNDN);
            mpfr_add(sum_real, sum_real, tr, MPFR_RNDN);
            mpfr_add(sum_imag, sum_imag, ti, MPFR_RNDN);
        }
        if (inverse) {
            mpfr_div_ui(sum_real, sum_real, (unsigned long)n, MPFR_RNDN);
            mpfr_div_ui(sum_imag, sum_imag, (unsigned long)n, MPFR_RNDN);
        }
        mpfr_set(out_real[k], sum_real, MPFR_RNDN);
        mpfr_set(out_imag[k], sum_imag, MPFR_RNDN);
    }
    for (size_t i = 0; i < n; i++) {
        mpfr_set(real[i], out_real[i], MPFR_RNDN);
        mpfr_set(imag[i], out_imag[i], MPFR_RNDN);
    }
    mpfr_clears(pi, angle, sum_real, sum_imag, cr, sr, tr, ti,
                (mpfr_ptr)0);
    series_free(out_real, out_imag, n);
}

static void transform(TerminalCCalc *c, mpfr_t *real, mpfr_t *imag, size_t n,
                      bool inverse)
{
    if (is_power_of_two(n))
        fft_radix2(c, real, imag, n, inverse);
    else
        dft_direct(c, real, imag, n, inverse);
}

static bool make_complex_tensor(TerminalCCalc *c, TerminalCValue *out,
                                mpfr_t *real, mpfr_t *imag, size_t n)
{
    size_t shape[2] = {2, n};
    if (!terminalc_tensor_create(c, out, 2, shape))
        return false;
    for (size_t i = 0; i < n; i++) {
        TerminalCValue *r = &out->ten->data[i];
        TerminalCValue *im = &out->ten->data[n + i];
        r->type = TERMINALC_REAL;
        r->display_decimal = true;
        im->type = TERMINALC_REAL;
        im->display_decimal = true;
        mpfr_set(r->r, real[i], MPFR_RNDN);
        mpfr_set(im->r, imag[i], MPFR_RNDN);
    }
    return true;
}

static bool extract_component(TerminalCCalc *c, const TerminalCValue *v,
                              size_t component, TerminalCValue *out)
{
    size_t n = 0;
    if (v->type == TERMINALC_TENSOR && v->ten && v->ten->rank == 1) {
        n = v->ten->n;
    } else if (v->type == TERMINALC_TENSOR && v->ten &&
               v->ten->rank == 2 && v->ten->shape[0] == 2) {
        n = v->ten->shape[1];
    } else {
        terminalc_fail(c, "real/imag expects a vector or 2 x N complex tensor");
        return false;
    }
    if (n == 0 || n > TERMINALC_FFT_MAX_SAMPLES) {
        terminalc_fail(c, "signal length must be between 1 and 4096");
        return false;
    }
    size_t shape[1] = {n};
    if (!terminalc_tensor_create(c, out, 1, shape))
        return false;
    for (size_t i = 0; i < n; i++) {
        const TerminalCValue *src = NULL;
        if (v->ten->rank == 1)
            src = component == 0 ? &v->ten->data[i] : NULL;
        else
            src = &v->ten->data[component * n + i];
        if (src)
            terminalc_value_copy(&out->ten->data[i], src);
        else
            terminalc_value_set_si(&out->ten->data[i], 0);
    }
    return true;
}

bool terminalc_signal_call(TerminalCCalc *c, const char *n,
                           TerminalCValue *args, size_t count,
                           TerminalCValue *out)
{
    bool is_fft = strcmp(n, "fft") == 0 || strcmp(n, "dft") == 0;
    bool is_ifft = strcmp(n, "ifft") == 0 || strcmp(n, "idft") == 0;
    bool is_component = strcmp(n, "real") == 0 || strcmp(n, "imag") == 0;
    if (!is_fft && !is_ifft && !is_component)
        return false;
    if (count != 1) {
        terminalc_fail(c, "signal function expects one argument");
        return true;
    }
    if (is_component) {
        (void)extract_component(c, &args[0], strcmp(n, "imag") == 0, out);
        return true;
    }

    size_t n_samples = 0;
    if (is_fft) {
        if (!vector_size(c, &args[0], &n_samples))
            return true;
    } else if (args[0].type == TERMINALC_TENSOR && args[0].ten &&
               args[0].ten->rank == 2) {
        if (args[0].ten->shape[0] != 2) {
            terminalc_fail(c, "IFFT expects a 2 x N complex tensor from fft()");
            return true;
        }
        n_samples = args[0].ten->shape[1];
    } else if (args[0].type == TERMINALC_TENSOR && args[0].ten &&
               args[0].ten->rank == 1) {
        n_samples = args[0].ten->n;
    } else {
        terminalc_fail(c, "IFFT expects a vector or 2 x N complex tensor");
        return true;
    }
    if (n_samples == 0 || n_samples > TERMINALC_FFT_MAX_SAMPLES) {
        terminalc_fail(c, "signal length must be between 1 and 4096");
        return true;
    }

    mpfr_t *real = NULL, *imag = NULL;
    if (!series_alloc(c, n_samples, &real, &imag))
        return true;
    bool ok;
    if (is_fft)
        ok = fill_real_vector(c, &args[0], real, imag, n_samples);
    else if (args[0].ten->rank == 2)
        ok = fill_complex_tensor(c, &args[0], real, imag, &n_samples);
    else
        ok = fill_real_vector(c, &args[0], real, imag, n_samples);
    if (ok)
        transform(c, real, imag, n_samples, is_ifft);
    if (terminalc_interrupt_requested()) {
        terminalc_fail(c, "calculation interrupted by Ctrl-C");
        ok = false;
    }
    if (ok)
        (void)make_complex_tensor(c, out, real, imag, n_samples);
    series_free(real, imag, n_samples);
    return true;
}
