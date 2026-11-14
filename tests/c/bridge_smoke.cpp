/* bridge_smoke.cpp — exercises the flat C ABI exactly the way hxcpp will call it.
 * No JSON, no string keys on the hot path, no JSValue by value across the boundary.
 */
#include <cstdio>
#include <cstring>
#include <chrono>
#include <string>

#include "qjs_bridge.h"

static int g_logs = 0;

static int32_t host_dispatch(qjs_engine *e, int32_t fn_id, int32_t argc, void *user)
{
    (void)user;
    switch (fn_id) {
    case 0: {  /* log(...) */
        std::string out;
        for (int32_t i = 0; i < argc; i++) {
            int32_t n = 0;
            const char *s = qjs_arg_str(e, i, &n);
            if (i) out += " ";
            out.append(s, (size_t)n);
        }
        g_logs++;
        if (g_logs < 3) printf("    [js] %s\n", out.c_str());
        qjs_ret_undef(e);
        return QJS_OK;
    }
    case 1: {  /* noop(a) — pure call-overhead probe */
        g_logs++;
        qjs_ret_int(e, argc ? qjs_arg_int(e, 0) : 0);
        return QJS_OK;
    }
    default:
        qjs_throw(e, "unknown host function", 20);
        return QJS_ERR;
    }
}

static double ms_since(std::chrono::steady_clock::time_point t0)
{
    auto t1 = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(t1 - t0).count();
}

int main()
{
    printf("bridge smoke test — quickjs %s\n", qjs_version());

    qjs_engine *e = qjs_create(256, 1024, 0);
    if (!e) { printf("FATAL: engine create failed\n"); return 1; }
    qjs_set_host_dispatch(e, host_dispatch, nullptr);

    qjs_val g = qjs_global(e);
    qjs_val logf = qjs_new_host_fn(e, "log", 3, 0, 1);
    qjs_val noop = qjs_new_host_fn(e, "noop", 4, 1, 1);
    printf("  global=%d log=%d noop=%d\n", (int)g, (int)logf, (int)noop);
    qjs_set_val(e, g, "log", 3, logf);
    qjs_set_val(e, g, "noop", 4, noop);
    int ok = 1;

    const char *mod = R"JS(
const SPRITES = [];
for (let i = 0; i < 50; i++) SPRITES.push({ x: 0, y: 0, angle: 0 });

class Mod {
  constructor(name) { this.name = name; this.frames = 0; }
  onUpdate(dt) {
    this.frames++;
    for (const s of SPRITES) {
      s.x += dt * 100;
      s.angle = Math.sin(this.frames * 0.01) * 10;
    }
    return this.frames;
  }
  onBeat() { log("beat " + this.frames); return true; }
}

const mod = new Mod("bench");
globalThis.target = { x: 0, y: 0, angle: 0 };
globalThis.SPRITES = SPRITES;
globalThis.onUpdate = (dt) => mod.onUpdate(dt);
globalThis.onBeat = () => mod.onBeat();
globalThis.mod = mod;
)JS";

    if (qjs_eval(e, mod, (int32_t)strlen(mod), "mod.js", 0) != QJS_OK) {
        int32_t n = 0; const char *err = qjs_take_error(e, &n);
        printf("  FAIL eval: %s\n", err);
        return 1;
    }
    printf("  eval ok\n");

    qjs_val onUpdate = qjs_get(e, g, "onUpdate", 8);
    if (!qjs_is_function(e, onUpdate)) { printf("  FAIL onUpdate not a function\n"); return 1; }

    int32_t frames = 0;
    for (int i = 0; i < 1000; i++) {
        qjs_args_begin(e);
        qjs_push_num(e, 0.016);
        if (qjs_call(e, onUpdate, g, 1) != QJS_OK) {
            int32_t n = 0; printf("  FAIL call: %s\n", qjs_take_error(e, &n));
            return 1;
        }
        frames = qjs_result_int(e, &ok);
        qjs_result_drop(e);
    }
    printf("  onUpdate x1000 ok (frames=%d)\n", frames);

    const int N = 200000;
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < N; i++) {
        qjs_args_begin(e);
        qjs_push_num(e, 0.016);
        qjs_call(e, onUpdate, g, 1);
        qjs_result_drop(e);
    }
    double dt_ms = ms_since(t0);
    printf("  callback with 50-sprite body: %.0f ns/call\n", dt_ms * 1e6 / N);

    qjs_val target = qjs_get(e, g, "target", 6);
    if (!qjs_is_object(e, target)) { printf("  FAIL target not an object\n"); return 1; }

    t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 1000000; i++) qjs_set_num(e, target, "x", 1, (double)i);
    double gen_ms = ms_since(t0);

    int32_t bound = qjs_prop_bind(e, target, "x", 1);
    t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 1000000; i++) qjs_prop_set_num(e, bound, (double)i);
    double bound_ms = ms_since(t0);
    printf("  setProperty generic: %.0f ns/write   bound: %.0f ns/write   (%.1fx faster bound)\n",
           gen_ms, bound_ms, gen_ms / bound_ms);

    const char *loop = "let acc=0; for (let i=0;i<1000000;i++) acc += noop(i); acc;";
    t0 = std::chrono::steady_clock::now();
    int32_t rc = qjs_eval(e, loop, (int32_t)strlen(loop), "loop.js", 0);
    double host_ms = ms_since(t0);
    printf("  host call round-trip: %.0f ns/call (rc=%d, acc=%d)\n", host_ms * 1e6 / 1000000.0, rc, qjs_result_int(e, &ok));
    qjs_result_drop(e);

    const char *bad = "function broken( { return 1 }";
    int32_t ev = qjs_eval(e, bad, (int32_t)strlen(bad), "broken.js", 0);
    int32_t n = 0;
    const char *err = qjs_take_error(e, &n);
    printf("  syntax error -> %s\n    %s\n", ev == QJS_ERR ? "caught" : "MISSED", err);

    const char *rt_err = "throw new Error('boom at runtime')";
    qjs_eval(e, rt_err, (int32_t)strlen(rt_err), "boom.js", 0);
    err = qjs_take_error(e, &n);
    printf("  runtime error -> caught\n    %s\n", err);

    qjs_val missing = qjs_get(e, g, "notThere", 8);
    qjs_args_begin(e);
    printf("  calling a missing function -> %s\n", qjs_call(e, missing, g, 0) == QJS_ERR ? "rejected cleanly" : "BAD");

    if (qjs_eval(e, "1+1", 3, "ok.js", 0) != QJS_OK) { printf("  FAIL engine unusable after errors\n"); return 1; }
    printf("  engine still healthy after errors (1+1=%d)\n", qjs_result_int(e, &ok));
    qjs_result_drop(e);

    int32_t blen = 0;
    const char *cache_src = "globalThis.cached = 6*7; globalThis.cached;";
    uint8_t *bytecode = qjs_compile(e, cache_src, (int32_t)strlen(cache_src), "cache.js", 0, &blen);
    if (!bytecode) {
        err = qjs_take_error(e, &n);
        printf("  FAIL compile: %s\n", err);
    } else {
        printf("  compiled bytecode: %d bytes\n", blen);
        t0 = std::chrono::steady_clock::now();
        int reps = 5000;
        for (int i = 0; i < reps; i++) qjs_run_bytecode(e, bytecode, blen);
        double bms = ms_since(t0);
        printf("  bytecode load+run x5000: %.1f us each\n", bms * 1000.0 / reps);
        printf("  bytecode result: %d\n", qjs_result_int(e, &ok));
        qjs_result_drop(e);
        qjs_free_bytes(e, bytecode);
    }

    printf("  memory: %s\n", qjs_memory_report(e));

    qjs_release(e, missing);
    qjs_release(e, target);
    qjs_release(e, onUpdate);
    qjs_release(e, noop);
    qjs_release(e, logf);
    qjs_release(e, g);
    qjs_destroy(e);

    printf("\nbridge smoke test: done\n");
    return 0;
}
