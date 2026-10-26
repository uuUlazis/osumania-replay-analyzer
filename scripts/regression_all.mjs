// regression_all.mjs — 一键回归基线: V1/V2 锚点 + 归属闸门 + 逐物件真值
// 用法: node scripts/regression_all.mjs <tag>          (默认对比 build/tmp_accupdate/regression_baseline.json)
//      node scripts/regression_all.mjs baseline --write  (覆盖基线)
// 输出: build/tmp_accupdate/regression_<tag>.json
import fs from 'fs';
import { withServer, measureMap } from '../build/v1_harness.mjs';

const tag = process.argv[2] || 'run';
const writeBaseline = process.argv.includes('--write');
const BASE = 'build/tmp_accupdate/regression_baseline.json';
const PROBE = 'C:/Users/19677/Desktop/workspace/osu-mania-live-reader/output/';
const inv = JSON.parse(fs.readFileSync('build/anchor_scores_inventory.json', 'utf8'));
const A = (id) => inv.anchors.find(x => x.anchorId === id);
const tickOf = (id) => String(A(id).identity.playedTicks);

const V1_ANCHORS = ['bz_09x', 'azimuth_c8b6075b', 'lndan_10th_v1', 'artificial_human_scp_v1', 'azimuth_lndan3_21a9c6b6', 'xueyueyedong_49b4bd1d'];
const V2_ANCHORS = ['negai_1001', 'trueblue_new', 'lndan12_yuugure_2304', 'lndan13_yoru_underjoy_0313', 'xfpsb_5th_beyond'];
// 逐物件真值: [anchorId, truthFile, mode]
const TRUTH_PAIRS = [
  ['bz_09x', 'test/sample/new/2026-10-20_225315_6ef7c488.json', 'v1'],
  ['azimuth_lndan3_21a9c6b6', 'test/sample/new/2026-10-10_002020_21a9c6b6.json', 'v1'],
  ['xueyueyedong_49b4bd1d', 'test/sample/new/2026-10-10_000931_49b4bd1d.json', 'v1'],
  ['negai_1001', 'test/sample/new/2026-10-16_122337_967fdec0.json', 'v2'],
  ['trueblue_new', 'test/sample/new/2026-10-16_125959_a5af1629.json', 'v2'],
];
const MAP = { MAX: 'perfect', 300: 'great', 200: 'good', 100: 'ok', 50: 'meh', MISS: 'miss' };

function declV1(c) { const n = c.n300 + c.n100 + c.n50 + c.geki + c.katu + c.miss; return ((c.n300 + c.geki) * 300 + c.katu * 200 + c.n100 * 100 + c.n50 * 50) / (300 * n) * 100; }
function declV2(c) { const n = c.n300 + c.n100 + c.n50 + c.geki + c.katu + c.miss; return (c.geki * 305 + c.n300 * 300 + c.katu * 200 + c.n100 * 100 + c.n50 * 50) / (305 * n) * 100; }

function truthAgree(r, truthFile, mode) {
  const truth = JSON.parse(fs.readFileSync(truthFile, 'utf8'));
  const tNotes = new Map(truth.notes.map(n => [n.index, n]));
  const objOf = new Map(r.objects.map(o => [o.index, o]));
  if (mode === 'v1') {
    const byObj = new Map();
    for (const h of r.hits || []) { if (h.phase === 'hold_tail') { const e = byObj.get(h.objectIndex) || {}; e.tail = h; byObj.set(h.objectIndex, e); } else { const e = byObj.get(h.objectIndex) || {}; if (!e.head) e.head = h; byObj.set(h.objectIndex, e); } }
    let agree = 0, total = 0;
    for (const [idx, t] of tNotes) {
      const o = objOf.get(idx); if (!o || !t.game || t.game.judge == null) continue;
      const exp = MAP[t.game.judge]; const e = byObj.get(idx);
      const got = o.type === 'hold' ? (e && e.tail ? (e.tail.v1Judgement || e.tail.judgement) : 'miss') : (e && e.head ? e.head.judgement : 'miss');
      total++; if (got === exp) agree++;
    }
    return { total, agree, rate: +(agree / total * 100).toFixed(3) };
  }
  // v2 pairwise: LN 头(headJudge, 仅 event 权威) + LN 尾(judge) + rice(judge)
  const pts = new Map();
  for (const h of r.hitsV2 || []) { const k = h.objectIndex + '|' + h.phase; if (!pts.has(k)) pts.set(k, h); }
  let agree = 0, total = 0;
  for (const [idx, t] of tNotes) {
    const o = objOf.get(idx); if (!o || !t.game) continue;
    const g = t.game;
    const chk = (phase, tn) => {
      if (tn == null) return;
      const exp = MAP[tn]; const e = pts.get(idx + '|' + phase); const got = e ? e.judgement : 'miss';
      total++; if (got === exp) agree++;
    };
    if (o.type === 'hold') { if (g.headSrc && g.headSrc !== 'gap') chk('hold_head', g.headJudge); chk('hold_tail', g.judge); }
    else chk('note_head', g.judge);
  }
  return { total, agree, rate: +(agree / total * 100).toFixed(3) };
}

const out = { tag, at: new Date().toISOString(), anchors: {}, truth: {}, gates: {} };
await withServer(28960, async (base) => {
  const cache = new Map();
  const measure = async (id) => {
    if (!cache.has(id)) cache.set(id, await measureMap(base, tickOf(id)));
    return cache.get(id);
  };
  for (const id of V1_ANCHORS) {
    const r = await measure(id); if (r.error) { out.anchors[id] = { error: r.error }; continue; }
    out.anchors[id] = { mode: r.summary.countMode, dV1: +(r.summary.accuracy.scoreV1 - declV1(A(id).counts)).toFixed(4), dV2: r.summary.accuracy.scoreV2 == null ? null : +(r.summary.accuracy.scoreV2 - declV2(A(id).counts)).toFixed(4) };
  }
  for (const id of V2_ANCHORS) {
    const r = await measure(id); if (r.error) { out.anchors[id] = { error: r.error }; continue; }
    out.anchors[id] = { mode: r.summary.countMode, dV1: +(r.summary.accuracy.scoreV1 - declV1(A(id).counts)).toFixed(4), dV2: r.summary.accuracy.scoreV2 == null ? null : +(r.summary.accuracy.scoreV2 - declV2(A(id).counts)).toFixed(4) };
  }
  for (const [id, tf, mode] of TRUTH_PAIRS) {
    const r = await measure(id); if (r.error) { out.truth[id + ':' + mode] = { error: r.error }; continue; }
    out.truth[id + ':' + mode] = truthAgree(r, tf, mode);
  }
  // 归属闸门: bz + negai + trueblue 的 0xA4 窗口一致率
  const WINDOWS = [
    ['bz_09x', ['bind_watchobj_w1394.jsonl', 'bind_watchobj_w2418.jsonl', 'bind_watchobj_w4635.jsonl', 'bind_watchobj_w5686.jsonl', 'bind_watchobj.jsonl']],
    ['negai_1001', ['bind_watchobj_negai_w1792.jsonl']],
    ['trueblue_new', ['bind_watchobj_trueblue_w1536.jsonl']],
  ];
  let wTotal = 0, wAgree = 0;
  for (const [id, files] of WINDOWS) {
    const writes = new Map();
    for (const f of files) {
      const p = PROBE + f; if (!fs.existsSync(p)) continue;
      for (const line of fs.readFileSync(p, 'utf8').split('\n')) {
        const s = line.trim(); if (!s) continue; let j; try { j = JSON.parse(s); } catch { continue; }
        if (j.off !== '0xA4' || j.i === undefined || j.to === undefined) continue;
        if (!writes.has(j.i)) writes.set(j.i, []);
        writes.get(j.i).push(j.to);
      }
    }
    const r = await measure(id); if (r.error) continue;
    const eng = new Map();
    for (const h of r.hits || []) { if (h.phase === 'hold_tail') continue; if (!eng.has(h.objectIndex)) eng.set(h.objectIndex, h.actualTimeMs); }
    for (const [i, ws] of writes) { wTotal++; const t = eng.get(i); if (t != null && ws.includes(t)) wAgree++; }
  }
  out.gates.attribution = { total: wTotal, agree: wAgree, rate: +(wAgree / Math.max(wTotal, 1) * 100).toFixed(2) };
});

console.log('=== regression', tag, '===');
for (const [k, v] of Object.entries(out.anchors)) console.log(' anchor', k.padEnd(30), JSON.stringify(v));
for (const [k, v] of Object.entries(out.truth)) console.log(' truth ', k.padEnd(30), JSON.stringify(v));
console.log(' gate  attribution', JSON.stringify(out.gates.attribution));

if (writeBaseline) {
  fs.writeFileSync(BASE, JSON.stringify(out, null, 1));
  console.log('baseline written ->', BASE);
} else if (fs.existsSync(BASE)) {
  const b = JSON.parse(fs.readFileSync(BASE, 'utf8'));
  let bad = 0;
  const cmp = (label, cur, ref) => {
    const d = +(cur - ref).toFixed(4);
    if (Math.abs(d) > 1e-9) { bad++; console.log('  [DIFF] ' + label + ': ' + ref + ' -> ' + cur + ' (' + (d > 0 ? '+' : '') + d + ')'); }
  };
  for (const [k, v] of Object.entries(out.anchors)) { const r = b.anchors && b.anchors[k]; if (!r || r.error || v.error) continue; if (v.dV1 != null) cmp(k + '.dV1', v.dV1, r.dV1); if (v.dV2 != null && r.dV2 != null) cmp(k + '.dV2', v.dV2, r.dV2); }
  for (const [k, v] of Object.entries(out.truth)) { const r = b.truth && b.truth[k]; if (!r || r.error || v.error) continue; cmp(k + '.rate', v.rate, r.rate); }
  if (b.gates && b.gates.attribution && out.gates.attribution) cmp('attribution.rate', out.gates.attribution.rate, b.gates.attribution.rate);
  console.log(bad === 0 ? '\n✅ 与基线逐位一致' : '\n⚠ ' + bad + ' 项偏离基线');
  process.exitCode = bad === 0 ? 0 : 1;
}
fs.writeFileSync('build/tmp_accupdate/regression_' + tag + '.json', JSON.stringify(out, null, 1));
console.log('wrote build/tmp_accupdate/regression_' + tag + '.json');
