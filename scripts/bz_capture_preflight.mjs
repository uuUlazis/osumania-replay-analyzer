// bz_capture_preflight.mjs — bz 读取器真值采集前的环境预检 (只读)
// 用法: node scripts/bz_capture_preflight.mjs
import fs from 'fs';
import net from 'net';
import { execSync } from 'child_process';

const ROOT = process.cwd();
const READER = 'C:/Users/19677/Desktop/workspace/osu-mania-live-reader';
const OSR = 'C:/osu!/Data/r/6ef7c4884793d33d2f21f33f1329605c-134355086056308553.osr';
const EXPECT = {
  anchor: 'bz_09x',
  beatmapId: 1174605,
  md5Prefix: '6ef7c488',
  playedTicks: '639266318056308553',
  counts300_100_50_geki_katu_miss: [3349, 236, 104, 2601, 740, 240],
};

let bad = 0;
const ok = (cond, label, detail = '') => {
  console.log((cond ? '  [PASS] ' : '  [FAIL] ') + label + (detail ? ' — ' + detail : ''));
  if (!cond) bad++;
};

const tcp = (port, host = '127.0.0.1') => new Promise((res) => {
  const s = net.connect({ port, host });
  const done = (v) => { try { s.destroy(); } catch {} res(v); };
  s.setTimeout(1200);
  s.once('connect', () => done(true));
  s.once('timeout', () => done(false));
  s.once('error', () => done(false));
});

console.log('=== bz 真值采集预检 ===');
console.log('\n[1] 运行环境');
const nodeMajor = Number(process.versions.node.split('.')[0]);
ok(nodeMajor >= 22, 'Node.js >= 22', 'v' + process.versions.node);
let osuRunning = false;
try {
  const out = execSync('tasklist /FI "IMAGENAME eq osu!.exe"', { encoding: 'utf8' });
  osuRunning = /osu!\.exe/i.test(out);
} catch {}
ok(osuRunning, 'osu!stable 进程在运行', osuRunning ? '' : '请先启动 osu! 并停在 mania 菜单');
const tosu = await tcp(24050);
ok(tosu, 'tosu 端口 24050 可达', tosu ? '' : '请先启动 tosu (C:\\Ulazis\\Tools\\osu!\\tosu)');

console.log('\n[2] 回放与工具');
ok(fs.existsSync(OSR), '锚定回放 .osr 存在', OSR);
if (fs.existsSync(OSR)) console.log('         大小 ' + fs.statSync(OSR).size + ' bytes');
ok(fs.existsSync(READER + '/src/index.mjs'), '读取器入口存在', READER + '/src/index.mjs');
const outDir = READER + '/output';
ok(fs.existsSync(outDir), '读取器输出目录存在', outDir);
let writable = false;
try { fs.accessSync(outDir, fs.constants.W_OK); writable = true; } catch {}
ok(writable, '读取器输出目录可写');

console.log('\n[3] 目标局身份 (播放时必须完全一致)');
console.log('         anchor      : ' + EXPECT.anchor);
console.log('         beatmapId   : ' + EXPECT.beatmapId + '  (Blue Zenith / JINDIMENSIONS 0.9x (180bpm) OD5 / 7K / OD5)');
console.log('         md5 前缀    : ' + EXPECT.md5Prefix);
console.log('         playedTicks : ' + EXPECT.playedTicks + '  (对局时间 2026-10-03 21:43:25 +08:00)');
console.log('         counts      : ' + EXPECT.counts300_100_50_geki_katu_miss.join(' / ') + '  (300/100/50/彩/200/miss, N=7270)');

console.log('\n=== 结果: ' + (bad === 0 ? '全部通过, 可以开始采集' : bad + ' 项未通过, 请先修复') + ' ===');
if (bad === 0) {
  console.log('\n下一步 (顺序不要变):');
  console.log('  ① 新开 cmd:  cd ' + READER.replace(/\//g, '\\'));
  console.log('              node src\\index.mjs --once --log output\\reader_bz.log');
  console.log('  ② 在 osu! 里播放锚定回放 (Local Scores 2026-10-03 21:43, 或把 .osr 拖进窗口)');
  console.log('  ③ 看完整首 → 等结算 → Ctrl+C 关读取器');
  console.log('  ④ 拷 output\\2026-*_6ef7c488.json 到 ' + ROOT + '\\test\\sample\\new\\');
  console.log('  ⑤ node scripts\\bz_capture_postcheck.mjs "test\\sample\\new\\2026-*_6ef7c488.json"');
}
process.exitCode = bad === 0 ? 0 : 1;
