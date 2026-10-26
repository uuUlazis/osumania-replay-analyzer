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

// 判定窗两套取值 (后端 matcher 实际上报, 见 /summary 的 windows 字段)
export interface ManiaWindowsDto {
  perfect: number;
  great: number;
  good: number;
  ok: number;
  meh: number;
  miss: number;
}

// 同一份 counts 下的两个 acc 权重口径: scoreV1 (legacy, MAX=300) /
// scoreV2 (MAX=305, 分母 305N)。
export interface AccuracyPair {
  scoreV1: number;
  scoreV2: number;
}

// 双计数口径: stable = 每物件一次 (LN 单计, osu!stable .osr 口径);
// pairwise = LN 头尾各计 (lazer / ScoreV2 口径)。
export interface AccuracyDetail {
  stable: AccuracyPair;
  pairwise: AccuracyPair;
}

export interface AnalysisSummary {
  counts: JudgementCounts;
  rulesProfile: "v1" | "v2";
  // 成绩来源 (Phase L1): "stable" (缺省, 兼容旧后端) | "lazer"
  scoreSource?: "stable" | "lazer";
  // lazer 局附加信息 (scoreSource === "lazer" 时由后端给出)
  lazer?: LazerSummaryInfo;
  // lazer 尾块 mods 原列表 (acronym + settings; 小数倍率/IN/RD 等透传显示)
  lazerMods?: LazerMod[];
  // "stable" = 每物件一次 (LN 单计) / "lazer" = LN 头尾各计
  countMode?: "scorev1" | "scorev2" | "stable" | "lazer";   // 兼容旧后端返回值
  headCounts: JudgementCounts;
  accuracy: { scoreV1: number; scoreV2: number };
  // 下面五项是向后兼容的新增字段: 旧后端不返回时前端优雅回退
  countsStable?: JudgementCounts;
  countsPairwise?: JudgementCounts;
  accuracyDetail?: AccuracyDetail;
  // .osr 原始 mod 位 / 判定 rate / 实际生效的判定窗 (MRA_RE_WIN 整数化 +
  // HR/EZ/DT/HT 因子之后)
  mods?: number;
  rate?: number;
  windows?: { integral: boolean; v1: ManiaWindowsDto; v2: ManiaWindowsDto };
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
  // 官方公式 pp (.osr 标定 counts + 官方 SR; -1 = SR 不可算)
  pp?: number;
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
  countMode?: "scorev1" | "scorev2" | "stable" | "lazer";   // 兼容旧后端返回值
  counts: JudgementCounts;
  headCounts: JudgementCounts;
  accuracyScoreV1: number;
  accuracyScoreV2: number;
  // 双计数口径 (可选, 旧后端不返回)
  countsStable?: JudgementCounts;
  countsPairwise?: JudgementCounts;
  accuracyDetail?: AccuracyDetail;
  offsets: OffsetStats;
  // 选段局部 SRR SR (-1 = 不可算)
  srrSr?: number;
  // 选段局部 stable SR (官方算法, rate 跟随 mods; -1 = 不可算)
  officialSr?: number;
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

// ---- scores.db (osu! stable local score browser) ----
export interface ScoreEntryDto {
  mode: number;
  clientVersion: number;
  beatmapMd5: string;
  playerName: string;
  replayMd5: string;
  c300: number;
  c100: number;
  c50: number;
  geki: number;
  katu: number;
  miss: number;
  totalScore: number;
  maxCombo: number;
  fullCombo: boolean;
  mods: number;
  playedMs: number;
  onlineId: number;
}

export interface ScoresLoadResult {
  version: number;
  groups: number;
  total: number;
  byMode: { mode: number; count: number }[];
  dataRAuto: boolean;
  dataRFolder: string;
  dataRHint: string;
}

export interface ScoreBrowseRow {
  mode: number;
  beatmapMd5: string;
  playerName: string;
  replayMd5: string;
  c300: number;
  c100: number;
  c50: number;
  geki: number;
  katu: number;
  miss: number;
  totalScore: number;
  maxCombo: number;
  fullCombo: boolean;
  mods: number;
  playedMs: number;
  // 100 ns .NET ticks as a string (exceeds JS safe-integer range)
  playedTicks: string;
  onlineId: number;
  title: string;
  artist: string;
  creator: string;
  version: string;
  keyCount: string;
  // stable 星数 (官方 SR, 按行 mods 档位; -1 = 不可算/无谱面)
  sr?: number;
  // sunnyxxy rework (SRR) 全局星数 (-1 = 不可算/无谱面)
  sunnySr?: number;
  // BPM (驻留最长无继承段; -1 = 不可算)
  bpm?: number;
  // 物件跨度长度 ms (-1 = 不可算)
  lengthMs?: number;
  // 官方公式 pp (lazer 移植 SR × 行 counts; -1 = SR 不可算)
  pp?: number;
  hasReplay: boolean;
}

export interface ScoresListResult {
  total: number;
  returned: number;
  scores: ScoreEntryDto[];
}

// lazer 局判定六档 (label 直接用 lazer 命名, 不走 300g/300/... stable 映射)
export interface JudgementLabel {
  label: string;
  key: keyof JudgementCounts;
  color: string;
}
export const LAZER_JUDGEMENTS: JudgementLabel[] = [
  { label: "Perfect", key: "perfect", color: JUDGEMENT_COLORS["300g"] },
  { label: "Great", key: "great", color: JUDGEMENT_COLORS["300"] },
  { label: "Good", key: "good", color: JUDGEMENT_COLORS["200"] },
  { label: "Ok", key: "ok", color: JUDGEMENT_COLORS["100"] },
  { label: "Meh", key: "meh", color: JUDGEMENT_COLORS["50"] },
  { label: "Miss", key: "miss", color: JUDGEMENT_COLORS["MISS"] },
];

// ---- lazer 数据通道 (osu!lazer, Phase L1) ----
// 顶栏 stable/lazer 模式开关 (持久化到 last_state 键 scoreSourceMode)
export type ScoreSourceMode = "stable" | "lazer";
// lazer 成绩判定计数的来源: json = realm 精确六档; ushort = 尾块缺失时的
// stable 兼容近似 (仅 300/100/50/miss 四档可得, 200/good 等按 stable 规则近似)
export type LazerCountsSource = "json" | "ushort";

// lazer mod: acronym + 原始 settings (前端只展示非默认项)
export interface LazerMod {
  acronym: string;
  settings: Record<string, unknown>;
}

// GET /api/lazer/browse 行结构 (file 为 load-replay 的键, 亦是列表行 key)
export interface LazerScoreRow {
  file: string;
  mode: number; // 3 = mania
  beatmapMd5: string;
  playerName: string;
  perfect: number;
  great: number;
  good: number;
  ok: number;
  meh: number;
  miss: number;
  countsSource: LazerCountsSource;
  totalScore: number;
  maxCombo: number;
  fullCombo: boolean;
  mods: LazerMod[];
  playedMs: number;
  onlineId: number;
  title: string;
  artist: string;
  creator: string;
  version: string;
  keyCount: string;
  // 未命中谱面时为 -1
  sr: number;
  sunnySr: number;
  bpm: number;
  lengthMs: number;
  // lazer 无 stable pp
  pp: null;
  // Songs 索引命中 (false = 不可载入分析)
  hasBeatmap: boolean;
  hasReplay: true;
}

export interface LazerBindResult {
  dir: string;
  count: number;
  lastScanMs: number;
}

export interface LazerBrowseResult {
  total: number;
  returned: number;
  scores: LazerScoreRow[];
}

// /api/analysis/{sid}/summary 的 lazer 附加块 (scoreSource === "lazer" 时存在)
export interface LazerSummaryInfo {
  countsSource: LazerCountsSource;
  acc305: number;
  rank: string;
  // 声明值六档 (json statistics 为准; ushort 回退时为兼容计数换算)
  counts: JudgementCounts;
}

// legacy mod flags (key mods 1K-9K are handled separately in ScoreBrowser)
export const MOD_NAMES: [number, string][] = [
  [1, "NF"], [2, "EZ"], [4, "TD"], [8, "HD"], [16, "HR"], [32, "SD"],
  [64, "DT"], [128, "RX"], [256, "HT"], [512, "NC"], [1024, "FL"],
  [2048, "AT"], [4096, "SO"], [8192, "AP"], [16384, "PF"], [1048576, "FI"],
  [2097152, "RD"], [4194304, "CN"], [8388608, "TP"], [33554432, "KC"],
  [536870912, "V2"],
];
// ---- 左右手分析 (POST /api/analysis/{sid}/hands) ----
// 单侧判定计数 (幽灵物件由后端排除; miss 仅展示, 不进 n/acc/偏差)
export interface HandJudgementCounts {
  max: number;
  c300: number;
  c200: number;
  c100: number;
  c50: number;
  miss: number;
}
export interface HandSideStats {
  counts: HandJudgementCounts;
  n: number;
  // acc 两位小数, 打早/打晚平均三位小数 (后端已取整; 符号保留, 打早为负)
  acc: number;
  earlyMeanMs: number;
  lateMeanMs: number;
  // 无样本 (该侧 offset 均值为空) 标记: false ⇒ 前端显示 -
  hasEarly: boolean;
  hasLate: boolean;
}
export interface HandAnalysisResponse {
  keyCount: number;
  odd: boolean;
  centerToLeft: boolean;
  profile: "v1" | "v2";
  left: HandSideStats;
  right: HandSideStats;
}

export const KEY_MOD_NAMES: [number, string][] = [
  [1 << 26, "1K"], [1 << 27, "2K"], [1 << 28, "3K"],
  [1 << 15, "4K"], [1 << 16, "5K"], [1 << 17, "6K"], [1 << 18, "7K"],
  [1 << 19, "8K"], [1 << 24, "9K"],
];
