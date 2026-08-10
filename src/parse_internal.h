#ifndef TERMINALC_PARSE_INTERNAL_H
#define TERMINALC_PARSE_INTERNAL_H

#include "terminalc.h"

void terminalc_fail(TerminalCCalc *c, const char *msg);
void terminalc_skip(TerminalCCalc *c);
bool terminalc_eat(TerminalCCalc *c, char ch);
bool terminalc_read_name(TerminalCCalc *c, char out[TERMINALC_NAME_MAX]);
bool terminalc_eval_nested(TerminalCCalc *c, const char *line,
                           TerminalCValue *out);

TerminalCValue terminalc_expression(TerminalCCalc *c);
TerminalCValue terminalc_call_expr_fn(TerminalCCalc *c, const char *name, const char *expr_text,
                        TerminalCValue *args, size_t count);

/* linalg.c */
bool terminalc_linalg_transpose(TerminalCCalc *c, const TerminalCValue *a, TerminalCValue *out);
bool terminalc_linalg_matmul(TerminalCCalc *c, const TerminalCValue *a, const TerminalCValue *b, TerminalCValue *out);
bool terminalc_tensor_ew_add(TerminalCCalc *c, const TerminalCValue *a, const TerminalCValue *b, bool sub,
                      TerminalCValue *out);
bool terminalc_tensor_ew_muldiv(TerminalCCalc *c, const TerminalCValue *a,
                                const TerminalCValue *b, char op,
                                TerminalCValue *out);
bool terminalc_linalg_det(TerminalCCalc *c, const TerminalCValue *a, TerminalCValue *out);
bool terminalc_linalg_inv(TerminalCCalc *c, const TerminalCValue *a, TerminalCValue *out);
bool terminalc_linalg_solve(TerminalCCalc *c, const TerminalCValue *a, const TerminalCValue *b, TerminalCValue *out);
bool terminalc_linalg_dot(TerminalCCalc *c, const TerminalCValue *a, const TerminalCValue *b, TerminalCValue *out);

/* domains.c */
bool terminalc_numeric_solve(TerminalCCalc *c, const char *expr,
                             const TerminalCValue *a, const TerminalCValue *b,
                             bool bracket, TerminalCValue *out);
bool terminalc_numeric_diff(TerminalCCalc *c, const char *expr,
                            const TerminalCValue *x0, TerminalCValue *out);
bool terminalc_numeric_integral(TerminalCCalc *c, const char *expr,
                                const TerminalCValue *a,
                                const TerminalCValue *b, TerminalCValue *out);
bool terminalc_units_to(TerminalCCalc *c, const TerminalCValue *qty, const TerminalCValue *target_unit,
                 TerminalCValue *out);
bool terminalc_units_mul(TerminalCCalc *c, const TerminalCValue *num, const TerminalCValue *unit, TerminalCValue *out);
bool terminalc_units_div(TerminalCCalc *c, const TerminalCValue *a, const TerminalCValue *b,
                         TerminalCValue *out);

/* series.c */
bool terminalc_series_call(TerminalCCalc *c, const char *n, TerminalCValue *args,
                           size_t count, TerminalCValue *o);
bool terminalc_signal_call(TerminalCCalc *c, const char *n, TerminalCValue *args,
                           size_t count, TerminalCValue *o);
bool terminalc_series_sum_expr(TerminalCCalc *c, const char *expr, long lo,
                               long hi, TerminalCValue *out);

#endif
