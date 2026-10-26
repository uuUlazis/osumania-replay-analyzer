// Mania skin support: skin.ini parsing, column layout and per-column
// element resolution. Layout math mirrors the reference mania skin editor
// (480-unit playfield space, osu 768-space values divided by 1.6).

export interface SkinColor {
  r: number;
  g: number;
  b: number;
  a: number;
}

export type SkinValues = Record<string, string>;

// osu!stable ManiaNoteBodyStyle (osu.Graphics.Skinning, IL 权威):
// Stretch = 0, RepeatTop = 2, RepeatBottom = 3 (v2.5+ 缺省), RepeatTopAndBottom = 4。
export type ManiaBodyStyle = "stretch" | "repeatTop" | "repeatBottom" | "repeatTopBottom";

export function parseManiaBodyStyle(v: string | undefined): ManiaBodyStyle | null {
  if (v === undefined) return null;
  const n = parseFloat(v);
  if (!Number.isFinite(n)) return null;
  if (n === 0) return "stretch";
  if (n === 2) return "repeatTop";
  if (n === 4) return "repeatTopBottom";
  // 3 与未知值都落入 stable switch 的 default 分支 (源矩形偏移 0) = repeatBottom。
  return "repeatBottom";
}

export interface SkinImageRef {
  img: HTMLImageElement | null; // null = missing → color fallback
  w: number;                    // drawn-space width (units)
  h: number;                    // drawn-space height (units)
}

export interface ManiaSkin {
  values: SkinValues;
  version: number;
  keyCount: number;
  columns: {
    x: number;
    width: number;
    bg: SkinColor;
    light: SkinColor;
    note: SkinImageRef | null;
    head: SkinImageRef | null;
    body: SkinImageRef | null;
    tail: SkinImageRef | null; // null = use head
    // stable LN 体绘制方式 (osu!stable ManiaNoteBodyStyle):
    // skin.ini [Mania] NoteBodyStyle{N} / NoteBodyStyle; Version < 2.5 强制
    // stretch, 否则缺省 repeatBottom (SkinMania.Obf022446, IL 权威)。
    bodyStyle: ManiaBodyStyle;
  }[];
  keys: { img: SkinImageRef | null; imgDown: SkinImageRef | null }[];
  stageLeft: number;
  stageRight: number;
  hitPosition: number;
  lightPosition: number;
  noteHeightScale: number;
  columnLineWidth: number;
  stageHint: SkinImageRef | null;
  stageLight: SkinImageRef | null;
  /** true when the light sprite is 1x1 (skins ship a transparent pixel to
   * disable column lighting — osu!stable renders nothing in that case). */
  stageLightBlank: boolean;
  judgementLineColor: SkinColor;
  judgementLineEnabled: boolean;
  keysUnderNotes: boolean;
  stageLeftImg: SkinImageRef | null;
  stageRightImg: SkinImageRef | null;
}

// lazer DrawableManiaRuleset: MAX_TIME_RANGE = 11485; visible time at
// scroll speed s is 11485/s ms (notes fall from playfield top to the
// judgement line within that window).
export const MAX_TIME_RANGE = 11485;

export function visibleTimeMs(scrollSpeed: number): number {
  const s = Math.max(1, Math.min(40, scrollSpeed || 22));
  return MAX_TIME_RANGE / s;
}

const BLANK_GIF =
  "data:image/gif;base64,R0lGODlhAQABAIAAAAAAAP///yH5BAEAAAAALAAAAAABAAEAAAIBRAA7";

function num(values: SkinValues, key: string, def: number): number {
  const v = values[key];
  if (v === undefined) return def;
  const n = parseFloat(v);
  return Number.isFinite(n) ? n : def;
}

function numList(values: SkinValues, key: string): number[] {
  const v = values[key];
  if (!v) return [];
  return v
    .split(",")
    .map((s) => parseFloat(s.trim()))
    .filter((n) => Number.isFinite(n));
}

function pickAt(list: number[], i: number, def: number): number {
  if (list.length === 0) return def;
  return list[Math.min(i, list.length - 1)];
}

function parseColor(v: string | undefined, def: SkinColor, legacy: boolean): SkinColor {
  if (!v) return def;
  const parts = v.split(",").map((s) => parseFloat(s.trim()));
  if (parts.length < 3 || parts.some((n) => !Number.isFinite(n))) return def;
  let a = parts.length >= 4 ? parts[3] / 255 : 1;
  if (legacy) a = a * a; // legacy skins square the alpha channel
  return { r: parts[0], g: parts[1], b: parts[2], a };
}

export function cssColor(c: SkinColor, extraAlpha?: number): string {
  const a = extraAlpha === undefined ? c.a : c.a * extraAlpha;
  return "rgba(" + Math.round(c.r) + "," + Math.round(c.g) + "," + Math.round(c.b) + "," + a.toFixed(3) + ")";
}

// Parses raw skin.ini text. skin.ini may contain MULTIPLE [Mania] blocks,
// one per Keys: value (osu!stable convention) — keep them all and let the
// caller pick the block matching the beatmap's key count.
export interface ManiaBlock {
  keys: number;
  values: SkinValues;
}

export function parseSkinIni(
  text: string,
): { blocks: ManiaBlock[]; version: number } {
  const blocks: ManiaBlock[] = [];
  let section = "";
  let current: ManiaBlock | null = null;
  let version = 2.5;
  for (const raw of text.split(/\r?\n/)) {
    const line = raw.trim();
    if (!line || line.startsWith("//")) continue;
    if (line.startsWith("[")) {
      const end = line.indexOf("]");
      section = end > 0 ? line.slice(1, end).trim().toLowerCase() : "";
      if (section === "mania") {
        current = { keys: 0, values: {} };
        blocks.push(current);
      } else {
        current = null;
      }
      continue;
    }
    const colon = line.indexOf(":");
    const eq = line.indexOf("=");
    let sep = -1;
    if (colon >= 0 && eq >= 0) sep = Math.min(colon, eq);
    else sep = colon >= 0 ? colon : eq;
    if (sep <= 0) continue;
    const key = line.slice(0, sep).trim().toLowerCase();
    const value = line.slice(sep + 1).trim();
    if (!key) continue;
    if (section === "mania" && current) {
      if (key === "keys") {
        const n = parseFloat(value);
        if (Number.isFinite(n)) current.keys = n;
      }
      current.values[key] = value;
    } else if (section === "general" && key === "version") {
      // "latest" and other words fall back to the default below.
      const n = parseFloat(value);
      if (Number.isFinite(n)) version = n;
      else version = value.trim().toLowerCase() === "latest" ? 2.5 : 2.5;
    }
  }
  return { blocks, version };
}

export function selectManiaBlock(
  blocks: ManiaBlock[],
  keyCount: number,
): SkinValues {
  const match = blocks.find((b) => b.keys === keyCount);
  return (match ?? blocks[0])?.values ?? {};
}

async function loadImage(sid: string, base: string): Promise<SkinImageRef | null> {
  const tryName = async (name: string): Promise<HTMLImageElement | null> => {
    try {
      // Names may carry subfolder paths (e.g. "newnote/LNTail1");
      // encode per segment so the slashes survive.
      const enc = name.split("/").map(encodeURIComponent).join("/");
      const res = await fetch("/api/skins/" + sid + "/resource/" + enc);
      if (!res.ok) return null;
      const blob = await res.blob();
      const img = new Image();
      img.src = URL.createObjectURL(blob);
      await img.decode();
      if (!img.naturalWidth || !img.naturalHeight) return null;
      return img;
    } catch {
      return null;
    }
  };
  // @2x preferred when present (drawn at half its natural size).
  let img = await tryName(base + "@2x");
  let scale = 2;
  if (!img) {
    img = await tryName(base);
    scale = 1;
  }
  if (!img) return null;
  const bare = base.toLowerCase().replace(/\.(png|jpg|jpeg)$/, "");
  if (bare === "_blank") return { img: null, w: 1, h: 1 };
  const div = img.src.startsWith("data:") ? 1 : scale;
  return { img, w: img.naturalWidth / div, h: img.naturalHeight / div };
}

// Element-name token for a column inside its stage segment (skin editor
// rules): odd-sized segments have a special center column 'S'; other
// columns pick '1'/'2' by the parity of the distance to the closer end.
function columnToken(
  col: number,
  segStart: number,
  segEnd: number,
  specialStyle: string,
): string {
  const size = segEnd - segStart + 1;
  const special =
    size % 2 === 1 && col === segStart + Math.floor(size / 2);
  if (special) {
    if (size > 5 && specialStyle === "1") return "1";
    if (size > 5 && specialStyle === "2") return "2";
    return "S";
  }
  const dl = col - segStart;
  const dr = segEnd - col;
  return Math.min(dl, dr) % 2 === 0 ? "1" : "2";
}

export async function loadManiaSkin(
  sid: string,
  keyCount: number,
): Promise<ManiaSkin> {
  let values: SkinValues = {};
  let skinVersion = 2.5;
  if (sid) {
    try {
      const res = await fetch("/api/skins/" + sid + "/file/skin.ini");
      if (res.ok) {
        const text = await res.text();
        const parsed = parseSkinIni(text);
        skinVersion = parsed.version;
        values = selectManiaBlock(parsed.blocks, keyCount);
      }
    } catch {
      values = {};
    }
  }
  const legacy = skinVersion < 2.5;

  // column widths / spacing / stage split
  const widths = numList(values, "columnwidth");
  const spacing = num(values, "columnspacing", 0);
  const split = values.splitstages;
  const twoStages = split === "1" || (keyCount > 9 && split !== "0");
  const stageSep = num(values, "stageseparation", 40);
  const boundaries: number[] = [];
  if (twoStages && keyCount >= 2) boundaries.push(Math.ceil(keyCount / 2));

  let total = 0;
  for (let i = 0; i < keyCount; ++i) total += pickAt(widths, i, 30);
  total += spacing * Math.max(0, keyCount - 1);
  total += twoStages ? stageSep : 0;

  const explicitStart = values.columnstart !== undefined;
  let cursor = explicitStart ? num(values, "columnstart", 136) / 1.6 : (480 - total) / 2;
  const columns: ManiaSkin["columns"] = [];
  for (let i = 0; i < keyCount; ++i) {
    const width = pickAt(widths, i, 30);
    const style = legacy
      ? "stretch"
      : (parseManiaBodyStyle(values["notebodystyle" + i])
        ?? parseManiaBodyStyle(values.notebodystyle)
        ?? "repeatBottom");
    columns.push({
      x: cursor,
      width,
      bg: parseColor(values["colour" + (i + 1)], { r: 0, g: 0, b: 0, a: 1 }, legacy),
      light: parseColor(values["colourlight" + (i + 1)], { r: 55, g: 255, b: 255, a: 1 }, legacy),
      note: null,
      head: null,
      body: null,
      tail: null,
      bodyStyle: style,
    });
    cursor += width + spacing;
    if (boundaries.includes(i + 1)) cursor += stageSep;
  }
  const stageLeft = columns[0]?.x ?? 0;
  const stageRight = columns.length
    ? columns[columns.length - 1].x + columns[columns.length - 1].width
    : 480;

  const hitPosition = Math.max(240, Math.min(480, num(values, "hitposition", 402)));
  const lightPosition = num(values, "lightposition", 413);
  const noteHeightScale = num(values, "widthfornoteheightscale", 0) > 0
    ? num(values, "widthfornoteheightscale", 0)
    : Math.min(...columns.map((c) => c.width));
  const columnLineWidth = num(values, "columnlinewidth", 2) * (0.74 / 1.6);

  const specialStyle = values.specialstyle ?? "0";
  const skinKeys: ManiaSkin["keys"] = [];
  for (let i = 0; i < keyCount; ++i) skinKeys.push({ img: null, imgDown: null });

  // segment ranges for token computation
  const segments: [number, number][] = [];
  let segStart = 0;
  for (const b of boundaries) {
    segments.push([segStart, b - 1]);
    segStart = b;
  }
  segments.push([segStart, keyCount - 1]);

  const tokenOf = (col: number) => {
    for (const [s0, s1] of segments)
      if (col >= s0 && col <= s1) return columnToken(col, s0, s1, specialStyle);
    return String(col + 1);
  };

  // per-column element candidate chains (editor rules), then load
  const loads: Promise<void>[] = [];
  for (let i = 0; i < keyCount; ++i) {
    const col = columns[i];
    const plain = String(i + 1);
    const zero = String(i); // osu!stable mania ini indices are 0-based
    const tok = tokenOf(i);
    const noteBases = [
      values["noteimage" + zero],
      values["noteimage" + plain],
      "mania-note" + tok,
      "mania-note" + plain,
    ].filter((s): s is string => !!s && s.length > 0);
    const headBases = [
      values["noteimage" + zero + "h"],
      values["noteimage" + plain + "h"],
      "mania-note" + tok + "h",
      "mania-note" + plain + "h",
    ].filter((s): s is string => !!s && s.length > 0);
    const bodyBases = [
      values["noteimage" + zero + "l"],
      values["noteimage" + plain + "l"],
      "mania-note" + tok + "l",
      "mania-note" + plain + "l",
    ].filter((s): s is string => !!s && s.length > 0);
    const tailBases = [
      values["noteimage" + zero + "t"],
      values["noteimage" + plain + "t"],
      "mania-note" + tok + "t",
      "mania-note" + plain + "t",
    ].filter((s): s is string => !!s && s.length > 0);
    const keyBases = [
      values["keyimage" + zero],
      values["keyimage" + plain],
      "mania-key" + tok,
      "mania-key" + plain,
    ].filter((s): s is string => !!s && s.length > 0);
    const keyDownBases = keyBases.map((s) => s + "d");

    const firstHit = async (bases: string[]) => {
      for (const b of bases) {
        const ref = await loadImage(sid, b);
        if (ref) return ref;
      }
      return null;
    };

    loads.push(
      (async () => {
        col.note = await firstHit(noteBases);
        col.head = await firstHit(headBases);
        col.body = await firstHit(bodyBases);
        col.tail = (await firstHit(tailBases)) ?? null;
        col.head = col.head ?? col.note;
      })(),
    );
    loads.push(
      (async () => {
        const k = await firstHit(keyBases);
        const kd = await firstHit(keyDownBases);
        skinKeys[i] = { img: k, imgDown: kd };
      })(),
    );
  }

  const stageHint = values["stagehint"]
    ? await loadImage(sid, values["stagehint"])
    : await loadImage(sid, "mania-stage-hint");
  const stageLight = await loadImage(sid, "mania-stage-light");
  // osu!stable skins disable column lighting by shipping a 1x1 transparent
  // mania-stage-light — honour that instead of synthesizing light.
  const stageLightBlank =
    !!stageLight?.img && stageLight.img.width <= 1 && stageLight.img.height <= 1;
  const stageLeftImg = values["stageleft"]
    ? await loadImage(sid, values["stageleft"])
    : await loadImage(sid, "mania-stage-left");
  const stageRightImg = values["stageright"]
    ? await loadImage(sid, values["stageright"])
    : await loadImage(sid, "mania-stage-right");

  await Promise.all(loads);

  return {
    values,
    version: skinVersion,
    keyCount,
    columns,
    keys: skinKeys,
    stageLeft,
    stageRight,
    hitPosition,
    lightPosition,
    noteHeightScale,
    columnLineWidth,
    stageHint,
    stageLight,
    stageLightBlank,
    judgementLineColor: parseColor(
      values["colourjudgementline"],
      { r: 255, g: 255, b: 255, a: 1 },
      legacy,
    ),
    judgementLineEnabled: (values.judgementline ?? "1") !== "0",
    keysUnderNotes: (values.keysundernotes ?? "0") === "1",
    stageLeftImg,
    stageRightImg,
  };
}

export { BLANK_GIF };
