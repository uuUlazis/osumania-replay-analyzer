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
  // 判定数据源跟随本次分析使用的规则档 (snap.analysis 可为 null → 回退 V1)
  const judgeVersion = snap.analysis?.rulesProfile === "v2" ? "v2" : "v1";

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
  }, [sid, keyCount, player.skinRev]);

  // Longest hold span in the map: LNs starting this long before the preview
  // time can still be on screen (their tail has not passed the line yet).
  const maxHoldMs = useMemo(() => {
    let m = 0;
    for (const o of snap.objects) {
      if (o.endTimeMs) {
        const d = o.endTimeMs - o.startTimeMs;
        if (d > m) m = d;
      }
    }
    return m;
  }, [snap.objects]);

  // visible objects window: binary search on the START-time-sorted objects.
  // The previous version binary-searched endTimeMs, but the array is sorted
  // by startTimeMs — with long holds the end times are not monotonic, so the
  // search could jump past a hold that was still visible and make whole LNs
  // vanish frame-to-frame while they were still mid-screen.
  const visible = useMemo(() => {
    const t = player.previewTimeMs;
    const objs = snap.objects;
    if (!objs.length) return [];
    const lead = 800 + maxHoldMs;
    let lo = 0, hi = objs.length;
    while (lo < hi) {
      const mid = (lo + hi) >> 1;
      if (objs[mid].startTimeMs < t - lead) lo = mid + 1; else hi = mid;
    }
    const out = [];
    for (let i = lo; i < objs.length && objs[i].startTimeMs - t <= windowMs; ++i) {
      const o = objs[i];
      const endT = o.endTimeMs ?? o.startTimeMs;
      if (endT - t >= -800) out.push(o);
    }
    return out;
  }, [snap.objects, player.previewTimeMs, windowMs, maxHoldMs]);

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

    // 皮肤未就绪 (会话已载入): 渲染完全静止 — 不画默认配色/背景/判定线,
    // 由灰屏遮罩接管; 皮肤到位后本 effect 因依赖变化重跑, 直接以皮肤绘制。
    if (sid && !skin) return;

    // 判定规则 V2 draws the ScoreV2 match (falls back to V1 events when the
    // session predates the second pass).
    const events =
      judgeVersion === "v2" && snap.eventsV2.length
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
      judgeVersion,
    });
  }, [visible, snap.events, snap.eventsV2, snap.frames, player.previewTimeMs, keyCount, player.scrollSpeed, windowMs, snap.sessionId, skin, player.showInputJudges, judgeVersion]);

  return (
    <div className="preview-wrap">
      <canvas ref={canvasRef} className="preview-canvas" />
      {sid && !skin && (
        <div className="preview-loading">
          <div className="preview-spinner" />
          正在载入皮肤…
        </div>
      )}
    </div>
  );
}
