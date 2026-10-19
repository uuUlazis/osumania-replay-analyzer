import { useEffect, useState } from "react";
import { api } from "../api/client";

interface Props {
  onLoad: (
    replayPath: string,
    beatmapPath: string,
    skinFolder?: string,
    audioPath?: string,
  ) => void;
  loading: boolean;
  onScoresLoaded: (
    path: string,
    result: import("../models/types").ScoresLoadResult,
  ) => void;
  onScoresDatar: (folder: string) => void;
  onScoresSongs: (folder: string) => void;
  onOpenBrowser: () => void;
  browserReady: boolean;
  onSkinFolder: (folder: string | undefined) => void;
  resolvedPaths: {
    replayPath: string;
    beatmapPath: string;
    audioPath?: string;
  } | null;
}

const REPLAY_FILTERS = [
  { name: "osu! Replay (*.osr)", pattern: "*.osr" },
  { name: "All Files", pattern: "*.*" },
];
const BEATMAP_FILTERS = [
  { name: "osu! Beatmap (*.osu)", pattern: "*.osu" },
  { name: "All Files", pattern: "*.*" },
];
const AUDIO_FILTERS = [
  {
    name: "Audio (*.mp3;*.ogg;*.wav;*.flac;*.m4a)",
    pattern: "*.mp3;*.ogg;*.wav;*.flac;*.m4a",
  },
  { name: "All Files", pattern: "*.*" },
];
const SCORES_FILTERS = [
  { name: "osu! scores.db", pattern: "*.db" },
  { name: "All Files", pattern: "*.*" },
];

export default function FileLoader({
  onLoad,
  loading,
  onScoresLoaded,
  onScoresDatar,
  onScoresSongs,
  onOpenBrowser,
  browserReady,
  onSkinFolder,
  resolvedPaths,
}: Props) {
  const [replayPath, setReplayPath] = useState("");
  const [beatmapPath, setBeatmapPath] = useState("");
  const [skinFolder, setSkinFolder] = useState<string | undefined>();
  const [audioPath, setAudioPath] = useState<string | undefined>();
  const [scoresPath, setScoresPath] = useState<string | undefined>();
  const [scoresInfo, setScoresInfo] = useState("");
  const [dataRAuto, setDataRAuto] = useState(false);
  const [dataRPath, setDataRPath] = useState("");
  const [dataRHint, setDataRHint] = useState("");
  const [songsPath, setSongsPath] = useState("");
  const [scoresLoading, setScoresLoading] = useState(false);
  const [dialogError, setDialogError] = useState("");
  const [lastPaths, setLastPaths] = useState<
    { scores: string; dataR: string; songs: string } | null
  >(null);
  const [restoreOpen, setRestoreOpen] = useState(false);
  const [restoring, setRestoring] = useState(false);
  const [lastSkin, setLastSkin] = useState<string | null>(null);
  const [restoringSkin, setRestoringSkin] = useState(false);

  // Restart-persistent last-session state. The backend stores it in
  // %LOCALAPPDATA%\ManiaReplayAnalyzer\last_state.json because the page
  // origin (port) changes on every launch, which resets localStorage.
  const rememberPaths = (scores: string, dataR: string, songs: string) => {
    if (!scores || !dataR || !songs) return;
    const v = { scores, dataR, songs };
    setLastPaths(v);
    api.saveLastState(v).catch(() => {});
  };
  const rememberSkin = (folder: string) => {
    if (!folder) return;
    setLastSkin(folder);
    api.saveLastState({ skin: folder }).catch(() => {});
  };

  useEffect(() => {
    let alive = true;
    api.lastState().then((s) => {
      if (!alive || !s) return;
      if (s.scores && s.dataR && s.songs)
        setLastPaths({ scores: s.scores, dataR: s.dataR, songs: s.songs });
      if (s.skin) setLastSkin(s.skin);
    }).catch(() => {});
    return () => {
      alive = false;
    };
  }, []);

  // Any finished load (manual pick or score-browser confirm) pushes its
  // resolved paths back into the inputs so the latest selection always wins.
  // Without this, a score-browser load kept stale manual paths on display.
  useEffect(() => {
    if (!resolvedPaths) return;
    setReplayPath(resolvedPaths.replayPath || "");
    setBeatmapPath(resolvedPaths.beatmapPath || "");
    setAudioPath(resolvedPaths.audioPath || undefined);
  }, [resolvedPaths]);

  const pick = async (
    action: () => Promise<{ path?: string; code?: string }>,
    apply: (path: string | undefined) => void,
  ) => {
    try {
      const res = await action();
      if (res.code === "DIALOG_CANCELLED") return;  // user closed the dialog
      apply(res.path);
    } catch (e) {
      setDialogError(e instanceof Error ? e.message : String(e));
    }
  };
  const pickReplay = () =>
    pick(
      () => api.dialogOpenFile("选择回放文件 (.osr)", REPLAY_FILTERS),
      (p) => setReplayPath(p ?? ""),
    );
  const pickBeatmap = () =>
    pick(
      () => api.dialogOpenFile("选择谱面文件 (.osu)", BEATMAP_FILTERS),
      (p) => setBeatmapPath(p ?? ""),
    );
  const pickSkin = () =>
    pick(
      () => api.dialogOpenFolder("选择皮肤文件夹 (可选)"),
      (p) => {
        setSkinFolder(p);
        onSkinFolder(p);
        if (p) rememberSkin(p);
      },
    );
  const pickAudio = () =>
    pick(
      () => api.dialogOpenFile("选择音乐文件 (可选)", AUDIO_FILTERS),
      (p) => setAudioPath(p),
    );

  // scores.db loads immediately on pick (independent of 加载分析).
  const pickScores = async () => {
    try {
      const res = await api.dialogOpenFile("选择 osu! scores.db", SCORES_FILTERS);
      if (res.code === "DIALOG_CANCELLED" || !res.path) return;
      setScoresPath(res.path);
      setScoresInfo("");
      setScoresLoading(true);
      try {
        const result = await api.scoresLoad(res.path);
        setScoresInfo(result.total + " 条成绩");
        setDataRAuto(result.dataRAuto);
        setDataRPath(result.dataRFolder);
        setDataRHint(result.dataRHint);
        onScoresLoaded(res.path, result);
        rememberPaths(res.path, result.dataRFolder || dataRPath, songsPath);
      } finally {
        setScoresLoading(false);
      }
    } catch (e) {
      setDialogError(e instanceof Error ? e.message : String(e));
    }
  };

  // Data\r override when auto-derivation from the scores.db root fails,
  // or the user prefers an explicit pick.
  const pickDataR = () =>
    pick(
      () => api.dialogOpenFolder("选择 osu! Data\r 目录 (只读)"),
      async (p) => {
        if (!p) return;
        try {
          await api.scoresDataR(p);
          setDataRPath(p);
          setDataRAuto(false);
          setDataRHint("");
          onScoresDatar(p);
          if (scoresPath) rememberPaths(scoresPath, p, songsPath);
        } catch (e) {
          setDialogError(e instanceof Error ? e.message : String(e));
        }
      },
    );

  // Songs folder: user-selected, read-only; resolves beatmaps + audio.
  const pickSongs = () =>
    pick(
      () => api.dialogOpenFolder("选择 osu! Songs 目录 (只读)"),
      async (p) => {
        if (!p) return;
        try {
          await api.scoresSongs(p);
          setSongsPath(p);
          onScoresSongs(p);
          if (scoresPath && dataRPath) rememberPaths(scoresPath, dataRPath, p);
        } catch (e) {
          setDialogError(e instanceof Error ? e.message : String(e));
        }
      },
    );

  // Restore last scores.db / Data\r / Songs session from localStorage.
  const restoreLastPaths = async () => {
    if (!lastPaths || restoring) return;
    setRestoring(true);
    setDialogError("");
    try {
      const result = await api.scoresLoad(lastPaths.scores);
      setScoresPath(lastPaths.scores);
      setScoresInfo(result.total + " 条成绩");
      setDataRAuto(result.dataRAuto);
      setDataRPath(result.dataRFolder);
      setDataRHint(result.dataRHint);
      onScoresLoaded(lastPaths.scores, result);
      if (lastPaths.dataR !== result.dataRFolder) {
        await api.scoresDataR(lastPaths.dataR);
        setDataRPath(lastPaths.dataR);
        setDataRAuto(false);
        setDataRHint("");
      }
      onScoresDatar(lastPaths.dataR);
      await api.scoresSongs(lastPaths.songs);
      setSongsPath(lastPaths.songs);
      onScoresSongs(lastPaths.songs);
      setRestoreOpen(false);
    } catch (e) {
      setDialogError(e instanceof Error ? e.message : String(e));
    } finally {
      setRestoring(false);
    }
  };

  // Re-apply last used skin (hot-swaps when a session is already loaded).
  const restoreLastSkin = () => {
    if (!lastSkin || restoringSkin) return;
    setRestoringSkin(true);
    try {
      setSkinFolder(lastSkin);
      onSkinFolder(lastSkin);
    } finally {
      setRestoringSkin(false);
    }
  };

  const effReplay = replayPath || resolvedPaths?.replayPath || "";
  const effBeatmap = beatmapPath || resolvedPaths?.beatmapPath || "";
  const effAudio = audioPath || resolvedPaths?.audioPath;
  const canLoad = effReplay !== "" && effBeatmap !== "" && !loading;

  return (
    <div className="panel loader-panel">
      <h2>文件</h2>
      <div className="pick-row">
        <button className="btn" onClick={pickReplay}>回放 .osr</button>
        <span className="path" title={replayPath || resolvedPaths?.replayPath}>
          {replayPath || resolvedPaths?.replayPath || "未选择"}
        </span>
      </div>
      <div className="pick-row">
        <button className="btn" onClick={pickBeatmap}>谱面 .osu</button>
        <span className="path" title={beatmapPath || resolvedPaths?.beatmapPath}>
          {beatmapPath || resolvedPaths?.beatmapPath || "未选择"}
        </span>
      </div>
      <div className="pick-row">
        <button
          className="btn"
          onClick={pickAudio}
          title={audioPath || resolvedPaths?.audioPath}
        >
          {audioPath || resolvedPaths?.audioPath ? "音乐已选" : "音乐(可选)"}
        </button>
        <span className="path" title={audioPath || resolvedPaths?.audioPath}>
          {audioPath || resolvedPaths?.audioPath || "未选择"}
        </span>
      </div>
      <div className="pick-row half2">
        <div className="half">
          <button className="btn" onClick={pickSkin} title={skinFolder}>
            {skinFolder ? "皮肤已选" : "皮肤(可选)"}
          </button>
          <span className="path" title={skinFolder}>
            {skinFolder || "未选择"}
          </span>
        </div>
        <div className="half">
          <button
            className="btn"
            disabled={!lastSkin || restoringSkin}
            onClick={restoreLastSkin}
            title={lastSkin ? "恢复上次使用的皮肤: " + lastSkin : "暂无上次使用的皮肤"}
          >
            上次皮肤
          </button>
        </div>
      </div>
      <div className="loader-sep" />
      <div className="pick-row half2">
        <div className="half">
          <button
            className="btn"
            onClick={pickScores}
            disabled={scoresLoading}
            title={scoresPath}
          >
            {scoresLoading ? "读取中…" : scoresPath ? "scores.db 已选" : "scores.db"}
          </button>
          <span className="path" title={scoresPath}>
            {scoresInfo || scoresPath || "未选择"}
          </span>
        </div>
        <div className="half">
          <button
            className="btn"
            onClick={pickDataR}
            disabled={!scoresPath}
            title={dataRPath || "选择 Data\\r 回放目录 (只读)"}
          >
            {dataRPath
              ? dataRAuto
                ? "Data\\r 已自动找到"
                : "Data\\r 已选"
              : "Data\\r 目录"}
          </button>
          <span className="path" title={dataRPath}>
            {dataRPath || dataRHint || "未选择"}
          </span>
        </div>
      </div>
      <div className="pick-row half2">
        <div className="half">
          <button
            className="btn"
            onClick={pickSongs}
            title={songsPath}
          >
            {songsPath ? "Songs 已选" : "Songs 目录"}
          </button>
          <span className="path" title={songsPath}>
            {songsPath || "未选择"}
          </span>
        </div>
        <div className="half">
          <button
            className="btn primary"
            disabled={!browserReady || !scoresPath || !dataRPath || !songsPath}
            onClick={onOpenBrowser}
            title={
              scoresPath && dataRPath && songsPath
                ? "浏览 scores.db 中的成绩并一键载入回放"
                : "需要先选择 scores.db、Data\\r 与 Songs 目录"
            }
          >
            回放列表
          </button>
          <button
            className="btn primary"
            disabled={!lastPaths}
            onClick={() => setRestoreOpen(true)}
            title={lastPaths ? "恢复上次的 scores.db / Data\\r / Songs 位置" : "暂无上次的位置记录"}
          >
            上次位置
          </button>
        </div>
      </div>
      <div className="loader-sep" />
      <div className="pick-row">
        <button
          className="btn primary"
          disabled={!canLoad}
          onClick={() => onLoad(effReplay, effBeatmap, skinFolder, effAudio)}
        >
          {loading ? "分析中…" : "加载分析"}
        </button>
      </div>
      {dialogError ? <div className="error-banner">{dialogError}</div> : null}
      {restoreOpen && lastPaths ? (
        <div className="score-modal-backdrop" onClick={() => setRestoreOpen(false)}>
          <div className="score-modal restore-modal" onClick={(e) => e.stopPropagation()}>
            <div className="score-modal-head">
              <h2>恢复上次位置</h2>
            </div>
            <div className="restore-paths">
              <div className="restore-path-row">
                <span className="restore-label">scores.db</span>
                <span className="path" title={lastPaths.scores}>{lastPaths.scores}</span>
              </div>
              <div className="restore-path-row">
                <span className="restore-label">Data\r</span>
                <span className="path" title={lastPaths.dataR}>{lastPaths.dataR}</span>
              </div>
              <div className="restore-path-row">
                <span className="restore-label">Songs</span>
                <span className="path" title={lastPaths.songs}>{lastPaths.songs}</span>
              </div>
            </div>
            {dialogError ? <div className="error-banner">{dialogError}</div> : null}
            <div className="score-modal-foot">
              <button className="btn" onClick={() => setRestoreOpen(false)}>取消</button>
              <button
                className="btn primary"
                disabled={restoring}
                onClick={restoreLastPaths}
              >
                {restoring ? "载入中…" : "确认载入"}
              </button>
            </div>
          </div>
        </div>
      ) : null}
    </div>
  );
}
