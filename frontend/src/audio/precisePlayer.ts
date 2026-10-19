// Sample-exact music playback built on WebAudio: decodeAudioData produces a
// PCM AudioBuffer and seeks become exact sample offsets. The HTMLAudioElement
// path was dropped because Chromium's mp3 demuxer maps time->byte via an
// average-bitrate estimate; on VBR files the audible landing missed by
// seconds and the error grew with position — that was the preview/music
// desync. osu! decodes frames exactly (BASS), which is why the same songs
// always synced there.
export class PreciseAudioPlayer {
  private ctx: AudioContext | null = null;
  private gain: GainNode | null = null;
  private buffer: AudioBuffer | null = null;
  private source: AudioBufferSourceNode | null = null;
  private startedAt = 0;
  private startOffset = 0;
  private offset = 0;
  private playingFlag = false;
  private volumeValue = 1;
  private rateValue = 1;
  private srcRate = 1;
  onEnded: (() => void) | null = null;

  get ready(): boolean { return this.buffer !== null; }
  get duration(): number { return this.buffer ? this.buffer.duration : 0; }
  get playing(): boolean { return this.playingFlag; }
  get rate(): number { return this.rateValue; }
  get currentTime(): number {
    if (!this.buffer) return 0;
    if (!this.playingFlag || !this.ctx) return this.offset;
    return Math.min(this.buffer.duration,
      this.startOffset + (this.ctx.currentTime - this.startedAt) * this.srcRate);
  }

  async load(url: string): Promise<void> {
    this.stopSource();
    this.playingFlag = false;
    this.offset = 0;
    this.buffer = null;
    const res = await fetch(url);
    if (!res.ok) throw new Error("audio http " + res.status);
    const bytes = await res.arrayBuffer();
    if (!this.ctx) {
      const Ctor = window.AudioContext ||
        (window as unknown as { webkitAudioContext?: typeof AudioContext }).webkitAudioContext;
      if (!Ctor) throw new Error("WebAudio unavailable");
      this.ctx = new Ctor();
      this.gain = this.ctx.createGain();
      this.gain.gain.value = this.volumeValue;
      this.gain.connect(this.ctx.destination);
    }
    this.buffer = await this.ctx.decodeAudioData(bytes);
  }

  async play(fromSec?: number): Promise<void> {
    if (!this.ctx || !this.gain || !this.buffer) return;
    if (this.ctx.state === "suspended") {
      try { await this.ctx.resume(); } catch { /* retried on the next play */ }
    }
    const at = Math.max(0, Math.min(
      fromSec !== undefined ? fromSec : this.offset,
      Math.max(0, this.buffer.duration - 0.001)));
    this.stopSource();
    const src = this.ctx.createBufferSource();
    src.buffer = this.buffer;
    src.connect(this.gain);
    src.onended = () => {
      if (this.source !== src) return;
      this.source = null;
      if (this.playingFlag) {
        this.playingFlag = false;
        this.offset = this.buffer ? this.buffer.duration : 0;
        this.onEnded?.();
      }
    };
    src.playbackRate.value = this.rateValue;
    this.srcRate = this.rateValue;
    src.start(0, at);
    this.source = src;
    this.startedAt = this.ctx.currentTime;
    this.startOffset = at;
    this.playingFlag = true;
  }

  pause(): void {
    if (!this.playingFlag) return;
    this.offset = this.currentTime;
    this.stopSource();
    this.playingFlag = false;
  }

  seek(sec: number): void {
    const t = Math.max(0, Math.min(sec, this.buffer ? this.buffer.duration : 0));
    if (this.playingFlag) void this.play(t);
    else this.offset = t;
  }

  setVolume(v: number): void {
    this.volumeValue = v;
    if (this.gain) this.gain.gain.value = v;
  }

  // Changing the rate mid-play restarts the source at the current position so
  // the position mapping stays consistent with the new srcRate.
  setRate(r: number): void {
    const clamped = Math.min(4, Math.max(0.25, r));
    if (clamped === this.rateValue) return;
    if (this.playingFlag) {
      const cur = this.currentTime;
      this.rateValue = clamped;
      void this.play(cur);
    } else {
      this.rateValue = clamped;
    }
  }

  private stopSource(): void {
    if (!this.source) return;
    const s = this.source;
    this.source = null;
    s.onended = null;
    try { s.stop(); } catch { /* already stopped */ }
    try { s.disconnect(); } catch { /* not connected */ }
  }
}
