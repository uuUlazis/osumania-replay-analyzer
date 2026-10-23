import { useCallback, useEffect, useState } from "react";
import { api, ApiError } from "./api/client";
import { sessionStore, type SessionSnapshot } from "./app/sessionStore";
import { playerStore } from "./app/playerStore";
import Layout from "./components/Layout";
import FileLoader from "./components/FileLoader";
import ScoreBrowser from "./components/ScoreBrowser";
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
  const [scoresDb, setScoresDb] = useState<
    (import("./models/types").ScoresLoadResult & { path: string }) | null
  >(null);
  const [browserOpen, setBrowserOpen] = useState(false);
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
        if (s && typeof s.debugLog === "boolean")
          setDebugLog(s.debugLog);
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
        // DT (1<<6) and NC (1<<9) play at 1.5x, HT (1<<8) at 0.75x
        playerStore.setRate(
          mods & 64 || mods & 512 ? 1.5 : mods & 256 ? 0.75 : 1,
        );
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
    [],
  );

  const applySkin = (folder: string | undefined) => {
    setSkinFolder(folder);
    if (snap.sessionId && folder) {
      api.skinsSelect(snap.sessionId, folder)
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
      </div>
      <div className="main">
        <div className="left-panel">
          <FileLoader
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
          onClose={() => setBrowserOpen(false)}
          loading={loading}
          onPlay={(playedTicks) => {
            setBrowserOpen(false);
            void (async () => {
              setLoading(true);
              setError(null);
              try {
                const r = await api.scoresLoadReplay(playedTicks, skinFolder);
                await loadSession(
                  r.replayPath,
                  r.beatmapPath,
                  skinFolder,
                  r.audioPath || undefined,
                );
              } catch (e) {
                setError(e instanceof Error ? e.message : String(e));
              } finally {
                setLoading(false);
              }
            })();
          }}
        />
          {error && <div className="error-banner">{error}</div>}
          {!ready && !error && (
            <div className="empty-hint">
              选择 .osr 回放与对应 .osu 谱面开始分析
            </div>
          )}
          {ready && (
            <>
              <AnalysisSummary />
              <SegmentPanel />
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
