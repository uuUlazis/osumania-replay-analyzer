// bz_13_engine_baseline.mjs — 预存 13 例目标物件在当前默认引擎下的判定与绑定 (采集回收后的对照基准)
// 用法: node scripts/bz_13_engine_baseline.mjs
import fs from 'fs';
import { withServer, measureMap } from '../build/v1_harness.mjs';

const TARGETS = [1394, 1401, 2770, 4635, 4690, 4696, 4700, 4706, 5011, 5017, 5022, 5028, 5686];
const FIVE = [1394, 2770, 4635, 4690, 5686];
const inv = JSON.parse(fs.readFileSync('build/anchor_scores_inventory.json', 'utf8'));
const ticks = String(inv.anchors.find(a => a.anchorId === 'bz_09x').identity.playedTicks);

await withServer(28930, async (base) => {
  const r = await measureMap(base, ticks);
  if (r.error) { console.log('ERR', r.error); return; }
  const objOf = new Map(r.objects.map(o => [o.index, o]));
  const heads = new Map();
  for (const h of r.hits) if (h.phase !== 'hold_tail' && !heads.has(h.objectIndex)) heads.set(h.objectIndex, h);
  const rows = TARGETS.map(i => {
    const o = objOf.get(i), h = heads.get(i);
    return {
      index: i, fiveUncapped: FIVE.includes(i),
      type: o && o.type, column: o && o.column, start: o && o.startTimeMs, end: o && o.endTimeMs,
      engineHeadTime: h ? h.actualTimeMs : null,
      engineJudgement: h ? h.judgement : null,
      engineOffset: h && o ? +(h.actualTimeMs - o.startTimeMs).toFixed(2) : null,
    };
  });
  rows.sort((a, b) => a.index - b.index);
  for (const x of rows) console.log(String(x.index).padEnd(6), String(x.type).padEnd(6), 'col', String(x.column).padEnd(3),
    'start', String(x.start).padEnd(8), 'engine', String(x.engineHeadTime).padEnd(8), String(x.engineJudgement).padEnd(8), 'off', x.engineOffset);
  fs.writeFileSync('build/tmp_accupdate/bz_13_engine_baseline.json', JSON.stringify({
    generatedAt: new Date().toISOString(), marker: 'bz_13_engine_baseline', rows,
  }, null, 1));
  console.log('wrote build/tmp_accupdate/bz_13_engine_baseline.json');
});
