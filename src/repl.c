#include "terminalc.h"
#include "color.h"
#include "parse_internal.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <conio.h>
#include <io.h>
#define TERMINALC_REPL_ISATTY(fd) (_isatty(fd) != 0)
#define TERMINALC_REPL_FILENO(stream) _fileno(stream)
#else
#include <termios.h>
#include <unistd.h>
#define TERMINALC_REPL_ISATTY(fd) (isatty(fd) != 0)
#define TERMINALC_REPL_FILENO(stream) fileno(stream)
#endif

enum {
    TERMINALC_FIB_LIST_MAX = 10000,
    TERMINALC_FIB_INDEX_MAX = 10000000,
    TERMINALC_HISTORY_MAX = 100
};

typedef struct {
    char entries[TERMINALC_HISTORY_MAX][TERMINALC_LINE_MAX];
    size_t count;
    bool persistent;
} TerminalCHistory;

static bool history_is_interactive(void)
{
    return TERMINALC_REPL_ISATTY(TERMINALC_REPL_FILENO(stdin)) &&
           TERMINALC_REPL_ISATTY(TERMINALC_REPL_FILENO(stdout));
}

static const char *history_file_path(void)
{
    const char *override = getenv("TERMINALC_HISTORY_FILE");
    if (override)
        return override;

    static char path[TERMINALC_LINE_MAX];
#ifdef _WIN32
    const char *home = getenv("USERPROFILE");
    if (!home)
        home = getenv("HOMEPATH");
#else
    const char *home = getenv("HOME");
#endif
    if (!home || !home[0])
        return NULL;
    snprintf(path, sizeof path, "%s/.terminalc_history", home);
    return path;
}

static void history_add(TerminalCHistory *history, const char *line)
{
    if (!line || !line[0])
        return;
    if (history->count > 0 &&
        strcmp(history->entries[history->count - 1], line) == 0)
        return;
    if (history->count == TERMINALC_HISTORY_MAX) {
        memmove(history->entries[0], history->entries[1],
                (TERMINALC_HISTORY_MAX - 1) * sizeof history->entries[0]);
        history->count--;
    }
    snprintf(history->entries[history->count], TERMINALC_LINE_MAX, "%s", line);
    history->count++;
}

static void history_load(TerminalCHistory *history)
{
    if (!history->persistent)
        return;
    const char *path = history_file_path();
    if (!path || !path[0])
        return;
    FILE *fp = fopen(path, "r");
    if (!fp)
        return;
    char line[TERMINALC_LINE_MAX];
    while (fgets(line, sizeof line, fp)) {
        line[strcspn(line, "\r\n")] = '\0';
        history_add(history, line);
    }
    fclose(fp);
}

static void history_save(const TerminalCHistory *history)
{
    if (!history->persistent)
        return;
    const char *path = history_file_path();
    if (!path || !path[0])
        return;
    FILE *fp = fopen(path, "w");
    if (!fp)
        return;
    for (size_t i = 0; i < history->count; i++)
        fprintf(fp, "%s\n", history->entries[i]);
    fclose(fp);
}

static void history_print(const TerminalCHistory *history)
{
    if (history->count == 0) {
        puts("(history is empty)");
        return;
    }
    for (size_t i = 0; i < history->count; i++)
        printf("%4zu  %s\n", i + 1, history->entries[i]);
}

static bool history_expand(const TerminalCHistory *history, char *line, size_t size)
{
    if (line[0] != '!')
        return true;
    size_t index = 0;
    if (strcmp(line, "!!") == 0) {
        if (history->count == 0) {
            fprintf(stderr, "%sHistory is empty.%s\n", C_B_RED, C_RST);
            return false;
        }
        index = history->count;
    } else {
        char *end = NULL;
        unsigned long requested = strtoul(line + 1, &end, 10);
        if (end == line + 1 || *end != '\0' || requested == 0 ||
            requested > history->count) {
            fprintf(stderr, "%sUnknown history entry '%s'.%s\n", C_B_RED, line,
                    C_RST);
            return false;
        }
        index = (size_t)requested;
    }
    snprintf(line, size, "%s", history->entries[index - 1]);
    printf("%s%s%s\n", C_DIM, line, C_RST);
    return true;
}

static bool handle_history_command(TerminalCHistory *history, char *line)
{
    if (strcmp(line, "history") == 0) {
        history_print(history);
        return true;
    }
    if (strcmp(line, "history clear") == 0) {
        history->count = 0;
        history_save(history);
        puts("History cleared.");
        return true;
    }
    return false;
}

enum {
    TERMINALC_REPL_KEY_EOF = -1,
    TERMINALC_REPL_KEY_NONE = -2,
    TERMINALC_REPL_KEY_ENTER = -3,
    TERMINALC_REPL_KEY_BACKSPACE = -4,
    TERMINALC_REPL_KEY_CANCEL = -5,
    TERMINALC_REPL_KEY_UP = -6,
    TERMINALC_REPL_KEY_DOWN = -7,
    TERMINALC_REPL_KEY_LEFT = -8,
    TERMINALC_REPL_KEY_RIGHT = -9,
    TERMINALC_REPL_KEY_HOME = -10,
    TERMINALC_REPL_KEY_END = -11
};

static int repl_read_byte(void)
{
#ifdef _WIN32
    return _getch();
#else
    unsigned char byte = 0;
    ssize_t n = read(TERMINALC_REPL_FILENO(stdin), &byte, 1);
    return n == 1 ? (int)byte : TERMINALC_REPL_KEY_EOF;
#endif
}

static int repl_read_key(void)
{
    int ch = repl_read_byte();
    if (ch == TERMINALC_REPL_KEY_EOF)
        return TERMINALC_REPL_KEY_EOF;
#ifdef _WIN32
    if (ch == 0 || ch == 224) {
        switch (repl_read_byte()) {
        case 72: return TERMINALC_REPL_KEY_UP;
        case 80: return TERMINALC_REPL_KEY_DOWN;
        case 75: return TERMINALC_REPL_KEY_LEFT;
        case 77: return TERMINALC_REPL_KEY_RIGHT;
        case 71: return TERMINALC_REPL_KEY_HOME;
        case 79: return TERMINALC_REPL_KEY_END;
        default: return TERMINALC_REPL_KEY_NONE;
        }
    }
#else
    if (ch == 27) {
        int first = repl_read_byte();
        if (first == '[' || first == 'O') {
            switch (repl_read_byte()) {
            case 'A': return TERMINALC_REPL_KEY_UP;
            case 'B': return TERMINALC_REPL_KEY_DOWN;
            case 'C': return TERMINALC_REPL_KEY_RIGHT;
            case 'D': return TERMINALC_REPL_KEY_LEFT;
            case 'H': return TERMINALC_REPL_KEY_HOME;
            case 'F': return TERMINALC_REPL_KEY_END;
            default: return TERMINALC_REPL_KEY_NONE;
            }
        }
        return TERMINALC_REPL_KEY_NONE;
    }
#endif
    if (ch == '\r' || ch == '\n')
        return TERMINALC_REPL_KEY_ENTER;
    if (ch == 8 || ch == 127)
        return TERMINALC_REPL_KEY_BACKSPACE;
    if (ch == 3)
        return TERMINALC_REPL_KEY_CANCEL;
    if (ch == 4)
        return TERMINALC_REPL_KEY_EOF;
    return ch >= 32 ? ch : TERMINALC_REPL_KEY_NONE;
}

static void repl_print_prompt(void)
{
    printf("%scalc>%s ", C_B_CYN, C_RST);
}

static void repl_redraw_line(const char *line, size_t length, size_t cursor,
                             size_t *painted)
{
    fputc('\r', stdout);
    repl_print_prompt();
    if (length > 0)
        fwrite(line, 1, length, stdout);
    if (*painted > length) {
        for (size_t i = length; i < *painted; i++)
            fputc(' ', stdout);
        for (size_t i = length; i < *painted; i++)
            fputc('\b', stdout);
    }
    for (size_t i = cursor; i < length; i++)
        fputc('\b', stdout);
    fflush(stdout);
    *painted = length;
}

#ifndef _WIN32
static bool repl_begin_raw_input(struct termios *saved)
{
    int fd = TERMINALC_REPL_FILENO(stdin);
    if (tcgetattr(fd, saved) != 0)
        return false;
    struct termios raw = *saved;
    raw.c_lflag &= (tcflag_t)~(ICANON | ECHO | ISIG);
    raw.c_iflag &= (tcflag_t)~(IXON | ICRNL);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    return tcsetattr(fd, TCSANOW, &raw) == 0;
}

static void repl_end_raw_input(const struct termios *saved)
{
    tcsetattr(TERMINALC_REPL_FILENO(stdin), TCSANOW, saved);
}
#endif

static bool repl_read_line(TerminalCHistory *history, char *line, size_t size,
                           bool *cancelled)
{
    *cancelled = false;
    if (!history->persistent)
        return fgets(line, size, stdin) != NULL;

#ifndef _WIN32
    struct termios saved;
    if (!repl_begin_raw_input(&saved))
        return fgets(line, size, stdin) != NULL;
#endif

    char draft[TERMINALC_LINE_MAX];
    size_t length = 0;
    size_t cursor = 0;
    size_t painted = 0;
    size_t history_cursor = history->count;
    bool draft_saved = false;
    line[0] = '\0';

    while (1) {
        int key = repl_read_key();
        if (key == TERMINALC_REPL_KEY_ENTER) {
#ifndef _WIN32
            repl_end_raw_input(&saved);
#endif
            repl_redraw_line(line, length, length, &painted);
            putchar('\n');
            line[length] = '\0';
            return true;
        }
        if (key == TERMINALC_REPL_KEY_EOF) {
#ifndef _WIN32
            repl_end_raw_input(&saved);
#endif
            if (length == 0) {
                putchar('\n');
                return false;
            }
            repl_redraw_line(line, length, length, &painted);
            putchar('\n');
            line[length] = '\0';
            return true;
        }
        if (key == TERMINALC_REPL_KEY_CANCEL) {
#ifndef _WIN32
            repl_end_raw_input(&saved);
#endif
            repl_redraw_line("", 0, 0, &painted);
            puts("^C");
            line[0] = '\0';
            *cancelled = true;
            return true;
        }

        if (key == TERMINALC_REPL_KEY_UP) {
            if (history_cursor == history->count) {
                snprintf(draft, sizeof draft, "%s", line);
                draft_saved = true;
            }
            if (history_cursor > 0) {
                history_cursor--;
                snprintf(line, size, "%s", history->entries[history_cursor]);
                length = strlen(line);
                cursor = length;
                repl_redraw_line(line, length, cursor, &painted);
            }
            continue;
        }
        if (key == TERMINALC_REPL_KEY_DOWN) {
            if (history_cursor < history->count) {
                history_cursor++;
                if (history_cursor == history->count) {
                    snprintf(line, size, "%s", draft_saved ? draft : "");
                } else {
                    snprintf(line, size, "%s", history->entries[history_cursor]);
                }
                length = strlen(line);
                cursor = length;
                repl_redraw_line(line, length, cursor, &painted);
            }
            continue;
        }
        if (key == TERMINALC_REPL_KEY_LEFT) {
            if (cursor > 0) {
                cursor--;
                repl_redraw_line(line, length, cursor, &painted);
            }
            continue;
        }
        if (key == TERMINALC_REPL_KEY_RIGHT) {
            if (cursor < length) {
                cursor++;
                repl_redraw_line(line, length, cursor, &painted);
            }
            continue;
        }
        if (key == TERMINALC_REPL_KEY_HOME) {
            cursor = 0;
            repl_redraw_line(line, length, cursor, &painted);
            continue;
        }
        if (key == TERMINALC_REPL_KEY_END) {
            cursor = length;
            repl_redraw_line(line, length, cursor, &painted);
            continue;
        }
        if (key == TERMINALC_REPL_KEY_BACKSPACE) {
            if (cursor > 0) {
                memmove(line + cursor - 1, line + cursor, length - cursor + 1);
                cursor--;
                length--;
                history_cursor = history->count;
                draft_saved = false;
                repl_redraw_line(line, length, cursor, &painted);
            }
            continue;
        }
        if (key >= 32 && key < 127 && length + 1 < size) {
            memmove(line + cursor + 1, line + cursor, length - cursor + 1);
            line[cursor] = (char)key;
            cursor++;
            length++;
            history_cursor = history->count;
            draft_saved = false;
            repl_redraw_line(line, length, cursor, &painted);
        }
    }
}

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

static bool line_equals_ci(const char *a, const char *b)
{
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return false;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static void strip_script_comment(char *s)
{
    for (size_t i = 0; s[i]; i++) {
        if (s[i] == '#' || (s[i] == '/' && s[i + 1] == '/')) {
            s[i] = '\0';
            break;
        }
    }
}

static void mark_script_error(TerminalCCalc *c, const char *message)
{
    if (c->script_mode && !c->error) {
        c->error = message;
        c->error_pos = 0;
    }
}

static bool is_protected(const char *name)
{
    return strcmp(name, "pi") == 0 || strcmp(name, "e") == 0 ||
           strcmp(name, "phi") == 0 || strcmp(name, "sqrt2") == 0 ||
           strcmp(name, "ln2") == 0 || strcmp(name, "ans") == 0 ||
           strcmp(name, "x") == 0 || strcmp(name, "_") == 0 ||
           strcmp(name, "true") == 0 || strcmp(name, "false") == 0;
}

static void fib_print(unsigned long n)
{
    mpz_t a, b, t;
    mpz_inits(a, b, t, NULL);
    mpz_set_ui(b, 1);
    for (unsigned long i = 0; i < n; i++) {
        if (i)
            fputs(", ", stdout);
        char *text = mpz_get_str(NULL, 10, a);
        if (text) {
            printf("%s%s%s", C_B_GRN, text, C_RST);
            free(text);
        }
        mpz_add(t, a, b);
        mpz_set(a, b);
        mpz_set(b, t);
    }
    puts("");
    mpz_clears(a, b, t, NULL);
}

static void fib_pair(mpz_t f, mpz_t next, unsigned long n)
{
    if (n == 0) {
        mpz_set_ui(f, 0);
        mpz_set_ui(next, 1);
        return;
    }
    mpz_t a, b, c, d, t;
    mpz_inits(a, b, c, d, t, NULL);
    fib_pair(a, b, n / 2);
    mpz_mul_ui(t, b, 2);
    mpz_sub(t, t, a);
    mpz_mul(c, a, t);
    mpz_mul(d, a, a);
    mpz_mul(t, b, b);
    mpz_add(d, d, t);
    if (n % 2 == 0) {
        mpz_set(f, c);
        mpz_set(next, d);
    } else {
        mpz_set(f, d);
        mpz_add(next, c, d);
    }
    mpz_clears(a, b, c, d, t, NULL);
}

static void fibn_print(unsigned long n)
{
    mpz_t f, next;
    mpz_inits(f, next, NULL);
    fib_pair(f, next, n);
    char *text = mpz_get_str(NULL, 10, f);
    if (text) {
        printf("%s%s%s\n", C_B_GRN, text, C_RST);
        free(text);
    }
    mpz_clears(f, next, NULL);
}

static void print_repl_help(void)
{
    printf("%s", C_B_CYN);
    puts("TerminalC interactive commands");
    printf("%s", C_RST);
    puts("  help [topic]              General help, or help hex / help square ...");
    puts("  help topics               List documented topics by category");
    puts("  help functions            List every callable function and signature");
    puts("  quit, exit                Leave the REPL");
    puts("  precision [N]             Show or set decimal digits (2-100000)");
    puts("  digits N                  Alias for precision N");
    puts("  angle radians|degrees     Trigonometric mode");
    puts("  vars                      List user variables");
    puts("  clear_vars                Clear user variables");
    puts("  history [clear]           Show or clear persistent command history");
    puts("  !! / !N                   Replay the last or numbered history entry");
    puts("  Up/Down arrows            Browse history; Left/Right edit the line");
    puts("  Ctrl-C                    Cancel input; press twice at an empty prompt to exit");
    puts("  color on|off              Toggle ANSI color output");
    puts("  advanced on|off           Time / memory / output-size stats");
    puts("");
    puts("Expressions: + - * / % ^  parentheses  a = expr  ans  _");
    puts("Booleans:    true false  == != < <= > >=  and or not  !  && ||");
    puts("             if(cond, a, b)");
    puts("Constants:   pi e phi sqrt2 ln2");
    puts("Literals:    0xFF  0b1010  0o77   (hex / binary / octal integers)");
    puts("Functions:   sin cos tan sind cosd tand asin acos atan atan2");
    puts("             sinh cosh tanh asinh acosh atanh hypot sqrt nthroot ln log exp");
    puts("             log2 log1p expm1 cbrt pow ramanujan_pi min max clamp");
    puts("             abs floor ceil fact gcd lcm ncr choose");
    puts("Statistics:  mean variance stddev norm sigmoid relu sign");
    puts("Bases:       hex(n) bin(n) oct(n) base(n,b) | tobase(n,b)  (b=2..36)");
    puts("Sequences:   fib(n)  fibn(n)");
    puts("Matrices:    [1,2;3,4]  eye(n) zeros(...) ones(...) mat(...)");
    puts("             det inv transpose linsolve dot shape");
    puts("Solve/calc:  solve(expr,guess|lo,hi)  diff(expr,x0)  integral(expr,a,b)");
    puts("Units:       5*km  to(qty, unit)  mag(qty)   (m km kg mg ha acre ...)");
    puts("Mensuration: square.area(s)  circle.area(r)  sphere.volume(r) ...");
    puts("Series:      ap_term/ap_sum/ap_list  gp_*  hp_*  sumi sumsq sumcu");
    puts("             sumrange  sum(expr,lo,hi)  isprime primes primes_to");
    puts("");
    puts("Algebra:");
    puts("  solve_quadratic(a, b, c)");
    puts("  p = poly([a, b, c, ...])   exact rational coefficients");
    puts("  degree(p)  evaluate(p, x)  derivative(p)  roots(p)");
    puts("  plot(expr, x=lo..hi)      native window on Windows/macOS; ASCII fallback");
    puts("  plot_save(exprs, x=lo..hi, file.svg)  export one or more curves");
    puts("Type: help topics   or   help square / help matrix / help units");
}

static bool assign_var(TerminalCCalc *c, const char *name, const TerminalCValue *v)
{
    if (is_protected(name)) {
        fprintf(stderr, "%sError: protected name '%s'%s\n", C_B_RED, name,
                C_RST);
        mark_script_error(c, "protected variable name");
        return false;
    }
    size_t i;
    for (i = 0; i < c->var_count; i++) {
        if (strcmp(c->vars[i].name, name) == 0)
            break;
    }
    if (i == c->var_count) {
        if (c->var_count == TERMINALC_VAR_MAX) {
            fprintf(stderr, "%sVariable limit reached.%s\n", C_B_RED, C_RST);
            mark_script_error(c, "variable limit reached");
            return false;
        }
        size_t k = 0;
        for (; k + 1 < TERMINALC_NAME_MAX && name[k]; k++)
            c->vars[i].name[k] = name[k];
        c->vars[i].name[k] = '\0';
        terminalc_value_init(&c->vars[i].value, c->bits);
        c->var_count++;
    }
    terminalc_value_copy(&c->vars[i].value, v);
    return true;
}

static bool handle_line(TerminalCCalc *c, char *line)
{
    trim_inplace(line);
    if (!line[0])
        return true;

    if (strcmp(line, "quit") == 0 || strcmp(line, "exit") == 0 ||
        strcmp(line, ":q") == 0)
        return false;

    if (line_equals_ci(line, "hi")) {
        puts("Hi there! Welcome to TerminalC.");
        return true;
    }
    if (line_equals_ci(line, "bye")) {
        puts("Bye! Thanks for using TerminalC.");
        return false;
    }

    if (strcmp(line, "help") == 0 || strcmp(line, "?") == 0 ||
        strcmp(line, ":help") == 0) {
        print_repl_help();
        printf("\n%sTip:%s help topics  |  help hex  |  help plot  |  help square\n",
               C_DIM, C_RST);
        return true;
    }

    /* help <topic>  /  ? <topic>  /  :help <topic> */
    {
        const char *tp = NULL;
        if (strncmp(line, "help ", 5) == 0)
            tp = line + 5;
        else if (strncmp(line, "? ", 2) == 0)
            tp = line + 2;
        else if (strncmp(line, ":help ", 6) == 0)
            tp = line + 6;
        if (tp) {
            while (isspace((unsigned char)*tp))
                tp++;
            if (*tp) {
                char topic[64];
                size_t n = 0;
                while (tp[n] && !isspace((unsigned char)tp[n]) && n + 1 < sizeof topic)
                    n++;
                memcpy(topic, tp, n);
                topic[n] = '\0';
                if (!terminalc_help_topic(topic))
                    mark_script_error(c, "unknown help topic");
                return true;
            }
        }
    }

    if (strcmp(line, "precision") == 0) {
        printf("%sPrecision:%s %s%d%s decimal digits\n", C_B_CYN, C_RST, C_B_GRN,
               c->digits, C_RST);
        return true;
    }

    int d;
    if (sscanf(line, "precision %d", &d) == 1 ||
        sscanf(line, "digits %d", &d) == 1) {
        if (!terminalc_set_precision(c, d, true))
            c->error = "precision must be between 2 and 100000";
        return true;
    }

    if (strncmp(line, "angle ", 6) == 0) {
        if (strcmp(line + 6, "degrees") == 0) {
            c->degrees = true;
            puts("Angle mode set to degrees.");
        } else if (strcmp(line + 6, "radians") == 0) {
            c->degrees = false;
            puts("Angle mode set to radians.");
        } else {
            fprintf(stderr, "%sAngle mode must be radians or degrees.%s\n",
                    C_B_RED, C_RST);
            c->error = "angle mode must be radians or degrees";
        }
        return true;
    }

    if (strncmp(line, "color ", 6) == 0) {
        if (strcmp(line + 6, "on") == 0) {
            terminalc_color_enable_windows_vt();
            terminalc_color_init(&g_terminalc_color, false);
            c->color = g_terminalc_color.enabled;
            puts("Color enabled (if terminal supports ANSI).");
        } else if (strcmp(line + 6, "off") == 0) {
            terminalc_color_init(&g_terminalc_color, true);
            c->color = false;
            puts("Color disabled.");
        } else {
            fprintf(stderr, "%sUsage: color on|off%s\n", C_B_RED, C_RST);
            c->error = "color expects on or off";
        }
        return true;
    }

    if (strcmp(line, "advanced") == 0) {
        printf("%sAdvanced mode:%s %s%s%s\n", C_B_CYN, C_RST, C_B_GRN,
               c->advanced ? "on" : "off", C_RST);
        return true;
    }
    if (strcmp(line, "advanced on") == 0 || strcmp(line, "mode advanced") == 0) {
        c->advanced = true;
        puts("Advanced mode on - showing time, memory, and output size.");
        return true;
    }
    if (strcmp(line, "advanced off") == 0 || strcmp(line, "mode normal") == 0) {
        c->advanced = false;
        puts("Advanced mode off.");
        return true;
    }

    if (strcmp(line, "vars") == 0) {
        if (c->var_count == 0) {
            puts("(no variables)");
            return true;
        }
        for (size_t i = 0; i < c->var_count; i++) {
            printf("%s%s%s = ", C_B_CYN, c->vars[i].name, C_RST);
            terminalc_print_value(c, &c->vars[i].value);
        }
        return true;
    }

    if (strcmp(line, "clear_vars") == 0) {
        for (size_t i = 0; i < c->var_count; i++)
            terminalc_value_clear(&c->vars[i].value);
        c->var_count = 0;
        puts("Variables cleared.");
        return true;
    }

    unsigned long nfib;
    char trailing;
    if (sscanf(line, "fibn(%lu) %c", &nfib, &trailing) == 1) {
        if (nfib > TERMINALC_FIB_INDEX_MAX) {
            fprintf(stderr, "%sfibn(n): n must be at most 10000000.%s\n",
                    C_B_RED, C_RST);
            mark_script_error(c, "fibn index limit exceeded");
            return true;
        }
        fibn_print(nfib);
        return true;
    }
    if (sscanf(line, "fib(%lu) %c", &nfib, &trailing) == 1) {
        if (nfib > TERMINALC_FIB_LIST_MAX) {
            fprintf(stderr, "%sfib(n): n must be at most 10000.%s\n",
                    C_B_RED, C_RST);
            mark_script_error(c, "fib list limit exceeded");
            return true;
        }
        fib_print(nfib);
        return true;
    }

    /* solve_quadratic(a,b,c) */
    if (strncmp(line, "solve_quadratic(", 16) == 0 ||
        strncmp(line, "quadratic(", 10) == 0) {
        char *open = strchr(line, '(');
        char *close = strrchr(line, ')');
        if (!open || !close || close[1] != '\0') {
            fprintf(stderr, "%sUse solve_quadratic(a, b, c).%s\n", C_B_RED,
                    C_RST);
            mark_script_error(c, "invalid quadratic command");
            return true;
        }
        *close = '\0';
        if (!terminalc_quadratic(c, open + 1))
            terminalc_print_error(c);
        return true;
    }

    /* p = poly([a,b,c]) */
    {
        char *eq = strchr(line, '=');
        if (eq) {
            char *rhs = eq + 1;
            while (isspace((unsigned char)*rhs))
                rhs++;
            if (strncmp(rhs, "poly([", 6) == 0) {
                char name[TERMINALC_NAME_MAX];
                size_t k = (size_t)(eq - line);
                while (k && isspace((unsigned char)line[k - 1]))
                    k--;
                if (k == 0 || k >= TERMINALC_NAME_MAX) {
                    fprintf(stderr, "%sInvalid polynomial name.%s\n", C_B_RED,
                            C_RST);
                    mark_script_error(c, "invalid polynomial name");
                    return true;
                }
                memcpy(name, line, k);
                name[k] = '\0';
                trim_inplace(name);
                char *body = rhs + 6;
                char *br = strrchr(body, ']');
                if (!br) {
                    fprintf(stderr, "%sUse p = poly([a, b, c]).%s\n", C_B_RED,
                            C_RST);
                    mark_script_error(c, "invalid polynomial syntax");
                    return true;
                }
                *br = '\0';
                if (!terminalc_store_poly(c, name, body))
                    terminalc_print_error(c);
                return true;
            }
        }
    }

    if (strncmp(line, "degree(", 7) == 0) {
        char name[TERMINALC_NAME_MAX];
        if (sscanf(line, "degree(%47[^)])", name) == 1) {
            TerminalCPolynomial *p = terminalc_find_poly(c, name);
            if (p) {
                printf("%s%zu%s\n", C_B_GRN, p->n - 1, C_RST);
            } else {
                fprintf(stderr, "%sUnknown polynomial.%s\n", C_B_RED, C_RST);
                mark_script_error(c, "unknown polynomial");
            }
        } else {
            fprintf(stderr, "%sUse degree(p).%s\n", C_B_RED, C_RST);
            mark_script_error(c, "invalid degree syntax");
        }
        return true;
    }

    if (strncmp(line, "evaluate(", 9) == 0) {
        char name[TERMINALC_NAME_MAX];
        char *comma = strchr(line + 9, ',');
        char *end = strrchr(line, ')');
        if (comma && end && sscanf(line + 9, "%47[^,]", name) == 1) {
            TerminalCPolynomial *p = terminalc_find_poly(c, name);
            if (!p) {
                fprintf(stderr, "%sUnknown polynomial.%s\n", C_B_RED, C_RST);
                mark_script_error(c, "unknown polynomial");
                return true;
            }
            TerminalCValue x;
            if (terminalc_eval_n(c, comma + 1, (size_t)(end - comma - 1), &x)) {
                mpfr_t y;
                mpfr_init2(y, c->bits);
                terminalc_poly_eval(c, p, &x, y);
                printf("%s+-- evaluate(%s, .) --------------------------+%s\n",
                       C_B_MAG, name, C_RST);
                printf("%s|%s  result = %s", C_B_MAG, C_RST, C_B_GRN);
                mpfr_printf("%.*RNf", c->digits, y);
                printf("%s\n", C_RST);
                printf("%s+-------------------------------------------+%s\n",
                       C_B_MAG, C_RST);
                mpfr_clear(y);
                terminalc_value_clear(&x);
            } else {
                terminalc_print_error(c);
            }
        } else {
            fprintf(stderr, "%sUse evaluate(p, x).%s\n", C_B_RED, C_RST);
            mark_script_error(c, "invalid evaluate syntax");
        }
        return true;
    }

    if (strncmp(line, "derivative(", 11) == 0) {
        char name[TERMINALC_NAME_MAX];
        if (sscanf(line, "derivative(%47[^)])", name) == 1) {
            char dst[TERMINALC_NAME_MAX + 4];
            snprintf(dst, sizeof dst, "%s'", name);
            /* store as name_d if prime awkward for identifier reuse */
            snprintf(dst, sizeof dst, "%s_d", name);
            if (!terminalc_poly_derivative(c, name, dst))
                terminalc_print_error(c);
            else
                printf("%sDerivative stored as %s%s%s\n", C_DIM, C_B_CYN, dst,
                       C_RST);
        } else {
            fprintf(stderr, "%sUse derivative(p).%s\n", C_B_RED, C_RST);
            mark_script_error(c, "invalid derivative syntax");
        }
        return true;
    }

    if (strncmp(line, "roots(", 6) == 0) {
        char name[TERMINALC_NAME_MAX];
        if (sscanf(line, "roots(%47[^)])", name) == 1) {
            TerminalCPolynomial *p = terminalc_find_poly(c, name);
            if (!p) {
                fprintf(stderr, "%sUnknown polynomial.%s\n", C_B_RED, C_RST);
                mark_script_error(c, "unknown polynomial");
                return true;
            }
            if (p->n == 2 || p->n == 3) {
                char *a = mpq_get_str(NULL, 10, p->coeff[0]);
                char *b = mpq_get_str(NULL, 10, p->coeff[1]);
                char *z = NULL;
                if (p->n == 3) {
                    z = mpq_get_str(NULL, 10, p->coeff[2]);
                } else {
                    z = (char *)malloc(2);
                    if (z) {
                        z[0] = '0';
                        z[1] = '\0';
                    }
                }
                if (a && b && z) {
                    size_t need = strlen(a) + strlen(b) + strlen(z) + 3;
                    char *args = (char *)malloc(need);
                    if (args) {
                        snprintf(args, need, "%s,%s,%s", a, b, z);
                        if (!terminalc_quadratic(c, args))
                            terminalc_print_error(c);
                        free(args);
                    }
                }
                free(a);
                free(b);
                free(z);
            } else {
                fprintf(stderr,
                        "%sRoots currently support linear and quadratic "
                        "polynomials.%s\n",
                        C_B_RED, C_RST);
                mark_script_error(c, "polynomial roots support degree two or less");
            }
        } else {
            fprintf(stderr, "%sUse roots(p).%s\n", C_B_RED, C_RST);
            mark_script_error(c, "invalid roots syntax");
        }
        return true;
    }

    if (strncmp(line, "plot_save(", 10) == 0) {
        /* plot_save(expr; expr, x=lo..hi, file.svg) */
        char *end = strrchr(line, ')');
        char *range = NULL;
        for (char *p = line + 10; *p; p++) {
            if (*p != ',')
                continue;
            char *q = p + 1;
            while (isspace((unsigned char)*q))
                q++;
            if ((q[0] == 'x' || q[0] == 'X')) {
                char *e = q + 1;
                while (isspace((unsigned char)*e))
                    e++;
                if (*e == '=') {
                    range = p;
                    break;
                }
            }
        }
        char *dots = range ? strstr(range, "..") : NULL;
        char *file_sep = dots ? strchr(dots + 2, ',') : NULL;
        if (range && dots && file_sep && end && end > file_sep) {
            *range = '\0';
            *dots = '\0';
            *file_sep = '\0';
            *end = '\0';
            char *xeq = range + 1;
            while (isspace((unsigned char)*xeq))
                xeq++;
            xeq++;
            while (isspace((unsigned char)*xeq))
                xeq++;
            if (*xeq == '=')
                xeq++;
            while (isspace((unsigned char)*xeq))
                xeq++;
            char *hi = dots + 2;
            while (isspace((unsigned char)*hi))
                hi++;
            char *path = file_sep + 1;
            while (isspace((unsigned char)*path))
                path++;
            size_t path_len = strlen(path);
            if (path_len >= 2 &&
                ((path[0] == '"' && path[path_len - 1] == '"') ||
                 (path[0] == '\'' && path[path_len - 1] == '\''))) {
                path[path_len - 1] = '\0';
                path++;
            }
            terminalc_plot_svg(c, line + 10, xeq, hi, path);
        } else {
            fprintf(stderr,
                    "%sUse: plot_save(expr; expr, x=lo..hi, file.svg)%s\n"
                    "%s  example: plot_save(sin(x); cos(x), x=0..2*pi, curves.svg)%s\n",
                    C_B_RED, C_RST, C_DIM, C_RST);
            mark_script_error(c, "invalid plot export syntax");
        }
        return true;
    }

    if (strncmp(line, "plot(", 5) == 0) {
        /* plot(expr, x=lo..hi)  — allows spaces around x= and y=expr form */
        char *end = strrchr(line, ')');
        char *range = NULL;
        for (char *p = line + 5; *p; p++) {
            if (*p != ',')
                continue;
            char *q = p + 1;
            while (isspace((unsigned char)*q))
                q++;
            if ((q[0] == 'x' || q[0] == 'X')) {
                char *e = q + 1;
                while (isspace((unsigned char)*e))
                    e++;
                if (*e == '=') {
                    range = p;
                    break;
                }
            }
        }
        char *dots = range ? strstr(range, "..") : NULL;
        if (range && dots && end && end > dots) {
            *range = '\0';
            *dots = '\0';
            *end = '\0';
            char *xeq = range + 1;
            while (isspace((unsigned char)*xeq))
                xeq++;
            /* skip x and = */
            xeq++;
            while (isspace((unsigned char)*xeq))
                xeq++;
            if (*xeq == '=')
                xeq++;
            while (isspace((unsigned char)*xeq))
                xeq++;
            char *hi = dots + 2;
            while (isspace((unsigned char)*hi))
                hi++;
            terminalc_plot(c, line + 5, xeq, hi);
        } else {
            fprintf(stderr,
                    "%sUse: plot(expr, x=lo..hi)%s\n"
                    "%s  examples: plot(x, x=0..2)%s\n"
                    "%s            plot(y=x, x=0..2)%s\n"
                    "%s            plot(sin(x), x=-pi..pi)%s\n"
                    "%s            plot_save(sin(x);cos(x), x=0..2*pi, curves.svg)%s\n",
                    C_B_RED, C_RST, C_DIM, C_RST, C_DIM, C_RST, C_DIM, C_RST,
                    C_DIM, C_RST);
            mark_script_error(c, "invalid plot syntax");
        }
        return true;
    }

    /* assignment: name = expression */
    const char *eq = strchr(line, '=');
    char name[TERMINALC_NAME_MAX];
    const char *rhs = line;
    bool has_assign = false;
    if (eq) {
        size_t n = (size_t)(eq - line);
        if (n == 0 || n >= TERMINALC_NAME_MAX) {
            fprintf(stderr, "%sInvalid assignment.%s\n", C_B_RED, C_RST);
            mark_script_error(c, "invalid assignment");
            return true;
        }
        memcpy(name, line, n);
        name[n] = '\0';
        trim_inplace(name);
        /* validate identifier */
        if (!name[0] || (!isalpha((unsigned char)name[0]) && name[0] != '_')) {
            fprintf(stderr, "%sInvalid assignment name.%s\n", C_B_RED, C_RST);
            mark_script_error(c, "invalid assignment name");
            return true;
        }
        for (char *p = name + 1; *p; p++) {
            if (!isalnum((unsigned char)*p) && *p != '_') {
                fprintf(stderr, "%sInvalid assignment name.%s\n", C_B_RED,
                        C_RST);
                mark_script_error(c, "invalid assignment name");
                return true;
            }
        }
        rhs = eq + 1;
        has_assign = true;
    }

    TerminalCValue v;
    TerminalCAdvStats adv;
    if (c->advanced)
        terminalc_adv_begin(&adv);

    if (terminalc_eval(c, rhs, &v)) {
        if (c->advanced)
            terminalc_adv_end(c, &v, &adv);
        if (has_assign) {
            if (!assign_var(c, name, &v)) {
                terminalc_value_clear(&v);
                return true;
            }
        }
        terminalc_value_copy(&c->ans, &v);
        terminalc_print_value(c, &v);
        if (c->advanced)
            terminalc_adv_print(c, &adv);
        terminalc_value_clear(&v);
    } else {
        if (c->advanced) {
            terminalc_adv_end(c, NULL, &adv);
            terminalc_print_error(c);
            terminalc_adv_print(c, &adv);
        } else {
            terminalc_print_error(c);
        }
        terminalc_value_clear(&v);
    }
    return true;
}

int terminalc_repl(TerminalCCalc *c)
{
    char line[TERMINALC_LINE_MAX];
    TerminalCHistory history;
    bool ctrl_c_pending = false;
    memset(&history, 0, sizeof history);
    history.persistent = history_is_interactive();
    history_load(&history);

    printf("%sTerminalC %s%s - arbitrary-precision terminal calculator\n",
           C_B_CYN, TERMINALC_VERSION_STRING, C_RST);
    printf("%sDefault precision: %d digits. Type %shelp%s for commands, "
           "%squit%s to exit.%s\n",
           C_DIM, c->digits, C_B_CYN, C_DIM, C_B_CYN, C_DIM, C_RST);

    while (1) {
        repl_print_prompt();
        fflush(stdout);
        bool cancelled = false;
        if (!repl_read_line(&history, line, sizeof line, &cancelled)) {
            putchar('\n');
            break;
        }
        if (cancelled) {
            if (ctrl_c_pending) {
                putchar('\n');
                break;
            }
            ctrl_c_pending = true;
            continue;
        }
        ctrl_c_pending = false;
        line[strcspn(line, "\r\n")] = '\0';
        trim_inplace(line);
        if (!line[0])
            continue;
        if (handle_history_command(&history, line))
            continue;
        if (!history_expand(&history, line, sizeof line))
            continue;
        history_add(&history, line);
        history_save(&history);
        if (!handle_line(c, line))
            break;
    }
    return 0;
}

int terminalc_run_script(TerminalCCalc *c, const char *path)
{
    if (!path || !path[0]) {
        fprintf(stderr, "%sScript path is empty.%s\n", C_B_RED, C_RST);
        return 2;
    }
    bool close_fp = strcmp(path, "-") != 0;
    FILE *fp = close_fp ? fopen(path, "r") : stdin;
    if (!fp) {
        fprintf(stderr, "%sCannot open script '%s'.%s\n", C_B_RED, path, C_RST);
        return 2;
    }

    char line[TERMINALC_LINE_MAX];
    size_t line_no = 0;
    int rc = 0;
    bool previous_script_mode = c->script_mode;
    c->script_mode = true;
    while (fgets(line, sizeof line, fp)) {
        line_no++;
        if (!strchr(line, '\n') && !feof(fp)) {
            fprintf(stderr, "%s%s:%zu: line exceeds %d characters.%s\n",
                    C_B_RED, path, line_no, TERMINALC_LINE_MAX - 1, C_RST);
            rc = 1;
            break;
        }
        line[strcspn(line, "\r\n")] = '\0';
        strip_script_comment(line);
        trim_inplace(line);
        if (!line[0] || line[0] == '#' ||
            (line[0] == '/' && line[1] == '/'))
            continue;

        c->error = NULL;
        c->error_pos = 0;
        if (!handle_line(c, line))
            break;
        if (c->error) {
            fprintf(stderr, "%sScript failure at %s:%zu.%s\n",
                    C_B_RED, path, line_no, C_RST);
            rc = 1;
            break;
        }
    }
    if (ferror(fp)) {
        fprintf(stderr, "%sCould not read script '%s'.%s\n", C_B_RED, path, C_RST);
        rc = 2;
    }
    if (close_fp)
        fclose(fp);
    c->script_mode = previous_script_mode;
    return rc;
}

void terminalc_print_version(void)
{
    printf("TerminalC %s\n", TERMINALC_VERSION_STRING);
    printf("Built with GMP + MPFR for exact and arbitrary-precision arithmetic.\n");
}

void terminalc_print_help(void)
{
    puts("TerminalC - arbitrary-precision terminal calculator");
    puts("");
    puts("USAGE");
    puts("  terminalc [OPTIONS]");
    puts("  terminalc --eval \"EXPRESSION\"");
    puts("  terminalc --script FILE.tc");
    puts("");
    puts("OPTIONS");
    puts("  -h, --help              Show this help and exit");
    puts("  -V, --version           Show version and exit");
    puts("  -e, --eval EXPR         Evaluate EXPR and print the result");
    puts("  -f, --script FILE       Run a .tc automation script");
    puts("  -p, --precision N       Set decimal precision (2-100000)");
    puts("  --no-color              Disable ANSI color output");
    puts("  --color                 Force ANSI color output");
    puts("  --advanced              Enable advanced timing/memory stats");
    puts("  --format plain|json|csv Machine-readable result format");
    puts("");
    puts("INTERACTIVE COMMANDS");
    puts("  help [topic] | help topics | help functions | quit | exit");
    puts("  precision [N] | digits N");
    puts("  angle radians|degrees");
    puts("  vars | clear_vars");
    puts("  history [clear] | !! | !N");
    puts("  Up/Down arrows browse history; Left/Right edit the current line");
    puts("  color on|off");
    puts("  advanced on|off         time / result mem / output size");
    puts("");
    puts("EXPRESSIONS");
    puts("  Operators:  +  -  *  /  %  ^   ( ^ is right-associative )");
    puts("  Compare:    ==  !=  <  <=  >  >=");
    puts("  Logic:      and  or  not   (also &&  ||  !)");
    puts("  Booleans:   true  false   if(cond, a, b)");
    puts("  Assignment: name = expression");
    puts("  Last value: ans  or  _");
    puts("  Constants:  pi  e  phi  sqrt2  ln2");
    puts("");
    puts("FUNCTIONS");
    puts("  sin cos tan  sind cosd tand  asin acos atan atan2  hypot");
    puts("  sinh cosh tanh asinh acosh atanh  sqrt nthroot ln log log10 exp");
    puts("  log2 log1p expm1 cbrt pow ramanujan_pi  min max clamp");
    puts("  abs floor ceil  fact  gcd  lcm  ncr|choose  if");
    puts("  mean variance stddev norm  sigmoid relu sign");
    puts("  hex(n)  bin(n)  oct(n)  base(n, b) | tobase(n, b)   b in 2..36");
    puts("  fib(n)  fibn(n)");
    puts("");
    puts("NUMBER BASES");
    puts("  Input:   0xFF  0b1010  0o77   (case-insensitive prefix)");
    puts("  Output:  hex(255) -> 0xFF   bin(10) -> 0b1010   base(255,16)");
    puts("");
    puts("ALGEBRA & PLOTS");
    puts("  solve_quadratic(a, b, c)");
    puts("  p = poly([a, b, c, ...])     exact rational coefficients");
    puts("  degree(p)  evaluate(p, x)  derivative(p)  roots(p)");
    puts("  plot(expr, x=lower..upper)   native window (Windows/macOS) or ASCII");
    puts("  plot_save(exprs, x=lo..hi, file.svg)  multi-curve SVG export");
    puts("                               wheel zoom, drag pan; Esc/q closes");
    puts("                               TERMINALC_ASCII_PLOT=1 -> terminal only");
    puts("");
    puts("EXAMPLES");
    puts("  terminalc --eval \"2^3^2\"");
    puts("  terminalc --eval \"1/3+2/7\"");
    puts("  terminalc --eval \"lcm(3,6)\"");
    puts("  terminalc --eval \"hex(255)\"");
    puts("  terminalc --eval \"0xFF + 0b1\"");
    puts("  terminalc --precision 100 --eval \"sqrt(2)\"");
    puts("  terminalc");
    puts("    calc> precision 20");
    puts("    calc> solve_quadratic(1, -3, 2)");
    puts("    calc> p = poly([1, -3, 2])");
    puts("    calc> evaluate(p, 5)");
    puts("    calc> plot(sin(x), x=-2*pi..2*pi)");
    puts("");
    puts("ENVIRONMENT");
    puts("  NO_COLOR                Disable color when set (any value)");
    puts("");
    puts("See README.md and docs/USAGE.md for full documentation.");
    puts("");
    (void)terminalc_help_topic("functions");
}
