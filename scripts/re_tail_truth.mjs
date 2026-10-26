// re_tail_truth.mjs — 用会话真值静态检验逆向包 IL 尾档模型:
//   e = |releaseErr|/1.5 对五窗 (tailWindows 已是 1.5×); |releaseErr| > 1.5*W50 ⇒ MISS;
//   若 (WasBroken || !HeadJudged) 且 result > 50 ⇒ cap 50 (只降不升)。
// 误差来源优先级: game.tailError (class 反推) > note.tail.error (内存实测)。
import fs from 'fs';

const MAPS = [
  ['xfpsb', 'test/sample/xfpsb ln 5th dan.json'],
  ['ln13', 'test/sample/4k lndan 13th-yoru.json'],
  ['twin', 'test/sample/twin bloom.json'],
  ['negai', 'test/sample/new/2026-10-16_122337_967fdec0.json'],
  ['ln12', 'test/sample/4k lndan 12th-yuugure.json'],
  ['trueblue', 'test/sample/new/2026-10-16_125959_a5af1629.json'],
];
const NAME = ['MAX', '300', '200', '100', '50'];
for (const [tag, f] of MAPS) {
  const t = JSON.parse(fs.readFileSync(f, 'utf8'));
  const tw = t.score.tailWindows;
  const W = [tw.perfect, tw.great, tw.good, tw.ok, tw.meh];
  const cls = (e) => {
    const a = Math.abs(e);
    for (let i = 0; i < 5; i++) if (a <= W[i]) return NAME[i];
    return 'MISS';
  };
  let n = 0, ok = 0, capped = 0, noErr = 0;
  const conf = {}, bad = [];
  let late = 0;   // release eventTime > end + 1.5*W100 (would have expired)
  for (const note of t.notes) {
    if (note.type !== 'hold' || !note.game) continue;
    const g = note.game;
    const rel = note.tail && note.tail.eventTime != null ? note.tail.eventTime : null;
    let e = g.tailError;
    let src = 'class';
    if (e == null && note.tail && note.tail.error != null) { e = note.tail.error; src = 'measured'; }
    if (g.judge == null || e == null) { noErr++; continue; }
    n++;
    if (rel != null && note.endTime != null && rel > note.endTime + W[3]) late++;
    const p = cls(e);
    if (p === g.judge) { ok++; continue; }
    if (g.judge === '50' && p !== 'MISS') { capped++; continue; }
    const k = src + ':' + p + '->' + g.judge;
    conf[k] = (conf[k] || 0) + 1;
    if (bad.length < 8) bad.push({ i: note.index, start: note.startTimeMs, end: note.endTimeMs, rel, e, pred: p, truth: g.judge, headJudge: g.headJudge, headSrc: g.headSrc });
  }
  console.log(tag.padEnd(9), 'tails', String(n).padStart(5), 'err-missing', String(noErr).padStart(4),
    'IL-map ok', String(ok).padStart(5), 'capped-50', String(capped).padStart(4),
    'unexplained', String(n - ok - capped).padStart(3), '| rel>end+1.5W100', String(late).padStart(3),
    '| truth.cappedTails', t.summary.cappedTails);
  if (Object.keys(conf).length) console.log('   conf', JSON.stringify(conf));
  for (const b of bad) console.log('   bad', JSON.stringify(b));
}
