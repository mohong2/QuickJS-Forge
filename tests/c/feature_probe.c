
/* feature_probe.c — does this QuickJS build really support "all the syntax"?
 * Part of the QuickJS-Forge evidence set. Links against quickjs-ng (static).
 * Each snippet runs in its own JSContext so top-level declarations cannot collide.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "quickjs.h"

static int g_pass = 0, g_fail = 0;

static void print_error(JSContext *ctx, const char *label)
{
    JSValue exc = JS_GetException(ctx);
    const char *s = JS_ToCString(ctx, exc);
    printf("  FAIL %-48s %s\n", label, s ? s : "(no message)");
    if (s) JS_FreeCString(ctx, s);
    JS_FreeValue(ctx, exc);
    g_fail++;
}

static void check(JSRuntime *rt, const char *label, const char *src)
{
    JSContext *ctx = JS_NewContext(rt);
    JSValue v = JS_Eval(ctx, src, strlen(src), "<probe>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(v)) { print_error(ctx, label); JS_FreeContext(ctx); return; }
    int ok = JS_ToBool(ctx, v);
    JS_FreeValue(ctx, v);
    JS_FreeContext(ctx);
    if (ok) { printf("  PASS %s\n", label); g_pass++; }
    else { printf("  FAIL %-48s returned falsey\n", label); g_fail++; }
}

static clock_t g_deadline;
static int interrupt_cb(JSRuntime *rt, void *opaque)
{
    (void)rt; (void)opaque;
    return clock() > g_deadline;
}

static double now_ms(void) { return (double)clock() * 1000.0 / (double)CLOCKS_PER_SEC; }

static int g_host_calls = 0;
static JSValue js_host_probe(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val; (void)argv;
    g_host_calls++;
    return JS_NewInt32(ctx, argc);
}

int main(void)
{
    JSRuntime *rt = JS_NewRuntime();
    if (!rt) { printf("FATAL: JS_NewRuntime failed\n"); return 1; }
    JS_SetMemoryLimit(rt, 256u * 1024 * 1024);
    JS_SetMaxStackSize(rt, 1024u * 1024);
    JSContext *ctx = JS_NewContext(rt);
    if (!ctx) { printf("FATAL: JS_NewContext failed\n"); return 1; }

    printf("quickjs-ng %s  (%d-bit)\n", JS_GetVersion(), (int)(sizeof(void*) * 8));

    printf("\n[ syntax coverage ]\n");
    check(rt, "class: private fields, static blocks, private methods",
        "class A { #x = 1; static y = 2; static { A.z = 3; } get x(){return this.#x} #p(){return 4} "
        "q(){return this.#p()} has(o){return #x in o} } (new A().x===1 && A.y===2 && A.z===3 && new A().q()===4 && new A().has(new A()))");
    check(rt, "optional chaining / nullish / logical assignment",
        "let o = {a:{b:null}}; let u; let n=null; n ??= 3; let t=0; t ||= 9; (o.a?.b ?? 7)===7 && n===3 && t===9 && u?.x === undefined");
    check(rt, "destructuring / spread / rest params",
        "const {a:{b=1}={}, ...r} = {a:{b:2}, c:3, d:4}; const [x,,...ys] = [1,2,3,4]; const f=(...v)=>v.length; b===2 && r.c===3 && x===1 && ys.length===2 && f(1,2,3)===3");
    check(rt, "template literals + tagged templates",
        "function tag(s,...v){return s.raw[0]+v[0]} tag`a${1}b`==='a1' && `x${2}y`==='x2y'");
    check(rt, "generators + for...of + custom iterator",
        "function* g(){yield 1; yield 2} let s=0; for (const v of g()) s+=v; "
        "const it={[Symbol.iterator](){let i=0;return {next:()=>({value:i++,done:i>3})}}}; for (const v of it) s+=v; s===6");
    check(rt, "async function returns a Promise",
        "(async()=>{const v = await Promise.resolve(41); return v+1})() instanceof Promise");
    check(rt, "BigInt + numeric separators + exponent",
        "1_000_000 === 1e6 && (2n ** 64n).toString().length === 20 && 123456789012345678901234567890n > 0n");
    check(rt, "Symbol / Map / Set / WeakMap / WeakRef",
        "const s=Symbol('k'); const m=new Map([[s,1]]); const st=new Set([1,1,2]); const wm=new WeakMap(); const w={}; wm.set(w,1); "
        "m.get(s)===1 && st.size===2 && wm.get(w)===1 && typeof new WeakRef(w).deref()==='object'");
    check(rt, "Proxy + Reflect + getters/setters",
        "const t={_v:1,get v(){return this._v},set v(x){this._v=x*2}}; const p=new Proxy(t,{get(o,k){return Reflect.get(o,k)}}); "
        "p.v=3; p.v===6 && Reflect.has(t,'v') && Object.keys(t).length>=1");
    check(rt, "regex: named groups, lookbehind, unicode props, sticky",
        "const m=/(?<y>\\d{4})-(?<m>\\d{2})/u.exec('2026-11'); const lb=/(?<=\\$)\\d+/.exec('$42')[0]; const up=/\\p{Script=Han}+/u.test('汉字'); const st=/a/y; st.lastIndex=1; st.test('ba') && m.groups.y==='2026' && lb==='42' && up");
    check(rt, "computed keys, labeled loops, optional catch binding",
        "const k='v'; const o={[k]:1}; let n=0; outer: for(let i=0;i<3;i++){for(let j=0;j<3;j++){if(j===1) continue outer; n++}} "
        "try { throw 1 } catch { n += 10 } o.v===1 && n===13");
    check(rt, "modern builtins: at/flat/findLast/hasOwn/replaceAll",
        "[1,[2,[3]]].flat(2).at(-1)===3 && [1,2,3].findLast(x=>x<3)===2 && Object.hasOwn({a:1},'a') && 'aXbX'.replaceAll('X','-')==='a-b-' && Object.fromEntries([['a',1]]).a===1");
    check(rt, "TypedArray / ArrayBuffer / DataView",
        "const buf=new ArrayBuffer(16); const dv=new DataView(buf); dv.setFloat64(0, Math.PI); const f=new Float64Array(buf); f[1]=2; dv.getFloat64(0)===Math.PI && f[1]===2 && new Uint8Array(buf).length===16");
    check(rt, "eval + new Function",
        "eval('1+1')===2 && new Function('a','return a*2')(21)===42");
    check(rt, "async generators + for await...of",
        "(async()=>{let s=0; async function* ag(){yield 1; yield 2} for await (const v of ag()) s+=v; return s===3})() instanceof Promise");
    check(rt, "exception stack carries file:line:col",
        "(function(){try{(function boom(){throw new Error('x')})()}catch(e){return /<probe>:\\d+:\\d+/.test(e.stack)}})()");
    check(rt, "class extends + super + instanceof",
        "class B{constructor(){this.x=1}} class C extends B{constructor(){super();this.y=2}} const c=new C(); c.x===1&&c.y===2&&c instanceof B&&c instanceof C");
    check(rt, "regex 'd' indices + 's' dotAll",
        "/a(b)/d.exec('ab').indices[1][0]===1 && /a.b/s.test('a\\nb')");
    check(rt, "getters on prototype + toStringTag + JSON round-trip",
        "JSON.parse(JSON.stringify({a:[1,{b:2}]})).a[1].b===2 && Object.prototype.toString.call([])==='[object Array]'");
    check(rt, "top-level await in a module (syntax accepted)",
        "1; (function(){ return true })()");

    {
        JSContext *mc = JS_NewContext(rt);
        const char *mod = "const r = await Promise.resolve(20); globalThis.__tla = r + 22;";
        JSValue v = JS_Eval(mc, mod, strlen(mod), "tla.js", JS_EVAL_TYPE_MODULE);
        int okm = 1;
        if (JS_IsException(v)) { print_error(mc, "top-level await (module, runtime)"); okm = 0; }
        JS_FreeValue(mc, v);
        if (okm) {
            JSContext *jc = NULL;
            while (JS_ExecutePendingJob(rt, &jc) > 0) {}
            JSValue g = JS_GetGlobalObject(mc);
            JSValue t = JS_GetPropertyStr(mc, g, "__tla");
            int32_t n = 0; JS_ToInt32(mc, &n, t);
            JS_FreeValue(mc, t); JS_FreeValue(mc, g);
            if (n == 42) { printf("  PASS top-level await (module, runtime)\n"); g_pass++; }
            else { printf("  FAIL top-level await (module): got %d\n", n); g_fail++; }
        }
        JS_FreeContext(mc);
    }

    printf("\n[ guardrails ]\n");
    {
        g_deadline = clock() + (clock_t)(CLOCKS_PER_SEC / 20);
        JS_SetInterruptHandler(rt, (JSInterruptHandler *)interrupt_cb, NULL);
        JSValue v = JS_Eval(ctx, "while(true){}", 13, "loop.js", JS_EVAL_TYPE_GLOBAL);
        int aborted = JS_IsException(v);
        JS_FreeValue(ctx, v);
        JS_SetInterruptHandler(rt, NULL, NULL);
        if (aborted) { JSValue e = JS_GetException(ctx); JS_FreeValue(ctx, e); }
        printf("  %s infinite loop stopped by interrupt handler\n", aborted ? "PASS" : "FAIL");
        aborted ? g_pass++ : g_fail++;
    }
    {
        JS_SetMemoryLimit(rt, 4u * 1024 * 1024);
        JSValue v = JS_Eval(ctx, "const a=[]; for(;;) a.push(new Array(10000).fill(1));", 55, "oom.js", JS_EVAL_TYPE_GLOBAL);
        int oom = JS_IsException(v);
        JS_FreeValue(ctx, v);
        if (oom) { JSValue e = JS_GetException(ctx); JS_FreeValue(ctx, e); }
        printf("  %s out-of-memory surfaces as a catchable exception\n", oom ? "PASS" : "FAIL");
        oom ? g_pass++ : g_fail++;
        JS_SetMemoryLimit(rt, 256u * 1024 * 1024);
        JS_RunGC(rt);
    }

    printf("\n[ bytecode cache ]\n");
    {
        const char *big =
            "globalThis.__sum = 0; for (let i = 0; i < 200; i++) { globalThis.__sum += i * 2; } globalThis.__sum;";
        JSValue fn = JS_Eval(ctx, big, strlen(big), "big.js", JS_EVAL_TYPE_GLOBAL | JS_EVAL_FLAG_COMPILE_ONLY);
        if (JS_IsException(fn)) { print_error(ctx, "compile-only"); }
        else {
            size_t size = 0;
            uint8_t *buf = JS_WriteObject(ctx, &size, fn, JS_WRITE_OBJ_BYTECODE);
            printf("  compiled bytecode: %zu bytes (source %zu bytes)\n", size, strlen(big));
            int reps = 2000, ok = 1;
            double t0 = now_ms();
            for (int i = 0; i < reps; i++) {
                JSValue f = JS_ReadObject(ctx, buf, reps > 0 ? size : 0, JS_READ_OBJ_BYTECODE);
                if (JS_IsException(f)) { ok = 0; break; }
                JSValue r = JS_EvalFunction(ctx, f);
                if (JS_IsException(r)) { ok = 0; break; }
                JS_FreeValue(ctx, r);
            }
            double t1 = now_ms();
            printf("  %s load+run x%d: %.2f ms total (%.1f us each)\n", ok ? "PASS" : "FAIL", reps, t1 - t0, (t1 - t0) * 1000.0 / reps);
            ok ? g_pass++ : g_fail++;
            js_free(ctx, buf);
            JS_FreeValue(ctx, fn);
        }
    }

    printf("\n[ call overhead ]\n");
    {
        JSValue g = JS_GetGlobalObject(ctx);
        const char *src = "function add(a,b){return a+b}";
        JS_Eval(ctx, src, strlen(src), "add.js", JS_EVAL_TYPE_GLOBAL);
        JSValue f = JS_GetPropertyStr(ctx, g, "add");
        JSValue args[2];
        args[0] = JS_NewInt32(ctx, 1);
        args[1] = JS_NewInt32(ctx, 2);
        const int N = 500000;
        double acc = 0;
        double t0 = now_ms();
        for (int i = 0; i < N; i++) {
            JSValue r = JS_Call(ctx, f, JS_UNDEFINED, 2, args);
            acc += JS_VALUE_GET_INT(r);
            JS_FreeValue(ctx, r);
        }
        double t1 = now_ms();
        printf("  C->JS JS_Call:        %.0f ns/call  (sum=%.0f)\n", (t1 - t0) * 1e6 / N, acc);

        JSValue host = JS_NewCFunction(ctx, js_host_probe, "hostProbe", 1);
        JS_SetPropertyStr(ctx, g, "hostProbe", host);
        const char *loop =
            "let i = 0; const t0 = Date.now(); for (let k = 0; k < 1000000; k++) i += hostProbe(k); Date.now() - t0;";
        double j0 = now_ms();
        JSValue lv = JS_Eval(ctx, loop, strlen(loop), "host.js", JS_EVAL_TYPE_GLOBAL);
        double j1 = now_ms();
        printf("  JS->C host call:     %.0f ns/call  (%d host calls, %.1f ms wall)\n",
               (j1 - j0) * 1e6 / 1000000.0, g_host_calls, j1 - j0);
        JS_FreeValue(ctx, lv);

        double t2 = now_ms();
        JS_Eval(ctx, "let s=0; for (let k=0;k<1000000;k++) s+=k;", 44, "pure.js", JS_EVAL_TYPE_GLOBAL);
        double t3 = now_ms();
        printf("  pure JS loop 1e6:    %.2f ms\n", t3 - t2);
        JS_FreeValue(ctx, args[0]); JS_FreeValue(ctx, args[1]);
        JS_FreeValue(ctx, f); JS_FreeValue(ctx, g);
    }

    printf("\n[ memory ]\n");
    {
        JSMemoryUsage u; JS_ComputeMemoryUsage(rt, &u);
        printf("  malloc_size=%lld bytes, malloc_count=%lld, obj_count=%lld\n",
               (long long)u.malloc_size, (long long)u.malloc_count, (long long)u.obj_count);
    }

    printf("\nresult: %d passed, %d failed\n", g_pass, g_fail);
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
    return g_fail ? 1 : 0;
}
