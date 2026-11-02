// 累计 acc 走势 (0.1.4): 按固定桶数采样「从头累计到该桶末」的 acc。
// 口径与显示 ACC 严格一致 (backend/src/mania/accuracy.cpp):
//   V1 (stable 显示口径): numerator = 50·meh + 100·ok + 200·good + 300·(great+perfect),
//       denominator = 已判定物件数 × 300; LN 整条单计 (判定取 hold_tail 上的 v1Judgement)
//   V2 (v2 / lazer 口径): numerator = 50·meh + 100·ok + 200·good + 300·great + 305·perfect,
//       denominator = 已判定数 × 305; LN 头尾各计
// 判定流已含合成 miss (hit_matcher 对未击中物件回填 Miss), 因此曲线终点 == 总 ACC。
// 无数据桶 (首个判定之前) = NaN。按 events 数组引用 memo, 会话内只算一次。
import type { Hit } from "../models/types";

export const ACC_CURVE_BUCKETS = 600;

export interface AccCurve {
  /** 桶末累计 acc (fraction 0..1); 无数据桶 = NaN */
  values: Float64Array;
  /** 有值桶数 */
  count: number;
  min: number;
  max: number;
}

// backend judgement 字符串 (api_server.cpp hitResultNameJson, 小写) -> 权重
const W_V1: Record<string, number> = {
  perfect: 300, great: 300, good: 200, ok: 100, meh: 50, miss: 0,
};
const W_V2: Record<string, number> = {
  perfect: 305, great: 300, good: 200, ok: 100, meh: 50, miss: 0,
};
const DEN_V1 = 300;
const DEN_V2 = 305;

interface Contribution {
  t: number;
  w: number;
}

interface CacheEntry extends AccCurve {
  v2: boolean;
  durationMs: number;
  buckets: number;
}

const cache = new WeakMap<object, CacheEntry>();

function weightOf(map: Record<string, number>, j: string | undefined): number {
  return j != null && map[j] != null ? map[j] : 0;
}

export function computeAccCurve(
  events: Hit[],
  v2: boolean,
  durationMs: number,
  buckets: number = ACC_CURVE_BUCKETS,
): AccCurve {
  const cached = cache.get(events);
  if (cached && cached.v2 === v2 && cached.durationMs === durationMs && cached.buckets === buckets) {
    return cached;
  }

  const wmap = v2 ? W_V2 : W_V1;
  const list: Contribution[] = [];
  // V1: 每物件恰好计一次 — hold_head 不计, hold_tail 携带整条 LN 的 v1Judgement;
  // 个别无尾事件的头 (退化数据) 以头事件兜底计入。
  // V2: note_head / hold_head / hold_tail 每个判定事件各计一次。
  const v1PendingHead = v2 ? null : new Map<number, Contribution>();
  for (const e of events) {
    if (v1PendingHead) {
      if (e.phase === "hold_head") {
        v1PendingHead.set(e.objectIndex, {
          t: e.objectTimeMs,
          w: weightOf(wmap, e.v1Judgement ?? e.judgement),
        });
        continue;
      }
      if (e.phase === "hold_tail") {
        v1PendingHead.delete(e.objectIndex);
        list.push({
          t: e.objectTimeMs,
          w: weightOf(wmap, e.v1Judgement ?? e.judgement),
        });
        continue;
      }
    }
    list.push({ t: e.objectTimeMs, w: weightOf(wmap, e.judgement) });
  }
  if (v1PendingHead) {
    for (const c of v1PendingHead.values()) list.push(c);
  }
  // backend 保证按 objectTimeMs 有序; 兜底项与同刻判定归并一下, 排序为 O(N log N) 保险
  list.sort((a, b) => a.t - b.t);

  const values = new Float64Array(buckets).fill(NaN);
  const den = v2 ? DEN_V2 : DEN_V1;
  let p = 0;
  let sum = 0;
  let k = 0;
  let min = Infinity;
  let max = -Infinity;
  let count = 0;
  for (let b = 0; b < buckets; ++b) {
    const end = ((b + 1) * durationMs) / buckets;
    while (p < list.length && list[p].t < end) {
      sum += list[p].w;
      ++k;
      ++p;
    }
    if (k > 0) {
      const v = sum / (den * k);
      values[b] = v;
      ++count;
      if (v < min) min = v;
      if (v > max) max = v;
    }
  }

  const curve: CacheEntry = {
    values,
    count,
    min: count ? min : 0,
    max: count ? max : 0,
    v2,
    durationMs,
    buckets,
  };
  cache.set(events, curve);
  return curve;
}
