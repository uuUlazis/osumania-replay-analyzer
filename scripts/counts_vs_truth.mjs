// counts_vs_truth.mjs — 引擎 counts vs 会话真值 counts (逐档 L1)
// 用法: node scripts/counts_vs_truth.mjs <anchorId> <truth.json> [tag]
import fs from 'fs';
import { withServer, measureMap } from '../build/v1_harness.mjs';

const ANCHOR = process.argv[2], TRUTH = process.argv[3], tag = process.argv[4] || 'run';
const inv = JSON.parse(fs.readFileSync('build/anchor_scores_inventory.json', 'utf8'));
const a = inv.anchors.find(x => x.anchorId === ANCHOR);
const ticks = String(a.identity.playedTicks);
const truth = JSON.parse(fs.readFileSync(TRUTH, 'utf8'));
// 会话 counts 序 = [彩, 300, 200, 100, 50, miss]
const T = truth.summary.counts;
const truthTier = { perfect: T[0], great: T[1], good: T[2], ok: T[3], meh: T[4], miss: T[5] };

await withServer(28951, async (base) => {
  const r = await measureMap(base, ticks);
  if (r.error) { console.log('ERR', r.error); return; }
  const c = r.summary.counts;
  const eng = { perfect: c.perfect, great: c.great, good: c.good, ok: c.ok, meh: c.meh, miss: c.miss };
  const keys = ['perfect', 'great', 'good', 'ok', 'meh', 'miss'];
  const d = {};
  let l1 = 0;
  for (const k of keys) { d[k] = (eng[k] ?? 0) - (truthTier[k] ?? 0); l1 += Math.abs(d[k]); }
  console.log('anchor', ANCHOR, '| tag', tag, '| env', JSON.stringify(Object.fromEntries(Object.entries(process.env).filter(([k]) => k.startsWith('MRA_')))));
  console.log('  engine', JSON.stringify(eng));
  console.log('  truth ', JSON.stringify(truthTier));
  console.log('  diff  ', JSON.stringify(d), '| L1 =', l1);
  fs.writeFileSync('build/tmp_accupdate/counts_' + tag + '.json', JSON.stringify({ anchor: ANCHOR, tag, engine: eng, truth: truthTier, diff: d, l1 }, null, 1));
});
