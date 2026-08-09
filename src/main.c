/**
 * TerminalC — CLI entry point
 */
#include "terminalc.h"
#include "color.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage_error(const char *msg)
{
    fprintf(stderr, "terminalc: %s\n", msg);
    fprintf(stderr, "Try 'terminalc --help' for more information.\n");
}

static bool parse_precision(const char *text, int *out)
{
    char *end = NULL;
    errno = 0;
    long value = strtol(text, &end, 10);
    if (errno == ERANGE || end == text || *end != '\0' || value < INT_MIN ||
        value > INT_MAX)
        return false;
    *out = (int)value;
    return true;
}

static bool parse_output_format(const char *text, TerminalCOutputFormat *out)
{
    if (strcmp(text, "plain") == 0) {
        *out = TERMINALC_OUTPUT_PLAIN;
        return true;
    }
    if (strcmp(text, "json") == 0) {
        *out = TERMINALC_OUTPUT_JSON;
        return true;
    }
    if (strcmp(text, "csv") == 0) {
        *out = TERMINALC_OUTPUT_CSV;
        return true;
    }
    return false;
}

int main(int argc, char **argv)
{
    /* Keep developer/product strings linked into the executable image. */
    terminalc_meta_anchor();
    terminalc_install_interrupt_handler();

    bool no_color = false;
    bool force_color = false;
    bool advanced = false;
    bool advanced_set = false;
    int precision = 0;
    bool precision_set = false;
    TerminalCOutputFormat output_format = TERMINALC_OUTPUT_PLAIN;
    bool format_set = false;
    const char *eval_expr = NULL;
    const char *script_path = NULL;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
            terminalc_color_init(&g_terminalc_color, true); /* plain help for pipes */
            terminalc_print_help();
            return 0;
        }
        if (strcmp(a, "-V") == 0 || strcmp(a, "--version") == 0) {
            terminalc_print_version();
            return 0;
        }
        if (strcmp(a, "--no-color") == 0) {
            no_color = true;
            continue;
        }
        if (strcmp(a, "--color") == 0) {
            force_color = true;
            continue;
        }
        if (strcmp(a, "--advanced") == 0) {
            advanced = true;
            advanced_set = true;
            continue;
        }
        if (strcmp(a, "--format") == 0) {
            if (i + 1 >= argc || !parse_output_format(argv[++i], &output_format)) {
                usage_error("format must be plain, json, or csv");
                return 2;
            }
            format_set = true;
            continue;
        }
        if (strcmp(a, "-p") == 0 || strcmp(a, "--precision") == 0) {
            if (i + 1 >= argc) {
                usage_error("option requires an argument -- precision");
                return 2;
            }
            if (!parse_precision(argv[++i], &precision)) {
                usage_error("invalid precision value");
                return 2;
            }
            precision_set = true;
            continue;
        }
        if (strcmp(a, "-e") == 0 || strcmp(a, "--eval") == 0) {
            if (i + 1 >= argc) {
                usage_error("option requires an argument -- eval");
                return 2;
            }
            eval_expr = argv[++i];
            continue;
        }
        if (strcmp(a, "-f") == 0 || strcmp(a, "--script") == 0) {
            if (i + 1 >= argc) {
                usage_error("option requires an argument -- script");
                return 2;
            }
            script_path = argv[++i];
            continue;
        }
        /* allow bare --eval=EXPR style? keep simple */
        if (strncmp(a, "--eval=", 7) == 0) {
            eval_expr = a + 7;
            continue;
        }
        if (strncmp(a, "--precision=", 12) == 0) {
            if (!parse_precision(a + 12, &precision)) {
                usage_error("invalid precision value");
                return 2;
            }
            precision_set = true;
            continue;
        }
        if (strncmp(a, "--format=", 9) == 0) {
            if (!parse_output_format(a + 9, &output_format)) {
                usage_error("format must be plain, json, or csv");
                return 2;
            }
            format_set = true;
            continue;
        }
        if (strncmp(a, "--script=", 9) == 0) {
            script_path = a + 9;
            continue;
        }
        fprintf(stderr, "terminalc: unrecognized option '%s'\n", a);
        usage_error("invalid arguments");
        return 2;
    }

    if (force_color) {
        terminalc_color_enable_windows_vt();
        terminalc_color_force_on(&g_terminalc_color);
    } else {
        if (!no_color)
            terminalc_color_enable_windows_vt();
        terminalc_color_init(&g_terminalc_color, no_color);
    }

    TerminalCCalc calc;
    terminalc_init(&calc);
    (void)terminalc_load_config(&calc);
    /* Explicit CLI color flags override startup configuration. */
    if (force_color) {
        terminalc_color_enable_windows_vt();
        terminalc_color_force_on(&g_terminalc_color);
        calc.color = true;
    } else if (no_color) {
        terminalc_color_init(&g_terminalc_color, true);
        calc.color = false;
    }
    if (advanced_set)
        calc.advanced = advanced;
    if (format_set)
        calc.output_format = output_format;

    if (precision_set) {
        if (!terminalc_set_precision(&calc, precision, false)) {
            terminalc_clear(&calc);
            return 2;
        }
    }

    int rc = 0;
    if (eval_expr && script_path) {
        usage_error("--eval and --script cannot be used together");
        terminalc_clear(&calc);
        return 2;
    }
    if (script_path) {
        rc = terminalc_run_script(&calc, script_path);
    } else if (eval_expr) {
        TerminalCValue v;
        TerminalCAdvStats adv;
        if (calc.advanced)
            terminalc_adv_begin(&adv);
        if (terminalc_eval(&calc, eval_expr, &v)) {
            if (calc.advanced)
                terminalc_adv_end(&calc, &v, &adv);
            if (terminalc_interrupt_requested()) {
                fprintf(stderr, "Calculation interrupted by Ctrl-C.\n");
                terminalc_clear_interrupt();
            } else {
                terminalc_print_value(&calc, &v);
                if (calc.advanced)
                    terminalc_adv_print(&calc, &adv);
            }
            terminalc_value_clear(&v);
            rc = 0;
        } else {
            if (calc.advanced)
                terminalc_adv_end(&calc, NULL, &adv);
            if (terminalc_interrupt_requested()) {
                fprintf(stderr, "Calculation interrupted by Ctrl-C.\n");
                terminalc_clear_interrupt();
                rc = 130;
            } else {
                terminalc_print_error(&calc);
                if (calc.advanced)
                    terminalc_adv_print(&calc, &adv);
                rc = 1;
            }
            terminalc_value_clear(&v);
        }
    } else {
        rc = terminalc_repl(&calc);
    }

    terminalc_clear(&calc);
    return rc;
}
