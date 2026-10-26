// re_v2tail_residual.mjs — V2 尾档/释放归属残差逐物件枚举 (2026-10-21, 子代理 v2tail)
//
// 用途: 对 V2 六图 (重点 xfpsb / 13th / 12th) 把每一个 hold 的**尾判定**与真值逐物件对拍,
//       并对每条不一致补齐: 真值侧 (game.judge / note.tail.*) + 我方侧 (hitsV2 hold_tail 的
//       offset/actualTimeMs + MRA_RE_DUMP 结算路径) + 探针 0xA8 释放写入。
//
// 用法: node scripts/re_v2tail_residual.mjs <tag> [--maps=xfpsb,ln13,ln12] [--no-probe]
//       额外 env 通过进程环境注入 (MRA_RE_* 门控)。
// 输出: build/tmp_accupdate/v2tail_residual_<tag>.json
//       build/tmp_accupdate/v2tail_residual_<tag>_<map>.md  (逐物件表)
//       build/tmp_accupdate/v2tail_dump_<tag>_<map>.txt     (MRA_RE_DUMP 原始)
import fs from 'fs';
import path from 'path';
import { withServer, measureMap } from '../build/v1_harness.mjs';

const PROBE = 'C:/Users/19677/Desktop/workspace/osu-mania-live-reader/output/';
const OUT = 'build/tmp_accupdate/';
const tag = process.argv[2] || 'run';
const only = (process.argv.find(a => a.startsWith('--maps=')) || '').slice(7).split(',').filter(Boolean);
const noProbe = process.argv.includes('--no-probe');

const MAPS = [
  ['xfpsb', 'xfpsb_5th_beyond', 'test/sample/xfpsb ln 5th dan.json',
    ['bind_watchobj_xfpsb.jsonl', 'bind_watchobj_xfpsb2.jsonl', 'bind_watchobj_xfpsb3.jsonl', 'bind_watchobj_xfpsb4.jsonl']],
  ['ln13', 'lndan13_yoru_underjoy_0313', 'test/sample/4k lndan 13th-yoru.json',
    ['bind_watchobj_ln13.jsonl']],
  ['ln12', 'lndan12_yuugure_2304', 'test/sample/4k lndan 12th-yuugure.json', []],
  ['twin', 'twinbloom', 'test/sample/twin bloom.json', []],
  ['negai', 'negai_1001', 'test/sample/new/2026-10-16_122337_967fdec0.json', []],
  ['trueblue', 'trueblue_new', 'test/sample/new/2026-10-16_125959_a5af1629.json', []],
];
const MAPN = { MAX: 'perfect', 300: 'great', 200: 'good', 100: 'ok', 50: 'meh', MISS: 'miss' };
const KEYS = ['perfect', 'great', 'good', 'ok', 'meh', 'miss'];
const inv = JSON.parse(fs.readFileSync('build/anchor_scores_inventory.json', 'utf8'));
const A = (id) => inv.anchors.find(x => x.anchorId === id);
const tickOf = (id) => String(A(id).identity.playedTicks);

// .osr 声明 acc (V2): geki 计 305 权重, 分母 305*n —— 与 inventory calibratedAcc 同口径。
function declAccV2(c) {
  const n = c.n300 + c.n100 + c.n50 + c.geki + c.katu + c.miss;
  if (!n) return 0;
  return (c.geki * 305 + c.n300 * 300 + c.katu * 200 + c.n100 * 100 + c.n50 * 50) / (305 * n) * 100;
}

const OFFS = ['0xA4', '0xA8', '0x5C', '0x80', '0xE0', '0x84', '0xB4', '0xD8', '0xE4', '0xE8'];

// 解析一个探针文件 -> Map(objIdx -> {0xA4:[t...], 0xA8:[t...], ...})
function loadProbe(file, holdSet) {
  const p = PROBE + file;
  if (!fs.existsSync(p)) return null;
  const m = new Map();
  let lines = 0;
  for (const line of fs.readFileSync(p, 'utf8').split('\n')) {
    const s = line.trim();
    if (!s || s[0] !== '{' || s.indexOf('"off"') < 0) continue;
    let j; try { j = JSON.parse(s); } catch { continue; }
    if (j.off === undefined || j.i === undefined) continue;
    if (!holdSet.has(j.i)) continue;
    lines++;
    let e = m.get(j.i); if (!e) { e = {}; m.set(j.i, e); }
    (e[j.off] = e[j.off] || []).push(j.to);
  }
  return { file, lines, m };
}

// MRA_RE_DUMP -> Map(oi -> [{path, at, err, j}...])  (REL/FLUSH/DEFER/DEFER_MEH/PRESSLN/UP)
function loadDump(file) {
  const m = new Map();
  if (!fs.existsSync(file)) return m;
  for (const line of fs.readFileSync(file, 'utf8').split('\n')) {
    const s = line.trim(); if (!s) continue;
    let mm = s.match(/^(REL|FLUSH|DEFER_MEH|DEFER) oi=(\d+) at=(-?\d+) err=(-?[\d.eE+]+) j=(-?\d+)/);
    if (mm) {
      const oi = +mm[2];
      let e = m.get(oi); if (!e) { e = []; m.set(oi, e); }
      e.push({ path: mm[1], at: +mm[3], err: +mm[4], j: +mm[5] });
      continue;
    }
    mm = s.match(/^(UP|PRESSLN|RELEASE) .*/);
    if (mm) {
      const t = +((s.match(/t=(-?\d+)/) || [])[1]);
      const oi = +((s.match(/(?:oi|active)=(-?\d+)/) || [])[1]);
      if (Number.isFinite(oi) && oi >= 0) {
        let e = m.get(oi); if (!e) { e = []; m.set(oi, e); }
        e.push({ path: mm[1], at: t });
      }
    }
  }
  return m;
}

const out = { tag, at: new Date().toISOString(), env: Object.fromEntries(Object.entries(process.env).filter(([k]) => k.startsWith('MRA_'))), maps: {}, totals: {} };
const mdChunks = [];

for (const [mtag, anchor, truthFile, probeFiles] of MAPS) {
  if (only.length && !only.includes(mtag)) continue;
  const truth = JSON.parse(fs.readFileSync(truthFile, 'utf8'));
  const tNotes = new Map(truth.notes.map(n => [n.index, n]));
  const holdSet = new Set([...tNotes.values()].filter(n => n.type === 'hold').map(n => n.index));
  const dumpFile = OUT + 'v2tail_dump_' + tag + '_' + mtag + '.txt';
  try { fs.unlinkSync(dumpFile); } catch {}
  let r;
  await withServer(28980 + MAPS.findIndex(x => x[0] === mtag), async (base) => {
    r = await measureMap(base, tickOf(anchor));
  }, { ...process.env, MRA_RE_DUMP: dumpFile });
  const dump = loadDump(dumpFile);
  if (r.error) { out.maps[mtag] = { anchor, error: r.error }; console.log('ERR', mtag, r.error); continue; }

  const objOf = new Map(r.objects.map(o => [o.index, o]));
  const tailOf = new Map();
  const headOf = new Map();
  for (const h of r.hitsV2 || []) {
    if (h.phase === 'hold_tail') { if (!tailOf.has(h.objectIndex)) tailOf.set(h.objectIndex, h); }
    else if (h.phase === 'hold_head') { if (!headOf.has(h.objectIndex)) headOf.set(h.objectIndex, h); }
  }
  // 探针: 逐文件与真值 tail.eventTime 的 0xA8 一致率, 取最好的那个当引用
  let probe = null;
  const probeStats = [];
  if (!noProbe && probeFiles.length) {
    for (const f of probeFiles) {
      const P = loadProbe(f, holdSet); if (!P) continue;
      let hit = 0, tot = 0;
      for (const [i, g] of tNotes) {
        if (!holdSet.has(i)) continue;
        const ev = g.tail && g.tail.eventTime;
        const ws = P.m.get(i) && P.m.get(i)['0xA8'];
        if (ev == null || !ws) continue;
        tot++; if (ws.includes(ev)) hit++;
      }
      const rate = tot ? hit / tot : 0;
      probeStats.push({ file: f, lines: P.lines, a8: tot, a8Match: hit, rate: tot ? +(rate * 100).toFixed(2) : null });
      if (!probe || rate > probe.rate) probe = { ...P, rate };
    }
  }
  const c = r.summary.counts;
  const eng = { perfect: c.perfect, great: c.great, good: c.good, ok: c.ok, meh: c.meh, miss: c.miss };
  const tc = truth.summary.counts;
  const tr = { perfect: tc[0], great: tc[1], good: tc[2], ok: tc[3], meh: tc[4], miss: tc[5] };
  const diff = {}; let l1 = 0;
  for (const k of KEYS) { diff[k] = (eng[k] ?? 0) - (tr[k] ?? 0); l1 += Math.abs(diff[k]); }
  const declAcc = declAccV2(A(anchor).counts);
  const oursV2 = r.summary.accuracy && r.summary.accuracy.scoreV2;
  const tailConf = {}; const mismatch = [];
  let tTot = 0, tOk = 0;
  for (const [i, t] of tNotes) {
    if (!holdSet.has(i)) continue;
    const o = objOf.get(i); if (!o) continue;
    const g = t.game || {};
    if (g.judge == null) continue;
    tTot++;
    const e = tailOf.get(i);
    const got = e ? e.judgement : 'miss';
    const exp = MAPN[g.judge];
    if (got === exp) { tOk++; continue; }
    tailConf[exp + '->' + got] = (tailConf[exp + '->' + got] || 0) + 1;
    const path = dump.get(i) || [];
    const settle = path.filter(x => ['REL', 'FLUSH', 'DEFER', 'DEFER_MEH'].includes(x.path)).slice(-1)[0] || null;
    const pr = probe && probe.m.get(i);
    const row = {
      i, col: o.column, start: o.startTimeMs, end: o.endTimeMs,
      dur: o.endTimeMs != null ? o.endTimeMs - o.startTimeMs : null,
      truth: g.judge, engine: got,
      engineOff: e ? e.offsetMs : null, engineAt: e ? e.actualTimeMs : null,
      dtRel: e && e.actualTimeMs != null && o.endTimeMs != null ? e.actualTimeMs - o.endTimeMs : null,
      settle: settle ? settle.path : null, settleAt: settle ? settle.at : null,
      settleErr: settle && settle.err !== undefined ? settle.err : null,
      truthTailErr: t.tail ? t.tail.error : null,
      truthTailEv: t.tail ? t.tail.eventTime : null,
      truthTailSrc: t.tail ? t.tail.source : null,
      truthTailJudge: t.tail ? t.tail.judgement : null,
      truthHead: g.headJudge, truthHeadSrc: g.headSrc, truthHeadErr: g.headError,
      engineHead: headOf.has(i) ? headOf.get(i).judgement : null,
      engineHeadOff: headOf.has(i) ? headOf.get(i).offsetMs : null,
      a4: pr && pr['0xA4'] ? pr['0xA4'] : null,
      a8: pr && pr['0xA8'] ? pr['0xA8'] : null,
      jres: pr && pr['0x5C'] ? pr['0x5C'] : null,
      e0: pr && pr['0xE0'] ? pr['0xE0'] : null,
    };
    // 族: 先按「真值→我方」方向, 再按机制补充
    const fam = [];
    if (row.a8 && row.a8.length >= 2) fam.push('A8>=2');
    if (row.settle === null) fam.push('NO_TAIL_SETTLE');
    if (row.engineAt === 0) fam.push('STALE_REL0');
    if (row.settle === 'FLUSH') fam.push('FLUSH');
    if (row.settle === 'DEFER_MEH') fam.push('DEFER_MEH');
    if (row.dtRel != null && row.dtRel > 154.5) fam.push('PAST_EXPIRE');
    if (row.engineOff != null && row.engineOff < 0) fam.push('EARLY');
    if (row.engineOff != null && row.engineOff > 0) fam.push('LATE');
    if (row.truthHead === 'MISS' || row.engineHead === 'miss') fam.push('HEAD_MISS');
    row.fam = fam;
    mismatch.push(row);
  }
  mismatch.sort((a, b) => a.i - b.i);
  const rec = {
    anchor, truthFile, probeFile: probe ? probe.file : null, probeStats,
    engine: eng, truth: tr, l1, diff,
    acc: { oursV2, declAccV2: +declAcc.toFixed(5), dAcc: oursV2 == null ? null : +(oursV2 - declAcc).toFixed(5) },
    counts: r.summary.counts,
    tail: { total: tTot, agree: tOk, rate: +(tOk / Math.max(tTot, 1) * 100).toFixed(3), conf: tailConf, mismatch: mismatch.length },
    mismatches: mismatch,
  };
  out.maps[mtag] = rec;
  console.log('== ' + mtag + ' (' + anchor + ')');
  console.log('   counts L1', l1, JSON.stringify(diff), '| accV2', oursV2, 'decl', declAcc.toFixed(5), 'dAcc', rec.acc.dAcc);
  console.log('   tail  ', tOk + '/' + tTot, rec.tail.rate + '%', JSON.stringify(tailConf));
  if (probeStats.length) console.log('   probe ', JSON.stringify(probeStats));
  console.log('   mism  ', mismatch.length, JSON.stringify(mismatch.reduce((a, m) => { a[m.truth + '->' + m.engine] = (a[m.truth + '->' + m.engine] || 0) + 1; return a; }, {})));
  // 逐物件 md 表
  const lines = [];
  lines.push('### ' + mtag + ' (' + anchor + ') — hold_tail 不一致 ' + mismatch.length + ' 条 (一致 ' + tOk + '/' + tTot + ' = ' + rec.tail.rate + '%)');
  lines.push('');
  lines.push('| i | col | start | end | dur | 真值尾 | 我方尾 | 我方off | 我方at | dtRel | 结算路径 | truthTailErr | truthTailEv | src | 真值头/headSrc | 探针0xA4 | 探针0xA8 | JRes |');
  lines.push('|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|');
  for (const m of mismatch) {
    lines.push('| ' + [m.i, m.col, m.start, m.end, m.dur, m.truth, m.engine,
      m.engineOff == null ? '' : m.engineOff, m.engineAt, m.dtRel,
      (m.settle || '-') + (m.settleAt == null ? '' : '@' + m.settleAt),
      m.truthTailErr, m.truthTailEv, m.truthTailSrc,
      (m.truthHead || '') + '/' + (m.truthHeadSrc || ''),
      m.a4 ? m.a4.join('/') : '', m.a8 ? m.a8.join('/') : '', m.jres ? m.jres.join('/') : ''].join(' | ') + ' |');
  }
  mdChunks.push({ mtag, text: lines.join('\n') });
}
const l1sum = Object.values(out.maps).reduce((a, m) => a + (m.l1 || 0), 0);
out.totals = {
  l1Sum: l1sum,
  perMapL1: Object.fromEntries(Object.entries(out.maps).map(([k, v]) => [k, v.l1 ?? null])),
  tailMismatch: Object.fromEntries(Object.entries(out.maps).map(([k, v]) => [k, v.tail ? v.tail.mismatch : null])),
  tailTotal: Object.fromEntries(Object.entries(out.maps).map(([k, v]) => [k, v.tail ? v.tail.total : null])),
};
fs.writeFileSync(OUT + 'v2tail_residual_' + tag + '.json', JSON.stringify(out, null, 1));
for (const ch of mdChunks) {
  fs.writeFileSync(OUT + 'v2tail_residual_' + tag + '_' + ch.mtag + '.md', ch.text + '\n');
}
console.log('\nV2 L1 sum =', l1sum, JSON.stringify(out.totals.perMapL1));
console.log('wrote ' + OUT + 'v2tail_residual_' + tag + '.json');
