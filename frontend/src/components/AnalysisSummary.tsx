import { useSyncExternalStore } from "react";
import { sessionStore } from "../app/sessionStore";
import {
  JUDGEMENT_COLORS,
  JUDGEMENT_COUNT_KEY,
  JUDGEMENT_ORDER,
  LAZER_JUDGEMENTS,
  type JudgementCounts,
  type JudgementLabel,
} from "../models/types";
import { modsDisplay } from "../utils/modsText";

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

// 判定列定义: stable = 300g/300/... 映射; lazer = Perfect/Great/... 六档直名
function judgementDefs(lazer: boolean): JudgementLabel[] {
  if (lazer) return LAZER_JUDGEMENTS;
  return JUDGEMENT_ORDER.map((name) => ({
    label: name,
    key: JUDGEMENT_COUNT_KEY[name],
    color: JUDGEMENT_COLORS[name],
  }));
}

export function countsRow(counts: JudgementCounts, lazer: boolean) {
  const defs = judgementDefs(lazer);
  return (
    <tr key="c">
      {defs.map((d) => {
        const v = counts[d.key];
        return (
          <td key={d.label} className="judge-count-cell">
            <div className="judge-count-wrap">
              <span className="judge-badge" style={{ background: d.color }}>
                {d.label}
              </span>
              <span className="judge-count">{v}</span>
            </div>
          </td>
        );
      })}
    </tr>
  );
}


export function fmtAcc(v: number | undefined | null): string {
  return v != null && Number.isFinite(v) ? v.toFixed(2) + "%" : "—";
}


export default function AnalysisSummary({
  onOpenHandAnalysis,
}: {
  onOpenHandAnalysis: () => void;
}) {
  const snap = useSyncExternalStore(sessionStore.subscribe, sessionStore.get);
  const a = snap.analysis;
  if (!a) return null;
  const isLazer = a.scoreSource === "lazer";
  const lazerInfo = a.lazer;
  const modsText = modsDisplay(a.mods, a.lazerMods);

  return (
    <div className="panel">
      <h2>总览</h2>
      {snap.replay && (
        <div className="stat-grid" style={{ marginBottom: 8 }}>
          <div className="stat">
            <div className="label">玩家</div>
            <div className="value" style={{ fontSize: 14 }}>{snap.replay.player}</div>
          </div>
          <div className="stat" title={modsText}>
            <div className="label">Mods</div>
            <div
              className="value"
              style={{
                fontSize: 14,
                whiteSpace: "normal",
                wordBreak: "break-word",
              }}
            >
              {modsText}
            </div>
          </div>
          <div
            className="stat"
            title={isLazer
              ? "lazer 305 口径: (305·Perfect + 300·Great + 200·Good + 100·Ok + 50·Meh) / (305·N)"
              : undefined}
          >
            <div className="label">Accuracy</div>
            <div className="value" style={{ fontSize: 14 }}>
              {fmtAcc(
                isLazer || (a.rulesProfile ?? "v1") === "v2"
                  ? a.accuracy?.scoreV2
                  : a.accuracy?.scoreV1,
              )}
            </div>
          </div>
          {isLazer && (
            <div className="stat">
              <div className="label">Rank</div>
              <div className="value" style={{ fontSize: 14 }}>
                {lazerInfo?.rank || "—"}
              </div>
            </div>
          )}
          <div
            className="stat"
            title="官方公式 · lazer 移植 SR · .osr 标定 counts"
          >
            <div className="label">pp</div>
            <div className="value" style={{ fontSize: 14 }}>
              {a.pp != null && a.pp >= 0 ? a.pp.toFixed(1) : "—"}
            </div>
          </div>
          <div className="stat">
            <div className="label">Score</div>
            <div className="value" style={{ fontSize: 14 }}>
              {snap.replay.score.toLocaleString()}
            </div>
          </div>
          <div className="stat">
            <div className="label">Combo</div>
            <div className="value" style={{ fontSize: 14 }}>
              {snap.replay.maxCombo}x
            </div>
          </div>
          <div className="stat">
            <div className="label">平均|偏移|</div>
            <div className="value">{a.offsets.meanAbs.toFixed(1)} ms</div>
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
      <div className="hand-open-row">
        <button
          className="btn"
          onClick={onOpenHandAnalysis}
          title="分析整局的左右手判定分布"
        >
          左右手分析
        </button>
      </div>
      <table className="hits">
        <tbody>{countsRow(a.counts, isLazer)}</tbody>
      </table>
      {isLazer && (
        <div style={{ color: "var(--text-dim)", fontSize: 12, marginTop: 4 }}>
          {lazerInfo?.countsSource === "ushort"
            ? "判定计数与 ACC 为 stable 兼容近似 (尾块缺失, ushort 档重建)"
            : "判定计数与 ACC 为 lazer 判定 profile 直接计算 (305 权重) · 声明值 (直接读取) 见成绩库"}
        </div>
      )}
    </div>
  );
}
