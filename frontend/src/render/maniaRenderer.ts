// Canvas renderer for the mania playfield — osu!stable-faithful rewrite (2026-10-27).
//
// 权威: docs/skin-render-spec-layout.md (A1) + docs/skin-render-spec-elements.md (A2),
// 逆向材料 archive/osu-reverse-engineering/ (IL > 反编译 C#)。绘制语义逐条对照
// ManiaStage/ManiaColumn/ManiaNote/ManiaLongNote/ManiaHitObjectManager 反编译源。
//
// 坐标: 全画布 = stable 的 FixedWidth 全宽 (scale = canvasH/480, 无居中/letterbox)。
// 滚动: y(note,t) = hitY + 21×Eff×(S(t)−S(noteTime)) 逐 timing 段积分; 速度源
// Eff = replay 帧 y (EffectiveSpeed) → UI speed 回退 → 默认 12; 渲染层不乘/除 rate
// (播放时钟已按 rate 前进, 墙钟落速恒定 — stable §R5.5 定案)。
//
// 保留的既定 UI 决定 (不回改, 见各处注释): StageLeft/Right 侧板不画; ScoreV2 尾端
// 判定色带不画 (只描边); 缺元素彩色回退块保留; lightingN/L additive 近似保留 (D19);
// 命中后的音符不隐藏/不变灰 (回放分析器要看得见全部物件 — stable 是 gameplay 行为)。
import type { ManiaObject } from "../models/types";
import { JUDGEMENT_BY_HIT, JUDGEMENT_COLORS } from "../models/types";
import {
  buildScrollSegments,
  computeLayout,
  cssColor,
  type ManiaSkin,
  type SkinAnim,
  type SkinColor,
  type SkinFrame,
  type ScrollSegment,
  type TimingPointDto,
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
  // UI 落速 (Eff 直控语义): 无 replay y 时的回退速度源
  scrollSpeed: number;
  // replay 帧 y = EffectiveSpeed (后端 scrollSpeedEff 透传); >0 时优先于 scrollSpeed
  effSpeed?: number;
  // 谱面 timing 点 (后端 beatmap.timingPoints): 逐段滚动/保守裁剪窗的依据
  timingPoints?: TimingPointDto[];
  windowMs: number;
  // 形参保留: rate 只前进播放时钟, 滚动几何是 song-time 的 (不再乘/除 rate);
  // lazerMode 形参继续透传 (stable V2 尾端色带已按用户定案移除)。
  rate?: number;
  lazerMode?: boolean;
  // replay input/judgement overlay (dotted column guides + coloured ticks)
  showInputJudges?: boolean;
  // 仅显示判定: 停止一切皮肤贴图绘制 (音符/LN/键/侧板/底框/灯光), 只保留
  // 列底/列线/判定线与判定 overlay (调用方保证此时 overlay 强制开启)。
  judgementOnly?: boolean;
  // 砖块默认显示 (无自定义皮肤会话): 元素全部走 0.1.1 程序化砖块回退
  // (FALLBACK_COLORS 圆角砖 + 交替暗列底 + 细白列线), osu 默认皮肤不参与
  // (它只作自定义皮肤的缺省填充, 2026-10-28 用户定案)。
  brickDefault?: boolean;
  // judgement rule the overlay reflects: stable V1 (LN = one judgement) or
  // ScoreV2 (LN tail judged separately)
  judgeVersion?: "v1" | "v2";
  // KeyCoop (1<<25): 无 ini SplitStages 时强制拆分舞台 (A1 R2.4)
  coop?: boolean;
}

// 每帧开销优化: drawPlayfield 每帧都会被调用 (播放时 ~60fps), 而事件数组
// 可达数万条。事件索引 (头尾映射 + 按时间排序副本) 按数组引用用 WeakMap
// 缓存, 只在事件数组变化时重建一次; 每帧的时间窗口裁剪用二分查找。
// (2026-10-26 性能定案, 架构保留。)
interface EventIndex {
  headByObject: Map<number, RenderEvents>;
  tailByObject: Map<number, RenderEvents>;
  byObjectTime: RenderEvents[];
  byDisplayTime: RenderEvents[];
}
const eventIndexCache = new WeakMap<RenderEvents[], EventIndex>();
function displayTimeOf(e: RenderEvents): number {
  return e.actualTimeMs ?? e.objectTimeMs;
}
function eventIndexOf(events: RenderEvents[]): EventIndex {
  const cached = eventIndexCache.get(events);
  if (cached) return cached;
  const headByObject = new Map<number, RenderEvents>();
  const tailByObject = new Map<number, RenderEvents>();
  for (const e of events) {
    if (e.objectIndex === undefined) continue;
    if (e.phase === "hold_tail") tailByObject.set(e.objectIndex, e);
    else if (!headByObject.has(e.objectIndex)) headByObject.set(e.objectIndex, e);
  }
  const byObjectTime = [...events].sort((a, b) => a.objectTimeMs - b.objectTimeMs);
  const byDisplayTime = [...events].sort((a, b) => displayTimeOf(a) - displayTimeOf(b));
  const idx = { headByObject, tailByObject, byObjectTime, byDisplayTime };
  eventIndexCache.set(events, idx);
  return idx;
}
function lowerBoundBy<T>(arr: T[], time: number, getTime: (x: T) => number): number {
  let lo = 0;
  let hi = arr.length;
  while (lo < hi) {
    const mid = (lo + hi) >> 1;
    if (getTime(arr[mid]) < time) lo = mid + 1;
    else hi = mid;
  }
  return lo;
}

// ── 滚动积分模型 (WeakMap 按 timingPoints 数组引用缓存; 重建与会话/裁剪同步) ──
// S(t) = ∫ dt/BL_eff(t) (单位累计, Eff 无关); 滚动位移 = 21×Eff×(S(b)−S(a))。
interface ScrollModel {
  segs: ScrollSegment[];
  cum: number[]; // cum[i] = Σ_{j<i} (nextStart−start)/bl
}
const EMPTY_TPS: TimingPointDto[] = [];
const scrollModelCache = new WeakMap<TimingPointDto[], ScrollModel>();
function scrollModelOf(tps: TimingPointDto[] | undefined): ScrollModel {
  const key = tps && tps.length ? tps : EMPTY_TPS;
  const cached = scrollModelCache.get(key);
  if (cached) return cached;
  const segs = buildScrollSegments(key);
  const cum = new Array<number>(segs.length);
  cum[0] = 0;
  for (let i = 1; i < segs.length; ++i) {
    const span = Math.max(0, segs[i].startMs - segs[i - 1].startMs);
    cum[i] = cum[i - 1] + span / segs[i - 1].beatLength;
  }
  const model = { segs, cum };
  scrollModelCache.set(key, model);
  return model;
}
function scrollPos(m: ScrollModel, t: number): number {
  const segs = m.segs;
  let lo = 0;
  let hi = segs.length - 1;
  while (lo < hi) {
    const mid = (lo + hi + 1) >> 1;
    if (segs[mid].startMs <= t) lo = mid;
    else hi = mid - 1;
  }
  const seg = segs[lo];
  return m.cum[lo] + (t - seg.startMs) / seg.beatLength; // 首段向前/末段向后线性外推
}
function beatLengthAt(m: ScrollModel, t: number): number {
  const segs = m.segs;
  let lo = 0;
  let hi = segs.length - 1;
  while (lo < hi) {
    const mid = (lo + hi + 1) >> 1;
    if (segs[mid].startMs <= t) lo = mid;
    else hi = mid - 1;
  }
  return segs[lo].beatLength;
}
// 逆滚动: 滚动累计值 → 时间 (scrollPos 的反函数; 分段线性单调, 二分求段)
function scrollTimeAt(m: ScrollModel, target: number): number {
  const segs = m.segs;
  const cum = m.cum;
  let lo = 0;
  let hi = segs.length - 1;
  while (lo < hi) {
    const mid = (lo + hi + 1) >> 1;
    if (cum[mid] <= target) lo = mid;
    else hi = mid - 1;
  }
  const seg = segs[lo];
  return seg.startMs + (target - cum[lo]) * seg.beatLength;
}

// ── 模块级逐帧状态 (无每帧分配; seek/大跳变时整体失效重置) ────────────────
// keyPressAt/keyReleaseAt: 按键灯帧重启 + KeyD 80ms 交叉淡化 + LN 头释放调度
// holdStartAt/holdReleaseAt/holdActive: LightingL 80ms 淡入 / 120ms 淡出
// bodyHeldMs: LN 体动画仅在按住期间推进 (R6.4)
const MAX_COLS = 18;
// LN 体帧时钟的**逐物件**按住时长 (o.index → ms)。stable 语义 (ManiaLongNote.cs
// L66-67/L130-132/L195 + Obf000443.Obf006447): BodySprite 构造时 Obf006419=false
// (帧推进禁用, 恒第 0 帧), 仅本物件 IsHeld 时置 true → 30ms/帧从 0 起推进; 松开
// 再置 false → 冻结在当前帧, 再按继续。旧的按列累计会把同列早按/前一物件的
// 按键时长带进未击中 LN 的帧相位 (2026-10-28 用户复测: "未击中时意外开始播放")。
const bodyHeldByObject = new Map<number, number>();
const frameState = {
  lastT: -1,
  keyPressAt: new Float64Array(MAX_COLS),
  keyReleaseAt: new Float64Array(MAX_COLS),
  prevKeys: new Uint8Array(MAX_COLS),
  holdStartAt: new Float64Array(MAX_COLS),
  holdReleaseAt: new Float64Array(MAX_COLS),
  holdActive: new Uint8Array(MAX_COLS),
  nFlash: new Float64Array(MAX_COLS), // 最近一次判定成功的 LightingN 触发时刻
  scratchFlash: new Float64Array(MAX_COLS), // 每帧窗口扫描结果 (负数 = 无)
  holdCovered: new Uint8Array(MAX_COLS), // 每帧: 该列是否有进行中的 hold
};
function updateFrameState(t: number, keyState: boolean[], cols: number): number {
  const st = frameState;
  const seek = st.lastT < 0 || t < st.lastT - 1 || t - st.lastT > 800;
  if (seek) {
    st.prevKeys.fill(0);
    st.holdActive.fill(0);
    bodyHeldByObject.clear();
    for (let c = 0; c < MAX_COLS; ++c) {
      st.keyReleaseAt[c] = -1;
      st.holdReleaseAt[c] = -1;
      st.keyPressAt[c] = t;
      st.holdStartAt[c] = t;
    }
  }
  const dt = seek ? 0 : Math.max(0, t - st.lastT);
  for (let c = 0; c < cols; ++c) {
    const k = keyState[c] ? 1 : 0;
    if (k && !st.prevKeys[c]) {
      st.keyPressAt[c] = t;
      st.keyReleaseAt[c] = -1;
    } else if (!k && st.prevKeys[c]) {
      st.keyReleaseAt[c] = t;
    }
    st.prevKeys[c] = k;
  }
  st.lastT = t;
  return dt;
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
function fallbackColor(col: number, skin: ManiaSkin | null): SkinColor {
  const c = skin?.columns[col];
  return c?.light ?? FALLBACK_COLORS[col % FALLBACK_COLORS.length];
}
// 0.1.1 砖块观感的圆角矩形路径 (调用方自行 fill)
function roundRectPath(
  ctx: CanvasRenderingContext2D,
  x: number, y: number, w: number, h: number, r: number,
): void {
  ctx.beginPath();
  ctx.moveTo(x + r, y);
  ctx.arcTo(x + w, y, x + w, y + h, r);
  ctx.arcTo(x + w, y + h, x, y + h, r);
  ctx.arcTo(x, y + h, x, y, r);
  ctx.arcTo(x, y, x + w, y, r);
  ctx.closePath();
}
// miss 变灰 (R5.6: Color.Gray 60ms 渐变 — 此处近似为直接灰化, 无渐变)
const GREY_TINT: SkinColor = { r: 122, g: 122, b: 122, a: 1 };
function greyFrame(frame: SkinFrame): SkinFrame {
  return { img: tintedImage(frame.img, GREY_TINT) as unknown as HTMLImageElement, w: frame.w, h: frame.h };
}
function judgeColor(judgement: string): string {
  const label = JUDGEMENT_BY_HIT[judgement];
  return (label && JUDGEMENT_COLORS[label]) || "#9aa0b4";
}

// ── 精灵着色 (R8.5 tint 语义; 缓存结果, 热路径零分配) ────────────────────
const tintCache = new WeakMap<HTMLImageElement, { key: string; out: HTMLCanvasElement }>();
function tintedImage(img: HTMLImageElement, color: SkinColor): HTMLImageElement | HTMLCanvasElement {
  const white = color.r >= 250 && color.g >= 250 && color.b >= 250 && color.a >= 0.99;
  if (white) return img;
  const key = Math.round(color.r) + "," + Math.round(color.g) + "," + Math.round(color.b) + "," + color.a.toFixed(2);
  const hit = tintCache.get(img);
  if (hit && hit.key === key) return hit.out;
  const canvas = document.createElement("canvas");
  canvas.width = img.naturalWidth;
  canvas.height = img.naturalHeight;
  const c2d = canvas.getContext("2d");
  if (!c2d) return img;
  c2d.drawImage(img, 0, 0);
  c2d.globalCompositeOperation = "multiply";
  c2d.fillStyle = cssColor({ r: color.r, g: color.g, b: color.b, a: 1 });
  c2d.fillRect(0, 0, canvas.width, canvas.height);
  c2d.globalCompositeOperation = "destination-in";
  c2d.drawImage(img, 0, 0);
  tintCache.set(img, { key, out: canvas });
  return canvas;
}

// 动画取帧 (R3.14): loop = 连续循环; restart = 指定起点重启 (按住重启语义);
// once = LoopOnce (LightingN), 帧停在末帧。
function animFrameLoop(anim: SkinAnim | null, timeMs: number): SkinFrame | null {
  if (!anim || anim.frames.length === 0) return null;
  const len = anim.frames.length;
  let idx = Math.floor(timeMs / anim.frameMs) % len;
  if (idx < 0) idx += len;
  return anim.frames[idx];
}
function animFrameRestart(anim: SkinAnim | null, sinceMs: number, now: number): SkinFrame | null {
  if (!anim || anim.frames.length === 0) return null;
  const len = anim.frames.length;
  const idx = Math.floor(Math.max(0, now - sinceMs) / anim.frameMs) % len;
  return anim.frames[idx];
}
function animFrameOnce(anim: SkinAnim | null, sinceMs: number, now: number): SkinFrame | null {
  if (!anim || anim.frames.length === 0) return null;
  const idx = Math.min(anim.frames.length - 1, Math.floor(Math.max(0, now - sinceMs) / anim.frameMs));
  return anim.frames[idx];
}
// 进屏锚定帧 (2026-10-28 IL 复核): stable 的 rice/头/尾动画精灵相位零点 = 首条
// 滚动变换 StartTime = 该 note 滚到屏幕上缘 (y=0) 的时刻 tEnter — 逐物件错相,
// 非全局同相 (Obf000443.Obf006447 L241-245); 时间未到 tEnter 恒帧 0。
function animFrameEnter(anim: SkinAnim | null, now: number, tEnter: number, lazer = false): SkinFrame | null {
  if (!anim || anim.frames.length === 0) return null;
  if (now <= tEnter) return anim.frames[0];
  // D2 对齐: lazer 帧间隔 = AnimationFramerate>0 ? 1000/AF : 1000/帧数
  // (LegacySkinExtensions.getFrameLength); stable 恒 16.667ms。
  const fms = lazer && anim.frameMsLazer ? anim.frameMsLazer : anim.frameMs;
  const idx = Math.floor((now - tEnter) / fms) % anim.frames.length;
  return anim.frames[idx];
}

// 音符高度 (R5.2): 绘制宽恒 = 列宽; 高 = texH × noteBase / texW (noteBase = WFNHS
// 或 min(ColumnWidth)×shrink; @2x 不影响该数学)。
function frameHeightUnits(frame: SkinFrame, colWidth: number, noteBase: number): number {
  void colWidth;
  return (frame.img.naturalHeight * noteBase) / frame.img.naturalWidth;
}

// LN 体平铺 (R4.5/R4.7 伪代码逐字): 源窗口 = 纵向重复贴图条带 [offset, offset+L),
// RepeatBottom/未知=0 / RepeatTop=tileH−L / RTAB=(tileH−L)/2; Stretch=整张拉伸。
// L > tileH 时按条带周期逐段绘制 (stable 裁剪平铺的等价实现)。
function drawBodyStrip(
  ctx: CanvasRenderingContext2D,
  frame: SkinFrame,
  x: number,
  w: number,
  top: number,
  bottom: number,
  style: string,
): void {
  const fullL = bottom - top;
  if (fullL <= 0.25) return;
  const texW = frame.img.naturalWidth;
  const texH = frame.img.naturalHeight;
  if (style === "stretch") {
    // R4.4: 整张贴图纵向拉伸到 L
    ctx.drawImage(frame.img, x, top, w, fullL);
    return;
  }
  // tileH = 贴图原生高 (单位) = texH / 1.6 — 不随列宽缩放 (2026-10-28 IL 勘误,
  // ln-body-il 定案: ManiaHitObjectManager.cs wrap 分支 num7 = Obf019153/Obf020593
  // = 纹理原生 px 高 ÷ 1.6; 之前按列宽等比 (×noteBase/texW) 把 tile 压扁约 3 倍,
  // 圆顶被竖向压平)。源窗口单位 → 纹理 px: pxPerUnit = texH / tileH = 1.6 (恒定)。
  const tileH = texH / 1.6;
  if (!(tileH > 0.25)) return;
  const pxPerUnit = texH / tileH;
  let offset: number;
  if (style === "repeatTop") offset = tileH - fullL;
  else if (style === "repeatTopBottom") offset = (tileH - fullL) / 2;
  else offset = 0; // repeatBottom / 未知值 → 0 (stable switch default)
  const stripStart = offset;
  const stripEnd = offset + fullL;
  let y = top;
  let p = stripStart;
  const texTop = frame.img;
  let guard = 0;
  while (p < stripEnd - 0.01 && guard < 128) {
    guard++;
    const texYUnits = ((p % tileH) + tileH) % tileH;
    const avail = tileH - texYUnits;
    const chunk = Math.min(avail, stripEnd - p);
    if (chunk > 0.01) {
      // 段目标高度 +0.75u 重叠 (非末段): 消除相邻段浮点接缝的发丝分隔线;
      // 源高度保持精确, 重叠部分为该段底部纹理的轻微重复 (不透明贴图不可见)。
      const isLast = p + chunk >= stripEnd - 0.01;
      const destH = isLast ? chunk : chunk + 0.75;
      ctx.drawImage(
        texTop,
        0,
        texYUnits * pxPerUnit,
        texW,
        chunk * pxPerUnit,
        x,
        y,
        w,
        destH,
      );
    }
    y += chunk;
    p += chunk;
  }
}

export function drawPlayfield(
  ctx: CanvasRenderingContext2D,
  canvasWidth: number,
  canvasHeight: number,
  opts: DrawOptions,
) {
  const skin = opts.skin;
  const n = skin ? skin.keyCount : 4;
  const layout = computeLayout(skin, n, canvasWidth, canvasHeight, !!opts.coop);
  if (!layout) return;
  const lay = layout; // 嵌入式函数声明 (hoisted) 内不能依赖 narrowing, 用捕获常量
  const t = opts.previewTimeMs;
  const fixedW = canvasWidth / (canvasHeight / 480);
  // 480 单位空间 → 画布像素的统一缩放 (2026-10-28 修复: 重写版漏掉了该变换,
  // 所有元素按单位数直绘像素, 台面只占画布左上角 — "渲染范围不对"的根因)。
  const unitScale = canvasHeight / 480;
  ctx.save();
  ctx.scale(unitScale, unitScale);
  const hitY = layout.hitY;
  // 速度源 Eff: replay 帧 y (EffectiveSpeed) 优先 → UI speed → 默认 12 (D2/D17 修正)
  const eff =
    opts.effSpeed !== undefined && opts.effSpeed > 0
      ? opts.effSpeed
      : Math.max(1, Math.min(40, opts.scrollSpeed || 12));
  const model = scrollModelOf(opts.timingPoints);
  const sNow = scrollPos(model, t);
  const blNow = beatLengthAt(model, t);
  const yOf = (noteTime: number): number =>
    hitY + (21 / 1.6) * eff * (sNow - scrollPos(model, noteTime));
  const timeForUnits = (units: number): number => (units * blNow) / ((21 / 1.6) * eff);
  // 进屏锚定相位 (Obf000443 首条变换 StartTime = 滚到屏幕上缘 y=0 的时刻):
  // yOf(noteTime)=0 ⇒ s(t*) = s(noteTime) − hitY/k; 头/单点锚 startTime, 尾锚 endTime
  const enterAt = (noteTime: number): number =>
    scrollTimeAt(model, scrollPos(model, noteTime) - hitY / ((21 / 1.6) * eff));

  const lazerMode = opts.lazerMode === true;
  const keyState = opts.keyState;
  const frameDt = updateFrameState(t, keyState, n);
  // 每列是否有进行中的 hold (LightingL + LN 钉线辅助)
  const st = frameState;
  st.holdCovered.fill(0);
  for (const o of opts.objects) {
    if (o.type !== "hold") continue;
    const endT = o.endTimeMs ?? o.startTimeMs;
    if (o.column >= 0 && o.column < n && o.startTimeMs <= t && endT >= t) {
      st.holdCovered[o.column] = 1;
    }
  }
  // LightingL 淡入/淡出状态机 (R8.4: 按住 80ms 淡入, 松开 120ms 淡出)
  for (let c = 0; c < n; ++c) {
    const want = st.holdCovered[c] && keyState[c] ? 1 : 0;
    if (want && !st.holdActive[c]) {
      st.holdActive[c] = 1;
      st.holdStartAt[c] = t;
      st.holdReleaseAt[c] = -1;
    } else if (!want && st.holdActive[c]) {
      st.holdActive[c] = 0;
      st.holdReleaseAt[c] = t;
    }
  }

  const events = opts.events;
  const eidx = eventIndexOf(events);
  // LN 体帧时钟: 逐物件按住时长累计 (每帧一次 — lnGeom 会被三个 pass 各调一次,
  // 不能放在那里)。钉住条件与 lnGeom.heldNow 一致: 头判定成功 + 输入时刻已到 +
  // 键仍按住。帧相位 = 本物件累计按住时长, 与 stable 的 IsHeld 门控一致。
  for (const o of opts.objects) {
    if (o.type !== "hold" || o.column < 0 || o.column >= n) continue;
    if (o.index === undefined) continue;
    const headEv = eidx.headByObject.get(o.index);
    // stable 判据 = IsHeld (按下即推进, 与头判好坏无关 — ManiaLongNote L130-132)。
    // 头非 miss → 用实际按下时刻; 头 miss/缺 (V1 LN 头归属残差: 晚按被记 miss)
    // → 回退 keyState && t ≥ start−158ms (早期输入窗近似; 真·未按的 LN 键态为
    // up, 不会累计, 未击中仍恒帧 0)。
    const gate = headEv && headEv.judgement !== "miss"
      ? lazerMode
        ? Math.max(displayTimeOf(headEv), o.startTimeMs) // D24: 早期击中从 StartTime 起累计
        : displayTimeOf(headEv)
      : lazerMode
        ? Number.POSITIVE_INFINITY // lazer: 头 miss/未按 → isHolding 恒 false → 体帧 0
        : o.startTimeMs - 158;
    if (t >= gate && keyState[o.column]) {
      bodyHeldByObject.set(o.index, (bodyHeldByObject.get(o.index) ?? 0) + frameDt);
    }
  }
  const showInputJudges = opts.showInputJudges === true || opts.judgementOnly === true;
  const judgementOnly = opts.judgementOnly === true;
  const brick = opts.brickDefault === true;
  const judgeVersion = opts.judgeVersion === "v2" ? "v2" : "v1";
  const entryMs = opts.windowMs;
  const exitMs = opts.windowMs; // 对称保守窗 (scrollWindowMs 已含 SV 安全裕量)

  // ---- 背景 (舞台矩形 = [stageLeft, stageLeft+stageWidth] × [0,480]) ----
  ctx.fillStyle = "#0b0c12";
  ctx.fillRect(0, 0, fixedW, 480);
  ctx.fillStyle = "#05060a";
  ctx.fillRect(layout.stageLeft, 0, layout.stageWidth, 480);
  // ---- 侧板 StageLeft/Right (R7.2, 2026-10-28 用户恢复渲染决定 — 之前"不画"的
  // 既定决定作废: 无侧板时部分皮肤舞台缺少边框视觉): origin TopRight@舞台左缘 /
  // TopLeft@舞台右缘。缩放 = 等比、纵向拉满舞台全高 (2026-10-28 用户对照游戏内
  // 实测修正: R7.2 原文 X=stageH/480 的读法会画出 32/64u 宽面板, 明显偏宽 —
  // 实际 X=Y=stageH/texH 等比 → 宽 = texW×480/texH = texW/1.6 单位, 高 480)。
  if (!judgementOnly && !brick && skin?.stageLeftImg?.img) {
    const w = skin.stageLeftImg.w;
    ctx.drawImage(skin.stageLeftImg.img, layout.stageLeft - w, 0, w, 480);
  }
  if (!judgementOnly && !brick && skin?.stageRightImg?.img) {
    const w = skin.stageRightImg.w;
    ctx.drawImage(skin.stageRightImg.img, layout.stageRight, 0, w, 480);
  }
  // (舞台角灯 R7.5 仍不渲染: 旋转锚点几何 [待验证], 与侧板是不同元素。)

  // ---- 列底色块 (R9.3, depth 0.6): Colour{i+1} 默认 Black, 全台高 ----
  // 砖块默认: 交替暗列底 (0.1.1 观感, 不读皮肤 Colour)
  for (let c = 0; c < n; ++c) {
    const col = layout.columns[c];
    const bg = brick
      ? c % 2 === 0
        ? "rgba(16,18,28,0.9)"
        : "rgba(12,14,22,0.9)"
      : skin?.columns[c]?.bg
        ? cssColor(skin.columns[c].bg!)
        : "rgba(16,18,28,0.9)";
    ctx.fillStyle = bg;
    ctx.fillRect(col.x, 0, col.width, 480);
  }

  // ---- 列边界线 (R9.2, depth 0.61): n+1 项逐缘; 宽 = 0.74×lw (768px→单位 ÷1.6);
  // 高 = 判定线上方 [0, hitY]; 左缘 lw[i]>0; 右缘 (lw[i+1]>0 && version≥2.4) 或末列
  // (末列 x−0.1 修正); 颜色 ColourColumnLine (默认 White)。
  if (brick) {
    // 砖块默认: 细白分隔线至判定线 (0.1.1 观感)
    ctx.fillStyle = "rgba(255,255,255,0.14)";
    for (let i = 0; i <= n; ++i) {
      const x = i < n ? layout.columns[i].x : layout.stageRight;
      ctx.fillRect(x, 0, 1, hitY);
    }
  } else if (skin) {
    const lw = skin.columnLineWidth;
    ctx.fillStyle = cssColor(skin.columnLineColor);
    const rightGateOpen = skin.version >= 2.4;
    for (let i = 0; i < n; ++i) {
      const col = layout.columns[i];
      if (lw[i] > 0) {
        ctx.fillRect(col.x, 0, (0.74 * lw[i]) / 1.6, hitY);
      }
      const isLast = i === n - 1;
      if ((lw[i + 1] > 0 && rightGateOpen) || isLast) {
        const wRight = (0.74 * lw[i + 1]) / 1.6;
        if (wRight > 0) {
          ctx.fillRect(col.x + col.width - (isLast ? 0.1 : 0), 0, wRight, hitY);
        }
      }
    }
  } else {
    // null skin 兜底 (实际不会走到: loadManiaSkin 总返回默认皮肤)
    ctx.fillStyle = "rgba(255,255,255,0.5)";
    for (let i = 0; i <= n; ++i) {
      const x = i < n ? layout.columns[i].x : layout.stageRight;
      ctx.fillRect(x, 0, 1, hitY);
    }
  }

  // ---- StageHint (R7.1, depth 0.62): 宽=舞台宽, 高 = texH×0.9 (Obf005036 1.6026
  // ≈ 1.6 → ×0.9015, 按 0.9 近似), CentreLeft 中心对齐判定线; 与判定线互相独立都画
  // (旧渲染器 "hint 缺失才画线" 不符合 stable — A2 D20 修正)。
  if (!judgementOnly && !brick && skin?.stageHint?.img) {
    const hintH = skin.stageHint.h * 0.9;
    ctx.drawImage(
      skin.stageHint.img,
      layout.stageLeft,
      hitY - hintH / 2,
      layout.stageWidth,
      hintH,
    );
  }

  // ---- 判定线 (R9.1): JudgementLine=true 恒画 (与 hint 无关); 高 0.70 单位
  // (0.7×1.6026/1.6), TopLeft @ hitY 向下; ColourJudgementLine (默认 White)。
  if (judgementOnly || !skin || skin.judgementLineEnabled) {
    // 仅显示判定: 强制画代码级白色判定线 (命中位置参考, 不受皮肤 JudgementLine=0 影响)
    const lc: SkinColor = judgementOnly
      ? { r: 255, g: 255, b: 255, a: 1 }
      : skin
        ? skin.judgementLineColor
        : { r: 255, g: 255, b: 255, a: 1 };
    ctx.fillStyle = cssColor(lc);
    ctx.fillRect(layout.stageLeft, hitY, layout.stageWidth, 0.7);
  }

  // ---- StageBottom (R7.3+§10.4): 已移至 over 组 depth 0.94 (keys 之上、判定灯之下) —
  // 2026-10-27 用户对照反馈"图层不对": stable 里它在按键之上, 背景组绘制被键盖住是错的。

  // ---- 列灯 StageLight (R8.1, depth 0.72): 每列一个; BottomLeft @ (列左缘,
  // LightPosition=413), 宽=列宽, 高=贴图自然高 (Y scale=stageH/480=1);
  // tint=ColourLight{i+1} (R8.5); 按下 alpha 1 + 动画从 0 帧重启, 松开 400ms 淡出
  // 且高度塌缩 (400×(100/Obf021294) ≈ 400ms NM, [待验证] 语义按常数实现);
  // 1×1 空贴图 = 禁灯 (R8.2)。
  if (!judgementOnly && !brick && skin?.stageLight && !skin.stageLightBlank) {
    const anim = skin.stageLight;
    for (let c = 0; c < n; ++c) {
      const col = layout.columns[c];
      const held = keyState[c];
      const relAt = st.keyReleaseAt[c];
      let alpha = 0;
      let heightFactor = 1;
      if (held) {
        alpha = 1;
      } else if (relAt >= 0 && t - relAt < 400) {
        alpha = 1 - (t - relAt) / 400;
        heightFactor = alpha;
      } else {
        continue;
      }
      const frame = animFrameRestart(anim, st.keyPressAt[c], t); // 动画持续推进 (含淡出期)
      if (!frame) continue;
      const colEl = skin.columns[c];
      const tinted = tintedImage(frame.img, colEl?.light ?? FALLBACK_LIGHT);
      ctx.globalAlpha = alpha;
      ctx.drawImage(tinted, col.x, layout.lightY - frame.h * heightFactor, col.width, frame.h * heightFactor);
      ctx.globalAlpha = 1;
    }
  }

  // ---- 按键 (KeysUnderNotes=true 时入背景组, 在 note 层之下 — A2 §10) ----
  const keysDrawnEarly = !judgementOnly && !brick && skin?.keysUnderNotes === true;
  if (keysDrawnEarly) drawKeys();

  // ---- notes / LN 组 (Obf020548, 裁剪到舞台矩形全高 — D14: miss 的物件继续落到台底,
  // 旧的 clip 到 hitY+2 已废弃); 深度序 = body(0.795) → tail(0.7975) → head/note(0.8) ----
  // 仅显示判定: 三个绘制 pass 由下方各循环内的 judgementOnly 守卫跳过 (精灵 = 皮肤元素)。
  ctx.save();
  ctx.beginPath();
  ctx.rect(layout.stageLeft, 0, layout.stageWidth, 480);
  ctx.clip();

  const noteBase = layout.noteBase;
  const objects = opts.objects;
  interface LnGeom {
    c: number;
    col: { x: number; width: number };
    headAnim: SkinAnim | null;
    tailAnim: SkinAnim | null;
    bodyAnim: SkinAnim | null;
    headFrame: SkinFrame | null;
    tailFrame: SkinFrame | null;
    headH: number;
    tailH: number;
    headY: number;
    tailY: number;
    bodyTop: number;
    bodyAnchorBottom: number;
    clipBottom: number;
    clipped: boolean;
    heldNow: boolean;
    missAlpha: number;
    headSuccess: boolean;
    headMiss: boolean;
    headTime: number;
    hideAll: boolean;
    greyAll: boolean;
    bodyStyle: string;
  }
  const lnGeom = (o: ManiaObject): LnGeom => {
    const c = o.column;
    const col = layout.columns[c];
    const colEl = skin?.columns[c];
    const endT = o.endTimeMs ?? o.startTimeMs;
    const headAnim = colEl?.head ?? colEl?.note ?? null;
    const tailAnim = colEl?.tail ?? headAnim; // R6.3 缺尾→头贴图 (load 时已回退, 双保险)
    const bodyAnim = colEl?.body ?? null;
    const headFrame = headAnim ? animFrameEnter(headAnim, t, enterAt(o.startTimeMs), lazerMode) : null;
    const tailFrame = tailAnim ? animFrameEnter(tailAnim, t, enterAt(endT), lazerMode) : null;
    const headH = headFrame
      ? frameHeightUnits(headFrame, col.width, noteBase)
      : Math.max(4, col.width * 0.32);
    const tailH = tailFrame ? frameHeightUnits(tailFrame, col.width, noteBase) : headH;
    // LN 头钉线 (Obf021387/Obf021388): 头判定成功且键仍按住 → 冻结在判定线;
    // 松开 → 从释放时刻重新下滚; 头失败 (miss) → 自然滚动 (WasBroken 无钉)。
    // replay 近似: 头判定事件非 miss + 当前键态 (帧数据), 无逐物件 release 事件。
    const headEv = o.index !== undefined ? eidx.headByObject.get(o.index) : undefined;
    const success = !!headEv && headEv.judgement !== "miss";
    const pressT = headEv ? displayTimeOf(headEv) : o.startTimeMs;
    // D24: lazer 钉线/收缩从 StartTime 起 — 早期击中先继续滚动, 到 StartTime 才钉
    // (DrawableHoldNote.cs L255-263 注释); stable 按我方 IL 定案 = 输入时刻即钉。
    const pressedNow = success && t >= (lazerMode ? Math.max(pressT, o.startTimeMs) : pressT);
    // 判定驱动的可见性 (R5.6 IsHiddenAfterHit / miss 变灰): V1 由尾部合并判定驱动,
    // V2 头/尾分别驱动。displayTimeOf = 实际输入时刻, 已发生才生效。
    const headTime = headEv ? displayTimeOf(headEv) : Number.POSITIVE_INFINITY;
    const headMiss = !!headEv && headEv.judgement === "miss";
    const tailEv = o.index !== undefined ? eidx.tailByObject.get(o.index) : undefined;
    const tailJudge = tailEv
      ? judgeVersion === "v1"
        ? tailEv.v1Judgement ?? tailEv.judgement
        : tailEv.judgement
      : undefined;
    const tailDone = !!tailEv && displayTimeOf(tailEv) <= t;
    const hideAll = tailDone && tailJudge !== "miss";
    const greyAll = tailDone && tailJudge === "miss";
    const heldNow = pressedNow && !!keyState[c];
    let headY: number;
    if (heldNow) {
      headY = hitY;
    } else if (pressedNow) {
      let relT = st.keyReleaseAt[c] >= 0 ? st.keyReleaseAt[c] : t;
      if (relT < pressT) relT = pressT;
      headY = hitY + (21 / 1.6) * eff * (scrollPos(model, t) - scrollPos(model, relT));
    } else {
      headY = yOf(o.startTimeMs);
    }
    // 体/尾裁剪 (Obf021387: clip 到 [舞台] ∩ y ≤ HitPos − headH/2; Obf021388:
    // 释放后裁剪矩形跟随头下滑张开 — 此处用头当前可见中点)
    const clipBottom = pressedNow ? headY - headH / 2 : Number.POSITIVE_INFINITY;
    const tailY = yOf(endT);
    // lazer: 断连/miss → 整条 FadeColour(DarkGray,60) + FadeOut(150,In)
    // (DrawableManiaHitObject L77-79 + LegacyBodyPiece.applyMissingDim);
    // stable: 灰化继续坠 (不淡出)。missAlpha 作用于体/头/尾三层 (D7)。
    const missFadeAt = lazerMode
      ? headMiss
        ? headTime
        : greyAll && tailEv
          ? displayTimeOf(tailEv)
          : Number.POSITIVE_INFINITY
      : Number.POSITIVE_INFINITY;
    const missAlpha = Number.isFinite(missFadeAt)
      ? Math.max(0, Math.min(1, 1 - (t - missFadeAt) / 150))
      : 1;
    return {
      c,
      col,
      headAnim,
      tailAnim,
      bodyAnim,
      headFrame,
      tailFrame,
      headH,
      tailH,
      headY,
      tailY,
      // D8: lazer 体尾端嵌入 = tailH/2 (DrawableHoldNote.cs L250-253「tail notes」);
      // stable = headH/2 (R4.3/R6.6, L = D(start,end))
      bodyTop: lazerMode ? tailY - tailH / 2 : tailY - headH / 2,
      bodyAnchorBottom: yOf(o.startTimeMs) - headH / 2, // 体锚 (自身调度, Obf021378 offset −headH/2)
      clipBottom,
      clipped: pressedNow,
      heldNow,
      headSuccess: success,
      headMiss,
      headTime,
      missAlpha,
      hideAll,
      greyAll,
      // D3/D4/D8 对齐: lazer 只读全局 NoteBodyStyle, 所有 repeat 形态同为无帽
      // 平铺 (LegacyBodyPiece 只分 Stretch/Repeat), <2.5 → null → Repeat
      // (LegacySkin.cs L207-208, 与 stable 强制 Stretch 相反)。
      // D2 修正: lazer 显式 NoteBodyStyle 任何版本生效, 缺省才按版本
      // (<2.5→null→Repeat, ≥2.5→RepeatBottom — LegacySkin.cs L202-210)
      bodyStyle: lazerMode
        ? (skin?.bodyStyleGlobal ?? ((skin?.version ?? 2.7) < 2.5 ? null : "repeatBottom")) === "stretch"
          ? "stretch"
          : "repeat"
        : colEl?.bodyStyle ?? "repeatBottom",
    };
  };

  // pass 1: LN 体 (depth 0.795; 仅显示判定时跳过)
  if (!judgementOnly) for (const o of objects) {
    if (o.type !== "hold") continue;
    if (o.startTimeMs - t > entryMs || (o.endTimeMs ?? o.startTimeMs) - t < -exitMs) continue;
    if (o.column < 0 || o.column >= n) continue;
    const g = lnGeom(o);
    if (g.hideAll) continue; // 判定完成 → 整条隐藏 (IsHiddenAfterHit)
    const visBottom = Math.min(g.bodyAnchorBottom, g.clipBottom);
    if (visBottom - g.bodyTop <= 0.25) continue;
    if (g.clipped) {
      ctx.save();
      ctx.beginPath();
      ctx.rect(layout.stageLeft, 0, layout.stageWidth, Math.max(0, g.clipBottom));
      ctx.clip();
    }
    const heldMs = bodyHeldByObject.get(o.index ?? -1) ?? 0;
    const frame = !g.bodyAnim
      ? null
      : lazerMode && !g.heldNow
        ? g.bodyAnim.frames[0] // lazer: !isHitting → GotoFrame(0) 每帧 (LegacyBodyPiece L184-185) — 松开弹回帧 0 (D1)
        : animFrameRestart(g.bodyAnim, t - heldMs, t); // stable: 30ms/帧仅按住推进, 松开冻结 (R6.4)
    if (frame) {
      ctx.globalAlpha = g.missAlpha;
      drawBodyStrip(ctx, g.greyAll ? greyFrame(frame) : frame, g.col.x, g.col.width, g.bodyTop, g.bodyAnchorBottom, g.bodyStyle);
      ctx.globalAlpha = 1;
    } else {
      // 缺体贴图回退色块 (UI 决定保留; stable 缺元素=不画); 砖块默认直接走
      // 此路径 (不加载元素) — 砖块配色统一蓝色 (2026-10-28 用户定案)
      const color = brick ? FALLBACK_LIGHT : fallbackColor(g.c, skin);
      ctx.globalAlpha = g.missAlpha;
      ctx.fillStyle = cssColor(color, 0.55);
      ctx.fillRect(g.col.x + g.col.width * 0.15, g.bodyTop, g.col.width * 0.7, visBottom - g.bodyTop);
      ctx.globalAlpha = 1;
    }
    if (g.clipped) ctx.restore();
  }

  // pass 2: LN 尾 (depth 0.7975; 任何版本纵向镜像 — R6.3, 非 UpsideDown 场; 仅显示判定时跳过)
  if (!judgementOnly) for (const o of objects) {
    if (o.type !== "hold") continue;
    if (o.startTimeMs - t > entryMs || (o.endTimeMs ?? o.startTimeMs) - t < -exitMs) continue;
    if (o.column < 0 || o.column >= n) continue;
    const g = lnGeom(o);
    if (!g.tailFrame) continue;
    if (g.hideAll) continue;
    if (lazerMode && g.missAlpha <= 0) {
      if (g.clipped) ctx.restore();
      continue;
    }
    if (g.clipped) {
      ctx.save();
      ctx.beginPath();
      ctx.rect(layout.stageLeft, 0, layout.stageWidth, Math.max(0, g.clipBottom));
      ctx.clip();
    }
    ctx.save();
    if (g.missAlpha < 1) ctx.globalAlpha = g.missAlpha;
    ctx.translate(g.col.x, g.tailY);
    ctx.scale(1, -1);
    const tf = g.greyAll ? greyFrame(g.tailFrame) : g.tailFrame;
    ctx.drawImage(tf.img, 0, 0, g.col.width, g.tailH);
    ctx.globalAlpha = 1;
    ctx.restore();
    if (g.clipped) ctx.restore();
  }

  // pass 3: 音符 + LN 头 (depth 0.8, BottomCentre: 底边中点落在其时相位置; 仅显示判定时跳过)
  if (!judgementOnly) for (const o of objects) {
    if (o.startTimeMs - t > entryMs || (o.endTimeMs ?? o.startTimeMs) - t < -exitMs) continue;
    if (o.column < 0 || o.column >= n) continue;
    const col = layout.columns[o.column];
    const colEl = skin?.columns[o.column];
    if (o.type === "hold") {
      const g = lnGeom(o);
      // V2: 头单独判定 — 成功即隐藏头精灵 (IsHiddenAfterHit), miss 变灰;
      // V1: 整条 LN 共享判定 — hideAll/greyAll 由尾部事件驱动。
      const headHideV2 = judgeVersion === "v2" && g.headSuccess && g.headTime <= t;
      const headGreyV2 = judgeVersion === "v2" && g.headMiss && g.headTime <= t;
      if (g.hideAll || headHideV2) continue;
      if (brick) continue; // 砖块默认: LN 只画体色块, 不画头 (0.1.1 观感)
      if (lazerMode && g.missAlpha <= 0) continue;
      ctx.globalAlpha = g.missAlpha;
      const hf = g.headFrame && (g.greyAll || headGreyV2) ? greyFrame(g.headFrame) : g.headFrame;
      if (hf) {
        ctx.drawImage(hf.img, col.x, g.headY - g.headH, col.width, g.headH);
      } else {
        const color = fallbackColor(o.column, skin);
        ctx.fillStyle = cssColor(color, 0.9);
        ctx.fillRect(col.x, g.headY - g.headH, col.width, g.headH);
      }
      ctx.globalAlpha = 1;
    } else {
      const anim = colEl?.note ?? colEl?.head ?? null;
      const frame = anim ? animFrameEnter(anim, t, enterAt(o.startTimeMs), lazerMode) : null;
      const e = o.index !== undefined ? eidx.headByObject.get(o.index) : undefined;
      const judged = !!e && displayTimeOf(e) <= t;
      if (judged && e.judgement !== "miss") continue; // IsHiddenAfterHit (R5.6)
      const grey = judged && e.judgement === "miss"; // miss 变灰
      // lazer: miss → FadeOut(150, In) (DrawableManiaHitObject L77-79); stable: 灰化坠落
      const tapMissAlpha = grey && lazerMode
        ? Math.max(0, 1 - (t - displayTimeOf(e!)) / 150)
        : 1;
      if (tapMissAlpha <= 0) continue;
      if (frame) {
        const h = frameHeightUnits(frame, col.width, noteBase);
        ctx.globalAlpha = tapMissAlpha;
        ctx.drawImage(grey ? greyFrame(frame).img : frame.img, col.x, yOf(o.startTimeMs) - h, col.width, h);
        ctx.globalAlpha = 1;
      } else {
        const h = Math.max(4, col.width * 0.32);
        const color = brick ? FALLBACK_LIGHT : fallbackColor(o.column, skin);
        ctx.fillStyle = cssColor(color, 0.9);
        // 砖块默认: 圆角砖 r=3 (0.1.1 观感); 其余缺图回退保持方块
        ctx.globalAlpha = tapMissAlpha;
        if (brick) {
          roundRectPath(ctx, col.x + 1, yOf(o.startTimeMs) - h, col.width - 2, h, 3);
          ctx.fill();
        } else {
          ctx.fillRect(col.x, yOf(o.startTimeMs) - h, col.width, h);
        }
        ctx.globalAlpha = 1;
      }
    }
  }

  // ---- 判定 overlay (MRA 特有, re-anchored 到新布局; showInputJudges) ----
  // 虚线导轨 (每列中心, 判定线以上) + 物件判定描边 + 实际输入时刻 tick。
  // V2 尾端判定色带已按用户定案移除 (长条只保留描边); lazerMode 形参透传保留。
  if (showInputJudges) {
    ctx.save();
    ctx.strokeStyle = "rgba(255,255,255,0.16)";
    ctx.lineWidth = 1;
    ctx.setLineDash([1.5, 5]);
    ctx.beginPath();
    for (let c = 0; c < n; ++c) {
      const col = layout.columns[c];
      const x = col.x + col.width / 2;
      ctx.moveTo(x, 0);
      ctx.lineTo(x, hitY);
    }
    ctx.stroke();
    ctx.restore();

    // 物件描边: 颜色 = 该输入的判定色; 严格在列内 (inset)。
    ctx.lineWidth = 1.4;
    ctx.globalAlpha = 0.92;
    for (const o of objects) {
      const e = o.index !== undefined ? eidx.headByObject.get(o.index) : undefined;
      if (!e) continue;
      if (o.column < 0 || o.column >= n) continue;
      const col = layout.columns[o.column];
      const endT = o.endTimeMs ?? o.startTimeMs;
      const isHold = o.type === "hold";
      const te = o.index !== undefined ? eidx.tailByObject.get(o.index) : undefined;
      const top = (isHold ? yOf(endT) : noteTopOf(o, col)) + 1;
      const bottom = yOf(o.startTimeMs) - 1;
      const h = Math.max(2, bottom - top);
      if (judgeVersion === "v2") {
        ctx.strokeStyle = judgeColor(e.judgement);
        ctx.strokeRect(col.x + 1, top, col.width - 2, h);
      } else {
        // V1 (stable): 整条 LN 共享合并判定 — 松开后用整体判定色, 之前用头输入色
        const color = isHold && te?.v1Judgement ? te.v1Judgement : e.judgement;
        ctx.strokeStyle = judgeColor(color);
        ctx.strokeRect(col.x + 1, top, col.width - 2, h);
      }
    }
    ctx.globalAlpha = 1;
    // tick 窗口: y ∈ [-4, hitY+4] 对应的时间区间 (逐段滚动换算), 二分定位后顺序遍历
    {
      const loMs = t - timeForUnits(4);
      const hiMs = t + timeForUnits(hitY + 4);
      let i = lowerBoundBy(eidx.byDisplayTime, loMs, displayTimeOf);
      for (; i < eidx.byDisplayTime.length; ++i) {
        const e = eidx.byDisplayTime[i];
        const dtime = displayTimeOf(e);
        if (dtime > hiMs) break;
        if (e.column < 0 || e.column >= n) continue;
        const col = layout.columns[e.column];
        const y = yOf(dtime);
        // V1 hold tails tick in the combined whole-LN judgement colour
        const tick =
          judgeVersion === "v1" && e.phase === "hold_tail" && e.v1Judgement
            ? e.v1Judgement
            : e.judgement;
        ctx.fillStyle = judgeColor(tick);
        ctx.fillRect(col.x + col.width * 0.18, y - 1.6, col.width * 0.64, 3.2);
      }
    }
  }

  ctx.restore(); // notes 组裁剪结束

  // ---- 按键 (over 组, depth 0.92/0.925) ----
  if (!judgementOnly && !brick && !keysDrawnEarly) drawKeys();

  // ---- StageBottom (§10.4, depth 0.94: keys 之上、判定灯之下): 动画型; 2026-10-28
  // 用户 stable 对照截图仲裁 R7.3 的 [待验证]: 等比缩放到舞台宽 (X 与 Y 同因子,
  // 高 = texH × stageW/texW — 对照图1 量得 bar/key 高度比 0.49 ≈ 41/85.6);
  // BottomCentre 锚舞台底边中点; 默认皮肤无此资产 → 不画。
  if (!judgementOnly && !brick && skin?.stageBottom) {
    const frame = animFrameLoop(skin.stageBottom, t);
    if (frame && frame.w > 0) {
      // 高 = texH 单位 (贴图像素高 1:1 → 44u, 顶边 436 与键视觉底 437 相接并轻微
      // 压过键贴图的透明渐变 — stable 实测: 框顶 y113 vs 键底 y111, 1u 重叠);
      // 宽 = 舞台宽 (X 铺满)。绿色药丸宽高比在两版下均与贴图一致。
      const sbH = frame.h * 1.6;
      ctx.drawImage(frame.img, layout.stageLeft, 480 - sbH, layout.stageWidth, sbH);
    }
  }

  // ---- 判定灯 LightingN/L (R8.3/R8.4, depth 0.98, over 组; 仅显示判定时跳过) ----
  if (!judgementOnly)
  // LightingN: 判定成功 (events 非 miss) 触发 LoopOnce — alpha 1 保持 80ms 后 200ms
  // 淡出; 锚 (列中心, HitPosition) Centre; 缩放 = (LightingNWidth[i]>0?该值:列宽)/30
  // × shrink (<2.5 恒 1)。LightingL: 列按住灯 — 按住 80ms 淡入, 松开 120ms 淡出。
  // additive 近似保留 (D19 标注; stable 混合态 [待验证]); 颜色 White 不叠 ColourLight。
  if (skin && (skin.lightingN || skin.lightingL)) {
    st.scratchFlash.fill(-1);
    {
      const loMs = t - 280; // 80ms 保持 + 200ms 淡出
      let i = lowerBoundBy(eidx.byDisplayTime, loMs, displayTimeOf);
      for (; i < eidx.byDisplayTime.length; ++i) {
        const e = eidx.byDisplayTime[i];
        const dtime = displayTimeOf(e);
        if (dtime > t) break;
        if (e.judgement === "miss") continue;
        if (e.column >= 0 && e.column < n && dtime > st.scratchFlash[e.column]) {
          st.scratchFlash[e.column] = dtime;
        }
      }
    }
    ctx.save();
    ctx.globalCompositeOperation = "lighter";
    for (let c = 0; c < n; ++c) {
      const col = layout.columns[c];
      const cx = col.x + col.width / 2;
      const nAnim = skin.lightingN;
      const trig = st.scratchFlash[c];
      if (nAnim && trig >= 0) {
        const elapsed = t - trig;
        const alpha = elapsed < 80 ? 1 : Math.max(0, 1 - (elapsed - 80) / 200);
        if (alpha > 0.02) {
          const frame = animFrameOnce(nAnim, trig, t);
          if (frame) {
            const s = (skin.lightingNScale[c] ?? 1) * layout.shrink;
            ctx.globalAlpha = alpha;
            ctx.drawImage(frame.img, cx - (frame.w * s) / 2, hitY - (frame.h * s) / 2, frame.w * s, frame.h * s);
          }
        }
      }
      const lAnim = skin.lightingL;
      if (lAnim) {
        let lAlpha = 0;
        if (st.holdActive[c]) lAlpha = Math.min(1, (t - st.holdStartAt[c]) / 80);
        else if (st.holdReleaseAt[c] >= 0 && t - st.holdReleaseAt[c] < 120) {
          lAlpha = 1 - (t - st.holdReleaseAt[c]) / 120;
        }
        if (lAlpha > 0.02) {
          const frame = animFrameRestart(lAnim, st.holdStartAt[c], t);
          if (frame) {
            const s = (skin.lightingLScale[c] ?? 1) * layout.shrink;
            ctx.globalAlpha = lAlpha;
            ctx.drawImage(frame.img, cx - (frame.w * s) / 2, hitY - (frame.h * s) / 2, frame.w * s, frame.h * s);
          }
        }
      }
    }
    ctx.globalAlpha = 1;
    ctx.restore();
  }

  ctx.restore(); // unitScale 全局变换结束 (与 drawPlayfield 开头的 save 配对)

  // ---- 按键绘制 (R4.1/R4.2): 宽拉伸到列宽 (不管原图宽), 高 = texH/1.6, 底边贴 480,
  // 底中心锚; KeyImage{i}D 按下切换, 松开 +80ms 才切回 (Obf005399)。----
  function drawKeys(): void {
    for (let c = 0; c < n; ++c) {
      const col = lay.columns[c];
      const k = skin?.keys[c];
      const downUntil = st.keyReleaseAt[c] >= 0 ? st.keyReleaseAt[c] + 80 : -1;
      const showDown = keyState[c] || (t < downUntil && t >= st.keyReleaseAt[c]);
      const ref = (showDown ? k?.imgDown : k?.img) ?? k?.img ?? null;
      if (ref?.img) {
        ctx.drawImage(ref.img, col.x, 480 - ref.h, col.width, ref.h);
        continue;
      }
      // 缺键图回退块 (UI 决定保留; stable 缺元素=不画)
      const h = 22;
      const ky = 480 - h;
      const bgc: SkinColor = skin?.columns[c]?.bg ?? { r: 20, g: 22, b: 34, a: 1 };
      const light = fallbackColor(c, skin);
      ctx.fillStyle = cssColor(bgc);
      ctx.fillRect(col.x, ky, col.width, h);
      ctx.fillStyle = cssColor(light, keyState[c] ? 0.35 : 0.1);
      ctx.fillRect(col.x, ky, col.width, h);
      ctx.strokeStyle = cssColor(light, keyState[c] ? 0.9 : 0.3);
      ctx.strokeRect(col.x + 0.5, ky - 0.5, col.width - 1, h - 1);
    }
  }

  // 非持有 note 的描边顶缘 (音符高按贴图比例)
  function noteTopOf(o: ManiaObject, col: { x: number; width: number }): number {
    const colEl = skin?.columns[o.column];
    const anim = colEl?.note ?? colEl?.head ?? null;
    const frame = anim ? animFrameEnter(anim, t, enterAt(o.startTimeMs), lazerMode) : null;
    const h = frame ? frameHeightUnits(frame, col.width, noteBase) : Math.max(4, col.width * 0.32);
    return yOf(o.startTimeMs) - h;
  }
}
