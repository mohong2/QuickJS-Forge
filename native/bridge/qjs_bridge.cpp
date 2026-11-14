/* qjs_bridge.cpp — see qjs_bridge.h for the contract. */
#include "qjs_bridge.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "quickjs.h"

struct QjsProp {
    JSValue obj;
    JSAtom  atom;
};

struct qjs_engine {
    JSRuntime *rt = nullptr;
    JSContext *ctx = nullptr;

    qjs_host_fn host_fn = nullptr;
    void       *host_user = nullptr;
    qjs_interrupt_fn intr = nullptr;
    void       *intr_user = nullptr;
    qjs_module_loader_fn loader = nullptr;
    void       *loader_user = nullptr;

    std::vector<JSValue> slots;
    std::vector<int32_t> free_slots;

    std::vector<JSValue> args;
    JSValue  result = JS_UNDEFINED;
    bool     result_valid = false;

    std::vector<JSValue> host_args;
    JSValue  host_ret = JS_UNDEFINED;

    std::vector<QjsProp> props;
    std::vector<int32_t> free_props;

    std::string scratch;
    std::string error;
    bool        has_error = false;
};

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

static void clear_error(qjs_engine *e) { e->has_error = false; e->error.clear(); }

static void capture_exception(qjs_engine *e)
{
    JSContext *ctx = e->ctx;
    JSValue exc = JS_GetException(ctx);
    e->error.clear();

    if (JS_IsObject(exc)) {
        JSValue msg = JS_GetPropertyStr(ctx, exc, "message");
        JSValue name = JS_GetPropertyStr(ctx, exc, "name");
        JSValue stack = JS_GetPropertyStr(ctx, exc, "stack");
        const char *n = JS_ToCString(ctx, name);
        const char *m = JS_ToCString(ctx, msg);
        const char *s = JS_ToCString(ctx, stack);

        if (!m) {
            /* thrown non-Error value */
            JSValue ex2 = JS_GetException(ctx);
            const char *t = JS_ToCString(ctx, ex2);
            if (t) { e->error.append(t); JS_FreeCString(ctx, t); }
            JS_FreeValue(ctx, ex2);
        } else {
            if (n && *n && strcmp(n, "Error") != 0) { e->error.append(n); e->error.append(": "); }
            e->error.append(m);
            if (s && *s) {
                const char *first_nl = strchr(s, '\n');
                if (first_nl) e->error.append(first_nl);
                else { e->error.append("\n    at "); e->error.append(s); }
            } else {
                JSValue fname = JS_GetPropertyStr(ctx, exc, "fileName");
                JSValue line = JS_GetPropertyStr(ctx, exc, "lineNumber");
                const char *f = JS_ToCString(ctx, fname);
                int32_t ln = 0; JS_ToInt32(ctx, &ln, line);
                if (f) {
                    char buf[32]; snprintf(buf, sizeof(buf), ":%d", ln);
                    e->error.append("\n    at ").append(f).append(buf);
                    JS_FreeCString(ctx, f);
                }
                JS_FreeValue(ctx, fname); JS_FreeValue(ctx, line);
            }
        }
        if (n) JS_FreeCString(ctx, n);
        if (m) JS_FreeCString(ctx, m);
        if (s) JS_FreeCString(ctx, s);
        JS_FreeValue(ctx, msg); JS_FreeValue(ctx, name); JS_FreeValue(ctx, stack);
    } else {
        const char *t = JS_ToCString(ctx, exc);
        e->error.append(t ? t : "unknown exception");
        if (t) JS_FreeCString(ctx, t);
    }
    if (e->error.empty()) e->error = "unknown exception";
    e->has_error = true;
    JS_FreeValue(ctx, exc);
}

static inline JSValue slot_get(qjs_engine *e, qjs_val h)
{
    if (h <= 0 || (size_t)h >= e->slots.size()) return JS_UNDEFINED;
    return e->slots[(size_t)h];
}

static qjs_val slot_new(qjs_engine *e, JSValue v)
{
    int32_t idx;
    if (!e->free_slots.empty()) { idx = e->free_slots.back(); e->free_slots.pop_back(); e->slots[(size_t)idx] = v; }
    else { idx = (int32_t)e->slots.size(); e->slots.push_back(v); }
    return idx;
}

static void slots_clear(qjs_engine *e)
{
    for (auto &v : e->slots) { if (!JS_IsUndefined(v) || true) JS_FreeValue(e->ctx, v); }
    e->slots.clear(); e->free_slots.clear();
}

/* ------------------------------------------------------------------ */
/* lifecycle                                                           */
/* ------------------------------------------------------------------ */

extern "C" qjs_engine *qjs_create(int32_t memory_limit_mb, int32_t max_stack_kb, int32_t enable_intl)
{
    (void)enable_intl;
    qjs_engine *e = new qjs_engine();
    e->rt = JS_NewRuntime();
    if (!e->rt) { delete e; return nullptr; }
    if (memory_limit_mb > 0) JS_SetMemoryLimit(e->rt, (size_t)memory_limit_mb * 1024u * 1024u);
    if (max_stack_kb > 0) JS_SetMaxStackSize(e->rt, (size_t)max_stack_kb * 1024u);
    e->ctx = JS_NewContext(e->rt);
    if (!e->ctx) { JS_FreeRuntime(e->rt); delete e; return nullptr; }
    JS_SetContextOpaque(e->ctx, e);
    e->slots.push_back(JS_UNDEFINED);   /* handle 0 is always invalid */
    e->props.push_back(QjsProp{JS_UNDEFINED, 0});
    return e;
}

extern "C" void qjs_destroy(qjs_engine *e)
{
    if (!e) return;
    slots_clear(e);
    for (size_t i = 1; i < e->props.size(); i++) {
        if (e->props[i].atom) {
            JS_FreeAtom(e->ctx, e->props[i].atom);
            JS_FreeValue(e->ctx, e->props[i].obj);
        }
    }
    e->props.clear();
    if (e->result_valid) { JS_FreeValue(e->ctx, e->result); e->result_valid = false; }
    JS_FreeContext(e->ctx);
    JS_FreeRuntime(e->rt);
    delete e;
}

extern "C" const char *qjs_version(void) { return JS_GetVersion(); }

/* ------------------------------------------------------------------ */
/* limits / gc                                                         */
/* ------------------------------------------------------------------ */

extern "C" void qjs_set_memory_limit(qjs_engine *e, int64_t bytes) { JS_SetMemoryLimit(e->rt, (size_t)bytes); }
extern "C" void qjs_set_max_stack(qjs_engine *e, int32_t bytes) { JS_SetMaxStackSize(e->rt, (size_t)bytes); }
extern "C" void qjs_run_gc(qjs_engine *e) { JS_RunGC(e->rt); }

extern "C" int64_t qjs_memory_used(qjs_engine *e)
{
    JSMemoryUsage u; JS_ComputeMemoryUsage(e->rt, &u);
    return (int64_t)u.malloc_size;
}

extern "C" const char *qjs_memory_report(qjs_engine *e)
{
    JSMemoryUsage u; JS_ComputeMemoryUsage(e->rt, &u);
    char buf[512];
    snprintf(buf, sizeof(buf),
             "malloc=%lldB blocks=%lld objs=%lld atoms=%lld strings=%lld shapes=%lld",
             (long long)u.malloc_size, (long long)u.malloc_count, (long long)u.obj_count,
             (long long)u.atom_count, (long long)u.str_count, (long long)u.shape_count);
    e->scratch.assign(buf);
    return e->scratch.c_str();
}

/* ------------------------------------------------------------------ */
/* interrupt                                                           */
/* ------------------------------------------------------------------ */

static int interrupt_trampoline(JSRuntime *rt, void *opaque)
{
    (void)rt;
    qjs_engine *e = (qjs_engine *)opaque;
    return e->intr ? e->intr(e->intr_user) : 0;
}

extern "C" void qjs_set_interrupt(qjs_engine *e, qjs_interrupt_fn fn, void *user)
{
    e->intr = fn; e->intr_user = user;
    JS_SetInterruptHandler(e->rt, fn ? (JSInterruptHandler *)interrupt_trampoline : nullptr, e);
}

/* ------------------------------------------------------------------ */
/* values                                                              */
/* ------------------------------------------------------------------ */

extern "C" qjs_val qjs_global(qjs_engine *e) { return slot_new(e, JS_GetGlobalObject(e->ctx)); }

extern "C" qjs_val qjs_get(qjs_engine *e, qjs_val obj, const char *key, int32_t key_len)
{
    JSValue v = JS_GetPropertyStr(e->ctx, slot_get(e, obj), key);
    if (JS_IsException(v)) { capture_exception(e); JS_FreeValue(e->ctx, v); return 0; }
    return slot_new(e, v);
}

extern "C" qjs_val qjs_get_index(qjs_engine *e, qjs_val obj, int32_t idx)
{
    JSValue v = JS_GetPropertyUint32(e->ctx, slot_get(e, obj), (uint32_t)idx);
    if (JS_IsException(v)) { capture_exception(e); JS_FreeValue(e->ctx, v); return 0; }
    return slot_new(e, v);
}

extern "C" void qjs_release(qjs_engine *e, qjs_val v)
{
    if (v <= 0 || (size_t)v >= e->slots.size()) return;
    JSValue cur = e->slots[(size_t)v];
    JS_FreeValue(e->ctx, cur);
    e->slots[(size_t)v] = JS_UNDEFINED;
    e->free_slots.push_back(v);
}

extern "C" int32_t qjs_is_function(qjs_engine *e, qjs_val v) { return JS_IsFunction(e->ctx, slot_get(e, v)) ? 1 : 0; }
extern "C" int32_t qjs_is_object(qjs_engine *e, qjs_val v) { return JS_IsObject(slot_get(e, v)) ? 1 : 0; }
extern "C" int32_t qjs_is_nullish(qjs_engine *e, qjs_val v)
{
    JSValue x = slot_get(e, v);
    return (JS_IsNull(x) || JS_IsUndefined(x)) ? 1 : 0;
}

extern "C" int32_t qjs_typeof(qjs_engine *e, qjs_val v)
{
    JSValue x = slot_get(e, v);
    if (JS_IsUndefined(x)) return QJS_T_UNDEFINED;
    if (JS_IsNull(x)) return QJS_T_NULL;
    if (JS_IsBool(x)) return QJS_T_BOOL;
    if (JS_IsNumber(x)) return QJS_T_NUMBER;
    if (JS_IsString(x)) return QJS_T_STRING;
    if (JS_IsBigInt(x)) return QJS_T_BIGINT;
    if (JS_IsSymbol(x)) return QJS_T_SYMBOL;
    if (JS_IsFunction(e->ctx, x)) return QJS_T_FUNCTION;
    if (JS_IsArray(x)) return QJS_T_ARRAY;
    if (JS_IsObject(x)) return QJS_T_OBJECT;
    return QJS_T_UNDEFINED;
}

extern "C" double qjs_to_number(qjs_engine *e, qjs_val v, int32_t *ok)
{
    double d = 0;
    int r = JS_ToFloat64(e->ctx, &d, slot_get(e, v));
    if (ok) *ok = (r == 0);
    return d;
}

extern "C" int32_t qjs_to_int32(qjs_engine *e, qjs_val v, int32_t *ok)
{
    int32_t i = 0;
    int r = JS_ToInt32(e->ctx, &i, slot_get(e, v));
    if (ok) *ok = (r == 0);
    return i;
}

extern "C" int32_t qjs_to_bool(qjs_engine *e, qjs_val v) { return JS_ToBool(e->ctx, slot_get(e, v)) ? 1 : 0; }

extern "C" const char *qjs_to_string(qjs_engine *e, qjs_val v, int32_t *len)
{
    size_t l = 0;
    const char *s = JS_ToCStringLen(e->ctx, &l, slot_get(e, v));
    if (!s) { if (len) *len = 0; return ""; }
    e->scratch.assign(s, l);
    JS_FreeCString(e->ctx, s);
    if (len) *len = (int32_t)e->scratch.size();
    return e->scratch.c_str();
}

/* ------------------------------------------------------------------ */
/* property writes                                                     */
/* ------------------------------------------------------------------ */

extern "C" int32_t qjs_set_num(qjs_engine *e, qjs_val obj, const char *key, int32_t key_len, double v)
{
    return JS_SetPropertyStr(e->ctx, slot_get(e, obj), key, JS_NewFloat64(e->ctx, v)) == 0;
}
extern "C" int32_t qjs_set_bool(qjs_engine *e, qjs_val obj, const char *key, int32_t key_len, int32_t v)
{
    return JS_SetPropertyStr(e->ctx, slot_get(e, obj), key, JS_NewBool(e->ctx, v != 0)) == 0;
}
extern "C" int32_t qjs_set_str(qjs_engine *e, qjs_val obj, const char *key, int32_t key_len, const char *v, int32_t v_len)
{
    return JS_SetPropertyStr(e->ctx, slot_get(e, obj), key, JS_NewStringLen(e->ctx, v, (size_t)v_len)) == 0;
}
extern "C" int32_t qjs_set_val(qjs_engine *e, qjs_val obj, const char *key, int32_t key_len, qjs_val v)
{
    return JS_SetPropertyStr(e->ctx, slot_get(e, obj), key, JS_DupValue(e->ctx, slot_get(e, v))) == 0;
}
extern "C" int32_t qjs_set_index_num(qjs_engine *e, qjs_val obj, int32_t idx, double v)
{
    return JS_SetPropertyUint32(e->ctx, slot_get(e, obj), (uint32_t)idx, JS_NewFloat64(e->ctx, v)) == 0;
}
extern "C" int32_t qjs_set_index_val(qjs_engine *e, qjs_val obj, int32_t idx, qjs_val v)
{
    return JS_SetPropertyUint32(e->ctx, slot_get(e, obj), (uint32_t)idx, JS_DupValue(e->ctx, slot_get(e, v))) == 0;
}

extern "C" int32_t qjs_prop_bind(qjs_engine *e, qjs_val obj, const char *key, int32_t key_len)
{
    JSValue o = slot_get(e, obj);
    if (!JS_IsObject(o)) return 0;
    QjsProp p;
    p.obj = JS_DupValue(e->ctx, o);
    p.atom = JS_NewAtomLen(e->ctx, key, (size_t)key_len);
    if (!p.atom) { JS_FreeValue(e->ctx, p.obj); return 0; }
    int32_t idx;
    if (!e->free_props.empty()) { idx = e->free_props.back(); e->free_props.pop_back(); e->props[(size_t)idx] = p; }
    else { idx = (int32_t)e->props.size(); e->props.push_back(p); }
    return idx;
}

#define PROP_AT(e, i) (((i) > 0 && (size_t)(i) < (e)->props.size()) ? &(e)->props[(size_t)(i)] : nullptr)

extern "C" int32_t qjs_prop_set_num(qjs_engine *e, int32_t prop, double v)
{
    QjsProp *p = PROP_AT(e, prop); if (!p) return 0;
    return JS_SetProperty(e->ctx, p->obj, p->atom, JS_NewFloat64(e->ctx, v)) == 0;
}
extern "C" int32_t qjs_prop_set_bool(qjs_engine *e, int32_t prop, int32_t v)
{
    QjsProp *p = PROP_AT(e, prop); if (!p) return 0;
    return JS_SetProperty(e->ctx, p->obj, p->atom, JS_NewBool(e->ctx, v != 0)) == 0;
}
extern "C" int32_t qjs_prop_set_str(qjs_engine *e, int32_t prop, const char *v, int32_t v_len)
{
    QjsProp *p = PROP_AT(e, prop); if (!p) return 0;
    return JS_SetProperty(e->ctx, p->obj, p->atom, JS_NewStringLen(e->ctx, v, (size_t)v_len)) == 0;
}
extern "C" int32_t qjs_prop_get_num(qjs_engine *e, int32_t prop, double *out)
{
    QjsProp *p = PROP_AT(e, prop); if (!p) return 0;
    JSValue v = JS_GetProperty(e->ctx, p->obj, p->atom);
    if (JS_IsException(v)) { capture_exception(e); JS_FreeValue(e->ctx, v); return 0; }
    double d = 0; int r = JS_ToFloat64(e->ctx, &d, v);
    JS_FreeValue(e->ctx, v);
    if (r != 0) return 0;
    if (out) *out = d;
    return 1;
}
extern "C" void qjs_prop_free(qjs_engine *e, int32_t prop)
{
    QjsProp *p = PROP_AT(e, prop); if (!p) return;
    JS_FreeAtom(e->ctx, p->atom);
    JS_FreeValue(e->ctx, p->obj);
    p->atom = 0; p->obj = JS_UNDEFINED;
    e->free_props.push_back(prop);
}

/* ------------------------------------------------------------------ */
/* calling                                                             */
/* ------------------------------------------------------------------ */

extern "C" void qjs_args_begin(qjs_engine *e)
{
    for (auto &v : e->args) JS_FreeValue(e->ctx, v);
    e->args.clear();
}

extern "C" void qjs_push_num(qjs_engine *e, double v) { e->args.push_back(JS_NewFloat64(e->ctx, v)); }
extern "C" void qjs_push_int(qjs_engine *e, int32_t v) { e->args.push_back(JS_NewInt32(e->ctx, v)); }
extern "C" void qjs_push_bool(qjs_engine *e, int32_t v) { e->args.push_back(JS_NewBool(e->ctx, v != 0)); }
extern "C" void qjs_push_null(qjs_engine *e) { e->args.push_back(JS_NULL); }
extern "C" void qjs_push_undef(qjs_engine *e) { e->args.push_back(JS_UNDEFINED); }
extern "C" void qjs_push_str(qjs_engine *e, const char *s, int32_t len) { e->args.push_back(JS_NewStringLen(e->ctx, s, (size_t)len)); }
extern "C" void qjs_push_val(qjs_engine *e, qjs_val v) { e->args.push_back(JS_DupValue(e->ctx, slot_get(e, v))); }

extern "C" void qjs_push_own(qjs_engine *e, qjs_val v)
{
    if (v <= 0 || (size_t)v >= e->slots.size()) return;
    e->args.push_back(e->slots[(size_t)v]);
    e->slots[(size_t)v] = JS_UNDEFINED;
    e->free_slots.push_back(v);
}

extern "C" int32_t qjs_call(qjs_engine *e, qjs_val fn, qjs_val this_val, int32_t argc)
{
    clear_error(e);
    if (e->result_valid) { JS_FreeValue(e->ctx, e->result); e->result = JS_UNDEFINED; e->result_valid = false; }
    JSValue f = slot_get(e, fn);
    if (!JS_IsFunction(e->ctx, f)) {
        e->error = "TypeError: value is not a function";
        e->has_error = true;
        qjs_args_begin(e);
        return QJS_ERR;
    }
    JSValue r = JS_Call(e->ctx, f, slot_get(e, this_val), (int)argc,
                        e->args.empty() ? nullptr : e->args.data());
    qjs_args_begin(e);
    if (JS_IsException(r)) { JS_FreeValue(e->ctx, r); capture_exception(e); return QJS_ERR; }
    e->result = r; e->result_valid = true;
    return QJS_OK;
}

extern "C" int32_t qjs_call_global(qjs_engine *e, const char *name, int32_t name_len, int32_t argc)
{
    clear_error(e);
    JSValue g = JS_GetGlobalObject(e->ctx);
    JSValue f = JS_GetPropertyStr(e->ctx, g, name);
    int32_t rc = QJS_OK;
    if (!JS_IsFunction(e->ctx, f)) { rc = QJS_ERR; e->error = "TypeError: no such global function"; e->has_error = true; }
    else {
        if (e->result_valid) { JS_FreeValue(e->ctx, e->result); e->result = JS_UNDEFINED; e->result_valid = false; }
        JSValue r = JS_Call(e->ctx, f, g, (int)argc, e->args.empty() ? nullptr : e->args.data());
        if (JS_IsException(r)) { JS_FreeValue(e->ctx, r); capture_exception(e); rc = QJS_ERR; }
        else { e->result = r; e->result_valid = true; }
    }
    JS_FreeValue(e->ctx, f);
    JS_FreeValue(e->ctx, g);
    qjs_args_begin(e);
    return rc;
}

extern "C" double qjs_result_num(qjs_engine *e, int32_t *ok)
{
    if (!e->result_valid) { if (ok) *ok = 0; return 0; }
    double d = 0; int r = JS_ToFloat64(e->ctx, &d, e->result);
    if (ok) *ok = (r == 0);
    return d;
}
extern "C" int32_t qjs_result_int(qjs_engine *e, int32_t *ok)
{
    if (!e->result_valid) { if (ok) *ok = 0; return 0; }
    int32_t i = 0; int r = JS_ToInt32(e->ctx, &i, e->result);
    if (ok) *ok = (r == 0);
    return i;
}
extern "C" int32_t qjs_result_bool(qjs_engine *e) { return e->result_valid ? (JS_ToBool(e->ctx, e->result) ? 1 : 0) : 0; }
extern "C" const char *qjs_result_str(qjs_engine *e, int32_t *len)
{
    if (!e->result_valid) { if (len) *len = 0; return ""; }
    size_t l = 0; const char *s = JS_ToCStringLen(e->ctx, &l, e->result);
    if (!s) { if (len) *len = 0; return ""; }
    e->scratch.assign(s, l);
    JS_FreeCString(e->ctx, s);
    if (len) *len = (int32_t)e->scratch.size();
    return e->scratch.c_str();
}
extern "C" qjs_val qjs_result_val(qjs_engine *e)
{
    if (!e->result_valid) return 0;
    JSValue v = e->result;
    e->result = JS_UNDEFINED; e->result_valid = false;
    return slot_new(e, v);
}
extern "C" void qjs_result_drop(qjs_engine *e)
{
    if (!e->result_valid) return;
    JS_FreeValue(e->ctx, e->result);
    e->result = JS_UNDEFINED; e->result_valid = false;
}

/* ------------------------------------------------------------------ */
/* host functions                                                      */
/* ------------------------------------------------------------------ */

static JSValue host_trampoline(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, int magic)
{
    (void)this_val;
    qjs_engine *e = (qjs_engine *)JS_GetContextOpaque(ctx);
    if (!e || !e->host_fn) return JS_UNDEFINED;
    e->host_args.assign(argv, argv + argc);
    e->host_ret = JS_UNDEFINED;
    int32_t rc = e->host_fn(e, magic, argc, e->host_user);
    JSValue ret = e->host_ret;
    e->host_ret = JS_UNDEFINED;
    e->host_args.clear();
    if (rc != QJS_OK) {
        JS_FreeValue(ctx, ret);
        if (!JS_IsException(ret) && !e->has_error) e->error = "Error: host function failed";
        JS_ThrowInternalError(ctx, "%s", e->error.empty() ? "host function failed" : e->error.c_str());
        return JS_EXCEPTION;
    }
    return ret;
}

extern "C" void qjs_set_host_dispatch(qjs_engine *e, qjs_host_fn fn, void *user) { e->host_fn = fn; e->host_user = user; }

extern "C" qjs_val qjs_new_host_fn(qjs_engine *e, const char *name, int32_t name_len, int32_t fn_id, int32_t arity)
{
    (void)name_len;
    JSValue f = JS_NewCFunctionMagic(e->ctx, host_trampoline, name, (int)arity, JS_CFUNC_generic_magic, (int)fn_id);
    return slot_new(e, f);
}

extern "C" int32_t qjs_argc(qjs_engine *e) { return (int32_t)e->host_args.size(); }
extern "C" int32_t qjs_arg_type(qjs_engine *e, int32_t i)
{
    if (i < 0 || (size_t)i >= e->host_args.size()) return QJS_T_UNDEFINED;
    JSValue v = e->host_args[(size_t)i];
    if (JS_IsUndefined(v)) return QJS_T_UNDEFINED;
    if (JS_IsNull(v)) return QJS_T_NULL;
    if (JS_IsBool(v)) return QJS_T_BOOL;
    if (JS_IsNumber(v)) return QJS_T_NUMBER;
    if (JS_IsString(v)) return QJS_T_STRING;
    if (JS_IsBigInt(v)) return QJS_T_BIGINT;
    if (JS_IsSymbol(v)) return QJS_T_SYMBOL;
    if (JS_IsFunction(e->ctx, v)) return QJS_T_FUNCTION;
    if (JS_IsArray(v)) return QJS_T_ARRAY;
    if (JS_IsObject(v)) return QJS_T_OBJECT;
    return QJS_T_UNDEFINED;
}
extern "C" double qjs_arg_num(qjs_engine *e, int32_t i)
{
    if (i < 0 || (size_t)i >= e->host_args.size()) return 0;
    double d = 0; JS_ToFloat64(e->ctx, &d, e->host_args[(size_t)i]); return d;
}
extern "C" int32_t qjs_arg_int(qjs_engine *e, int32_t i)
{
    if (i < 0 || (size_t)i >= e->host_args.size()) return 0;
    int32_t n = 0; JS_ToInt32(e->ctx, &n, e->host_args[(size_t)i]); return n;
}
extern "C" int32_t qjs_arg_bool(qjs_engine *e, int32_t i)
{
    if (i < 0 || (size_t)i >= e->host_args.size()) return 0;
    return JS_ToBool(e->ctx, e->host_args[(size_t)i]) ? 1 : 0;
}
extern "C" const char *qjs_arg_str(qjs_engine *e, int32_t i, int32_t *len)
{
    if (i < 0 || (size_t)i >= e->host_args.size()) { if (len) *len = 0; return ""; }
    size_t l = 0; const char *s = JS_ToCStringLen(e->ctx, &l, e->host_args[(size_t)i]);
    if (!s) { if (len) *len = 0; return ""; }
    e->scratch.assign(s, l);
    JS_FreeCString(e->ctx, s);
    if (len) *len = (int32_t)e->scratch.size();
    return e->scratch.c_str();
}
extern "C" qjs_val qjs_arg_val(qjs_engine *e, int32_t i)
{
    if (i < 0 || (size_t)i >= e->host_args.size()) return 0;
    return slot_new(e, JS_DupValue(e->ctx, e->host_args[(size_t)i]));
}

extern "C" void qjs_ret_num(qjs_engine *e, double v) { e->host_ret = JS_NewFloat64(e->ctx, v); }
extern "C" void qjs_ret_int(qjs_engine *e, int32_t v) { e->host_ret = JS_NewInt32(e->ctx, v); }
extern "C" void qjs_ret_bool(qjs_engine *e, int32_t v) { e->host_ret = JS_NewBool(e->ctx, v != 0); }
extern "C" void qjs_ret_null(qjs_engine *e) { e->host_ret = JS_NULL; }
extern "C" void qjs_ret_undef(qjs_engine *e) { e->host_ret = JS_UNDEFINED; }
extern "C" void qjs_ret_str(qjs_engine *e, const char *s, int32_t len) { e->host_ret = JS_NewStringLen(e->ctx, s, (size_t)len); }
extern "C" void qjs_ret_val(qjs_engine *e, qjs_val v) { e->host_ret = JS_DupValue(e->ctx, slot_get(e, v)); }

/* ------------------------------------------------------------------ */
/* evaluation / bytecode                                               */
/* ------------------------------------------------------------------ */

extern "C" int32_t qjs_eval(qjs_engine *e, const char *src, int32_t len, const char *filename, int32_t flags)
{
    clear_error(e);
    if (e->result_valid) { JS_FreeValue(e->ctx, e->result); e->result = JS_UNDEFINED; e->result_valid = false; }
    int js_flags = JS_EVAL_TYPE_GLOBAL;
    if (flags & 1) js_flags = JS_EVAL_TYPE_MODULE;
    if (flags & 2) js_flags |= JS_EVAL_FLAG_STRICT;
    if (flags & 4) js_flags |= JS_EVAL_FLAG_COMPILE_ONLY;
    JSValue r = JS_Eval(e->ctx, src, (size_t)len, filename ? filename : "<eval>", js_flags);
    if (JS_IsException(r)) { JS_FreeValue(e->ctx, r); capture_exception(e); return QJS_ERR; }
    e->result = r; e->result_valid = true;
    return QJS_OK;
}

extern "C" int32_t qjs_eval_fn(qjs_engine *e, qjs_val fn)
{
    clear_error(e);
    JSValue f = slot_get(e, fn);
    JSValue r = JS_EvalFunction(e->ctx, JS_DupValue(e->ctx, f));
    if (JS_IsException(r)) { JS_FreeValue(e->ctx, r); capture_exception(e); return QJS_ERR; }
    if (e->result_valid) { JS_FreeValue(e->ctx, e->result); }
    e->result = r; e->result_valid = true;
    return QJS_OK;
}

extern "C" void qjs_pump_jobs(qjs_engine *e)
{
    JSContext *ctx = nullptr;
    int r;
    while ((r = JS_ExecutePendingJob(e->rt, &ctx)) > 0) {}
    if (r < 0 && ctx) {
        JSValue exc = JS_GetException(ctx);
        JS_FreeValue(ctx, exc);
    }
}

extern "C" uint8_t *qjs_compile(qjs_engine *e, const char *src, int32_t len, const char *filename,
                                int32_t flags, int32_t *out_len)
{
    clear_error(e);
    int js_flags = ((flags & 1) ? JS_EVAL_TYPE_MODULE : JS_EVAL_TYPE_GLOBAL) | JS_EVAL_FLAG_COMPILE_ONLY;
    JSValue fn = JS_Eval(e->ctx, src, (size_t)len, filename ? filename : "<eval>", js_flags);
    if (JS_IsException(fn)) { JS_FreeValue(e->ctx, fn); capture_exception(e); return nullptr; }
    size_t sz = 0;
    uint8_t *buf = JS_WriteObject(e->ctx, &sz, fn, JS_WRITE_OBJ_BYTECODE);
    JS_FreeValue(e->ctx, fn);
    if (!buf) { e->error = "bytecode serialization failed"; e->has_error = true; return nullptr; }
    if (out_len) *out_len = (int32_t)sz;
    return buf;
}

extern "C" int32_t qjs_run_bytecode(qjs_engine *e, const uint8_t *buf, int32_t len)
{
    clear_error(e);
    if (e->result_valid) { JS_FreeValue(e->ctx, e->result); e->result = JS_UNDEFINED; e->result_valid = false; }
    JSValue fn = JS_ReadObject(e->ctx, buf, (size_t)len, JS_READ_OBJ_BYTECODE);
    if (JS_IsException(fn)) { JS_FreeValue(e->ctx, fn); capture_exception(e); return QJS_ERR; }
    JSValue r = JS_EvalFunction(e->ctx, fn);
    if (JS_IsException(r)) { JS_FreeValue(e->ctx, r); capture_exception(e); return QJS_ERR; }
    e->result = r; e->result_valid = true;
    return QJS_OK;
}

extern "C" void qjs_free_bytes(qjs_engine *e, uint8_t *buf) { if (buf) js_free(e->ctx, buf); }

/* ------------------------------------------------------------------ */
/* errors                                                              */
/* ------------------------------------------------------------------ */

extern "C" int32_t qjs_has_exception(qjs_engine *e) { return e->has_error ? 1 : 0; }
extern "C" const char *qjs_take_error(qjs_engine *e, int32_t *len)
{
    if (len) *len = (int32_t)e->error.size();
    return e->error.c_str();
}
extern "C" void qjs_throw(qjs_engine *e, const char *msg, int32_t len)
{
    e->error.assign(msg, (size_t)(len > 0 ? len : (int32_t)strlen(msg)));
    e->has_error = true;
}

/* ------------------------------------------------------------------ */
/* modules (M2)                                                        */
/* ------------------------------------------------------------------ */

extern "C" void qjs_set_module_loader(qjs_engine *e, qjs_module_loader_fn fn, void *user)
{
    e->loader = fn; e->loader_user = user;
    /* The real loader lands with M2; until then importing a module throws a
       readable error instead of failing silently. */
}

/* ------------------------------------------------------------------ */
/* plain object / array helpers                                        */
/* ------------------------------------------------------------------ */

extern "C" qjs_val qjs_new_object(qjs_engine *e) { return slot_new(e, JS_NewObject(e->ctx)); }
extern "C" qjs_val qjs_new_array(qjs_engine *e, int32_t len)
{
    JSValue a = JS_NewArray(e->ctx);
    if (len > 0) { JSValue l = JS_NewInt32(e->ctx, len); JS_SetPropertyStr(e->ctx, a, "length", l); }
    return slot_new(e, a);
}
extern "C" int32_t qjs_array_length(qjs_engine *e, qjs_val arr)
{
    JSValue l = JS_GetPropertyStr(e->ctx, slot_get(e, arr), "length");
    int32_t n = 0; JS_ToInt32(e->ctx, &n, l); JS_FreeValue(e->ctx, l); return n;
}
extern "C" qjs_val qjs_json_parse(qjs_engine *e, const char *s, int32_t len)
{
    JSValue v = JS_ParseJSON(e->ctx, s, (size_t)len, "<json>");
    if (JS_IsException(v)) { JS_FreeValue(e->ctx, v); capture_exception(e); return 0; }
    return slot_new(e, v);
}
extern "C" const char *qjs_json_stringify(qjs_engine *e, qjs_val v, int32_t *len)
{
    JSValue str = JS_JSONStringify(e->ctx, slot_get(e, v), JS_UNDEFINED, JS_UNDEFINED);
    if (JS_IsException(str)) { JS_FreeValue(e->ctx, str); capture_exception(e); return ""; }
    size_t l = 0; const char *s = JS_ToCStringLen(e->ctx, &l, str);
    JS_FreeValue(e->ctx, str);
    if (!s) { if (len) *len = 0; return ""; }
    e->scratch.assign(s, l);
    JS_FreeCString(e->ctx, s);
    if (len) *len = (int32_t)e->scratch.size();
    return e->scratch.c_str();
}
