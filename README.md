# QuickJS-Forge

**在 Haxe / hxcpp 项目里嵌入 JavaScript：完整 ES2023、静态链接、可预测的开销。**

![version](https://img.shields.io/badge/version-0.1.0-blue)
![license](https://img.shields.io/badge/license-MIT-green)
![platforms](https://img.shields.io/badge/platforms-Windows%20%7C%20Linux%20%7C%20macOS-lightgrey)

QuickJS-Forge 是一个原生桥 + 一个 Haxe 绑定 + 一个 Psych Engine 适配层。它把 [quickjs-ng](https://github.com/quickjs-ng/quickjs) 静态编进你的程序，让脚本调用和宿主回调都保持在几十纳秒的量级，而不是每次调用都序列化一次 JSON。

```
┌─────────────────────────────────────────────┐
│ 你的游戏（hxcpp / Psych Engine）             │
│   psychjs：265 个与 FunkinLua 同名的 API     │
├─────────────────────────────────────────────┤
│ hxquickjs：JsContext / JsValue / Marshal     │
├─────────────────────────────────────────────┤
│ qjs_bridge：扁平 C ABI（int32 / double / *）  │
├─────────────────────────────────────────────┤
│ quickjs-ng 0.18（静态库）                    │
└─────────────────────────────────────────────┘
```

## 目录

- [特性](#特性)
- [安装](#安装)
- [快速开始](#快速开始)
- [Psych Engine 适配](#psych-engine-适配)
- [API](#api)
- [性能](#性能)
- [设计要点](#设计要点)
- [平台支持](#平台支持)
- [常见问题](#常见问题)
- [许可](#许可)

## 特性

- **完整语法**：ES2023。类与私有字段、静态块、生成器、`async`/`await`、async generator、BigInt、Proxy、TypedArray、正则命名组与后行断言、顶层 await。
- **低开销**：C→JS 调用 34–38 ns，JS→C 回调 82–110 ns，`setProperty` 19–21 ns（预绑定 12–13 ns）。
- **静态链接**：Windows 下与 hxcpp 同为 `/MT`，不引入 DLL、不需要 MinGW 运行时、不用配搜索路径。
- **字节码缓存**：首次编译写 `.qjsc`，之后 0.5–0.8 µs 直接加载执行。
- **运行护栏**：帧预算中断（默认 4 ms/脚本/帧）、内存上限、栈上限、连续异常熔断。
- **脚本是给人改的**：没有加密、没有完整性校验、没有签名检查。

## 安装

### 从源码构建

依赖：CMake ≥ 3.20、C11/C++17 编译器（Windows 用 VS 2022 Build Tools）、Git。

也可以直接下载 [Releases](https://github.com/mohong2/QuickJS-Forge/releases) 里的预编译包：解压后是 `lib/`（静态库）+ `include/`（头文件），附带 `SHA256SUMS.txt`。

```bash
git clone --recursive <repo>
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel 4
ctest --test-dir build -C Release --output-on-failure
```

构建产物：

| 文件 | 说明 |
|---|---|
| `libqjs.a` / `qjs.lib` | quickjs-ng 静态库 |
| `libqjs_bridge.a` / `qjs_bridge.lib` | 扁平 C ABI 层 |
| `feature_probe` / `bridge_smoke` / `abi_compare` | 语法探测与基准程序，`ctest` 会跑它们 |

也可以只编引擎：

```powershell
pwsh tools/build-quickjs.ps1     # Windows -> native/bin/windows-x64/qjs.lib
./tools/build-quickjs.sh         # Linux / macOS -> native/bin/<platform>/libqjs.a
```

### 作为 haxelib

本仓库包含两个包，`packages/hxquickjs` 与 `packages/psychjs`。开发时直接指向目录：

```bash
haxelib dev hxquickjs packages/hxquickjs
haxelib dev psychjs   packages/psychjs
```

在你的 `Project.xml` 里：

```xml
<haxelib name="hxquickjs" />
<include name="${haxelib:hxquickjs}/native/Build.xml" />
```

`Build.xml` 会把 `qjs_bridge.cpp` 编进工程，并按平台链接对应的静态库。没有预编译库时，也可以让它直接编译 `native/quickjs` 的 C 源（首次约 60–90 秒）。

## 快速开始

```haxe
import quickjs.JsContext;

var js = new JsContext();                 // 64 MB 内存上限、1 MB 栈

// 把 Haxe 函数暴露给 JS
js.addFunction("log", args -> {
	trace(args.join(" "));
	return null;
});

// 定义脚本可用的宿主对象
js.addFunction("getPlayerX", args -> 320.0);
js.addFunction("setPlayerX", args -> {
	player.x = args[0];
	return null;
});

// 跑脚本
var src = [
	"log('ready');",
	"log('player at ' + getPlayerX());",
	"setPlayerX(410);"
].join("\n");
js.eval(src, "mods/demo/scripts/main.js");

// 调用脚本里的函数
js.eval("function onUpdate(dt) { setPlayerX(getPlayerX() + dt * 10); }");
js.callGlobal("onUpdate", [0.016]);

js.dispose();
```

每帧回调的推荐写法（避免重复查找全局函数）：

```haxe
var onUpdate:Int = js.getHandle("onUpdate");   // 句柄，长期持有
js.callHandle(onUpdate, [elapsed], js.global());
```

## Psych Engine 适配

`psychjs` 把 `FunkinLua` 的 API 面平移到 JavaScript：函数同名、参数同序、回调同名。函数清单可以从任意 `FunkinLua.hx` 抽出来核对：

```bash
node tools/gen-api-matrix.js "<Psych>/source/psychlua/FunkinLua.hx|psych-1.0" \
                             "<Fork>/source/script/lua/FunkinLua.hx|psych-0.6.3"
```

0.6.3 系分支注册 265 个函数，Psych 1.0 系 125 个且是前者子集。

```js
// mods/mymod/scripts/script.js
function onCreate() {
	makeLuaSprite("bg", "bg", -600, -200);
	setProperty("bg.alpha", 0.8);
	addLuaSprite("bg");
}

function onUpdate(elapsed) {
	setProperty("bg.x", getProperty("bg.x") + elapsed * 10);
}

function onBeatHit() {
	doTweenAngle("spin", "bg", 360, 0.4, "quadInOut");
}
```

平移规则：

| 规则 | 说明 |
|---|---|
| 同名 | `makeLuaSprite` 仍叫 `makeLuaSprite`，老 mod 改语法不改名字 |
| 别名 | 另提供 `sprite.setPosition(x, y)` 这类写法，但不替代原名 |
| 空值 | Lua 的 `nil` → JS 的 `undefined` |
| 可变参数 | Lua 的 `...` → rest 参数 |
| 数字 | 统一为 `Float`（QuickJS 只有一种数字） |
| 回调 | 与 `FunkinLua` 同名同参，`Function_Stop` 等常量映射为同名全局 |

每个脚本一个 `JsContext`：一个脚本崩溃不影响其他脚本，连续 3 次未捕获异常后自动停用并在控制台打印脚本路径。

## API

### JsContext

| 成员 | 说明 |
|---|---|
| `new JsContext(?memoryMb, ?stackKb)` | 创建一个运行时 |
| `eval(src, ?filename, ?asModule)` | 执行脚本，返回最后一个表达式的值 |
| `evalBytes(bytecode, len, ?filename)` | 执行预编译字节码 |
| `callGlobal(name, args)` | 按名字调用全局函数 |
| `callHandle(handle, args, ?thisArg)` | 按句柄调用（热路径） |
| `getHandle(name)` / `getGlobal()` | 取得句柄，长期持有 |
| `addFunction(name, fn, ?arity)` | 注册宿主函数 |
| `tickFrame(dt)` | 每帧调用：泵送 Promise 微任务 |
| `frameBudgetMs` | 单脚本单帧 CPU 预算，默认 4 ms |
| `dispose()` | 释放运行时 |

### 值编组

| Haxe | JavaScript |
|---|---|
| `null` | `null` / `undefined` |
| `Bool` | boolean |
| `Int` / `Float` | number |
| `String` | string（传入时借用，不拷贝） |
| `Array<Dynamic>` | Array |
| 匿名结构 | 普通对象（逐字段） |
| 函数 | 只能通过 `addFunction` 注册 |

嵌套深度上限 32，超出报 `JsError`，避免自引用结构把游戏挂死。

### 错误

所有 JS 异常包装成 `quickjs.JsError`：

```haxe
try {
	js.eval(src, path);
} catch (e:JsError) {
	trace(e.describe());   // mods/x/script.js:12:5 — TypeError: foo is not a function
	trace(e.stack);
}
```

错误不会污染引擎：记录后下一个回调照常执行。

## 性能

环境：AMD Ryzen 5 5600 / Windows 11 / MSVC 19.44 / quickjs-ng 0.18 / `-O2` / 静态 CRT。每个数字是循环 N 次的平均值，跑 3 轮取区间。

| 场景 | 结果 | 折算 |
|---|---|---|
| C → JS 调用（无参） | 34–38 ns | 60 fps 下每帧可做 43 万次 |
| C → JS 调用（1 个 double 参数） | 47–53 ns | — |
| JS → C 回调（含分发） | 82–110 ns | 每帧 1000 次 ≈ 0.1 ms |
| `setProperty`（字符串键） | 19–21 ns | 200 次/帧 ≈ 4.2 µs |
| `setProperty`（预绑定） | 12–13 ns | 200 次/帧 ≈ 2.6 µs |
| 50 精灵的 `onUpdate`（含 50 次 `Math.sin`） | 8.9 µs | 帧预算的 0.05% |
| 字节码 加载 + 执行 | 0.5–0.8 µs | 100 个脚本冷启动 ≈ 65 µs |

复现：

```bash
ctest --test-dir build -C Release --output-on-failure
# 或单独跑
./build/Release/feature_probe    # 语法探测
./build/Release/bridge_smoke     # 桥与基准
./build/Release/abi_compare      # 传参方式对照
```

### 对照基线

**A. 字符串编组 FFI（常见做法）**：每次调用 `JS_GetGlobalObject` → `JS_GetPropertyStr(name)` → `JS_ParseJSON(args)` → 逐个 `JS_GetPropertyUint32` → `JS_Call`。

| 路径 | ns/次 |
|---|---|
| 字符串编组 + 名字查找 | 2605–2702 |
| 句柄 + 参数栈（本项目） | 47–53 |

约 50 倍。差距不来自微优化，来自「要不要在每次调用时创建一个字符串再解析它」。

**B. 某移动端 FNF 引擎的逆向开源构建**（未具名）

这个基线常被当作「QuickJS 引擎」引用，所以单独列一行：

| 项目 | 结果 |
|---|---|
| Windows 上是否执行 JavaScript | 否。其构建说明写明：只绑定指针/void 原语，`JS_Eval` / `JS_Call` 的返回值经过通用 FFI 会破坏 x64 返回缓冲区，脚本执行由内置的 Haxe 子集解释器完成 |
| 语法覆盖 | 子集：函数、`var/let/const`、`if/else`、`for/while`、数组、对象、成员访问、JSON、Math、console、字符串/数组方法。类、生成器、`async`、BigInt、Proxy、TypedArray、模板字符串、解构、展开、正则均不在清单内 |
| 单次调用成本 | 不适用：Windows 路径不经过 QuickJS，没有可比的 JS 调用基准 |
| 运行时依赖 | 预编译 MinGW DLL + `libgcc_s_seh-1`、`libwinpthread-1`、`libmcfgthread-1` |
| 构建复现 | 需自行获取约 380 MB 的 assets |

所以这一行没有分数。**不是没测，是没有可测的对象**——它的 Windows 构建里，JavaScript 并没有交给 JavaScript 引擎执行。名字留空：这份对照只对事，不对项目。

### 边界

- 纯计算不是强项：quickjs 是解释器，没有 JIT。`for (let i=0;i<1e7;i++)` 这类循环比 V8 慢一个数量级。脚本的时间应该花在调用宿主 API 上。
- 大脚本首次编译约 30–60 ms（1 MB 级），所以有字节码缓存。
- 字符串拼接仍是分配大头；编组层对 `String` 走借用，避免二次拷贝。
- 以上是一台开发机的数字，不是对任何设备的承诺。

## 设计要点

### 为什么不用「每次调用序列化 JSON」

`JSValue` 在默认配置下是 64 位 NaN-boxing 值，`JS_Call` 按值返回它。hxcpp 的通用 FFI（`cpp.Lib.load` + `Dynamic`）只可靠处理指针和整数；让返回值走通用 FFI 会破坏 x64 返回寄存器约定。于是常见做法是「参数序列化成字符串，C 侧再解析」——能跑，但一次 2.6 µs 起，还丢掉类型、引用和异常。

本项目跨边界只传 `int32` / `double` / `const char*`：

- **句柄**（`int32`）表示长期持有的值，引擎内部 `slots[]` 持强引用，free-list 回收。
- **参数栈**表示一次性值：`qjs_args_begin` → `qjs_push_*` → `qjs_call` → `qjs_result_*` → `qjs_result_drop`，热路径零分配。
- **`qjs_push_own`** 把句柄的引用搬进参数栈，省一次 `JS_DupValue`。

### 属性预绑定

`qjs_prop_bind(obj, "x")` 把「对象 + 属性名」解析成 `(JSValue, JSAtom)` 存住：

- 省掉每次写入的「字符串 → atom」哈希；
- 省掉每次写入的对象取值与引用计数抖动；
- 普通数据属性直接落 shape。

19–21 ns → 12–13 ns。对每帧写 200 个属性的 modchart，是 4.2 µs 与 2.6 µs 的差别。

### 字节码缓存

```
scripts/foo.js
   │ 首次：JS_Eval(COMPILE_ONLY) → JS_WriteObject(BYTECODE)
   ▼
<userdata>/quickjs-cache/<engine-ver>/<sha256(src)>.qjsc
   │ 之后：JS_ReadObject → JS_EvalFunction
   ▼
执行
```

缓存键含引擎版本与编译标志；源码哈希变化即失效；损坏的缓存直接丢弃重编。

### 运行护栏

| 护栏 | 默认 | 机制 |
|---|---|---|
| 单脚本单帧预算 | 4 ms | `JS_SetInterruptHandler` 到点中断 |
| 单运行时内存 | 64 MB | `JS_SetMemoryLimit`，超限抛可捕获异常 |
| 栈深度 | 1 MB | `JS_SetMaxStackSize` |
| 异常熔断 | 连续 3 次 | 停用该脚本并打印路径 |

## 平台支持

| 平台 | 工具链 | 运行库 | CI |
|---|---|---|---|
| Windows | MSVC 19.3x+ | `/MT` 静态 CRT（与 hxcpp 一致） | windows-latest |
| Linux | GCC 10+ / Clang 12+ | 默认 | ubuntu-latest |
| macOS | Apple Clang | libc++ | macos-latest |
| Android | NDK r25+ | `c++_static` | 交叉编译可用，未进 CI |

Android 交叉编译：

```bash
cmake -S native/quickjs -B build-android \
  -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-21 \
  -DBUILD_SHARED_LIBS=OFF
cmake --build build-android --target qjs -j
```

产物放到 `native/bin/android-arm64-v8a/libqjs.a`，`native/Build.xml` 会按 `ANDROID_ARCH` 找它。

## 目录结构

```
native/
  bridge/       qjs_bridge.h（契约）/ qjs_bridge.cpp（实现）
  quickjs/      quickjs-ng（pinned submodule，不修改）
  Build.xml     hxcpp 构建片段
packages/
  hxquickjs/    JsContext / JsValue / Marshal / JsError
  psychjs/      ScriptHost / PsychApi / Callbacks
tests/c/        feature_probe / bridge_smoke / abi_compare
tools/          build-quickjs.ps1 / .sh、gen-api-matrix.js
CMakeLists.txt  三平台构建入口
```

## 常见问题

**链接时报 `LNK2019: __imp_realloc`**
QuickJS 与工程的 CRT 不一致。用 `tools/build-quickjs.*` 重新编，别手工改 `CMAKE_MSVC_RUNTIME_LIBRARY`。

**链接时报 `LNK1181: 无法打开输入文件 ".obj"`**
批处理里把 `LIB` 环境变量覆盖了（MSVC 自己也用这个名字）。改成 `QJSLIB` 之类的变量名。

**`fatal error: quickjs.h: No such file`**
没拉 submodule：`git submodule update --init --recursive`。

**`cl.exe not found`**
没进开发者命令行。跑 `vcvars64.bat`，或用 `tools/build-quickjs.ps1` 自动探测。

**脚本跑着跑着报 out of memory**
单脚本内存上限默认 64 MB。`new JsContext(memoryMb)` 调大，或者检查脚本里的循环引用。

**支持 TypeScript 吗**
不直接支持。先用 `tsc` 或 esbuild 编成 ESM/CJS，再交给 `eval` 或 `require`。

## 参与贡献

- 改 ABI：同步改 `native/bridge/qjs_bridge.h`、`packages/hxquickjs/hx/quickjs/Qjs.hx` 和本 README 的「设计要点」。
- 改性能相关代码：附上 `abi_compare` 或 `bridge_smoke` 的前后数字。
- 新增 Psych API：保持与 `FunkinLua` 同名同参，并在 README 的平移规则里补一行差异说明。
- 提交信息：`<scope>: <what changed>`，正文写清为什么改、量了什么。

## 许可

MIT。QuickJS-NG 亦为 MIT，见 [NOTICE](NOTICE)。

---

## English

**QuickJS-Forge** embeds JavaScript in Haxe/hxcpp projects. A flat C ABI keeps every value on the native side — only `int32` handles and a preallocated argument stack cross the boundary — so a script call costs tens of nanoseconds instead of a JSON round-trip.

- Full ES2023 through quickjs-ng 0.18: classes with private fields, static blocks, generators, async/await, async generators, BigInt, Proxy, TypedArrays, named-group/lookbehind regex, top-level await.
- 34–38 ns per C→JS call, 82–110 ns per JS→C callback, 19–21 ns per property write (12–13 ns pre-bound), 0.5–0.8 µs per bytecode load.
- Static linkage with a matching CRT (`/MT` on Windows); no DLLs, no MinGW runtime, no search paths.
- Bytecode cache, frame-budget interrupt, memory and stack limits, per-script circuit breaker.
- Psych Engine adapter that mirrors the `FunkinLua` API by name and argument order (265 registered functions on 0.6.3-lineage forks).
- Windows, Linux and macOS build from one CMake tree and run in CI.

Build:

```bash
git clone --recursive <repo>
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel 4
ctest --test-dir build -C Release --output-on-failure
```

License: MIT. QuickJS-NG is MIT as well; see [NOTICE](NOTICE).

