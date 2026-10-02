import { useState } from "react";
import { api } from "../api/client";

interface Props {
  onLoad: (
    replayPath: string,
    beatmapPath: string,
    skinFolder?: string,
    audioPath?: string,
  ) => void;
  loading: boolean;
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

export default function FileLoader({ onLoad, loading }: Props) {
  const [replayPath, setReplayPath] = useState("");
  const [beatmapPath, setBeatmapPath] = useState("");
  const [skinFolder, setSkinFolder] = useState<string | undefined>();
  const [audioPath, setAudioPath] = useState<string | undefined>();
  const [dialogError, setDialogError] = useState("");

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
      (p) => setSkinFolder(p),
    );
  const pickAudio = () =>
    pick(
      () => api.dialogOpenFile("选择音乐文件 (可选)", AUDIO_FILTERS),
      (p) => setAudioPath(p),
    );

  const canLoad = replayPath !== "" && beatmapPath !== "" && !loading;

  return (
    <div className="panel loader-panel">
      <h2>文件</h2>
      <div className="pick-row">
        <button className="btn" onClick={pickReplay}>回放 .osr</button>
        <span className="path" title={replayPath}>{replayPath || "未选择"}</span>
      </div>
      <div className="pick-row">
        <button className="btn" onClick={pickBeatmap}>谱面 .osu</button>
        <span className="path" title={beatmapPath}>{beatmapPath || "未选择"}</span>
      </div>
      <div className="pick-row">
        <button className="btn" onClick={pickAudio} title={audioPath}>
          {audioPath ? "音乐已选" : "音乐(可选)"}
        </button>
        <span className="path" title={audioPath}>
          {audioPath || "未选择"}
        </span>
      </div>
      <div className="pick-row">
        <button className="btn" onClick={pickSkin} title={skinFolder}>
          {skinFolder ? "皮肤已选" : "皮肤(可选)"}
        </button>
        <span className="path" title={skinFolder}>
          {skinFolder || "未选择"}
        </span>
      </div>
      <div className="pick-row">
        <button
          className="btn primary"
          disabled={!canLoad}
          onClick={() =>
            onLoad(replayPath, beatmapPath, skinFolder, audioPath)
          }
        >
          {loading ? "分析中…" : "加载分析"}
        </button>
      </div>
      {dialogError ? <div className="error-banner">{dialogError}</div> : null}
    </div>
  );
}
