import { useEffect, useState } from "react";
import { api } from "../api/client";

interface Props {
  mode: import("../models/types").ScoreSourceMode;
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

// lastScanMs 的语义 (时间戳 / 扫描耗时) 联调待定: 按量级自适应显示
function lazerScanText(ms: number): string {
  if (ms > 1e12) {
    const d = new Date(ms);
    const p = (n: number) => String(n).padStart(2, "0");
    return (
      "上次扫描 " + d.getFullYear() + "-" + p(d.getMonth() + 1) + "-" +
      p(d.getDate()) + " " + p(d.getHours()) + ":" + p(d.getMinutes()) + ":" +
      p(d.getSeconds())
    );
  }
  return "上次扫描耗时 " + ms + " ms";
}

export default function FileLoader({
  mode,
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
  const [dataRAuto, setDataRAuto] = useState(false);
  const [dataRPath, setDataRPath] = useState("");
  const [dataRHint, setDataRHint] = useState("");
  const [songsPath, setSongsPath] = useState("");
  const [songsAuto, setSongsAuto] = useState(false);
  const [songsHint, setSongsHint] = useState("");
  const [scoresLoading, setScoresLoading] = useState(false);
  const [dialogError, setDialogError] = useState("");
  const [lastPaths, setLastPaths] = useState<
    { scores: string; dataR: string; songs: string } | null
  >(null);
  const [restoreOpen, setRestoreOpen] = useState(false);
  const [restoring, setRestoring] = useState(false);
  const [lastSkin, setLastSkin] = useState<string | null>(null);
  const [restoringSkin, setRestoringSkin] = useState(false);
  // lazer 成绩源绑定 (仅 lazer 模式的成绩源区展示; stable 式「选中即绑定」,
  // 目录持久化到 last_state.lazerDir; 挂载时只记上次位置, 不自动回填/绑定)
  const [lazerDir, setLazerDir] = useState("");
  const [lastLazerDir, setLastLazerDir] = useState("");
  const [lastSongs, setLastSongs] = useState("");
  const [lazerInfo, setLazerInfo] = useState<
    import("../models/types").LazerBindResult | null
  >(null);
  const [lazerBusy, setLazerBusy] = useState<"bind" | null>(null);
  const [lazerError, setLazerError] = useState("");

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
      if (s.lazerSongs) setLastSongs(s.lazerSongs);  // 上次位置按钮的恢复来源 (不自动回填)
      if (s.skin) setLastSkin(s.skin);
      if (s.lazerDir) setLastLazerDir(s.lazerDir);
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
      setScoresLoading(true);
      try {
        const result = await api.scoresLoad(res.path);
        setDataRAuto(result.dataRAuto);
        setDataRPath(result.dataRFolder);
        setDataRHint(result.dataRHint);
        // Songs auto-derives on the same response; a failed derive clears
        // any previously bound folder exactly like Data\\r above.
        setSongsAuto(result.songsAuto);
        setSongsPath(result.songsFolder);
        setSongsHint(result.songsHint);
        onScoresLoaded(res.path, result);
        rememberPaths(
          res.path,
          result.dataRFolder || dataRPath,
          result.songsFolder || songsPath,
        );
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
          if (mode === "lazer") {
            // lazer 的 Songs 记忆独立成键 (lazerSongs), 不覆写 stable
            // 上次位置三元组 (scores/dataR/songs)。
            api.saveLastState({ lazerSongs: p }).catch(() => undefined);
            setSongsPath(p);
            onScoresSongs(p);
            // 已绑 (或已记) lazer 目录时重发绑定, 后端 songsFolder_ 立即跟随
            if (lazerInfo && (lazerDir || lastLazerDir))
              await api.lazerBind(lazerDir || lastLazerDir, p);
          } else {
            await api.scoresSongs(p);
            setSongsPath(p);
            onScoresSongs(p);
            if (scoresPath && dataRPath) rememberPaths(scoresPath, dataRPath, p);
          }
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
      // Auto-derive already bound the remembered Songs when it matches;
      // re-bind explicitly only on a difference (mirrors Data\\r above).
      if (lastPaths.songs !== result.songsFolder) {
        await api.scoresSongs(lastPaths.songs);
        setSongsAuto(false);
        setSongsHint("");
      } else {
        setSongsAuto(result.songsAuto);
        setSongsHint(result.songsHint);
      }
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

  // 切回默认显示皮肤 (卸载自定义皮肤; onSkinFolder("") 会通知后端卸载并
  // 触发内置回退渲染)。特例 (2026-10-25 用户约定): 不写 last_state.skin,
  // 上次自定义皮肤的记忆保留, 下次启动仍可从「上次皮肤」恢复。
  const restoreDefaultSkin = () => {
    setSkinFolder("");
    onSkinFolder("");
  };

  // ---- lazer 数据目录 (仅 lazer 模式) ----
  // stable 式「选中即绑定」: 目录对话框确认后立即扫描绑定 (对话框以上次
  // 位置为初始目录); 取消则不动。绑定成功才持久化 last_state.lazerDir。
  const bindLazerDir = async (dir: string) => {
    const d = dir.trim();
    if (!d || lazerBusy) return;
    setLazerBusy("bind");
    setLazerError("");
    setLazerDir(d);
    try {
      const info = await api.lazerBind(d, songsPath || undefined);
      setLazerInfo(info);
      setLazerDir(info.dir || d);
      api.saveLastState({ lazerDir: d }).catch(() => undefined);
    } catch (e) {
      setLazerError(e instanceof Error ? e.message : String(e));
    } finally {
      setLazerBusy(null);
    }
  };

  const pickLazerDir = () =>
    pick(
      // 上次位置: 对话框直接开在上次绑定 (或当前输入) 的目录。
      () =>
        api.dialogOpenFolder(
          "选择 osu!lazer 数据目录",
          lazerDir || lastLazerDir || undefined,
        ),
      (p) => {
        if (p) void bindLazerDir(p);
      },
    );

  // 上次位置: 恢复上次绑定的 lazer 目录 **与 Songs 目录** 并立即绑定。
  // 与 stable 的上次位置同构: 先弹窗预览, 确认后才应用; 成功才关弹窗并
  // 更新显示, 失败留在弹窗内报错 —— 不做乐观回填, 避免路径显示"闪一下"
  // 或显示已变化但实际未绑定的状态。
  const restoreLastLazer = async () => {
    if (lazerBusy || (!lastLazerDir && !lastSongs)) return;
    // 已绑定同一目录且 Songs 一致 → 无需重绑 (lazerBind 会重扫目录), 直接关弹窗。
    if (lazerInfo && lazerInfo.dir === lastLazerDir &&
        (!lastSongs || songsPath === lastSongs)) {
      setRestoreOpen(false);
      return;
    }
    setLazerBusy("bind");
    setLazerError("");
    try {
      if (lastLazerDir) {
        // lazerBind 携带 songsDir, 一次调用同时恢复两个绑定; 成功才更新
        // 显示与记忆。
        const info = await api.lazerBind(lastLazerDir, lastSongs || undefined);
        setLazerDir(info.dir || lastLazerDir);
        setLazerInfo(info);
        api.saveLastState({ lazerDir: lastLazerDir }).catch(() => undefined);
      }
      if (lastSongs) {
        // 仅恢复显示与记忆 (lazerSongs); 运行时绑定随下次 lazerBind 携带,
        // 不走 scoresSongs (避免覆写 stable 的 songs 记忆)。
        setSongsPath(lastSongs);
        api.saveLastState({ lazerSongs: lastSongs }).catch(() => undefined);
      }
      setRestoreOpen(false);
    } catch (e) {
      setLazerError(e instanceof Error ? e.message : String(e));
    } finally {
      setLazerBusy(null);
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
          <div style={{ display: "flex", gap: 4 }}>
            <button
              className="btn"
              disabled={!lastSkin || restoringSkin}
              onClick={restoreLastSkin}
              title={lastSkin ? "恢复上次使用的皮肤: " + lastSkin : "暂无上次使用的皮肤"}
            >
              上次皮肤
            </button>
            <button
              className="btn"
              disabled={!skinFolder}
              onClick={restoreDefaultSkin}
              title="卸载自定义皮肤, 切回内置默认渲染"
            >
              默认皮肤
            </button>
          </div>
        </div>
      </div>
      <div className="loader-sep" />
      {mode === "lazer" ? (
        <>
          <div className="pick-row half2">
            <div className="half">
              <button
                className="btn"
                disabled={lazerBusy !== null}
                onClick={pickLazerDir}
                title="选择 osu!lazer 数据目录 (确认后立即扫描绑定)"
              >
                {lazerBusy === "bind"
                  ? "绑定中…"
                  : lazerInfo
                    ? "lazer 目录已绑"
                    : "lazer 目录"}
              </button>
              <span
                className="path"
                title={
                  lazerInfo
                    ? lazerInfo.dir + " · " + lazerScanText(lazerInfo.lastScanMs)
                    : lazerDir
                }
              >
                {lazerInfo?.dir || lazerDir || "未选择"}
              </span>
            </div>
            <div className="half">
              <button className="btn" onClick={pickSongs} title={songsPath}>
                {songsPath ? "Songs 已选" : "Songs 目录"}
              </button>
              <span className="path" title={songsPath}>
                {songsPath || "未选择"}
              </span>
            </div>
          </div>
          <div className="pick-row half2">
            <div className="half">
              <button
                className="btn primary"
                disabled={!lazerInfo || !songsPath}
                onClick={onOpenBrowser}
                title={
                  !lazerInfo
                    ? "需要先选择 lazer 数据目录"
                    : !songsPath
                      ? "需要先选择 Songs 目录 (谱面关联)"
                      : "浏览 lazer 成绩并一键载入分析"
                }
              >
                成绩库
              </button>
            </div>
            <div className="half">
              <button
                className="btn primary"
                disabled={lazerBusy !== null || (!lastLazerDir && !lastSongs)}
                onClick={() => setRestoreOpen(true)}
                title={
                  lastLazerDir || lastSongs
                    ? "恢复上次的位置并绑定: " +
                      [lastLazerDir && "lazer=" + lastLazerDir,
                       lastSongs && "Songs=" + lastSongs].filter(Boolean).join(" · ")
                    : "暂无上次的位置记录"
                }
              >
                上次位置
              </button>
            </div>
          </div>
          {lazerError ? (
            <div className="error-banner" style={{ margin: "6px 0 0" }}>
              {lazerError}
            </div>
          ) : null}
        </>
      ) : (
        <>
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
                {scoresPath || "未选择"}
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
                {songsPath
                  ? songsAuto
                    ? "Songs 已自动找到"
                    : "Songs 已选"
                  : "Songs 目录"}
              </button>
              <span className="path" title={songsPath}>
                {songsPath || songsHint || "未选择"}
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
        </>
      )}
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
      {mode === "lazer" && restoreOpen && (lastLazerDir || lastSongs) ? (
        <div className="score-modal-backdrop" onClick={() => setRestoreOpen(false)}>
          <div className="score-modal restore-modal" onClick={(e) => e.stopPropagation()}>
            <div className="score-modal-head">
              <h2>恢复上次位置</h2>
            </div>
            <div className="restore-paths">
              {lastLazerDir ? (
                <div className="restore-path-row">
                  <span className="restore-label">lazer 数据目录</span>
                  <span className="path" title={lastLazerDir}>{lastLazerDir}</span>
                </div>
              ) : null}
              {lastSongs ? (
                <div className="restore-path-row">
                  <span className="restore-label">Songs</span>
                  <span className="path" title={lastSongs}>{lastSongs}</span>
                </div>
              ) : null}
            </div>
            {lazerError ? <div className="error-banner">{lazerError}</div> : null}
            <div className="score-modal-foot">
              <button className="btn" onClick={() => setRestoreOpen(false)}>取消</button>
              <button
                className="btn primary"
                disabled={lazerBusy !== null}
                onClick={() => void restoreLastLazer()}
              >
                {lazerBusy !== null ? "绑定中…" : "确认绑定"}
              </button>
            </div>
          </div>
        </div>
      ) : null}
      {mode === "stable" && restoreOpen && lastPaths ? (
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
