// Typed fetch helpers for the local HTTP API.
import type { ApiResponse } from "../models/types";

export class ApiError extends Error {
  code: string;
  constructor(code: string, message: string) {
    super(message);
    this.code = code;
  }
}

async function call<T>(
  method: "GET" | "POST",
  path: string,
  body?: unknown,
): Promise<T> {
  const res = await fetch(path, {
    method,
    headers: body !== undefined ? { "Content-Type": "application/json" } : {},
    body: body !== undefined ? JSON.stringify(body) : undefined,
  });
  let json: ApiResponse<T>;
  try {
    json = (await res.json()) as ApiResponse<T>;
  } catch {
    // Non-JSON or empty body (server closed the connection mid-request):
    // surface a readable error instead of a raw parse failure.
    throw new ApiError(
      "EMPTY_RESPONSE",
      "服务器返回空响应 (HTTP " + res.status + " " + res.statusText + ")",
    );
  }
  if (!json.ok) throw new ApiError(json.error.code, json.error.message);
  return json.data;
}

export const api = {
  get: <T>(path: string) => call<T>("GET", path),
  post: <T>(path: string, body: unknown) => call<T>("POST", path, body),
  health: () => call<{ status: string }>("GET", "/api/health"),
  appInfo: () =>
    call<{ name: string; apiVersion: number; platform: string }>(
      "GET",
      "/api/app/info",
    ),
  dialogOpenFile: (title: string, filters?: { name: string; pattern: string }[]) =>
    call<{ path?: string; code?: string }>("POST", "/api/dialog/open-file", {
      title,
      filters: filters ?? [{ name: "All Files", pattern: "*.*" }],
    }),
  dialogOpenFolder: (title: string, initialDir?: string) =>
    call<{ path?: string; code?: string }>("POST", "/api/dialog/open-folder", {
      title,
      ...(initialDir ? { initialDir } : {}),
    }),
  loadReplay: (
    replayPath: string,
    beatmapPath: string,
    skinFolder?: string,
    audioPath?: string,
  ) =>
    call<{ sessionId: string }>("POST", "/api/replays/load", {
      replayPath,
      beatmapPath,
      skinFolder,
      audioPath,
    }),
  replayMetadata: (sid: string) =>
    call<import("../models/types").ReplayMetadata>(
      "GET",
      `/api/replays/${sid}/metadata`,
    ),
  frames: (sid: string, fromMs: number, toMs: number, limit = 20000) =>
    call<{
      from: number;
      to: number | null;
      returned: number;
      totalInRange: number;
      frames: import("../models/types").ReplayFrameDto[];
    }>("GET", `/api/replays/${sid}/frames?from=${fromMs}&to=${toMs}&limit=${limit}`),
  beatmapMetadata: (sid: string) =>
    call<import("../models/types").BeatmapMetadata>(
      "GET",
      `/api/beatmaps/${sid}/metadata`,
    ),
  objects: (sid: string, fromMs: number, toMs: number) =>
    call<{ from: number; to: number | null; objects: import("../models/types").ManiaObject[] }>(
      "GET",
      `/api/beatmaps/${sid}/objects?from=${fromMs}&to=${toMs}`,
    ),
  summary: (sid: string) =>
    call<import("../models/types").AnalysisSummary>(
      "GET",
      `/api/analysis/${sid}/summary`,
    ),
  hits: (
    sid: string,
    fromMs: number,
    toMs: number,
    limit = 5000,
    version?: "v1" | "v2",
  ) =>
    call<{ hits: import("../models/types").Hit[] }>(
      "GET",
      `/api/analysis/${sid}/hits?from=${fromMs}&to=${toMs}&limit=${limit}` +
        (version ? `&version=${version}` : ""),
    ),
  audioUrl: (sid: string) => `/api/audio/${sid}`,
  // 左右手分析: startMs/endMs 同时给 = 选段范围, 不给 = 全局
  hands: (
    sid: string,
    centerToLeft: boolean,
    startMs?: number,
    endMs?: number,
  ) =>
    call<import("../models/types").HandAnalysisResponse>(
      "POST",
      `/api/analysis/${sid}/hands`,
      {
        centerToLeft,
        ...(startMs != null && endMs != null ? { startMs, endMs } : {}),
      },
    ),
  segment: (sid: string, startMs: number, endMs: number) =>
    call<import("../models/types").SegmentResult>(
      "POST",
      `/api/analysis/${sid}/segment`,
      { startMs, endMs },
    ),
  scoresLoad: (path: string) =>
    call<import("../models/types").ScoresLoadResult>(
      "POST",
      "/api/scores/load",
      { path },
    ),
  skinsSelect: (sessionId: string, folder: string) =>
    call<{ folder: string }>("POST", "/api/skins/select", { sessionId, folder }),
  scoresDataR: (folder: string) =>
    call<{ dataRFolder: string }>("POST", "/api/scores/datar", { folder }),
  scoresSongs: (folder: string) =>
    call<{ songsFolder: string }>("POST", "/api/scores/songs", { folder }),
  lastState: async () =>
    call<{
      scores?: string; dataR?: string; songs?: string; skin?: string;
      volume?: number;
      showInputJudges?: boolean;
      debugLog?: boolean;
      scoreColWidths?: number[];
      // Phase L1: 数据源模式 + lazer 数据目录 (后端按需透传, 缺省时前端用默认值)
      scoreSourceMode?: import("../models/types").ScoreSourceMode;
      lazerDir?: string;
      // lazer 模式的 Songs 记忆 (独立键, 不与 stable 上次位置三元组共用)
      lazerSongs?: string;
      // 左右手分析: 奇数 key 时中键归属 (true=归左, 缺省 false=归左)
      handCenterToLeft?: boolean;
      // 预览落速 (lazer mania scale 1..40)
      scrollSpeed?: number;
    }>("GET", "/api/last-state"),
  saveLastState: (patch: Record<string, string | number | boolean | null | number[]>) =>
    call("POST", "/api/last-state", patch),
  scoresBrowse: (mode: number, force?: boolean) =>
    call<{
      total: number;
      replays: number;
      beatmaps: number;
      returned: number;
      scores: import("../models/types").ScoreBrowseRow[];
    }>("POST", "/api/scores/browse", { mode, force }),
  scoresLoadReplay: (playedTicks: string, skinFolder?: string) =>
    call<{
      sessionId: string;
      replayPath: string;
      beatmapPath: string;
      audioPath: string;
    }>("POST", "/api/scores/load-replay", { playedTicks, skinFolder }),
  scoresList: (mode: number, limit = 2000) =>
    call<import("../models/types").ScoresListResult>(
      "GET",
      "/api/scores?mode=" + mode + "&limit=" + limit,
    ),
  // ---- lazer 数据通道 (Phase L1) ----
  // 绑定 lazer 数据目录 (含 client.realm / scores.db), 后端扫描成绩库
  lazerBind: (dir: string, songsDir?: string) =>
    call<import("../models/types").LazerBindResult>(
      "POST",
      "/api/lazer/bind",
      { dir, ...(songsDir ? { songsDir } : {}) },
    ),
  lazerUnbind: () => call<Record<string, never>>("POST", "/api/lazer/unbind"),
  // force=1: 跳过缓存重扫 lazer 数据目录
  lazerBrowse: (force?: boolean) =>
    call<import("../models/types").LazerBrowseResult>(
      "GET",
      "/api/lazer/browse?force=" + (force ? 1 : 0),
    ),
  // 与 /api/replays/load 同构 (返回 sessionId); audioPath 为可选扩展字段,
  // null 表示后端明确无法解析音频, 缺省时前端按谱面已命中尝试加载
  lazerLoadReplay: (file: string) =>
    call<{
      sessionId: string;
      replayPath: string;
      beatmapPath: string;
      audioPath?: string | null;
      beatmapMd5: string;
    }>("POST", "/api/lazer/load-replay", { file }),
};
