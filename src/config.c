#include "terminalc.h"
#include "color.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void trim(char *s)
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

static const char *config_path(void)
{
    const char *override = getenv("TERMINALC_CONFIG");
    if (override)
        return override[0] ? override : NULL;

    static char path[TERMINALC_LINE_MAX];
#ifdef _WIN32
    const char *home = getenv("USERPROFILE");
#else
    const char *home = getenv("HOME");
#endif
    if (!home || !home[0])
        return NULL;
    snprintf(path, sizeof path, "%s/.terminalcrc", home);
    return path;
}

bool terminalc_load_config(TerminalCCalc *c)
{
    const char *path = config_path();
    if (!path)
        return false;
    FILE *fp = fopen(path, "r");
    if (!fp)
        return false;

    char line[TERMINALC_LINE_MAX];
    while (fgets(line, sizeof line, fp)) {
        line[strcspn(line, "\r\n")] = '\0';
        trim(line);
        if (!line[0] || line[0] == '#' ||
            (line[0] == '/' && line[1] == '/'))
            continue;
        char key[32], value[128];
        if (sscanf(line, "%31s %127s", key, value) != 2)
            continue;
        for (char *p = key; *p; p++)
            *p = (char)tolower((unsigned char)*p);
        for (char *p = value; *p; p++)
            *p = (char)tolower((unsigned char)*p);

        if ((strcmp(key, "precision") == 0 || strcmp(key, "digits") == 0)) {
            char *end = NULL;
            long digits = strtol(value, &end, 10);
            if (end != value && *end == '\0')
                (void)terminalc_set_precision(c, (int)digits, false);
        } else if (strcmp(key, "angle") == 0) {
            if (strcmp(value, "degrees") == 0)
                c->degrees = true;
            else if (strcmp(value, "radians") == 0)
                c->degrees = false;
        } else if (strcmp(key, "advanced") == 0) {
            if (strcmp(value, "on") == 0 || strcmp(value, "true") == 0)
                c->advanced = true;
            else if (strcmp(value, "off") == 0 || strcmp(value, "false") == 0)
                c->advanced = false;
        } else if (strcmp(key, "format") == 0 || strcmp(key, "output") == 0) {
            if (strcmp(value, "plain") == 0)
                c->output_format = TERMINALC_OUTPUT_PLAIN;
            else if (strcmp(value, "json") == 0)
                c->output_format = TERMINALC_OUTPUT_JSON;
            else if (strcmp(value, "csv") == 0)
                c->output_format = TERMINALC_OUTPUT_CSV;
        } else if (strcmp(key, "color") == 0 && strcmp(value, "off") == 0) {
            terminalc_color_init(&g_terminalc_color, true);
            c->color = false;
        }
    }
    fclose(fp);
    return true;
}
