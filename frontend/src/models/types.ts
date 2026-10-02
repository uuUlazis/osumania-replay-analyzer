// API DTO types mirroring backend/src/http/api_server.cpp responses.
export interface ApiOk<T> {
  ok: true;
  data: T;
}
export interface ApiErr {
  ok: false;
  error: { code: string; message: string; details?: Record<string, unknown> };
}
export type ApiResponse<T> = ApiOk<T> | ApiErr;

export interface JudgementCounts {
  perfect: number;
  great: number;
  good: number;
  ok: number;
  meh: number;
  miss: number;
}

export interface OffsetStats {
  count: number;
  mean: number;
  meanAbs: number;
  median: number;
  sd: number;
  p95: number;
  early: number;
  late: number;
  earlyMean: number;
  lateMean: number;
  min: number;
  max: number;
}

export interface HistogramBucket {
  fromMs: number;
  toMs: number;
  count: number;
}

export interface ColumnStats {
  column: number;
  counts: JudgementCounts;
  offsets: OffsetStats;
}

export interface BreakdownItem {
  result: string;
  count: number;
  share: number;
}

export interface ConsistencyItem {
  check: string;
  pass: boolean | null;
}

export interface AnalysisSummary {
  counts: JudgementCounts;
  headCounts: JudgementCounts;
  accuracy: { scoreV1: number; scoreV2: number };
  offsets: OffsetStats;
  tailOffsets: OffsetStats;
  histogram: HistogramBucket[];
  histogramBucketMs: number;
  perColumn: ColumnStats[];
  breakdown: BreakdownItem[];
  consistency: ConsistencyItem[];
  consistencyReport: {
    rolling20Sd: OffsetStats;
    chordOffsetSpread: number;
    chordCount: number;
    tapStability: number;
  };
  unmatchedInputCount: number;
  unmatchedObjects: number;
  firstEventTimeMs: number;
  lastEventTimeMs: number;
  osrDeclared: {
    counts: {
      n300: number;
      n100: number;
      n50: number;
      gekki: number;
      katu: number;
      miss: number;
    };
    maxCombo: number;
    score: number;
  };
}

export interface ReplayMetadata {
  player: string;
  gameMode: number;
  gameVersion: number;
  beatmapMd5: string;
  replayMd5: string;
  score: number;
  maxCombo: number;
  perfectCombo: boolean;
  mods: number;
  timestamp: number;
  counts: {
    n300: number;
    n100: number;
    n50: number;
    gekki: number;
    katu: number;
    miss: number;
  };
  keyCount: number;
  frameCount: number;
  replayPath: string;
}

export interface BeatmapMetadata {
  title: string;
  artist: string;
  creator: string;
  version: string;
  beatmapId: number;
  beatmapSetId: number;
  mode: number;
  keyCount: number;
  od: number;
  hp: number;
  audioFilename: string;
  backgroundFile: string;
  objectCount: number;
  beatmapPath: string;
}

export interface ManiaObject {
  index: number;
  column: number;
  startTimeMs: number;
  endTimeMs?: number;
  type: "note" | "hold";
}

export interface Hit {
  objectIndex: number;
  column: number;
  objectTimeMs: number;
  actualTimeMs?: number;
  offsetMs?: number;
  judgement: string;
  phase: string;
  // V1 (stable) combined whole-LN judgement; present on hold_tail hits only.
  v1Judgement?: string;
}

export interface ReplayFrameDto {
  timeMs: number;
  keys: number;
  isSeed: boolean;
}

export interface SegmentResult {
  startMs: number;
  endMs: number;
  counts: JudgementCounts;
  headCounts: JudgementCounts;
  accuracyScoreV1: number;
  accuracyScoreV2: number;
  offsets: OffsetStats;
  perColumn: ColumnStats[];
}

export interface ReplaySessionDto {
  sessionId: string;
}

// Display labels: MAX (Perfect) renders as 300g. Colors per spec:
// 300g blue, 300 yellow, 200 green; 100/50/MISS unchanged.
export const JUDGEMENT_ORDER = ["300g", "300", "200", "100", "50", "MISS"];
export const JUDGEMENT_COLORS: Record<string, string> = {
  "300g": "#57c8ff",
  "300": "#d9c358",
  "200": "#66d17a",
  "100": "#e09a4a",
  "50": "#c46a6a",
  MISS: "#e34a4a",
};
export const JUDGEMENT_COUNT_KEY: Record<string, keyof JudgementCounts> = {
  "300g": "perfect",
  "300": "great",
  "200": "good",
  "100": "ok",
  "50": "meh",
  MISS: "miss",
};
// backend judgement string (api_server.cpp hitResultNameJson) -> label
export const JUDGEMENT_BY_HIT: Record<string, string> = {
  perfect: "300g",
  great: "300",
  good: "200",
  ok: "100",
  meh: "50",
  miss: "MISS",
};
// timeline tick bottom edge per label: 300g lowest, MISS tallest
export const JUDGEMENT_TICK_HEIGHT: Record<string, number> = {
  "300g": 0.4,
  "300": 0.52,
  "200": 0.64,
  "100": 0.76,
  "50": 0.88,
  MISS: 1.0,
};
