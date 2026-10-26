// re_a12_diag.mjs — A12 (ActiveObjects 切片) 取证: 用 MRA_RE_ACTIVE=2 让引擎
// 在全量候选与切片候选之间做逐帧对比, 统计 lo_excluded / cand_diff。
// 用法: node scripts/re_a12_diag.mjs [tag]
import fs from 'fs';
import { withServer, measureMap } from '../build/v1_harness.mjs';

const tag = process.argv[2] || 'diag';
const inv = JSON.parse(fs.readFileSync('build/anchor_scores_inventory.json', 'utf8'));
const A = (id) => inv.anchors.find(x => x.anchorId === id);
const MAPS = [
  ['xfpsb_5th_beyond', 'test/sample/xfpsb ln 5th dan.json'],
  ['lndan13_yoru_underjoy_0313', 'test/sample/4k lndan 13th-yoru.json'],
  ['twinbloom', 'test/sample/twin bloom.json'],
  ['negai_1001', 'test/sample/new/2026-10-16_122337_967fdec0.json'],
  ['lndan12_yuugure_2304', 'test/sample/4k lndan 12th-yuugure.json'],
  ['trueblue_new', 'test/sample/new/2026-10-16_125959_a5af1629.json'],
];
const DUMP = 'build/tmp_accupdate/activediag_' + tag + '.txt';
try { fs.unlinkSync(DUMP); } catch {}
const out = {};
for (const [id] of MAPS) {
  await withServer(28996, async (base) => {
    const r = await measureMap(base, String(A(id).identity.playedTicks));
    if (r.error) { out[id] = { error: r.error }; return; }
    const c = r.summary.counts;
    out[id] = { counts: c };
  });
}
// 解析 dump
const lines = fs.existsSync(DUMP) ? fs.readFileSync(DUMP, 'utf8').split('\n') : [];
const diffs = lines.filter(l => l.startsWith('ACTIVEDIFF')).length;
const sums = lines.filter(l => l.startsWith('ACTIVESUMMARY'));
out._dumped = DUMP;
out._activeDiffLines = diffs;
out._summaries = sums;
console.log('ACTIVEDIFF lines', diffs);
for (const s of sums) console.log(s);
fs.writeFileSync('build/tmp_accupdate/re_a12_diag_' + tag + '.json', JSON.stringify(out, null, 1));
