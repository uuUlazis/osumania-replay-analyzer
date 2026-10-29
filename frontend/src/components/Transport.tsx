import { useSyncExternalStore } from "react";
import { playerStore } from "../app/playerStore";
import { api } from "../api/client";
import { fmtTime } from "./AnalysisSummary";
import { visibleTimeMs } from "../render/maniaSkin";

export default function Transport() {
  const player = useSyncExternalStore(playerStore.subscribe, playerStore.get);
  // 落速显示 = UI 滑条值 (会话载入时已用回放帧 y 初始化 — 滑条为唯一速度源);
  // visibleTimeMs 为按主导 BL=600 的近似显示。
  const eff = Math.max(1, Math.min(40, player.scrollSpeed || 12));
  // lazer 局判定显示 = lazer 判定 profile 重建 (与 stable 同一叠加层)
  const dur = Math.max(1, player.durationMs);
  return (
    <div className="transport">
      <button className="btn" onClick={() => playerStore.toggle()}>
        {player.playing ? "暂停" : "播放"}
      </button>
      <input
        type="range"
        min={0}
        max={dur}
        step={10}
        value={Math.min(player.previewTimeMs, dur)}
        onChange={(e) => playerStore.seek(Number(e.target.value))}
        onPointerDown={() => playerStore.setScrubbing(true)}
        onPointerUp={() => playerStore.setScrubbing(false)}
        onPointerCancel={() => playerStore.setScrubbing(false)}
        style={{ flex: 1 }}
      />
      <span className="time">
        {fmtTime(player.previewTimeMs)} / {fmtTime(dur)}
      </span>
      <span className="judge-ctl">
        <input
          id="judge-toggle"
          type="checkbox"
          checked={player.judgementOnly ? true : player.showInputJudges}
          disabled={player.judgementOnly}
          style={player.judgementOnly ? { opacity: 0.45 } : undefined}
          onChange={(e) => {
            if (player.judgementOnly) return;
            const v = e.target.checked;
            playerStore.setShowInputJudges(v);
            api.saveLastState({ showInputJudges: v }).catch(() => {});
          }}
        />
        <label
          htmlFor="judge-toggle"
          style={player.judgementOnly ? { opacity: 0.45, cursor: "default" } : undefined}
        >
          判定显示
        </label>
        <input
          id="judge-only-toggle"
          type="checkbox"
          checked={player.judgementOnly}
          onChange={(e) => {
            const v = e.target.checked;
            playerStore.setJudgementOnly(v);
            api.saveLastState({ judgementOnly: v }).catch(() => {});
          }}
        />
        <label htmlFor="judge-only-toggle">仅显示判定</label>
      </span>
      <span className="speed-ctl">
        <label htmlFor="music-volume">音量</label>
        <input
          id="music-volume"
          type="range"
          min={0}
          max={100}
          step={1}
          value={Math.round(player.volume * 100)}
          onChange={(e) => {
            const vol = Number(e.target.value) / 100;
            playerStore.setVolume(vol);
            api.saveLastState({ volume: vol }).catch(() => {});
          }}
        />
        <span className="speed-value">
          {Math.round(player.volume * 100)}%
        </span>
      </span>
      <span className="speed-ctl">
        <label htmlFor="scroll-speed">落速</label>
        <input
          id="scroll-speed"
          type="range"
          min={1}
          max={40}
          step={1}
          value={player.scrollSpeed}
          onChange={(e) => {
            const v = Number(e.target.value);
            playerStore.setScrollSpeed(v);
            api.saveLastState({ scrollSpeed: v }).catch(() => {});
          }}
        />
        <span className="speed-value">
          {Math.round(eff * 10) / 10} ({Math.round(visibleTimeMs(eff))}ms)
        </span>
      </span>
    </div>
  );
}
