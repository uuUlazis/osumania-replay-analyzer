// re_ab_v1.mjs — 同一 binary 内 A/B 两配置的 V1 逐物件判定对比 (V1 stable 口径)
// 用法: node scripts/re_ab_v1.mjs <anchorId> [KEY=VAL[,...]]
//   A = 默认 env;  B = 附加 env (例: MRA_RE_V1_TAIL2=1)
import fs from 'fs';
import { withServer, measureMap } from '../build/v1_harness.mjs';

const anchor = process.argv[2];
const extra = {};
for (const kv of (process.argv[3] || '').split(',').filter(Boolean)) {
  const i = kv.indexOf('=');
  extra[kv.slice(0, i)] = kv.slice(i + 1);
}
const inv = JSON.parse(fs.readFileSync('build/anchor_scores_inventory.json', 'utf8'));
const a = inv.anchors.find(x => x.anchorId === anchor);
const ticks = String(a.identity.playedTicks);
const PORT = 28993;

async function run(env) {
  let out = null;
  await withServer(PORT, async (base) => { out = await measureMap(base, ticks); }, env);
  return out;
}
const A = await run({});
const B = await run(extra);
for (const [k, r] of [['A(default)', A], ['B(gated)', B]]) {
  if (r.error) { console.log(k, 'ERR', JSON.stringify(r.error)); process.exit(1); }
}
// V1 stable 口径: rice 用 head judgement, LN 用 tail.v1Judgement (无 hold 记录 ⇒ 缺计)
function tally(r) {
  const m = new Map();
  const byObj = new Map();
  for (const h of r.hits || []) {
    const e = byObj.get(h.objectIndex) || {};
    if (h.phase === 'hold_tail') e.tail = h; else if (!e.head) e.head = h;
    byObj.set(h.objectIndex, e);
  }
  for (const o of r.objects || []) {
    const e = byObj.get(o.index);
    if (o.type === 'hold') {
      if (!e || !e.tail) { m.set(o.index, 'none'); continue; }
      m.set(o.index, e.tail.v1Judgement || e.tail.judgement);
    } else {
      m.set(o.index, e && e.head ? e.head.judgement : 'none');
    }
  }
  return m;
}
const MAP = { MAX: 'perfect', 300: 'great', 200: 'good', 100: 'ok', 50: 'meh', MISS: 'miss' };
function truthRate(t, truthFile, objs) {
  const truth = JSON.parse(fs.readFileSync(truthFile, 'utf8'));
  const objOf = new Map((objs || []).map(o => [o.index, o]));
  let agree = 0, total = 0;
  const conf = {};
  for (const n of truth.notes) {
    const o = objOf.get(n.index);
    if (!o || !n.game || n.game.judge == null) continue;
    const exp = MAP[n.game.judge];
    const got = t.get(n.index) ?? 'none';
    total++;
    if (got === exp) agree++; else conf[exp + '->' + got] = (conf[exp + '->' + got] || 0) + 1;
  }
  return { total, agree, rate: +(agree / total * 100).toFixed(3), conf };
}
const tA = tally(A), tB = tally(B);
const TRUTH = process.argv[4];
if (TRUTH) {
  const ra = truthRate(tA, TRUTH, A.objects), rb = truthRate(tB, TRUTH, B.objects);
  console.log('truthA', JSON.stringify(ra));
  console.log('truthB', JSON.stringify(rb));
}
const chg = {};
let n = 0;
for (const [oi, va] of tA) {
  const vb = tB.get(oi);
  if (va !== vb) { const k = va + '->' + vb; chg[k] = (chg[k] || 0) + 1; n++; }
}
console.log('anchor', anchor, 'envB', JSON.stringify(extra));
console.log('counts A', JSON.stringify(A.summary.counts));
console.log('counts B', JSON.stringify(B.summary.counts));
console.log('V1 unit changes', n, JSON.stringify(chg));
const objOf = new Map((A.objects || []).map(o => [o.index, o]));
let shown = 0;
for (const [oi, va] of tA) {
  const vb = tB.get(oi);
  if (va !== vb && shown < 25) {
    const o = objOf.get(oi) || {};
    console.log('  oi', oi, va, '->', vb, 'start', o.startTimeMs, 'end', o.endTimeMs, 'type', o.type, 'col', o.column);
    shown++;
  }
}
fs.writeFileSync('build/tmp_accupdate/abv1_' + anchor + '.json', JSON.stringify({
  anchor, extra, countsA: A.summary.counts, countsB: B.summary.counts,
  changes: n, byKind: chg }, null, 1));
