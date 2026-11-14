/* qjs_bridge.h — flat C ABI between hxcpp (Haxe) and QuickJS.
 *
 * Design rules (see README.md, section 设计要点):
 *   1. Nothing here ever returns a JSValue by value. JSValue is 8/16 bytes and
 *      hxcpp's generic FFI cannot marshal it; the naive workaround (JSON per call)
 *      costs microseconds. Values are either borrowed through the argument stack
 *      or owned through 32-bit handles.
 *   2. Every entry point takes only primitives: int32, double, const char*.
 *   3. Arguments and results travel through a per-engine scratch stack, so a
 *      host->JS call allocates nothing.
 *   4. Single-threaded: one engine per runtime, always touched from the main thread.
 */
#ifndef QJS_BRIDGE_H
#define QJS_BRIDGE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct qjs_engine qjs_engine;
typedef int32_t qjs_val;   /* 0 = invalid */

#define QJS_OK 1
#define QJS_ERR 0

/* ---- lifecycle ------------------------------------------------------- */
qjs_engine *qjs_create(int32_t memory_limit_mb, int32_t max_stack_kb, int32_t enable_intl);
void        qjs_destroy(qjs_engine *e);
const char *qjs_version(void);

/* ---- limits / gc ----------------------------------------------------- */
void qjs_set_memory_limit(qjs_engine *e, int64_t bytes);
void qjs_set_max_stack(qjs_engine *e, int32_t bytes);
void qjs_run_gc(qjs_engine *e);
int64_t qjs_memory_used(qjs_engine *e);
const char *qjs_memory_report(qjs_engine *e);

/* ---- interrupt (frame budget / watchdog) ----------------------------- */
typedef int (*qjs_interrupt_fn)(void *user);
void qjs_set_interrupt(qjs_engine *e, qjs_interrupt_fn fn, void *user);

/* ---- values ---------------------------------------------------------- */
qjs_val qjs_global(qjs_engine *e);
qjs_val qjs_get(qjs_engine *e, qjs_val obj, const char *key, int32_t key_len);
qjs_val qjs_get_index(qjs_engine *e, qjs_val obj, int32_t idx);
void    qjs_release(qjs_engine *e, qjs_val v);
int32_t qjs_is_function(qjs_engine *e, qjs_val v);
int32_t qjs_is_object(qjs_engine *e, qjs_val v);
int32_t qjs_is_nullish(qjs_engine *e, qjs_val v);

/* type tags mirror a small enum so Haxe can branch without strings */
enum {
    QJS_T_UNDEFINED = 0, QJS_T_NULL = 1, QJS_T_BOOL = 2, QJS_T_NUMBER = 3,
    QJS_T_STRING = 4, QJS_T_OBJECT = 5, QJS_T_FUNCTION = 6, QJS_T_BIGINT = 7,
    QJS_T_SYMBOL = 8, QJS_T_ARRAY = 9
};
int32_t qjs_typeof(qjs_engine *e, qjs_val v);

double      qjs_to_number(qjs_engine *e, qjs_val v, int32_t *ok);
int32_t     qjs_to_int32(qjs_engine *e, qjs_val v, int32_t *ok);
int32_t     qjs_to_bool(qjs_engine *e, qjs_val v);
/* Borrowed pointer, valid until the next call that mutates this engine. */
const char *qjs_to_string(qjs_engine *e, qjs_val v, int32_t *len);

/* ---- property writes (hot path) -------------------------------------- */
int32_t qjs_set_num(qjs_engine *e, qjs_val obj, const char *key, int32_t key_len, double v);
int32_t qjs_set_bool(qjs_engine *e, qjs_val obj, const char *key, int32_t key_len, int32_t v);
int32_t qjs_set_str(qjs_engine *e, qjs_val obj, const char *key, int32_t key_len, const char *v, int32_t v_len);
int32_t qjs_set_val(qjs_engine *e, qjs_val obj, const char *key, int32_t key_len, qjs_val v);   /* borrows v */
int32_t qjs_set_index_num(qjs_engine *e, qjs_val obj, int32_t idx, double v);
int32_t qjs_set_index_val(qjs_engine *e, qjs_val obj, int32_t idx, qjs_val v);

/* Bound property: resolve object+atom once, then write in O(1) per frame.
 * This is the difference between "setProperty every frame" and "setProperty
 * every frame without hashing a string, walking a prototype chain or bumping
 * a refcount". */
int32_t qjs_prop_bind(qjs_engine *e, qjs_val obj, const char *key, int32_t key_len);
int32_t qjs_prop_set_num(qjs_engine *e, int32_t prop, double v);
int32_t qjs_prop_set_bool(qjs_engine *e, int32_t prop, int32_t v);
int32_t qjs_prop_set_str(qjs_engine *e, int32_t prop, const char *v, int32_t v_len);
int32_t qjs_prop_get_num(qjs_engine *e, int32_t prop, double *out);
void    qjs_prop_free(qjs_engine *e, int32_t prop);

/* ---- calling --------------------------------------------------------- */
void qjs_args_begin(qjs_engine *e);
void qjs_push_num(qjs_engine *e, double v);
void qjs_push_int(qjs_engine *e, int32_t v);
void qjs_push_bool(qjs_engine *e, int32_t v);
void qjs_push_null(qjs_engine *e);
void qjs_push_undef(qjs_engine *e);
void qjs_push_str(qjs_engine *e, const char *s, int32_t len);
void qjs_push_val(qjs_engine *e, qjs_val v);      /* borrow, refcounted copy */
void qjs_push_own(qjs_engine *e, qjs_val v);      /* steal the handle: zero extra refcount */

int32_t qjs_call(qjs_engine *e, qjs_val fn, qjs_val this_val, int32_t argc);
int32_t qjs_call_global(qjs_engine *e, const char *name, int32_t name_len, int32_t argc);

double      qjs_result_num(qjs_engine *e, int32_t *ok);
int32_t     qjs_result_int(qjs_engine *e, int32_t *ok);
int32_t     qjs_result_bool(qjs_engine *e);
const char *qjs_result_str(qjs_engine *e, int32_t *len);
qjs_val     qjs_result_val(qjs_engine *e);   /* take ownership as a handle */
void        qjs_result_drop(qjs_engine *e);

/* ---- host functions -------------------------------------------------- */
/* Called from JS. Return QJS_OK after filling the return slot with one of the
 * qjs_ret_* calls, or QJS_ERR to throw the pending JS exception. */
typedef int32_t (*qjs_host_fn)(qjs_engine *e, int32_t fn_id, int32_t argc, void *user);
void qjs_set_host_dispatch(qjs_engine *e, qjs_host_fn fn, void *user);
qjs_val qjs_new_host_fn(qjs_engine *e, const char *name, int32_t name_len, int32_t fn_id, int32_t arity);

int32_t     qjs_argc(qjs_engine *e);
int32_t     qjs_arg_type(qjs_engine *e, int32_t i);
double      qjs_arg_num(qjs_engine *e, int32_t i);
int32_t     qjs_arg_int(qjs_engine *e, int32_t i);
int32_t     qjs_arg_bool(qjs_engine *e, int32_t i);
const char *qjs_arg_str(qjs_engine *e, int32_t i, int32_t *len);
qjs_val     qjs_arg_val(qjs_engine *e, int32_t i);   /* owning handle */

void qjs_ret_num(qjs_engine *e, double v);
void qjs_ret_int(qjs_engine *e, int32_t v);
void qjs_ret_bool(qjs_engine *e, int32_t v);
void qjs_ret_null(qjs_engine *e);
void qjs_ret_undef(qjs_engine *e);
void qjs_ret_str(qjs_engine *e, const char *s, int32_t len);
void qjs_ret_val(qjs_engine *e, qjs_val v);    /* borrows */

/* ---- evaluation & bytecode ------------------------------------------- */
/* flags: 1 = module, 2 = strict, 4 = compile only */
int32_t qjs_eval(qjs_engine *e, const char *src, int32_t len, const char *filename, int32_t flags);
int32_t qjs_eval_fn(qjs_engine *e, qjs_val fn);
void    qjs_pump_jobs(qjs_engine *e);

/* Compile to bytecode. Returns a malloc'ed buffer (free with qjs_free_bytes). */
uint8_t *qjs_compile(qjs_engine *e, const char *src, int32_t len, const char *filename,
                     int32_t flags, int32_t *out_len);
int32_t  qjs_run_bytecode(qjs_engine *e, const uint8_t *buf, int32_t len);
void     qjs_free_bytes(qjs_engine *e, uint8_t *buf);

/* ---- errors ---------------------------------------------------------- */
int32_t     qjs_has_exception(qjs_engine *e);
void        qjs_throw(qjs_engine *e, const char *msg, int32_t len);
const char *qjs_take_error(qjs_engine *e, int32_t *len);  /* NUL-terminated, engine-owned */

/* ---- modules --------------------------------------------------------- */
typedef const char *(*qjs_module_loader_fn)(qjs_engine *e, const char *spec, int32_t len, int32_t *out_len, void *user);
void qjs_set_module_loader(qjs_engine *e, qjs_module_loader_fn fn, void *user);

/* ---- interop: plain objects <-> host --------------------------------- */
qjs_val qjs_new_object(qjs_engine *e);
qjs_val qjs_new_array(qjs_engine *e, int32_t len);
int32_t qjs_array_length(qjs_engine *e, qjs_val arr);
qjs_val qjs_json_parse(qjs_engine *e, const char *s, int32_t len);  /* slow path only */
const char *qjs_json_stringify(qjs_engine *e, qjs_val v, int32_t *len);

#ifdef __cplusplus
}
#endif
#endif
