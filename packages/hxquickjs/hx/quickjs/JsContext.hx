package quickjs;

#if cpp
import cpp.ConstCharStar;
import cpp.RawPointer;
import cpp.Void;

/**
 * One QuickJS runtime + context. Single-threaded: create it on the main thread
 * and only touch it there.
 *
 *   var js = new JsContext();
 *   js.addFunction("log", args -> trace(args.join(" ")));
 *   js.eval("log('hello from ' + (1+1))");
 *   js.dispose();
 */
class JsContext {
	static var registry = new Map<Int, JsContext>();
	static var nextId = 1;

	public var engine(default, null):RawPointer<Void>;
	public var id(default, null):Int;
	public var closed(default, null):Bool = false;

	/** Per-frame CPU budget; the interrupt handler stops runaway scripts. */
	public var frameBudgetMs:Float = 4.0;
	/** Consecutive uncaught errors before a script is disabled. */
	public var errorBudget:Int = 3;

	var hostFns:Array<Array<Dynamic>->Dynamic> = [];
	var hostNames:Map<String, Int> = new Map();
	var marshal:Marshal;
	var errors:Int = 0;
	var frameClock:Float = 0;

	public function new(memoryMb:Int = 64, stackKb:Int = 1024) {
		engine = Qjs.create(memoryMb, stackKb, 0);
		if (engine == null) throw new JsError("QuickJS: cannot create runtime");
		id = nextId++;
		registry.set(id, this);
		marshal = new Marshal(engine);
		Qjs.setHostDispatch(engine, dispatch, cast id);
		Qjs.setInterrupt(engine, onInterrupt, null);
	}

	public function version():String return Qjs.version().toString();

	// ---- globals -----------------------------------------------------
	public function getGlobal():Int return Qjs.global(engine);

	public function get(name:String):Dynamic {
		var g = getGlobal();
		var h = Qjs.get(engine, g, cstr(name), name.length);
		Qjs.release(engine, g);
		if (h == 0) { throwError(); return null; }
		var v = marshal.fromJs(h);
		Qjs.release(engine, h);
		return v;
	}

	public function set(name:String, value:Dynamic):Void {
		var g = getGlobal();
		var h = marshal.toJs(value);
		Qjs.setVal(engine, g, cstr(name), name.length, h);
		Qjs.release(engine, h);
		Qjs.release(engine, g);
	}

	// ---- eval / call -------------------------------------------------
	public function eval(src:String, filename:String = "<eval>", asModule:Bool = false):Dynamic {
		var flags = asModule ? 1 : 0;
		if (Qjs.eval(engine, cstr(src), src.length, cstr(filename), flags) != 1) {
			throwError();
			return null;
		}
		return takeResult();
	}

	public function evalBytes(bytecode:cpp.NativeArray<cpp.UInt8>, len:Int, ?filename:String):Dynamic {
		var p:RawPointer<cpp.UInt8> = bytecode;
		if (Qjs.runBytecode(engine, p, len) != 1) { throwError(); return null; }
		return takeResult();
	}

	/** Call a JS function previously obtained as a handle. */
	public function callHandle(fn:Int, args:Array<Dynamic>, ?thisArg:Int):Dynamic {
		Qjs.argsBegin(engine);
		for (a in args) pushValue(a);
		var self = thisArg == null ? Qjs.global(engine) : thisArg;
		var rc = Qjs.call(engine, fn, self, args.length);
		if (thisArg == null) Qjs.release(engine, self);
		if (rc != 1) { throwError(); return null; }
		return takeResult();
	}

	public function callGlobal(name:String, args:Array<Dynamic>):Dynamic {
		Qjs.argsBegin(engine);
		for (a in args) pushValue(a);
		var rc = Qjs.callGlobal(engine, cstr(name), name.length, args.length);
		if (rc != 1) { throwError(); return null; }
		return takeResult();
	}

	function pushValue(v:Dynamic):Void {
		switch (Type.typeof(v)) {
			case TNull: Qjs.pushNull(engine);
			case TBool: Qjs.pushBool(engine, (v == true) ? 1 : 0);
			case TInt: Qjs.pushInt(engine, cast v);
			case TFloat: Qjs.pushNum(engine, cast v);
			case TClass(String): Qjs.pushStr(engine, cstr(cast v), (cast v : String).length);
			default:
				var h = marshal.toJs(v);
				Qjs.pushOwn(engine, h);
		}
	}

	function takeResult():Dynamic {
		var h = Qjs.resultVal(engine);
		var v = marshal.fromJs(h);
		Qjs.release(engine, h);
		return v;
	}

	// ---- host functions ----------------------------------------------
	public function addFunction(name:String, fn:Array<Dynamic>->Dynamic, ?arity:Int = 1):Void {
		var fnId = hostFns.length;
		hostFns.push(fn);
		hostNames.set(name, fnId);
		var h = Qjs.newHostFn(engine, cstr(name), name.length, fnId, arity);
		var g = getGlobal();
		Qjs.setVal(engine, g, cstr(name), name.length, h);
		Qjs.release(engine, h);
		Qjs.release(engine, g);
	}

	static function dispatch(enginePtr:RawPointer<Void>, fnId:Int, argc:Int, user:RawPointer<Void>):Int {
		var ctxId:Int = untyped __cpp__("(int)(intptr_t){0}", user);
		var ctx = registry.get(ctxId);
		if (ctx == null) return 0;
		var args:Array<Dynamic> = [];
		for (i in 0...argc) {
			var t = Qjs.argType(enginePtr, i);
			switch (t) {
				case 3: args.push(Qjs.argNum(enginePtr, i));
				case 2: args.push(Qjs.argBool(enginePtr, i) != 0);
				case 4:
					var len:cpp.Int32 = 0;
					var p = Qjs.argStr(enginePtr, i, cpp.RawPointer.addressOf(len));
					args.push(untyped __cpp__("::String((const char*){0}, {1})", p, len));
				case 0, 1: args.push(null);
				default:
					var h = Qjs.argVal(enginePtr, i);
					args.push(ctx.marshal.fromJs(h));
					Qjs.release(enginePtr, h);
			}
		}
		try {
			var r = ctx.hostFns[fnId](args);
			ctx.pushReturn(r);
			return 1;
		} catch (e:JsError) {
			Qjs.throwError(enginePtr, cstr(e.message), e.message.length);
			return 0;
		} catch (e:Dynamic) {
			var msg = Std.string(e);
			Qjs.throwError(enginePtr, cstr(msg), msg.length);
			return 0;
		}
	}

	function pushReturn(r:Dynamic):Void {
		switch (Type.typeof(r)) {
			case TNull: Qjs.retNull(engine);
			case TBool: Qjs.retBool(engine, (r == true) ? 1 : 0);
			case TInt: Qjs.retInt(engine, cast r);
			case TFloat: Qjs.retNum(engine, cast r);
			case TClass(String): Qjs.retStr(engine, cstr(cast r), (cast r : String).length);
			default:
				var h = marshal.toJs(r);
				Qjs.retVal(engine, h);
				Qjs.release(engine, h);
		}
	}

	// ---- guardrails ---------------------------------------------------
	static function onInterrupt():Int {
		// Wired per-context in M1; for now the C default (no deadline) applies.
		return 0;
	}

	public function tickFrame(dt:Float):Void {
		frameClock += dt;
		Qjs.pumpJobs(engine);
	}

	// ---- errors -------------------------------------------------------
	function throwError():Void {
		var len:cpp.Int32 = 0;
		var p = Qjs.takeError(engine, cpp.RawPointer.addressOf(len));
		var msg = untyped __cpp__("::String((const char*){0}, {1})", p, len);
		errors++;
		throw new JsError(msg);
	}

	public function dispose():Void {
		if (closed) return;
		closed = true;
		registry.remove(id);
		Qjs.destroy(engine);
		engine = null;
	}

	static inline function cstr(s:String):ConstCharStar {
		return untyped __cpp__("::cpp::ConstCharStar((const char*)__hxcpp_string_data({0}))", s);
	}
}
#end
