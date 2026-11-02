// Timeline: left press/drag = live scrub; right press/drag = selection;
// right click (degenerate selection) = jump.
// 0.1.4: 判定刻度上下翻转 (基线=画布底缘, 向上伸, MISS 伸到刻度区顶) + 同画布
// 分区叠加累计 acc 走势曲线 (不交叉: 曲线占刻度区上方区域, 与刻度区间隔
// CURVE_GAP_PX; y 轴按曲线实际范围自适应收窄) + hover 联动: 全高亮竖线 + timing
// 标注 + 曲线交点 acc 小框, 指针落在曲线上 (±CURVE_HOVER_PX) 时曲线加亮加粗。
// acc 口径 = 显示口径 (utils/accCurve.ts)。
import { useEffect, useMemo, useRef, useState } from "react";
import { useSyncExternalStore } from "react";
import { sessionStore } from "../app/sessionStore";
import { playerStore } from "../app/playerStore";
import {
  JUDGEMENT_BY_HIT,
  JUDGEMENT_COLORS,
  JUDGEMENT_TICK_HEIGHT,
} from "../models/types";
import { computeAccCurve, type AccCurve } from "../utils/accCurve";
import { fmtTime } from "./AnalysisSummary";

const MOVE_THRESHOLD_PX = 3;
// 翻转刻度区占画布高度比例 (MISS=1.0 恰好伸到刻度区顶)
const TICK_ZONE = 0.3;
// 旧渲染基线 (h*0.25) — JUDGEMENT_TICK_HEIGHT 的底端位置换算相对长度用
const TICK_OLD_BASE = 0.25;
// 曲线区纵向边界: 上缘安全边距 + 与刻度区的间隙 (px) — 分区渲染不交叉
const CURVE_MARGIN_PX = 3;
const CURVE_GAP_PX = 4;
// 指针到曲线的吸附距离 (px): 判定「鼠标在曲线上」
const CURVE_HOVER_PX = 8;
const CURVE_COLOR = "#57c8ff";

// y 轴自适应范围: 曲线实际 [min,max] 加 pad (max(0.2pp, 范围×5%)); 平线时撑开 ±0.2pp
function curveRange(c: AccCurve): { lo: number; hi: number } {
  const span = c.max - c.min;
  const pad = Math.max(0.002, span * 0.05);
  return { lo: c.min - pad, hi: c.max + pad };
}

function accYOf(acc: number, lo: number, hi: number, top: number, bottom: number): number {
  return bottom - ((acc - lo) / (hi - lo)) * (bottom - top);
}

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

export default function Timeline() {
  const snap = useSyncExternalStore(sessionStore.subscribe, sessionStore.get);
  const player = useSyncExternalStore(playerStore.subscribe, playerStore.get);
  const canvasRef = useRef<HTMLCanvasElement | null>(null);
  // hover 状态: 画布内指针位置 (CSS px); null = 指针不在画布上
  const [hover, setHover] = useState<{ x: number; y: number } | null>(null);

  // acc 口径与显示一致 (SegmentPanel/AnalysisSummary 同款判式):
  // lazer 或 rulesProfile=v2 → accuracyV2 (eventsV2), 否则 V1 counts 公式 (events)
  const profileV2 =
    snap.analysis?.scoreSource === "lazer" || snap.analysis?.rulesProfile === "v2";
  const accEvents =
    profileV2 && snap.eventsV2.length ? snap.eventsV2 : snap.events;
  const curve = useMemo(
    () =>
      accEvents.length && player.durationMs > 0
        ? computeAccCurve(accEvents, profileV2, Math.max(1, player.durationMs))
        : null,
    [accEvents, profileV2, player.durationMs],
  );

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
    // 曲线区纵向边界: 不与刻度区交叉 — 底缘 = 刻度区顶 (h*(1-TICK_ZONE)) 上方留间隙
    const curveTop = CURVE_MARGIN_PX;
    const curveBottom = h * (1 - TICK_ZONE) - CURVE_GAP_PX;
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
    // object ticks: color + length by replay judgement (300g shortest, MISS tallest)
    // 翻转渲染: 基线 = 画布底缘, 刻度向上伸; 旧底端位置 (0.25..1.0) 换算为刻度区
    // 内相对长度, MISS 恰好伸到刻度区顶。LN 横线同步翻入刻度区 (旧 0.55h 位置)。
    // 每帧开销优化: judgeLabel 按事件数组引用缓存, 物件窗口用二分定位
    const judgeLabel = judgeLabelOf(snap.events);
    const tickTipY = (bottom: number) =>
      h - ((bottom - TICK_OLD_BASE) / (1 - TICK_OLD_BASE)) * TICK_ZONE * h;
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
        path.moveTo(x, h);
        path.lineTo(x, tickTipY(bottom));
      } else {
        noModPath.moveTo(x, h);
        noModPath.lineTo(x, tickTipY(bottom));
      }
      if (o.endTimeMs !== undefined) {
        const xe = xOf(o.endTimeMs);
        // LN 横线: 旧 0.55h 在挂载区 (0.25..1) 的相对位置 0.4, 翻转后同比例入刻度区
        lnPath.moveTo(x, h - ((0.55 - TICK_OLD_BASE) / (1 - TICK_OLD_BASE)) * TICK_ZONE * h);
        lnPath.lineTo(xe, h - ((0.55 - TICK_OLD_BASE) / (1 - TICK_OLD_BASE)) * TICK_ZONE * h);
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

    // ---- 累计 acc 走势曲线 (绘于刻度上层, 允许交叉) ----
    const hoverOnCurve =
      hover != null && curve != null && curve.count > 0 && (() => {
        const idx = Math.min(
          curve.values.length - 1,
          Math.max(0, Math.floor((hover.x / w) * curve.values.length)),
        );
        const v = curve.values[idx];
        if (!Number.isFinite(v)) return false;
        const { lo, hi } = curveRange(curve);
        return Math.abs(hover.y - accYOf(v, lo, hi, curveTop, curveBottom)) <= CURVE_HOVER_PX;
      })();
    if (curve && curve.count > 0) {
      const { lo, hi } = curveRange(curve);
      ctx.beginPath();
      let started = false;
      const n = curve.values.length;
      for (let b = 0; b < n; ++b) {
        const v = curve.values[b];
        if (!Number.isFinite(v)) { started = false; continue; }
        const px = ((b + 1) / n) * w;
        const py = accYOf(v, lo, hi, curveTop, curveBottom);
        if (!started) { ctx.moveTo(px, py); started = true; }
        else ctx.lineTo(px, py);
      }
      ctx.strokeStyle = CURVE_COLOR;
      ctx.globalAlpha = hoverOnCurve ? 1 : 0.85;
      ctx.lineWidth = hoverOnCurve ? 2.4 : 1.4;
      if (hoverOnCurve) { ctx.shadowColor = CURVE_COLOR; ctx.shadowBlur = 6; }
      ctx.stroke();
      ctx.shadowBlur = 0;
      ctx.globalAlpha = 1;
      ctx.lineWidth = 1;
      // 当前 y 轴范围标注 (右上 = max, 右下 = min)
      ctx.font = "10px system-ui, sans-serif";
      ctx.fillStyle = "rgba(160,168,190,0.9)";
      ctx.textAlign = "right";
      ctx.fillText((curve.max * 100).toFixed(2) + "%", w - 4, CURVE_MARGIN_PX + 9);
      ctx.fillText((curve.min * 100).toFixed(2) + "%", w - 4, curveBottom - 3);
      ctx.textAlign = "left";
    }

    // playhead
    const px = xOf(player.previewTimeMs);
    ctx.strokeStyle = "#e8eaf2";
    ctx.lineWidth = 2;
    ctx.beginPath(); ctx.moveTo(px, 0); ctx.lineTo(px, h); ctx.stroke();
    ctx.lineWidth = 1;

    // ---- hover 联动: 全高亮竖线 + timing 标注 + 曲线交点 acc 小框 ----
    if (hover) {
      ctx.strokeStyle = "rgba(87,200,255,0.7)";
      ctx.lineWidth = 1;
      ctx.beginPath(); ctx.moveTo(hover.x, 0); ctx.lineTo(hover.x, h); ctx.stroke();
      ctx.font = "10px system-ui, sans-serif";
      ctx.fillStyle = "#e8eaf2";
      const tLabel = fmtTime((hover.x / w) * duration);
      const tw = ctx.measureText(tLabel).width;
      const tx = Math.min(Math.max(2, hover.x + 4), w - tw - 2);
      ctx.fillText(tLabel, tx, 11);
      if (curve && curve.count > 0) {
        const n = curve.values.length;
        const idx = Math.min(n - 1, Math.max(0, Math.floor((hover.x / w) * n)));
        const v = curve.values[idx];
        if (Number.isFinite(v)) {
          const { lo, hi } = curveRange(curve);
          const py = accYOf(v, lo, hi, curveTop, curveBottom);
          // 曲线交点小圆点
          ctx.fillStyle = CURVE_COLOR;
          ctx.beginPath(); ctx.arc(hover.x, py, 2.5, 0, Math.PI * 2); ctx.fill();
          // acc 小框: 曲线点上方, 越顶翻到下方; x 贴右缘翻转
          const label = (v * 100).toFixed(2) + "%";
          ctx.font = "11px system-ui, sans-serif";
          const bw = ctx.measureText(label).width + 10;
          const bh = 16;
          let bx = hover.x + 6;
          if (bx + bw > w - 2) bx = hover.x - bw - 6;
          let by = py - bh - 6;
          if (by < 2) by = py + 6;
          ctx.fillStyle = "rgba(20,22,30,0.92)";
          ctx.strokeStyle = CURVE_COLOR;
          ctx.beginPath();
          ctx.roundRect(bx, by, bw, bh, 3);
          ctx.fill();
          ctx.stroke();
          ctx.fillStyle = "#e8eaf2";
          ctx.fillText(label, bx + 5, by + 11.5);
        }
      }
    }
  }, [snap.objects, snap.events, curve, player.previewTimeMs, player.durationMs, player.selection, hover]);

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
    const rect = e.currentTarget.getBoundingClientRect();
    setHover({ x: e.clientX - rect.left, y: e.clientY - rect.top });
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
        onPointerLeave={() => setHover(null)}
        onContextMenu={(e) => e.preventDefault()}
      />
    </div>
  );
}
