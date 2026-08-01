#ifndef TERMINALC_COLOR_H
#define TERMINALC_COLOR_H

#include <stdbool.h>
#include <stdio.h>

/* Lightweight ANSI styling. Zero cost when disabled (empty strings). */

typedef struct {
    bool enabled;
    const char *reset;
    const char *bold;
    const char *dim;
    const char *red;
    const char *green;
    const char *yellow;
    const char *blue;
    const char *magenta;
    const char *cyan;
    const char *white;
    const char *bold_cyan;
    const char *bold_yellow;
    const char *bold_green;
    const char *bold_magenta;
    const char *bold_red;
} TerminalCColor;

void terminalc_color_init(TerminalCColor *c, bool force_off);
void terminalc_color_enable_windows_vt(void);

/* Global session palette (set once at startup / when color toggled). */
extern TerminalCColor g_terminalc_color;

#define C_RST   (g_terminalc_color.reset)
#define C_BOLD  (g_terminalc_color.bold)
#define C_DIM   (g_terminalc_color.dim)
#define C_RED   (g_terminalc_color.red)
#define C_GRN   (g_terminalc_color.green)
#define C_YEL   (g_terminalc_color.yellow)
#define C_BLU   (g_terminalc_color.blue)
#define C_MAG   (g_terminalc_color.magenta)
#define C_CYN   (g_terminalc_color.cyan)
#define C_B_CYN (g_terminalc_color.bold_cyan)
#define C_B_YEL (g_terminalc_color.bold_yellow)
#define C_B_GRN (g_terminalc_color.bold_green)
#define C_B_MAG (g_terminalc_color.bold_magenta)
#define C_B_RED (g_terminalc_color.bold_red)

#endif /* TERMINALC_COLOR_H */
