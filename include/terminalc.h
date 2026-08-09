/**
 * TerminalC — public API
 * Arbitrary-precision terminal calculator (GMP + MPFR).
 *
 * Product metadata (developer id) lives in the binary image; see terminalc_meta_*.
 */
#ifndef TERMINALC_H
#define TERMINALC_H

#include <gmp.h>
#include <mpfr.h>
#include <stdbool.h>
#include <stddef.h>

#define TERMINALC_VERSION_MAJOR 1
#define TERMINALC_VERSION_MINOR 0
#define TERMINALC_VERSION_PATCH 0
#define TERMINALC_VERSION_STRING "1.0.0-beta.1"

#define TERMINALC_NAME_MAX 48
#define TERMINALC_VAR_MAX 128
#define TERMINALC_LINE_MAX 8192
#define TERMINALC_GUARD_BITS 32
#define TERMINALC_MAX_ARGS 32
#define TERMINALC_TENSOR_MAX_ELEM 10000
#define TERMINALC_TENSOR_MAX_RANK 4
#define TERMINALC_PARSE_MAX_DEPTH 64
#define TERMINALC_EXACT_RESULT_MAX_BITS 8000000

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    TERMINALC_RAT = 0,
    TERMINALC_REAL = 1,
    TERMINALC_BOOL = 2,
    TERMINALC_TENSOR = 3,
    TERMINALC_MEASURE = 4
} TerminalCValueType;

typedef enum {
    TERMINALC_OUTPUT_PLAIN = 0,
    TERMINALC_OUTPUT_JSON = 1,
    TERMINALC_OUTPUT_CSV = 2
} TerminalCOutputFormat;

typedef struct TerminalCValue TerminalCValue;

typedef struct TerminalCTensor {
    int rank;                 /* 1..4 */
    size_t shape[TERMINALC_TENSOR_MAX_RANK];
    size_t n;                 /* product of shape */
    TerminalCValue *data;            /* length n; scalar cells only */
} TerminalCTensor;

struct TerminalCValue {
    TerminalCValueType type;
    mpq_t q;
    mpfr_t r;
    bool b;
    TerminalCTensor *ten; /* TERMINALC_TENSOR (owned) */
    int unit_id;   /* TERMINALC_MEASURE: magnitude in q (or r if promoted) */
};

typedef struct {
    char name[TERMINALC_NAME_MAX];
    TerminalCValue value;
} TerminalCVariable;

typedef struct {
    char name[TERMINALC_NAME_MAX];
    mpq_t *coeff; /* highest degree first */
    size_t n;
} TerminalCPolynomial;

typedef struct TerminalCCalc {
    const char *text;
    const char *at;
    const char *error;
    size_t error_pos;
    char *owned_text; /* backing storage retained by terminalc_eval_n */
    unsigned parse_depth;

    mpfr_prec_t bits;
    int digits;
    bool degrees;
    bool color;
    bool advanced; /* show time / memory / output-size after eval */
    bool script_mode; /* make command errors fatal for --script */
    int out_base;
    TerminalCOutputFormat output_format;

    TerminalCValue ans;
    TerminalCValue x;
    TerminalCVariable vars[TERMINALC_VAR_MAX];
    size_t var_count;

    TerminalCPolynomial polys[TERMINALC_VAR_MAX];
    size_t poly_count;
} TerminalCCalc;

/* lifecycle */
void terminalc_init(TerminalCCalc *c);
void terminalc_clear(TerminalCCalc *c);
bool terminalc_set_precision(TerminalCCalc *c, int digits, bool verbose);

/* values */
void terminalc_value_init(TerminalCValue *v, mpfr_prec_t bits);
void terminalc_value_clear(TerminalCValue *v);
void terminalc_value_copy(TerminalCValue *dst, const TerminalCValue *src);
void terminalc_value_set_si(TerminalCValue *v, long n);
void terminalc_value_set_q(TerminalCValue *v, const mpq_t q);
void terminalc_value_set_r(TerminalCValue *v, const mpfr_t r);
void terminalc_value_set_bool(TerminalCValue *v, bool b);
void terminalc_as_real(const TerminalCCalc *c, mpfr_t out, const TerminalCValue *v);
bool terminalc_is_integer(const TerminalCValue *v);
bool terminalc_is_truthy(const TerminalCValue *v);
bool terminalc_is_scalar(const TerminalCValue *v);
void terminalc_print_value(TerminalCCalc *c, const TerminalCValue *v);
void terminalc_print_error(const TerminalCCalc *c);
const char *terminalc_measure_unit_name(int unit_id);

/* Advanced-mode metrics (time / memory / print size) */
typedef struct {
    double elapsed_ms;
    size_t object_bytes;       /* full result object (shell + payload) */
    size_t payload_bytes;      /* variable content only (limbs / cells) */
    size_t output_bytes;       /* estimated printed size of the result */
    size_t working_set_bytes;  /* process working set after eval (0 if N/A) */
    long working_set_delta;    /* signed delta vs before eval (0 if N/A) */
} TerminalCAdvStats;

void terminalc_adv_begin(TerminalCAdvStats *st);
void terminalc_adv_end(TerminalCCalc *c, const TerminalCValue *v,
                       TerminalCAdvStats *st);
void terminalc_adv_print(const TerminalCCalc *c, const TerminalCAdvStats *st);
/* object = shell + payload; payload = numeric limbs / tensor cell content only */
size_t terminalc_value_object_bytes(const TerminalCValue *v);
size_t terminalc_value_payload_bytes(const TerminalCValue *v);
size_t terminalc_value_output_bytes(const TerminalCCalc *c,
                                    const TerminalCValue *v);
/* alias kept for clarity in docs */
#define terminalc_value_mem_bytes terminalc_value_object_bytes

/* evaluate */
bool terminalc_eval(TerminalCCalc *c, const char *line, TerminalCValue *out);
bool terminalc_eval_n(TerminalCCalc *c, const char *s, size_t n, TerminalCValue *out);

/* algebra / plot */
bool terminalc_quadratic(TerminalCCalc *c, const char *args);
bool terminalc_store_poly(TerminalCCalc *c, const char *name, const char *body);
TerminalCPolynomial *terminalc_find_poly(TerminalCCalc *c, const char *name);
void terminalc_poly_eval(TerminalCCalc *c, const TerminalCPolynomial *p, const TerminalCValue *x, mpfr_t out);
bool terminalc_poly_derivative(TerminalCCalc *c, const char *src_name, const char *dst_name);
void terminalc_plot(TerminalCCalc *c, const char *expr, const char *lo, const char *hi);
void terminalc_plot_svg(TerminalCCalc *c, const char *expr, const char *lo,
                        const char *hi, const char *path);

/* CLI */
void terminalc_print_help(void);
void terminalc_print_version(void);
int terminalc_repl(TerminalCCalc *c);
int terminalc_run_script(TerminalCCalc *c, const char *path);
bool terminalc_help_topic(const char *topic);

const char *terminalc_meta_developer_id(void);
const char *terminalc_meta_organization(void);
void terminalc_meta_anchor(void);

/* tensor (tensor.c) */
bool terminalc_tensor_create(TerminalCCalc *c, TerminalCValue *out, int rank, const size_t *shape);
void terminalc_tensor_free(TerminalCTensor *t);
bool terminalc_tensor_fill(TerminalCCalc *c, TerminalCValue *out, int rank, const size_t *shape,
                    const TerminalCValue *fill);
bool terminalc_tensor_eye(TerminalCCalc *c, TerminalCValue *out, size_t n);
bool terminalc_tensor_from_args(TerminalCCalc *c, TerminalCValue *out, size_t rows, size_t cols,
                         TerminalCValue *args, size_t nargs);
void terminalc_print_tensor(const TerminalCCalc *c, const TerminalCTensor *t);

/* domain dispatch used by parse (functions + modules) */
TerminalCValue terminalc_call(TerminalCCalc *c, const char *name, TerminalCValue *args, size_t count);
TerminalCValue terminalc_module_call(TerminalCCalc *c, const char *module, const char *method,
                       TerminalCValue *args, size_t count);
bool terminalc_try_unit_constant(const char *name, TerminalCValue *out, mpfr_prec_t bits);

#ifdef __cplusplus
}
#endif

#endif /* TERMINALC_H */
