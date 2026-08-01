/**
 * Binary-resident product metadata.
 * Not printed by the REPL/help UI. Kept in the linked image for identification
 * (alongside Windows VERSIONINFO in terminalc.rc — File Properties → Details).
 */
#include "terminalc.h"

#include <stddef.h>

#if defined(__GNUC__) || defined(__clang__)
#define TERMINALC_USED __attribute__((used))
#else
#define TERMINALC_USED
#endif

/* Explicit tags in the executable's read-only data. */
TERMINALC_USED static const char terminalc_meta_developer[] = "Developer: Impractical.in";
TERMINALC_USED static const char terminalc_meta_org[] = "Impractical.in";
TERMINALC_USED static const char terminalc_meta_product[] = "TerminalC by Impractical.in";
TERMINALC_USED static const char terminalc_meta_url[] = "https://impractical.in";

const char *terminalc_meta_developer_id(void)
{
    return terminalc_meta_developer;
}

const char *terminalc_meta_organization(void)
{
    return terminalc_meta_org;
}

void terminalc_meta_anchor(void)
{
    /*
     * Opaque use so link-time GC / LTO cannot prove the payloads unused.
     * Result is never printed during normal operation.
     */
    size_t n = 0;
    n += terminalc_meta_developer[0];
    n += terminalc_meta_org[0];
    n += terminalc_meta_product[0];
    n += terminalc_meta_url[0];
    n += (size_t)(terminalc_meta_developer_id() != NULL);
    n += (size_t)(terminalc_meta_organization() != NULL);
    if (n == (size_t)-1)
        (void)terminalc_meta_url[0];
}
