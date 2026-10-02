// Hidden <audio> element kept in sync with the preview clock so the loaded
// music plays exactly with the replay timeline.
import { useEffect, useRef, useSyncExternalStore } from "react";
import { playerStore } from "../app/playerStore";

const DRIFT_LIMIT_S = 0.15;

export default function AudioSync() {
  const player = useSyncExternalStore(playerStore.subscribe, playerStore.get);
  const ref = useRef<HTMLAudioElement | null>(null);

  useEffect(() => {
    const el = ref.current;
    if (!el) return;
    const target = Math.max(0, player.previewTimeMs) / 1000;
    // re-seek on large drift (seek bar jumps, tab throttling, loop-around)
    if (Number.isFinite(target) && Math.abs(el.currentTime - target) > DRIFT_LIMIT_S) {
      try {
        el.currentTime = target;
      } catch {
        // seeking before metadata is loaded — the next tick retries
      }
    }
    if (player.playing) {
      if (el.paused) el.play().catch(() => {}); // autoplay guard: user gesture exists (播放 button)
    } else if (!el.paused) {
      el.pause();
    }
    el.volume = player.volume;
  }, [player.previewTimeMs, player.playing, player.musicUrl, player.volume]);

  if (!player.musicUrl) return null;
  return <audio ref={ref} src={player.musicUrl} preload="auto" style={{ display: "none" }} />;
}