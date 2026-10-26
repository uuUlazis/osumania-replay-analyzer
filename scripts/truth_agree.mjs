// truth_agree.mjs — 通用逐物件真值闸门: 会话 game.judge vs 引擎 V1/V2 逐物件判定
// 用法: node scripts/truth_agree.mjs <anchorId> <truth.json> [tag]
// 输出: build/tmp_accupdate/agree_<tag>.json
import fs from 'fs';
import { withServer, measureMap } from '../build/v1_harness.mjs';

const ANCHOR = process.argv[2];
const TRUTH = process.argv[3];
const tag = process.argv[4] || (ANCHOR + '_run');
if (!ANCHOR || !TRUTH) { console.error('用法: node scripts/truth_agree.mjs <anchorId> <truth.json> [tag]'); process.exit(2); }

const inv = JSON.parse(fs.readFileSync('build/anchor_scores_inventory.json', 'utf8'));
const a = inv.anchors.find(x => x.anchorId === ANCHOR);
if (!a) { console.error('anchor not found: ' + ANCHOR); process.exit(2); }
const ticks = String(a.identity.playedTicks);

const MAP = { MAX: 'perfect', 300: 'great', 200: 'good', 100: 'ok', 50: 'meh', MISS: 'miss' };
const truth = JSON.parse(fs.readFileSync(TRUTH, 'utf8'));
const tNotes = new Map(truth.notes.map(n => [n.index, n]));
// V2 局 (pairwise 计数) 用 v2 逐物件: LN 头 + 尾 + rice
const countsSum = truth.summary.counts.reduce((x, y) => x + y, 0);
const isV2 = countsSum === truth.notes.length + truth.notes.filter(n => n.type === 'hold').length;

await withServer(28950, async (base) => {
  const r = await measureMap(base, ticks);
  if (r.error) { console.log('ERR', r.error); return; }
  const byObj = new Map();
  for (const h of r.hits) {
    if (!byObj.has(h.objectIndex)) byObj.set(h.objectIndex, {});
    const e = byObj.get(h.objectIndex);
    if (h.phase === 'hold_tail') e.tail = h;
    else if (!e.head) e.head = h;
  }
  const conf = {};
  const mism = [];
  let agree = 0, total = 0, noTruth = 0;
  for (const o of r.objects) {
    const t = tNotes.get(o.index);
    if (!t || !t.game || t.game.judge == null) { noTruth++; continue; }
    const exp = MAP[t.game.judge] || null;
    const e = byObj.get(o.index);
    const got = o.type === 'hold'
      ? (e && e.tail ? (e.tail.v1Judgement || e.tail.judgement) : 'miss')
      : (e && e.head ? e.head.judgement : 'miss');
    total++;
    if (got === exp) { agree++; continue; }
    conf[exp + '->' + got] = (conf[exp + '->' + got] || 0) + 1;
    mism.push({
      i: o.index, col: o.column, type: o.type, start: o.startTimeMs, end: o.endTimeMs,
      truth: t.game.judge, engine: got,
      headOff: e && e.head ? +(e.head.actualTimeMs - o.startTimeMs) : null,
      tailErr: t.game.tailError, headJudge: t.game.headJudge, headSrc: t.game.headSrc,
    });
  }
  const rate = total ? agree / total * 100 : 0;
  console.log('anchor', ANCHOR, '| tag', tag, '| env', JSON.stringify(Object.fromEntries(Object.entries(process.env).filter(([k]) => k.startsWith('MRA_')))));
  console.log('对象', total, '一致', agree, '(' + rate.toFixed(3) + '%)', '| 无真值', noTruth);
  const top = Object.entries(conf).sort((x, y) => y[1] - x[1]);
  for (const [k, v] of top.slice(0, 8)) console.log('   ', k, v);
  fs.writeFileSync('build/tmp_accupdate/agree_' + tag + '.json', JSON.stringify({
    anchor: ANCHOR, tag, truthFile: TRUTH, at: new Date().toISOString(),
    env: Object.fromEntries(Object.entries(process.env).filter(([k]) => k.startsWith('MRA_'))),
    total, agree, rate: +rate.toFixed(4), noTruth,
    confusion: Object.fromEntries(top), mismatches: mism,
  }, null, 1));
  console.log('wrote build/tmp_accupdate/agree_' + tag + '.json');
});
