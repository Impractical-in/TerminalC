/**
 * Advanced mode: wall time, result object size, payload size, print size,
 * optional process working-set (Windows).
 *
 * Naming:
 *   object  - full estimated in-memory result (struct shell + GMP/MPFR + data)
 *   payload - variable content only (integer limbs / real limbs / tensor data)
 *   output  - estimated bytes of the printed form
 */
#include "terminalc.h"
#include "color.h"

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <psapi.h>
#else
#include <time.h>
#endif

/* ---- timers ---- */

#ifdef _WIN32
static double now_ms(void)
{
    static LARGE_INTEGER freq;
    static int init;
    LARGE_INTEGER t;
    if (!init) {
        QueryPerformanceFrequency(&freq);
        init = 1;
    }
    QueryPerformanceCounter(&t);
    return (double)t.QuadPart * 1000.0 / (double)freq.QuadPart;
}

static size_t process_working_set(void)
{
    PROCESS_MEMORY_COUNTERS pmc;
    memset(&pmc, 0, sizeof pmc);
    pmc.cb = sizeof pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof pmc))
        return (size_t)pmc.WorkingSetSize;
    return 0;
}
#else
static double now_ms(void)
{
#if defined(CLOCK_MONOTONIC)
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
#else
    return (double)clock() * 1000.0 / (double)CLOCKS_PER_SEC;
#endif
}

static size_t process_working_set(void)
{
    return 0;
}
#endif

/* ---- size helpers ---- */

/* Raw limb storage for an integer (0 for zero). */
static size_t mpz_payload_bytes(const mpz_t z)
{
    if (mpz_sgn(z) == 0)
        return 0;
    return (mpz_sizeinbase(z, 2) + 7) / 8;
}

/* mpq overhead beyond bare limbs (GMP headers / min limbs). */
static size_t mpq_shell_bytes(void)
{
    return 32;
}

static size_t mpfr_payload_bytes(const mpfr_t r)
{
    mpfr_prec_t p = mpfr_get_prec(r);
    return ((size_t)p + 7) / 8;
}

static size_t mpfr_shell_bytes(void)
{
    return 64; /* rough mpfr header / min allocation */
}

/*
 * payload = mathematical content only (grows with big ints / precision / tensors).
 * Does not include TerminalCValue struct or fixed library headers.
 */
size_t terminalc_value_payload_bytes(const TerminalCValue *v)
{
    if (!v)
        return 0;
    switch (v->type) {
    case TERMINALC_BOOL:
        return 1;
    case TERMINALC_RAT:
        return mpz_payload_bytes(mpq_numref(v->q)) +
               (mpz_cmp_ui(mpq_denref(v->q), 1) == 0
                    ? 0
                    : mpz_payload_bytes(mpq_denref(v->q)));
    case TERMINALC_REAL:
        return mpfr_payload_bytes(v->r);
    case TERMINALC_MEASURE:
        return mpz_payload_bytes(mpq_numref(v->q)) + mpfr_payload_bytes(v->r);
    case TERMINALC_TENSOR:
        if (!v->ten)
            return 0;
        {
            size_t n = 0;
            for (size_t i = 0; i < v->ten->n; i++)
                n += terminalc_value_payload_bytes(&v->ten->data[i]);
            return n;
        }
    }
    return 0;
}

/*
 * object = full estimated footprint of this value instance:
 *   TerminalCValue shell + always-on mpq/mpfr storage + payload (+ tensor array).
 */
size_t terminalc_value_object_bytes(const TerminalCValue *v)
{
    if (!v)
        return 0;

    size_t n = sizeof(TerminalCValue);

    switch (v->type) {
    case TERMINALC_BOOL:
        /* shell already counted; bool flag is inside struct */
        break;
    case TERMINALC_RAT:
        n += mpq_shell_bytes() + terminalc_value_payload_bytes(v);
        /* unused mpfr still allocated on the value */
        n += mpfr_shell_bytes() + mpfr_payload_bytes(v->r);
        break;
    case TERMINALC_REAL:
        n += mpq_shell_bytes(); /* unused mpq still present */
        n += mpfr_shell_bytes() + terminalc_value_payload_bytes(v);
        break;
    case TERMINALC_MEASURE:
        n += mpq_shell_bytes() + mpfr_shell_bytes() +
             terminalc_value_payload_bytes(v);
        break;
    case TERMINALC_TENSOR:
        if (v->ten) {
            n += sizeof(TerminalCTensor);
            n += v->ten->n * sizeof(TerminalCValue);
            for (size_t i = 0; i < v->ten->n; i++) {
                /* each cell has its own shell + content */
                n += terminalc_value_object_bytes(&v->ten->data[i]) -
                     sizeof(TerminalCValue);
            }
        }
        break;
    }
    return n;
}

static size_t digits_of_mpz(const mpz_t z)
{
    if (mpz_sgn(z) == 0)
        return 1;
    return mpz_sizeinbase(z, 10) + (mpz_sgn(z) < 0 ? 1 : 0);
}

size_t terminalc_value_output_bytes(const TerminalCCalc *c,
                                    const TerminalCValue *v)
{
    if (!v)
        return 0;
    switch (v->type) {
    case TERMINALC_BOOL:
        return v->b ? 4 : 5;
    case TERMINALC_RAT:
        if (mpz_cmp_ui(mpq_denref(v->q), 1) == 0)
            return digits_of_mpz(mpq_numref(v->q)) + 1;
        return digits_of_mpz(mpq_numref(v->q)) + digits_of_mpz(mpq_denref(v->q)) +
               (size_t)c->digits + 48;
    case TERMINALC_REAL:
        return (size_t)c->digits + 40;
    case TERMINALC_MEASURE:
        return (size_t)c->digits + 16;
    case TERMINALC_TENSOR:
        if (!v->ten)
            return 16;
        {
            size_t cells = v->ten->n;
            size_t per = (size_t)c->digits + 4;
            if (per < 4)
                per = 4;
            return 32 + cells * per + v->ten->shape[0] * 4;
        }
    }
    return 0;
}

void terminalc_adv_begin(TerminalCAdvStats *st)
{
    memset(st, 0, sizeof *st);
    st->elapsed_ms = now_ms();
    st->working_set_bytes = process_working_set();
}

void terminalc_adv_end(TerminalCCalc *c, const TerminalCValue *v,
                       TerminalCAdvStats *st)
{
    double t1 = now_ms();
    double t0 = st->elapsed_ms;
    st->elapsed_ms = t1 - t0;
    if (st->elapsed_ms < 0)
        st->elapsed_ms = 0;

    size_t ws1 = process_working_set();
    size_t ws0 = st->working_set_bytes;
    st->working_set_bytes = ws1;
    if (ws1 && ws0)
        st->working_set_delta = (long)ws1 - (long)ws0;
    else
        st->working_set_delta = 0;

    if (v) {
        st->object_bytes = terminalc_value_object_bytes(v);
        st->payload_bytes = terminalc_value_payload_bytes(v);
        st->output_bytes = terminalc_value_output_bytes(c, v);
    } else {
        st->object_bytes = 0;
        st->payload_bytes = 0;
        st->output_bytes = 0;
    }
}

static void fmt_bytes(char *buf, size_t n, size_t b)
{
    if (b < 1024)
        snprintf(buf, n, "%zu B", b);
    else if (b < 1024ull * 1024)
        snprintf(buf, n, "%.2f KiB", b / 1024.0);
    else
        snprintf(buf, n, "%.2f MiB", b / (1024.0 * 1024.0));
}

static void fmt_time(char *buf, size_t n, double ms)
{
    if (ms < 0.001)
        snprintf(buf, n, "%.3f us", ms * 1000.0);
    else if (ms < 1.0)
        snprintf(buf, n, "%.3f ms", ms);
    else if (ms < 1000.0)
        snprintf(buf, n, "%.2f ms", ms);
    else
        snprintf(buf, n, "%.3f s", ms / 1000.0);
}

void terminalc_adv_print(const TerminalCCalc *c, const TerminalCAdvStats *st)
{
    (void)c;
    char tbuf[32], obuf[32], pbuf[32], outbuf[32], wbuf[32];
    fmt_time(tbuf, sizeof tbuf, st->elapsed_ms);
    fmt_bytes(obuf, sizeof obuf, st->object_bytes);
    fmt_bytes(pbuf, sizeof pbuf, st->payload_bytes);
    fmt_bytes(outbuf, sizeof outbuf, st->output_bytes);

    /*
     * object  = full result value in memory (fixed shell + content)
     * payload = variable numeric content only (limbs / tensor data)
     * output  = estimated printed form size
     */
    printf("%s[advanced]%s time %s%s%s  |  object %s%s%s  |  payload %s%s%s  |  "
           "output ~%s%s%s",
           C_DIM, C_RST, C_B_YEL, tbuf, C_RST, C_B_YEL, obuf, C_RST, C_B_YEL,
           pbuf, C_RST, C_B_YEL, outbuf, C_RST);

    if (st->working_set_bytes) {
        fmt_bytes(wbuf, sizeof wbuf, st->working_set_bytes);
        printf("  |  working set %s%s%s", C_B_YEL, wbuf, C_RST);
        if (st->working_set_delta != 0) {
            char dbuf[32];
            long d = st->working_set_delta;
            if (d < 0) {
                fmt_bytes(dbuf, sizeof dbuf, (size_t)(-d));
                printf(" (%s-%s%s)", C_B_YEL, dbuf, C_RST);
            } else {
                fmt_bytes(dbuf, sizeof dbuf, (size_t)d);
                printf(" (%s+%s%s)", C_B_YEL, dbuf, C_RST);
            }
        }
    }
    putchar('\n');
}
