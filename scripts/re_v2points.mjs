// re_v2points.mjs — V2 六图**全部判定点** (hold_head / hold_tail / note_head) 与真值逐点对拍
// 用法: <env...> node scripts/re_v2points.mjs <tag>
// 输出: build/tmp_accupdate/v2points_<tag>.json
//   每图: counts / L1 / accV2 / 声明 acc(2 位小数, lead 口径) / 逐点 confusion (带 acc 权重)
//         / 全部不一致点明细 (相位 + 真值 + 我方 + offset/at + 真值误差源)
import fs from 'fs';
import { withServer, measureMap } from '../build/v1_harness.mjs';

const tag = process.argv[2] || 'run';
const only = (process.argv.find(a => a.startsWith('--maps=')) || '').slice(7).split(',').filter(Boolean);
const MAPS = [
  ['xfpsb', 'xfpsb_5th_beyond', 'test/sample/xfpsb ln 5th dan.json'],
  ['ln13', 'lndan13_yoru_underjoy_0313', 'test/sample/4k lndan 13th-yoru.json'],
  ['twin', 'twinbloom', 'test/sample/twin bloom.json'],
  ['negai', 'negai_1001', 'test/sample/new/2026-10-16_122337_967fdec0.json'],
  ['ln12', 'lndan12_yuugure_2304', 'test/sample/4k lndan 12th-yuugure.json'],
  ['trueblue', 'trueblue_new', 'test/sample/new/2026-10-16_125959_a5af1629.json'],
];
const MAPN = { MAX: 'perfect', 300: 'great', 200: 'good', 100: 'ok', 50: 'meh', MISS: 'miss' };
const KEYS = ['perfect', 'great', 'good', 'ok', 'meh', 'miss'];
const WT = { perfect: 305, great: 300, good: 200, ok: 100, meh: 50, miss: 0 };
const inv = JSON.parse(fs.readFileSync('build/anchor_scores_inventory.json', 'utf8'));
const A = (id) => inv.anchors.find(x => x.anchorId === id);
const tickOf = (id) => String(A(id).identity.playedTicks);

const out = { tag, at: new Date().toISOString(), env: Object.fromEntries(Object.entries(process.env).filter(([k]) => k.startsWith('MRA_'))), maps: {} };
await withServer(28995, async (base) => {
  for (const [mtag, anchor, truthFile] of MAPS) {
    if (only.length && !only.includes(mtag)) continue;
    const r = await measureMap(base, tickOf(anchor));
    if (r.error) { out.maps[mtag] = { error: r.error }; console.log('ERR', mtag, r.error); continue; }
    const truth = JSON.parse(fs.readFileSync(truthFile, 'utf8'));
    const tNotes = new Map(truth.notes.map(n => [n.index, n]));
    const objOf = new Map(r.objects.map(o => [o.index, o]));
    const pts = new Map();
    for (const h of r.hitsV2 || []) {
      const k = h.objectIndex + '|' + h.phase;
      if (!pts.has(k)) pts.set(k, h);
    }
    const c = r.summary.counts;
    const eng = { perfect: c.perfect, great: c.great, good: c.good, ok: c.ok, meh: c.meh, miss: c.miss };
    const tc = truth.summary.counts;
    const tr = { perfect: tc[0], great: tc[1], good: tc[2], ok: tc[3], meh: tc[4], miss: tc[5] };
    const diff = {}; let l1 = 0;
    for (const k of KEYS) { diff[k] = (eng[k] ?? 0) - (tr[k] ?? 0); l1 += Math.abs(diff[k]); }
    const declAcc = truth.summary.acc;                      // 回放显示/声明 acc (2 位小数, lead 口径)
    const oursV2 = r.summary.accuracy.scoreV2;
    const phaseStat = {};
    const mism = [];
    let wSum = 0, nPts = 0, agree = 0;
    for (const [i, t] of tNotes) {
      const o = objOf.get(i); if (!o || !t.game) continue;
      const g = t.game;
      const chk = (phase, tn) => {
        if (tn == null) return;
        const exp = MAPN[tn];
        const e = pts.get(i + '|' + phase);
        const got = e ? e.judgement : 'miss';
        nPts++;
        phaseStat[phase] = phaseStat[phase] || { total: 0, agree: 0, wErr: 0, conf: {} };
        const st = phaseStat[phase];
        st.total++;
        wSum += (WT[got] || 0) - (WT[exp] || 0);
        if (got === exp) { agree++; st.agree++; return; }
        st.wErr += (WT[got] || 0) - (WT[exp] || 0);
        st.conf[exp + '->' + got] = (st.conf[exp + '->' + got] || 0) + 1;
        mism.push({
          i, phase, type: o.type, col: o.column, start: o.startTimeMs, end: o.endTimeMs,
          dur: o.endTimeMs != null ? o.endTimeMs - o.startTimeMs : null,
          truth: tn, engine: got,
          engineOff: e ? e.offsetMs : null, engineAt: e ? e.actualTimeMs : null,
          dt: e && e.actualTimeMs != null && o.endTimeMs != null && phase === 'hold_tail' ? e.actualTimeMs - o.endTimeMs : null,
          truthErr: phase === 'hold_tail' ? (t.tail && t.tail.error) : (t.head && t.head.error),
          truthEv: phase === 'hold_tail' ? (t.tail && t.tail.eventTime) : (t.head && t.head.eventTime),
          truthSrc: phase === 'hold_tail' ? (t.tail && t.tail.source) : (t.head && t.head.source),
          truthJudge: g.judge, truthHead: g.headJudge, headSrc: g.headSrc,
        });
      };
      if (o.type === 'hold') { chk('hold_head', g.headJudge); chk('hold_tail', g.judge); }
      else chk('note_head', g.judge);
    }
    mism.sort((a, b) => a.i - b.i || (a.phase < b.phase ? -1 : 1));
    out.maps[mtag] = {
      anchor, truthFile, engine: eng, truth: tr, diff, l1,
      acc: { oursV2, declared2: declAcc, dAcc: +(oursV2 - declAcc).toFixed(5) },
      points: nPts, agree, rate: +(agree / Math.max(nPts, 1) * 100).toFixed(3),
      wErrSum: wSum, phaseStat, mismatch: mism.length, mismatches: mism,
    };
    console.log(mtag.padEnd(9), 'L1', String(l1).padStart(3), JSON.stringify(diff),
      '| dAcc', (oursV2 - declAcc).toFixed(5),
      '| pts', agree + '/' + nPts, '| mism', mism.length, '| wErr', wSum);
    for (const [ph, st] of Object.entries(phaseStat)) {
      console.log('   ', ph.padEnd(10), (st.agree + '/' + st.total).padStart(11), 'wErr', String(st.wErr).padStart(6), JSON.stringify(st.conf));
    }
  }
});
out.totals = {
  l1: Object.values(out.maps).reduce((a, m) => a + (m.l1 || 0), 0),
  perMap: Object.fromEntries(Object.entries(out.maps).map(([k, v]) => [k, v.l1 ?? null])),
  dAcc: Object.fromEntries(Object.entries(out.maps).map(([k, v]) => [k, v.acc ? v.acc.dAcc : null])),
};
fs.writeFileSync('build/tmp_accupdate/v2points_' + tag + '.json', JSON.stringify(out, null, 1));
console.log('\nL1 sum', out.totals.l1, JSON.stringify(out.totals.perMap));
console.log('dAcc', JSON.stringify(out.totals.dAcc));
