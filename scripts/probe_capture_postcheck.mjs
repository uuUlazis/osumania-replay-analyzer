// probe_capture_postcheck.mjs — 探针采集回收校验 + 归属闸门直跑
// 用法: node scripts/probe_capture_postcheck.mjs <anchorId> <reader.json> <probe.jsonl>
import fs from 'fs';
import { execFileSync } from 'child_process';

const [ANCHOR, READER_JSON, PROBE_JSONL] = process.argv.slice(2);
if (!ANCHOR || !READER_JSON || !PROBE_JSONL) {
  console.error('用法: node scripts/probe_capture_postcheck.mjs <anchorId> <reader.json> <probe.jsonl>'); process.exit(2);
}
const inv = JSON.parse(fs.readFileSync('build/anchor_scores_inventory.json', 'utf8'));
const a = inv.anchors.find(x => x.anchorId === ANCHOR);
const c = a.counts;
const expectCounts = [c.n300, c.n100, c.n50, c.geki, c.katu, c.miss];

let bad = 0;
const ok = (cond, label, detail = '') => {
  console.log((cond ? '  [PASS] ' : '  [FAIL] ') + label + (detail ? ' — ' + detail : ''));
  if (!cond) bad++;
};

console.log('=== 回收校验: ' + ANCHOR + ' ===\n[1] 读取器会话');
ok(fs.existsSync(READER_JSON), 'reader JSON 存在', READER_JSON);
let notes = [];
if (fs.existsSync(READER_JSON)) {
  const d = JSON.parse(fs.readFileSync(READER_JSON, 'utf8'));
  const bm = d.beatmap || {}, sum = d.summary || {};
  notes = d.notes || [];
  ok(bm.beatmapId === a.chart.beatmapId, 'beatmapId = ' + a.chart.beatmapId, '实际 ' + bm.beatmapId);
  const sc = sum.counts || [];
  const multiset = (x) => JSON.stringify([...x].sort((p, q) => p - q));
  const totalOf = (x) => x.reduce((p, q) => p + q, 0);
  const core = [c.n300, c.geki, c.n100, c.n50];
  const rest = sc.filter(v => !core.includes(v));
  const pairOk = rest.length === 2 &&
    ((Math.abs(rest[0] - c.katu) <= 2 && Math.abs(rest[1] - c.miss) <= 2) ||
     (Math.abs(rest[0] - c.miss) <= 2 && Math.abs(rest[1] - c.katu) <= 2));
  // 会话 counts 与 .osr 落盘 counts 会有 ≤2 的重算差 (bz ±1 / 12th ±2 已知现象):
  // 允许 n300 与 miss 各 ±2, 其余(彩/200/100/50)必须精确。
  const near = (v, t) => Math.abs(v - t) <= 2;
  const okNear = sc.length === 6 && totalOf(sc) === totalOf(expectCounts) &&
    near(sc[1], c.n300) && near(sc[5], c.miss) &&
    sc[0] === c.geki && sc[2] === c.katu && sc[3] === c.n100 && sc[4] === c.n50;
  const okCounts = sc.length === 6 &&
    (multiset(sc) === multiset(expectCounts) || (core.every(v => sc.includes(v)) && totalOf(sc) === totalOf(expectCounts) && pairOk) || okNear);
  ok(okCounts, 'counts 与锚定局一致 (容许 200/MISS ±2 落盘差)', '实际 ' + sc.join('/'));
  const withGame = notes.filter(n => n.game && n.game.judge != null).length;
  ok(notes.length > 0 && withGame / notes.length >= 0.99, 'game 层覆盖 >= 99%', withGame + '/' + notes.length);
  console.log('         headSrc 分布: ' + JSON.stringify(notes.reduce((acc, n) => { const s = (n.game && n.game.headSrc) || 'none'; acc[s] = (acc[s] || 0) + 1; return acc; }, {})));
}

console.log('\n[2] 绑定探针');
ok(fs.existsSync(PROBE_JSONL), 'probe jsonl 存在', PROBE_JSONL);
if (fs.existsSync(PROBE_JSONL)) {
  const lines = fs.readFileSync(PROBE_JSONL, 'utf8').split('\n').filter(Boolean);
  let a4 = 0, ev = 0, hdrA = null, errs = 0;
  for (const s of lines) {
    if (s.indexOf('"hdr"') >= 0) { hdrA = s; continue; }
    if (s.indexOf('"error"') >= 0) { errs++; continue; }
    ev++;
    if (s.indexOf('0xA4') >= 0) a4++;
  }
  console.log('         行数 ' + lines.length + ' | 事件 ' + ev + ' | 0xA4 写入 ' + a4 + ' | 前置错误行 ' + errs);
  ok(a4 > 0, '探针采到 0xA4 写入', a4 + ' 条');
  ok(!!hdrA, 'hdr 存在 (range/interval 溯源)');
  if (hdrA) {
    const mo = /"objects":(\d+)/.exec(hdrA);
    const mw = /"watch":(\d+)/.exec(hdrA);
    console.log('         hdr: ' + hdrA.slice(0, 200));
    // 关键: 探针枚举到的物件数必须等于谱面物件数 —— 不等说明探针**晚挂**(play 已开始后才连上),
    // 此时索引基准错位, A4 归属数据作废, 必须重采。
    if (mo && notes.length) {
      const got = Number(mo[1]);
      ok(got === notes.length, 'hdr objects = 谱面物件数 (' + notes.length + ')', '实际 ' + got + (got < notes.length ? ' → 探针晚挂, 索引错位, 需重采' : ''));
    }
    if (mw) console.log('         hdr watch=' + mw[1]);
  }
}

console.log('\n[3] 归属闸门 (自动跑)');
if (bad === 0) {
  const out = execFileSync(process.execPath, ['scripts/bind_gate.mjs', ANCHOR, PROBE_JSONL, '--tag', 'cap_' + ANCHOR], { encoding: 'utf8' });
  console.log(out.trim().split('\n').slice(0, 10).join('\n'));
} else {
  console.log('         跳过 (上面有未通过项)');
}

console.log('\n=== 结果: ' + (bad === 0 ? '通过 → 可进入规则分析' : bad + ' 项未通过') + ' ===');
if (bad === 0) {
  console.log('后续: 用 build/tmp_accupdate/bind_gate_cap_' + ANCHOR + '.json 的 mismatches 做逐案定性;');
  console.log('      旧判据: A4 首写 = stable 绑定时刻; engine-only = stable 未绑而引擎绑 (LN 头归属族的正面证据)。');
}
process.exitCode = bad === 0 ? 0 : 1;
