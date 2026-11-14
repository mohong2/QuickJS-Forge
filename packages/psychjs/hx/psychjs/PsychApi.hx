package psychjs;

#if cpp
import quickjs.JsContext;
import quickjs.JsError;

/**
 * Registry of Psych Engine host functions exposed to JavaScript.
 *
 * Parity policy (README.md, section Psych Engine 适配):
 *   - function names match FunkinLua exactly (makeLuaSprite, setProperty, ...)
 *   - JS-only aliases are additive, never replacements
 *   - every function is registered through one table so the M3 port is a
 *     checklist, not a scavenger hunt
 *
 * M0: the table exists and reports what is bound; implementations land in M3.
 */
class PsychApi {
	public static final VERSION = "0.1.0-m0";

	/** name -> implementation. Populated by the adapter modules in M3. */
	public var functions:Map<String, Array<Dynamic>->Dynamic> = new Map();

	public function new() {}

	public function define(name:String, fn:Array<Dynamic>->Dynamic):Void {
		functions.set(name, fn);
	}

	public function bind(ctx:JsContext):Void {
		for (name in functions.keys()) {
			ctx.addFunction(name, functions.get(name));
		}
	}

	/** Registers a stub that throws a readable error instead of silently doing nothing. */
	public function stub(name:String):Void {
		define(name, function(args) {
			throw new JsError("psychjs: '" + name + "' is not implemented yet (M3)");
		});
	}

	public function count():Int return Lambda.count(functions);
}
#end
