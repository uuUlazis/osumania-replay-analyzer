// bz_truth_agree.mjs — bz 逐物件真值闸门 (会话 game.judge  vs  引擎 V1 逐物件判定)
// 用法: node scripts/bz_truth_agree.mjs <tag> [truth.json]
// 输出: build/tmp_accupdate/bz_agree_<tag>.json + 控制台混淆矩阵/差异清单
import fs from 'fs';
import { withServer, measureMap } from '../build/v1_harness.mjs';

const tag = process.argv[2] || 'run';
const TRUTH = process.argv[3] || 'test/sample/new/2026-10-20_225315_6ef7c488.json';
const inv = JSON.parse(fs.readFileSync('build/anchor_scores_inventory.json', 'utf8'));
const ticks = String(inv.anchors.find(a => a.anchorId === 'bz_09x').identity.playedTicks);

const MAP = { MAX: 'perfect', 300: 'great', 200: 'good', 100: 'ok', 50: 'meh', MISS: 'miss' };
const truth = JSON.parse(fs.readFileSync(TRUTH, 'utf8'));
const tNotes = new Map(truth.notes.map(n => [n.index, n]));

await withServer(28940, async (base) => {
  const r = await measureMap(base, ticks);
  if (r.error) { console.log('ERR', r.error); return; }
  // 引擎 V1 最终判定: rice = head; hold = 最后一个 hold_tail 的 v1Judgement
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
    const e = byObj.get(o.index);
    const truthName = t && t.game ? t.game.judge : null;
    if (!truthName) { noTruth++; continue; }
    const exp = MAP[truthName] || null;
    const got = o.type === 'hold'
      ? (e && e.tail ? (e.tail.v1Judgement || e.tail.judgement) : 'miss')
      : (e && e.head ? e.head.judgement : 'miss');
    total++;
    if (got === exp) { agree++; continue; }
    const k = exp + '->' + got;
    conf[k] = (conf[k] || 0) + 1;
    mism.push({
      i: o.index, col: o.column, type: o.type, start: o.startTimeMs, end: o.endTimeMs,
      truth: truthName, engine: got,
      engineHead: e && e.head ? e.head.actualTimeMs : null,
      engineTail: e && e.tail ? e.tail.actualTimeMs : null,
      headOff: e && e.head ? +(e.head.actualTimeMs - o.startTimeMs) : null,
      tailErr: t.game.tailError, headJudge: t.game.headJudge, headSrc: t.game.headSrc,
    });
  }
  const rate = total ? agree / total * 100 : 0;
  console.log('tag', tag, '| env', JSON.stringify(Object.fromEntries(Object.entries(process.env).filter(([k]) => k.startsWith('MRA_')))));
  console.log('对象', total, '一致', agree, '(' + rate.toFixed(3) + '%)', '| 无真值', noTruth);
  console.log('不一致', mism.length);
  const top = Object.entries(conf).sort((a, b) => b[1] - a[1]);
  for (const [k, v] of top) console.log('   ', k, v);
  // bz 已知 13 例目标
  const T = [1394, 1401, 2770, 4635, 4690, 4696, 4700, 4706, 5011, 5017, 5022, 5028, 5686];
  const tset = T.map(i => mism.find(m => m.i === i)).filter(Boolean);
  console.log('13 例目标仍不一致:', tset.length + '/13', JSON.stringify(tset.map(m => m.i)));
  fs.writeFileSync('build/tmp_accupdate/bz_agree_' + tag + '.json', JSON.stringify({
    tag, at: new Date().toISOString(),
    env: Object.fromEntries(Object.entries(process.env).filter(([k]) => k.startsWith('MRA_'))),
    total, agree, rate: +rate.toFixed(4), noTruth,
    confusion: Object.fromEntries(top), mismatches: mism,
  }, null, 1));
  console.log('wrote build/tmp_accupdate/bz_agree_' + tag + '.json');
});
