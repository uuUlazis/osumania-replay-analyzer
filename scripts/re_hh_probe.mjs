// re_hh_probe.mjs — 诊断: LN 头归属残差 (truth MISS vs 我方判档) 的逐例上下文。
// 用法: <env...> node scripts/re_hh_probe.mjs <anchorId> <truthFile> [kind]
//   kind: hold_head (默认) | note_head
// 输出: build/tmp_accupdate/re_hh_probe_<anchor>[_<tag>].txt (同时打印摘要)
import fs from 'fs';
import { withServer, measureMap } from '../build/v1_harness.mjs';

const anchor = process.argv[2];
const truthFile = process.argv[3];
const kind = process.argv[4] || 'hold_head';
if (!anchor || !truthFile) { console.error('usage: <anchorId> <truthFile> [kind]'); process.exit(1); }
const tag = process.env.PROBE_TAG || '';
const inv = JSON.parse(fs.readFileSync('build/anchor_scores_inventory.json', 'utf8'));
const A = inv.anchors.find(x => x.anchorId === anchor);
const ticks = String(A.identity.playedTicks);
const truth = JSON.parse(fs.readFileSync(truthFile, 'utf8'));
const tNotes = new Map(truth.notes.map(n => [n.index, n]));
const NAME = { MAX: 'perfect', 300: 'great', 200: 'good', 100: 'ok', 50: 'meh', MISS: 'miss' };

const out = [];
const log = (s) => { out.push(s); };

await withServer(28991, async (base) => {
  const r = await measureMap(base, ticks);
  if (r.error) { console.error('ERR', r.error, r.detail || ''); return; }
  const objOf = new Map(r.objects.map(o => [o.index, o]));
  const pts = new Map();
  for (const h of r.hitsV2 || []) {
    const k = h.objectIndex + '|' + h.phase;
    if (!pts.has(k)) pts.set(k, h);
  }
  // 同列物件索引
  const byCol = new Map();
  for (const o of r.objects) {
    if (!byCol.has(o.column)) byCol.set(o.column, []);
    byCol.get(o.column).push(o);
  }
  for (const list of byCol.values()) list.sort((a, b) => a.startTimeMs - b.startTimeMs);

  const cases = [];
  for (const [idx, t] of tNotes) {
    const o = objOf.get(idx); if (!o || !t.game) continue;
    const g = t.game;
    if (kind === 'hold_head' && o.type !== 'hold') continue;
    if (kind === 'note_head' && o.type === 'hold') continue;
    const tn = kind === 'hold_head' ? g.headJudge : g.judge;
    if (tn == null) continue;
    if (kind === 'hold_head' && g.headSrc && g.headSrc === 'gap') continue;
    const e = pts.get(idx + '|' + kind);
    const got = e ? e.judgement : 'miss';
    if (got === NAME[tn]) continue;
    cases.push({ idx, o, truth: tn, got, ev: e });
  }
  log(`# ${anchor} ${kind}: ${cases.length} mismatches (of ${r.hitsV2 ? r.hitsV2.length : 0} points)`);
  // 帧查询 helper
  const framesIn = async (from, to) => {
    const res = await (await fetch(base + '/api/replays/' + r.sid + '/frames?from=' + from + '&to=' + to + '&limit=20000')).json();
    return res.ok ? res.data.frames : [];
  };
  let prevKeys = null;
  for (const c of cases.slice(0, 40)) {
    const o = c.o;
    const from = o.startTimeMs - 400, to = (o.endTimeMs || o.startTimeMs) + 400;
    const frames = await framesIn(from, to);
    log(`\n## oi=${c.idx} col=${o.column} start=${o.startTimeMs} end=${o.endTimeMs} dur=${o.endTimeMs - o.startTimeMs} truth=${c.truth} ours=${c.got}`);
    if (c.ev) log(`   our head ev: at=${c.ev.actualTimeMs} off=${c.ev.offsetMs} j=${c.ev.judgement}`);
    else log('   our head ev: (none)');
    const col = byCol.get(o.column) || [];
    for (const nb of col) {
      if (nb.startTimeMs < o.startTimeMs - 500 || nb.startTimeMs > o.startTimeMs + 500) continue;
      const tn = tNotes.get(nb.index);
      const th = tn && tn.game ? (nb.type === 'hold' ? tn.game.headJudge : tn.game.judge) : '?';
      const tt = tn && tn.game ? tn.game.judge : '?';
      const he = pts.get(nb.index + '|' + (nb.type === 'hold' ? 'hold_head' : 'note_head'));
      const te = pts.get(nb.index + '|hold_tail');
      log(`   nb oi=${nb.index} ${nb.type} start=${nb.startTimeMs} end=${nb.endTimeMs} | truth head=${th} tail=${tt} | ours head=${he ? he.judgement + '@' + (he.actualTimeMs - nb.startTimeMs) : '-'} tail=${te ? te.judgement + '@' + (te.actualTimeMs - nb.endTimeMs) : '-'}`);
    }
    let last = null;
    for (const f of frames) {
      if (f.isSeed) continue;
      const k = f.keys;
      const prev = last == null ? k : last;
      const down = k & ~prev, up = ~k & prev;
      last = k;
      if (!down && !up) continue;
      log(`   f t=${f.timeMs} (${f.timeMs - o.startTimeMs > 0 ? '+' : ''}${f.timeMs - o.startTimeMs}) keys=${k.toString(2)} ${down ? 'DOWN' : ''}${up ? 'UP' : ''}`);
    }
  }
});
const f = 'build/tmp_accupdate/re_hh_probe_' + anchor + (tag ? '_' + tag : '') + '.txt';
fs.writeFileSync(f, out.join('\n'));
console.log(out.slice(0, 200).join('\n'));
console.log('wrote', f);
