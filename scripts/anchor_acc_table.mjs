// anchor_acc_table.mjs — 全部锚点的 标定 acc / 回放 acc / 系统 acc 对照表
// 用法: node scripts/anchor_acc_table.mjs [outTag]
// 输出: build/tmp_accupdate/anchor_acc_table[_tag].json + 控制台 markdown 表
import fs from 'fs';
import { withServer, measureMap } from '../build/v1_harness.mjs';

const tag = process.argv[2] || '';
const inv = JSON.parse(fs.readFileSync('build/anchor_scores_inventory.json', 'utf8'));

const rows = [];
await withServer(28970, async (base) => {
  const cache = new Map();
  for (const a of inv.anchors) {
    const row = {
      anchorId: a.anchorId,
      role: a.role,
      chart: `${a.chart.artist} - ${a.chart.title} [${a.chart.version}]`,
      keys: a.chart.keys, od: a.chart.od,
      countsN: (a.counts.n300 + a.counts.n100 + a.counts.n50 + a.counts.geki + a.counts.katu + a.counts.miss),
      calAcc: a.calibratedAcc ? a.calibratedAcc.value : null,
      calCaliber: a.calibratedAcc ? a.calibratedAcc.caliber : null,
      replayAcc: a.replayEndAcc ? (a.replayEndAcc.acc ?? null) : null,
      replaySrc: a.replayEndAcc ? (a.replayEndAcc.source ?? null) : null,
      oldDacc: a.ourShip ? a.ourShip.dACC : null,
    };
    const ticks = String(a.identity.playedTicks);
    try {
      let r = cache.get(ticks);
      if (!r) {
        for (let attempt = 0; attempt < 3; attempt++) {
          r = await measureMap(base, ticks);
          if (!r.error) break;
          await new Promise(res => setTimeout(res, 1500));
        }
        cache.set(ticks, r);
      }
      if (r.error) { row.error = r.error; rows.push(row); continue; }
      row.mode = r.summary.countMode;
      row.engV1 = r.summary.accuracy.scoreV1;
      row.engV2 = r.summary.accuracy.scoreV2;
      // 口径: V2 局用 accuracyV2, 其余用 V1 counts 公式
      const isV2 = (r.summary.countMode === 'scorev2' || r.summary.countMode === 'lazer');
      row.engAcc = isV2 ? row.engV2 : row.engV1;
      row.engCaliber = isV2 ? 'accuracyV2' : 'counts公式';
      row.dVsysCal = row.calAcc != null && row.engAcc != null ? +(row.engAcc - row.calAcc).toFixed(4) : null;
      // 回放 acc 只认 replayEndAcc.acc (stable 显示值); 旧 accV1/accV2 是引擎输出, 不当回放
      const rep = row.chart && a.replayEndAcc ? a.replayEndAcc.acc : null;
      row.replayUsed = rep ?? null;
      row.dVsysRep = rep != null && row.engAcc != null ? +(row.engAcc - rep).toFixed(4) : null;
      row.dRepCal = rep != null && row.calAcc != null ? +(rep - row.calAcc).toFixed(4) : null;
    } catch (e) { row.error = String(e).slice(0, 120); }
    rows.push(row);
  }
});

const j = (v) => (v == null ? '—' : v);
console.log('| # | 锚点 | 图 (K/OD) | 标定 acc | 回放 acc | 系统 acc | 系统−标定 | 系统−回放 | 回放−标定 |');
console.log('|---|---|---|---|---|---|---|---|---|');
rows.forEach((r, i) => {
  console.log(`| ${i + 1} | ${r.anchorId} | ${r.keys}K/OD${r.od} | ${j(r.calAcc)} | ${j(r.replayUsed)} | ${j(r.engAcc)} | ${j(r.dVsysCal)} | ${j(r.dVsysRep)} | ${j(r.dRepCal)} |`);
});
const errs = rows.filter(r => r.error);
if (errs.length) console.log('\n未测出: ' + errs.map(e => e.anchorId + '(' + e.error + ')').join(', '));
fs.writeFileSync('build/tmp_accupdate/anchor_acc_table' + (tag ? '_' + tag : '') + '.json', JSON.stringify({ generatedAt: new Date().toISOString(), rows }, null, 1));
console.log('\nwrote build/tmp_accupdate/anchor_acc_table' + (tag ? '_' + tag : '') + '.json');
