package psychjs;

#if cpp
import quickjs.JsContext;
import quickjs.JsError;
import sys.FileSystem;
import sys.io.File;

/**
 * Owns the JS contexts for one mod (or for the whole game).
 *
 *   var host = new ScriptHost();
 *   host.loadDirectory("mods/mymod/scripts");
 *   host.dispatch(Callbacks.CREATE, []);
 *   // every frame:
 *   host.dispatch(Callbacks.UPDATE, [elapsed]);
 */
class ScriptHost {
	public var api(default, null):PsychApi;
	public var scripts:Array<Script> = [];

	public function new(?api:PsychApi) {
		this.api = api == null ? new PsychApi() : api;
	}

	public function loadDirectory(dir:String):Int {
		if (!FileSystem.exists(dir)) return 0;
		var added = 0;
		for (name in FileSystem.readDirectory(dir)) {
			if (!StringTools.endsWith(name, ".js")) continue;
			if (name.indexOf(".min.") >= 0) continue;
			scripts.push(new Script(File.getContent(dir + "/" + name), dir + "/" + name, this));
			added++;
		}
		return added;
	}

	public function dispatch(callback:String, args:Array<Dynamic>):Void {
		for (s in scripts) {
			if (s.disabled) continue;
			s.invoke(callback, args);
		}
	}

	public function dispose():Void {
		for (s in scripts) s.dispose();
		scripts = [];
	}
}

/** One script file = one JsContext. Isolation is cheaper than a shared heap. */
class Script {
	public var path:String;
	public var ctx:JsContext;
	public var disabled:Bool = false;
	public var failures:Int = 0;
	public var budgetMs:Float = 4.0;

	public function new(source:String, path:String, host:ScriptHost) {
		this.path = path;
		ctx = new JsContext();
		host.api.bind(ctx);
		ctx.addFunction("log", function(a) { trace(a.join(" ")); return null; });
		ctx.addFunction("print", function(a) { trace(a.join(" ")); return null; });
		try {
			ctx.eval(source, path);
		} catch (e:JsError) {
			failures++;
			trace("[psychjs] " + path + " failed to load: " + e.message);
			disabled = true;
		}
	}

	public function invoke(callback:String, args:Array<Dynamic>):Void {
		try {
			ctx.callGlobal(callback, args);
		} catch (e:JsError) {
			failures++;
			trace("[psychjs] " + path + " " + callback + ": " + e.message);
			if (failures >= 3) {
				disabled = true;
				trace("[psychjs] " + path + " disabled after " + failures + " failures");
			}
		}
	}

	public function dispose():Void {
		ctx.dispose();
	}
}
#end
