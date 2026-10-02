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
  // judgement rule used by the preview overlay: stable V1 or ScoreV2
  judgeVersion: "v1" | "v2";
  // optional session music object URL (null = no audio)
  musicUrl: string | null;
  // music volume 0..1
  volume: number;
}

let state: PlayerSnapshot = {
  previewTimeMs: 0,
  durationMs: 0,
  playing: false,
  scrollSpeed: 22,
  selection: null,
  showInputJudges: false,
  judgeVersion: "v1",
  musicUrl: null,
  volume: 1,
};

const listeners = new Set<() => void>();

function emit() {
  for (const fn of listeners) fn();
}

let rafId: number | null = null;
let lastTick: number | null = null;

function tick(now: number) {
  if (lastTick !== null && state.playing) {
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
  play() {
    if (state.previewTimeMs >= state.durationMs) state.previewTimeMs = 0;
    state = { ...state, playing: true };
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
  setJudgeVersion(v: "v1" | "v2") {
    state = { ...state, judgeVersion: v };
    emit();
  },
  setMusicUrl(url: string | null) {
    state = { ...state, musicUrl: url };
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
