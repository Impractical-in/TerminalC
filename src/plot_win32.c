/**
 * Interactive TerminalC plot window — Win32 GDI (no extra libraries).
 *
 * Controls:
 *   Mouse wheel     zoom toward cursor
 *   Left-drag       pan
 *   + / =           zoom in (center)
 *   - / _           zoom out (center)
 *   0               reset view
 *   y               auto-fit Y to visible samples
 *   a               dump view range to the console
 *   Esc / q         close window
 *   Hover curve     snap readout of (x, y) near the line
 */
#include "plot_gui.h"
#include "color.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <windowsx.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    TERMINALC_PLOT_SAMPLES_MIN = 2500,
    TERMINALC_PLOT_SAMPLES_MAX = 10000,
    TERMINALC_PLOT_MARGIN_L = 78,
    TERMINALC_PLOT_MARGIN_R = 22,
    TERMINALC_PLOT_MARGIN_T = 44,
    TERMINALC_PLOT_MARGIN_B = 58,
    TERMINALC_PLOT_MAX_TICKS = 12,
    /* Hit radius in pixels for "hovering the curve". */
    TERMINALC_PLOT_HOVER_PX = 12
};

typedef struct {
    TerminalCCalc *calc;
    char expr[TERMINALC_LINE_MAX];
    double x0, x1, y0, y1;
    double ix0, ix1, iy0, iy1;
    bool dragging;
    int last_mx, last_my;
    /* Hover snap-to-curve readout */
    bool hover_on;
    double hover_x, hover_y;
    int hover_sx, hover_sy;
    int mouse_x, mouse_y;
} TerminalCPlotState;

static TerminalCPlotState g_ps;
static const char *TERMINALC_PLOT_CLASS = "TerminalCPlotWindow";

static void plot_size(RECT client, int *pw, int *ph)
{
    *pw = (client.right - client.left) - TERMINALC_PLOT_MARGIN_L - TERMINALC_PLOT_MARGIN_R;
    *ph = (client.bottom - client.top) - TERMINALC_PLOT_MARGIN_T - TERMINALC_PLOT_MARGIN_B;
    if (*pw < 1)
        *pw = 1;
    if (*ph < 1)
        *ph = 1;
}

static void world_to_screen(const TerminalCPlotState *s, RECT client, double x,
                            double y, int *ox, int *oy)
{
    int pw, ph;
    plot_size(client, &pw, &ph);
    double dx = s->x1 - s->x0;
    double dy = s->y1 - s->y0;
    if (dx == 0.0)
        dx = 1.0;
    if (dy == 0.0)
        dy = 1.0;
    *ox = TERMINALC_PLOT_MARGIN_L + (int)((x - s->x0) / dx * pw + 0.5);
    *oy = TERMINALC_PLOT_MARGIN_T + (int)((s->y1 - y) / dy * ph + 0.5);
}

static void screen_to_world(const TerminalCPlotState *s, RECT client, int sx, int sy,
                            double *ox, double *oy)
{
    int pw, ph;
    plot_size(client, &pw, &ph);
    double dx = s->x1 - s->x0;
    double dy = s->y1 - s->y0;
    *ox = s->x0 + (double)(sx - TERMINALC_PLOT_MARGIN_L) / pw * dx;
    *oy = s->y1 - (double)(sy - TERMINALC_PLOT_MARGIN_T) / ph * dy;
}

static bool sample_y(TerminalCCalc *c, const char *expr, double x, double *y_out)
{
    c->x.type = TERMINALC_REAL;
    mpfr_set_d(c->x.r, x, MPFR_RNDN);
    TerminalCValue v;
    if (!terminalc_eval(c, expr, &v))
        return false;
    mpfr_t yr;
    mpfr_init2(yr, c->bits);
    terminalc_as_real(c, yr, &v);
    *y_out = mpfr_get_d(yr, MPFR_RNDN);
    mpfr_clear(yr);
    terminalc_value_clear(&v);
    return isfinite(*y_out);
}

static int sample_count_for(int plot_w)
{
    /* ~2–3 samples per horizontal pixel for crisp curves when zoomed. */
    int n = plot_w * 3;
    if (n < TERMINALC_PLOT_SAMPLES_MIN)
        n = TERMINALC_PLOT_SAMPLES_MIN;
    if (n > TERMINALC_PLOT_SAMPLES_MAX)
        n = TERMINALC_PLOT_SAMPLES_MAX;
    return n;
}

static void auto_y_fit(TerminalCPlotState *s)
{
    double mn = 0, mx = 0;
    int nvalid = 0;
    int n = TERMINALC_PLOT_SAMPLES_MIN;
    for (int i = 0; i < n; i++) {
        double t = (double)i / (double)(n - 1);
        double x = s->x0 + t * (s->x1 - s->x0);
        double y;
        if (!sample_y(s->calc, s->expr, x, &y))
            continue;
        if (nvalid == 0 || y < mn)
            mn = y;
        if (nvalid == 0 || y > mx)
            mx = y;
        nvalid++;
    }
    if (nvalid == 0) {
        s->y0 = -1.0;
        s->y1 = 1.0;
        return;
    }
    double span = mx - mn;
    if (span == 0.0 || !isfinite(span)) {
        double pad = (fabs(mx) > 1.0) ? fabs(mx) * 0.1 : 1.0;
        mx += pad;
        mn -= pad;
    } else {
        double pad = span * 0.08;
        if (pad == 0.0)
            pad = 1.0;
        mx += pad;
        mn -= pad;
    }
    if (mn > 0.0 && mn < (mx - mn) * 0.25)
        mn = 0.0;
    if (mx < 0.0 && -mx < (mx - mn) * 0.25)
        mx = 0.0;
    s->y0 = mn;
    s->y1 = mx;
}

static void zoom_about(TerminalCPlotState *s, double wx, double wy, double factor)
{
    if (factor <= 0.0 || !isfinite(factor))
        return;
    s->x0 = wx - (wx - s->x0) / factor;
    s->x1 = wx + (s->x1 - wx) / factor;
    s->y0 = wy - (wy - s->y0) / factor;
    s->y1 = wy + (s->y1 - wy) / factor;
    if (!(s->x1 > s->x0) || !isfinite(s->x0) || !isfinite(s->x1)) {
        s->x0 = s->ix0;
        s->x1 = s->ix1;
    }
    if (!(s->y1 > s->y0) || !isfinite(s->y0) || !isfinite(s->y1)) {
        s->y0 = s->iy0;
        s->y1 = s->iy1;
    }
}

/* Nice tick step for ~target major ticks across [lo, hi]. */
static double nice_step(double lo, double hi, int target)
{
    double span = hi - lo;
    if (!(span > 0.0) || !isfinite(span))
        return 1.0;
    if (target < 2)
        target = 2;
    double rough = span / (double)target;
    double expv = floor(log10(rough));
    double base = pow(10.0, expv);
    double frac = rough / base;
    double nice;
    if (frac <= 1.0)
        nice = 1.0;
    else if (frac <= 2.0)
        nice = 2.0;
    else if (frac <= 2.5)
        nice = 2.5;
    else if (frac <= 5.0)
        nice = 5.0;
    else
        nice = 10.0;
    return nice * base;
}

static double nice_ceil(double v, double step)
{
    return ceil(v / step - 1e-12) * step;
}

/* Format tick labels with precision that tightens as you zoom. */
static void fmt_tick(char *buf, size_t n, double v, double step)
{
    if (!isfinite(v)) {
        snprintf(buf, n, "nan");
        return;
    }
    /* Snap near-zero to 0 for clean axes. */
    if (fabs(v) < step * 1e-9)
        v = 0.0;

    double av = fabs(v);
    double as = fabs(step);
    if (as == 0.0)
        as = 1.0;

    if (av != 0.0 && (av >= 1e6 || av < 1e-4)) {
        int exp_digits = 2;
        snprintf(buf, n, "%.*e", exp_digits, v);
        return;
    }

    int decimals = 0;
    if (as < 1.0) {
        decimals = (int)ceil(-log10(as));
        if (decimals < 0)
            decimals = 0;
        if (decimals > 12)
            decimals = 12;
        /* Extra digit when zoomed deep so labels stay distinct. */
        if (as < 1e-3)
            decimals += 1;
        if (decimals > 14)
            decimals = 14;
    }
    snprintf(buf, n, "%.*f", decimals, v);

    /* Trim trailing zeros after decimal. */
    if (strchr(buf, '.')) {
        char *p = buf + strlen(buf) - 1;
        while (p > buf && *p == '0')
            *p-- = '\0';
        if (p > buf && *p == '.')
            *p = '\0';
    }
}

static void draw_string(HDC hdc, int x, int y, const char *text)
{
    TextOutA(hdc, x, y, text, (int)strlen(text));
}

/* Format hover coords with precision that tracks current zoom. */
static void fmt_hover_num(char *buf, size_t n, double v, double view_span)
{
    if (!isfinite(v)) {
        snprintf(buf, n, "nan");
        return;
    }
    if (fabs(v) != 0.0 && (fabs(v) >= 1e6 || fabs(v) < 1e-5)) {
        snprintf(buf, n, "%.6g", v);
        return;
    }
    int decimals = 4;
    if (view_span > 0.0 && isfinite(view_span)) {
        double s = view_span / 200.0; /* ~pixel-scale significance */
        if (s > 0.0) {
            decimals = (int)ceil(-log10(s));
            if (decimals < 2)
                decimals = 2;
            if (decimals > 12)
                decimals = 12;
        }
    }
    snprintf(buf, n, "%.*f", decimals, v);
}

/*
 * Snap hover to y=f(x) under the cursor. For a function graph the curve has
 * one y per x — evaluate once and accept if the mouse is within TERMINALC_PLOT_HOVER_PX
 * of that screen point (cheap, no full nearest-neighbor scan).
 */
static void update_hover(HWND hwnd, int mx, int my)
{
    RECT client;
    GetClientRect(hwnd, &client);
    int w = client.right - client.left;
    int h = client.bottom - client.top;
    int clip_l = TERMINALC_PLOT_MARGIN_L;
    int clip_t = TERMINALC_PLOT_MARGIN_T;
    int clip_r = w - TERMINALC_PLOT_MARGIN_R;
    int clip_b = h - TERMINALC_PLOT_MARGIN_B;

    g_ps.mouse_x = mx;
    g_ps.mouse_y = my;
    g_ps.hover_on = false;

    if (mx < clip_l || mx > clip_r || my < clip_t || my > clip_b)
        return;

    double wx, wy_mouse;
    screen_to_world(&g_ps, client, mx, my, &wx, &wy_mouse);
    (void)wy_mouse;

    if (!(wx >= g_ps.x0 && wx <= g_ps.x1))
        return;

    double wy;
    if (!sample_y(g_ps.calc, g_ps.expr, wx, &wy))
        return;
    if (!(wy >= g_ps.y0 && wy <= g_ps.y1))
        return;

    int sx, sy;
    world_to_screen(&g_ps, client, wx, wy, &sx, &sy);

    int dx = mx - sx;
    int dy = my - sy;
    if (dx * dx + dy * dy > TERMINALC_PLOT_HOVER_PX * TERMINALC_PLOT_HOVER_PX)
        return;

    g_ps.hover_on = true;
    g_ps.hover_x = wx;
    g_ps.hover_y = wy;
    g_ps.hover_sx = sx;
    g_ps.hover_sy = sy;
}

static void draw_hover(HDC hdc, RECT client, HFONT font_ui)
{
    if (!g_ps.hover_on)
        return;

    int w = client.right - client.left;
    int h = client.bottom - client.top;
    int clip_l = TERMINALC_PLOT_MARGIN_L;
    int clip_t = TERMINALC_PLOT_MARGIN_T;
    int clip_r = w - TERMINALC_PLOT_MARGIN_R;
    int clip_b = h - TERMINALC_PLOT_MARGIN_B;
    int sx = g_ps.hover_sx;
    int sy = g_ps.hover_sy;

    /* Hairlines to axes */
    HPEN hair = CreatePen(PS_DOT, 1, RGB(100, 160, 220));
    HGDIOBJ old_pen = SelectObject(hdc, hair);
    MoveToEx(hdc, sx, clip_t, NULL);
    LineTo(hdc, sx, clip_b);
    MoveToEx(hdc, clip_l, sy, NULL);
    LineTo(hdc, clip_r, sy);

    /* Point marker */
    HPEN ring = CreatePen(PS_SOLID, 2, RGB(255, 220, 80));
    HBRUSH fill = CreateSolidBrush(RGB(80, 220, 120));
    SelectObject(hdc, ring);
    HGDIOBJ old_brush = SelectObject(hdc, fill);
    Ellipse(hdc, sx - 5, sy - 5, sx + 6, sy + 6);

    /* Hollow outer ring */
    SelectObject(hdc, GetStockObject(NULL_BRUSH));
    HPEN outer = CreatePen(PS_SOLID, 1, RGB(255, 255, 255));
    SelectObject(hdc, outer);
    Ellipse(hdc, sx - 8, sy - 8, sx + 9, sy + 9);

    /* Tooltip box */
    char xb[48], yb[48], line1[96], line2[96];
    fmt_hover_num(xb, sizeof xb, g_ps.hover_x, g_ps.x1 - g_ps.x0);
    fmt_hover_num(yb, sizeof yb, g_ps.hover_y, g_ps.y1 - g_ps.y0);
    snprintf(line1, sizeof line1, "x = %s", xb);
    snprintf(line2, sizeof line2, "y = %s", yb);

    HGDIOBJ old_font = SelectObject(hdc, font_ui);
    SIZE s1, s2;
    GetTextExtentPoint32A(hdc, line1, (int)strlen(line1), &s1);
    GetTextExtentPoint32A(hdc, line2, (int)strlen(line2), &s2);
    int tw = s1.cx > s2.cx ? s1.cx : s2.cx;
    int th = s1.cy + s2.cy + 10;
    int pad = 8;
    int box_w = tw + pad * 2;
    int box_h = th + pad;
    int bx = sx + 14;
    int by = sy - box_h - 10;
    /* Keep box inside window */
    if (bx + box_w > w - 8)
        bx = sx - box_w - 14;
    if (bx < 8)
        bx = 8;
    if (by < 8)
        by = sy + 14;
    if (by + box_h > h - 8)
        by = h - box_h - 8;

    HBRUSH tip_bg = CreateSolidBrush(RGB(32, 36, 48));
    HPEN tip_bd = CreatePen(PS_SOLID, 1, RGB(255, 220, 80));
    SelectObject(hdc, tip_bg);
    SelectObject(hdc, tip_bd);
    RoundRect(hdc, bx, by, bx + box_w, by + box_h, 6, 6);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(255, 240, 180));
    draw_string(hdc, bx + pad, by + 6, line1);
    SetTextColor(hdc, RGB(180, 255, 200));
    draw_string(hdc, bx + pad, by + 6 + s1.cy + 2, line2);

    SelectObject(hdc, old_font);
    SelectObject(hdc, old_pen);
    SelectObject(hdc, old_brush);
    DeleteObject(hair);
    DeleteObject(ring);
    DeleteObject(outer);
    DeleteObject(fill);
    DeleteObject(tip_bg);
    DeleteObject(tip_bd);
}

static void draw_grid_and_ticks(HDC hdc, RECT client, HFONT font_ui)
{
    int w = client.right - client.left;
    int h = client.bottom - client.top;
    int pw, ph;
    plot_size(client, &pw, &ph);

    HGDIOBJ old_font = SelectObject(hdc, font_ui);
    SetBkMode(hdc, TRANSPARENT);

    double x_step = nice_step(g_ps.x0, g_ps.x1, 8);
    double y_step = nice_step(g_ps.y0, g_ps.y1, 8);
    double x_minor = x_step / 5.0;
    double y_minor = y_step / 5.0;

    HPEN grid_minor = CreatePen(PS_SOLID, 1, RGB(36, 36, 46));
    HPEN grid_major = CreatePen(PS_SOLID, 1, RGB(52, 52, 68));
    HPEN axis_pen = CreatePen(PS_SOLID, 1, RGB(110, 110, 130));
    HPEN tick_pen = CreatePen(PS_SOLID, 1, RGB(150, 150, 170));
    HGDIOBJ old_pen = SelectObject(hdc, grid_minor);

    /* Clip grid/curve to plot area */
    int clip_l = TERMINALC_PLOT_MARGIN_L;
    int clip_t = TERMINALC_PLOT_MARGIN_T;
    int clip_r = w - TERMINALC_PLOT_MARGIN_R;
    int clip_b = h - TERMINALC_PLOT_MARGIN_B;

    /* Minor vertical grid */
    SelectObject(hdc, grid_minor);
    for (double x = nice_ceil(g_ps.x0, x_minor); x <= g_ps.x1 + x_minor * 0.5;
         x += x_minor) {
        if (!isfinite(x))
            break;
        /* Skip major positions (drawn thicker). */
        double r = fabs(x / x_step - round(x / x_step));
        if (r < 1e-9)
            continue;
        int sx, sy;
        world_to_screen(&g_ps, client, x, g_ps.y0, &sx, &sy);
        (void)sy;
        if (sx < clip_l || sx > clip_r)
            continue;
        MoveToEx(hdc, sx, clip_t, NULL);
        LineTo(hdc, sx, clip_b);
    }
    /* Minor horizontal grid */
    for (double y = nice_ceil(g_ps.y0, y_minor); y <= g_ps.y1 + y_minor * 0.5;
         y += y_minor) {
        if (!isfinite(y))
            break;
        double r = fabs(y / y_step - round(y / y_step));
        if (r < 1e-9)
            continue;
        int sx, sy;
        world_to_screen(&g_ps, client, g_ps.x0, y, &sx, &sy);
        (void)sx;
        if (sy < clip_t || sy > clip_b)
            continue;
        MoveToEx(hdc, clip_l, sy, NULL);
        LineTo(hdc, clip_r, sy);
    }

    /* Major grid + tick marks + labels */
    SelectObject(hdc, grid_major);
    SetTextColor(hdc, RGB(175, 175, 190));

    int xticks = 0;
    for (double x = nice_ceil(g_ps.x0, x_step); x <= g_ps.x1 + x_step * 0.5;
         x += x_step) {
        if (!isfinite(x) || xticks >= TERMINALC_PLOT_MAX_TICKS * 2)
            break;
        xticks++;
        int sx, sy;
        world_to_screen(&g_ps, client, x, g_ps.y0, &sx, &sy);
        (void)sy;
        if (sx < clip_l - 2 || sx > clip_r + 2)
            continue;
        MoveToEx(hdc, sx, clip_t, NULL);
        LineTo(hdc, sx, clip_b);
        SelectObject(hdc, tick_pen);
        MoveToEx(hdc, sx, clip_b, NULL);
        LineTo(hdc, sx, clip_b + 6);
        SelectObject(hdc, grid_major);

        char lab[48];
        fmt_tick(lab, sizeof lab, x, x_step);
        SIZE sz;
        GetTextExtentPoint32A(hdc, lab, (int)strlen(lab), &sz);
        int tx = sx - sz.cx / 2;
        if (tx < clip_l - 8)
            tx = clip_l - 8;
        if (tx + sz.cx > clip_r + 8)
            tx = clip_r + 8 - sz.cx;
        draw_string(hdc, tx, clip_b + 10, lab);
    }

    int yticks = 0;
    for (double y = nice_ceil(g_ps.y0, y_step); y <= g_ps.y1 + y_step * 0.5;
         y += y_step) {
        if (!isfinite(y) || yticks >= TERMINALC_PLOT_MAX_TICKS * 2)
            break;
        yticks++;
        int sx, sy;
        world_to_screen(&g_ps, client, g_ps.x0, y, &sx, &sy);
        (void)sx;
        if (sy < clip_t - 2 || sy > clip_b + 2)
            continue;
        MoveToEx(hdc, clip_l, sy, NULL);
        LineTo(hdc, clip_r, sy);
        SelectObject(hdc, tick_pen);
        MoveToEx(hdc, clip_l - 6, sy, NULL);
        LineTo(hdc, clip_l, sy);
        SelectObject(hdc, grid_major);

        char lab[48];
        fmt_tick(lab, sizeof lab, y, y_step);
        SIZE sz;
        GetTextExtentPoint32A(hdc, lab, (int)strlen(lab), &sz);
        draw_string(hdc, clip_l - 10 - sz.cx, sy - sz.cy / 2, lab);
    }

    /* Primary axes through origin when visible */
    SelectObject(hdc, axis_pen);
    if (g_ps.y0 <= 0.0 && g_ps.y1 >= 0.0) {
        int ax, ay;
        world_to_screen(&g_ps, client, g_ps.x0, 0.0, &ax, &ay);
        (void)ax;
        if (ay >= clip_t && ay <= clip_b) {
            MoveToEx(hdc, clip_l, ay, NULL);
            LineTo(hdc, clip_r, ay);
        }
    }
    if (g_ps.x0 <= 0.0 && g_ps.x1 >= 0.0) {
        int ax, ay;
        world_to_screen(&g_ps, client, 0.0, g_ps.y0, &ax, &ay);
        (void)ay;
        if (ax >= clip_l && ax <= clip_r) {
            MoveToEx(hdc, ax, clip_t, NULL);
            LineTo(hdc, ax, clip_b);
        }
    }

    SelectObject(hdc, old_pen);
    SelectObject(hdc, old_font);
    DeleteObject(grid_minor);
    DeleteObject(grid_major);
    DeleteObject(axis_pen);
    DeleteObject(tick_pen);
}

static void draw_axis_titles(HDC hdc, RECT client, HFONT font_label,
                             HFONT font_y)
{
    int w = client.right - client.left;
    int h = client.bottom - client.top;
    int clip_l = TERMINALC_PLOT_MARGIN_L;
    int clip_r = w - TERMINALC_PLOT_MARGIN_R;
    int clip_t = TERMINALC_PLOT_MARGIN_T;
    int clip_b = h - TERMINALC_PLOT_MARGIN_B;

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(150, 190, 230));

    /* X axis label centered under ticks */
    HGDIOBJ old = SelectObject(hdc, font_label);
    const char *xlabel = "x";
    SIZE sz;
    GetTextExtentPoint32A(hdc, xlabel, 1, &sz);
    draw_string(hdc, (clip_l + clip_r) / 2 - sz.cx / 2, h - 20, xlabel);

    /* Y axis label rotated */
    SelectObject(hdc, font_y);
    const char *ylabel = "y";
    GetTextExtentPoint32A(hdc, ylabel, 1, &sz);
    /* With 90° escapement, TextOut position is the baseline start. */
    draw_string(hdc, 14, (clip_t + clip_b) / 2 + sz.cx / 2, ylabel);

    SelectObject(hdc, old);
}

static void draw_legend(HDC hdc, RECT client, HFONT font_ui)
{
    int w = client.right - client.left;
    int clip_r = w - TERMINALC_PLOT_MARGIN_R;
    int clip_t = TERMINALC_PLOT_MARGIN_T;

    char line[200];
    /* Keep the legend bounded even when the expression is near the parser's
       maximum line length.  The UI box is intentionally compact. */
    snprintf(line, sizeof line, "y = %.190s", g_ps.expr);

    HGDIOBJ old_font = SelectObject(hdc, font_ui);
    SIZE sz;
    GetTextExtentPoint32A(hdc, line, (int)strlen(line), &sz);

    int pad = 10;
    int swatch = 28;
    int box_w = swatch + pad + sz.cx + pad + 8;
    int box_h = sz.cy + pad * 2;
    if (box_w > 360)
        box_w = 360;
    int bx = clip_r - box_w - 8;
    int by = clip_t + 8;
    if (bx < TERMINALC_PLOT_MARGIN_L + 8)
        bx = TERMINALC_PLOT_MARGIN_L + 8;

    HBRUSH fill = CreateSolidBrush(RGB(28, 28, 36));
    HPEN border = CreatePen(PS_SOLID, 1, RGB(80, 80, 100));
    HGDIOBJ old_brush = SelectObject(hdc, fill);
    HGDIOBJ old_pen = SelectObject(hdc, border);
    RoundRect(hdc, bx, by, bx + box_w, by + box_h, 8, 8);

    /* Color swatch line */
    HPEN curve = CreatePen(PS_SOLID, 2, RGB(80, 220, 120));
    SelectObject(hdc, curve);
    int midy = by + box_h / 2;
    MoveToEx(hdc, bx + pad, midy, NULL);
    LineTo(hdc, bx + pad + swatch - 4, midy);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(200, 230, 200));
    /* Clip long expressions in the legend text area */
    char shown[160];
    if (strlen(line) + 1 > sizeof shown) {
        memcpy(shown, line, sizeof shown - 4);
        shown[sizeof shown - 4] = '.';
        shown[sizeof shown - 3] = '.';
        shown[sizeof shown - 2] = '.';
        shown[sizeof shown - 1] = '\0';
    } else {
        memcpy(shown, line, strlen(line) + 1);
    }
    draw_string(hdc, bx + pad + swatch, midy - sz.cy / 2, shown);

    SelectObject(hdc, old_pen);
    SelectObject(hdc, old_brush);
    SelectObject(hdc, old_font);
    DeleteObject(curve);
    DeleteObject(border);
    DeleteObject(fill);
}

static void paint_plot(HWND hwnd, HDC hdc)
{
    RECT client;
    GetClientRect(hwnd, &client);
    int w = client.right - client.left;
    int h = client.bottom - client.top;
    if (w < 2 || h < 2)
        return;

    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, w, h);
    HGDIOBJ old_bmp = SelectObject(mem, bmp);

    HBRUSH bg = CreateSolidBrush(RGB(18, 18, 22));
    FillRect(mem, &client, bg);
    DeleteObject(bg);

    HFONT font_ui = CreateFontA(
        -13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_SWISS, "Segoe UI");
    HFONT font_title = CreateFontA(
        -16, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_SWISS, "Segoe UI");
    HFONT font_label = CreateFontA(
        -14, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_SWISS, "Segoe UI");
    /* 90° counterclockwise for vertical Y label */
    HFONT font_y = CreateFontA(
        -14, 0, 900, 900, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_SWISS, "Segoe UI");

    /* Frame */
    HPEN frame = CreatePen(PS_SOLID, 1, RGB(70, 70, 85));
    HGDIOBJ old_pen = SelectObject(mem, frame);
    HGDIOBJ old_brush = SelectObject(mem, GetStockObject(NULL_BRUSH));
    Rectangle(mem, TERMINALC_PLOT_MARGIN_L - 1, TERMINALC_PLOT_MARGIN_T - 1,
              w - TERMINALC_PLOT_MARGIN_R + 1, h - TERMINALC_PLOT_MARGIN_B + 1);

    draw_grid_and_ticks(mem, client, font_ui);

    /* High-resolution curve (samples scale with window width). */
    int pw, ph;
    plot_size(client, &pw, &ph);
    (void)ph;
    int ns = sample_count_for(pw);

    HPEN curve = CreatePen(PS_SOLID, 2, RGB(80, 220, 120));
    SelectObject(mem, curve);

    /* Clip to plot interior so lines don't bleed into labels. */
    int clip_l = TERMINALC_PLOT_MARGIN_L;
    int clip_t = TERMINALC_PLOT_MARGIN_T;
    int clip_r = w - TERMINALC_PLOT_MARGIN_R;
    int clip_b = h - TERMINALC_PLOT_MARGIN_B;
    HRGN clip = CreateRectRgn(clip_l, clip_t, clip_r, clip_b);
    SelectClipRgn(mem, clip);

    POINT *pts = (POINT *)malloc((size_t)ns * sizeof(POINT));
    int npts = 0;
    if (pts) {
        for (int i = 0; i < ns; i++) {
            double t = (double)i / (double)(ns - 1);
            double x = g_ps.x0 + t * (g_ps.x1 - g_ps.x0);
            double y;
            if (!sample_y(g_ps.calc, g_ps.expr, x, &y)) {
                if (npts >= 2)
                    Polyline(mem, pts, npts);
                npts = 0;
                continue;
            }
            int sx, sy;
            world_to_screen(&g_ps, client, x, y, &sx, &sy);
            pts[npts].x = sx;
            pts[npts].y = sy;
            npts++;
            /* Flush long runs to keep stack-like limits small. */
            if (npts >= ns) {
                Polyline(mem, pts, npts);
                /* Keep last point to continue the stroke. */
                pts[0] = pts[npts - 1];
                npts = 1;
            }
        }
        if (npts >= 2)
            Polyline(mem, pts, npts);
        free(pts);
    }

    SelectClipRgn(mem, NULL);
    DeleteObject(clip);

    /* Title */
    HGDIOBJ old_font = SelectObject(mem, font_title);
    SetBkMode(mem, TRANSPARENT);
    SetTextColor(mem, RGB(120, 200, 255));
    char title[256];
    snprintf(title, sizeof title, "TerminalC  |  interactive plot");
    draw_string(mem, TERMINALC_PLOT_MARGIN_L, 10, title);

    draw_axis_titles(mem, client, font_label, font_y);
    draw_legend(mem, client, font_ui);
    draw_hover(mem, client, font_ui);

    /* Footer: live range + sample count + controls hint */
    SelectObject(mem, font_ui);
    SetTextColor(mem, RGB(130, 130, 148));
    char footer[384];
    if (g_ps.hover_on) {
        char xb[48], yb[48];
        fmt_hover_num(xb, sizeof xb, g_ps.hover_x, g_ps.x1 - g_ps.x0);
        fmt_hover_num(yb, sizeof yb, g_ps.hover_y, g_ps.y1 - g_ps.y0);
        snprintf(footer, sizeof footer,
                 "point  x = %s   y = %s     |  %d samples  |  "
                 "hover curve for readout | wheel zoom | Esc close",
                 xb, yb, ns);
    } else {
        snprintf(footer, sizeof footer,
                 "x: [%.8g, %.8g]   y: [%.8g, %.8g]   %d samples   "
                 "hover curve | wheel zoom | drag pan | +/- | 0 | y | Esc",
                 g_ps.x0, g_ps.x1, g_ps.y0, g_ps.y1, ns);
    }
    draw_string(mem, 8, h - 18, footer);

    BitBlt(hdc, 0, 0, w, h, mem, 0, 0, SRCCOPY);

    SelectObject(mem, old_font);
    SelectObject(mem, old_pen);
    SelectObject(mem, old_brush);
    DeleteObject(curve);
    DeleteObject(frame);
    DeleteObject(font_ui);
    DeleteObject(font_title);
    DeleteObject(font_label);
    DeleteObject(font_y);
    SelectObject(mem, old_bmp);
    DeleteObject(bmp);
    DeleteDC(mem);
}

static LRESULT CALLBACK plot_wnd_proc(HWND hwnd, UINT msg, WPARAM wParam,
                                      LPARAM lParam)
{
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        paint_plot(hwnd, hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_MOUSEWHEEL: {
        RECT client;
        GetClientRect(hwnd, &client);
        POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ScreenToClient(hwnd, &pt);
        double wx, wy;
        screen_to_world(&g_ps, client, pt.x, pt.y, &wx, &wy);
        short delta = GET_WHEEL_DELTA_WPARAM(wParam);
        double factor = (delta > 0) ? 1.2 : (1.0 / 1.2);
        zoom_about(&g_ps, wx, wy, factor);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }
    case WM_LBUTTONDOWN:
        g_ps.dragging = true;
        g_ps.last_mx = GET_X_LPARAM(lParam);
        g_ps.last_my = GET_Y_LPARAM(lParam);
        SetCapture(hwnd);
        return 0;
    case WM_LBUTTONUP:
        g_ps.dragging = false;
        ReleaseCapture();
        return 0;
    case WM_MOUSEMOVE: {
        int mx = GET_X_LPARAM(lParam);
        int my = GET_Y_LPARAM(lParam);
        /* Track mouse leave so the tooltip clears. */
        TRACKMOUSEEVENT tme;
        memset(&tme, 0, sizeof tme);
        tme.cbSize = sizeof tme;
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = hwnd;
        TrackMouseEvent(&tme);

        if (g_ps.dragging) {
            RECT client;
            GetClientRect(hwnd, &client);
            int pw, ph;
            plot_size(client, &pw, &ph);
            double dx = (double)(g_ps.last_mx - mx) / pw * (g_ps.x1 - g_ps.x0);
            double dy = (double)(my - g_ps.last_my) / ph * (g_ps.y1 - g_ps.y0);
            g_ps.x0 += dx;
            g_ps.x1 += dx;
            g_ps.y0 += dy;
            g_ps.y1 += dy;
            g_ps.last_mx = mx;
            g_ps.last_my = my;
            g_ps.hover_on = false;
        } else {
            bool was = g_ps.hover_on;
            int osx = g_ps.hover_sx, osy = g_ps.hover_sy;
            update_hover(hwnd, mx, my);
            /* Repaint only when the snap target or hover enter/leave changes. */
            if (was != g_ps.hover_on || g_ps.hover_sx != osx ||
                g_ps.hover_sy != osy) {
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }
    case WM_MOUSELEAVE:
        if (g_ps.hover_on) {
            g_ps.hover_on = false;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE || wParam == 'Q') {
            DestroyWindow(hwnd);
            return 0;
        }
        if (wParam == '0') {
            g_ps.x0 = g_ps.ix0;
            g_ps.x1 = g_ps.ix1;
            g_ps.y0 = g_ps.iy0;
            g_ps.y1 = g_ps.iy1;
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        if (wParam == 'Y') {
            auto_y_fit(&g_ps);
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        if (wParam == VK_OEM_PLUS || wParam == VK_ADD) {
            double cx = 0.5 * (g_ps.x0 + g_ps.x1);
            double cy = 0.5 * (g_ps.y0 + g_ps.y1);
            zoom_about(&g_ps, cx, cy, 1.25);
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        if (wParam == VK_OEM_MINUS || wParam == VK_SUBTRACT) {
            double cx = 0.5 * (g_ps.x0 + g_ps.x1);
            double cy = 0.5 * (g_ps.y0 + g_ps.y1);
            zoom_about(&g_ps, cx, cy, 1.0 / 1.25);
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        if (wParam == 'A') {
            printf("%sPlot view:%s x=[%.8g .. %.8g]  y=[%.8g .. %.8g]  y=%s\n",
                   C_B_CYN, C_RST, g_ps.x0, g_ps.x1, g_ps.y0, g_ps.y1,
                   g_ps.expr);
            fflush(stdout);
            return 0;
        }
        return 0;
    case WM_CHAR:
        if (wParam == '+' || wParam == '=') {
            double cx = 0.5 * (g_ps.x0 + g_ps.x1);
            double cy = 0.5 * (g_ps.y0 + g_ps.y1);
            zoom_about(&g_ps, cx, cy, 1.25);
            InvalidateRect(hwnd, NULL, FALSE);
        } else if (wParam == '-' || wParam == '_') {
            double cx = 0.5 * (g_ps.x0 + g_ps.x1);
            double cy = 0.5 * (g_ps.y0 + g_ps.y1);
            zoom_about(&g_ps, cx, cy, 1.0 / 1.25);
            InvalidateRect(hwnd, NULL, FALSE);
        } else if (wParam == 'q' || wParam == 'Q' || wParam == 27) {
            DestroyWindow(hwnd);
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcA(hwnd, msg, wParam, lParam);
    }
}

bool terminalc_plot_gui(TerminalCCalc *c, const char *expr, double xmin, double xmax)
{
    if (!c || !expr || !(xmax > xmin) || !isfinite(xmin) || !isfinite(xmax))
        return false;

    memset(&g_ps, 0, sizeof g_ps);
    g_ps.calc = c;
    size_t n = strlen(expr);
    if (n >= sizeof g_ps.expr)
        return false;
    memcpy(g_ps.expr, expr, n + 1);
    g_ps.x0 = g_ps.ix0 = xmin;
    g_ps.x1 = g_ps.ix1 = xmax;
    auto_y_fit(&g_ps);
    g_ps.iy0 = g_ps.y0;
    g_ps.iy1 = g_ps.y1;

    HINSTANCE inst = GetModuleHandleA(NULL);
    WNDCLASSEXA wc;
    memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc;
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = plot_wnd_proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(NULL, IDC_CROSS);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = TERMINALC_PLOT_CLASS;
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hIconSm = LoadIcon(NULL, IDI_APPLICATION);

    if (!RegisterClassExA(&wc)) {
        if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            return false;
    }

    char title[280];
    snprintf(title, sizeof title, "TerminalC plot - y = %s", expr);

    HWND hwnd = CreateWindowExA(
        0, TERMINALC_PLOT_CLASS, title, WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 1024, 680, NULL, NULL, inst, NULL);
    if (!hwnd)
        return false;

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    SetForegroundWindow(hwnd);

    printf("%sInteractive plot window open.%s  "
           "Hover curve for (x,y) | wheel zoom | drag pan | Esc close\n",
           C_B_CYN, C_RST);
    fflush(stdout);

    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return true;
}
