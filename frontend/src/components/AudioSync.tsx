// Precise music playback synced to the preview clock. The song is fully
// decoded via WebAudio (decodeAudioData), so every seek lands on the exact
// sample and the decoded clock is the single source of truth for the preview.
import { useEffect, useRef, useState, useSyncExternalStore } from "react";
import { playerStore } from "../app/playerStore";
import { PreciseAudioPlayer } from "../audio/precisePlayer";

// Preview and audio may disagree up to this much before the loop issues one
// exact sample seek (a BufferSource restart — no demuxer, no byte mapping).
// Larger than a frame gap so normal clock advance never triggers a seek.
const SEEK_TOLERANCE_S = 0.08;

export default function AudioSync() {
  const player = useSyncExternalStore(playerStore.subscribe, playerStore.get);
  const playerRef = useRef<PreciseAudioPlayer | null>(null);
  const [ready, setReady] = useState(false);
  const [failed, setFailed] = useState(false);

  // Decode the song whenever the session music changes.
  useEffect(() => {
    if (!player.musicUrl) {
      setReady(false);
      setFailed(false);
      return;
    }
    let alive = true;
    setReady(false);
    setFailed(false);
    let p = playerRef.current;
    if (!p) {
      p = new PreciseAudioPlayer();
      p.onEnded = () => playerStore.pause();
      playerRef.current = p;
      (window as unknown as { __mraAudio?: PreciseAudioPlayer }).__mraAudio = p;
    }
    p.load(player.musicUrl)
      .then(() => {
        if (!alive) return;
        p!.setVolume(playerStore.get().volume);
        setReady(true);
      })
      .catch(() => {
        if (alive) setFailed(true);
      });
    return () => { alive = false; };
  }, [player.musicUrl]);

  // Follow loop: the decoded player clock is the only time source. The loop
  // seeks the player when the preview jumps (slider click, drag release) and
  // writes the player position back to the preview while playing.
  useEffect(() => {
    if (!player.musicUrl) return;
    let raf = 0;
    let alive = true;
    const step = () => {
      if (!alive) return;
      raf = requestAnimationFrame(step);
      const p = playerRef.current;
      if (!p || !p.ready) return;
      const st = playerStore.get();
      if (st.scrubbing) {
        // Drag in progress: the slider owns the preview; audio stays parked.
        if (p.playing) p.pause();
        return;
      }
      const rate = p.rate || 1;
      const target = Math.max(0, st.previewTimeMs) / 1000 / rate; // audio sec
      if (st.playing) {
        if (!p.playing) {
          void p.play(target); // drag ended while playing: resume at the slider
        } else if (Math.abs(target - p.currentTime) > SEEK_TOLERANCE_S) {
          p.seek(target); // external seek: one exact sample jump
        }
        playerStore.snapAudio(p.currentTime * 1000 * rate); // back to beatmap ms
      } else if (Math.abs(target - p.currentTime) > SEEK_TOLERANCE_S) {
        p.seek(target); // paused seek: moves the resume offset
      }
    };
    raf = requestAnimationFrame(step);
    return () => {
      alive = false;
      cancelAnimationFrame(raf);
    };
  }, [player.musicUrl]);

  // Play/pause mirrors the store; play always starts from the preview so a
  // restart after the end begins from zero.
  useEffect(() => {
    const p = playerRef.current;
    if (!p || !ready) return;
    const rate = p.rate || 1;
    if (player.playing) {
      void p.play(Math.max(0, player.previewTimeMs) / 1000 / rate);
    } else p.pause();
  }, [player.playing, ready]);

  useEffect(() => {
    playerRef.current?.setVolume(player.volume);
  }, [player.volume, ready]);

  useEffect(() => {
    playerRef.current?.setRate(player.rate);
  }, [player.rate, ready]);

  if (failed) {
    return <div className="audio-error">音频解码失败，预览无声音</div>;
  }
  return null;
}
