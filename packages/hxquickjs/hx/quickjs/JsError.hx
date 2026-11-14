package quickjs;

/** A JavaScript exception, already formatted by the bridge. */
class JsError extends haxe.Exception {
	public var stack:String;
	public var file:String;
	public var line:Int;
	public var script:String;

	public function new(message:String, ?stack:String, ?file:String, line:Int = 0, ?script:String) {
		super(message);
		this.stack = stack == null ? "" : stack;
		this.file = file == null ? "" : file;
		this.line = line;
		this.script = script == null ? "" : script;
	}

	/** "bar.js:12:5 — TypeError: x is not a function" */
	public function describe():String {
		var where = file == "" ? script : file;
		if (where == null || where == "") where = "<script>";
		return where + ":" + line + " — " + message;
	}
}
