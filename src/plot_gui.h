/**
 * Optional interactive plot window (real vector lines, zoom/pan).
 * Returns true if a GUI window was shown (and closed by the user).
 * Returns false if unavailable — caller should fall back to ASCII.
 */
#ifndef TERMINALC_PLOT_GUI_H
#define TERMINALC_PLOT_GUI_H

#include "terminalc.h"

#include <stdbool.h>

bool terminalc_plot_gui(TerminalCCalc *c, const char *expr, double xmin, double xmax);

#endif /* TERMINALC_PLOT_GUI_H */
