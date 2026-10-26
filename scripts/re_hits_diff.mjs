// re_hits_diff.mjs — 两配置逐物件判定差异 (V1 + V2 head/tail)
// 用法: node scripts/re_hits_diff.mjs <anchorId> <tagA> <tagB>
//   tagA / tagB 各自读 build/tmp_accupdate/hits_<tag>_<anchor>.json (若不存在则现场测)
import fs from 'fs';
import { withServer, measureMap } from '../build/v1_harness.mjs';

const anchor = process.argv[2], tagA = process.argv[3], tagB = process.argv[4];
const inv = JSON.parse(fs.readFileSync('build/anchor_scores_inventory.json', 'utf8'));
const A = (id) => inv.anchors.find(x => x.anchorId === id);
const ticks = String(A(anchor).identity.playedTicks);
let r = null;
await withServer(28997, async (base) => { r = await measureMap(base, ticks); });
if (r.error) { console.log('ERR', JSON.stringify(r.error)); process.exit(1); }

const f = 'build/tmp_accupdate/hitsbase_' + anchor + '.json';
if (!fs.existsSync(f)) {
  fs.writeFileSync(f, JSON.stringify({ hits: r.hits, hitsV2: r.hitsV2, objects: r.objects, counts: r.summary.counts }));
  console.log('baseline captured ->', f);
}
const b = JSON.parse(fs.readFileSync(f, 'utf8'));
const key = (h) => h.objectIndex + '|' + h.phase;
const mA = new Map(), mB = new Map();
for (const h of b.hitsV2 || []) if (!mA.has(key(h))) mA.set(key(h), h);
for (const h of r.hitsV2 || []) if (!mB.has(key(h))) mB.set(key(h), h);
const v1A = new Map(), v1B = new Map();
for (const h of b.hits || []) if (!v1A.has(key(h))) v1A.set(key(h), h);
for (const h of r.hits || []) if (!v1B.has(key(h))) v1B.set(key(h), h);
const objOf = new Map((r.objects || []).map(o => [o.index, o]));
const NAME = { 0: 'perfect', 1: 'great', 2: 'good', 3: 'ok', 4: 'meh', 5: 'miss' };
const pick = (h) => h ? (h.v1Judgement ?? h.judgement) : 'none';
const allKeys = new Set([...mA.keys(), ...mB.keys()]);
let n = 0;
console.log('--- V2 head/tail diffs', anchor, '---');
for (const k of [...allKeys].sort((x, y) => parseInt(x) - parseInt(y))) {
  const a = mA.get(k), c = mB.get(k);
  if (pick(a) !== pick(c) || (a && c && a.actualTimeMs !== c.actualTimeMs)) {
    const oi = parseInt(k);
    const o = objOf.get(oi) || {};
    console.log(k.padEnd(18), 'A=', pick(a), '(' + (a ? a.actualTimeMs : '-') + ')',
      'B=', pick(c), '(' + (c ? c.actualTimeMs : '-') + ')',
      'obj start', o.startTimeMs, 'end', o.endTimeMs, 'type', o.type, 'col', o.column);
    n++;
  }
}
console.log('V2 diffs', n);
let m = 0;
console.log('--- V1 diffs ---');
for (const k of [...new Set([...v1A.keys(), ...v1B.keys()])].sort((x, y) => parseInt(x) - parseInt(y))) {
  const a = v1A.get(k), c = v1B.get(k);
  if (pick(a) !== pick(c)) {
    const oi = parseInt(k);
    const o = objOf.get(oi) || {};
    console.log(k.padEnd(18), 'A=', pick(a), 'B=', pick(c), 'obj', o.startTimeMs, o.endTimeMs, o.type, o.column);
    m++;
  }
}
console.log('V1 diffs', m);
console.log('counts A(base)', JSON.stringify(b.counts), '\ncounts B(cur) ', JSON.stringify(r.summary.counts));
