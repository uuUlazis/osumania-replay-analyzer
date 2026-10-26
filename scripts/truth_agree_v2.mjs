// truth_agree_v2.mjs — V2 (ScoreV2, pairwise) 逐判定点真值闸门
// 用法: node scripts/truth_agree_v2.mjs <anchorId> <truth.json> [tag]
// 对照口径 (VERDICT §5): pairwise 真值 = LN 头(game.headJudge) + LN 尾(game.judge) + rice(game.judge)
// 输出: build/tmp_accupdate/agreev2_<tag>.json
import fs from 'fs';
import { withServer, measureMap } from '../build/v1_harness.mjs';

const ANCHOR = process.argv[2], TRUTH = process.argv[3], tag = process.argv[4] || 'run';
if (!ANCHOR || !TRUTH) { console.error('用法: node scripts/truth_agree_v2.mjs <anchorId> <truth.json> [tag]'); process.exit(2); }
const inv = JSON.parse(fs.readFileSync('build/anchor_scores_inventory.json', 'utf8'));
const a = inv.anchors.find(x => x.anchorId === ANCHOR);
if (!a) { console.error('anchor not found'); process.exit(2); }
const ticks = String(a.identity.playedTicks);
const MAP = { MAX: 'perfect', 300: 'great', 200: 'good', 100: 'ok', 50: 'meh', MISS: 'miss' };
const truth = JSON.parse(fs.readFileSync(TRUTH, 'utf8'));
const tNotes = new Map(truth.notes.map(n => [n.index, n]));

await withServer(28955, async (base) => {
  const r = await measureMap(base, ticks);
  if (r.error) { console.log('ERR', r.error); return; }
  // 引擎 V2: hold_head / hold_tail / note_head 各自独立
  const pts = new Map(); // key: objIdx|phase
  for (const h of r.hitsV2 || []) {
    const key = h.objectIndex + '|' + h.phase;
    if (!pts.has(key)) pts.set(key, h);
  }
  const conf = {}, mism = [];
  let agree = 0, total = 0, missing = 0;
  const objOf = new Map(r.objects.map(o => [o.index, o]));
  for (const [idx, t] of tNotes) {
    const o = objOf.get(idx); if (!o) continue;
    const g = t.game || {};
    const push = (phase, truthName) => {
      if (truthName == null) { missing++; return; }
      const exp = MAP[truthName]; const e = pts.get(idx + '|' + phase);
      const got = e ? e.judgement : 'miss';
      total++;
      if (got === exp) { agree++; return; }
      conf[exp + '->' + got] = (conf[exp + '->' + got] || 0) + 1;
      mism.push({ i: idx, phase, type: o.type, col: o.column, start: o.startTimeMs, end: o.endTimeMs,
        truth: truthName, engine: got, engineOff: e && e.offsetMs, truthErr: phase === 'hold_tail' ? g.tailError : g.headError, headSrc: g.headSrc });
    };
    if (o.type === 'hold') { push('hold_head', g.headJudge); push('hold_tail', g.judge); }
    else push('note_head', g.judge);
  }
  const rate = total ? agree / total * 100 : 0;
  console.log('anchor', ANCHOR, '| tag', tag, '| env', JSON.stringify(Object.fromEntries(Object.entries(process.env).filter(([k]) => k.startsWith('MRA_')))));
  console.log('判定点', total, '一致', agree, '(' + rate.toFixed(3) + '%)', '| 真值缺失', missing);
  const top = Object.entries(conf).sort((x, y) => y[1] - x[1]);
  for (const [k, v] of top.slice(0, 10)) console.log('   ', k, v);
  fs.writeFileSync('build/tmp_accupdate/agreev2_' + tag + '.json', JSON.stringify({
    anchor: ANCHOR, tag, truthFile: TRUTH, at: new Date().toISOString(),
    env: Object.fromEntries(Object.entries(process.env).filter(([k]) => k.startsWith('MRA_'))),
    points: total, agree, rate: +rate.toFixed(4), missingTruth: missing,
    confusion: Object.fromEntries(top), mismatches: mism,
  }, null, 1));
  console.log('wrote build/tmp_accupdate/agreev2_' + tag + '.json');
});
