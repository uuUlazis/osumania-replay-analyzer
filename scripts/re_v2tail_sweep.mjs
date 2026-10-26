// re_v2tail_sweep.mjs — 汇总 V2 尾档/释放归属残差各配置的指标 (读 build/tmp_accupdate 下已有产物)
// 用法: node scripts/re_v2tail_sweep.mjs
// 输出: build/tmp_accupdate/v2tail_sweep.json
import fs from 'fs';
const O = 'build/tmp_accupdate/';
const rd = (f) => { try { return JSON.parse(fs.readFileSync(O + f, 'utf8')); } catch { return null; } };
const MAPS = ['xfpsb', 'ln13', 'twin', 'negai', 'ln12', 'trueblue'];
const N = { xfpsb: 16540, ln13: 17640, twin: 9532, negai: 5725, ln12: 15739, trueblue: 3978 };

function fromPoints(file, label, envNote) {
  const j = rd(file); if (!j) return null;
  const per = {};
  for (const m of MAPS) {
    const M = j.maps[m]; if (!M || M.error) { per[m] = { error: M && M.error }; continue; }
    const tail = M.phaseStat.hold_tail || { wErr: 0, total: 0, agree: 0 };
    const head = M.phaseStat.hold_head || { wErr: 0, total: 0, agree: 0 };
    const rice = M.phaseStat.note_head || { wErr: 0, total: 0, agree: 0 };
    per[m] = {
      l1: M.l1, diff: M.diff, dAcc: M.acc.dAcc, oursV2: M.acc.oursV2, declared2: M.acc.declared2,
      wErr: M.wErrSum,
      tail: { total: tail.total, agree: tail.agree, mism: tail.total - tail.agree, wErr: tail.wErr, conf: tail.conf },
      head: { total: head.total, agree: head.agree, mism: head.total - head.agree, wErr: head.wErr },
      rice: { total: rice.total, agree: rice.agree, mism: rice.total - rice.agree, wErr: rice.wErr },
      // 「尾层全修好」的上界 (只把 hold_tail 的加权误差清零):
      dAccIfTailPerfect: +(M.acc.dAcc - tail.wErr / (305 * N[m]) * 100).toFixed(5),
    };
  }
  return { label, env: envNote, envRaw: j.env, per, l1Sum: Object.values(per).reduce((a, x) => a + (x.l1 || 0), 0) };
}

const out = {
  at: new Date().toISOString(),
  maps: MAPS,
  configs: {},
  ln12exp: {},
  gates: {},
};
out.configs.base = fromPoints('v2points_base.json', '默认 (七门控 ON)', 'no env');
out.configs.t1 = fromPoints('v2points_t1a.json', 'T1 LN 晚绑定 (head 到期后 press 仍绑定)', 'MRA_RE_LNLATE_BIND=1');

// 12th 穿零归因
out.ln12exp = {
  'pre-D9 (SEL_V2=0,EXPIRE_FIRST=0)': fromPoints('v2points_pre_d9_ln12.json', 'pre-D9', 'MRA_RE_SEL_V2=0 MRA_RE_EXPIRE_FIRST=0'),
  'SEL_V2=0 (EXP=2)': fromPoints('v2points_selv0_ln12.json', 'selv2 off', 'MRA_RE_SEL_V2=0'),
  'SEL_V2=1 (EXP=0)': fromPoints('v2points_exp0_ln12.json', 'exp off', 'MRA_RE_EXPIRE_FIRST=0'),
};

// 逐物件尾残差枚举 (v2tail_residual_*)
const resid = {};
for (const [tag, file] of [['base', 'v2tail_residual_base.json'], ['t1', 'v2tail_residual_t1a.json'], ['t1b', 'v2tail_residual_t1b.json']]) {
  const j = rd(file); if (!j) continue;
  const per = {};
  for (const [m, v] of Object.entries(j.maps)) {
    if (v.error) { per[m] = { error: v.error }; continue; }
    per[m] = {
      tailTotal: v.tail.total, tailAgree: v.tail.agree, tailRate: v.tail.rate,
      tailMismatch: v.tail.mismatch, tailConf: v.tail.conf,
      l1: v.l1, dAccFromFormula: v.acc.dAcc,
      rows: v.mismatches.map((r) => ({
        i: r.i, col: r.col, start: r.start, end: r.end, dur: r.dur,
        truth: r.truth, engine: r.engine, engineOff: r.engineOff, engineAt: r.engineAt,
        settle: r.settle, truthTailErr: r.truthTailErr, truthTailSrc: r.truthTailSrc,
        truthHead: r.truthHead, headSrc: r.truthHeadSrc, fam: r.fam,
      })),
    };
  }
  resid[tag] = per;
}
out.residual = resid;

// 门禁
const reg = rd('regression_check_final.json');
const regT1 = rd('regression_t1chk.json');
const arb = rd('arb_gate_v1_final.json');
const arbT1 = rd('arb_gate_v1_t1.json');
out.gates = {
  regressionDefault: reg ? { tag: reg.tag, anchors: reg.anchors, truth: reg.truth, attribution: reg.gates.attribution } : null,
  regressionWithT1: regT1 ? { anchors: regT1.anchors, truth: regT1.truth, attribution: regT1.gates.attribution } : null,
  arbGateV1Default: arb ? arb.cases.map((c) => ({ id: c.id, dV1: c.dV1, scoreV1: c.scoreV1 })) : null,
  arbGateV1WithT1: arbT1 ? arbT1.cases.map((c) => ({ id: c.id, dV1: c.dV1, scoreV1: c.scoreV1 })) : null,
};
fs.writeFileSync(O + 'v2tail_sweep.json', JSON.stringify(out, null, 1));

// 控制台摘要
const line = (name, cfg) => {
  if (!cfg) return;
  console.log('== ' + name + ' | L1sum ' + cfg.l1Sum);
  for (const m of MAPS) {
    const p = cfg.per[m]; if (!p || p.error) { console.log('  ', m, 'ERR'); continue; }
    console.log('  ', m.padEnd(9), 'L1', String(p.l1).padStart(3), 'dAcc', String(p.dAcc).padStart(9),
      '| tail mism', String(p.tail.mism).padStart(2), 'wErr', String(p.tail.wErr).padStart(5),
      '| head wErr', String(p.head.wErr).padStart(5), '| dAccIfTailPerfect', String(p.dAccIfTailPerfect).padStart(9));
  }
};
line('base', out.configs.base);
line('T1', out.configs.t1);
for (const [k, v] of Object.entries(out.ln12exp)) {
  if (!v) continue;
  const p = v.per.ln12;
  console.log('ln12 ' + k.padEnd(34), 'L1', p.l1, 'dAcc', p.dAcc, 'tail mism', p.tail.mism, 'tail wErr', p.tail.wErr);
}
console.log('wrote ' + O + 'v2tail_sweep.json');
