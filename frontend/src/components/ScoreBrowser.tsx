import {
  useCallback,
  useEffect,
  useMemo,
  useRef,
  useState,
  type MouseEvent as ReactMouseEvent,
} from "react";
import { api, ApiError } from "../api/client";
import type {
  LazerScoreRow,
  ScoreBrowseRow,
  ScoreSourceMode,
} from "../models/types";
import { lazerModText, modsText } from "../utils/modsText";
import { KEY_MOD_NAMES, MOD_NAMES } from "../models/types";

interface Props {
  open: boolean;
  onClose: () => void;
  onPlay: (rowKey: string) => void;
  loading: boolean;
  // 数据源模式: stable = scores.db browse; lazer = /api/lazer/browse
  mode: ScoreSourceMode;
}

// 两套成绩行 (stable scores.db / lazer browse) 的共用显示模型:
// 共有字段直接取用, 差异字段经 isLazerRow 收窄。
type AnyRow = ScoreBrowseRow | LazerScoreRow;

// 表头点击排序键 (谱面md5 为哈希串, 不参与排序)
type SortKey =
  | "date" | "keys" | "title" | "artist" | "version"
  | "mods" | "acc" | "sr" | "sunnySr" | "pp" | "bpm" | "length"
  | "score" | "player" | "replay";

// 表头定义, 列序同 GRID_TEMPLATE 注释; key=null 表示不可排序。
// 居中列 (cls): 键数/ACC/分数 = num, Mods/有回放/谱面md5 = ctr; 其余左对齐。
const HEADERS: { key: SortKey | null; label: string; cls: string }[] = [
  { key: "date", label: "日期", cls: "" },
  { key: "keys", label: "键数", cls: "num" },
  { key: "title", label: "歌曲名", cls: "" },
  { key: "artist", label: "曲师", cls: "" },
  { key: "version", label: "难度名", cls: "" },
  { key: "mods", label: "Mods", cls: "ctr" },
  { key: "acc", label: "ACC", cls: "num" },
  { key: "score", label: "分数", cls: "num" },
  { key: "sr", label: "SR", cls: "num" },
  { key: "sunnySr", label: "Sunny SR", cls: "num" },
  { key: "pp", label: "pp", cls: "num" },
  { key: "player", label: "玩家", cls: "" },
  { key: "bpm", label: "BPM", cls: "num" },
  { key: "length", label: "长度", cls: "num" },
  { key: "replay", label: "有回放", cls: "ctr" },
  { key: null, label: "谱面md5", cls: "ctr" },
];

const MAX_RENDER = 500;

// mania 绑死
const MODE_MANIA = 3;

// 列宽模板 (inline 覆盖 global.css 的 .score-tr 定义), 列序:
// 日期 | 键数 | 歌曲名 | 曲师 | 难度名 | Mods | ACC | 分数 | SR | Sunny SR | pp | 玩家 | BPM | 长度 | 有回放 | 谱面md5
const GRID_TEMPLATE =
  "118px 48px minmax(140px, 1.4fr) minmax(90px, 0.9fr) minmax(90px, 0.9fr) " +
  "70px 62px 78px 52px 56px 56px minmax(90px, 0.8fr) 52px 56px 52px 230px";

// 各列默认像素宽 (与列序一致; fr 文本列取名义值, 供列宽拖拽/重置)
const DEFAULT_COL_PX = [
  118, 48, 200, 120, 120, 70, 62, 78, 52, 56, 56, 110, 52, 56, 52, 230
];

function fmtDate(ms: number): string {
  const d = new Date(ms);
  const p = (n: number) => String(n).padStart(2, "0");
  return (
    d.getFullYear() + "-" + p(d.getMonth() + 1) + "-" + p(d.getDate()) +
      " " + p(d.getHours()) + ":" + p(d.getMinutes())
  );
}

// ACC 数值 (stable 屏显口径); 非 mania / 零 objects 返回 -1 (排序时垫底)
function accValue(row: ScoreBrowseRow): number {
  if (row.mode !== 3) return -1;
  const total = row.c300 + row.c100 + row.c50 + row.geki + row.katu + row.miss;
  if (!total) return -1;
  if (row.mods & (1 << 29)) {
    // ScoreV2 局 stable 屏显口径 = accuracyV2 (MAX 权重 305, 归一 N*305)
    return (
      ((305 * row.geki +
        300 * row.c300 +
        200 * row.katu +
        100 * row.c100 +
        50 * row.c50) /
        (total * 305)) *
      100
    );
  }
  // V1 局 stable 屏显口径 = counts 公式 (MAX 权重 300, 归一 N*300)
  return (
    ((300 * (row.c300 + row.geki) +
      200 * row.katu +
      100 * row.c100 +
      50 * row.c50) /
      (total * 300)) *
    100
  );
}

// ---- lazer 行 (AnyRow) 适配 ----
function isLazerRow(r: AnyRow): r is LazerScoreRow {
  return "countsSource" in r;
}

// 行的稳定 key: stable = playedTicks+谱面+回放 (复合), lazer = file。
// stable 只用 playedTicks 不唯一: scores.db 极少数情况下同一条成绩会被写两次
// (实库实锤: COOLEST b88aeb46… 重复条目 ticks 全同), React 重复 key 会在
// 搜索/排序重排时错误复用 DOM —— 行内容串位与「同一行重复多份」的渲染污染。
function rowKey(r: AnyRow): string {
  return isLazerRow(r)
    ? r.file
    : r.playedTicks + "|" + r.beatmapMd5 + "|" + r.replayMd5;
}

// 是否可载入分析: stable = 有回放文件; lazer = Songs 索引命中谱面
function canAnalyze(r: AnyRow): boolean {
  return isLazerRow(r) ? r.hasBeatmap : r.hasReplay;
}

// lazer ACC (305 公式口径): (305·perfect + 300·great + 200·good + 100·ok +
// 50·meh) / (305·N); 零 objects 返回 -1 (排序时垫底)
function lazerAccValue(r: LazerScoreRow): number {
  const total = r.perfect + r.great + r.good + r.ok + r.meh + r.miss;
  if (!total) return -1;
  return (
    ((305 * r.perfect +
      300 * r.great +
      200 * r.good +
      100 * r.ok +
      50 * r.meh) /
      (total * 305)) *
    100
  );
}

function accOf(r: AnyRow): number {
  return isLazerRow(r) ? lazerAccValue(r) : accValue(r);
}

function accText(r: AnyRow): string {
  const v = accOf(r);
  return v < 0 ? "-" : v.toFixed(2) + "%";
}

// lazer mods 列: acronym 列表 + 非默认 settings, 如 "HD DT(1.25×) RD(seed=123)"
function modsOf(r: AnyRow): string {
  if (!isLazerRow(r)) return modsText(r.mods);
  return r.mods.length ? r.mods.map(lazerModText).join(" ") : "-";
}

// SR / pp: -1 或缺字段 (旧缓存行) 显示 "—"
function srText(row: AnyRow): string {
  return row.sr != null && row.sr >= 0 ? row.sr.toFixed(2) : "—";
}

function ppText(row: AnyRow): string {
  return row.pp != null && row.pp >= 0 ? row.pp.toFixed(1) : "—";
}

// 排序数值: 缺字段/-1/null 垫底 (与 accValue 的 -1 约定一致)
function sortableOrMinusOne(v: number | null | undefined): number {
  return v != null && v >= 0 ? v : -1;
}

// keyCount 后端以字符串传输, 排序前转数值 (空/无效记 0)
function keyCountNum(s: string): number {
  const v = parseFloat(s);
  return Number.isFinite(v) ? v : 0;
}

// 长度列: m:ss 文本
function lenText(ms: number | undefined): string {
  if (ms == null || ms < 0) return "—";
  const s2 = Math.round(ms / 1000);
  return Math.floor(s2 / 60) + ":" + String(s2 % 60).padStart(2, "0");
}

// ---- stable 式命令筛选 (Beatmap_search wiki): 字段+操作符+值, 空格分割,
// 多条件 AND, 与自由文本共存。未知 token 回退文本匹配。
type CmpOp = "=" | "==" | "!=" | "<" | ">" | "<=" | ">=";
const NUM_FIELDS: Record<string, (r: AnyRow) => number> = {
  key: (r) => keyCountNum(r.keyCount),
  keys: (r) => keyCountNum(r.keyCount),
  cs: (r) => keyCountNum(r.keyCount),
  stars: (r) => sortableOrMinusOne(r.sr),
  star: (r) => sortableOrMinusOne(r.sr),
  sunny: (r) => sortableOrMinusOne(r.sunnySr),
  acc: (r) => accOf(r),
  pp: (r) => sortableOrMinusOne(r.pp),
  bpm: (r) => sortableOrMinusOne(r.bpm),
  length: (r) => (r.lengthMs != null && r.lengthMs >= 0 ? r.lengthMs / 1000 : -1),
  score: (r) => r.totalScore,
};
const TEXT_FIELDS: Record<string, (r: AnyRow) => string> = {
  artist: (r) => r.artist,
  creator: (r) => r.creator,
  title: (r) => r.title,
  difficulty: (r) => r.version,
  diff: (r) => r.version,
  player: (r) => r.playerName,
};

// mod 筛选: mod=<组合> — 精确匹配整组 mod (与 osu 语义一致, DTNC ≠ NC)。
// 组合分隔符: 逗号或加号 (mod=dt,nc / mod=dt+nc); nm = 空 mod 组; != 为排除。
// stable 行按 legacy 位表还原 acronym 集 (键数位不参与, 键数走 key= 筛选);
// lazer 行取尾块 acronym 集。
const MOD_FIELDS = new Set(["mod", "mods"]);
function rowModSet(r: AnyRow): Set<string> {
  if (isLazerRow(r))
    return new Set(
      r.mods.map((m) => (m.acronym || "").toLowerCase()).filter(Boolean),
    );
  let mods = r.mods;
  // DTNC 归一为 NC (osu 语义, 与 modsText 显示一致)
  if (mods & 512) mods &= ~64;
  for (const [bit] of KEY_MOD_NAMES) if (mods & bit) mods &= ~bit;
  const set = new Set<string>();
  for (const [bit, name] of MOD_NAMES)
    if (mods & bit) { set.add(name.toLowerCase()); mods &= ~bit; }
  if (mods & (1 << 30)) set.add("mr");
  return set;
}
function rowModsMatch(r: AnyRow, value: string): boolean {
  const parts = value
    .trim()
    .toLowerCase()
    .split(/[+,]/)
    .map((s) => s.trim())
    .filter(Boolean);
  const want = new Set(parts[0] === "nm" ? [] : parts);
  const have = rowModSet(r);
  if (want.size !== have.size) return false;
  for (const w of want) if (!have.has(w)) return false;
  return true;
}
function numEq(a: number, b: number): boolean {
  return Math.abs(a - b) <= 0.005;  // 容差覆盖 lazer 两位截断显示
}
function cmpNum(a: number, op: CmpOp, b: number): boolean {
  switch (op) {
    case "=": case "==": return numEq(a, b);
    case "!=": return !numEq(a, b);
    case "<": return a < b;
    case ">": return a > b;
    case "<=": return a <= b;
    case ">=": return a >= b;
  }
}
function cmpText(a: string, op: CmpOp, b: string): boolean {
  const hay = a.toLowerCase(), needle = b.toLowerCase();
  if (op === "!=") return !hay.includes(needle);
  return hay.includes(needle);  // = 及其他操作符一律包含匹配
}
// length 值: 纯数字=秒, "m:ss"=分秒
function parseLengthSec(v: string): number {
  const m = /^(\d+):(\d{1,2})$/.exec(v);
  if (m) return parseInt(m[1], 10) * 60 + parseInt(m[2], 10);
  const n = parseFloat(v);
  return Number.isFinite(n) ? n : NaN;
}

export default function ScoreBrowser({ open, onClose, onPlay, loading, mode }: Props) {
  const [rows, setRows] = useState<AnyRow[]>([]);
  const [info, setInfo] = useState("");
  const [err, setErr] = useState("");
  const [busy, setBusy] = useState(false);
  const [query, setQuery] = useState("");
  const [sortKey, setSortKey] = useState<SortKey>("date");
  const [sortDesc, setSortDesc] = useState(true);
  const [selected, setSelected] = useState<string | null>(null);
  // 搜索命令一览浮层
  const [helpOpen, setHelpOpen] = useState(false);
  const helpRef = useRef<HTMLDivElement | null>(null);
  const helpBtnRef = useRef<HTMLButtonElement | null>(null);
  // 列宽 (px): last_state 持久化, 表头右缘拖拽调整; null = 用默认模板
  const [colWidths, setColWidths] = useState<number[] | null>(null);
  const colWidthsRef = useRef<number[] | null>(null);
  colWidthsRef.current = colWidths;

  // force=true: 让后端跳过响应缓存重扫 (stable: Data\r / Songs; lazer: 数据目录)。
  const fetchRows = useCallback(async (force = false) => {
    setBusy(true);
    setErr("");
    setInfo("");
    setSelected(null);
    try {
      if (mode === "lazer") {
        const res = await api.lazerBrowse(force);
        setRows(res.scores);
        setInfo(res.total + " 条成绩 · 返回 " + res.returned + " 条");
      } else {
        const res = await api.scoresBrowse(MODE_MANIA, force);
        setRows(res.scores);
        setInfo(
          res.total + " 条成绩 · " +
            res.replays + " 个回放 · " +
            res.beatmaps + " 个谱面",
        );
      }
    } catch (e) {
      setErr(e instanceof ApiError ? e.code + ": " + e.message : String(e));
      setRows([]);
    } finally {
      setBusy(false);
    }
  }, [mode]);

  useEffect(() => {
    if (open) void fetchRows();
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [open, fetchRows]);

  // Esc: 一览浮层开着先收浮层, 否则关成绩库 (点击周围关闭已取消, 关闭只走按钮/Esc)
  useEffect(() => {
    if (!open) return;
    const onKey = (e: KeyboardEvent) => {
      if (e.key !== "Escape") return;
      if (helpOpen) setHelpOpen(false);
      else onClose();
    };
    window.addEventListener("keydown", onKey);
    return () => window.removeEventListener("keydown", onKey);
  }, [open, helpOpen, onClose]);

  // 一览浮层: 点按钮与面板以外收起
  useEffect(() => {
    if (!helpOpen) return;
    const onDown = (e: MouseEvent) => {
      const t = e.target as Node;
      if (helpRef.current?.contains(t) || helpBtnRef.current?.contains(t)) return;
      setHelpOpen(false);
    };
    document.addEventListener("mousedown", onDown);
    return () => document.removeEventListener("mousedown", onDown);
  }, [helpOpen]);

  // 列宽恢复 (长度/内容校验不符则忽略)
  useEffect(() => {
    api
      .lastState()
      .then((s) => {
        if (s && Array.isArray(s.scoreColWidths) &&
            s.scoreColWidths.length === HEADERS.length)
          setColWidths(
            s.scoreColWidths.map((n) => Math.max(40, Math.round(n))),
          );
      })
      .catch(() => undefined);
  }, []);

  // 列宽: 全 px 模板 (未自定义时用默认模板, 保留 fr 弹性)
  const gridStyle = {
    gridTemplateColumns: colWidths
      ? colWidths.map((w) => w + "px").join(" ")
      : GRID_TEMPLATE,
  };
  const startResize = (idx: number, e: ReactMouseEvent) => {
    e.preventDefault();
    e.stopPropagation();
    const startX = e.clientX;
    const base = colWidths ?? DEFAULT_COL_PX;
    const startW = base[idx];
    const onMove = (ev: MouseEvent) => {
      const w = Math.max(40, Math.min(800, Math.round(startW + ev.clientX - startX)));
      setColWidths(base.map((v, i) => (i === idx ? w : v)));
    };
    const onUp = () => {
      window.removeEventListener("mousemove", onMove);
      window.removeEventListener("mouseup", onUp);
      const cur = colWidthsRef.current;
      if (cur) api.saveLastState({ scoreColWidths: cur }).catch(() => undefined);
    };
    window.addEventListener("mousemove", onMove);
    window.addEventListener("mouseup", onUp);
  };
  const resetColWidth = (idx: number) => {
    setColWidths((cur) => {
      const next = (cur ?? DEFAULT_COL_PX).map((w, i) =>
        i === idx ? DEFAULT_COL_PX[i] : w);
      api.saveLastState({ scoreColWidths: next }).catch(() => undefined);
      return next;
    });
  };
  // 表头点击排序: 非日期列三态 (首击降序 → 再击升序 → 三击恢复默认日期降序);
  // 日期列双态 (降 ↔ 升, 默认态即日期降序, 无恢复击)。
  const toggleSort = (key: SortKey) => {
    if (key === sortKey) {
      if (key === "date") {
        setSortDesc((v) => !v); // 日期双态: 降 ↔ 升
      } else if (sortDesc) {
        setSortDesc(false); // 三态第 2 击: 升序
      } else {
        setSortKey("date"); // 三态第 3 击: 恢复默认 (日期降序)
        setSortDesc(true);
      }
    } else {
      // 首击新列 (含从其他列点日期) = 降序
      setSortKey(key);
      setSortDesc(true);
    }
  };

  const filtered = useMemo(() => {
    // 查询解析: 识别 stable 式命令 token (字段+操作符+值, 空格分割),
    // 未识别的词回退自由文本包含匹配 (与命令 AND 共存)。
    const words = query.trim().toLowerCase().split(/\s+/).filter(Boolean);
    const conds: { field: string; op: CmpOp; value: string }[] = [];
    const textWords: string[] = [];
    for (const w of words) {
      const m = /^([a-z]+)(==|!=|<=|>=|<|>|=)(.*)$/.exec(w);
      if (m && (NUM_FIELDS[m[1]] || TEXT_FIELDS[m[1]] || MOD_FIELDS.has(m[1])))
        conds.push({ field: m[1], op: m[2] as CmpOp, value: m[3] });
      else textWords.push(w);
    }
    let out = rows;
    for (const w of textWords) {
      if (/^\d+$/.test(w)) {
        // 裸数字: onlineId 精确匹配 (stable identifier 行为)
        const id = Number(w);
        out = out.filter((r) => r.onlineId === id);
      } else {
        out = out.filter(
          (r) =>
            r.title.toLowerCase().includes(w) ||
            r.version.toLowerCase().includes(w) ||
            r.artist.toLowerCase().includes(w) ||
            r.creator.toLowerCase().includes(w) ||
            r.playerName.toLowerCase().includes(w),
        );
      }
    }
    for (const c of conds) {
      if (MOD_FIELDS.has(c.field)) {
        const want = c.op !== "!=";
        out = out.filter((r) => rowModsMatch(r, c.value) === want);
      } else if (NUM_FIELDS[c.field]) {
        const get = NUM_FIELDS[c.field];
        const val =
          c.field === "length" ? parseLengthSec(c.value) : parseFloat(c.value);
        if (!Number.isFinite(val)) continue;
        out = out.filter((r) => cmpNum(get(r), c.op as CmpOp, val));
      } else {
        const get = TEXT_FIELDS[c.field];
        out = out.filter((r) => cmpText(get(r), c.op as CmpOp, c.value));
      }
    }
    const dir = sortDesc ? -1 : 1;
    const cmp: Record<SortKey, (a: AnyRow, b: AnyRow) => number> = {
      date: (a, b) => (a.playedMs - b.playedMs) * dir,
      keys: (a, b) => (keyCountNum(a.keyCount) - keyCountNum(b.keyCount)) * dir,
      title: (a, b) => a.title.localeCompare(b.title) * dir,
      artist: (a, b) => a.artist.localeCompare(b.artist) * dir,
      version: (a, b) => a.version.localeCompare(b.version) * dir,
      mods: (a, b) => modsOf(a).localeCompare(modsOf(b)) * dir,
      acc: (a, b) => (accOf(a) - accOf(b)) * dir,
      sr: (a, b) => (sortableOrMinusOne(a.sr) - sortableOrMinusOne(b.sr)) * dir,
      sunnySr: (a, b) =>
        (sortableOrMinusOne(a.sunnySr) - sortableOrMinusOne(b.sunnySr)) * dir,
      pp: (a, b) => (sortableOrMinusOne(a.pp) - sortableOrMinusOne(b.pp)) * dir,
      bpm: (a, b) => (sortableOrMinusOne(a.bpm) - sortableOrMinusOne(b.bpm)) * dir,
      length: (a, b) =>
        (sortableOrMinusOne(a.lengthMs) - sortableOrMinusOne(b.lengthMs)) * dir,
      score: (a, b) => (a.totalScore - b.totalScore) * dir,
      player: (a, b) => a.playerName.localeCompare(b.playerName) * dir,
      replay: (a, b) =>
        ((canAnalyze(a) ? 1 : 0) - (canAnalyze(b) ? 1 : 0)) * dir,
    };
    return [...out].sort(cmp[sortKey]);
  }, [rows, query, sortKey, sortDesc]);

  if (!open) return null;
  const selectedRow = selected
    ? rows.find((r) => rowKey(r) === selected) ?? null
    : null;
  const visible = filtered.slice(0, MAX_RENDER);
  const lazerMode = mode === "lazer";

  return (
    <div className="score-modal-backdrop">
      {/* 误触防护: 点击周围 (backdrop) 不再关闭, 关闭只走「关闭」按钮 / Esc */}
      <div className="score-modal">
        <div className="score-modal-head">
          <h2>成绩库{lazerMode ? " · lazer" : ""}</h2>
          <span className="score-modal-info">
            {busy ? (lazerMode
              ? "正在扫描 lazer 数据目录…"
              : "正在检索文件夹（首次可能需要一两分钟）…")
            : info}
          </span>
          <div className="spacer" />
          <button
            className="btn"
            disabled={busy}
            title={
              lazerMode
                ? "跳过缓存, 重扫 lazer 数据目录"
                : "跳过缓存, 重扫 Data\\r 与 Songs (读取新 .osr / 新谱面)"
            }
            onClick={() => void fetchRows(true)}
          >
            强制刷新
          </button>
          <button className="btn" onClick={onClose}>
            关闭
          </button>
        </div>
        <div className="score-modal-controls">
          <input
            placeholder="搜索 谱面/曲师/难度/玩家/mod 或 key=4 star>5 mod=nc …"
            title="支持 stable 式命令筛选, 空格分割多条件 AND: key/keys/cs=键数, star/stars=stable 星数, sunny=SRR 星数, acc 准度, pp, score, bpm, length(秒或 m:ss), player/artist/creator/title/difficulty 文本, mod/mods=<acronym> (如 mod=nc / mod=v2 / mod=nm 无mod); 操作符 = == != < > <= >="
            value={query}
            onChange={(e) => setQuery(e.target.value)}
          />
          <button
            className={"btn" + (helpOpen ? " btn-active" : "")}
            ref={helpBtnRef}
            title="查看搜索命令一览"
            onClick={() => setHelpOpen((v) => !v)}
          >
            搜索命令一览
          </button>
          <span className="score-modal-count">
            {filtered.length + " 条"}
          </span>
          {helpOpen ? (
            <div className="score-search-help" ref={helpRef}>
              <div className="ssh-title">
                空格分割多条件 AND, 未知词回退文本匹配
              </div>
              <table>
                <tbody>
                  <tr>
                    <th>数值字段</th>
                    <td>
                      <div>key / keys / cs = 键数</div>
                      <div>star / stars = stable 星数</div>
                      <div>sunny = SRR 星数</div>
                      <div>acc = 准度</div>
                      <div>pp</div>
                      <div>score = 分数</div>
                      <div>bpm</div>
                      <div>length = 时长 (秒或 m:ss)</div>
                    </td>
                  </tr>
                  <tr>
                    <th>文本字段</th>
                    <td>
                      <div>player = 玩家</div>
                      <div>artist = 曲师</div>
                      <div>creator = 谱师</div>
                      <div>title = 标题</div>
                      <div>difficulty / diff = 难度名 (包含匹配)</div>
                    </td>
                  </tr>
                  <tr>
                    <th>Mod 筛选</th>
                    <td>
                      <div>mod / mods = &lt;mod 组合&gt;, 精确匹配整组 (DTNC ≠ NC)</div>
                      <div>
                        组合用逗号或加号: mod=dt,nc = DTNC&emsp;&emsp;mod=nm = 无 mod
                      </div>
                      <div>
                        mod!=nc = 排除纯 NC&emsp;&emsp;缩写不区分大小写
                      </div>
                      <div>
                        nm&emsp;v2 (Score V2)&emsp;mr (镜像)&emsp;kc (KeyCoop)&emsp;
                        rd (Random)&emsp;fi (FadeIn)
                      </div>
                      <div>
                        dt&emsp;nc&emsp;ht&emsp;hd&emsp;hr&emsp;ez&emsp;fl&emsp;nf&emsp;sd&emsp;pf&emsp;
                        at&emsp;so&emsp;ap&emsp;td&emsp;rx&emsp;cn&emsp;tp
                      </div>
                    </td>
                  </tr>
                  <tr>
                    <th>操作符</th>
                    <td>{"=  ==  !=  <  >  <=  >="}</td>
                  </tr>
                  <tr>
                    <th>示例</th>
                    <td>
                      <div>key=4 star&gt;5</div>
                      <div>length&lt;3:00 player!=abc</div>
                      <div>artist=camellia acc&gt;98</div>
                    </td>
                  </tr>
                </tbody>
              </table>
            </div>
          ) : null}
        </div>
        {err ? <div className="error-banner">{err}</div> : null}
        <div className="score-table">
          <div className="score-tbody">
            <div
              className="score-tr score-tr-head"
              style={{
                gridTemplateColumns: colWidths
                  ? colWidths.map((w) => w + "px").join(" ")
                  : GRID_TEMPLATE,
                position: "sticky",
                top: 0,
                zIndex: 1,
                background: "#24262e",
              }}
            >
              {HEADERS.map((h, idx) => {
                const k = h.key;
                return (
                  <span
                    key={h.label}
                    className={(h.cls + " " + (k ? "sortable" : "")).trim()}
                    onClick={k ? () => toggleSort(k) : undefined}
                    title={k ? "点击切换排序" : undefined}
                  >
                    {h.label}
                    {k === sortKey ? (sortDesc ? " ▼" : " ▲") : ""}
                    <span
                      className="col-resizer"
                      title="拖拽调整列宽, 双击重置"
                      onClick={(e) => e.stopPropagation()}
                      onDoubleClick={() => resetColWidth(idx)}
                      onMouseDown={(e) => startResize(idx, e)}
                    />
                  </span>
                );
              })}
            </div>
            {visible.map((r) => {
              const key = rowKey(r);
              const lz = isLazerRow(r);
              const sel = selected === key;
              const analyzable = canAnalyze(r);
              const noBeat = lz && !r.hasBeatmap;
              const mods = modsOf(r);
              return (
                <div
                  key={key}
                  className={
                    "score-tr" + (sel ? " score-tr-sel" : "") +
                      (analyzable ? "" : " score-tr-dead")
                  }
                  style={gridStyle}
                  onClick={() => setSelected(analyzable ? key : null)}
                >
                  <span>{fmtDate(r.playedMs)}</span>
                  <span className="num">{r.keyCount || "-"}</span>
                  <span className="ellip" title={r.title || undefined}>
                    {r.title || (noBeat ? "" : "(未找到谱面)")}
                    {lz && r.countsSource === "ushort" ? (
                      <span className="row-badge" title="json 尾块缺失, 判定计数为 stable 兼容近似">
                        近似计数
                      </span>
                    ) : null}
                    {noBeat ? (
                      <span className="row-badge row-badge-warn" title="Songs 索引未命中谱面, 无法载入分析">
                        未找到谱面
                      </span>
                    ) : null}
                  </span>
                  <span className="ellip" title={r.artist}>{r.artist || "-"}</span>
                  <span className="ellip" title={r.version}>
                    {r.version || "-"}
                  </span>
                  <span className="ctr" title={lz ? mods : undefined}>
                    {mods}
                  </span>
                  <span
                    className="num"
                    title={lz ? "lazer 305 公式口径 ACC" : undefined}
                  >
                    {accText(r)}
                  </span>
                  <span className="num">{r.totalScore.toLocaleString()}</span>
                  <span className="num" title={r.sr != null && r.sr >= 0 ? "stable 星数 (官方 SR, 按行 mods 档位)" : undefined}>
                    {srText(r)}
                  </span>
                  <span className="num" title={r.sunnySr != null && r.sunnySr >= 0 ? "sunnyxxy rework (SRR) 全局星数" : undefined}>
                    {r.sunnySr != null && r.sunnySr >= 0 ? r.sunnySr.toFixed(2) : "无法计算"}
                  </span>
                  <span className="num" title={r.pp != null && r.pp >= 0 ? "官方公式 pp (lazer 移植 SR × 行判定)" : undefined}>
                    {ppText(r)}
                  </span>
                  <span className="ellip" title={r.playerName}>
                    {r.playerName || "-"}
                  </span>
                  <span className="num" title={r.bpm != null && r.bpm >= 0 ? "BPM (驻留最长段)" : undefined}>
                    {r.bpm != null && r.bpm >= 0 ? Math.round(r.bpm) : "—"}
                  </span>
                  <span className="num" title={r.lengthMs != null && r.lengthMs >= 0 ? "物件跨度长度" : undefined}>
                    {lenText(r.lengthMs)}
                  </span>
                  <span className="ctr">
                    {lz ? "✓" : r.hasReplay ? "✓" : "—"}
                  </span>
                  <span
                    className="ctr"
                    style={{ fontSize: 11, fontFamily: "monospace" }}
                  >
                    {r.beatmapMd5 || "-"}
                  </span>
                </div>
              );
            })}
          </div>
        </div>
        <div className="score-modal-foot">
          <span>
            {selectedRow
              ? "已选: " +
                  (selectedRow.title || "(未知谱面)") + " · " +
                  selectedRow.playerName
              : "单击一行选择成绩"}
            {filtered.length > MAX_RENDER
              ? " (仅显示前 " + MAX_RENDER + " 条)"
              : ""}
          </span>
          <div className="spacer" />
          <button
            className="btn primary"
            disabled={!selectedRow || !canAnalyze(selectedRow) || busy || loading}
            onClick={() => selected && onPlay(selected)}
          >
            {loading ? "分析中…" : "确认载入分析"}
          </button>
        </div>
      </div>
    </div>
  );
}