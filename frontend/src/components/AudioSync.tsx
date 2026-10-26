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

// 谱面时间 ↔ 音频时间换算: 1:1。playbackRate 只负责把歌放快/放慢 (buffer 位置
// 时间换算基数 = player.timeBaseValue: WSOLA 拉伸路径 (DT/HT/自定义 rate,
// 音高保持) 下 buffer 时间 × timeBase = 谱面时间; NC 重采样路径 (升调) 与
// NM 下 buffer 时间 ≡ 谱面时间 (timeBase=1)。曾因无 timeBase 概念把
// NC 的重采样双重换算 (×1.5 又 ÷1.5 的位置错了对象), 造成音画不同步。
function beatmapMsToAudioSec(ms: number, tb: number): number {
  return Math.max(0, ms) / 1000 / tb;
}
function audioSecToBeatmapMs(sec: number, tb: number): number {
  return sec * tb * 1000;
}

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
    // NC (升调) = 重采样: timeStretch=1 走 1:1 映射; 其余 rate mod (DT/HT/
    // 自定义) 不升调 = WSOLA 拉伸 (timeBase=rate, playbackRate 锁 1)
    const st = playerStore.get();
    p.load(player.musicUrl, st.nightcore ? 1 : st.rate)
      .then(() => {
        if (!alive) return;
        p!.setVolume(playerStore.get().volume);
        setReady(true);
      })
      .catch(() => {
        if (alive) setFailed(true);
      });
    return () => { alive = false; };
  }, [player.musicUrl, player.nightcore]);

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
      const tb = p.timeBaseValue || 1;
      const target = beatmapMsToAudioSec(st.previewTimeMs, tb);
      if (st.playing) {
        if (!p.playing) {
          void p.play(target); // drag ended while playing: resume at the slider
        } else if (Math.abs(target - p.currentTime) > SEEK_TOLERANCE_S) {
          p.seek(target); // external seek: one exact sample jump
        }
        playerStore.snapAudio(audioSecToBeatmapMs(p.currentTime, tb)); // beatmap ms
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
    if (player.playing) {
      void p.play(
        beatmapMsToAudioSec(player.previewTimeMs, p.timeBaseValue || 1),
      );
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
