// Sample-exact music playback built on WebAudio: decodeAudioData produces a
// PCM AudioBuffer and seeks become exact sample offsets. The HTMLAudioElement
// path was dropped because Chromium's mp3 demuxer maps time->byte via an
// average-bitrate estimate; on VBR files the audible landing missed by
// seconds and the error grew with position — that was the preview/music
// desync. osu! decodes frames exactly (BASS), which is why the same songs
// always synced there.
//
// rate mod 变速不变调 (stable DT/NC 均不变调, BASS_FX tempo 语义):
// load(url, timeStretch=rate) 时对 buffer 做离线 WSOLA 时间处理, 之后以
// playbackRate=1 播放 (音高保持)。此时 buffer 时间 ≠ 谱面时间:
// 谱面ms = buffer秒 × timeBase × 1000 (timeBase = timeStretch), AudioSync
// 的换算用 timeBase; 未拉伸 (rate=1) 时 timeBase=1, 换算恒等。
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
  // 拉伸后 buffer 的时间基数 (谱面ms = buffer秒 × timeBase × 1000); 1 = 未拉伸
  private timeBase = 1;
  // load 代际号: 并发/过期 load (NC→非 NC 时 nightcore 翻转先触发旧 URL
  // 重解码、musicUrl 再触发新 URL) 后完成者不得覆盖新歌 buffer
  private loadSeq = 0;
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

  // timeStretch ≠ 1: 解码后离线 WSOLA 处理 buffer, 播放速率锁 1, timeBase 生效
  async load(url: string, timeStretch = 1): Promise<void> {
    const seq = ++this.loadSeq;
    this.stopSource();
    this.playingFlag = false;
    this.offset = 0;
    this.buffer = null;
    this.timeBase = Math.abs(timeStretch - 1) > 0.001 ? timeStretch : 1;
    const res = await fetch(url);
    if (seq !== this.loadSeq) return; // 已被更新的 load 取代
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
    const decoded = await this.ctx.decodeAudioData(bytes);
    if (seq !== this.loadSeq) return; // 过期: 不覆盖新歌 buffer
    let buffer = decoded;
    if (this.timeBase !== 1) {
      const { wsolaStretchBuffer } = await import("./wsola");
      const stretched = await wsolaStretchBuffer(
        this.ctx, buffer, this.timeBase);
      if (seq !== this.loadSeq) return;
      buffer = stretched;
    }
    this.buffer = buffer;
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
    // 拉伸 buffer: tempo 已编码进 buffer, 播放速率恒 1 (音高保持)
    src.playbackRate.value = this.timeBase !== 1 ? 1 : this.rateValue;
    this.srcRate = src.playbackRate.value;
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

  get timeBaseValue(): number { return this.timeBase; }

  // 拉伸模式下 tempo 在 buffer 里, setRate 只换时间基数不重启源 (rate 变更
  // 需重新 load — 会话切换本来就换 musicUrl)
  setRate(r: number): void {
    const clamped = Math.min(4, Math.max(0.25, r));
    if (this.timeBase !== 1) {
      this.rateValue = clamped;
      return;
    }
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
