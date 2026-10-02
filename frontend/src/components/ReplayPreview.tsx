// Replay preview: downscroll playfield rendered from the same matcher
// data, with skin layout/images from the loaded skin folder.
import { useEffect, useMemo, useRef, useState, useSyncExternalStore } from "react";
import { sessionStore } from "../app/sessionStore";
import { playerStore } from "../app/playerStore";
import { drawPlayfield } from "../render/maniaRenderer";
import { loadManiaSkin, visibleTimeMs, type ManiaSkin } from "../render/maniaSkin";

export default function ReplayPreview() {
  const snap = useSyncExternalStore(sessionStore.subscribe, sessionStore.get);
  const player = useSyncExternalStore(playerStore.subscribe, playerStore.get);
  const canvasRef = useRef<HTMLCanvasElement | null>(null);
  const [skin, setSkin] = useState<ManiaSkin | null>(null);
  const keyCount = snap.beatmap?.keyCount ?? 4;

  // visible time window from scroll speed (notes spawn above the field)
  const windowMs = useMemo(
    () => visibleTimeMs(player.scrollSpeed) + 300,
    [player.scrollSpeed],
  );

  const sid = snap.sessionId;

  // skin loads once per session (ini + per-column images)
  useEffect(() => {
    let cancelled = false;
    setSkin(null);
    if (sid) {
      loadManiaSkin(sid, keyCount).then((loaded) => {
        if (!cancelled) setSkin(loaded);
      });
    }
    return () => { cancelled = true; };
  }, [sid, keyCount]);

  // visible objects window: binary search on sorted objects
  const visible = useMemo(() => {
    const t = player.previewTimeMs;
    const objs = snap.objects;
    let lo = 0, hi = objs.length;
    while (lo < hi) {
      const mid = (lo + hi) >> 1;
      const endT = objs[mid].endTimeMs ?? objs[mid].startTimeMs;
      if (endT < t - 800) lo = mid + 1; else hi = mid;
    }
    const out = [];
    for (let i = lo; i < objs.length && objs[i].startTimeMs - t <= windowMs; ++i)
      out.push(objs[i]);
    return out;
  }, [snap.objects, player.previewTimeMs, windowMs]);

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const ctx = canvas.getContext("2d");
    if (!ctx) return;
    const dpr = window.devicePixelRatio || 1;
    const rect = canvas.getBoundingClientRect();
    if (rect.width === 0 || rect.height === 0) return;
    const needW = Math.round(rect.width * dpr);
    const needH = Math.round(rect.height * dpr);
    if (canvas.width !== needW || canvas.height !== needH) {
      canvas.width = needW;
      canvas.height = needH;
    }
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);

    // key state at playhead: last frame with timeMs <= previewTimeMs
    const frames = snap.frames;
    let keys = 0;
    if (frames.length && player.previewTimeMs >= frames[0].timeMs) {
      let lo2 = 0, hi2 = frames.length - 1;
      const t = player.previewTimeMs;
      while (lo2 < hi2) {
        const mid = (lo2 + hi2 + 1) >> 1;
        if (frames[mid].timeMs <= t) lo2 = mid; else hi2 = mid - 1;
      }
      keys = frames[lo2].keys;
    }
    const ks: boolean[] = [];
    for (let c = 0; c < keyCount; ++c) ks.push((keys & (1 << c)) !== 0);

    // 判定规则 V2 draws the ScoreV2 match (falls back to V1 events when the
    // session predates the second pass).
    const events =
      player.judgeVersion === "v2" && snap.eventsV2.length
        ? snap.eventsV2
        : snap.events;
    drawPlayfield(ctx, rect.width, rect.height, {
      previewTimeMs: player.previewTimeMs,
      objects: visible,
      events,
      keyState: ks,
      skin,
      scrollSpeed: player.scrollSpeed,
      windowMs,
      showInputJudges: player.showInputJudges,
      judgeVersion: player.judgeVersion,
    });
  }, [visible, snap.events, snap.eventsV2, snap.frames, player.previewTimeMs, keyCount, player.scrollSpeed, windowMs, snap.sessionId, skin, player.showInputJudges, player.judgeVersion]);

  return (
    <canvas ref={canvasRef} className="preview-canvas" />
  );
}
