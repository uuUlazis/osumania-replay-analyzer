// Mania skin support — osu!stable-faithful rewrite (2026-10-27).
//
// 权威: docs/skin-render-spec-layout.md (A1) + docs/skin-render-spec-elements.md (A2),
// 逆向材料 archive/osu-reverse-engineering/ (IL > 反编译 C# > 默认皮肤资产)。
// 本文件负责: skin.ini 解析 (stable 精确词法) / 列 token / 元素解析与回退链 /
// 资源加载 (/api/skins/{sid}/resource) / 舞台布局几何 (A1 §7) / 滚动模型 (A1 §5)。
//
// 与 stable 的两处**注明为超集扩展**的兼容回退 (其余不宽容):
//  1. 精确键名未命中后再试大小写不敏感 (stable 键名大小写敏感);
//  2. 整行无 ':' 时才允许 '=' 作分隔 (stable 只认 ':')。
//
// lighting 回退特例 (有意偏离 stable, 2026-10-27 用户游戏内实测定案):
// 皮肤缺 lighting 家族 = 完全无灯 (服务端 resource 端点同步跳过内嵌默认);
// 皮肤内 LightingL→LightingN 帧复用照 stable (ManiaColumn.cs L108-113)。
// 既有 UI 决定 (不回改): StageLeft/Right 侧板不画 (maniaRenderer.ts 注释在案)。

export interface SkinColor {
  r: number;
  g: number;
  b: number;
  a: number;
}

export type SkinValues = Record<string, string>;

// osu!stable ManiaNoteBodyStyle (SkinMania.cs Obf022446, IL 权威):
// Stretch=0, RepeatTop=2, RepeatBottom=3 (v2.5+ 缺省), RepeatTopAndBottom=4;
// 未知值走 stable switch default = RepeatBottom (源窗口偏移 0)。
export type ManiaBodyStyle = "stretch" | "repeatTop" | "repeatBottom" | "repeatTopBottom";

export function parseManiaBodyStyle(v: string | undefined): ManiaBodyStyle | null {
  if (v === undefined) return null;
  const t = v.trim();
  if (t.length === 0) return null;
  // 枚举名 (忽略大小写) 或数字 (S4 Obf022402)
  const byName: Record<string, ManiaBodyStyle> = {
    stretch: "stretch",
    repeattop: "repeatTop",
    repeatbottom: "repeatBottom",
    repeattopandbottom: "repeatTopBottom",
  };
  const named = byName[t.toLowerCase()];
  if (named) return named;
  const n = parseFloat(t);
  if (!Number.isFinite(n)) return null;
  if (n === 0) return "stretch";
  if (n === 2) return "repeatTop";
  if (n === 4) return "repeatTopBottom";
  return "repeatBottom";
}

// 单帧: img + 480 空间的自然尺寸 (@2x 已折算半像素, R3.3 — @2x 只影响清晰度,
// 不影响布局数学)。
export interface SkinFrame {
  img: HTMLImageElement;
  w: number; // natural width in 480-space units
  h: number;
}

// 帧动画 (R3.12-R3.14): 帧从 0 连续; frameMs 按消费者规则给。
export interface SkinAnim {
  frames: SkinFrame[];
  frameMs: number;
  // lazer 帧间隔 (LegacySkinExtensions.getFrameLength: AnimationFramerate>0
  // ? 1000/AF : 1000/帧数); 缺省 = frameMs (stable 语义)。
  frameMsLazer?: number;
}

// 兼容别名 (旧 ManiaSkin 消费者类型)。
export type SkinImageRef = SkinFrame;

// ── 滚动模型 (A1 §5) ────────────────────────────────────────────────────
// pxPerMs(t) = 21 × Eff / BL_eff(t); BL_eff = 未继承点 BL × SV(inherited: clamp(-bl,10,10000)/100)。
// 速度源 Eff = replay 帧 y (EffectiveSpeed, DT/HT 因子已烘焙); 无 y 回退 UI speed, 再回退 12。
// 渲染层不乘/除 rate: 播放时钟已按 rate 前进, 墙钟落速自然恒定 (stable §R5.5 定案)。

export interface TimingPointDto {
  timeMs: number;
  beatLength: number;
  uninherited: boolean;
}

export interface ScrollSegment {
  startMs: number;
  beatLength: number; // 有效拍长 (已并 SV)
}

// stable ManiaHitObjectManager.Obf021378 的滚动段构建 (反编译源逐行对照):
//  - 继承点: bl = 前一未继承 BL × clamp(-bl,10,10000)/100 (Obf001036.Obf012297);
//  - 同刻点: 后者生效 (移除前一段);
//  - 有效 BL 与前段相同: 合并 (不新增段);
//  - 首段向前线性外推 (等价 stable 的负对齐首段; 只有 barline 需要对齐, 滚动不需)。
export function buildScrollSegments(tps: TimingPointDto[] | null | undefined): ScrollSegment[] {
  const segs: ScrollSegment[] = [];
  if (tps && tps.length) {
    let prevUninh = tps[0].beatLength; // stable: obf = list[0].beatLength
    for (const tp of tps) {
      let bl: number;
      if (tp.beatLength < 0) {
        const sv = Math.min(10000, Math.max(10, -tp.beatLength)) / 100;
        bl = prevUninh * sv;
      } else {
        bl = tp.beatLength;
        prevUninh = bl;
      }
      if (!(bl > 0)) continue;
      // 同刻: 后者生效 (移除前段再比较)
      if (segs.length > 0 && tp.timeMs <= segs[segs.length - 1].startMs) segs.pop();
      if (segs.length === 0 || segs[segs.length - 1].beatLength !== bl) {
        segs.push({ startMs: tp.timeMs, beatLength: bl });
      }
    }
  }
  if (segs.length === 0) segs.push({ startMs: 0, beatLength: 600 }); // 无 timing 点: BPM100 段 (lazer 11485 的同一基准)
  return segs;
}

// 无 timing 上下文的显示近似 (Transport): 402×600/(21×s) — 按主导 BL=600ms(BPM100) 近似,
// 换 BPM/SV 谱面的真实可视时间逐段变化; 裁剪窗勿直接用此值 (见 scrollWindowMs)。
export const MAX_TIME_RANGE = 11485;

export function visibleTimeMs(scrollSpeed: number): number {
  const s = Math.max(1, Math.min(40, scrollSpeed || 12)); // stable Eff 缺省 12 (D2 修正, 旧值 22)
  // stable 常数 21 是 768px 空间; 480 单位空间的可视时间 = 480×1.6×BL/(21×Eff)
  // 主导段近似 BL=600 (BPM100): = 768×600/(21×s) ≈ 2194/s
  return (768 * 600) / (21 * s);
}

// 保守裁剪窗 (song-ms): 按**最大**有效 BL 的全屏穿越时间 + 余量 —
// 对任意 SV/换段都不得让音符凭空消失/残留 (SV 安全系数 = 直接取 max 而非倍数近似)。
export function scrollWindowMs(effSpeed: number, tps: TimingPointDto[] | null | undefined): number {
  const eff = effSpeed > 0 ? effSpeed : 12;
  let maxBl = 600;
  const segs = buildScrollSegments(tps);
  for (const s of segs) if (s.beatLength > maxBl) maxBl = s.beatLength;
  // ×1.6: 21 常数是 768px 空间, 480 单位空间可视窗 = 768×BL/(21×Eff)
  return (768 * maxBl) / (21 * eff) + 200;
}

// ── skin.ini 词法 (stable Obf001310 + Section.cs + Skin.cs 逐条对照) ──────
//
// R1.2 节名 = '[' 与第一个 ']' 之间原文不 trim, 大小写敏感;
// R1.3 ':' 唯一分隔符 (无 ':' 整行是键、值为空; 超集扩展: 此时才允许 '=');
// R1.4 值 = 第二个冒号前段;
// R1.5 '//' 行首注释; 行中 '//' 之后剥离 (stable 对任意位置的 '//' 剥离, 前导空格一并);
// R1.6 键名 trim 后保留原大小写, 大小写敏感 (超集: 未命中再试不敏感);
// R1.7 同键首个生效; R1.8 值空 → 保持默认。
export interface ManiaBlock {
  keys: number;
  values: SkinValues; // 精确大小写键
}

export interface ParsedSkinIni {
  blocks: ManiaBlock[];
  version: number; // [General] Version: latest→2.7, 数值, 缺省 1.0 (R1.16)
  animationFramerate: number; // [General] AnimationFramerate (>0 覆盖默认 16.667ms 帧时长)
}

export function parseSkinIni(text: string): ParsedSkinIni {
  const blocks: ManiaBlock[] = [];
  let version = 1.0; // stable 缺省 1.0
  let versionSeen = false;
  let animationFramerate = 0;
  let section = "General";
  // [Mania] 节的临时解析器语义 (Skin.cs Obf003683): Keys 行前累计, Keys 行创建块并回放
  // 该节至今全部行, 之后直写最后块; 无 Keys 行的节整体作废 (R1.10-R1.12)。
  let maniaPending: { key: string; value: string }[] = [];
  let current: ManiaBlock | null = null;

  const storeInto = (block: ManiaBlock, key: string, value: string) => {
    if (!(key in block.values)) block.values[key] = value; // R1.7 首个生效
  };

  for (const rawLine of text.split(/\r?\n/)) {
    // stable 词法: Section 先 Trim 整行, 再找 "//" (行首=整行注释; 行中=其后剥离)
    let line = rawLine.trim();
    if (line.length === 0) continue;
    const cut = line.indexOf("//");
    if (cut === 0) continue;
    if (cut > 0) {
      let c = cut;
      while (c > 0 && line[c - 1] === " ") c--;
      line = line.slice(0, c).trim();
      if (line.length === 0) continue;
    }
    if (line.startsWith("[")) {
      const end = line.indexOf("]");
      section = end < 0 ? line.slice(1) : line.slice(1, end); // 原文不 trim (R1.2)
      if (section === "Mania") {
        maniaPending = [];
        current = null;
      } else {
        current = null;
      }
      continue;
    }
    // 键值切分: ':' 唯一分隔符; 值 = 第二个冒号前段 (R1.3/R1.4)
    let key: string;
    let value: string;
    const colon = line.indexOf(":");
    if (colon >= 0) {
      key = line.slice(0, colon).trim();
      const rest = line.slice(colon + 1);
      const colon2 = rest.indexOf(":");
      value = colon2 >= 0 ? rest.slice(0, colon2).trim() : rest.trim();
    } else if (line.indexOf("=") > 0) {
      // 超集扩展 (注明): stable 整行无 ':' 时键=整行、值丢失; 真实皮肤常见 '=' 笔误,
      // 这里放宽为分隔符。仍要求 '=' 不在行首。
      const eq = line.indexOf("=");
      key = line.slice(0, eq).trim();
      value = line.slice(eq + 1).trim();
    } else {
      key = line;
      value = ""; // 整行是键, 值为空 (R1.3)
    }
    if (key.length === 0) continue;

    if (section === "Mania") {
      if (current) {
        storeInto(current, key, value);
        continue;
      }
      maniaPending.push({ key, value });
      if (key === "Keys") {
        // int.TryParse 语义: 整段必须是整数, 失败 = 0; <0 或 >18 → 0 (Skin.cs L101-105)
        const t = value.trim();
        const keys = /^\d+$/.test(t) ? parseInt(t, 10) : -1;
        const k = keys >= 0 && keys <= 18 ? keys : 0;
        current = { keys: k, values: {} };
        blocks.push(current);
        for (const p of maniaPending) storeInto(current, p.key, p.value); // 回放含 Keys 行
        maniaPending = [];
      }
      continue;
    }
    // 非 Mania 节: 只取 General 的 Version / AnimationFramerate (R1.16 / R3.14)
    if (section === "General") {
      if (key === "Version" && !versionSeen) {
        versionSeen = true;
        if (value.trim().toLowerCase() === "latest") version = 2.7;
        else {
          const n = parseFloat(value);
          if (Number.isFinite(n)) version = n;
        }
      } else if (key === "AnimationFramerate") {
        const n = parseFloat(value);
        if (Number.isFinite(n) && n > 0) animationFramerate = n;
      }
    }
  }
  return { blocks, version, animationFramerate };
}

// 按谱面键数选块 (R1.13): Find(Columns == keys); **找不到 = 全新默认块**,
// 不借用其它键数的值 (旧实现回退 blocks[0] 已废弃 — A2 D3 修正)。
export function selectManiaBlock(
  blocks: ManiaBlock[],
  keyCount: number,
): SkinValues | null {
  const match = blocks.find((b) => b.keys === keyCount);
  return match ? match.values : null;
}

// 精确键名查找; 未命中再试大小写不敏感 (超集扩展, 注明; stable 键名大小写敏感)。
function iniValue(values: SkinValues, key: string): string | undefined {
  if (key in values) return values[key];
  const lower = key.toLowerCase();
  for (const k of Object.keys(values)) if (k.toLowerCase() === lower) return values[k];
  return undefined;
}

function iniNum(values: SkinValues, key: string, def: number): number {
  const v = iniValue(values, key);
  if (v === undefined || v.length === 0) return def; // R1.8 值空 → 默认
  const n = parseFloat(v);
  return Number.isFinite(n) ? n : def;
}

function iniBool(values: SkinValues, key: string, def: boolean): boolean {
  const v = iniValue(values, key);
  if (v === undefined || v.length === 0) return def;
  const t = v.toLowerCase();
  if (t === "true") return true;
  if (t === "false") return false;
  const n = parseFloat(t);
  if (Number.isFinite(n)) return n !== 0;
  return def;
}

function iniBoolOpt(values: SkinValues, key: string): boolean | null {
  const v = iniValue(values, key);
  if (v === undefined || v.length === 0) return null;
  const t = v.toLowerCase();
  if (t === "true") return true;
  if (t === "false") return false;
  const n = parseFloat(t);
  return Number.isFinite(n) ? n !== 0 : null;
}

// 列表键 (Obf007442): 按 ini 条数覆盖, 多余丢弃、缺失保持逐项默认 (A2 §2.2;
// 旧渲染器 pickAt "取最后一项" 补齐已废弃 — A1 §R1.3 [待验证] 项按 A2 定案)。
function iniList(values: SkinValues, key: string): number[] {
  const v = iniValue(values, key);
  if (v === undefined || v.length === 0) return [];
  return v
    .split(",")
    .map((s) => parseFloat(s.trim()))
    .filter((m) => Number.isFinite(m));
}

// 颜色 (R1.9): 3-4 个 0-255 分量; [Mania] 内第 4 分量 alpha 原样生效, 无平方
// (旧渲染器的 legacy alpha 平方是 lazer 规则, stable 无此逻辑 — A2 D4 修正)。
function parseColor(v: string | undefined, def: SkinColor): SkinColor {
  if (v === undefined || v.length === 0) return def;
  const parts = v.split(",").map((s) => parseFloat(s.trim()));
  if (parts.length < 3 || parts.some((m) => !Number.isFinite(m))) return def;
  return {
    r: Math.min(255, Math.max(0, parts[0])),
    g: Math.min(255, Math.max(0, parts[1])),
    b: Math.min(255, Math.max(0, parts[2])),
    a: parts.length >= 4 ? Math.min(1, Math.max(0, parts[3] / 255)) : 1,
  };
}

export function cssColor(c: SkinColor, extraAlpha?: number): string {
  const a = extraAlpha === undefined ? c.a : c.a * extraAlpha;
  return "rgba(" + Math.round(c.r) + "," + Math.round(c.g) + "," + Math.round(c.b) + "," + a.toFixed(3) + ")";
}

// ── 列 token (ManiaColumn.Obf005384 / ManiaStage.Obf020597/Obf020574/Obf020600 反编译逐行) ──
// 对每个舞台段独立计算: size=本段列数, idx=段内 0 基列号, style=本段有效 SpecialStyle
// (第二段左右互换 Obf020574)。奇数段中心恒 S (含 1K/3K: 2+(size-5)/2, C# 整除);
// SpecialStyle 只影响偶数 >4 的段 (S 贴首/尾缘)。
export function columnToken(
  size: number,
  idx: number,
  style: number, // 0 None / 1 Left / 2 Right
): string {
  const capable = size > 4 && size % 2 === 0; // Obf020597
  const hasStyle = capable && style !== 0;
  let specialIdx: number;
  if (size % 2 === 0) {
    specialIdx = hasStyle ? (style === 1 ? 0 : size - 1) : -1;
  } else {
    specialIdx = 2 + Math.trunc((size - 5) / 2); // C# 整除: 1K→0, 3K→1, 5K→2, 7K→3, 9K→4
  }
  if (idx === specialIdx) return "S";
  if (hasStyle) {
    if ((idx % 2 === 1 && style === 2) || (idx % 2 === 0 && style === 1)) return "2";
    return "1";
  }
  const half = Math.floor(size / 2);
  const oddTwo =
    (size % 2 === 1 && idx !== half && idx % 2 === 1) ||
    (size % 2 === 0 && ((idx < half && idx % 2 === 1) || (idx >= half && idx % 2 === 0)));
  return oddTwo ? "2" : "1";
}

// ── ManiaSkin ──────────────────────────────────────────────────────────
export interface ManiaColumn {
  token: string;
  bg: SkinColor; // Colour{i+1} (1 基), 默认 Black — 列底色块
  light: SkinColor; // ColourLight{i+1} (1 基), 默认 White — 列灯 tint (R8.1/R8.5)
  note: SkinAnim | null; // NoteImage{i} → mania-note{token}
  head: SkinAnim | null; // NoteImage{i}H → mania-note{token}H
  body: SkinAnim | null; // NoteImage{i}L → mania-note{token}L (30ms/帧, 按住推进)
  tail: SkinAnim | null; // NoteImage{i}T → mania-note{token}T; 缺→头贴图 (R6.3)
  bodyStyle: ManiaBodyStyle;
}

// 布局参数 (loadManiaSkin 解析并 clamp, computeLayout 消费)
export interface SkinLayoutParams {
  widths: number[]; // ColumnWidth, clamp [5,100], n 项
  spacings: number[]; // ColumnSpacing, clamp ≥ −w[i+1], n−1 项
  sep: number; // StageSeparation, ≥5
  start: number; // ColumnStart (默认 136)
  right: number; // ColumnRight (默认 19)
  wfnhs: number; // WidthForNoteHeightScale
}

export interface ManiaSkin {
  values: SkinValues; // 命中块的原始 ini 值 (精确大小写); 无 ini = {}
  version: number;
  keyCount: number;
  columns: ManiaColumn[];
  keys: { img: SkinFrame | null; imgDown: SkinFrame | null }[];
  layout: SkinLayoutParams;
  hitPosition: number;
  lightPosition: number;
  comboPosition: number;
  scorePosition: number;
  columnLineWidth: number[]; // n+1 项, 已 (0,2)→2 clamp
  columnLineColor: SkinColor;
  judgementLineColor: SkinColor;
  judgementLineEnabled: boolean;
  barlineHeight: number;
  keysUnderNotes: boolean;
  upsideDown: boolean; // 仅解析; 渲染按非 UpsideDown 场 (A1 D15 低影响, 见回报)
  splitStages: boolean;
  specialStyle: number; // 0 None / 1 Left / 2 Right
  stageHint: SkinFrame | null;
  stageLight: SkinAnim | null; // mania-stage-light, 帧率 1000/LightFramePerSecond
  stageLightFrameMs: number;
  stageLightBlank: boolean; // 1×1 空贴图 = 禁灯 (stable 惯例, R8.2)
  stageBottom: SkinAnim | null;
  stageLeftImg: SkinFrame | null; // 载入但不渲染 (用户定案)
  stageRightImg: SkinFrame | null;
  lightingN: SkinAnim | null; // LightingN → lightingN (判定成功触发 LoopOnce); 缺=无灯
  lightingL: SkinAnim | null; // LightingL → lightingL; 皮肤内缺→复用 N 帧 (R8.4)
  lightingNScale: number[]; // (LightingNWidth[i]>0?该值:ColumnWidth[i])/30; <2.5 恒 1
  lightingLScale: number[];
  animationFramerate: number;
  // 全局 NoteBodyStyle (lazer 只读全局; null = 未配置)。stable 用逐列 bodyStyle。
  bodyStyleGlobal: ManiaBodyStyle | null;
}

const DEFAULT_FRAME_MS = 16.667; // R3.14 默认帧时长
const BODY_FRAME_MS = 30; // LN 体 30ms/帧, 仅按住推进 (R6.4)

// ── 资源加载 ───────────────────────────────────────────────────────────
// 每个名字经 /api/skins/{sid}/resource/{name}; 服务端链 = 谱面目录 → 自定义皮肤 →
// 内嵌默认 (X-MRA-Skin-Source: default 头)。@2x 是**同店弱候选优先**: 自定义 @2x >
// 自定义/谱面 1x > 默认 @2x (stable store 4→2→1 + HiRes; R3.1/R3.3)。
async function fetchImage(
  sid: string,
  name: string,
): Promise<{ img: HTMLImageElement; fromDefault: boolean } | null> {
  try {
    // ini 值可带子目录与反斜杠 (R3.4, stable 惯用 "Arrownote\left") — 归一成 / 再逐段编码
    const enc = name.replace(/\\/g, "/").split("/").map(encodeURIComponent).join("/");
    const res = await fetch("/api/skins/" + sid + "/resource/" + enc);
    if (!res.ok) return null;
    const fromDefault = res.headers.get("x-mra-skin-source") === "default";
    const blob = await res.blob();
    const img = new Image();
    img.src = URL.createObjectURL(blob);
    await img.decode();
    if (!img.naturalWidth || !img.naturalHeight) return null;
    return { img, fromDefault };
  } catch {
    return null;
  }
}

function toFrame(img: HTMLImageElement, at2x: boolean): SkinFrame {
  const div = at2x ? 2 * 1.6 : 1.6; // 皮肤 px → 480 空间单位
  return { img, w: img.naturalWidth / div, h: img.naturalHeight / div };
}

// 单名解析: base@2x (自定义) → base (自定义) → 默认弱候选 (allowDefault 时)。
// allowDefault=false: 只接受自定义命中 (动画帧链用 — 防止端点的内嵌默认回退
// 劫持帧链, 2026-10-28 修复: 自定义单帧 mania-note1L.png 被默认皮肤帧链顶掉)。
async function resolveFrame(sid: string, base: string, allowDefault = true): Promise<SkinFrame | null> {
  const at2x = await fetchImage(sid, base + "@2x");
  if (at2x && !at2x.fromDefault) return toFrame(at2x.img, true);
  const oneX = await fetchImage(sid, base);
  if (oneX && !(oneX.fromDefault && !allowDefault)) return toFrame(oneX.img, false);
  if (allowDefault) {
    if (at2x) return toFrame(at2x.img, true);
    if (oneX) return toFrame(oneX.img, false);
  }
  return null;
}

// 动画帧收集 (R3.12-R3.13): 元素甄别按四级优先, 逐级回退 (2026-10-28 甄别完善):
//   1. 自定义帧链  <base>-0,-1,… (allowDefault=false — 端点的内嵌默认回退会被视为
//      断档, 防止默认皮肤的帧动画劫持自定义贴图, 见 resolveFrame 注释)
//   2. 自定义单帧  <base>       (allowDefault=false)
//   3. 默认皮肤帧链 <base>-0,…   (自定义整组缺失时 — stable 的元素缺省回退到默认
//      皮肤的同一元素, 默认皮肤的动画帧同样生效)
//   4. 默认皮肤单帧 <base>
// 命中 1/2 即停止 (自定义任何形态都优先于默认皮肤任何形态); 3/4 仅在自定义完全
// 缺失时生效。缺省帧时长由消费者覆盖 (R3.14)。
async function resolveAnimChain(sid: string, base: string, allowDefault: boolean): Promise<SkinFrame[] | null> {
  const f0 = await resolveFrame(sid, base + "-0", allowDefault);
  if (f0) {
    const frames: SkinFrame[] = [f0];
    for (let k = 1; ; ++k) {
      const fk = await resolveFrame(sid, base + "-" + k, allowDefault);
      if (!fk) break;
      frames.push(fk);
      if (frames.length >= 64) break; // 上限保护 (异常皮肤)
    }
    return frames;
  }
  const single = await resolveFrame(sid, base, allowDefault);
  return single ? [single] : null;
}
async function resolveAnim(sid: string, base: string): Promise<SkinFrame[] | null> {
  return (await resolveAnimChain(sid, base, false))
    ?? (await resolveAnimChain(sid, base, true));
}

// ini 名优先 (可为动画), 未给/未命中 → 默认槽位 (§3.2 每元素调用点写死)。
async function loadAnim(
  sid: string,
  iniName: string | undefined,
  slot: string,
): Promise<SkinAnim | null> {
  const base = iniName && iniName.length > 0 ? iniName : slot;
  if (base !== slot) {
    const frames = await resolveAnim(sid, base);
    if (frames) return { frames, frameMs: DEFAULT_FRAME_MS };
  }
  const frames = await resolveAnim(sid, slot);
  return frames ? { frames, frameMs: DEFAULT_FRAME_MS } : null;
}

async function loadSingle(
  sid: string,
  iniName: string | undefined,
  slot: string,
): Promise<SkinFrame | null> {
  if (iniName && iniName.length > 0) {
    const f = await resolveFrame(sid, iniName);
    if (f) return f;
  }
  return resolveFrame(sid, slot);
}

function isBlank1x1(f: SkinFrame | null | undefined): boolean {
  return !!f && f.img.naturalWidth <= 1 && f.img.naturalHeight <= 1;
}

// ── loadManiaSkin ──────────────────────────────────────────────────────
// sid 空/拉不到 skin.ini → **stable 默认几何**: 默认布局 + 全默认块。元素请求由
// 服务端内嵌默认皮肤应答 — 该池自 2026-10-28 起只作自定义皮肤缺省填充; 无自定义
// 皮肤会话 (skipElements=true, 砖块默认显示) 不发起任何元素请求, 显示走渲染器
// 的砖块回退路径 (0.1.1 观感)。
export async function loadManiaSkin(
  sid: string,
  keyCount: number,
  coop = false,
  skipElements = false,
): Promise<ManiaSkin> {
  const n = Math.max(1, Math.min(18, keyCount));
  let values: SkinValues = {};
  let version = 2.7; // 无 skin.ini → Default 语义强制 2.7 (R1.16/R3.5)
  let animationFramerate = 0;
  if (sid) {
    try {
      const res = await fetch("/api/skins/" + sid + "/file/skin.ini");
      if (res.ok) {
        const text = await res.text();
        const parsed = parseSkinIni(text);
        version = parsed.version;
        animationFramerate = parsed.animationFramerate;
        const block = selectManiaBlock(parsed.blocks, n);
        if (block) values = block;
      }
    } catch {
      values = {};
      version = 2.7;
      animationFramerate = 0;
    }
  }
  const legacy = version < 2.5; // <2.5: 强制 Stretch / 忽略 Note 翻转 / 灯缩放恒 1 (R1.18)

  // [Mania] 标量与列表 (R1.1 默认值 + R1.2 clamp + A2 §2.2 列表语义)
  const widthsRaw = iniList(values, "ColumnWidth");
  const widths: number[] = [];
  for (let i = 0; i < n; ++i) {
    const w = i < widthsRaw.length ? widthsRaw[i] : 30;
    widths.push(Math.min(100, Math.max(5, w)));
  }
  const spacingRaw = iniList(values, "ColumnSpacing");
  const spacings: number[] = [];
  for (let i = 0; i < n - 1; ++i) {
    const s = i < spacingRaw.length ? spacingRaw[i] : 0;
    spacings.push(Math.max(s, -widths[i + 1])); // ≥ −w[i+1] (允许列重叠)
  }
  const lineRaw = iniList(values, "ColumnLineWidth");
  const lineWidths: number[] = [];
  for (let i = 0; i < n + 1; ++i) {
    const w = i < lineRaw.length ? lineRaw[i] : 2;
    lineWidths.push(w > 0 && w < 2 ? 2 : w); // 开区间 (0,2) → 2; ≤0 = 不画该缘
  }
  const layoutParams: SkinLayoutParams = {
    widths,
    spacings,
    sep: Math.max(iniNum(values, "StageSeparation", 40), 5),
    start: iniNum(values, "ColumnStart", 136),
    right: iniNum(values, "ColumnRight", 19),
    wfnhs: iniNum(values, "WidthForNoteHeightScale", 0),
  };
  const hitPosition = Math.round(Math.max(240, Math.min(480, iniNum(values, "HitPosition", 402))));
  const lightPosition = Math.round(iniNum(values, "LightPosition", 413));
  const comboPosition = Math.round(iniNum(values, "ComboPosition", 111));
  const scorePosition = Math.round(iniNum(values, "ScorePosition", 325));
  let lightFps = Math.round(iniNum(values, "LightFramePerSecond", 60));
  if (lightFps <= 0) lightFps = 24;
  const specialStyleRaw = (iniValue(values, "SpecialStyle") ?? "").trim().toLowerCase();
  let specialStyle = 0;
  if (specialStyleRaw === "left" || specialStyleRaw === "1") specialStyle = 1;
  else if (specialStyleRaw === "right" || specialStyleRaw === "2") specialStyle = 2;
  const splitIni = iniBoolOpt(values, "SplitStages");
  // R1.14/R2.4: ini 显式值优先; 否则 keys ≥ 10 或 KeyCoop 强制拆分
  const splitStages = splitIni ?? (n >= 10 || coop);
  const keysUnderNotes = iniBool(values, "KeysUnderNotes", false);
  const upsideDown = iniBool(values, "UpsideDown", false);
  const judgementLineEnabled = iniBool(values, "JudgementLine", true);
  const barlineHeight = iniNum(values, "BarlineHeight", 1.2);

  // 双段切分 + 段列数 (Obf020601/Obf020602); 逐列 token (段内独立; 第二段左右互换)
  const segSizes: number[] = [];
  if (splitStages && n > 1) {
    const first = Math.ceil(n / 2);
    segSizes.push(first, n - first);
  } else {
    segSizes.push(n);
  }
  const tokens: string[] = [];
  {
    let idx = 0;
    for (let s = 0; s < segSizes.length; ++s) {
      const size = segSizes[s];
      const style = s % 2 === 0 ? specialStyle : specialStyle === 1 ? 2 : specialStyle === 2 ? 1 : 0;
      for (let j = 0; j < size; ++j, ++idx) tokens.push(columnToken(size, j, style));
    }
  }

  // 逐列颜色 (1 基 Colour{i+1} / ColourLight{i+1}) 与 NoteBodyStyle (Obf022446)
  const columns: ManiaColumn[] = [];
  for (let i = 0; i < n; ++i) {
    const style = legacy
      ? "stretch"
      : parseManiaBodyStyle(iniValue(values, "NoteBodyStyle" + i)) ??
        parseManiaBodyStyle(iniValue(values, "NoteBodyStyle")) ??
        "repeatBottom";
    columns.push({
      token: tokens[i],
      bg: parseColor(iniValue(values, "Colour" + (i + 1)), { r: 0, g: 0, b: 0, a: 1 }),
      light: parseColor(iniValue(values, "ColourLight" + (i + 1)), { r: 255, g: 255, b: 255, a: 1 }),
      note: null,
      head: null,
      body: null,
      tail: null,
      bodyStyle: style,
    });
  }

  // 全局 NoteBodyStyle (lazer 只消费全局键 — LegacySkin.cs L202-210 无逐列;
  // stable 用逐列 bodyStyle)。null = 未配置 (<2.5 时 lazer 视为 Repeat, D4)。
  const bodyStyleGlobal = parseManiaBodyStyle(iniValue(values, "NoteBodyStyle"));
  // LightingN/LWidth 缩放 (Obf022450): (w[i]>0?w:ColumnWidth[i])/30; <2.5 恒 1
  const lnw = iniList(values, "LightingNWidth");
  const llw = iniList(values, "LightingLWidth");
  const lightingNScale: number[] = [];
  const lightingLScale: number[] = [];
  for (let i = 0; i < n; ++i) {
    const nw = i < lnw.length ? lnw[i] : 0;
    const lw = i < llw.length ? llw[i] : 0;
    lightingNScale.push(legacy ? 1 : (nw > 0 ? nw : widths[i]) / 30);
    lightingLScale.push(legacy ? 1 : (lw > 0 ? lw : widths[i]) / 30);
  }

  // ── 元素加载 (ini 名 → 默认槽位; A2 §3.2 调用点写死) ──
  // R3.14: 默认帧时长 16.667ms; LN 体固定 30ms/帧 (仅按住推进, R6.4)。
  // 2026-10-28 IL 复核 (Obf006448 调用方核查): mania 的 note/头/尾精灵从不调用
  // SetFrameDelay(AnimationFramerate) — [General] AnimationFramerate 对 mania
  // 完全不生效, 恒 16.667ms; 体是硬编码 30ms (ManiaLongNote ctor ldc.r8 30.0)。
  const noteFrameMs = DEFAULT_FRAME_MS;
  // lazer 帧间隔 (LegacySkinExtensions.getFrameLength L227-242): AnimationFramerate>0
  // ? 1000/AF : 1000/帧数 (每秒一整循环) — stable 恒 16.667ms, 两者在此分叉 (D2)。
  const lazerFrameMs = (frames: number): number =>
    animationFramerate > 0 ? 1000 / animationFramerate : 1000 / Math.max(1, frames);
  const skinKeys: { img: SkinFrame | null; imgDown: SkinFrame | null }[] = [];
  for (let i = 0; i < n; ++i) skinKeys.push({ img: null, imgDown: null });
  const loads: Promise<void>[] = [];
  if (!skipElements) for (let i = 0; i < n; ++i) {
    const col = columns[i];
    const tok = col.token;
    loads.push(
      (async () => {
        col.note = await loadAnim(sid, iniValue(values, "NoteImage" + i), "mania-note" + tok);
        if (col.note) {
          col.note.frameMs = noteFrameMs;
          col.note.frameMsLazer = lazerFrameMs(col.note.frames.length);
        }
        col.head = await loadAnim(sid, iniValue(values, "NoteImage" + i + "H"), "mania-note" + tok + "H");
        if (col.head) {
          col.head.frameMs = noteFrameMs;
          col.head.frameMsLazer = lazerFrameMs(col.head.frames.length);
        }
        // R6.4: 体 30ms/帧且仅按住推进 (播放标志由 renderer 控制); 帧间隔两边同为
        // 硬编码 30ms (stable ManiaLongNote ctor ldc.r8 30.0 / lazer frameLength:30)
        col.body = await loadAnim(sid, iniValue(values, "NoteImage" + i + "L"), "mania-note" + tok + "L");
        if (col.body) {
          col.body.frameMs = BODY_FRAME_MS;
          col.body.frameMsLazer = BODY_FRAME_MS;
        }
        // R6.3: 尾贴图缺 → 直接用头贴图 (S9 ?? HeadSprite texture); 头也缺 → null
        const tail = await loadAnim(sid, iniValue(values, "NoteImage" + i + "T"), "mania-note" + tok + "T");
        if (tail) {
          tail.frameMs = noteFrameMs;
          tail.frameMsLazer = lazerFrameMs(tail.frames.length);
        }
        col.tail = tail ?? col.head;
      })(),
    );
    loads.push(
      (async () => {
        // stable: KeyImage{i} / KeyImage{i}D 是两个独立 ini 键 (ManiaColumn.cs L81/L87)
        const img = await loadSingle(sid, iniValue(values, "KeyImage" + i), "mania-key" + tok);
        const imgDown = await loadSingle(sid, iniValue(values, "KeyImage" + i + "D"), "mania-key" + tok + "D");
        skinKeys[i] = { img, imgDown };
      })(),
    );
  }

  const stageLightFrameMs = 1000 / lightFps;
  let stageHint: SkinFrame | null = null;
  let stageLightAnim: SkinAnim | null = null;
  let lightingNAnim: SkinAnim | null = null;
  let lightingLAnim: SkinAnim | null = null;
  let stageBottomAnim: SkinAnim | null = null;
  let stageLeftImg: SkinFrame | null = null;
  let stageRightImg: SkinFrame | null = null;
  if (!skipElements) {
    stageHint = await loadSingle(sid, iniValue(values, "StageHint"), "mania-stage-hint");
    stageLightAnim = await loadAnim(sid, iniValue(values, "StageLight"), "mania-stage-light");
    if (stageLightAnim) stageLightAnim.frameMs = stageLightFrameMs;
    // stable: LightingL 缺 → 复用 LightingN 帧数组 (ManiaColumn.cs L108-113)
    lightingNAnim = await loadAnim(sid, iniValue(values, "LightingN"), "lightingN");
    lightingLAnim = await loadAnim(sid, iniValue(values, "LightingL"), "lightingL");
    if (!lightingLAnim) lightingLAnim = lightingNAnim;
    // lighting 回退特例 (用户定案): lighting 家族不做默认皮肤回退 — 服务端 resource
    // 端点对 lighting* 跳过内嵌默认; 拉不到即 null = 无灯。1×1 空贴图同义 (R8.2)。
    if (lightingNAnim && isBlank1x1(lightingNAnim.frames[0])) {
      // 空贴图禁灯: N 置空, L 若复用 N 也随之置空
      if (lightingLAnim === lightingNAnim) lightingLAnim = null;
      lightingNAnim = null;
    }
    // LightingN/L 帧时长: max(170/帧数, 16.667) (R8.3/R8.4)
    if (lightingNAnim) lightingNAnim.frameMs = Math.max(170 / lightingNAnim.frames.length, DEFAULT_FRAME_MS);
    if (lightingLAnim && lightingLAnim !== lightingNAnim) {
      lightingLAnim.frameMs = Math.max(170 / lightingLAnim.frames.length, DEFAULT_FRAME_MS);
    }
    stageBottomAnim = await loadAnim(sid, iniValue(values, "StageBottom"), "mania-stage-bottom");
    if (stageBottomAnim) stageBottomAnim.frameMs = DEFAULT_FRAME_MS; // R3.14: StageBottom 固定 16.667ms
    stageLeftImg = await loadSingle(sid, iniValue(values, "StageLeft"), "mania-stage-left");
    stageRightImg = await loadSingle(sid, iniValue(values, "StageRight"), "mania-stage-right");

    await Promise.all(loads);
  }

  return {
    values,
    version,
    keyCount: n,
    columns,
    keys: skinKeys,
    layout: layoutParams,
    hitPosition,
    lightPosition,
    comboPosition,
    scorePosition,
    columnLineWidth: lineWidths,
    columnLineColor: parseColor(iniValue(values, "ColourColumnLine"), { r: 255, g: 255, b: 255, a: 1 }),
    judgementLineColor: parseColor(iniValue(values, "ColourJudgementLine"), { r: 255, g: 255, b: 255, a: 1 }),
    judgementLineEnabled,
    barlineHeight,
    keysUnderNotes,
    upsideDown,
    splitStages,
    specialStyle,
    stageHint,
    stageLight: stageLightAnim,
    stageLightFrameMs,
    stageLightBlank: isBlank1x1(stageLightAnim ? stageLightAnim.frames[0] : null),
    stageBottom: stageBottomAnim,
    stageLeftImg,
    stageRightImg,
    lightingN: lightingNAnim,
    lightingL: lightingLAnim,
    lightingNScale,
    lightingLScale,
    animationFramerate,
    bodyStyleGlobal,
  };
}

// ── 舞台布局几何 (A1 §7 伪代码逐行; ManiaStage.cs 构造 L344-413 反编译对照) ──
//
// 480 空间: 高恒 480, 宽 = FixedWidth = ceil(canvasW / (canvasH/480)); mania 舞台用
// FixedWidth 全宽, 无 letterbox、不再固定 480 居中 (旧渲染器的 (480−total)/2 居中
// 与画布强制 480 宽已废弃 — A1 D3 修正)。
export interface SkinLayoutColumn {
  x: number;
  width: number;
}

export interface SkinLayout {
  columns: SkinLayoutColumn[];
  stageLeft: number;
  stageRight: number;
  stageWidth: number;
  hitY: number;
  lightY: number;
  shrink: number; // (0,1], 1 = 不压缩 (R2.2 fit 收缩)
  noteBase: number; // 音符高度基准宽: WFNHS>0 ? WFNHS : min(ColumnWidth)×shrink (R5.2/Obf020596)
  split: boolean;
}

function defaultLayoutParams(n: number): SkinLayoutParams {
  return {
    widths: Array.from({ length: n }, () => 30),
    spacings: Array.from({ length: Math.max(0, n - 1) }, () => 0),
    sep: 40,
    start: 136,
    right: 19,
    wfnhs: 0,
  };
}

// 布局缓存 (canvas 尺寸/皮肤/coop/键数不变则零开销复用; 热路径禁止每帧分配)
const layoutCache: {
  skin: ManiaSkin | null;
  w: number;
  h: number;
  coop: boolean;
  n: number;
  layout: SkinLayout | null;
} = { skin: null, w: 0, h: 0, coop: false, n: 0, layout: null };

export function computeLayout(
  skin: ManiaSkin | null,
  keyCount: number,
  canvasW: number,
  canvasH: number,
  coop: boolean,
): SkinLayout | null {
  const n = Math.max(1, Math.min(18, keyCount));
  if (!(canvasW > 0) || !(canvasH > 0)) return null;
  const cache = layoutCache;
  if (
    cache.layout && cache.skin === skin && cache.w === canvasW &&
    cache.h === canvasH && cache.coop === coop && cache.n === n
  ) {
    return cache.layout;
  }
  const p = skin ? skin.layout : defaultLayoutParams(n);
  const scalePx = canvasH / 480;
  const fixedW = Math.ceil(canvasW / scalePx); // ScreenManager.Obf007649
  // R2.1 列起点保护 (IL_0729-0769)
  const left = Math.min(p.start, fixedW - p.right);
  const rightM = Math.min(p.right, fixedW - left);
  // R2.2 总宽与 shrink (ManiaStage.cs L349-357: total 含全部 n−1 个 spacing + SEP)
  let total = 0;
  for (let j = 0; j < n; ++j) {
    total += p.widths[j] + (j > 0 ? p.spacings[j - 1] : 0);
  }
  const segSizes: number[] = [];
  if (skin ? skin.splitStages : n >= 10 || coop) {
    if (n > 1) {
      const first = Math.ceil(n / 2);
      segSizes.push(first, n - first);
    } else {
      segSizes.push(n);
    }
  } else {
    segSizes.push(n);
  }
  const hasSecond = segSizes.length > 1 && segSizes[1] > 0;
  if (hasSecond) total += p.sep;
  const overflow = Math.max(0, left + total + rightM - fixedW);
  const shrink = total > 0 ? (total - overflow) / total : 1;
  // R2.3/R2.4 逐列累计 (每段独立 stageLeft; 次段起点 = 上段末 + SEP×shrink)
  const columns: SkinLayoutColumn[] = [];
  let x = left;
  for (let s = 0; s < segSizes.length; ++s) {
    const segLeft = x;
    let accum = 0;
    for (let j = 0; j < segSizes[s]; ++j) {
      const i = columns.length;
      const sp = j > 0 ? p.spacings[i - 1] * shrink : 0;
      const w = p.widths[i] * shrink;
      columns.push({ x: segLeft + accum + sp, width: w });
      accum += sp + w;
    }
    x = segLeft + accum + p.sep * shrink;
  }
  let minW = 100;
  for (const w of p.widths) if (w < minW) minW = w;
  const rightEdge = columns.length
    ? columns[columns.length - 1].x + columns[columns.length - 1].width
    : left;
  // 2026-10-28: 列左右边界吸附到设备整像素 — 消除相邻列浮点坐标的抗锯齿缝
  // (note 间分隔线的根因)。吸附只是绘制精度修正 (每列宽度误差 ≤1 设备像素),
  // 布局数值本身不变; 相邻列共享同一吸附边界 → 严格无缝拼接。
  const snapU = (u: number): number => Math.round(u * scalePx) / scalePx;
  const snapped = columns.map((c) => {
    const xL = snapU(c.x);
    const xR = snapU(c.x + c.width);
    return { x: xL, width: Math.max(xR - xL, 1 / scalePx) };
  });
  const leftS = snapU(left);
  const rightS = snapU(rightEdge);
  const layout: SkinLayout = {
    columns: snapped,
    stageLeft: leftS,
    stageRight: rightS,
    stageWidth: rightS - leftS,
    hitY: skin ? skin.hitPosition : 402,
    lightY: skin ? skin.lightPosition : 413,
    shrink,
    noteBase: p.wfnhs > 0 ? p.wfnhs : minW * shrink, // Obf020596
    split: hasSecond,
  };
  cache.skin = skin;
  cache.w = canvasW;
  cache.h = canvasH;
  cache.coop = coop;
  cache.n = n;
  cache.layout = layout;
  return layout;
}
