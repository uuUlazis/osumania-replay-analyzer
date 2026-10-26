// Playback/preview state (timeline cursor, selection) + rAF ticker.
export interface PlayerSnapshot {
  previewTimeMs: number;
  durationMs: number;
  playing: boolean;
  // Preview scroll speed (lazer mania scale 1..40; visible time 11485/s ms).
  scrollSpeed: number;
  // active timeline selection (null = none). Half-open [startMs, endMs).
  selection: { startMs: number; endMs: number } | null;
  // replay-preview judgement overlay (input ticks + object outlines)
  showInputJudges: boolean;
  // optional session music object URL (null = no audio)
  musicUrl: string | null;
  // music volume 0..1
  volume: number;
  // audio playback rate (beatmap seconds per audio second): DT/NC 1.5, HT 0.75
  rate: number;
  // NC (Nightcore): stable 语义 = 升调 (重采样, 1:1 映射); 非 NC rate mod
  // (DT/HT/自定义) = 不升调 (WSOLA 拉伸, timeBase=rate)
  nightcore: boolean;
  // bumped on skin hot-swap so the preview reloads skin resources
  skinRev: number;
  // user is dragging the seek slider: freeze the clock (and the audio) so the
  // preview tracks the slider exactly instead of running ahead of the music
  scrubbing: boolean;
  // an audio seek is in flight after a handoff: hold the clock until the
  // element reports where it actually landed (VBR seeks are approximate)
  holdClock: boolean;
}

let state: PlayerSnapshot = {
  previewTimeMs: 0,
  durationMs: 0,
  playing: false,
  scrollSpeed: 22,
  selection: null,
  showInputJudges: true,
  musicUrl: null,
  volume: 1,
  rate: 1,
  nightcore: false,
  skinRev: 0,
  scrubbing: false,
  holdClock: false,
};

const listeners = new Set<() => void>();

function emit() {
  for (const fn of listeners) fn();
}

let rafId: number | null = null;
let lastTick: number | null = null;

function tick(now: number) {
  // With music loaded the preview clock is driven exclusively by the audio
  // element (AudioSync follow loop — lazer-style single source of truth):
  // wall-clock integration here would be a second, competing time source,
  // and the two kept fighting (the audible preview/audio desync).
  if (lastTick !== null && state.playing && !state.scrubbing &&
      !state.holdClock && !state.musicUrl) {
    const dt = now - lastTick;
    setPreviewTime(state.previewTimeMs + dt);
  }
  lastTick = now;
  if (state.playing) {
    rafId = requestAnimationFrame(tick);
  } else {
    rafId = null;
    lastTick = null;
  }
}

function ensureTicker() {
  if (state.playing && rafId === null) {
    lastTick = null;
    rafId = requestAnimationFrame(tick);
  }
}

function setPreviewTime(t: number) {
  let nt = t;
  if (state.durationMs > 0) {
    if (nt > state.durationMs) {
      nt = state.durationMs;
      state = { ...state, playing: false };
    }
  }
  if (nt < 0) nt = 0;
  state = { ...state, previewTimeMs: nt };
  emit();
}

export const playerStore = {
  get(): PlayerSnapshot {
    return state;
  },
  setDuration(ms: number) {
    state = { ...state, durationMs: ms };
    emit();
  },
  seek(ms: number) {
    setPreviewTime(ms);
  },
  // Snap the preview to wherever the audio element actually landed after a
  // seek (VBR mp3 seeks are approximate: the browser maps time->byte via the
  // average bitrate). Keeps notes locked to the music no matter what.
  snapAudio(ms: number) {
    state = { ...state, previewTimeMs: Math.max(0, ms) };
    emit();
  },
  setScrubbing(v: boolean) {
    state = { ...state, scrubbing: v };
    emit();
  },
  setHoldClock(v: boolean) {
    state = { ...state, holdClock: v };
    emit();
  },
  // DT/NC speed the song up 1.5x, HT slows it to 0.75x; the beatmap timeline
  // itself is untouched (stable replays store frames in beatmap time)
  setRate(r: number) {
    const clamped = Math.min(4, Math.max(0.25, r));
    if (clamped !== state.rate) {
      state = { ...state, rate: clamped };
      emit();
    }
  },
  play() {
    if (state.previewTimeMs >= state.durationMs) state.previewTimeMs = 0;
    state = { ...state, playing: true, scrubbing: false, holdClock: false };
    ensureTicker();
    emit();
  },
  pause() {
    state = { ...state, playing: false };
    emit();
  },
  toggle() {
    if (state.playing) this.pause();
    else this.play();
  },
  setSelection(sel: { startMs: number; endMs: number } | null) {
    state = { ...state, selection: sel };
    emit();
  },
  setShowInputJudges(v: boolean) {
    state = { ...state, showInputJudges: v };
    emit();
  },
  setNightcore(v: boolean) {
    if (v !== state.nightcore) {
      state = { ...state, nightcore: v };
      emit();
    }
  },
  setMusicUrl(url: string | null) {
    state = { ...state, musicUrl: url };
    emit();
  },
  bumpSkin() {
    state = { ...state, skinRev: state.skinRev + 1 };
    emit();
  },
  setVolume(v: number) {
    const vol = Math.max(0, Math.min(1, Number.isFinite(v) ? v : 1));
    state = { ...state, volume: vol };
    emit();
  },
  setScrollSpeed(speed: number) {
    const s = Math.max(1, Math.min(40, Math.round(speed) || 22));
    state = { ...state, scrollSpeed: s };
    emit();
  },
  subscribe(fn: () => void): () => void {
    listeners.add(fn);
    return () => listeners.delete(fn);
  },
};
