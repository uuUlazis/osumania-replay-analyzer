import { useSyncExternalStore } from "react";
import { sessionStore } from "../app/sessionStore";

const CHECK_LABELS: Record<string, string> = {
  beatmapMd5Match: "谱面 MD5 匹配",
  modeIsMania: "模式为 mania",
  judgementCountsMatch: "判定数与 .osr 声明一致",
  replayEndedNormally: "回放正常结束",
};

export default function Diagnostics() {
  const snap = useSyncExternalStore(sessionStore.subscribe, sessionStore.get);
  const a = snap.analysis;
  if (!a) return null;
  const warnings: string[] = [];
  if (a.unmatchedInputCount > 0)
    warnings.push(a.unmatchedInputCount + " 次输入未匹配到任何物件（空敲/窗口外）");
  if (a.unmatchedObjects > 0)
    warnings.push(a.unmatchedObjects + " 个物件未被解析（可能与长按重叠等）");
  const stabilityText =
    "稳定性指标: tapStability " + a.consistencyReport.tapStability.toFixed(3) +
    " · 和弦内散布 " + a.consistencyReport.chordOffsetSpread.toFixed(1) + " ms";
  return (
    <div className="panel">
      <h2>一致性诊断</h2>
      {a.consistency.map((c) => (
        <div className="check-row" key={c.check}>
          <span className={
            c.pass === true ? "check-pass" : c.pass === false ? "check-fail" : "check-na"
          }>
            {c.pass === true ? "✓" : c.pass === false ? "✗" : "–"}
          </span>
          <span>{CHECK_LABELS[c.check] ?? c.check}</span>
        </div>
      ))}
      <div className="check-row" style={{ color: "var(--text-dim)", fontSize: 12 }}>
        {stabilityText}
      </div>
      {warnings.length > 0 && (
        <div className="warnings">{warnings.join("\n")}</div>
      )}
    </div>
  );
}
