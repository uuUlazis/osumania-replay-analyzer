// Replay preview: downscroll playfield rendered from the same matcher
// data, with skin layout/images from the loaded skin folder.
//
// 渲染架构 (2026-10-26 性能定案): 播放时 AudioSync 的 rAF 跟随环每帧更新
// playerStore; 若经 useSyncExternalStore 驱动 React 提交, 高刷屏上每秒
// 数百次 React 提交会造成周期性 GC/提交停顿 (实测 25-50ms 长帧, 1 次/秒)。
// 因此本组件不订阅 playerStore: 自己跑 rAF 循环直读 playerStore.get()/
// sessionStore.get() 绘制 (无 React 提交, 跟随显示器刷新率)。
// 帧数限制已证伪 (2026-10-27 用户实测: rAF 跳拍与定时器定频两种方案卡顿
// 均回归) — 不做任何限频, 回退为限频前 (11o-c 验收版) 的自由 rAF。
// playerStore 的 emit 降频 (~30Hz) 供 Transport/时间轴等文本 UI 使用。
import { useEffect, useRef, useState, useSyncExternalStore } from "react";
import { sessionStore } from "../app/sessionStore";
import { playerStore } from "../app/playerStore";
import { drawPlayfield } from "../render/maniaRenderer";
import { loadManiaSkin, visibleTimeMs, type ManiaSkin } from "../render/maniaSkin";
import type { ManiaObject } from "../models/types";

export default function ReplayPreview() {
  const snap = useSyncExternalStore(sessionStore.subscribe, sessionStore.get);
  // 皮肤经 ref 桥接给 rAF 循环 (循环仅会话级重建)
  const skinRef = useRef<ManiaSkin | null>(null);
  // 只订阅 skinRev (数值快照): 皮肤热重载时重建渲染循环, 其余 player 更新
  // 由 rAF 循环直读 store, 不触发 React 提交
  const skinRev = useSyncExternalStore(
    playerStore.subscribe,
    () => playerStore.get().skinRev,
  );
  const canvasRef = useRef<HTMLCanvasElement | null>(null);
  const [skin, setSkin] = useState<ManiaSkin | null>(null);
  const keyCount = snap.beatmap?.keyCount ?? 4;
  const judgeVersion = snap.analysis?.rulesProfile === "v2" ? "v2" : "v1";
  const isLazer = snap.analysis?.scoreSource === "lazer";

  // skin loads once per session (ini + per-column images)
  useEffect(() => {
    let cancelled = false;
    setSkin(null);
    skinRef.current = null;
    if (snap.sessionId) {
      loadManiaSkin(snap.sessionId, keyCount).then((loaded) => {
        if (!cancelled) {
          skinRef.current = loaded;
          setSkin(loaded);
        }
      });
    }
    return () => { cancelled = true; };
  }, [snap.sessionId, keyCount, skinRev]);

  // ---- 独立 rAF 渲染循环: 不经 React, 每帧直读 store ----
  useEffect(() => {
    let raf = 0;
    let alive = true;
    const loop = () => {
      if (!alive) return;
      raf = requestAnimationFrame(loop);
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

      const st = playerStore.get();
      const s2 = sessionStore.get();

      // key state at playhead: last frame with timeMs <= previewTimeMs
      const frames = s2.frames;
      let keys = 0;
      const t = st.previewTimeMs;
      if (frames.length && t >= frames[0].timeMs) {
        let lo2 = 0, hi2 = frames.length - 1;
        while (lo2 < hi2) {
          const mid = (lo2 + hi2 + 1) >> 1;
          if (frames[mid].timeMs <= t) lo2 = mid; else hi2 = mid - 1;
        }
        keys = frames[lo2].keys;
      }
      const ks: boolean[] = [];
      const keyCount2 = s2.beatmap?.keyCount ?? keyCount;
      for (let c = 0; c < keyCount2; ++c) ks.push((keys & (1 << c)) !== 0);

      const judgeVersion2 =
        s2.analysis?.rulesProfile === "v2" ? "v2" : "v1";
      const isLazer2 = s2.analysis?.scoreSource === "lazer";
      const events =
        judgeVersion2 === "v2" && s2.eventsV2.length ? s2.eventsV2 : s2.events;

      // 皮肤未就绪 (会话已载入): 不画, 由灰屏遮罩接管
      if (s2.sessionId && !skinRef.current) return;

      drawPlayfield(ctx, rect.width, rect.height, {
        previewTimeMs: t,
        objects: objsWindow(s2, t, windowMsOf(st)),
        events,
        keyState: ks,
        skin: skinRef.current,
        scrollSpeed: st.scrollSpeed,
        windowMs: windowMsOf(st),
        rate: st.rate || 1,
        showInputJudges: st.showInputJudges,
        judgeVersion: judgeVersion2,
        lazerMode: isLazer2,
      });
    };
    raf = requestAnimationFrame(loop);
    return () => {
      alive = false;
      cancelAnimationFrame(raf);
    };
  }, [snap.sessionId, keyCount, skinRev, isLazer, judgeVersion, skin]);

  return (
    <div className="preview-wrap">
      <canvas ref={canvasRef} className="preview-canvas" />
      {snap.sessionId && !skin && (
        <div className="preview-loading">
          <div className="preview-spinner" />
          正在载入皮肤…
        </div>
      )}
    </div>
  );
}
function objsWindow(
  s2: { objects: ManiaObject[] },
  t: number,
  windowMs: number,
): ManiaObject[] {
  const objs = s2.objects;
  if (!objs.length) return [];
  let maxHold = 0;
  for (const o of objs) {
    if (o.endTimeMs) {
      const d = o.endTimeMs - o.startTimeMs;
      if (d > maxHold) maxHold = d;
    }
  }
  const lead = 800 + maxHold;
  let lo = 0, hi = objs.length;
  while (lo < hi) {
    const mid = (lo + hi) >> 1;
    if (objs[mid].startTimeMs < t - lead) lo = mid + 1; else hi = mid;
  }
  const out: ManiaObject[] = [];
  for (let i = lo; i < objs.length && objs[i].startTimeMs - t <= windowMs; ++i) {
    const o = objs[i];
    const endT = o.endTimeMs ?? o.startTimeMs;
    if (endT - t >= -800) out.push(o);
  }
  return out;
}


function windowMsOf(st: { scrollSpeed: number; rate: number }): number {
  return (visibleTimeMs(st.scrollSpeed) + 300) * (st.rate || 1);
}
