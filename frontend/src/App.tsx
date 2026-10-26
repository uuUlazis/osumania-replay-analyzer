import { useCallback, useEffect, useState } from "react";
import { api, ApiError } from "./api/client";
import { sessionStore, type SessionSnapshot } from "./app/sessionStore";
import { playerStore } from "./app/playerStore";
import type { ScoreSourceMode } from "./models/types";
import Layout from "./components/Layout";
import FileLoader from "./components/FileLoader";
import ScoreBrowser from "./components/ScoreBrowser";
import HandAnalysisWindow, { type HandScope } from "./components/HandAnalysisWindow";
import AnalysisSummary from "./components/AnalysisSummary";
import SegmentPanel from "./components/SegmentPanel";
import ReplayPreview from "./components/ReplayPreview";
import Timeline from "./components/Timeline";
import Transport from "./components/Transport";
import AudioSync from "./components/AudioSync";

export default function App() {
  const [snap, setSnap] = useState<SessionSnapshot>(sessionStore.get());
  const [error, setError] = useState<string | null>(null);
  const [loading, setLoading] = useState(false);
  // 数据源模式: stable (.osr / scores.db) | lazer (lazer 数据目录)
  // 持久化到 last_state.scoreSourceMode
  const [mode, setMode] = useState<ScoreSourceMode>("stable");
  const [scoresDb, setScoresDb] = useState<
    (import("./models/types").ScoresLoadResult & { path: string }) | null
  >(null);
  const [browserOpen, setBrowserOpen] = useState(false);
  // 左右手分析小窗: null=关; {}=全局; {startMs,endMs}=选段 (两处入口共用一窗)
  const [handScope, setHandScope] = useState<HandScope | null>(null);
  const [skinFolder, setSkinFolder] = useState<string | undefined>();
  const [debugLog, setDebugLog] = useState(false);
  const [resolvedPaths, setResolvedPaths] = useState<{
    replayPath: string;
    beatmapPath: string;
    audioPath?: string;
  } | null>(null);

  useEffect(() => sessionStore.subscribe(() => setSnap(sessionStore.get())), []);
  useEffect(() => {
    api
      .lastState()
      .then((s) => {
        if (s && typeof s.volume === "number")
          playerStore.setVolume(s.volume);
        if (s && typeof s.showInputJudges === "boolean")
          playerStore.setShowInputJudges(s.showInputJudges);
        // 落速持久化 (Transport 修改时写入)
        if (s && typeof s.scrollSpeed === "number")
          playerStore.setScrollSpeed(s.scrollSpeed);
        if (s && typeof s.debugLog === "boolean")
          setDebugLog(s.debugLog);
        // 上次数据源模式: 从 config 恢复 (无记录时默认 stable);
        // 切换时 scoreSourceMode 持久化到 last_state (见 switchMode)。
        if (s && s.scoreSourceMode === "lazer") setMode("lazer");
      })
      .catch(() => {});
  }, []);

  // 右键菜单白名单: 输入控件放行原生菜单, 其余界面一律拦截
  useEffect(() => {
    const onContextMenu = (e: MouseEvent) => {
      const t = e.target;
      if (t instanceof Element && t.closest("input, textarea, select")) return;
      e.preventDefault();
    };
    document.addEventListener("contextmenu", onContextMenu);
    return () => document.removeEventListener("contextmenu", onContextMenu);
  }, []);

  // 载入后的公共数据获取 (stable 手动/成绩库 与 lazer 成绩库共用):
  // metadata / summary / 双规则 hits / objects / frames → 各 store。
  const hydrateSession = useCallback(async (sessionId: string) => {
    const [replay, beatmap, summary, hits, hitsV2, objects, frames] =
      await Promise.all([
        api.replayMetadata(sessionId),
        api.beatmapMetadata(sessionId),
        api.summary(sessionId),
        api.hits(sessionId, -8640000000, 8640000000, 50000),
        api.hits(sessionId, -8640000000, 8640000000, 50000, "v2"),
        api.objects(sessionId, -8640000000, 8640000000),
        api.frames(sessionId, -8640000000, 8640000000, 400000),
      ]);
    const mods = replay.mods ?? 0;
    // DT (1<<6)/NC (1<<9) 1.5x, HT (1<<8) 0.75x; lazer 局 (特殊 mod 适配)
    // 用 summary 的实际 rate (尾块 speed_change 小数倍率, 如 DT 1.25×),
    // 音频同步与滚动速度不再被固定 1.5×/0.75× 失真。
    const summaryRate =
      typeof summary.rate === "number" && summary.rate > 0
        ? summary.rate
        : null;
    playerStore.setRate(
      summaryRate ?? (mods & 64 || mods & 512 ? 1.5 : mods & 256 ? 0.75 : 1),
    );
    // NC = 升调 (重采样 1:1 映射); DT/HT = 不升调 (WSOLA 拉伸, timeBase=rate)。
    // stable 看 mod 位 512, lazer 看尾块 acronym。AudioSync load 时消费。
    const lazerNc =
      summary.lazerMods?.some((m) => m.acronym === "NC") ?? false;
    playerStore.setNightcore(!!(mods & 512) || lazerNc);

    playerStore.setDuration(
      Math.max(
        objects.objects.length
          ? objects.objects[objects.objects.length - 1].endTimeMs ??
              objects.objects[objects.objects.length - 1].startTimeMs
          : 0,
        frames.frames.length ? frames.frames[frames.frames.length - 1].timeMs : 0,
      ) + 2000,
    );
    playerStore.seek(0);
    playerStore.setSelection(null);
    sessionStore.set({
      sessionId,
      replay,
      beatmap,
      analysis: summary,
      events: hits.hits,
      eventsV2: hitsV2.hits,
      objects: objects.objects,
      frames: frames.frames,
      lastEventTimeMs: summary.lastEventTimeMs,
    });
  }, []);

  const loadSession = useCallback(
    async (
      replayPath: string,
      beatmapPath: string,
      skinFolder?: string,
      audioPath?: string,
    ) => {
      setLoading(true);
      setError(null);
      try {
        const { sessionId } = await api.loadReplay(
          replayPath,
          beatmapPath,
          skinFolder,
          audioPath,
        );
        setResolvedPaths({ replayPath, beatmapPath, audioPath });
        await hydrateSession(sessionId);
        playerStore.setMusicUrl(audioPath ? api.audioUrl(sessionId) : null);
      } catch (e) {
        const msg =
          e instanceof ApiError
            ? `${e.code}: ${e.message}`
            : String(e);
        setError(msg);
        sessionStore.reset();
        playerStore.setRate(1);
        playerStore.setMusicUrl(null);
        setResolvedPaths(null);
      } finally {
        setLoading(false);
      }
    },
    [hydrateSession],
  );

  // lazer 成绩载入: /api/lazer/load-replay 直接给 sessionId, 之后取数同 stable。
  // 音频: 后端未显式声明无音频 (audioPath != null) 时按谱面命中尝试加载。
  const loadLazerSession = useCallback(
    async (file: string) => {
      const r = await api.lazerLoadReplay(file);
      // 与 stable 成绩库载入一致: 把解析出的回放/谱面/音乐路径回填输入面板
      setResolvedPaths({
        replayPath: r.replayPath,
        beatmapPath: r.beatmapPath,
        audioPath: r.audioPath ?? undefined,
      });
      await hydrateSession(r.sessionId);
      playerStore.setMusicUrl(
        r.audioPath === null ? null : api.audioUrl(r.sessionId),
      );
      if (skinFolder) {
        try {
          await api.skinsSelect(r.sessionId, skinFolder);
          playerStore.bumpSkin();
        } catch {
          // 皮肤可选: 失败不阻断分析
        }
      }
    },
    [hydrateSession, skinFolder],
  );

  // 模式切换: 清理进行中的会话/列表状态 (跟随现有清理逻辑),
  // 并把选择持久化到 last_state.scoreSourceMode
  const switchMode = (m: ScoreSourceMode) => {
    if (m === mode || loading) return;
    setMode(m);
    setError(null);
    setBrowserOpen(false);
    setResolvedPaths(null);
    sessionStore.reset();
    playerStore.setRate(1);
    playerStore.setMusicUrl(null);
    playerStore.setSelection(null);
    playerStore.seek(0);
    api.saveLastState({ scoreSourceMode: m }).catch(() => {});
  };

  const applySkin = (folder: string | undefined) => {
    setSkinFolder(folder);
    if (snap.sessionId) {
      // 空串 = 卸载自定义皮肤, 切回内置默认渲染 (后端 skins/select 支持空串)
      api.skinsSelect(snap.sessionId, folder ?? "")
        .then(() => playerStore.bumpSkin())
        .catch((e) =>
          setError(e instanceof Error ? e.message : String(e)));
    }
  };

  const ready = snap.sessionId !== null;

  // 调试日志开关: 状态持久化到 last-state, 后端写 <exe 目录>/debug/debug.log
  const toggleDebugLog = (v: boolean) => {
    setDebugLog(v);
    api.saveLastState({ debugLog: v }).catch(() => {});
  };

  return (
    <Layout title="osu!mania Replay Analyzer">
      <div className="topbar">
        <h1>osu!mania Replay Analyzer</h1>
        <div className="spacer" />
        <label
          style={{ display: "flex", alignItems: "center", gap: 6, fontSize: 13, cursor: "pointer" }}
          title="开启后在 exe 同级 debug/debug.log 记录会话/错误/SR 计算等诊断日志"
        >
          <input type="checkbox" checked={debugLog} onChange={(e) => toggleDebugLog(e.target.checked)} />
          输出调试日志
        </label>
        <div className="mode-switch" role="group" aria-label="数据源模式">
          <button
            className={"mode-btn" + (mode === "stable" ? " active" : "")}
            disabled={loading}
            title="stable 数据通道 (.osr 回放 / scores.db 成绩库)"
            onClick={() => switchMode("stable")}
          >
            stable
          </button>
          <button
            className={"mode-btn" + (mode === "lazer" ? " active" : "")}
            disabled={loading}
            title="lazer 数据通道 (lazer 数据目录成绩库)"
            onClick={() => switchMode("lazer")}
          >
            lazer
          </button>
        </div>
      </div>
      <div className="main">
        <div className="left-panel">
          <FileLoader
            mode={mode}
            onLoad={loadSession}
            loading={loading}
            onScoresLoaded={(path, result) => setScoresDb({ ...result, path })}
            onScoresDatar={() => undefined}
            onScoresSongs={() => undefined}
            onOpenBrowser={() => setBrowserOpen(true)}
            browserReady={!!scoresDb}
            onSkinFolder={applySkin}
            resolvedPaths={resolvedPaths}
          />
          <ScoreBrowser
          open={browserOpen}
          mode={mode}
          onClose={() => setBrowserOpen(false)}
          loading={loading}
          onPlay={(rowKey) => {
            setBrowserOpen(false);
            void (async () => {
              setLoading(true);
              setError(null);
              try {
                if (mode === "lazer") {
                  await loadLazerSession(rowKey);
                } else {
                  const r = await api.scoresLoadReplay(rowKey, skinFolder);
                  await loadSession(
                    r.replayPath,
                    r.beatmapPath,
                    skinFolder,
                    r.audioPath || undefined,
                  );
                }
              } catch (e) {
                setError(e instanceof Error ? e.message : String(e));
              } finally {
                setLoading(false);
              }
            })();
          }}
        />
          <HandAnalysisWindow scope={handScope} onClose={() => setHandScope(null)} />
          {error && <div className="error-banner">{error}</div>}
          {!ready && !error && (
            <div className="empty-hint">
              {mode === "lazer"
                ? "绑定 lazer 数据目录后，从成绩库载入 lazer 成绩开始分析"
                : "选择 .osr 回放与对应 .osu 谱面开始分析"}
            </div>
          )}
          {ready && (
            <>
              <AnalysisSummary
                onOpenHandAnalysis={() => setHandScope({})}
              />
              <SegmentPanel
                onOpenHandAnalysis={(range) => setHandScope(range)}
              />
            </>
          )}
        </div>
        <div className="right-panel">
          {ready && (
            <>
              <AudioSync />
              <Timeline />
              <ReplayPreview />
              <Transport />
            </>
          )}
        </div>
      </div>
    </Layout>
  );
}
