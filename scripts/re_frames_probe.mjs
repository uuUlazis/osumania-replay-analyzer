// re_frames_probe.mjs — 打印某锚点在时间窗内的 replay 记录 (查同 ts / 边沿)
// 用法: node scripts/re_frames_probe.mjs <anchorId> <from> <to>
import fs from 'fs';
import { withServer, measureMap } from '../build/v1_harness.mjs';
const [anchor, from, to] = [process.argv[2], +process.argv[3], +process.argv[4]];
const inv = JSON.parse(fs.readFileSync('build/anchor_scores_inventory.json', 'utf8'));
const a = inv.anchors.find(x => x.anchorId === anchor);
await withServer(28998, async (base) => {
  const r = await measureMap(base, String(a.identity.playedTicks));
  if (r.error) { console.log('ERR', JSON.stringify(r.error)); return; }
  const fr = await (await fetch(base + '/api/replays/' + r.sid + '/frames?from=' + from + '&to=' + to + '&limit=1000')).json();
  let prev = null, prevTime = null;
  for (const f of fr.data.frames) {
    const dn = prev == null ? f.keys : (f.keys & ~prev);
    const up = prev == null ? 0 : (~f.keys & prev);
    const same = (prevTime != null && f.timeMs === prevTime);
    console.log('t=' + String(f.timeMs).padStart(8), 'keys=' + f.keys, 'dn=' + dn, 'up=' + up,
      same ? '  <== SAME TS AS PREV' : '');
    prev = f.keys; prevTime = f.timeMs;
  }
});
