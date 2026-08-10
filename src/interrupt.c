#include "terminalc.h"

#include <signal.h>

static volatile sig_atomic_t terminalc_interrupted = 0;

static void terminalc_sigint_handler(int signal_number)
{
    (void)signal_number;
    terminalc_interrupted = 1;
}

void terminalc_install_interrupt_handler(void)
{
    signal(SIGINT, terminalc_sigint_handler);
}

bool terminalc_interrupt_requested(void)
{
    return terminalc_interrupted != 0;
}

void terminalc_clear_interrupt(void)
{
    terminalc_interrupted = 0;
}
