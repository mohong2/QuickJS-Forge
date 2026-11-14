/* abi_compare.cpp — the cost of the argument-marshalling strategy, measured.
 *
 * Path A ("stringly" FFI, the shape used by the reference implementation):
 *   per call: JS_GetGlobalObject + JS_GetPropertyStr(name) + JS_ParseJSON(args)
 *             + JS_GetPropertyUint32 per argument + JS_Call + frees
 * Path B (this project): cached function handle + preallocated argument stack.
 */
#include <cstdio>
#include <cstring>
#include <chrono>
#include "qjs_bridge.h"

static double ms_since(std::chrono::steady_clock::time_point t0)
{
    auto t1 = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(t1 - t0).count();
}

int main()
{
    printf("ABI cost comparison — quickjs %s\n", qjs_version());
    qjs_engine *e = qjs_create(64, 512, 0);
    qjs_val g = qjs_global(e);

    const char *seed =
        "globalThis.tick = function(dt){ return dt * 2; };"
        "globalThis.acc = 0;";
    if (qjs_eval(e, seed, (int32_t)strlen(seed), "seed.js", 0) != QJS_OK) {
        int32_t n = 0; printf("seed failed: %s\n", qjs_take_error(e, &n)); return 1;
    }

    const int N = 300000;
    int32_t ok = 0;

    /* ---- Path B: ours ---- */
    qjs_val fn = qjs_get(e, g, "tick", 4);
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < N; i++) {
        qjs_args_begin(e);
        qjs_push_num(e, 0.016);
        qjs_call(e, fn, g, 1);
        qjs_result_drop(e);
    }
    double ours = ms_since(t0);

    /* ---- Path A: stringly, as in the decompiled bridge ---- */
    /* run it through the bridge's own eval so we use the same engine/context */
    t0 = std::chrono::steady_clock::now();
    const char *loop =
        "globalThis.__tick = globalThis.tick;"
        "let s = 0;"
        "for (let i = 0; i < 300000; i++) s += globalThis.__tick(0.016);"
        "globalThis.__s = s;";
    /* equivalent of: parse a JSON argument array and call through a name lookup */
    qjs_eval(e, "globalThis.__json = '[0.016]';", 28, "j.js", 0);
    qjs_val jsn = qjs_get(e, g, "__json", 6);
    int32_t n = 0;
    const char *json = qjs_to_string(e, jsn, &n);

    t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < N; i++) {
        qjs_val arr = qjs_json_parse(e, json, n);
        int32_t len = qjs_array_length(e, arr);
        qjs_val a0 = qjs_get_index(e, arr, 0);
        qjs_args_begin(e);
        qjs_push_val(e, a0);
        qjs_val f = qjs_get(e, g, "tick", 4);
        qjs_call(e, f, g, len);
        qjs_result_drop(e);
        qjs_release(e, f);
        qjs_release(e, a0);
        qjs_release(e, arr);
    }
    double theirs = ms_since(t0);

    printf("  stringly FFI (JSON args + name lookup): %6.0f ns/call\n", theirs * 1e6 / N);
    printf("  handle FFI   (cached fn + arg stack):   %6.0f ns/call\n", ours * 1e6 / N);
    printf("  ratio: %.1fx\n", theirs / ours);

    ok = 1;
    qjs_release(e, jsn);
    qjs_release(e, fn);
    qjs_release(e, g);
    qjs_destroy(e);
    return ok ? 0 : 1;
}
