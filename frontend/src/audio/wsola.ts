// rate mod 变速不变调 (stable 的 DT/NC 均不变调: BASS_FX tempo 语义)。
// WebAudio 的 playbackRate 是重采样 (速度与音调耦合), 因此对解码后的
// AudioBuffer 做离线 WSOLA (波形相似叠加) 时间域处理: 输出时长 = 输入/factor
// (factor>1 压缩=加速, <1 拉伸=减速), 音高不变。处理后的 buffer 以
// playbackRate=1 播放 (PreciseAudioPlayer 锁定), 此时 buffer 时间轴 ≡ 原曲
// 时间轴, 但谱面时间 = buffer 时间 × factor — 时间换算由 AudioSync 按
// timeBase= factor 完成。
//
// 算法: 合成帧 10ms hop + hann 窗; 每个输出帧在名义输入位置 ±5ms 内搜索
// 与上一帧尾部互相关的最佳偏移, 按窗函数叠加后除以叠加包络。多声道共用
// 同一偏移序列 (相关性在混音单声道上算), 避免声道间图像撕裂。
// 分块让出主线程 (几分钟的歌总计 <1s 计算量)。
export async function wsolaStretchBuffer(
  ctx: AudioContext,
  buffer: AudioBuffer,
  factor: number,
  onProgress?: (p: number) => void,
): Promise<AudioBuffer> {
  if (!(Math.abs(factor - 1) > 0.001) || !(factor > 0)) return buffer;
  const sr = buffer.sampleRate;
  const H = Math.round(sr * 0.01); // 10ms hop
  const channels: Float32Array[] = [];
  for (let c = 0; c < buffer.numberOfChannels; ++c)
    channels.push(buffer.getChannelData(c));
  const inLen = channels[0].length;
  if (inLen < 2 * H + 8) return buffer;

  const hann = new Float32Array(2 * H);
  for (let i = 0; i < 2 * H; ++i)
    hann[i] = 0.5 - 0.5 * Math.cos((2 * Math.PI * i) / (2 * H - 1));

  const outLen = Math.ceil(inLen / factor) + 2 * H;
  const norm = new Float32Array(outLen);
  for (let k = 0; k * H < outLen; ++k)
    for (let i = 0; i < 2 * H; ++i) {
      const at = k * H + i;
      if (at < outLen) norm[at] += hann[i];
    }
  const outs: Float32Array[] = channels.map(() => new Float32Array(outLen));

  const maxDelta = Math.round(H / 2); // ±5ms 搜索域
  const deltaStep = 4;
  const outFrames = Math.max(
    0,
    Math.floor((inLen - 2 * H - maxDelta - 2) / (H * factor)),
  );
  // 上一帧输入尾部的 H 样本 = 互相关模板 (首帧无模板, δ=0)
  let template: Float32Array | null = null;
  // 混音参考 (δ 搜索用), 各声道共用同一 δ 序列
  const mix = new Float32Array(inLen);
  for (const ch of channels)
    for (let i = 0; i < inLen; ++i) mix[i] += ch[i] / channels.length;

  for (let k = 0; k < outFrames; ++k) {
    const nominal = Math.round(k * H * factor);
    let bestDelta = 0;
    if (template) {
      let best = -Infinity;
      for (let d = -maxDelta; d <= maxDelta; d += deltaStep) {
        const at = nominal + d;
        if (at < 0 || at + H >= inLen) continue;
        let sum = 0;
        for (let i = 0; i < H; i += 2) sum += mix[at + i] * template[i];
        if (sum > best) { best = sum; bestDelta = d; }
      }
    }
    const at = nominal + bestDelta;
    for (let c = 0; c < channels.length; ++c) {
      const ch = channels[c];
      const out = outs[c];
      for (let i = 0; i < 2 * H; ++i) out[k * H + i] += ch[at + i] * hann[i];
    }
    template = channels[0].slice(at + H, at + 2 * H);
    if ((k & 255) === 255) {
      onProgress?.(k / outFrames);
      await new Promise((r) => setTimeout(r, 0));
    }
  }

  const out = ctx.createBuffer(channels.length, outLen, sr);
  for (let c = 0; c < channels.length; ++c) {
    const d = out.getChannelData(c);
    for (let i = 0; i < outLen; ++i)
      d[i] = norm[i] > 1e-4 ? outs[c][i] / norm[i] : 0;
  }
  onProgress?.(1);
  return out;
}
