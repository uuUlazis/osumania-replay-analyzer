// re_sweep.mjs — 逆向包修正 (MRA_RE_*) 单配置一次性量化:
//   V2 六图 counts L1 (vs 会话真值) + V1 七锚点 dV1 (vs .osr 声明) + 逐物件真值 (5 对)
// 用法: <env...> node scripts/re_sweep.mjs <tag> [--skip-v1] [--skip-truth]
// 输出: build/tmp_accupdate/re_sweep_<tag>.json
import fs from 'fs';
import { withServer, measureMap } from '../build/v1_harness.mjs';

const tag = process.argv[2] || 'run';
const skipV1 = process.argv.includes('--skip-v1');
const skipTruth = process.argv.includes('--skip-truth');
const inv = JSON.parse(fs.readFileSync('build/anchor_scores_inventory.json', 'utf8'));
const A = (id) => inv.anchors.find(x => x.anchorId === id);
const tickOf = (id) => String(A(id).identity.playedTicks);

// V2 六图: [anchorId, truthFile]
const V2_MAPS = [
  ['xfpsb_5th_beyond', 'test/sample/xfpsb ln 5th dan.json'],
  ['lndan13_yoru_underjoy_0313', 'test/sample/4k lndan 13th-yoru.json'],
  ['twinbloom', 'test/sample/twin bloom.json'],
  ['negai_1001', 'test/sample/new/2026-10-16_122337_967fdec0.json'],
  ['lndan12_yuugure_2304', 'test/sample/4k lndan 12th-yuugure.json'],
  ['trueblue_new', 'test/sample/new/2026-10-16_125959_a5af1629.json'],
];
const V1_ANCHORS = ['bz_09x', 'azimuth_c8b6075b', 'lndan_10th_v1', 'artificial_human_scp_v1',
  'azimuth_lndan3_21a9c6b6', 'xueyueyedong_49b4bd1d', 'fubuki_vibro'];
const TRUTH_PAIRS = [
  ['bz_09x', 'test/sample/new/2026-10-20_225315_6ef7c488.json', 'v1'],
  ['azimuth_lndan3_21a9c6b6', 'test/sample/new/2026-10-10_002020_21a9c6b6.json', 'v1'],
  ['xueyueyedong_49b4bd1d', 'test/sample/new/2026-10-10_000931_49b4bd1d.json', 'v1'],
  ...V2_MAPS.map(([id, tf]) => [id, tf, 'v2']),
];
const MAP = { MAX: 'perfect', 300: 'great', 200: 'good', 100: 'ok', 50: 'meh', MISS: 'miss' };
const KEYS = ['perfect', 'great', 'good', 'ok', 'meh', 'miss'];

function declV1(c) {
  const n = c.n300 + c.n100 + c.n50 + c.geki + c.katu + c.miss;
  return ((c.n300 + c.geki) * 300 + c.katu * 200 + c.n100 * 100 + c.n50 * 50) / (300 * n) * 100;
}
function truthCounts(file) {
  const t = JSON.parse(fs.readFileSync(file, 'utf8')).summary.counts;
  return { perfect: t[0], great: t[1], good: t[2], ok: t[3], meh: t[4], miss: t[5] };
}
function truthAgree(r, truthFile, mode) {
  const truth = JSON.parse(fs.readFileSync(truthFile, 'utf8'));
  const tNotes = new Map(truth.notes.map(n => [n.index, n]));
  const objOf = new Map(r.objects.map(o => [o.index, o]));
  if (mode === 'v1') {
    const byObj = new Map();
    for (const h of r.hits || []) {
      if (h.phase === 'hold_tail') { const e = byObj.get(h.objectIndex) || {}; e.tail = h; byObj.set(h.objectIndex, e); }
      else { const e = byObj.get(h.objectIndex) || {}; if (!e.head) e.head = h; byObj.set(h.objectIndex, e); }
    }
    let agree = 0, total = 0;
    for (const [idx, t] of tNotes) {
      const o = objOf.get(idx); if (!o || !t.game || t.game.judge == null) continue;
      const exp = MAP[t.game.judge]; const e = byObj.get(idx);
      const got = o.type === 'hold' ? (e && e.tail ? (e.tail.v1Judgement || e.tail.judgement) : 'miss') : (e && e.head ? e.head.judgement : 'miss');
      total++; if (got === exp) agree++;
    }
    return { total, agree, rate: +(agree / total * 100).toFixed(3) };
  }
  const pts = new Map();
  for (const h of r.hitsV2 || []) { const k = h.objectIndex + '|' + h.phase; if (!pts.has(k)) pts.set(k, h); }
  let agree = 0, total = 0;
  const conf = {};
  for (const [idx, t] of tNotes) {
    const o = objOf.get(idx); if (!o || !t.game) continue;
    const g = t.game;
    const chk = (phase, tn) => {
      if (tn == null) return;
      const exp = MAP[tn]; const e = pts.get(idx + '|' + phase); const got = e ? e.judgement : 'miss';
      total++;
      if (got === exp) { agree++; return; }
      const k = phase + ':' + exp + '->' + got; conf[k] = (conf[k] || 0) + 1;
    };
    if (o.type === 'hold') { if (g.headSrc && g.headSrc !== 'gap') chk('hold_head', g.headJudge); chk('hold_tail', g.judge); }
    else chk('note_head', g.judge);
  }
  return { total, agree, rate: +(agree / total * 100).toFixed(3), conf };
}

const env = Object.fromEntries(Object.entries(process.env).filter(([k]) => k.startsWith('MRA_')));
const out = { tag, at: new Date().toISOString(), env, v2: {}, v1: {}, truth: {}, totals: {} };

await withServer(28990, async (base) => {
  const cache = new Map();
  const measure = async (id) => {
    if (cache.has(id)) return cache.get(id);
    let r = await measureMap(base, tickOf(id));
    for (let i = 0; i < 3 && r.error; i++) {
      console.log('  retry', id, r.error);
      r = await measureMap(base, tickOf(id));
    }
    cache.set(id, r);
    return r;
  };
  let l1sum = 0;
  for (const [id, tf] of V2_MAPS) {
    const r = await measure(id);
    if (r.error) { out.v2[id] = { error: r.error }; console.log('ERR', id, r.error); continue; }
    const T = truthCounts(tf);
    const c = r.summary.counts;
    const eng = { perfect: c.perfect, great: c.great, good: c.good, ok: c.ok, meh: c.meh, miss: c.miss };
    const d = {}; let l1 = 0;
    for (const k of KEYS) { d[k] = (eng[k] ?? 0) - (T[k] ?? 0); l1 += Math.abs(d[k]); }
    l1sum += l1;
    out.v2[id] = { l1, diff: d, engine: eng, truth: T };
    console.log('V2', id.padEnd(30), 'L1', String(l1).padStart(3), JSON.stringify(d));
  }
  out.totals.v2L1 = l1sum;
  console.log('   V2 L1 sum =', l1sum);
  if (!skipV1) {
    for (const id of V1_ANCHORS) {
      const r = await measure(id);
      if (r.error) { out.v1[id] = { error: r.error }; console.log('ERR', id, r.error); continue; }
      const acc = r.summary.accuracy || {};
      const dV1 = acc.scoreV1 == null ? null : +(acc.scoreV1 - declV1(A(id).counts)).toFixed(5);
      out.v1[id] = { mode: r.summary.countMode, dV1, scoreV1: acc.scoreV1, counts: r.summary.counts };
      console.log('V1', id.padEnd(30), 'dV1', String(dV1).padStart(10), 'mode', r.summary.countMode);
    }
  }
  if (!skipTruth) {
    for (const [id, tf, mode] of TRUTH_PAIRS) {
      const r = await measure(id);
      if (r.error) { out.truth[id + ':' + mode] = { error: r.error }; continue; }
      const t = truthAgree(r, tf, mode);
      out.truth[id + ':' + mode] = t;
      console.log('TR', (id + ':' + mode).padEnd(30), JSON.stringify(t.rate), t.agree + '/' + t.total,
        mode === 'v2' ? JSON.stringify(t.conf) : '');
      for (const [k, v] of Object.entries(t)) if (k !== 'conf') out.truth[id + ':' + mode][k] = v;
    }
  }
});
fs.writeFileSync('build/tmp_accupdate/re_sweep_' + tag + '.json', JSON.stringify(out, null, 1));
console.log('wrote build/tmp_accupdate/re_sweep_' + tag + '.json');
