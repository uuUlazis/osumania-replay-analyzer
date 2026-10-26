// probe_capture_preflight.mjs — 0xA4 绑定探针采集预检 (通用)
// 用法: node scripts/probe_capture_preflight.mjs <anchorId> [notes] [durationMs]
//   例: node scripts/probe_capture_preflight.mjs xfpsb_5th_beyond 8937 520000
import fs from 'fs';
import net from 'net';
import { execSync } from 'child_process';

const ROOT = process.cwd();
const READER = 'C:/Users/19677/Desktop/workspace/osu-mania-live-reader';
const PROBE = READER + '/tools/bindprobe.exe';
const ANCHOR = process.argv[2];
const NOTES = process.argv[3];
const DUR = process.argv[4] || '520000';
if (!ANCHOR) { console.error('用法: node scripts/probe_capture_preflight.mjs <anchorId> [notes] [durationMs]'); process.exit(2); }

const inv = JSON.parse(fs.readFileSync('build/anchor_scores_inventory.json', 'utf8'));
const a = inv.anchors.find(x => x.anchorId === ANCHOR);
if (!a) { console.error('anchor not found: ' + ANCHOR); process.exit(2); }
const md5 = a.identity.md5.toLowerCase();
// playedTicks > 2^53, 必须用 BigInt 才能得到准确的 filetime
const ft = (BigInt(a.identity.playedTicks) - 504911232000000000n).toString();
const OSR = `C:/osu!/Data/r/${md5}-${ft}.osr`;
const c = a.counts;
const countsStr = [c.n300, c.n100, c.n50, c.geki, c.katu, c.miss].join(' / ');

let bad = 0;
const ok = (cond, label, detail = '') => {
  console.log((cond ? '  [PASS] ' : '  [FAIL] ') + label + (detail ? ' — ' + detail : ''));
  if (!cond) bad++;
};
const tcp = (port) => new Promise((res) => {
  const s = net.connect({ port, host: '127.0.0.1' });
  const done = (v) => { try { s.destroy(); } catch {} res(v); };
  s.setTimeout(1200);
  s.once('connect', () => done(true));
  s.once('timeout', () => done(false));
  s.once('error', () => done(false));
});

console.log('=== 0xA4 探针采集预检: ' + ANCHOR + ' ===');
console.log('\n[1] 运行环境');
ok(Number(process.versions.node.split('.')[0]) >= 22, 'Node.js >= 22', 'v' + process.versions.node);
let osuRunning = false;
try { osuRunning = /osu!\.exe/i.test(execSync('tasklist /FI "IMAGENAME eq osu!.exe"', { encoding: 'utf8' })); } catch {}
ok(osuRunning, 'osu!stable 在运行', osuRunning ? '' : '请启动 osu! 并停在 mania 菜单');
ok(await tcp(24050), 'tosu 24050 可达', '');

console.log('\n[2] 工具与回放');
ok(fs.existsSync(OSR), '锚定回放存在', OSR);
ok(fs.existsSync(PROBE), 'bindprobe.exe 存在', PROBE);
ok(fs.existsSync(READER + '/src/index.mjs'), '读取器入口存在');
ok(fs.existsSync(READER + '/output'), '读取器 output 目录存在');

console.log('\n[3] 目标局身份 (播放时必须一致)');
console.log('         图         : ' + a.chart.artist + ' - ' + a.chart.title + ' [' + a.chart.version + ']');
console.log('         beatmapId  : ' + a.chart.beatmapId + ' | ' + a.chart.keys + 'K OD' + a.chart.od);
console.log('         md5        : ' + md5);
console.log('         playedTicks: ' + a.identity.playedTicks + '  (' + (a.identity.playDate || '?') + ')');
console.log('         counts     : ' + countsStr + '  (300/100/50/彩/200/MISS)');
if (NOTES) console.log('         notes      : ' + NOTES);

console.log('\n=== 结果: ' + (bad === 0 ? '环境就绪' : bad + ' 项未通过') + ' ===');
if (bad === 0) {
  const notes = NOTES || '<notes>';
  console.log('\n采集步骤 (顺序不要变, 两条命令分别在**两个 cmd 窗口**里常驻):');
  console.log('  ① 读取器 (窗口 A):');
  console.log('     cd /d ' + READER.replace(/\//g, '\\'));
  console.log('     node src\\index.mjs --once --log output\\reader_' + ANCHOR + '.log');
  console.log('  ② 绑定探针 (窗口 B, 全图 0xA4 范围模式; >2048 索引自动放宽到 20ms):');
  console.log('     cd /d ' + READER.replace(/\//g, '\\'));
  console.log(`     tools\\bindprobe.exe osu! watchobj 20 ${DUR} 0 ${notes} > output\\bind_watchobj_${ANCHOR}.jsonl`);
  console.log('  ③ 在 osu! 里播放锚定回放 (Local Scores 里该局, 或把 .osr 拖进 osu! 窗口)');
  console.log('     图: ' + a.chart.title + ' [' + a.chart.version + ']');
  console.log('  ④ 看完整首, 等结算; 两个窗口 Ctrl+C');
  console.log('  ⑤ 回仓校验:');
  console.log('     node scripts/probe_capture_postcheck.mjs ' + ANCHOR + ' <reader.json> "' + READER.replace(/\//g, '\\') + '\\output\\bind_watchobj_' + ANCHOR + '.jsonl"');
}
process.exitCode = bad === 0 ? 0 : 1;
