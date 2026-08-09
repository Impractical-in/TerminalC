#include "color.h"

#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#define terminalc_isatty(fd) _isatty(fd)
#define terminalc_fileno(f)  _fileno(f)
#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif
#else
#include <unistd.h>
#define terminalc_isatty(fd) isatty(fd)
#define terminalc_fileno(f)  fileno(f)
#endif

TerminalCColor g_terminalc_color;

static const char *empty = "";

static void terminalc_color_set_ansi(TerminalCColor *c)
{
    c->enabled = true;
    c->reset = "\033[0m";
    c->bold = "\033[1m";
    c->dim = "\033[2m";
    c->red = "\033[31m";
    c->green = "\033[32m";
    c->yellow = "\033[33m";
    c->blue = "\033[34m";
    c->magenta = "\033[35m";
    c->cyan = "\033[36m";
    c->white = "\033[37m";
    c->bold_cyan = "\033[1;36m";
    c->bold_yellow = "\033[1;33m";
    c->bold_green = "\033[1;32m";
    c->bold_magenta = "\033[1;35m";
    c->bold_red = "\033[1;31m";
}

void terminalc_color_enable_windows_vt(void)
{
#ifdef _WIN32
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (h != INVALID_HANDLE_VALUE && GetConsoleMode(h, &mode)) {
        SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }
#endif
}

void terminalc_color_init(TerminalCColor *c, bool force_off)
{
    bool on = !force_off;

    if (on) {
        const char *no_color = getenv("NO_COLOR");
        if (no_color && no_color[0] != '\0')
            on = false;
    }
    if (on) {
        const char *term = getenv("TERM");
        if (term && strcmp(term, "dumb") == 0)
            on = false;
    }
    if (on && !terminalc_isatty(terminalc_fileno(stdout)))
        on = false;

    c->enabled = on;
    if (!on) {
        c->reset = empty;
        c->bold = empty;
        c->dim = empty;
        c->red = empty;
        c->green = empty;
        c->yellow = empty;
        c->blue = empty;
        c->magenta = empty;
        c->cyan = empty;
        c->white = empty;
        c->bold_cyan = empty;
        c->bold_yellow = empty;
        c->bold_green = empty;
        c->bold_magenta = empty;
        c->bold_red = empty;
        return;
    }

    terminalc_color_set_ansi(c);
}

void terminalc_color_force_on(TerminalCColor *c)
{
    terminalc_color_set_ansi(c);
}
