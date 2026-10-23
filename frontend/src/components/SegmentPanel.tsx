import { useSyncExternalStore, useState } from "react";
import { api } from "../api/client";
import { sessionStore } from "../app/sessionStore";
import { playerStore } from "../app/playerStore";
import { fmtMs, fmtTime, fmtAcc } from "./AnalysisSummary";
import type { SegmentResult } from "../models/types";

export default function SegmentPanel() {
  const snap = useSyncExternalStore(sessionStore.subscribe, sessionStore.get);
  const player = useSyncExternalStore(playerStore.subscribe, playerStore.get);
  const [seg, setSeg] = useState<SegmentResult | null>(null);
  const [busy, setBusy] = useState(false);
  const sid = snap.sessionId;
  const sel = player.selection;

  const analyze = async () => {
    if (!sid || !sel) return;
    setBusy(true);
    try {
      setSeg(await api.segment(sid, sel.startMs, sel.endMs));
    } finally {
      setBusy(false);
    }
  };

  return (
    <div className="panel">
      <h2>选段分析</h2>
      <div className="seg-row">
        <button className="btn" disabled={busy || !sid || !sel} onClick={analyze}>
          分析选中区间
        </button>
        {sel && (
          <span className="range">
            {fmtTime(sel.startMs)} – {fmtTime(sel.endMs)}
          </span>
        )}
        {!sel && (
          <span className="range" style={{ color: "var(--text-dim)" }}>
            在时间轴上右键拖动选择一个区间
          </span>
        )}
      </div>
      {seg && (
        <div className="stat-grid" style={{ marginTop: 8 }}>
          <div className="stat">
            <div className="label">Accuracy</div>
            <div className="value" style={{ fontSize: 14 }}>
              {fmtAcc((snap.analysis?.rulesProfile ?? "v1") === "v2" ? seg.accuracyScoreV2 : seg.accuracyScoreV1)}
            </div>
          </div>
          <div className="stat">
            <div className="label">Stable SR</div>
            <div className="value" style={{ fontSize: 14 }}>
              {seg.officialSr != null && seg.officialSr >= 0 ? seg.officialSr.toFixed(2) : "无法计算"}
            </div>
          </div>
          <div className="stat">
            <div className="label">Sunny SR</div>
            <div className="value" style={{ fontSize: 14 }}>
              {seg.srrSr != null && seg.srrSr >= 0 ? seg.srrSr.toFixed(2) : "无法计算"}
            </div>
          </div>
          <div className="stat">
            <div className="label">平均偏移</div>
            <div className="value">{fmtMs(seg.offsets.mean)} ms</div>
          </div>
          <div className="stat">
            <div className="label">标准差</div>
            <div className="value">{seg.offsets.sd.toFixed(1)} ms</div>
          </div>
          <div className="stat">
            <div className="label">打早平均 ({seg.offsets.early})</div>
            <div className="value">
              {seg.offsets.early > 0 ? fmtMs(seg.offsets.earlyMean) : "-"} ms
            </div>
          </div>
          <div className="stat">
            <div className="label">打晚平均 ({seg.offsets.late})</div>
            <div className="value">
              {seg.offsets.late > 0 ? fmtMs(seg.offsets.lateMean) : "-"} ms
            </div>
          </div>
        </div>
      )}
    </div>
  );
}
