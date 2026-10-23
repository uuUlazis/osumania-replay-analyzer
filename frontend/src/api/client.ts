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
  dialogOpenFolder: (title: string) =>
    call<{ path?: string; code?: string }>("POST", "/api/dialog/open-folder", {
      title,
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
};
