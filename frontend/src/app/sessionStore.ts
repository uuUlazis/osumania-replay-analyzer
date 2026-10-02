// Module-level session store: big arrays (frames/objects/events) stay OUT of
// React state; components subscribe to lightweight snapshots.
import type {
  AnalysisSummary,
  BeatmapMetadata,
  Hit,
  ManiaObject,
  ReplayFrameDto,
  ReplayMetadata,
} from "../models/types";

export interface SessionSnapshot {
  sessionId: string | null;
  replay: ReplayMetadata | null;
  beatmap: BeatmapMetadata | null;
  analysis: AnalysisSummary | null;
  // big arrays live here, not in React state:
  objects: ManiaObject[];
  events: Hit[];    // V1 (stable) match — default judgement view
  eventsV2: Hit[];  // ScoreV2 match — 判定规则 V2 view
  frames: ReplayFrameDto[];
  lastEventTimeMs: number;
}

let state: SessionSnapshot = {
  sessionId: null,
  replay: null,
  beatmap: null,
  analysis: null,
  objects: [],
  events: [],
  eventsV2: [],
  frames: [],
  lastEventTimeMs: 0,
};

const listeners = new Set<() => void>();

function emit() {
  for (const fn of listeners) fn();
}

export const sessionStore = {
  get(): SessionSnapshot {
    return state;
  },
  set(patch: Partial<SessionSnapshot>) {
    state = { ...state, ...patch };
    emit();
  },
  reset() {
    state = {
      sessionId: null,
      replay: null,
      beatmap: null,
      analysis: null,
      objects: [],
      events: [],
      eventsV2: [],
      frames: [],
      lastEventTimeMs: 0,
    };
    emit();
  },
  subscribe(fn: () => void): () => void {
    listeners.add(fn);
    return () => listeners.delete(fn);
  },
};
