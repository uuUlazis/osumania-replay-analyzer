// Timeline: left press/drag = live scrub; right press/drag = selection;
// right click (degenerate selection) = jump.
import { useEffect, useRef } from "react";
import { useSyncExternalStore } from "react";
import { sessionStore } from "../app/sessionStore";
import { playerStore } from "../app/playerStore";
import {
  JUDGEMENT_BY_HIT,
  JUDGEMENT_COLORS,
  JUDGEMENT_TICK_HEIGHT,
} from "../models/types";

const MOVE_THRESHOLD_PX = 3;

export default function Timeline() {
  const snap = useSyncExternalStore(sessionStore.subscribe, sessionStore.get);
  const player = useSyncExternalStore(playerStore.subscribe, playerStore.get);
  const canvasRef = useRef<HTMLCanvasElement | null>(null);

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const dpr = window.devicePixelRatio || 1;
    const rect = canvas.getBoundingClientRect();
    canvas.width = Math.max(1, rect.width * dpr);
    canvas.height = Math.max(1, rect.height * dpr);
    const ctx = canvas.getContext("2d");
    if (!ctx) return;
    ctx.scale(dpr, dpr);
    const w = rect.width, h = rect.height;
    const duration = Math.max(1, player.durationMs);
    ctx.clearRect(0, 0, w, h);
    ctx.fillStyle = "#12131a";
    ctx.fillRect(0, 0, w, h);

    // seconds grid (单 path 批量 stroke, 长曲数百条线不再逐条 stroke)
    ctx.strokeStyle = "#22242f";
    const secW = (w / duration) * 1000;
    if (secW > 8) {
      ctx.beginPath();
      for (let s = 0; s * 1000 < duration; ++s) {
        const x = ((s * 1000) / duration) * w;
        ctx.moveTo(x, 0); ctx.lineTo(x, h);
      }
      ctx.stroke();
    }

    const xOf = (t: number) => (t / duration) * w;
    // object ticks: color + height by replay judgement (300g lowest, MISS tallest)
    // 每帧开销优化: judgeLabel 按事件数组引用缓存, 物件窗口用二分定位
    const judgeLabel = judgeLabelOf(snap.events);
    {
      const tLo = -2 * (duration / w);
      const tHi = (w + 2) * (duration / w);
      let lo = 0, hi = snap.objects.length;
      while (lo < hi) {
        const mid = (lo + hi) >> 1;
        if (snap.objects[mid].startTimeMs < tLo) lo = mid + 1; else hi = mid;
      }
    // 判定刻度按颜色分批 (每色一条 path), LN 横线按同一透明色分批
    const noModPath = new Path2D();
    const lnPath = new Path2D();
    const byColor = new Map<string, Path2D>();
    for (let i = lo; i < snap.objects.length; ++i) {
      const o = snap.objects[i];
      if (o.startTimeMs > tHi) break;
      const x = xOf(o.startTimeMs);
      const label = judgeLabel.get(o.index);
      const bottom = label ? (JUDGEMENT_TICK_HEIGHT[label] ?? 0.7) : 0.7;
      if (label) {
        const color = JUDGEMENT_COLORS[label];
        let path = byColor.get(color);
        if (!path) { path = new Path2D(); byColor.set(color, path); }
        path.moveTo(x, h * 0.25);
        path.lineTo(x, h * bottom);
      } else {
        noModPath.moveTo(x, h * 0.25);
        noModPath.lineTo(x, h * bottom);
      }
      if (o.endTimeMs !== undefined) {
        const xe = xOf(o.endTimeMs);
        lnPath.moveTo(x, h * 0.55);
        lnPath.lineTo(xe, h * 0.55);
      }
    }
    ctx.lineWidth = 1;
    ctx.strokeStyle = "rgba(160,168,190,0.35)";
    ctx.stroke(lnPath);
    ctx.strokeStyle = "#6a7080";
    ctx.stroke(noModPath);
    for (const [color, path] of byColor) {
      ctx.strokeStyle = color;
      ctx.stroke(path);
    }
    ctx.lineWidth = 1;
    }
    // selection overlay (normalize so reverse drags draw correctly)
    if (player.selection) {
      const x1 = xOf(Math.min(player.selection.startMs, player.selection.endMs));
      const x2 = xOf(Math.max(player.selection.startMs, player.selection.endMs));
      ctx.fillStyle = "rgba(87, 200, 255, 0.18)";
      ctx.fillRect(x1, 0, x2 - x1, h);
      ctx.strokeStyle = "#57c8ff";
      ctx.strokeRect(x1, 0, x2 - x1, h);
    }
    // playhead
    const px = xOf(player.previewTimeMs);
    ctx.strokeStyle = "#e8eaf2";
    ctx.lineWidth = 2;
    ctx.beginPath(); ctx.moveTo(px, 0); ctx.lineTo(px, h); ctx.stroke();
    ctx.lineWidth = 1;
  }, [snap.objects, snap.events, player.previewTimeMs, player.durationMs, player.selection]);

// Timeline 判定标签缓存 (数组引用键)
const timelineLabelCache = new WeakMap<object, Map<number, string>>();
function judgeLabelOf(events: object): Map<number, string> {
  const cached = timelineLabelCache.get(events);
  if (cached) return cached;
  const map = new Map<number, string>();
  const list = events as unknown as {
    phase: string;
    objectIndex: number;
    judgement: string;
  }[];
  for (const e of list) {
    if (e.phase === "hold_tail") continue; // start tick shows the head judgement
    map.set(e.objectIndex, JUDGEMENT_BY_HIT[e.judgement] ?? "MISS");
  }
  timelineLabelCache.set(events, map);
  return map;
}

  const dragRef = useRef<{
    pointerId: number;
    startX: number;
    startTime: number;
    mode: "scrub" | "select";
    moved: boolean;
  } | null>(null);

  const timeAt = (clientX: number) => {
    const canvas = canvasRef.current;
    if (!canvas) return 0;
    const rect = canvas.getBoundingClientRect();
    const ratio = Math.min(1, Math.max(0, (clientX - rect.left) / rect.width));
    return ratio * Math.max(1, player.durationMs);
  };

  const onPointerDown = (e: React.PointerEvent<HTMLCanvasElement>) => {
    if (e.button !== 0 && e.button !== 2) return;
    e.currentTarget.setPointerCapture(e.pointerId);
    const t = timeAt(e.clientX);
    const mode = e.button === 0 ? "scrub" : "select";
    dragRef.current = {
      pointerId: e.pointerId,
      startX: e.clientX,
      startTime: t,
      mode,
      moved: false,
    };
    if (mode === "scrub") {
      playerStore.setScrubbing(true);
      playerStore.seek(t);
    } else {
      playerStore.setSelection({ startMs: t, endMs: t });
    }
  };

  const onPointerMove = (e: React.PointerEvent<HTMLCanvasElement>) => {
    const d = dragRef.current;
    if (!d || e.pointerId !== d.pointerId) return;
    if (Math.abs(e.clientX - d.startX) > MOVE_THRESHOLD_PX) d.moved = true;
    const t = timeAt(e.clientX);
    if (d.mode === "scrub") {
      playerStore.seek(t);
    } else {
      playerStore.setSelection({ startMs: d.startTime, endMs: t });
    }
  };

  const onPointerUp = (e: React.PointerEvent<HTMLCanvasElement>) => {
    const d = dragRef.current;
    dragRef.current = null;
    if (!d) return;
    const endT = timeAt(e.clientX);
    if (d.mode === "scrub") {
      playerStore.setScrubbing(false);
      if (!d.moved) {
        // single click: jump & clear selection (a drag keeps the selection)
        playerStore.setSelection(null);
      }
      return;
    }
    // selection finish: normalize; degenerate (right click) -> treat as jump
    const a = Math.min(d.startTime, endT);
    const b = Math.max(d.startTime, endT);
    playerStore.setSelection(b - a < 5 ? null : { startMs: a, endMs: b });
    playerStore.seek(endT);
  };

  const onPointerCancel = () => {
    const d = dragRef.current;
    dragRef.current = null;
    if (d && d.mode === "scrub") playerStore.setScrubbing(false);
  };

  return (
    <div className="timeline-wrap">
      <canvas
        ref={canvasRef}
        className="timeline-canvas"
        onPointerDown={onPointerDown}
        onPointerMove={onPointerMove}
        onPointerUp={onPointerUp}
        onPointerCancel={onPointerCancel}
        onContextMenu={(e) => e.preventDefault()}
      />
    </div>
  );
}
