#include "terminalc.h"
#include "color.h"
#include "plot_gui.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#define TERMINALC_ISATTY_OUT() (_isatty(_fileno(stdout)) != 0)
#define TERMINALC_ISATTY_IN()  (_isatty(_fileno(stdin)) != 0)
#else
#include <unistd.h>
#define TERMINALC_ISATTY_OUT() (isatty(STDOUT_FILENO) != 0)
#define TERMINALC_ISATTY_IN()  (isatty(STDIN_FILENO) != 0)
#endif

enum { PLOT_W = 61, PLOT_H = 21, YLAB = 10 };

static void trim_spaces(char *s)
{
    char *a = s;
    while (isspace((unsigned char)*a))
        a++;
    if (a != s)
        memmove(s, a, strlen(a) + 1);
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1]))
        s[--n] = '\0';
}

/* Accept "x", "y=x", " y = sin(x) " → expression in free variable x. */
static void normalize_expr(char *expr)
{
    trim_spaces(expr);
    /* strip optional y= / Y= assignment form */
    if ((expr[0] == 'y' || expr[0] == 'Y')) {
        char *p = expr + 1;
        while (isspace((unsigned char)*p))
            p++;
        if (*p == '=') {
            p++;
            while (isspace((unsigned char)*p))
                p++;
            memmove(expr, p, strlen(p) + 1);
            trim_spaces(expr);
        }
    }
}

static int y_to_row(double y, double mn, double mx, int h)
{
    if (mx <= mn)
        return h / 2;
    double t = (mx - y) / (mx - mn);
    int r = (int)(t * (h - 1) + 0.5);
    if (r < 0)
        r = 0;
    if (r >= h)
        r = h - 1;
    return r;
}

static int x_to_col(double x, double xmin, double xmax, int w)
{
    if (xmax <= xmin)
        return 0;
    int c = (int)((x - xmin) / (xmax - xmin) * (w - 1) + 0.5);
    if (c < 0)
        c = 0;
    if (c >= w)
        c = w - 1;
    return c;
}

/* Curve glyphs (= | / \ * o) — distinct from axis glyphs (- : +). */
static bool is_curve_ch(char ch)
{
    return ch == '=' || ch == '|' || ch == '/' || ch == '\\' || ch == '*' ||
           ch == 'o';
}

static bool is_axis_ch(char ch)
{
    return ch == '-' || ch == ':' || ch == '+';
}

/* Slope-aware continuous line character (row grows downward). */
static char line_glyph(int dc, int dr)
{
    if (dc == 0)
        return '|';
    if (dr == 0)
        return '=';
    int adc = abs(dc);
    int adr = abs(dr);
    if (adc >= 2 * adr)
        return '=';
    if (adr >= 2 * adc)
        return '|';
    /* Diagonal: screen y increases down → same-sign (dc,dr) is backslash. */
    if ((dc > 0 && dr > 0) || (dc < 0 && dr < 0))
        return '\\';
    return '/';
}

static void put_cell(char canvas[PLOT_H][PLOT_W], int row, int col, char ch)
{
    if (row < 0 || row >= PLOT_H || col < 0 || col >= PLOT_W)
        return;
    char cur = canvas[row][col];

    if (is_curve_ch(ch)) {
        /* Crossing strokes → '*' (kept distinct from axis origin '+'). */
        if (is_curve_ch(cur) && cur != 'o' && cur != ch &&
            ((cur == '=' && ch == '|') || (cur == '|' && ch == '=') ||
             (cur == '/' && ch == '\\') || (cur == '\\' && ch == '/')))
            canvas[row][col] = '*';
        else if (ch == 'o' || cur == ' ' || cur == '\0' || is_axis_ch(cur) ||
                 !is_curve_ch(cur))
            canvas[row][col] = ch;
        return;
    }

    /* Axis glyphs never overwrite the curve. */
    if (is_curve_ch(cur))
        return;
    if (cur == ' ' || cur == '\0') {
        canvas[row][col] = ch;
        return;
    }
    if (is_axis_ch(ch) && is_axis_ch(cur)) {
        if ((cur == ':' && ch == '-') || (cur == '-' && ch == ':'))
            canvas[row][col] = '+';
        else if (cur != '+')
            canvas[row][col] = ch;
    }
}

/* Continuous polyline (not star dots). */
static void connect_cols(char canvas[PLOT_H][PLOT_W], int c0, int r0, int c1,
                         int r1)
{
    int dc = c1 - c0;
    int dr = r1 - r0;
    char glyph = line_glyph(dc, dr);

    if (c0 == c1) {
        int lo = r0 < r1 ? r0 : r1;
        int hi = r0 > r1 ? r0 : r1;
        for (int r = lo; r <= hi; r++)
            put_cell(canvas, r, c0, glyph);
        return;
    }

    int steps = abs(dc) > abs(dr) ? abs(dc) : abs(dr);
    if (steps < 1)
        steps = 1;
    for (int s = 0; s <= steps; s++) {
        int c = c0 + (dc * s) / steps;
        int r = r0 + (dr * s) / steps;
        put_cell(canvas, r, c, glyph);
    }
}

static void fmt_num(char *buf, size_t n, double v)
{
    if (!isfinite(v)) {
        snprintf(buf, n, "  nan");
        return;
    }
    double a = fabs(v);
    if (a != 0.0 && (a >= 1e5 || a < 1e-3))
        snprintf(buf, n, "%9.2e", v);
    else
        snprintf(buf, n, "%9.4g", v);
}

void terminalc_plot(TerminalCCalc *c, const char *expr_in, const char *lo, const char *hi)
{
    char expr[TERMINALC_LINE_MAX];
    size_t elen = strlen(expr_in);
    if (elen >= sizeof expr) {
        fprintf(stderr, "%sPlot expression too long.%s\n", C_B_RED, C_RST);
        return;
    }
    memcpy(expr, expr_in, elen + 1);
    normalize_expr(expr);
    if (!expr[0]) {
        fprintf(stderr, "%sEmpty plot expression. Try: plot(x, x=0..2)%s\n",
                C_B_RED, C_RST);
        return;
    }

    TerminalCValue a, b;
    if (!terminalc_eval(c, lo, &a) || !terminalc_eval(c, hi, &b)) {
        terminalc_print_error(c);
        return;
    }

    mpfr_t l, h, xv, yv;
    mpfr_inits2(c->bits, l, h, xv, yv, (mpfr_ptr)0);
    terminalc_as_real(c, l, &a);
    terminalc_as_real(c, h, &b);
    terminalc_value_clear(&a);
    terminalc_value_clear(&b);

    if (mpfr_cmp(l, h) >= 0) {
        fprintf(stderr, "%sPlot range must increase (lo < hi).%s\n", C_B_RED,
                C_RST);
        mpfr_clears(l, h, xv, yv, (mpfr_ptr)0);
        return;
    }

    double xmin = mpfr_get_d(l, MPFR_RNDN);
    double xmax = mpfr_get_d(h, MPFR_RNDN);

    /*
     * Interactive popup with real vector lines (Win32 GDI) when stdout is a
     * TTY. Force ASCII with TERMINALC_ASCII_PLOT=1. Non-Windows → ASCII only.
     */
    /* GUI only for fully interactive sessions (both stdin/stdout are TTYs). */
    if (TERMINALC_ISATTY_OUT() && TERMINALC_ISATTY_IN() && !getenv("TERMINALC_ASCII_PLOT")) {
        if (terminalc_plot_gui(c, expr, xmin, xmax)) {
            mpfr_clears(l, h, xv, yv, (mpfr_ptr)0);
            return;
        }
    }

    double z[PLOT_W];
    bool ok[PLOT_W];
    double mn = 0, mx = 0;
    int nvalid = 0;
    int first_err = -1;
    const char *err_msg = NULL;

    for (int i = 0; i < PLOT_W; i++) {
        mpfr_sub(xv, h, l, MPFR_RNDN);
        mpfr_mul_ui(xv, xv, (unsigned long)i, MPFR_RNDN);
        mpfr_div_ui(xv, xv, (unsigned long)(PLOT_W - 1), MPFR_RNDN);
        mpfr_add(xv, xv, l, MPFR_RNDN);
        c->x.type = TERMINALC_REAL;
        mpfr_set(c->x.r, xv, MPFR_RNDN);

        TerminalCValue v;
        if (!terminalc_eval(c, expr, &v)) {
            ok[i] = false;
            z[i] = 0;
            if (first_err < 0) {
                first_err = i;
                err_msg = c->error;
            }
            continue;
        }
        terminalc_as_real(c, yv, &v);
        z[i] = mpfr_get_d(yv, MPFR_RNDN);
        terminalc_value_clear(&v);
        if (!isfinite(z[i])) {
            ok[i] = false;
            continue;
        }
        ok[i] = true;
        if (nvalid == 0 || z[i] < mn)
            mn = z[i];
        if (nvalid == 0 || z[i] > mx)
            mx = z[i];
        nvalid++;
    }

    if (nvalid == 0) {
        fprintf(stderr,
                "%sPlot failed: could not evaluate '%s'%s\n", C_B_RED, expr,
                C_RST);
        if (err_msg)
            fprintf(stderr, "%s  reason: %s%s\n", C_DIM, err_msg, C_RST);
        fprintf(stderr,
                "%s  tip: use free variable x, e.g. plot(x, x=0..2) or "
                "plot(y=x, x=0..2)%s\n",
                C_DIM, C_RST);
        mpfr_clears(l, h, xv, yv, (mpfr_ptr)0);
        return;
    }

    /* Pad y-range 8%; ensure non-zero span. */
    double span = mx - mn;
    if (span == 0.0 || !isfinite(span)) {
        double pad = (fabs(mx) > 1.0) ? fabs(mx) * 0.1 : 1.0;
        mx += pad;
        mn -= pad;
        span = mx - mn;
    } else {
        double pad = span * 0.08;
        if (pad == 0.0)
            pad = 1.0;
        mx += pad;
        mn -= pad;
    }

    /* Prefer including y=0 in view when data crosses or is near zero. */
    if (mn > 0 && mn < span * 0.25)
        mn = 0;
    if (mx < 0 && -mx < span * 0.25)
        mx = 0;

    char canvas[PLOT_H][PLOT_W];
    for (int r = 0; r < PLOT_H; r++)
        for (int q = 0; q < PLOT_W; q++)
            canvas[r][q] = ' ';

    /* Axes */
    int zero_row = -1;
    int zero_col = -1;
    if (mn <= 0.0 && mx >= 0.0)
        zero_row = y_to_row(0.0, mn, mx, PLOT_H);
    if (xmin <= 0.0 && xmax >= 0.0)
        zero_col = x_to_col(0.0, xmin, xmax, PLOT_W);

    /* Use : and - so axes don't collide visually with the outer | frame. */
    if (zero_row >= 0) {
        for (int q = 0; q < PLOT_W; q++)
            put_cell(canvas, zero_row, q, '-');
    }
    if (zero_col >= 0) {
        for (int r = 0; r < PLOT_H; r++)
            put_cell(canvas, r, zero_col, ':');
    }
    if (zero_row >= 0 && zero_col >= 0)
        put_cell(canvas, zero_row, zero_col, '+');

    /* Curve: continuous polyline (slope-aware - | / \) */
    int prev_c = -1, prev_r = -1;
    for (int i = 0; i < PLOT_W; i++) {
        if (!ok[i]) {
            prev_c = -1;
            continue;
        }
        int r = y_to_row(z[i], mn, mx, PLOT_H);
        if (prev_c >= 0)
            connect_cols(canvas, prev_c, prev_r, i, r);
        else
            put_cell(canvas, r, i, '=');
        prev_c = i;
        prev_r = r;
    }

    /* Mark endpoints */
    for (int i = 0; i < PLOT_W; i++) {
        if (ok[i]) {
            put_cell(canvas, y_to_row(z[i], mn, mx, PLOT_H), i, 'o');
            break;
        }
    }
    for (int i = PLOT_W - 1; i >= 0; i--) {
        if (ok[i]) {
            put_cell(canvas, y_to_row(z[i], mn, mx, PLOT_H), i, 'o');
            break;
        }
    }

    printf("%s+-- Plot: y = %s%s", C_B_CYN, C_B_YEL, C_RST);
    printf("%s", C_B_YEL);
    fputs(expr, stdout);
    printf("%s ", C_RST);
    /* pad title bar */
    int tlen = 12 + (int)strlen(expr);
    for (int i = tlen; i < 48; i++)
        putchar('-');
    printf("%s+%s\n", C_B_CYN, C_RST);

    char ybuf[16];
    for (int row = 0; row < PLOT_H; row++) {
        double yv_lab = mx - (mx - mn) * row / (double)(PLOT_H - 1);
        /* Label top, middle, bottom (and zero row if distinct). */
        bool label = (row == 0 || row == PLOT_H - 1 || row == PLOT_H / 2 ||
                      row == zero_row);
        if (label) {
            double lab = (row == zero_row) ? 0.0 : yv_lab;
            fmt_num(ybuf, sizeof ybuf, lab);
            printf("%s%*s%s %s|%s", C_DIM, YLAB, ybuf, C_RST, C_B_CYN, C_RST);
        } else {
            printf("%*s %s|%s", YLAB, "", C_B_CYN, C_RST);
        }
        for (int col = 0; col < PLOT_W; col++) {
            char ch = canvas[row][col];
            if (is_curve_ch(ch))
                printf("%s%c%s", C_B_GRN, ch, C_RST);
            else if (is_axis_ch(ch))
                printf("%s%c%s", C_DIM, ch, C_RST);
            else
                putchar(ch);
        }
        printf("%s|%s\n", C_B_CYN, C_RST);
    }

    /* X-axis tick labels */
    char x0[16], xmid[16], x1[16];
    fmt_num(x0, sizeof x0, xmin);
    fmt_num(xmid, sizeof xmid, 0.5 * (xmin + xmax));
    fmt_num(x1, sizeof x1, xmax);
    printf("%*s %s", YLAB, "", C_DIM);
    printf("%-10s", x0);
    int midpad = PLOT_W / 2 - 5;
    if (midpad < 1)
        midpad = 1;
    for (int i = 10; i < midpad; i++)
        putchar(' ');
    printf("%-10s", xmid);
    int rest = PLOT_W - midpad - 10;
    for (int i = 10; i < rest; i++)
        putchar(' ');
    printf("%10s", x1);
    printf("%s\n", C_RST);

    printf("%s|%s  x: %s", C_B_CYN, C_RST, C_YEL);
    mpfr_printf("%.*RNf", 6, l);
    printf("%s .. %s", C_RST, C_YEL);
    mpfr_printf("%.*RNf", 6, h);
    printf("%s   y: %s%.6g%s .. %s%.6g%s", C_RST, C_YEL, mn, C_RST, C_YEL, mx,
           C_RST);
    if (nvalid < PLOT_W)
        printf("  %s[%d/%d samples]%s", C_DIM, nvalid, PLOT_W, C_RST);
    putchar('\n');
    printf("%s+-------------------------------------------+%s\n", C_B_CYN, C_RST);

    mpfr_clears(l, h, xv, yv, (mpfr_ptr)0);
}
