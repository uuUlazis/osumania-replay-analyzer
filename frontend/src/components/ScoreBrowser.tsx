import { useCallback, useEffect, useMemo, useState } from "react";
import { api, ApiError } from "../api/client";
import type { ScoreBrowseRow } from "../models/types";
import { KEY_MOD_NAMES, MOD_NAMES } from "../models/types";

interface Props {
  open: boolean;
  onClose: () => void;
  onPlay: (playedTicks: string) => void;
  loading: boolean;
}

// 表头点击排序键 (谱面md5 为哈希串, 不参与排序)
type SortKey =
  | "date" | "keys" | "title" | "artist" | "version"
  | "mods" | "acc" | "score" | "player" | "replay";

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
  { key: "player", label: "玩家", cls: "" },
  { key: "replay", label: "有回放", cls: "ctr" },
  { key: null, label: "谱面md5", cls: "ctr" },
];

const MAX_RENDER = 500;

// mania 绑死
const MODE_MANIA = 3;

// 列宽模板 (inline 覆盖 global.css 的 .score-tr 定义), 列序:
// 日期 | 键数 | 歌曲名 | 曲师 | 难度名 | Mods | ACC | 分数 | 玩家 | 有回放 | 谱面md5
const GRID_TEMPLATE =
  "118px 48px minmax(140px, 1.4fr) minmax(90px, 0.9fr) minmax(90px, 0.9fr) " +
  "70px 62px 78px minmax(90px, 0.8fr) 52px 230px";

function modsText(mods: number): string {
  const parts: string[] = [];
  for (const [bit, name] of KEY_MOD_NAMES)
    if (mods & bit) { parts.push(name); mods &= ~bit; }
  for (const [bit, name] of MOD_NAMES) if (mods & bit) parts.push(name);
  return parts.length ? parts.join("") : "-";
}

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

function acc(row: ScoreBrowseRow): string {
  const v = accValue(row);
  return v < 0 ? "-" : v.toFixed(2) + "%";
}

// keyCount 后端以字符串传输, 排序前转数值 (空/无效记 0)
function keyCountNum(s: string): number {
  const v = parseFloat(s);
  return Number.isFinite(v) ? v : 0;
}

export default function ScoreBrowser({ open, onClose, onPlay, loading }: Props) {
  const [rows, setRows] = useState<ScoreBrowseRow[]>([]);
  const [info, setInfo] = useState("");
  const [err, setErr] = useState("");
  const [busy, setBusy] = useState(false);
  const [query, setQuery] = useState("");
  const [sortKey, setSortKey] = useState<SortKey>("date");
  const [sortDesc, setSortDesc] = useState(true);
  const [selected, setSelected] = useState<string | null>(null);

  // force=true: 让后端跳过响应缓存重扫 Data\r / Songs (读取新 .osr / 新谱面)。
  const fetchRows = useCallback(async (force = false) => {
    setBusy(true);
    setErr("");
    setInfo("");
    setSelected(null);
    try {
      const res = await api.scoresBrowse(MODE_MANIA, force);
      setRows(res.scores);
      setInfo(
        res.total + " 条成绩 · " +
          res.replays + " 个回放 · " +
          res.beatmaps + " 个谱面",
      );
    } catch (e) {
      setErr(e instanceof ApiError ? e.code + ": " + e.message : String(e));
      setRows([]);
    } finally {
      setBusy(false);
    }
  }, []);

  useEffect(() => {
    if (open) void fetchRows();
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [open, fetchRows]);

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
    const q = query.trim().toLowerCase();
    let out = rows;
    if (q)
      out = out.filter(
        (r) =>
          r.title.toLowerCase().includes(q) ||
          r.version.toLowerCase().includes(q) ||
r.artist.toLowerCase().includes(q) ||
          r.creator.toLowerCase().includes(q) ||
          r.playerName.toLowerCase().includes(q),
      );
    const dir = sortDesc ? -1 : 1;
    const cmp: Record<SortKey, (a: ScoreBrowseRow, b: ScoreBrowseRow) => number> = {
      date: (a, b) => (a.playedMs - b.playedMs) * dir,
      keys: (a, b) => (keyCountNum(a.keyCount) - keyCountNum(b.keyCount)) * dir,
      title: (a, b) => a.title.localeCompare(b.title) * dir,
      artist: (a, b) => a.artist.localeCompare(b.artist) * dir,
      version: (a, b) => a.version.localeCompare(b.version) * dir,
      mods: (a, b) => modsText(a.mods).localeCompare(modsText(b.mods)) * dir,
      acc: (a, b) => (accValue(a) - accValue(b)) * dir,
      score: (a, b) => (a.totalScore - b.totalScore) * dir,
      player: (a, b) => a.playerName.localeCompare(b.playerName) * dir,
      replay: (a, b) => ((a.hasReplay ? 1 : 0) - (b.hasReplay ? 1 : 0)) * dir,
    };
    return [...out].sort(cmp[sortKey]);
  }, [rows, query, sortKey, sortDesc]);

  if (!open) return null;
  const selectedRow = selected
    ? rows.find((r) => r.playedTicks === selected) ?? null
    : null;
  const visible = filtered.slice(0, MAX_RENDER);

  return (
    <div className="score-modal-backdrop" onClick={onClose}>
      <div className="score-modal" onClick={(e) => e.stopPropagation()}>
        <div className="score-modal-head">
          <h2>成绩库</h2>
          <span className="score-modal-info">
            {busy ? "正在检索文件夹（首次可能需要一两分钟）…" : info}
          </span>
          <div className="spacer" />
          <button className="btn" onClick={onClose}>
            关闭
          </button>
        </div>
        <div className="score-modal-controls">
          <input
            placeholder="搜索 谱面/曲师/谱师/玩家"
            value={query}
            onChange={(e) => setQuery(e.target.value)}
          />
          <button
            className="btn"
            disabled={busy}
            title="跳过缓存, 重扫 Data\r 与 Songs (读取新 .osr / 新谱面)"
            onClick={() => void fetchRows(true)}
          >
            强制刷新
          </button>
          <span className="score-modal-count">
            {filtered.length + " 条"}
          </span>
        </div>
        {err ? <div className="error-banner">{err}</div> : null}
        <div className="score-table">
          <div className="score-tbody">
            <div
              className="score-tr score-tr-head"
              style={{
                gridTemplateColumns: GRID_TEMPLATE,
                position: "sticky",
                top: 0,
                zIndex: 1,
                background: "#24262e",
              }}
            >
              {HEADERS.map((h) => {
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
                  </span>
                );
              })}
            </div>
            {visible.map((r) => {
              const sel = selected === r.playedTicks;
              return (
                <div
                  key={r.playedTicks}
                  className={
                    "score-tr" + (sel ? " score-tr-sel" : "") +
                      (r.hasReplay ? "" : " score-tr-dead")
                  }
                  style={{ gridTemplateColumns: GRID_TEMPLATE }}
                  onClick={() => setSelected(r.hasReplay ? r.playedTicks : null)}
                >
                  <span>{fmtDate(r.playedMs)}</span>
                  <span className="num">{r.keyCount || "-"}</span>
                  <span className="ellip" title={r.title}>
                    {r.title || "(未找到谱面)"}
                  </span>
                  <span className="ellip" title={r.artist}>{r.artist || "-"}</span>
                  <span className="ellip" title={r.version}>
                    {r.version || "-"}
                  </span>
                  <span className="ctr">{modsText(r.mods)}</span>
                  <span className="num">{acc(r)}</span>
                  <span className="num">{r.totalScore.toLocaleString()}</span>
                  <span className="ellip" title={r.playerName}>
                    {r.playerName || "-"}
                  </span>
                  <span className="ctr">{r.hasReplay ? "✓" : "—"}</span>
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
            disabled={!selectedRow || !selectedRow.hasReplay || busy || loading}
            onClick={() => selected && onPlay(selected)}
          >
            {loading ? "分析中…" : "确认载入分析"}
          </button>
        </div>
      </div>
    </div>
  );
}