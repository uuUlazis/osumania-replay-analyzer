// Timeline: click = jump; long-press (250ms) or drag (>4px) = selection.
import { useEffect, useRef } from "react";
import { useSyncExternalStore } from "react";
import { sessionStore } from "../app/sessionStore";
import { playerStore } from "../app/playerStore";
import {
  JUDGEMENT_BY_HIT,
  JUDGEMENT_COLORS,
  JUDGEMENT_TICK_HEIGHT,
} from "../models/types";

const LONG_PRESS_MS = 250;
const DRAG_THRESHOLD_PX = 4;

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

    // seconds grid
    ctx.strokeStyle = "#22242f";
    const secW = (w / duration) * 1000;
    if (secW > 8) {
      for (let s = 0; s * 1000 < duration; ++s) {
        const x = ((s * 1000) / duration) * w;
        ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, h); ctx.stroke();
      }
    }

    const xOf = (t: number) => (t / duration) * w;
    // object ticks: color + height by replay judgement (300g lowest, MISS tallest)
    const judgeLabel = new Map<number, string>();
    for (const e of snap.events) {
      if (e.phase === "hold_tail") continue; // start tick shows the head judgement
      judgeLabel.set(e.objectIndex, JUDGEMENT_BY_HIT[e.judgement] ?? "MISS");
    }
    for (const o of snap.objects) {
      const x = xOf(o.startTimeMs);
      if (x < -2 || x > w + 2) continue;
      const label = judgeLabel.get(o.index);
      ctx.strokeStyle = label ? JUDGEMENT_COLORS[label] : "#6a7080";
      const bottom = label ? (JUDGEMENT_TICK_HEIGHT[label] ?? 0.7) : 0.7;
      ctx.beginPath();
      ctx.moveTo(x, h * 0.25);
      ctx.lineTo(x, h * bottom);
      ctx.stroke();
      if (o.endTimeMs !== undefined) {
        const xe = xOf(o.endTimeMs);
        ctx.strokeStyle = "rgba(160,168,190,0.35)";
        ctx.beginPath();
        ctx.moveTo(x, h * 0.55); ctx.lineTo(xe, h * 0.55);
        ctx.stroke();
      }
    }
    // selection overlay
    if (player.selection) {
      const x1 = xOf(player.selection.startMs);
      const x2 = xOf(player.selection.endMs);
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

  const dragRef = useRef<{
    pointerId: number;
    startX: number;
    startTime: number;
    longPressFired: boolean;
    moved: boolean;
    active: boolean;
  } | null>(null);

  const timeAt = (clientX: number) => {
    const canvas = canvasRef.current;
    if (!canvas) return 0;
    const rect = canvas.getBoundingClientRect();
    const ratio = Math.min(1, Math.max(0, (clientX - rect.left) / rect.width));
    return ratio * Math.max(1, player.durationMs);
  };

  const onPointerDown = (e: React.PointerEvent<HTMLCanvasElement>) => {
    e.currentTarget.setPointerCapture(e.pointerId);
    dragRef.current = {
      pointerId: e.pointerId,
      startX: e.clientX,
      startTime: timeAt(e.clientX),
      longPressFired: false,
      moved: false,
      active: true,
    };
    const d = dragRef.current;
    if (d) {
      window.setTimeout(() => {
        // long-press begins selection if pointer is still down & unmoved
        if (dragRef.current && dragRef.current.active && !dragRef.current.moved) {
          dragRef.current.longPressFired = true;
          playerStore.setSelection({
            startMs: dragRef.current.startTime,
            endMs: dragRef.current.startTime,
          });
        }
      }, LONG_PRESS_MS);
    }
  };

  const onPointerMove = (e: React.PointerEvent<HTMLCanvasElement>) => {
    const d = dragRef.current;
    if (!d || !d.active || e.pointerId !== d.pointerId) return;
    if (Math.abs(e.clientX - d.startX) > DRAG_THRESHOLD_PX) d.moved = true;
    if (d.moved || d.longPressFired) {
      const t = timeAt(e.clientX);
      const a = Math.min(d.startTime, t);
      const b = Math.max(d.startTime, t);
      playerStore.setSelection({ startMs: a, endMs: b });
    }
  };

  const onPointerUp = (e: React.PointerEvent<HTMLCanvasElement>) => {
    const d = dragRef.current;
    dragRef.current = null;
    if (!d) return;
    const endT = timeAt(e.clientX);
    if (!d.moved && !d.longPressFired) {
      // single click: jump & clear selection
      playerStore.seek(endT);
      playerStore.setSelection(null);
      return;
    }
    // selection finish: normalize; degenerate -> treat as jump
    const a = Math.min(d.startTime, endT);
    const b = Math.max(d.startTime, endT);
    if (b - a < 5) {
      playerStore.setSelection(null);
      playerStore.seek(endT);
    } else {
      playerStore.setSelection({ startMs: a, endMs: b });
    }
  };

  return (
    <div className="timeline-wrap">
      <canvas
        ref={canvasRef}
        className="timeline-canvas"
        onPointerDown={onPointerDown}
        onPointerMove={onPointerMove}
        onPointerUp={onPointerUp}
        onPointerCancel={onPointerUp}
      />
    </div>
  );
}
