import { useSyncExternalStore } from "react";
import { playerStore } from "../app/playerStore";
import { api } from "../api/client";
import { fmtTime } from "./AnalysisSummary";
import { visibleTimeMs } from "../render/maniaSkin";

export default function Transport() {
  const player = useSyncExternalStore(playerStore.subscribe, playerStore.get);
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
          checked={player.showInputJudges}
          onChange={(e) => {
            const v = e.target.checked;
            playerStore.setShowInputJudges(v);
            api.saveLastState({ showInputJudges: v }).catch(() => {});
          }}
        />
        <label htmlFor="judge-toggle">判定显示</label>
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
          onChange={(e) => playerStore.setScrollSpeed(Number(e.target.value))}
        />
        <span className="speed-value">
          {player.scrollSpeed} ({Math.round(visibleTimeMs(player.scrollSpeed))}ms)
        </span>
      </span>
    </div>
  );
}
