// bz_capture_postcheck.mjs — bz 读取器会话回收校验 + 13 例真值提取
// 用法: node scripts/bz_capture_postcheck.mjs "<reader-json>"
// 产出: build/tmp_accupdate/bz_truth_13.json (供后续判定侧定性)
import fs from 'fs';
import path from 'path';

const FILE = process.argv[2];
if (!FILE) { console.error('用法: node scripts/bz_capture_postcheck.mjs "<reader-json>"'); process.exit(2); }
if (!fs.existsSync(FILE)) { console.error('文件不存在: ' + FILE); process.exit(2); }

const EXPECT = {
  beatmapId: 1174605,
  md5Prefix: '6ef7c488',
  // 会话 summary.counts 序 = [彩, 300, 200/100?, ...] —— 实测为 [geki, n300, katu, n100, n50, miss]
  // (acc-semantics §2 已记「计数顺序不一」); 因此按**元素多重集 + 总量**核对, 并允许 katu/miss
  // 的 ±2 落盘差 (dan12 曾差 2)。
  countsMultiset: [3349, 236, 104, 2601, 740, 240],
  notes: 7270,
};
// I-4 归属闸门残余 13 例 (6 源 + 7 下游) + bz fiveUncapped 参照
const TARGETS = [1394, 1401, 2770, 4635, 4690, 4696, 4700, 4706, 5011, 5017, 5022, 5028, 5686];
const FIVE_UNCAPPED = [1394, 2770, 4635, 4690, 5686];

let bad = 0;
const ok = (cond, label, detail = '') => {
  console.log((cond ? '  [PASS] ' : '  [FAIL] ') + label + (detail ? ' — ' + detail : ''));
  if (!cond) bad++;
};

const d = JSON.parse(fs.readFileSync(FILE, 'utf8'));
const bm = d.beatmap || {};
const sum = d.summary || {};
const notes = d.notes || [];

console.log('=== bz 会话回收校验 ===\n[1] 身份');
ok(bm.beatmapId === EXPECT.beatmapId, 'beatmapId = ' + EXPECT.beatmapId, '实际 ' + bm.beatmapId);
ok(String(path.basename(FILE)).toLowerCase().includes(EXPECT.md5Prefix), '文件名含 md5 前缀 ' + EXPECT.md5Prefix, path.basename(FILE));
console.log('         ' + [bm.artist, bm.title, bm.version].filter(Boolean).join(' - ') + ' | ' + (bm.keys || '?') + 'K OD' + (bm.od ?? '?'));

console.log('\n[2] 计数 (证明播的是锚定那一把)');
const c = sum.counts || [];
const total = (a) => a.reduce((x, y) => x + y, 0);
const sortedEq = (a, b) => JSON.stringify([...a].sort((x, y) => x - y)) === JSON.stringify([...b].sort((x, y) => x - y));
const needCore = [3349, 2601, 236, 104];           // n300 / geki / n100 / n50 必须精确出现
const hasCore = Array.isArray(c) && c.length === 6 && needCore.every(v => c.includes(v));
const rest = Array.isArray(c) && c.length === 6 ? [...c].filter(v => !needCore.includes(v)) : [];
const pairOk = rest.length === 2 &&
  ((Math.abs(rest[0] - 740) <= 2 && Math.abs(rest[1] - 240) <= 2) ||
   (Math.abs(rest[0] - 240) <= 2 && Math.abs(rest[1] - 740) <= 2));
const okCounts = Array.isArray(c) && c.length === 6 &&
  (sortedEq(c, EXPECT.countsMultiset) || (hasCore && total(c) === 7270 && pairOk));
ok(okCounts, 'counts 与锚定局一致 (多重集; 容许 katu/miss ±2 落盘差)',
  '实际 ' + (Array.isArray(c) ? c.join('/') : '缺失'));
if (Array.isArray(c) && !sortedEq(c, EXPECT.countsMultiset)) {
  console.log('         注: 与锚点记录差 1 个 200↔miss (落盘 vs 内存直读, 已知现象; 逐物件真值不受影响)');
}
console.log('         acc ' + (sum.acc ?? '?') + ' | maxCombo ' + (sum.maxCombo ?? '?') + ' | countsSource ' + (sum.countsSource || '?'));

console.log('\n[3] 逐物件覆盖');
ok(notes.length === EXPECT.notes, 'notes = ' + EXPECT.notes, '实际 ' + notes.length);
const withGame = notes.filter((n) => n.game && (n.game.judge != null || n.game.raw != null));
ok(notes.length > 0 && withGame.length / notes.length >= 0.99, 'game 层覆盖 >= 99%',
  (withGame.length + '/' + notes.length + ' = ' + (notes.length ? (withGame.length / notes.length * 100).toFixed(2) : '0') + '%'));
const headSrc = {};
for (const n of withGame) { const s = (n.game && n.game.headSrc) || 'null'; headSrc[s] = (headSrc[s] || 0) + 1; }
console.log('         headSrc 分布: ' + JSON.stringify(headSrc));

console.log('\n[4] 目标物件真值 (13 残余 + 5 fiveUncapped)');
const byIdx = new Map(notes.map((n) => [n.index, n]));
const rows = [];
let missing = 0;
console.log('  idx    type  col  start     game.judge  headJudge  raw  done  tailErr  headSrc');
for (const i of TARGETS) {
  const n = byIdx.get(i);
  if (!n) { console.log('  ' + i + '  <缺失>'); missing++; continue; }
  const g = n.game || {};
  rows.push({ index: i, type: n.type, column: n.column, start: n.startTime, end: n.endTime, game: g, fiveUncapped: FIVE_UNCAPPED.includes(i) });
  console.log('  ' + String(i).padEnd(6) + String(n.type).padEnd(6) + String(n.column).padEnd(5) + String(n.startTime).padEnd(10) +
    String(g.judge ?? '-').padEnd(12) + String(g.headJudge ?? '-').padEnd(11) + String(g.raw ?? '-').padEnd(5) +
    String(g.done ?? '-').padEnd(6) + String(g.tailError ?? '-').padEnd(9) + String(g.headSrc ?? '-'));
}
ok(missing === 0, '目标物件全部存在且带 game 层', missing ? missing + ' 个缺失' : '');

const outPath = 'build/tmp_accupdate/bz_truth_13.json';
if (bad === 0) {
  fs.writeFileSync(outPath, JSON.stringify({
    source: FILE,
    generatedAt: new Date().toISOString(),
    beatmap: bm,
    counts: c,
    acc: sum.acc,
    notesTotal: notes.length,
    gameCoverage: notes.length ? +(withGame.length / notes.length).toFixed(4) : 0,
    headSrc,
    targets: rows,
  }, null, 1));
  console.log('\n写出 ' + outPath);
} else {
  console.log('\n(未通过, 不写出 ' + outPath + ' — 避免污染)');
}

console.log('\n=== 结果: ' + (bad === 0 ? '通过, 可以进入判定侧定性' : bad + ' 项未通过') + ' ===');
if (bad === 0) {
  console.log('后续 (主会话执行): 依据 targets 的 game.judge/headJudge 给 13 例定性 → 决定幽灵重按的判定侧语义 → 门控改引擎 → 过归属闸门 + V1 六锚点。');
}
process.exitCode = bad === 0 ? 0 : 1;
