import { useCallback, useEffect, useState } from "react";
import { api, ApiError } from "./api/client";
import { sessionStore, type SessionSnapshot } from "./app/sessionStore";
import { playerStore } from "./app/playerStore";
import Layout from "./components/Layout";
import FileLoader from "./components/FileLoader";
import AnalysisSummary from "./components/AnalysisSummary";
import Diagnostics from "./components/Diagnostics";
import SegmentPanel from "./components/SegmentPanel";
import ReplayPreview from "./components/ReplayPreview";
import Timeline from "./components/Timeline";
import Transport from "./components/Transport";
import AudioSync from "./components/AudioSync";

export default function App() {
  const [snap, setSnap] = useState<SessionSnapshot>(sessionStore.get());
  const [error, setError] = useState<string | null>(null);
  const [loading, setLoading] = useState(false);

  useEffect(() => sessionStore.subscribe(() => setSnap(sessionStore.get())), []);

  const loadSession = useCallback(
    async (
      replayPath: string,
      beatmapPath: string,
      skinFolder?: string,
      audioPath?: string,
    ) => {
      setLoading(true);
      setError(null);
      try {
        const { sessionId } = await api.loadReplay(
          replayPath,
          beatmapPath,
          skinFolder,
          audioPath,
        );
        const [replay, beatmap, summary, hits, hitsV2, objects, frames] =
          await Promise.all([
            api.replayMetadata(sessionId),
            api.beatmapMetadata(sessionId),
            api.summary(sessionId),
            api.hits(sessionId, -8640000000, 8640000000, 50000),
            api.hits(sessionId, -8640000000, 8640000000, 50000, "v2"),
            api.objects(sessionId, -8640000000, 8640000000),
            api.frames(sessionId, -8640000000, 8640000000, 400000),
          ]);
        playerStore.setDuration(
          Math.max(
            objects.objects.length
              ? objects.objects[objects.objects.length - 1].endTimeMs ??
                  objects.objects[objects.objects.length - 1].startTimeMs
              : 0,
            frames.frames.length ? frames.frames[frames.frames.length - 1].timeMs : 0,
          ) + 2000,
        );
        playerStore.seek(0);
        playerStore.setSelection(null);
        sessionStore.set({
          sessionId,
          replay,
          beatmap,
          analysis: summary,
          events: hits.hits,
          eventsV2: hitsV2.hits,
          objects: objects.objects,
          frames: frames.frames,
          lastEventTimeMs: summary.lastEventTimeMs,
        });
        playerStore.setMusicUrl(audioPath ? api.audioUrl(sessionId) : null);
      } catch (e) {
        const msg =
          e instanceof ApiError
            ? `${e.code}: ${e.message}`
            : String(e);
        setError(msg);
        sessionStore.reset();
        playerStore.setMusicUrl(null);
      } finally {
        setLoading(false);
      }
    },
    [],
  );

  const ready = snap.sessionId !== null;

  return (
    <Layout title="osu!mania Replay Analyzer">
      <div className="topbar">
        <h1>osu!mania Replay Analyzer</h1>
        <div className="spacer" />
      </div>
      <div className="main">
        <div className="left-panel">
          <FileLoader onLoad={loadSession} loading={loading} />
          {error && <div className="error-banner">{error}</div>}
          {!ready && !error && (
            <div className="empty-hint">
              选择 .osr 回放与对应 .osu 谱面开始分析
            </div>
          )}
          {ready && (
            <>
              <AnalysisSummary />
              <Diagnostics />
              <SegmentPanel />
            </>
          )}
        </div>
        <div className="right-panel">
          {ready && (
            <>
              <AudioSync />
              <Timeline />
              <ReplayPreview />
              <Transport />
            </>
          )}
        </div>
      </div>
    </Layout>
  );
}
