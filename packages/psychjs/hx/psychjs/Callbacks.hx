package psychjs;

/**
 * Callback names shared with FunkinLua. Same names, same argument order, same
 * return-value meaning (Function_Stop family included).
 */
class Callbacks {
	public static inline var CREATE = "onCreate";
	public static inline var DESTROY = "onDestroy";
	public static inline var UPDATE = "onUpdate";
	public static inline var UPDATE_POST = "onUpdatePost";
	public static inline var BEAT_HIT = "onBeatHit";
	public static inline var STEP_HIT = "onStepHit";
	public static inline var SECTION_HIT = "onSectionHit";
	public static inline var SONG_START = "onSongStart";
	public static inline var SONG_END = "onEndSong";
	public static inline var NOTE_HIT = "goodNoteHit";
	public static inline var NOTE_MISS = "opponentNoteHit";
	public static inline var NOTE_MISSED = "noteMiss";
	public static inline var EVENT = "onEvent";
	public static inline var PAUSE = "onPause";
	public static inline var RESUME = "onResume";

	public static inline var ALL:Array<String> = [
		CREATE, DESTROY, UPDATE, UPDATE_POST, BEAT_HIT, STEP_HIT, SECTION_HIT,
		SONG_START, SONG_END, NOTE_HIT, NOTE_MISS, NOTE_MISSED, EVENT, PAUSE, RESUME
	];
}
#end
