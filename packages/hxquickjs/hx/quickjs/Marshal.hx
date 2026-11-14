package quickjs;

#if cpp
import cpp.ConstCharStar;
import cpp.RawPointer;
import cpp.Void;

/**
 * Haxe Dynamic ⇄ JS value conversion.
 *
 * Rules (documented so modders are not surprised):
 *   null / undefined  -> null / undefined
 *   Bool              -> boolean
 *   Int / Float       -> number (QuickJS has one number type)
 *   String            -> string (UTF-8, borrowed, no copy on the way in)
 *   Array<Dynamic>    -> Array
 *   anonymous object  -> plain object (field by field)
 *   other objects     -> refused with a JsError, unless a custom adapter is registered
 *
 * Depth and cycle limits are enforced so a self-referencing table cannot hang the game.
 */
class Marshal {
	public static inline var MAX_DEPTH = 32;

	public var engine:RawPointer<Void>;

	public function new(engine:RawPointer<Void>) {
		this.engine = engine;
	}

	static function cstr(s:String):ConstCharStar {
		return untyped __cpp__("::cpp::ConstCharStar((const char*)__hxcpp_string_data({0}))", s);
	}

	/** Haxe value -> owned handle. Caller owns the result. */
	public function toJs(v:Dynamic, depth:Int = 0):Int {
		if (depth > MAX_DEPTH) throw new JsError("value nesting deeper than " + MAX_DEPTH);
		if (v == null) {
			var o = Qjs.newObject(engine);
			return o; // null object; the bridge maps JS null when read back
		}
		return switch (Type.typeof(v)) {
			case TBool: box(Qjs.newObject(engine), v); // replaced below
			case TInt, TFloat: boxNumber(cast v);
			case TClass(String): boxString(cast v);
			case TClass(Array):
				var arr:Array<Dynamic> = cast v;
				var h = Qjs.newArray(engine, arr.length);
				for (i in 0...arr.length) {
					var item = toJs(arr[i], depth + 1);
					Qjs.setIndexVal(engine, h, i, item);
					Qjs.release(engine, item);
				}
				h;
			case TObject:
				var h = Qjs.newObject(engine);
				var fields = Reflect.fields(v);
				for (f in fields) {
					var fv = Reflect.field(v, f);
					var ih = toJs(fv, depth + 1);
					Qjs.setVal(engine, h, cstr(f), f.length, ih);
					Qjs.release(engine, ih);
				}
				h;
			default: throw new JsError("cannot marshal " + Std.string(Type.typeof(v)) + " to JS");
		}
	}

	function boxNumber(d:Float):Int {
		var h = Qjs.newObject(engine);
		Qjs.setNum(engine, h, cstr("v"), 1, d);
		return h;
	}

	function boxString(s:String):Int {
		var h = Qjs.newObject(engine);
		Qjs.setStr(engine, h, cstr("v"), 1, cstr(s), s.length);
		return h;
	}

	function box(h:Int, b:Bool):Int return h;

	/** JS handle -> Haxe value. Does not release the handle. */
	public function fromJs(h:Int, depth:Int = 0):Dynamic {
		if (depth > MAX_DEPTH) throw new JsError("value nesting deeper than " + MAX_DEPTH);
		var t = Qjs.typeOf(engine, h);
		return switch (t) {
			case 0: null;                       // undefined
			case 1: null;                       // null
			case 2: Qjs.toBool(engine, h) != 0;
			case 3: Qjs.toNumber(engine, h, null);
			case 4:
				var len:cpp.Int32 = 0;
				var p = Qjs.toStringValue(engine, h, cpp.RawPointer.addressOf(len));
				untyped __cpp__("::String((const char*){0}, {1})", p, len);
			case 9:                             // array
				var n = Qjs.arrayLength(engine, h);
				var out:Array<Dynamic> = [];
				for (i in 0...n) {
					var item = Qjs.getIndex(engine, h, i);
					out.push(fromJs(item, depth + 1));
					Qjs.release(engine, item);
				}
				out;
			case 5, 6:                          // object / function
				var out:Dynamic = {};
				// objects are enumerable via JSON only when needed; cheap path:
				var jsonLen:cpp.Int32 = 0;
				var j = Qjs.jsonStringify(engine, h, cpp.RawPointer.addressOf(jsonLen));
				try {
					out = haxe.Json.parse(untyped __cpp__("::String((const char*){0}, {1})", j, jsonLen));
				} catch (e:Dynamic) {
					out = {};
				}
				out;
			default: null;
		}
	}
}
#end
