// re_hits_diff2.mjs — 两配置的 V2/V1 事件多重集差异 (含重复事件, 不做去重)
// 用法: node scripts/re_hits_diff2.mjs <anchorId> <baseFile> [lo] [hi]
import fs from 'fs';
import { withServer, measureMap } from '../build/v1_harness.mjs';
const [anchor, baseFile, lo, hi] = [process.argv[2], process.argv[3],
  process.argv[4] ? +process.argv[4] : -1e9, process.argv[5] ? +process.argv[5] : 1e9];
const inv = JSON.parse(fs.readFileSync('build/anchor_scores_inventory.json', 'utf8'));
const a = inv.anchors.find(x => x.anchorId === anchor);
let r = null;
await withServer(28999, async (base) => { r = await measureMap(base, String(a.identity.playedTicks)); });
if (r.error) { console.log('ERR', JSON.stringify(r.error)); process.exit(1); }
const b = JSON.parse(fs.readFileSync(baseFile, 'utf8'));
const ser = (h) => h.objectIndex + '|' + h.phase + '|' + h.judgement + '|' + (h.v1Judgement ?? '-') + '|' + h.actualTimeMs;
const run = (list) => (list || []).map(ser);
const A2 = run(b.hitsV2), B2 = run(r.hitsV2);
const cnt = new Map();
for (const s of A2) cnt.set(s, (cnt.get(s) || 0) + 1);
for (const s of B2) cnt.set(s, (cnt.get(s) || 0) - 1);
const diffs = [...cnt.entries()].filter(([, v]) => v !== 0);
console.log('V2 event multiset diffs:', diffs.length);
for (const [k, v] of diffs.sort((x, y) => parseInt(x[0]) - parseInt(y[0]))) {
  const oi = parseInt(k);
  if (oi < lo || oi > hi) continue;
  const o = (r.objects || []).find(x => x.index === oi) || {};
  console.log(' ', k, 'delta', v, '| obj start', o.startTimeMs, 'end', o.endTimeMs, 'type', o.type, 'col', o.column);
}
const A1 = run(b.hits), B1 = run(r.hits);
const cnt1 = new Map();
for (const s of A1) cnt1.set(s, (cnt1.get(s) || 0) + 1);
for (const s of B1) cnt1.set(s, (cnt1.get(s) || 0) - 1);
const d1 = [...cnt1.entries()].filter(([, v]) => v !== 0);
console.log('V1 event multiset diffs:', d1.length);
for (const [k, v] of d1.sort((x, y) => parseInt(x[0]) - parseInt(y[0]))) {
  const oi = parseInt(k);
  if (oi < lo || oi > hi) continue;
  const o = (r.objects || []).find(x => x.index === oi) || {};
  console.log(' ', k, 'delta', v, '| obj start', o.startTimeMs, 'end', o.endTimeMs, 'type', o.type, 'col', o.column);
}
