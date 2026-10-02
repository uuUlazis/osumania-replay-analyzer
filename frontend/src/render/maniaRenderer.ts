// Canvas renderer for the mania playfield: downscroll (judgement line at
// the bottom), skin-driven layout (mania skin editor rules in 480-unit
// space) and lazer scroll-speed mapping (visible time = 11485/speed ms).
import type { ManiaObject } from "../models/types";
import { JUDGEMENT_BY_HIT, JUDGEMENT_COLORS } from "../models/types";
import {
  cssColor,
  visibleTimeMs,
  type ManiaSkin,
  type SkinColor,
  type SkinImageRef,
} from "./maniaSkin";

export interface RenderEvents {
  objectIndex?: number;
  column: number;
  objectTimeMs: number;
  actualTimeMs?: number;
  judgement: string;
  phase: string;
  // V1 combined whole-LN judgement (hold_tail events only)
  v1Judgement?: string;
}

export interface DrawOptions {
  previewTimeMs: number;
  objects: ManiaObject[];
  events: RenderEvents[];
  keyState: boolean[];
  skin: ManiaSkin | null;
  scrollSpeed: number;
  windowMs: number;
  // replay input/judgement overlay (dotted column guides + coloured ticks)
  showInputJudges?: boolean;
  // judgement rule the overlay reflects: stable V1 (LN = one judgement) or
  // ScoreV2 (LN tail judged separately, band at the tail end)
  judgeVersion?: "v1" | "v2";
}

export function drawPlayfield(
  ctx: CanvasRenderingContext2D,
  canvasWidth: number,
  canvasHeight: number,
  opts: DrawOptions,
) {
  const { previewTimeMs: t, objects, events, keyState, skin, windowMs, showInputJudges, judgeVersion = "v1" } = opts;
  ctx.clearRect(0, 0, canvasWidth, canvasHeight);
  ctx.fillStyle = "#0b0c12";
  ctx.fillRect(0, 0, canvasWidth, canvasHeight);

  // 480-unit playfield space scaled to canvas height, horizontally centered.
  const scale = canvasHeight / 480;
  const ox = (canvasWidth - 480 * scale) / 2;
  ctx.save();
  ctx.translate(ox, 0);
  ctx.scale(scale, scale);

  const skinKeys = skin?.keyCount ?? 4;
  const stageLeft = skin ? skin.stageLeft : stageLeftDefault(skinKeys);
  const stageWidth = skin ? skin.stageRight - skin.stageLeft : stageWidthDefault(skinKeys);
  const hitY = skin ? skin.hitPosition : 402;
  const T = visibleTimeMs(opts.scrollSpeed);
  const pxPerMs = hitY / T;

  const yOf = (objTime: number) => hitY - (objTime - t) * pxPerMs;

  // ---- background / stage ----
  // (stage side boards and bottom are deliberately not drawn)
  ctx.fillStyle = "#05060a";
  ctx.fillRect(0, 0, 480, 480);

  // column backgrounds
  if (skin) {
    for (const c of skin.columns) {
      ctx.fillStyle = cssColor(c.bg);
      ctx.fillRect(c.x, 0, c.width, 480);
    }
    // column lines (width 0 = disabled, per skin.ini)
    if (skin.columnLineWidth > 0) {
      ctx.fillStyle = "rgba(255,255,255,0.22)";
      for (let i = 0; i <= skin.columns.length; ++i) {
        const x = i < skin.columns.length ? skin.columns[i].x : skin.stageRight;
        ctx.fillRect(x - skin.columnLineWidth / 2, 0, skin.columnLineWidth, hitY);
      }
    }
  } else {
    // default: dark columns with separators
    const cw = stageWidth / skinKeys;
    for (let c = 0; c < skinKeys; ++c) {
      ctx.fillStyle = c % 2 === 0 ? "rgba(16,18,28,0.9)" : "rgba(12,14,22,0.9)";
      ctx.fillRect(stageLeft + c * cw, 0, cw, hitY);
      ctx.fillStyle = "rgba(255,255,255,0.14)";
      ctx.fillRect(stageLeft + c * cw, 0, 1, hitY);
    }
    ctx.fillRect(stageLeft + stageWidth - 1, 0, 1, hitY);
  }

  // stage hint / judgement line
  if (skin?.stageHint?.img) {
    const hgt = skin.stageHint.h * 1.44225;
    drawStretched(ctx, skin.stageHint, skin.stageLeft, hitY - hgt / 2, skin.stageRight - skin.stageLeft, hgt);
  } else if (!skin || skin.judgementLineEnabled) {
    const lineColor: SkinColor = skin ? skin.judgementLineColor : { r: 255, g: 255, b: 255, a: 0.9 };
    ctx.fillStyle = cssColor(lineColor);
    ctx.fillRect(stageLeft, hitY - 0.75, stageWidth, 1.5);
    ctx.fillStyle = "rgba(255,255,255,0.9)";
    ctx.fillRect(stageLeft, hitY - 0.5, stageWidth, 1);
  }

  // column lights: additive stage-light image + column glow. Keys held =
  // steady light; each hit fires a bright osu-style hit lighting burst
  // (420 ms ease-out) with a white-hot core at the judgement line.
  const flash = new Array<number>(skinKeys).fill(0);
  for (const e of events) {
    const dt = t - e.objectTimeMs;
    if (dt < 0 || dt > 420) continue;
    const f = 1 - dt / 420;
    const eased = f * f * (3 - 2 * f);
    if (e.column >= 0 && e.column < skinKeys && eased > flash[e.column])
      flash[e.column] = eased;
  }
  if (skin?.stageLight && !skin.stageLightBlank) {
    for (let c = 0; c < skinKeys; ++c) {
      const intensity = Math.max(keyState[c] ? 1 : 0, flash[c] * 0.9);
      if (intensity <= 0.01) continue;
      drawColumnLight(c, intensity);
    }
  }

  // judgement display: dotted center guide per column (reference video look)
  if (showInputJudges) {
    ctx.save();
    ctx.strokeStyle = "rgba(255,255,255,0.16)";
    ctx.lineWidth = 1;
    ctx.setLineDash([1.5, 5]);
    ctx.beginPath();
    for (let c = 0; c < skinKeys; ++c) {
      const x = columnX(skin, stageLeft, stageWidth, skinKeys, c) + columnW(skin, stageWidth, skinKeys, c) / 2;
      ctx.moveTo(x, 0);
      ctx.lineTo(x, hitY);
    }
    ctx.stroke();
    ctx.restore();
  }

  // ---- keys under notes (KeysUnderNotes skins) ----
  const keysDrawnEarly = skin?.keysUnderNotes === true;
  if (keysDrawnEarly) drawKeys();

  // ---- notes / holds (clipped above the key area) ----
  ctx.save();
  ctx.beginPath();
  ctx.rect(stageLeft, 0, stageWidth, hitY + 2);
  ctx.clip();

  for (const o of objects) {
    const endT = o.endTimeMs ?? o.startTimeMs;
    if (o.startTimeMs - t > windowMs || endT - t < -800) continue;
    const x = columnX(skin, stageLeft, stageWidth, skinKeys, o.column);
    const cw = columnW(skin, stageWidth, skinKeys, o.column);
    if (o.type === "note") {
      drawNoteSprite(ctx, skin, o.column, x, cw, yOf(o.startTimeMs));
    } else {
      drawHoldSprite(ctx, skin, o.column, x, cw, yOf(o.startTimeMs), yOf(endT));
    }
  }

  // judgement display: coloured outline on each input's object + a tick at
  // the actual press/release time (offset from the object = early/late),
  // both in the judgement's colour (matches the reference video).
  // Outlines stay strictly inside the column (inset rects) so they never
  // bleed into the neighbouring track when columnspacing is 0.
  if (showInputJudges) {
    const headByObject = new Map<number, RenderEvents>();
    const tailByObject = new Map<number, RenderEvents>();
    for (const e of events) {
      if (e.objectIndex === undefined) continue;
      if (e.phase === "hold_tail") tailByObject.set(e.objectIndex, e);
      else if (!headByObject.has(e.objectIndex)) headByObject.set(e.objectIndex, e);
    }
    ctx.lineWidth = 1.4;
    ctx.globalAlpha = 0.92;
    for (const o of objects) {
      const e = o.index !== undefined ? headByObject.get(o.index) : undefined;
      if (!e) continue;
      const x = columnX(skin, stageLeft, stageWidth, skinKeys, o.column);
      const cw = columnW(skin, stageWidth, skinKeys, o.column);
      const endT = o.endTimeMs ?? o.startTimeMs;
      const isHold = o.type === "hold";
      const te = o.index !== undefined ? tailByObject.get(o.index) : undefined;
      const top = (isHold ? yOf(endT) : noteTop(skin, o.column, cw, yOf(o.startTimeMs))) + 1;
      const bottom = yOf(o.startTimeMs) - 1;
      const h = Math.max(2, bottom - top);
      if (judgeVersion === "v2") {
        // V2: head keeps its own outline; the tail is judged separately at
        // release, shown as a thin ring at the tail end of the LN (outline
        // only, no filled band).
        ctx.strokeStyle = judgeColor(e.judgement);
        ctx.strokeRect(x + 1, top, cw - 2, h);
        if (isHold && te) {
          ctx.strokeStyle = judgeColor(te.judgement);
          ctx.strokeRect(x + 1, top, cw - 2, Math.min(8, h));
        }
      } else {
        // V1 (stable): the whole LN shares one combined judgement — the
        // hold's colour once released, the head input's colour before.
        const color = isHold && te?.v1Judgement ? te.v1Judgement : e.judgement;
        ctx.strokeStyle = judgeColor(color);
        ctx.strokeRect(x + 1, top, cw - 2, h);
      }
    }
    ctx.globalAlpha = 1;
    for (const e of events) {
      if (e.column < 0 || e.column >= skinKeys) continue;
      const y = yOf(e.actualTimeMs ?? e.objectTimeMs);
      if (y < -4 || y > hitY + 4) continue;
      const x = columnX(skin, stageLeft, stageWidth, skinKeys, e.column);
      const cw = columnW(skin, stageWidth, skinKeys, e.column);
      // V1 hold tails tick in the combined whole-LN judgement colour
      const tick =
        judgeVersion === "v1" && e.phase === "hold_tail" && e.v1Judgement
          ? e.v1Judgement
          : e.judgement;
      ctx.fillStyle = judgeColor(tick);
      ctx.fillRect(x + cw * 0.18, y - 1.6, cw * 0.64, 3.2);
    }
  }

  ctx.restore();

  // ---- keys at the bottom ----
  if (!keysDrawnEarly) drawKeys();

  ctx.restore();

  function drawColumnLight(col: number, intensity: number) {
    // Faithful to osu!stable: only the skin's mania-stage-light sprite is
    // drawn (tinted with the column's ColourLight). No synthesized glow or
    // hit burst — skins that ship a blank sprite get no light, matching the
    // in-game look.
    const img = skin?.stageLight?.img;
    if (!skin || !img) return;
    const x = columnX(skin, stageLeft, stageWidth, skinKeys, col);
    const cw = columnW(skin, stageWidth, skinKeys, col);
    const lightY = skin.lightPosition;
    const lh = skin.stageLight!.h;
    const light: SkinColor = skin.columns[col]?.light ?? FALLBACK_LIGHT;
    ctx.save();
    ctx.globalCompositeOperation = "lighter";
    ctx.globalAlpha = Math.min(1, intensity);
    ctx.drawImage(img, x, lightY - lh, cw, lh);
    ctx.globalAlpha = Math.min(1, intensity) * 0.7;
    ctx.fillStyle = cssColor(light);
    ctx.fillRect(x, lightY - lh, cw, lh);
    ctx.restore();
  }

  function drawKeys() {
    for (let c = 0; c < skinKeys; ++c) {
      const x = columnX(skin, stageLeft, stageWidth, skinKeys, c);
      const cw = columnW(skin, stageWidth, skinKeys, c);
      drawKey(ctx, skin, c, x, cw, keyState[c]);
    }
  }
}

function noteTop(
  skin: ManiaSkin | null,
  col: number,
  cw: number,
  yBottom: number,
): number {
  const ref = skin ? skin.columns[col]?.note ?? null : null;
  const scaleH = skin ? skin.noteHeightScale : cw * 0.5;
  const h = ref?.img
    ? Math.max(2, scaleH * (ref.img.naturalHeight / ref.img.naturalWidth))
    : Math.max(4, cw * 0.32);
  return yBottom - h;
}

function judgeColor(judgement: string): string {
  const label = JUDGEMENT_BY_HIT[judgement];
  return (label && JUDGEMENT_COLORS[label]) || "#9aa0b4";
}

function stageLeftDefault(keys: number): number {
  return (480 - stageWidthDefault(keys)) / 2;
}
function stageWidthDefault(keys: number): number {
  return keys * 30;
}
function columnX(
  skin: ManiaSkin | null,
  stageLeft: number,
  stageWidth: number,
  keys: number,
  col: number,
): number {
  if (skin) return skin.columns[col]?.x ?? stageLeft;
  return stageLeft + col * (stageWidth / keys);
}
function columnW(
  skin: ManiaSkin | null,
  stageWidth: number,
  keys: number,
  col: number,
): number {
  if (skin) return skin.columns[col]?.width ?? stageWidth / keys;
  return stageWidth / keys;
}

function drawNoteSprite(
  ctx: CanvasRenderingContext2D,
  skin: ManiaSkin | null,
  col: number,
  x: number,
  cw: number,
  yBottom: number,
) {
  const ref: SkinImageRef | null = skin ? skin.columns[col]?.note ?? null : null;
  const scaleH = skin ? skin.noteHeightScale : cw * 0.5;
  if (ref?.img) {
    const h = Math.max(2, scaleH * (ref.img.naturalHeight / ref.img.naturalWidth));
    ctx.drawImage(ref.img, x - 0.5, yBottom - h, cw + 1, h);
  } else {
    const h = Math.max(4, cw * 0.32);
    const color: SkinColor = skin ? skin.columns[col]?.light ?? FALLBACK_LIGHT : FALLBACK_COLORS[col % FALLBACK_COLORS.length];
    ctx.fillStyle = cssColor(color, 0.9);
    roundRect(ctx, x + 1, yBottom - h, cw - 2, h, 3);
    ctx.fill();
  }
}

function drawHoldSprite(
  ctx: CanvasRenderingContext2D,
  skin: ManiaSkin | null,
  col: number,
  x: number,
  cw: number,
  headBottom: number,
  tailBottom: number,
) {
  const c = skin?.columns[col];
  const head = c?.head ?? c?.note ?? null;
  const tail = c?.tail ?? head;
  const body = c?.body ?? null;
  const scaleH = skin ? skin.noteHeightScale : cw * 0.5;
  const headH = head?.img ? Math.max(2, scaleH * (head.img.naturalHeight / head.img.naturalWidth)) : cw * 0.32;
  const tailH = tail?.img ? Math.max(2, scaleH * (tail.img.naturalHeight / tail.img.naturalWidth)) : headH;

  // span between head bottom (start time) and tail bottom (end time)
  const spanTop = tailBottom; // later time → higher on screen
  const spanBottom = headBottom;
  if (spanBottom - spanTop < 1) return;

  // body between the head/tail sprites
  const bodyTop = spanTop + tailH;
  const bodyBottom = spanBottom - headH;
  if (bodyBottom - bodyTop > 0.5) {
    if (body?.img) {
      const natAspect = body.img.naturalHeight / body.img.naturalWidth;
      const tileH = cw * natAspect;
      if (tileH >= bodyBottom - bodyTop) {
        // crop the top segment of the body texture
        const frac = (bodyBottom - bodyTop) / tileH;
        ctx.drawImage(
          body.img,
          0, 0, body.img.naturalWidth, body.img.naturalHeight * frac,
          x, bodyTop, cw, bodyBottom - bodyTop,
        );
      } else {
        ctx.drawImage(body.img, x, bodyTop, cw, bodyBottom - bodyTop);
      }
    } else {
      const color: SkinColor = c?.light ?? FALLBACK_COLORS[col % FALLBACK_COLORS.length];
      ctx.fillStyle = cssColor(color, 0.55);
      ctx.fillRect(x + cw * 0.15, bodyTop, cw * 0.7, bodyBottom - bodyTop);
    }
  }

  // tail sprite at the END position (bottom edge on it); v2.5+ skins flip
  // the tail texture vertically (NoteFlipWhenUpsideDown default on).
  if (tail?.img) {
    const flip = !skin || skin.version >= 2.5;
    if (flip) {
      ctx.save();
      ctx.translate(0, 2 * spanTop);
      ctx.scale(1, -1);
      ctx.drawImage(tail.img, x - 0.5, spanTop - tailH, cw + 1, tailH);
      ctx.restore();
    } else {
      ctx.drawImage(tail.img, x - 0.5, spanTop - tailH, cw + 1, tailH);
    }
  }
  // head sprite at the START position (bottom edge on it)
  if (head?.img) {
    ctx.drawImage(head.img, x - 0.5, spanBottom - headH, cw + 1, headH);
  }
}

function drawKey(
  ctx: CanvasRenderingContext2D,
  skin: ManiaSkin | null,
  col: number,
  x: number,
  cw: number,
  pressed: boolean,
) {
  const k = skin?.keys[col];
  const ref = (pressed ? k?.imgDown : k?.img) ?? k?.img ?? null;
  if (ref?.img) {
    const h = Math.max(2, ref.h);
    ctx.drawImage(ref.img, x, 480 - h, cw, h);
    return;
  }
  const bg: SkinColor = skin?.columns[col]?.bg ?? { r: 20, g: 22, b: 34, a: 1 };
  const light: SkinColor = skin?.columns[col]?.light ?? { r: 87, g: 200, b: 255, a: 1 };
  ctx.fillStyle = cssColor(bg);
  ctx.fillRect(x, 480 - 22, cw, 22);
  ctx.fillStyle = cssColor(light, pressed ? 0.35 : 0.1);
  ctx.fillRect(x, 480 - 22, cw, 22);
  ctx.strokeStyle = cssColor(light, pressed ? 0.9 : 0.3);
  ctx.strokeRect(x + 0.5, 480 - 21.5, cw - 1, 21);
}

function drawStretched(
  ctx: CanvasRenderingContext2D,
  ref: SkinImageRef,
  x: number,
  y: number,
  w: number,
  h: number,
) {
  if (ref.img) ctx.drawImage(ref.img, x, y, w, h);
}

function roundRect(
  ctx: CanvasRenderingContext2D,
  x: number, y: number, w: number, h: number, r: number,
) {
  ctx.beginPath();
  ctx.moveTo(x + r, y);
  ctx.arcTo(x + w, y, x + w, y + h, r);
  ctx.arcTo(x + w, y + h, x, y + h, r);
  ctx.arcTo(x, y + h, x, y, r);
  ctx.arcTo(x, y, x + w, y, r);
  ctx.closePath();
}

const FALLBACK_LIGHT: SkinColor = { r: 87, g: 200, b: 255, a: 1 };
const FALLBACK_COLORS: SkinColor[] = [
  { r: 227, g: 74, b: 111, a: 1 },
  { r: 87, g: 200, b: 255, a: 1 },
  { r: 87, g: 200, b: 255, a: 1 },
  { r: 227, g: 210, b: 74, a: 1 },
  { r: 227, g: 74, b: 111, a: 1 },
  { r: 87, g: 200, b: 255, a: 1 },
  { r: 87, g: 200, b: 255, a: 1 },
  { r: 154, g: 111, b: 227, a: 1 },
];
