#include "terminalc.h"
#include "color.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    TERMINALC_SVG_WIDTH = 1000,
    TERMINALC_SVG_HEIGHT = 640,
    TERMINALC_SVG_MARGIN_L = 76,
    TERMINALC_SVG_MARGIN_R = 30,
    TERMINALC_SVG_MARGIN_T = 74,
    TERMINALC_SVG_MARGIN_B = 64,
    TERMINALC_SVG_SAMPLES = 1200,
    TERMINALC_SVG_MAX_CURVES = 8
};

static void trim_inplace(char *s)
{
    char *start = s;
    while (isspace((unsigned char)*start))
        start++;
    if (start != s)
        memmove(s, start, strlen(start) + 1);
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1]))
        s[--n] = '\0';
}

static void normalize_expr(char *expr)
{
    trim_inplace(expr);
    if (expr[0] == 'y' || expr[0] == 'Y') {
        char *p = expr + 1;
        while (isspace((unsigned char)*p))
            p++;
        if (*p == '=') {
            p++;
            while (isspace((unsigned char)*p))
                p++;
            memmove(expr, p, strlen(p) + 1);
            trim_inplace(expr);
        }
    }
}

static size_t split_curves(const char *input,
                           char curves[TERMINALC_SVG_MAX_CURVES][TERMINALC_LINE_MAX])
{
    size_t count = 0;
    int depth = 0;
    const char *start = input;
    for (const char *p = input;; p++) {
        char ch = *p;
        if (ch == '(' || ch == '[')
            depth++;
        else if (ch == ')' || ch == ']') {
            if (depth > 0)
                depth--;
        }
        if ((ch == ';' && depth == 0) || ch == '\0') {
            size_t n = (size_t)(p - start);
            if (n == 0 || n >= TERMINALC_LINE_MAX || count >= TERMINALC_SVG_MAX_CURVES)
                return 0;
            memcpy(curves[count], start, n);
            curves[count][n] = '\0';
            normalize_expr(curves[count]);
            if (!curves[count][0])
                return 0;
            count++;
            if (ch == '\0')
                break;
            start = p + 1;
        }
    }
    return count;
}

static void svg_text(FILE *fp, const char *text)
{
    for (const char *p = text; *p; p++) {
        switch (*p) {
        case '&': fputs("&amp;", fp); break;
        case '<': fputs("&lt;", fp); break;
        case '>': fputs("&gt;", fp); break;
        case '"': fputs("&quot;", fp); break;
        default: fputc(*p, fp); break;
        }
    }
}

static double nice_step(double lo, double hi, int target)
{
    double span = hi - lo;
    if (!(span > 0.0) || !isfinite(span))
        return 1.0;
    double rough = span / (double)target;
    double power = pow(10.0, floor(log10(rough)));
    double fraction = rough / power;
    double nice = fraction <= 1.0 ? 1.0 : fraction <= 2.0 ? 2.0
                           : fraction <= 5.0 ? 5.0 : 10.0;
    return nice * power;
}

static void format_tick(char *buf, size_t size, double value, double step)
{
    if (fabs(value) < fabs(step) * 1e-9)
        value = 0.0;
    double av = fabs(value);
    double as = fabs(step);
    if (av != 0.0 && (av >= 1e6 || av < 1e-4)) {
        snprintf(buf, size, "%.2e", value);
        return;
    }
    int decimals = as < 1.0 ? (int)ceil(-log10(as)) : 0;
    if (decimals < 0)
        decimals = 0;
    if (decimals > 12)
        decimals = 12;
    snprintf(buf, size, "%.*f", decimals, value);
    char *dot = strchr(buf, '.');
    if (dot) {
        char *end = buf + strlen(buf) - 1;
        while (end > dot && *end == '0')
            *end-- = '\0';
        if (end == dot)
            *end = '\0';
    }
}

void terminalc_plot_svg(TerminalCCalc *c, const char *expr_in, const char *lo,
                        const char *hi, const char *path)
{
    char curves[TERMINALC_SVG_MAX_CURVES][TERMINALC_LINE_MAX];
    size_t curve_count = split_curves(expr_in, curves);
    if (curve_count == 0) {
        fprintf(stderr, "%sPlot export needs one to eight expressions separated by ';'.%s\n",
                C_B_RED, C_RST);
        return;
    }
    if (!path || !path[0]) {
        fprintf(stderr, "%sPlot export path is empty.%s\n", C_B_RED, C_RST);
        return;
    }

    TerminalCValue lower, upper;
    if (!terminalc_eval(c, lo, &lower) || !terminalc_eval(c, hi, &upper)) {
        terminalc_print_error(c);
        return;
    }
    mpfr_t l, h, xv, yv;
    mpfr_inits2(c->bits, l, h, xv, yv, (mpfr_ptr)0);
    terminalc_as_real(c, l, &lower);
    terminalc_as_real(c, h, &upper);
    terminalc_value_clear(&lower);
    terminalc_value_clear(&upper);
    if (mpfr_cmp(l, h) >= 0) {
        fprintf(stderr, "%sPlot range must increase (lo < hi).%s\n", C_B_RED, C_RST);
        mpfr_clears(l, h, xv, yv, (mpfr_ptr)0);
        return;
    }

    double xmin = mpfr_get_d(l, MPFR_RNDN);
    double xmax = mpfr_get_d(h, MPFR_RNDN);
    size_t total = curve_count * TERMINALC_SVG_SAMPLES;
    double *ys = (double *)calloc(total, sizeof *ys);
    bool *valid = (bool *)calloc(total, sizeof *valid);
    double *xs = (double *)calloc(TERMINALC_SVG_SAMPLES, sizeof *xs);
    if (!ys || !valid || !xs) {
        fprintf(stderr, "%sPlot export ran out of memory.%s\n", C_B_RED, C_RST);
        free(ys);
        free(valid);
        free(xs);
        mpfr_clears(l, h, xv, yv, (mpfr_ptr)0);
        return;
    }

    double ymin = 0.0, ymax = 0.0;
    size_t valid_count = 0;
    for (int i = 0; i < TERMINALC_SVG_SAMPLES; i++) {
        mpfr_sub(xv, h, l, MPFR_RNDN);
        mpfr_mul_ui(xv, xv, (unsigned long)i, MPFR_RNDN);
        mpfr_div_ui(xv, xv, TERMINALC_SVG_SAMPLES - 1, MPFR_RNDN);
        mpfr_add(xv, xv, l, MPFR_RNDN);
        xs[i] = mpfr_get_d(xv, MPFR_RNDN);
        c->x.type = TERMINALC_REAL;
        mpfr_set(c->x.r, xv, MPFR_RNDN);
        for (size_t curve = 0; curve < curve_count; curve++) {
            TerminalCValue value;
            if (!terminalc_eval(c, curves[curve], &value))
                continue;
            terminalc_as_real(c, yv, &value);
            double y = mpfr_get_d(yv, MPFR_RNDN);
            terminalc_value_clear(&value);
            if (!isfinite(y))
                continue;
            ys[curve * TERMINALC_SVG_SAMPLES + i] = y;
            valid[curve * TERMINALC_SVG_SAMPLES + i] = true;
            if (valid_count == 0 || y < ymin)
                ymin = y;
            if (valid_count == 0 || y > ymax)
                ymax = y;
            valid_count++;
        }
    }
    mpfr_clears(l, h, xv, yv, (mpfr_ptr)0);

    if (valid_count == 0) {
        fprintf(stderr, "%sPlot export failed: no curve samples were valid.%s\n",
                C_B_RED, C_RST);
        free(ys);
        free(valid);
        free(xs);
        return;
    }
    double span = ymax - ymin;
    if (!(span > 0.0) || !isfinite(span)) {
        double pad = fabs(ymax) > 1.0 ? fabs(ymax) * 0.1 : 1.0;
        ymin -= pad;
        ymax += pad;
    } else {
        ymin -= span * 0.08;
        ymax += span * 0.08;
    }

    FILE *fp = fopen(path, "w");
    if (!fp) {
        fprintf(stderr, "%sCannot write plot export '%s'.%s\n", C_B_RED, path, C_RST);
        free(ys);
        free(valid);
        free(xs);
        return;
    }

    const double left = TERMINALC_SVG_MARGIN_L;
    const double right = TERMINALC_SVG_WIDTH - TERMINALC_SVG_MARGIN_R;
    const double top = TERMINALC_SVG_MARGIN_T;
    const double bottom = TERMINALC_SVG_HEIGHT - TERMINALC_SVG_MARGIN_B;
    fprintf(fp, "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%d\" height=\"%d\" viewBox=\"0 0 %d %d\">\n",
            TERMINALC_SVG_WIDTH, TERMINALC_SVG_HEIGHT, TERMINALC_SVG_WIDTH,
            TERMINALC_SVG_HEIGHT);
    fputs("<rect width=\"100%\" height=\"100%\" fill=\"white\"/>\n", fp);
    fputs("<style>text{font-family:ui-monospace,SFMono-Regular,Menlo,monospace;font-size:12px;fill:#334155}.grid{stroke:#dbe3ec;stroke-width:1}.axis{stroke:#64748b;stroke-width:1.2}</style>\n", fp);
    fputs("<text x=\"76\" y=\"30\" font-size=\"18\" font-weight=\"600\">TerminalC plot</text>\n", fp);
    fputs("<text x=\"76\" y=\"50\">y = ", fp);
    for (size_t i = 0; i < curve_count; i++) {
        if (i)
            fputs(" ; ", fp);
        svg_text(fp, curves[i]);
    }
    fputs("</text>\n", fp);

    double x_step = nice_step(xmin, xmax, 9);
    double y_step = nice_step(ymin, ymax, 8);
    for (double x = ceil(xmin / x_step - 1e-12) * x_step;
         x <= xmax + x_step * 0.5 && x < xmin + x_step * 40.0; x += x_step) {
        double sx = left + (x - xmin) / (xmax - xmin) * (right - left);
        char label[32];
        format_tick(label, sizeof label, x, x_step);
        fprintf(fp, "<line class=\"grid\" x1=\"%.2f\" y1=\"%.2f\" x2=\"%.2f\" y2=\"%.2f\"/><text x=\"%.2f\" y=\"%.2f\" text-anchor=\"middle\">%s</text>\n",
                sx, top, sx, bottom, sx, bottom + 28.0, label);
    }
    for (double y = ceil(ymin / y_step - 1e-12) * y_step;
         y <= ymax + y_step * 0.5 && y < ymin + y_step * 40.0; y += y_step) {
        double sy = bottom - (y - ymin) / (ymax - ymin) * (bottom - top);
        char label[32];
        format_tick(label, sizeof label, y, y_step);
        fprintf(fp, "<line class=\"grid\" x1=\"%.2f\" y1=\"%.2f\" x2=\"%.2f\" y2=\"%.2f\"/><text x=\"%.2f\" y=\"%.2f\" text-anchor=\"end\">%s</text>\n",
                left, sy, right, sy, left - 10.0, sy + 4.0, label);
    }
    if (xmin <= 0.0 && xmax >= 0.0) {
        double sx = left + (-xmin) / (xmax - xmin) * (right - left);
        fprintf(fp, "<line class=\"axis\" x1=\"%.2f\" y1=\"%.2f\" x2=\"%.2f\" y2=\"%.2f\"/>\n",
                sx, top, sx, bottom);
    }
    if (ymin <= 0.0 && ymax >= 0.0) {
        double sy = bottom - (-ymin) / (ymax - ymin) * (bottom - top);
        fprintf(fp, "<line class=\"axis\" x1=\"%.2f\" y1=\"%.2f\" x2=\"%.2f\" y2=\"%.2f\"/>\n",
                left, sy, right, sy);
    }

    static const char *colors[] = {"#16a34a", "#2563eb", "#dc2626", "#9333ea",
                                   "#ea580c", "#0891b2", "#be123c", "#4f46e5"};
    for (size_t curve = 0; curve < curve_count; curve++) {
        fprintf(fp, "<path d=\"");
        bool started = false;
        for (int i = 0; i < TERMINALC_SVG_SAMPLES; i++) {
            if (!valid[curve * TERMINALC_SVG_SAMPLES + i]) {
                started = false;
                continue;
            }
            double sx = left + (xs[i] - xmin) / (xmax - xmin) * (right - left);
            double sy = bottom - (ys[curve * TERMINALC_SVG_SAMPLES + i] - ymin) /
                        (ymax - ymin) * (bottom - top);
            fprintf(fp, "%c %.2f %.2f ", started ? 'L' : 'M', sx, sy);
            started = true;
        }
        fprintf(fp, "\" fill=\"none\" stroke=\"%s\" stroke-width=\"2\"/>\n",
                colors[curve % (sizeof colors / sizeof colors[0])]);
        double lx = left + 16.0 + curve * 140.0;
        fprintf(fp, "<line x1=\"%.2f\" y1=\"%.2f\" x2=\"%.2f\" y2=\"%.2f\" stroke=\"%s\" stroke-width=\"3\"/><text x=\"%.2f\" y=\"%.2f\">",
                lx, top - 22.0, lx + 24.0, top - 22.0,
                colors[curve % (sizeof colors / sizeof colors[0])], lx + 30.0,
                top - 18.0);
        svg_text(fp, curves[curve]);
        fputs("</text>\n", fp);
    }
    fputs("</svg>\n", fp);
    fclose(fp);
    free(ys);
    free(valid);
    free(xs);
    printf("Wrote SVG plot to '%s' (%zu curve%s).\n", path, curve_count,
           curve_count == 1 ? "" : "s");
}

void terminalc_plot_csv(TerminalCCalc *c, const char *expr_in, const char *lo,
                        const char *hi, const char *path)
{
    char curves[TERMINALC_SVG_MAX_CURVES][TERMINALC_LINE_MAX];
    size_t curve_count = split_curves(expr_in, curves);
    if (curve_count == 0 || !path || !path[0]) {
        fprintf(stderr, "%sCSV plot export needs expressions and a file path.%s\n",
                C_B_RED, C_RST);
        return;
    }

    TerminalCValue lower, upper;
    if (!terminalc_eval(c, lo, &lower) || !terminalc_eval(c, hi, &upper)) {
        terminalc_print_error(c);
        return;
    }
    mpfr_t l, h, xv, yv;
    mpfr_inits2(c->bits, l, h, xv, yv, (mpfr_ptr)0);
    terminalc_as_real(c, l, &lower);
    terminalc_as_real(c, h, &upper);
    terminalc_value_clear(&lower);
    terminalc_value_clear(&upper);
    if (mpfr_cmp(l, h) >= 0) {
        fprintf(stderr, "%sPlot range must increase (lo < hi).%s\n", C_B_RED,
                C_RST);
        mpfr_clears(l, h, xv, yv, (mpfr_ptr)0);
        return;
    }

    FILE *fp = fopen(path, "w");
    if (!fp) {
        fprintf(stderr, "%sCannot write CSV plot export '%s'.%s\n", C_B_RED,
                path, C_RST);
        mpfr_clears(l, h, xv, yv, (mpfr_ptr)0);
        return;
    }
    fputs("x", fp);
    for (size_t curve = 0; curve < curve_count; curve++)
        fprintf(fp, ",\"%s\"", curves[curve]);
    fputc('\n', fp);

    for (int i = 0; i < TERMINALC_SVG_SAMPLES; i++) {
        mpfr_sub(xv, h, l, MPFR_RNDN);
        mpfr_mul_ui(xv, xv, (unsigned long)i, MPFR_RNDN);
        mpfr_div_ui(xv, xv, TERMINALC_SVG_SAMPLES - 1, MPFR_RNDN);
        mpfr_add(xv, xv, l, MPFR_RNDN);
        double x = mpfr_get_d(xv, MPFR_RNDN);
        fprintf(fp, "%.17g", x);
        c->x.type = TERMINALC_REAL;
        mpfr_set(c->x.r, xv, MPFR_RNDN);
        for (size_t curve = 0; curve < curve_count; curve++) {
            TerminalCValue value;
            if (!terminalc_eval(c, curves[curve], &value)) {
                fputc(',', fp);
                continue;
            }
            terminalc_as_real(c, yv, &value);
            double y = mpfr_get_d(yv, MPFR_RNDN);
            terminalc_value_clear(&value);
            if (isfinite(y))
                fprintf(fp, ",%.17g", y);
            else
                fputc(',', fp);
        }
        fputc('\n', fp);
    }
    fclose(fp);
    mpfr_clears(l, h, xv, yv, (mpfr_ptr)0);
    printf("Wrote CSV plot data to '%s' (%zu curve%s, %d samples).\n", path,
           curve_count, curve_count == 1 ? "" : "s", TERMINALC_SVG_SAMPLES);
}
