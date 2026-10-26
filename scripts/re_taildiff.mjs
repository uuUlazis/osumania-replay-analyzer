// re_taildiff.mjs — 同一谱面在两种 env 配置下的逐物件判定差异 (诊断用)
// 用法: node scripts/re_taildiff.mjs <anchorId> <truth.json> <tag> [ENV=VAL ...]
//   两个配置: 默认 (无 MRA_RE_*) vs 追加 argv 里的 env。
import fs from 'fs';
import { withServer, measureMap } from '../build/v1_harness.mjs';

const ANCHOR = process.argv[2], TRUTHF = process.argv[3], tag = process.argv[4] || 'diff';
const extra = {};
for (const a of process.argv.slice(5)) {
  const i = a.indexOf('=');
  if (i > 0) extra[a.slice(0, i)] = a.slice(i + 1);
}
const inv = JSON.parse(fs.readFileSync('build/anchor_scores_inventory.json', 'utf8'));
const a = inv.anchors.find(x => x.anchorId === ANCHOR);
const ticks = String(a.identity.playedTicks);
const truth = JSON.parse(fs.readFileSync(TRUTHF, 'utf8'));
const tNotes = new Map(truth.notes.map(n => [n.index, n]));
const MAP = { MAX: 'perfect', 300: 'great', 200: 'good', 100: 'ok', 50: 'meh', MISS: 'miss' };

const clean = Object.fromEntries(Object.entries(process.env).filter(([k]) => !(k in extra)));
let A, B;
await withServer(28971, async (base) => {
  const r = await measureMap(base, ticks);
  if (r.error) throw new Error(r.error);
  A = r;
}, clean);
await withServer(28972, async (base) => {
  const r = await measureMap(base, ticks);
  if (r.error) throw new Error(r.error);
  B = r;
}, { ...clean, ...extra });

const idx = (r) => {
  const m = new Map();
  for (const h of r.hitsV2 || []) {
    const k = h.objectIndex + '|' + h.phase;
    if (!m.has(k)) m.set(k, h);
  }
  return m;
};
const ia = idx(A), ib = idx(B);
const objs = new Map(A.objects.map(o => [o.index, o]));
const changes = [];
for (const [k, hb] of ib) {
  const ha = ia.get(k);
  const ja = ha ? ha.judgement : 'missing';
  if (ja === hb.judgement) continue;
  const [oi, phase] = k.split('|');
  const o = objs.get(+oi);
  const t = tNotes.get(+oi);
  const g = (t && t.game) || {};
  changes.push({
    i: +oi, phase, type: o && o.type, col: o && o.column, start: o && o.startTimeMs, end: o && o.endTimeMs,
    from: ja, to: hb.judgement,
    offA: ha && ha.offsetMs, offB: hb.offsetMs, atA: ha && ha.actualTimeMs, atB: hb.actualTimeMs,
    truthHead: g.headJudge, truthJudge: g.judge, headSrc: g.headSrc,
  });
}
const conf = {};
for (const c of changes) conf[c.phase + ':' + c.from + '->' + c.to] = (conf[c.phase + ':' + c.from + '->' + c.to] || 0) + 1;
const countsA = A.summary.counts, countsB = B.summary.counts;
console.log('anchor', ANCHOR, 'env', JSON.stringify(extra));
console.log(' counts A', JSON.stringify(countsA));
console.log(' counts B', JSON.stringify(countsB));
console.log(' changes', changes.length, JSON.stringify(conf));
const score = (c) => {
  const t = c.phase === 'hold_tail' ? c.truthJudge : c.truthHead;
  const truthName = t == null ? null : MAP[t];
  const better = truthName && truthName === c.to;
  const worse = truthName && truthName === c.from;
  return (better ? 1 : 0) - (worse ? 1 : 0);
};
let net = 0;
for (const c of changes) net += score(c);
console.log(' net 修好-弄坏 =', net);
const show = changes.filter(c => score(c) !== 0 || process.argv.includes('--all')).slice(0, 40);
for (const c of show) console.log('  ', JSON.stringify(c), score(c) > 0 ? 'FIX' : score(c) < 0 ? 'BREAK' : 'neutral');
fs.writeFileSync('build/tmp_accupdate/re_taildiff_' + tag + '.json', JSON.stringify({ anchor: ANCHOR, extra, countsA, countsB, changes }, null, 1));
console.log('wrote build/tmp_accupdate/re_taildiff_' + tag + '.json');
