// re_truth_src.mjs — V2 逐判定点真值闸门 (source-aware)
//
// 背景: 会话真值里 hold 头有两个字段 ——
//   * note.head.judgement / head.error  (source='error' = 读者从**实测误差流**
//     推出来的头档; source='end' = 由末态反推)
//   * note.game.headJudge / headSrc     (source-headSrc = recovered/solver 推导,
//     含 'constrained' 等反解值)
// docs (build/stable-re/v2anchor/VERDICT.md oracleCaveat) 已记录: hold 头真值
// 82% 由读者推导, 「不能当 hold 的逐物件 oracle」。
// 本脚本同时给出两种口径, 便于判断残差是引擎错还是真值推导错。
//
// 用法: <env...> node scripts/re_truth_src.mjs <tag>
// 输出: build/tmp_accupdate/re_truth_src_<tag>.json
import fs from 'fs';
import { withServer, measureMap } from '../build/v1_harness.mjs';

const tag = process.argv[2] || 'run';
const inv = JSON.parse(fs.readFileSync('build/anchor_scores_inventory.json', 'utf8'));
const A = (id) => inv.anchors.find(x => x.anchorId === id);
const tickOf = (id) => String(A(id).identity.playedTicks);
const V2_MAPS = [
  ['xfpsb_5th_beyond', 'test/sample/xfpsb ln 5th dan.json'],
  ['lndan13_yoru_underjoy_0313', 'test/sample/4k lndan 13th-yoru.json'],
  ['twinbloom', 'test/sample/twin bloom.json'],
  ['negai_1001', 'test/sample/new/2026-10-16_122337_967fdec0.json'],
  ['lndan12_yuugure_2304', 'test/sample/4k lndan 12th-yuugure.json'],
  ['trueblue_new', 'test/sample/new/2026-10-16_125959_a5af1629.json'],
];
const MAP = { MAX: 'perfect', 300: 'great', 200: 'good', 100: 'ok', 50: 'meh', MISS: 'miss' };

await withServer(28992, async (base) => {
  const out = { tag, at: new Date().toISOString(), env: Object.fromEntries(Object.entries(process.env).filter(([k]) => k.startsWith('MRA_'))), maps: {} };
  const tot = { measured: [0, 0], inferred: [0, 0], oldMetric: [0, 0] };
  for (const [id, tf] of V2_MAPS) {
    const r = await measureMap(base, tickOf(id));
    if (r.error) { console.log('ERR', id, r.error); continue; }
    const truth = JSON.parse(fs.readFileSync(tf, 'utf8'));
    const pts = new Map();
    for (const h of r.hitsV2 || []) {
      const k = h.objectIndex + '|' + h.phase;
      if (!pts.has(k)) pts.set(k, h);
    }
    const objOf = new Map(r.objects.map(o => [o.index, o]));
    const conf = {}, confInf = {}, confOld = {};
    let a1 = 0, t1 = 0, a2 = 0, t2 = 0, a3 = 0, t3 = 0;
    for (const n of truth.notes) {
      const o = objOf.get(n.index);
      if (!o) continue;
      const g = n.game || {};
      const ours = (phase) => { const e = pts.get(n.index + '|' + phase); return e ? e.judgement : 'miss'; };
      const chk = (phase, tn, store) => {
        if (tn == null) return;
        const exp = MAP[tn];
        const got = ours(phase);
        const key = phase + ':' + exp + '->' + got;
        if (got === exp) return true;
        store[key] = (store[key] || 0) + 1;
        return false;
      };
      // 口径 A (measured): 只用 source==='error' 的逐点判定
      if (n.type === 'hold') {
        if (n.head && n.head.source === 'error') { t1++; if (chk('hold_head', n.head.judgement, conf)) a1++; }
        if (n.tail && n.tail.source === 'error') { t1++; if (chk('hold_tail', n.tail.judgement, conf)) a1++; }
      } else if (n.head && n.head.source === 'error') {
        t1++; if (chk('note_head', n.head.judgement, conf)) a1++;
      }
      // 口径 B (inferred): source==='end' 的点单独统计
      if (n.type === 'hold') {
        if (n.head && n.head.source === 'end') { t2++; if (chk('hold_head', n.head.judgement, confInf)) a2++; }
        if (n.tail && n.tail.source === 'end') { t2++; if (chk('hold_tail', n.tail.judgement, confInf)) a2++; }
      } else if (n.head && n.head.source === 'end') {
        t2++; if (chk('note_head', n.head.judgement, confInf)) a2++;
      }
      // 口径 C (old): game.headJudge + game.judge —— 现行 re_sweep / truth_agree_v2
      if (n.type === 'hold') {
        if (g.headSrc && g.headSrc !== 'gap') { t3++; if (chk('hold_head', g.headJudge, confOld)) a3++; }
        t3++; if (chk('hold_tail', g.judge, confOld)) a3++;
      } else { t3++; if (chk('note_head', g.judge, confOld)) a3++; }
    }
    // ── hold 头真值双字段审计: 老口径 (game.headJudge) 判为不一致的点里,
    //    有多少在**实测误差流**口径 (head.judgement, source=error) 下其实一致
    //    ⇒ 属于真值侧推导伪影, 而不是引擎错。
    let flagTot = 0, artifact = 0, real = 0;
    const artifactEx = [];
    for (const n of truth.notes) {
      if (n.type !== 'hold' || !n.game || !n.head) continue;
      const g = n.game;
      if (!(g.headSrc && g.headSrc !== 'gap')) continue;
      const got = (() => { const e = pts.get(n.index + '|hold_head'); return e ? e.judgement : 'miss'; })();
      if (got === MAP[g.headJudge]) continue;
      flagTot++;
      const hd = n.head;
      if (hd.source === 'error' && got === MAP[hd.judgement]) {
        artifact++;
        if (artifactEx.length < 4) artifactEx.push({ i: n.index, gameHead: g.headJudge, src: g.headSrc, headJudgement: hd.judgement, headErr: hd.error, ours: got });
      } else {
        real++;
      }
    }
    const mk = (a, t) => ({ agree: a, total: t, rate: t ? +(a / t * 100).toFixed(3) : null });
    out.maps[id] = {
      measured: mk(a1, t1), inferredEnd: mk(a2, t2), old: mk(a3, t3),
      conf, confInferred: confInf, confOld,
      holdHeadAudit: { flagged: flagTot, truthArtifact: artifact, realMismatch: real, examples: artifactEx },
    };
    console.log('    hold-head audit: flagged', flagTot, '| 真值侧伪影', artifact, '| 真不一致', real, JSON.stringify(artifactEx));
    tot.measured[0] += a1; tot.measured[1] += t1;
    tot.inferred[0] += a2; tot.inferred[1] += t2;
    tot.oldMetric[0] += a3; tot.oldMetric[1] += t3;
    console.log(id.padEnd(30),
      'measured', (t1 ? (a1 / t1 * 100).toFixed(3) : '-').padStart(8), a1 + '/' + t1,
      '| inferredEnd', (t2 ? (a2 / t2 * 100).toFixed(3) : '-').padStart(8), a2 + '/' + t2,
      '| old', (t3 ? (a3 / t3 * 100).toFixed(3) : '-').padStart(8), a3 + '/' + t3);
    console.log('    measured conf:', JSON.stringify(conf));
  }
  const rate = (x) => (x[1] ? +(x[0] / x[1] * 100).toFixed(3) : null);
  out.totals = { measured: rate(tot.measured), inferredEnd: rate(tot.inferred), old: rate(tot.oldMetric),
    pts: { measured: tot.measured, inferredEnd: tot.inferred, old: tot.oldMetric } };
  console.log('TOTAL measured', out.totals.measured, '| inferredEnd', out.totals.inferredEnd, '| old', out.totals.old);
  fs.writeFileSync('build/tmp_accupdate/re_truth_src_' + tag + '.json', JSON.stringify(out, null, 1));
  console.log('wrote build/tmp_accupdate/re_truth_src_' + tag + '.json');
});
