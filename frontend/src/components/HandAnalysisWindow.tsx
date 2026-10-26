// 左右手分析小窗: 全局 / 选段两用模态 (固定宽度, 成绩库同款防误触约定:
// backdrop 不点击关闭, 只走关闭按钮 / Esc)。
import { useEffect, useState, useSyncExternalStore } from "react";
import { api } from "../api/client";
import { sessionStore } from "../app/sessionStore";
import { fmtTime } from "./AnalysisSummary";
import type { HandAnalysisResponse, HandSideStats } from "../models/types";

// scope = { startMs, endMs } → 选段; scope = {} → 全局; null → 关闭
export interface HandScope {
  startMs?: number;
  endMs?: number;
}

interface Props {
  scope: HandScope | null;
  onClose: () => void;
}

// 六档判定行: label + 显示色 (miss 仅展示, 不进 n/acc/偏差)
const COUNT_ROWS: { key: keyof HandAnalysisResponse["left"]["counts"]; label: string; color: string }[] = [
  { key: "max", label: "300g", color: "#57c8ff" },
  { key: "c300", label: "300", color: "#d9c358" },
  { key: "c200", label: "200", color: "#66d17a" },
  { key: "c100", label: "100", color: "#e09a4a" },
  { key: "c50", label: "50", color: "#c46a6a" },
  { key: "miss", label: "Miss", color: "#c44444" },
];

function fmtSigned3(ms: number): string {
  return (ms < 0 ? "-" : "+") + Math.abs(ms).toFixed(3);
}

function SideColumn({ title, side }: { title: string; side: HandSideStats }) {
  return (
    <div className="hand-col">
      <div className="hand-col-title">{title}</div>
      {COUNT_ROWS.map((r) => (
        <div className="hand-row" key={r.key}>
          <span className="hand-label" style={{ color: r.color }}>
            {r.label}
          </span>
          <span className="hand-value">{side.counts[r.key]}</span>
        </div>
      ))}
      <div className="hand-row">
        <span className="hand-label">ACC</span>
        <span className="hand-value">{side.acc.toFixed(2)}%</span>
      </div>
      <div className="hand-row">
        <span className="hand-label">偏差</span>
        <span className="hand-value">
          {side.hasEarly ? fmtSigned3(side.earlyMeanMs) : "-"}
          <span className="hand-dev-gap" />
          {side.hasLate ? fmtSigned3(side.lateMeanMs) : "-"}
        </span>
      </div>
      <div className="hand-row">
        <span className="hand-label">note数</span>
        <span className="hand-value">{side.n}</span>
      </div>
    </div>
  );
}

export default function HandAnalysisWindow({ scope, onClose }: Props) {
  const sid = useSyncExternalStore(sessionStore.subscribe, sessionStore.get).sessionId;
  const [data, setData] = useState<HandAnalysisResponse | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);
  // 中键归属: 打开时从 last_state 读回, 切换即持久化并就地重取
  const [centerToLeft, setCenterToLeft] = useState(false);

  const isSegment = scope?.startMs != null && scope?.endMs != null;

  // 每次打开读回持久化的中键归属 (全局与选段窗共用该值)
  useEffect(() => {
    if (!scope) return;
    let cancelled = false;
    api
      .lastState()
      .then((s) => {
        if (!cancelled && typeof s.handCenterToLeft === "boolean")
          setCenterToLeft(s.handCenterToLeft);
      })
      .catch(() => {});
    return () => {
      cancelled = true;
    };
  }, [scope]);

  // 请求: 打开 (scope 变化) 或中键归属切换且窗开着时重取
  useEffect(() => {
    if (!scope || !sid) return;
    let cancelled = false;
    setBusy(true);
    setError(null);
    api
      .hands(sid, centerToLeft, scope.startMs, scope.endMs)
      .then((d) => {
        if (!cancelled) setData(d);
      })
      .catch((e) => {
        if (!cancelled) {
          setData(null);
          setError(e instanceof Error ? e.message : String(e));
        }
      })
      .finally(() => {
        if (!cancelled) setBusy(false);
      });
    return () => {
      cancelled = true;
    };
  }, [scope, sid, centerToLeft]);

  // Esc 关闭 (backdrop 不点击关闭, 与成绩库一致)
  useEffect(() => {
    if (!scope) return;
    const onKey = (e: KeyboardEvent) => {
      if (e.key === "Escape") onClose();
    };
    window.addEventListener("keydown", onKey);
    return () => window.removeEventListener("keydown", onKey);
  }, [scope, onClose]);

  if (!scope) return null;

  const toggleCenter = (v: boolean) => {
    setCenterToLeft(v);
    api.saveLastState({ handCenterToLeft: v }).catch(() => undefined);
  };

  return (
    <div className="score-modal-backdrop">
      <div className="score-modal hand-modal">
        <div className="score-modal-head">
          <h2>左右手分析</h2>
          <span className="hand-subtitle">
            {isSegment && scope.startMs != null && scope.endMs != null
              ? `选段 ${fmtTime(scope.startMs)}–${fmtTime(scope.endMs)}`
              : "全局"}
          </span>
          <button className="btn hand-close" onClick={onClose} title="关闭 (Esc)">
            ✕
          </button>
        </div>
        {data?.odd && (
          <div className="hand-center-row">
            <button
              type="button"
              className={"hand-switch" + (centerToLeft ? " on-left" : " on-right")}
              title="切换中键归属 (写入 last_state)"
              onClick={() => toggleCenter(!centerToLeft)}
            >
              <span className="hand-switch-knob" />
            </button>
            <span className="hand-switch-text">中键归{centerToLeft ? "左" : "右"}</span>
          </div>
        )}
        {busy && <div className="hand-loading">分析中…</div>}
        {error && <div className="hand-error">请求失败: {error}</div>}
        {!busy && !error && data && (
          <div className="hand-columns">
            <SideColumn title="左手" side={data.left} />
            <SideColumn title="右手" side={data.right} />
          </div>
        )}
      </div>
    </div>
  );
}
