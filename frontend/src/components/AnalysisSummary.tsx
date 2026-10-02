import { useSyncExternalStore } from "react";
import { sessionStore } from "../app/sessionStore";
import {
  JUDGEMENT_COLORS,
  JUDGEMENT_COUNT_KEY,
  JUDGEMENT_ORDER,
  type JudgementCounts,
} from "../models/types";

export function fmtMs(ms: number): string {
  const sign = ms < 0 ? "-" : "+";
  return sign + Math.abs(ms).toFixed(1);
}

export function fmtTime(ms: number): string {
  const m = Math.floor(ms / 60000);
  const s = Math.floor((ms % 60000) / 1000);
  const cs = Math.floor((ms % 1000) / 10);
  return m + ":" + String(s).padStart(2, "0") + "." + String(cs).padStart(2, "0");
}

function countsRow(counts: JudgementCounts) {
  return (
    <tr key="c">
      {JUDGEMENT_ORDER.map((name) => {
        const v = counts[JUDGEMENT_COUNT_KEY[name]];
        return (
          <td key={name}>
            <span className="judge-badge" style={{ background: JUDGEMENT_COLORS[name] }}>
              {name}
            </span>{" "}
            {v}
          </td>
        );
      })}
    </tr>
  );
}

export default function AnalysisSummary() {
  const snap = useSyncExternalStore(sessionStore.subscribe, sessionStore.get);
  const a = snap.analysis;
  if (!a) return null;

  return (
    <div className="panel">
      <h2>总览</h2>
      {snap.replay && (
        <div className="stat-grid" style={{ marginBottom: 8 }}>
          <div className="stat">
            <div className="label">玩家</div>
            <div className="value" style={{ fontSize: 14 }}>{snap.replay.player}</div>
          </div>
          <div className="stat">
            <div className="label">ACC (V1 / V2)</div>
            <div className="value" style={{ fontSize: 14 }}>
              {a.accuracy.scoreV1.toFixed(2)}% / {a.accuracy.scoreV2.toFixed(2)}%
            </div>
          </div>
          <div className="stat">
            <div className="label">Score / Combo</div>
            <div className="value" style={{ fontSize: 14 }}>
              {snap.replay.score.toLocaleString()} / {snap.replay.maxCombo}x
            </div>
          </div>
          <div className="stat">
            <div className="label">平均偏移</div>
            <div className="value">{fmtMs(a.offsets.mean)} ms</div>
          </div>
          <div className="stat">
            <div className="label">平均|偏移|</div>
            <div className="value">{a.offsets.meanAbs.toFixed(1)} ms</div>
          </div>
          <div className="stat">
            <div className="label">标准差 / P95</div>
            <div className="value">
              {a.offsets.sd.toFixed(1)} / {a.offsets.p95.toFixed(1)} ms
            </div>
          </div>
          <div className="stat">
            <div className="label">打早平均 ({a.offsets.early})</div>
            <div className="value">
              {a.offsets.early > 0 ? fmtMs(a.offsets.earlyMean) : "-"} ms
            </div>
          </div>
          <div className="stat">
            <div className="label">打晚平均 ({a.offsets.late})</div>
            <div className="value">
              {a.offsets.late > 0 ? fmtMs(a.offsets.lateMean) : "-"} ms
            </div>
          </div>
        </div>
      )}
      <table className="hits">
        <tbody>{countsRow(a.counts)}</tbody>
      </table>
      <div style={{ color: "var(--text-dim)", fontSize: 12, marginTop: 4 }}>
        偏移 = 实际按键时刻 − 物件时刻 (ms)，负值=提前
        {a.unmatchedInputCount > 0
          ? " · 未匹配输入 " + a.unmatchedInputCount
          : ""}
      </div>
    </div>
  );
}
