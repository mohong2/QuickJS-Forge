package quickjs;

#if cpp
import cpp.ConstCharStar;
import cpp.RawPointer;
import cpp.UInt8;
import cpp.Void;

/**
 * Raw bindings for native/bridge/qjs_bridge.h.
 *
 * Contract: nothing here passes a JSValue by value. Either a 32-bit handle
 * (qjs_val) or the per-engine argument stack. See README.md, section 设计要点.
 *
 * STATUS: M0 — the C side is built and tested (tests/c). The hxcpp externs
 * below are the frozen contract; M1 compiles them against a real game build.
 */
@:include("qjs_bridge.h")
extern class Qjs {
	@:native("qjs_version") static function version():ConstCharStar;

	// lifecycle
	@:native("qjs_create") static function create(memoryLimitMb:Int, maxStackKb:Int, enableIntl:Int):RawPointer<Void>;
	@:native("qjs_destroy") static function destroy(e:RawPointer<Void>):Void;

	// limits
	@:native("qjs_set_memory_limit") static function setMemoryLimit(e:RawPointer<Void>, bytes:cpp.Int64):Void;
	@:native("qjs_set_max_stack") static function setMaxStack(e:RawPointer<Void>, bytes:Int):Void;
	@:native("qjs_run_gc") static function runGc(e:RawPointer<Void>):Void;
	@:native("qjs_memory_used") static function memoryUsed(e:RawPointer<Void>):cpp.Int64;
	@:native("qjs_memory_report") static function memoryReport(e:RawPointer<Void>):ConstCharStar;

	// interrupt
	@:native("qjs_set_interrupt") static function setInterrupt(e:RawPointer<Void>, fn:cpp.Callable<()->Int>, user:RawPointer<Void>):Void;

	// values
	@:native("qjs_global") static function global(e:RawPointer<Void>):Int;
	@:native("qjs_get") static function get(e:RawPointer<Void>, obj:Int, key:ConstCharStar, keyLen:Int):Int;
	@:native("qjs_get_index") static function getIndex(e:RawPointer<Void>, obj:Int, idx:Int):Int;
	@:native("qjs_release") static function release(e:RawPointer<Void>, v:Int):Void;
	@:native("qjs_is_function") static function isFunction(e:RawPointer<Void>, v:Int):Int;
	@:native("qjs_is_object") static function isObject(e:RawPointer<Void>, v:Int):Int;
	@:native("qjs_is_nullish") static function isNullish(e:RawPointer<Void>, v:Int):Int;
	@:native("qjs_typeof") static function typeOf(e:RawPointer<Void>, v:Int):Int;

	@:native("qjs_to_number") static function toNumber(e:RawPointer<Void>, v:Int, ok:cpp.RawPointer<cpp.Int32>):Float;
	@:native("qjs_to_int32") static function toInt32(e:RawPointer<Void>, v:Int, ok:cpp.RawPointer<cpp.Int32>):Int;
	@:native("qjs_to_bool") static function toBool(e:RawPointer<Void>, v:Int):Int;
	@:native("qjs_to_string") static function toStringValue(e:RawPointer<Void>, v:Int, len:cpp.RawPointer<cpp.Int32>):ConstCharStar;

	// property writes
	@:native("qjs_set_num") static function setNum(e:RawPointer<Void>, obj:Int, key:ConstCharStar, keyLen:Int, v:Float):Int;
	@:native("qjs_set_bool") static function setBool(e:RawPointer<Void>, obj:Int, key:ConstCharStar, keyLen:Int, v:Int):Int;
	@:native("qjs_set_str") static function setStr(e:RawPointer<Void>, obj:Int, key:ConstCharStar, keyLen:Int, v:ConstCharStar, vLen:Int):Int;
	@:native("qjs_set_val") static function setVal(e:RawPointer<Void>, obj:Int, key:ConstCharStar, keyLen:Int, v:Int):Int;
	@:native("qjs_set_index_num") static function setIndexNum(e:RawPointer<Void>, obj:Int, idx:Int, v:Float):Int;
	@:native("qjs_set_index_val") static function setIndexVal(e:RawPointer<Void>, obj:Int, idx:Int, v:Int):Int;

	// bound properties
	@:native("qjs_prop_bind") static function propBind(e:RawPointer<Void>, obj:Int, key:ConstCharStar, keyLen:Int):Int;
	@:native("qjs_prop_set_num") static function propSetNum(e:RawPointer<Void>, prop:Int, v:Float):Int;
	@:native("qjs_prop_set_bool") static function propSetBool(e:RawPointer<Void>, prop:Int, v:Int):Int;
	@:native("qjs_prop_set_str") static function propSetStr(e:RawPointer<Void>, prop:Int, v:ConstCharStar, vLen:Int):Int;
	@:native("qjs_prop_get_num") static function propGetNum(e:RawPointer<Void>, prop:Int, out:cpp.RawPointer<cpp.Float64>):Int;
	@:native("qjs_prop_free") static function propFree(e:RawPointer<Void>, prop:Int):Void;

	// calls
	@:native("qjs_args_begin") static function argsBegin(e:RawPointer<Void>):Void;
	@:native("qjs_push_num") static function pushNum(e:RawPointer<Void>, v:Float):Void;
	@:native("qjs_push_int") static function pushInt(e:RawPointer<Void>, v:Int):Void;
	@:native("qjs_push_bool") static function pushBool(e:RawPointer<Void>, v:Int):Void;
	@:native("qjs_push_null") static function pushNull(e:RawPointer<Void>):Void;
	@:native("qjs_push_undef") static function pushUndef(e:RawPointer<Void>):Void;
	@:native("qjs_push_str") static function pushStr(e:RawPointer<Void>, s:ConstCharStar, len:Int):Void;
	@:native("qjs_push_val") static function pushVal(e:RawPointer<Void>, v:Int):Void;
	@:native("qjs_push_own") static function pushOwn(e:RawPointer<Void>, v:Int):Void;

	@:native("qjs_call") static function call(e:RawPointer<Void>, fn:Int, thisVal:Int, argc:Int):Int;
	@:native("qjs_call_global") static function callGlobal(e:RawPointer<Void>, name:ConstCharStar, nameLen:Int, argc:Int):Int;

	@:native("qjs_result_num") static function resultNum(e:RawPointer<Void>, ok:cpp.RawPointer<cpp.Int32>):Float;
	@:native("qjs_result_int") static function resultInt(e:RawPointer<Void>, ok:cpp.RawPointer<cpp.Int32>):Int;
	@:native("qjs_result_bool") static function resultBool(e:RawPointer<Void>):Int;
	@:native("qjs_result_str") static function resultStr(e:RawPointer<Void>, len:cpp.RawPointer<cpp.Int32>):ConstCharStar;
	@:native("qjs_result_val") static function resultVal(e:RawPointer<Void>):Int;
	@:native("qjs_result_drop") static function resultDrop(e:RawPointer<Void>):Void;

	// host functions
	@:native("qjs_set_host_dispatch") static function setHostDispatch(e:RawPointer<Void>, fn:cpp.Callable<(RawPointer<Void>, Int, Int, RawPointer<Void>) -> Int>, user:RawPointer<Void>):Void;
	@:native("qjs_new_host_fn") static function newHostFn(e:RawPointer<Void>, name:ConstCharStar, nameLen:Int, fnId:Int, arity:Int):Int;

	@:native("qjs_argc") static function argc(e:RawPointer<Void>):Int;
	@:native("qjs_arg_type") static function argType(e:RawPointer<Void>, i:Int):Int;
	@:native("qjs_arg_num") static function argNum(e:RawPointer<Void>, i:Int):Float;
	@:native("qjs_arg_int") static function argInt(e:RawPointer<Void>, i:Int):Int;
	@:native("qjs_arg_bool") static function argBool(e:RawPointer<Void>, i:Int):Int;
	@:native("qjs_arg_str") static function argStr(e:RawPointer<Void>, i:Int, len:cpp.RawPointer<cpp.Int32>):ConstCharStar;
	@:native("qjs_arg_val") static function argVal(e:RawPointer<Void>, i:Int):Int;

	@:native("qjs_ret_num") static function retNum(e:RawPointer<Void>, v:Float):Void;
	@:native("qjs_ret_int") static function retInt(e:RawPointer<Void>, v:Int):Void;
	@:native("qjs_ret_bool") static function retBool(e:RawPointer<Void>, v:Int):Void;
	@:native("qjs_ret_null") static function retNull(e:RawPointer<Void>):Void;
	@:native("qjs_ret_undef") static function retUndef(e:RawPointer<Void>):Void;
	@:native("qjs_ret_str") static function retStr(e:RawPointer<Void>, s:ConstCharStar, len:Int):Void;
	@:native("qjs_ret_val") static function retVal(e:RawPointer<Void>, v:Int):Void;

	// eval / bytecode
	@:native("qjs_eval") static function eval(e:RawPointer<Void>, src:ConstCharStar, len:Int, filename:ConstCharStar, flags:Int):Int;
	@:native("qjs_eval_fn") static function evalFn(e:RawPointer<Void>, fn:Int):Int;
	@:native("qjs_pump_jobs") static function pumpJobs(e:RawPointer<Void>):Void;
	@:native("qjs_compile") static function compile(e:RawPointer<Void>, src:ConstCharStar, len:Int, filename:ConstCharStar, flags:Int, outLen:cpp.RawPointer<cpp.Int32>):RawPointer<UInt8>;
	@:native("qjs_run_bytecode") static function runBytecode(e:RawPointer<Void>, buf:RawPointer<UInt8>, len:Int):Int;
	@:native("qjs_free_bytes") static function freeBytes(e:RawPointer<Void>, buf:RawPointer<UInt8>):Void;

	// errors
	@:native("qjs_has_exception") static function hasException(e:RawPointer<Void>):Int;
	@:native("qjs_take_error") static function takeError(e:RawPointer<Void>, len:cpp.RawPointer<cpp.Int32>):ConstCharStar;
	@:native("qjs_throw") static function throwError(e:RawPointer<Void>, msg:ConstCharStar, len:Int):Void;

	// objects
	@:native("qjs_new_object") static function newObject(e:RawPointer<Void>):Int;
	@:native("qjs_new_array") static function newArray(e:RawPointer<Void>, len:Int):Int;
	@:native("qjs_array_length") static function arrayLength(e:RawPointer<Void>, arr:Int):Int;
	@:native("qjs_json_parse") static function jsonParse(e:RawPointer<Void>, s:ConstCharStar, len:Int):Int;
	@:native("qjs_json_stringify") static function jsonStringify(e:RawPointer<Void>, v:Int, len:cpp.RawPointer<cpp.Int32>):ConstCharStar;
}
#end
