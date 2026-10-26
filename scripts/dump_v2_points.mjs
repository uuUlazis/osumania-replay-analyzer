// dump_v2_points.mjs — 导出引擎 V2 逐点档位 + 物件基础信息 (供 oracle 分析)
// 用法: node scripts/dump_v2_points.mjs <anchorId> <outTag>
import fs from 'fs';
import { withServer, measureMap } from '../build/v1_harness.mjs';

const ANCHOR = process.argv[2], TAG = process.argv[3] || 'dump';
const inv = JSON.parse(fs.readFileSync('build/anchor_scores_inventory.json', 'utf8'));
const a = inv.anchors.find(x => x.anchorId === ANCHOR);
const ticks = String(a.identity.playedTicks);

await withServer(28995, async (base) => {
  const r = await measureMap(base, ticks);
  if (r.error) { console.log('ERR', r.error); return; }
  const pts = {};
  for (const h of r.hitsV2 || []) {
    if (!pts[h.objectIndex]) pts[h.objectIndex] = {};
    // 同名相位保留首个
    if (!pts[h.objectIndex][h.phase]) pts[h.objectIndex][h.phase] = { j: h.judgement, off: h.offsetMs, t: h.actualTimeMs };
  }
  const objs = r.objects.map(o => ({ i: o.index, col: o.column, type: o.type, start: o.startTimeMs, end: o.endTimeMs }));
  fs.writeFileSync('build/tmp_accupdate/v2points_' + TAG + '.json', JSON.stringify({
    anchor: ANCHOR, counts: r.summary.counts, accV2: r.summary.accuracy.scoreV2, points: pts, objects: objs,
  }));
  console.log('wrote build/tmp_accupdate/v2points_' + TAG + '.json | objects', objs.length, '| points objects', Object.keys(pts).length);
});
