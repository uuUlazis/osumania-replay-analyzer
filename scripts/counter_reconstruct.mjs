// counter_reconstruct.mjs — 方案 A 离线重建: counterlog jsonl -> 逐窗/逐点判定事件序列 + 真机指标
//
// 输入
//   <counterlog.jsonl>  RD/tools/counterlog.mjs 的产物 (tosu 6 档计数 + hitErrors 增量)
//   <session.json>      RD/output/2026-*.json (读取器一局的产物; 用 summary.counts 做自检基准)
//   [<v2points.json>]   可选: 引擎逐点表 (build/tmp_accupdate/v2points_*.json) -> 额外出评估块
//
// 做什么 (真机数据实测口径, 2026-10-21 第一次真机采集后重写)
//   1. 用 `ev:"st"` 的 playing 状态切局 (不认 ev:"run" —— 旧采集端把 hitErrors 的瞬时尾部截断
//      当成新一局, 一局能切出 700+ 个假局, 每个假局的首窗又把已落计数补一遍, 重建会虚高几十倍);
//   2. 逐窗差分用「逐档单调包络 + 夹到终值」: 真机里 tosu 偶发读到稍旧计数, 快照总和会小跌 1~20
//      再恢复 (一局 96 次), 直接做差会出现负差窗 / 差分不再 telescoping;
//      d[档] = min(终值, max(包络, 本快照)) - 包络, 于是 Σd 严格等于终值计数 - 首窗前缀;
//   3. 误差 -> 窗 对齐: 先试 order(按判定顺序 + 无输入MISS 槽位比例补偿) 与 arrival(按到达时刻,
//      lag 0/25/50/75/100ms) 两族, 用「|err| 独立分类与计数差多重集的精确命中数」自选最优
//      (真机选中的是 arrival+lag0, 精确命中 94.8%; 纯顺序只有 84.2%);
//   4. 窗内配对: 计数差多重集是硬约束, |err| 判定窗分类优先, 其余按 |err| 升序 ↔ 档位升序单调配对;
//   5. 自检: 逐档总数必须 == 会话 summary.counts (L1 = 0), 否则退出码 1;
//   6. 指标: 窗长(st/wall)分布、单判定窗/同档窗/歧义窗比例、逐点档位准确率 (对照会话真值与引擎逐点)、
//      面条头档位覆盖率 (计数减法)、以及同算法在不同窗长下的仿真上限 (--v2points 时全出)。
//
// 用法
//   node scripts/counter_reconstruct.mjs <counterlog.jsonl> <session.json> [<v2points.json>] [--tag x]
//   node scripts/counter_reconstruct.mjs --synth  <session.json> [--hz 30] [--tag x]
//       用会话真值合成一份「完美计数序列」jsonl (离线模拟采集, 无 osu! 也能验证全链路)
//   node scripts/counter_reconstruct.mjs --attrib <session.json> <v2points_xf4.json> [--tag x]
//       覆盖率/可归属性上限模拟: (时刻,档位) 事件 -> 物件, 以及「计数减法」能救回多少面条头真值
//
// 兼容旧日志: 读入时对 e 行按 n 去重 (旧采集端会把整个 hitErrors 数组重落 600+ 次), 分块流式读取。
//
// 产物: build/tmp_accupdate/counter_events_<tag>.json (--attrib 另出 counter_attrib_<tag>.json)

import fs from 'fs';
import path from 'path';

const OUT_DIR = 'build/tmp_accupdate';
const KEY = ['MAX', '300', '200', '100', '50', 'MISS'];   // 判定档位序 (与 summary.counts 同序)
const TIER = { MAX: 0, 300: 1, 200: 2, 100: 3, 50: 4, MISS: 5 };

// ------------------------------------------------------------------ CLI

function parseArgs(argv) {
  const o = { mode: 'reconstruct', tag: null, hz: 30, files: [], v2: null };
  for (let i = 0; i < argv.length; i++) {
    const a = argv[i];
    if (a === '--synth') o.mode = 'synth';
    else if (a === '--attrib') o.mode = 'attrib';
    else if (a === '--tag') o.tag = argv[++i];
    else if (a === '--hz') o.hz = Number(argv[++i]);
    else if (a === '--v2points' || a === '--v2') o.v2 = argv[++i];
    else if (a === '--help' || a === '-h') { console.log(fs.readFileSync(new URL(import.meta.url), 'utf8').split('\n').slice(1, 30).join('\n')); process.exit(0); }
    else o.files.push(a);
  }
  return o;
}

const o = parseArgs(process.argv.slice(2));
fs.mkdirSync(OUT_DIR, { recursive: true });
const rd = (f) => JSON.parse(fs.readFileSync(f, 'utf8'));
const slug = (f) => String(f).replace(/^.*[\\/]/, '').replace(/\.(json|jsonl)$/, '');
const fmt = (x, n) => Number(x).toFixed(n === undefined ? 2 : n);

// ------------------------------------------------------------------ 判定窗

// 与 RD/src/windows.mjs 同公式 (此处自带一份, 避免跨仓库 import 耦合)
const clamp = (v, a, b) => (v < a ? a : v > b ? b : v);
const range = (od, min, mid, max) => (od > 5 ? mid + ((max - mid) * (od - 5)) / 5 : od < 5 ? mid - ((mid - min) * (5 - od)) / 5 : mid);
function buildWindows(od, rate, scoreV2) {
  const ox = clamp(Number(od) || 0, 0, 10);
  const r = Number(rate || 1);
  const raw = scoreV2
    ? { perfect: range(ox, 22.4, 19.4, 13.9), great: range(ox, 64, 49, 34), good: range(ox, 97, 82, 67), ok: range(ox, 127, 112, 97), meh: range(ox, 151, 136, 121), miss: range(ox, 188, 173, 158) }
    : { perfect: 16, great: 34 + 3 * (10 - ox), good: 67 + 3 * (10 - ox), ok: 97 + 3 * (10 - ox), meh: 121 + 3 * (10 - ox), miss: 158 + 3 * (10 - ox) };
  const out = {};
  for (const k of ['perfect', 'great', 'good', 'ok', 'meh', 'miss']) out[k] = Math.floor(raw[k] * r);
  return out;
}
function scale(w, f) { const o2 = {}; for (const k in w) o2[k] = w[k] * f; return o2; }

/** |err| -> 档位下标; 用 head 窗。返回 {head: tier, tail: tier} 便于和多重集对齐时择优 */
function classify(err, W, TW) {
  const a = Math.abs(Number(err));
  if (!Number.isFinite(a)) return { head: 5, tail: 5 };
  const one = (w) => (a <= w.perfect ? 0 : a <= w.great ? 1 : a <= w.good ? 2 : a <= w.ok ? 3 : a <= w.meh ? 4 : 5);
  return { head: one(W), tail: one(TW) };
}

// ------------------------------------------------------------------ 读 jsonl

/**
 * 分块逐行读 jsonl (真机第一版采出 557MB/669 万行, readFileSync+split 会吃掉几个 GB)。
 * 同时对 e 行按 n 去重: 旧版采集端会把整个 hitErrors 数组在每次「尾部截断」时重落一遍,
 * 同一 n 会出现 600+ 次 —— 去重后旧日志也能直接重建成真值 (保留首次出现 = 时序正确的那份)。
 */
function readCounterLog(file) {
  const out = { hdr: null, snaps: [], errs: [], runs: [], states: [], end: null, raw: 0, dups: 0 };
  const seenN = new Set();
  const CHUNK = 4 << 20;
  const fd = fs.openSync(file, 'r');
  const handle = (line) => {
    if (!line.trim()) return;
    let j; try { j = JSON.parse(line); } catch (e) { return; }
    out.raw += 1;
    if (j.ev === 'hdr') out.hdr = j;
    else if (j.ev === 'c') out.snaps.push({ t: j.t, st: j.st, counts: j.counts, combo: j.combo, score: j.score, live: j.live, liveWall: j.liveWall });
    else if (j.ev === 'e') {
      if (j.n !== undefined) { if (seenN.has(j.n)) { out.dups += 1; return; } seenN.add(j.n); }
      out.errs.push({ t: j.arrival !== undefined ? j.arrival : j.t, st: j.st, err: j.err, n: j.n });
    } else if (j.ev === 'run') out.runs.push(j);
    else if (j.ev === 'st') out.states.push(j);
    else if (j.ev === 'end') out.end = j;
  };
  try {
    const buf = Buffer.allocUnsafe(CHUNK);
    let carry = '';
    let n;
    while ((n = fs.readSync(fd, buf, 0, CHUNK, null)) > 0) {
      const text = carry + buf.toString('utf8', 0, n);
      const parts = text.split('\n');
      carry = parts.pop();
      for (const line of parts) handle(line);
    }
    if (carry) handle(carry);
  } finally { try { fs.closeSync(fd); } catch (e) {} }
  return out;
}

// ------------------------------------------------------------------ 重建 (真机数据)
//
// 真机第一次采集暴露的三个坑 (2026-10-21):
//   ① 采集端把 hitErrors 的「瞬时尾部截断」当成新一局, 整包重落 600+ 次 -> 557MB/669 万行。
//      (已在 RD/tools/counterlog.mjs 修掉; 现在 e 行只落新增项, n 单调。)
//   ② 旧重建把每条 `ev:"run"`(包括那些 errors-reset) 都当成一局的开头, 每个「局」的
//      首个快照又按「首窗前已发生的判定」整包补一遍 —— 727 个假局 × 每局首快照的计数,
//      重建出的 MAX 直接变成 618091 (真值 10005)。→ 现在只用 playing 状态切局,
//      且「首窗补前缀」每局只做一次。
//   ③ tosu 偶发读到稍旧的计数, 快照总和会**小跌 1~20** 再恢复 (真机 96 次)。
//      旧实现把这种窗当「负差窗」整窗丢掉, 差分不再telescoping。
//      → 现在用「逐档单调包络」(running max): d = max(0, cur - env); env = max(env, cur)。
//      小跌被吸收, 差分严格 telescoping 到终值, 自检 L1 恒为 0。
const PLAY_RE = /^(play|playing|replay|replaying|watch|watching|spectate|spectating)$/i;
const sum6 = (c) => c.reduce((a, b) => a + b, 0);
const pct = (a, b) => (b ? +(a / b * 100).toFixed(2) : null);

function emptyDiag() {
  return {
    windows: 0, activeWindows: 0, singleWindows: 0, sameTierWindows: 0, maxK: 0,
    windowSizes: {}, sizesMs: [], sizesWallMs: [],
    paired: 0, exactClass: 0, noErrorSlots: 0, chordExtra: 0, prefixEvents: 0,
    negativeWindows: 0, lateAssigned: 0, earlyOrphan: 0, plays: 0,
  };
}

/** 用 playing 状态切局; 没有状态行时退回「整段一局」 */
function splitPlays(log) {
  const states = log.states.slice().sort((a, b) => a.t - b.t);
  const ivs = [];
  let cur = null;
  for (const s of states) {
    const playing = s.playing !== undefined ? !!s.playing : PLAY_RE.test(String(s.state || '').trim());
    if (playing && !cur) cur = { start: s.t, end: Infinity, states: [s] };
    else if (playing && cur) cur.states.push(s);
    else if (!playing && cur) { cur.end = s.t; ivs.push(cur); cur = null; }
  }
  if (cur) ivs.push(cur);
  if (!ivs.length) ivs.push({ start: -Infinity, end: Infinity, states: [] });
  for (const iv of ivs) {
    iv.snaps = log.snaps.filter((s) => s.t >= iv.start - 1 && s.t <= iv.end + 1);
    iv.errs = log.errs.filter((e) => e.t >= iv.start - 1 && e.t <= iv.end + 1);
    iv.finalCounts = iv.snaps.length ? iv.snaps[iv.snaps.length - 1].counts.slice() : [0, 0, 0, 0, 0, 0];
  }
  return ivs;
}

/** 窗内配对: 计数差多重集是硬约束; 前两轮用判定窗, 兜底用 |err| 单调配对 */
function pairWindow(slots, cands, diag) {
  const used = new Array(slots.length).fill(false);
  const assign = new Array(slots.length).fill(null);
  for (const p of cands) {
    if (p.c.head !== p.c.tail) continue;            // 头窗/尾窗结论不一致 -> 留给单调配对
    const idx = slots.findIndex((s, i) => !used[i] && s === p.c.head);
    if (idx >= 0) { used[idx] = true; assign[idx] = { e: p.e, exact: true }; p.done = true; }
  }
  const rest = cands.filter((p) => !p.done).sort((a, b) => Math.abs(a.e.err) - Math.abs(b.e.err));
  const free = [];
  for (let i = 0; i < slots.length; i++) if (!used[i]) free.push(i);
  free.sort((x, y) => slots[x] - slots[y]);
  const extra = [];
  for (let i = 0; i < rest.length; i++) {
    if (i < free.length) {
      const si = free[i];
      used[si] = true;
      const near = Math.min(Math.abs(slots[si] - rest[i].c.head), Math.abs(slots[si] - rest[i].c.tail));
      assign[si] = { e: rest[i].e, exact: near === 0 };
      rest[i].done = true;
    } else extra.push(rest[i].e);
  }
  return { assign, extra };
}

/**
 * 把一局的「计数快照 + 误差流」还原成逐窗/逐点事件序列。
 * 事件 { t, st, tier, err, src, amb, win }
 *   src: prefix | single | group | loose | orphan
 * 窗口 { i, t0,t1, st0,st1, sizeMs, sizeWallMs, diff, k, sameTier, eFrom, eTo }
 */
/**
 * 误差 -> 窗口 的对齐器。两种策略, 用「精确分类命中率」自选:
 *   order   : 按判定顺序, 每窗取 k 条 —— 但真机有 88 个「无输入 MISS」没有误差,
 *             纯顺序取 k 条会让误差流整体前移 (到局中偏 ~44 条 = ~11 个窗),
 *             所以按 (总槽位-总误差)/总槽位 的比例跳过相应槽位。
 *   arrival : 计数在 tosu 下一次读内存之后才可见, 所以「到达时刻 t 的误差」属于
 *             第一个满足 b.t >= t + lag 的快照窗 (lag = 读内存到分发之间的平均滞后)。
 */
function makeAligner(kind, elist, totalSlots, totalErrors) {
  let ei = 0;
  let acc = 0;
  const pending = [];
  const missR = totalSlots > 0 ? Math.max(0, (totalSlots - totalErrors) / totalSlots) : 0;
  const lag = kind === 'order' ? 0 : (kind.lag || 0);
  return {
    missR,
    /** 返回本窗的候选误差 (不足 = 该窗有无输入 MISS 槽; 多余留给下一窗) */
    next(k, winEndT) {
      const out = [];
      if (kind === 'order' || kind.type === 'order') {
        acc += k * missR;
        let skip = Math.floor(acc);
        acc -= skip;
        skip = Math.min(skip, k);
        let want = k - skip;
        while (want > 0 && ei < elist.length) { out.push(elist[ei++]); want -= 1; }
        return out;
      }
      while (ei < elist.length && elist[ei].t + lag <= winEndT) pending.push(elist[ei++]);
      while (out.length < k && pending.length) out.push(pending.shift());
      return out;
    },
    rest() { return pending.concat(elist.slice(ei)); },
  };
}

const ALIGN_KINDS = [
  { type: 'order' }, { type: 'arrival', lag: 0 }, { type: 'arrival', lag: 25 },
  { type: 'arrival', lag: 50 }, { type: 'arrival', lag: 75 }, { type: 'arrival', lag: 100 },
];
const alignName = (k) => (k.type === 'order' ? 'order(miss-skip)' : 'arrival+lag' + k.lag);

function rebuildPlay(iv, W, TW, diag, alignKind) {
  const events = [], windows = [];
  const snaps = iv.snaps.slice().sort((a, b) => a.t - b.t);
  const elist = iv.errs.slice().sort((a, b) => a.t - b.t);
  const totalSlots = snaps.length
    ? (sum6(snaps[snaps.length - 1].counts) - 0) : 0;
  const aligner = makeAligner(alignKind || 'order', elist, Math.max(totalSlots, elist.length), elist.length);
  const preK = snaps.length ? sum6(snaps[0].counts) : 0;
  const skipPrefix = preK;
  // 对齐器按「整局槽位」算 miss 比例时要扣掉首窗前的前缀
  aligner.missR = (totalSlots - skipPrefix - elist.length) > 0
    ? Math.max(0, (totalSlots - skipPrefix - elist.length) / Math.max(1, totalSlots - skipPrefix))
    : 0;

  const pushSlots = (slots, assign, win, k, sameTier) => {
    const eFrom = events.length;
    for (let i = 0; i < slots.length; i++) {
      const a = assign[i] || null;
      if (!a) {
        events.push({ t: win ? win.t1 : null, st: win ? win.st1 : null, tier: slots[i], err: null, src: win && k === 1 ? 'single' : 'loose', amb: !(win && k === 1), noErr: true, win: win ? win.i : -1 });
        diag.noErrorSlots += 1;
        continue;
      }
      diag.paired += 1;
      if (a.exact) diag.exactClass += 1;
      events.push({
        t: a.e.t, st: a.e.st, tier: slots[i], err: a.e.err,
        src: !win ? 'prefix' : k === 1 ? 'single' : sameTier ? 'group' : 'loose',
        amb: !!(win && k > 1 && !sameTier), win: win ? win.i : -1,
      });
    }
    return [eFrom, events.length];
  };

  // ---- 首窗前已发生的判定 (只在本局第一个快照上补一次) ----
  if (snaps.length) {
    const pre = snaps[0].counts.slice();
    const k = sum6(pre);
    if (k > 0) {
      const slots = [];
      for (let b = 0; b < 6; b++) for (let i = 0; i < pre[b]; i++) slots.push(b);
      const cand = aligner.next(k, snaps[0].t).map((e) => ({ e, c: classify(e.err, W, TW) }));
      pushSlots(slots, pairWindow(slots, cand, diag).assign, null, k, slots.every((x) => x === slots[0]));
      diag.prefixEvents += k;
    }
  }

  // ---- 逐窗差分 (单调包络, 且夹到终值: 真机也会偶发「计数虚高」后回落) ----
  // 包络从首个快照起算 —— 首窗前的那部分已经作为 prefix 事件补过, 不能再算一次。
  const final = snaps.length ? snaps[snaps.length - 1].counts.slice() : [0, 0, 0, 0, 0, 0];
  const env = snaps.length ? snaps[0].counts.slice() : [0, 0, 0, 0, 0, 0];
  for (let i = 1; i < snaps.length; i++) {
    const a = snaps[i - 1], b = snaps[i];
    const next = new Array(6);
    const d = new Array(6);
    let neg = false;
    for (let kk = 0; kk < 6; kk++) {
      next[kk] = Math.min(final[kk] | 0, Math.max(env[kk], b.counts[kk] | 0));
      d[kk] = next[kk] - env[kk];
      if ((b.counts[kk] | 0) < env[kk]) neg = true;
    }
    for (let kk = 0; kk < 6; kk++) env[kk] = next[kk];
    const k = d.reduce((x, y) => x + y, 0);
    diag.windows += 1;
    if (Number.isFinite(a.st) && Number.isFinite(b.st)) diag.sizesMs.push(b.st - a.st);
    diag.sizesWallMs.push(b.t - a.t);
    if (neg) diag.negativeWindows += 1;    // 只是诊断: 包络已经吸收掉了
    if (k === 0) continue;
    diag.windowSizes[k] = (diag.windowSizes[k] || 0) + 1;
    if (k > diag.maxK) diag.maxK = k;
    diag.activeWindows += 1;
    const slots = [];
    for (let t2 = 0; t2 < 6; t2++) for (let c = 0; c < d[t2]; c++) slots.push(t2);
    const sameTier = slots.every((x) => x === slots[0]);
    if (k === 1) diag.singleWindows += 1;
    if (sameTier) diag.sameTierWindows += 1;

    const win = {
      i: windows.length, t0: a.t, t1: b.t, st0: a.st, st1: b.st,
      sizeMs: (Number.isFinite(a.st) && Number.isFinite(b.st)) ? +(b.st - a.st).toFixed(1) : null,
      sizeWallMs: b.t - a.t, diff: d, k, sameTier,
      cls: k === 1 ? 'single' : sameTier ? 'group' : 'loose',
    };
    // 误差 -> 本窗 (见 makeAligner): 对齐方式由 reconstruct 用「精确分类命中率」自选
    const cand = aligner.next(k, b.t).map((e) => ({ e, c: classify(e.err, W, TW) }));
    let late = 0;
    for (const p of cand) if (p.e.t > b.t) late += 1;
    diag.lateAssigned += late;
    const pr = pairWindow(slots, cand, diag);
    diag.chordExtra += pr.extra.length;
    for (const x of pr.extra) events.push({ t: x.t, st: x.st, tier: null, err: x.err, src: 'orphan', amb: true, win: win.i });
    const [ef, et] = pushSlots(slots, pr.assign, win, k, sameTier);
    win.eFrom = ef; win.eTo = et;
    windows.push(win);
  }

  // 队列里剩下的误差: 计数从未确认过 -> 只能当「无档位」事件
  for (const e of aligner.rest()) {
    diag.chordExtra += 1; diag.earlyOrphan += 1;
    events.push({ t: e.t, st: e.st, tier: null, err: e.err, src: 'orphan', amb: true, win: -1 });
  }
  diag.align = { kind: alignKind && alignKind.type === 'order' ? 'order' : 'arrival', lag: (alignKind && alignKind.lag) || 0, missR: +aligner.missR.toFixed(5) };
  return { events, windows };
}

function dist(arr) {
  if (!arr.length) return null;
  const a = arr.slice().sort((x, y) => x - y);
  const q = (p) => a[Math.min(a.length - 1, Math.max(0, Math.floor(a.length * p)))];
  return { n: a.length, min: a[0], p5: q(0.05), p25: q(0.25), p50: q(0.5), p75: q(0.75), p95: q(0.95), max: a[a.length - 1], mean: +(a.reduce((x, y) => x + y, 0) / a.length).toFixed(2) };
}

function reconstruct(counterFile, sessionFile, tag, v2file) {
  const session = rd(sessionFile);
  const log = readCounterLog(counterFile);
  const bm = session.beatmap || {};
  const sc = session.score || {};
  const scoreV2 = sc.scoreV2 !== undefined ? !!sc.scoreV2 : true;
  const rate = Number(sc.rate || 1);
  const W = (sc.windows && sc.windows.perfect !== undefined) ? sc.windows : buildWindows(bm.od, rate, scoreV2);
  const TW = (sc.tailWindows && sc.tailWindows.perfect !== undefined) ? sc.tailWindows : (scoreV2 ? scale(W, 1.5) : W);

  const truth = (session.summary && session.summary.counts) || null;
  const plays = splitPlays(log);

  // ---- 选主局: 终值计数 == 会话 summary.counts 的那一局; 否则取判定数最多的一局 ----
  let main = null, mainWhy = '';
  if (truth) {
    main = plays.find((p) => p.finalCounts && p.finalCounts.every((v, i) => v === truth[i]));
    if (main) mainWhy = '终值计数 == summary.counts';
  }
  if (!main) {
    main = plays.slice().sort((a, b) => (b.snaps.length - a.snaps.length))[0];
    mainWhy = '快照最多 (没有一局的终值计数对上 summary.counts)';
  }
  main.states = main.states || [];

  // ---- 对齐方式自选: 谁的「精确分类命中数」最高就用谁 (无需真值) ----
  const trials = [];
  let best = null;
  for (const kind of ALIGN_KINDS) {
    const dg = emptyDiag();
    dg.plays = plays.length;
    const r = rebuildPlay(main, W, TW, dg, kind);
    const rec = {
      align: alignName(kind), exact: dg.exactClass, paired: dg.paired,
      noErrorSlots: dg.noErrorSlots, orphans: dg.chordExtra,
      exactShare: pct(dg.exactClass, dg.paired), missR: dg.align ? dg.align.missR : null,
      events: r.events.length,
    };
    trials.push(rec);
    if (!best || dg.exactClass > best.score) best = { score: dg.exactClass, kind, r, dg, rec };
  }
  const diag = best.dg;
  const { events, windows } = best.r;
  diag.alignTrials = trials;

  // ---- 逐窗统计 ----
  const sizes = dist(diag.sizesMs);
  const wallSizes = dist(diag.sizesWallMs);
  const active = diag.activeWindows;
  const cls = { single: 0, group: 0, loose: 0 };
  for (const w of windows) cls[w.cls] += 1;

  // ---- 自检: 逐档总数必须 == 会话 summary.counts ----
  const got = [0, 0, 0, 0, 0, 0];
  let withTier = 0, withErr = 0;
  for (const e of events) {
    if (e.tier !== null && e.tier !== undefined) { got[e.tier] += 1; withTier += 1; }
    if (e.err !== null && e.err !== undefined) withErr += 1;
  }
  const delta = truth ? truth.map((v, i) => got[i] - v) : null;
  const l1 = delta ? delta.reduce((a, b) => a + Math.abs(b), 0) : null;
  const ok = !!(truth && l1 === 0);

  // ---- 逐点/覆盖评估 (需要 --v2points 或第 3 个文件参数) ----
  let evalOut = null;
  if (v2file) {
    const truthPts = buildTruthPoints(session).pts.map((p) => ({ ...p, blind: p.part === 'head' && p.type === 'hold' }));
    const enginePts = buildEnginePoints(rd(v2file));
    evalOut = evaluateReal(events, windows, truthPts, enginePts, {
      od: bm.od, scoreV2, rate, W, TW,
      directHeadShare: nowHeadShare(session),
    });
  }

  const out = {
    tag, mode: 'reconstruct',
    generatedAt: new Date().toISOString(),
    inputs: { counterlog: counterFile, session: sessionFile, v2points: v2file || null },
    meta: {
      beatmap: [bm.artist, bm.title, bm.version].filter(Boolean).join(' - '), od: bm.od, keys: bm.keys,
      scoreV2, rate, windows: W, tailWindows: TW,
      tosu: log.hdr && log.hdr.tosu, hz: log.hdr && log.hdr.hz, maxMb: log.hdr && log.hdr.maxMb,
      counterLines: log.raw, snapshots: log.snaps.length, errors: log.errs.length,
      dupErrorsDropped: log.dups || 0,
      counterEnd: log.end ? {
        reason: log.end.reason, errors: log.end.errors, snapshots: log.end.snapshots,
        truncations: log.end.truncations, truncatedItems: log.end.truncatedItems,
        reanchors: log.end.reanchors, errRewinds: log.end.errRewinds, bytes: log.end.bytes,
      } : null,
      states: log.states.map((s) => s.state),
      plays: plays.map((p, i) => ({
        i, start: p.start === -Infinity ? null : p.start, end: p.end === Infinity ? null : p.end,
        snapshots: p.snaps.length, errors: p.errs.length, finalCounts: p.finalCounts,
        isMain: p === main,
      })),
      mainPlay: mainWhy,
    },
    check: {
      order: KEY, truth, rebuilt: got, delta, l1, pass: ok,
      finalCounts: main.finalCounts, finalCountsMatchTruth: !!(truth && main.finalCounts.every((v, i) => v === truth[i])),
      note: ok
        ? '重建档位总数 == 会话 summary.counts (差分严格 telescoping 到主局终值计数)'
        : '不一致: 主局终值计数与会话 summary.counts 不符 (采集被截断 / 不是同一局)',
    },
    stats: {
      events: events.length, withTier, withErr,
      pairRate: pct(withErr, events.length),
      windows: diag.windows, activeWindows: active, runs: diag.plays, maxEventsPerWindow: diag.maxK,
      singleWindowShare: pct(cls.single, active),
      sameTierWindowShare: pct(cls.group, active),
      looseWindowShare: pct(cls.loose, active),
      windowSizeMs: sizes, windowSizeWallMs: wallSizes,
      windowSizeHist: diag.windowSizes,
      exactClassShare: pct(diag.exactClass, diag.paired),
      noErrorSlots: diag.noErrorSlots, chordExtra: diag.chordExtra,
      prefixEvents: diag.prefixEvents, negativeWindows: diag.negativeWindows,
      lateAssigned: diag.lateAssigned,
      align: diag.align || null, alignTrials: diag.alignTrials || null,
    },
    windows,
    events,
    eval: evalOut,
  };
  const outFile = path.join(OUT_DIR, 'counter_events_' + (tag || slug(sessionFile)) + '.json');
  fs.writeFileSync(outFile, JSON.stringify(out));

  // ---- 控制台 ----
  console.log('== counter_reconstruct (真机数据) ==');
  console.log('jsonl   : ' + counterFile + '  (' + log.raw + ' 行 / 快照 ' + log.snaps.length + ' / 误差 ' + log.errs.length
    + (log.dups ? ' / 按 n 去重 ' + log.dups : '') + ')');
  console.log('session : ' + sessionFile + '  (' + out.meta.beatmap + ', od=' + bm.od + ', ' + (scoreV2 ? 'V2' : 'V1') + ')');
  if (log.end) console.log('采集端  : reason=' + log.end.reason + ' e=' + log.end.errors + ' 截断=' + (log.end.truncations || 0)
    + ' 省掉重复=' + (log.end.truncatedItems || 0) + ' 重锚=' + (log.end.reanchors || 0) + ' 体积=' + ((log.end.bytes || 0) / 1048576).toFixed(2) + 'MB');
  console.log('局      : ' + plays.length + ' 段 playing, 主局 #' + plays.indexOf(main) + ' (' + mainWhy + ')'
    + '  ' + main.snaps.length + ' 快照 / ' + main.errs.length + ' 误差');
  console.log('误差对齐: ' + best.rec.align + '  (精确分类命中 ' + best.rec.exact + '/' + best.rec.paired
    + ' = ' + best.rec.exactShare + '%, 无误差槽 ' + best.rec.noErrorSlots + ')');
  for (const tr of trials) {
    if (tr === best.rec) continue;
    console.log('          ' + tr.align.padEnd(18) + ' 精确 ' + String(tr.exact).padStart(5) + '/' + String(tr.paired).padStart(5) + ' = ' + String(tr.exactShare).padStart(6) + '%');
  }
  console.log('窗      : ' + KEY.map((k, i) => k + '=' + W[['perfect', 'great', 'good', 'ok', 'meh', 'miss'][i]]).join(' ') + '  (tail ×' + fmt(TW.perfect / W.perfect, 2) + ')');
  console.log('事件    : ' + events.length + '  (带档位 ' + withTier + ', 带误差 ' + withErr + ', 配对率 ' + out.stats.pairRate + '%)');
  console.log('窗口    : ' + diag.windows + ' 个 (有判定 ' + active + '), 最大 ' + diag.maxK + ' 判定/窗');
  if (sizes) console.log('窗长 st : p5=' + fmt(sizes.p5, 1) + ' p25=' + fmt(sizes.p25, 1) + ' p50=' + fmt(sizes.p50, 1) + ' p75=' + fmt(sizes.p75, 1) + ' p95=' + fmt(sizes.p95, 1) + ' max=' + fmt(sizes.max, 1) + ' ms');
  if (wallSizes) console.log('窗长墙钟: p5=' + wallSizes.p5 + ' p50=' + wallSizes.p50 + ' p95=' + wallSizes.p95 + ' max=' + wallSizes.max + ' ms');
  console.log('窗分类  : 单判定 ' + out.stats.singleWindowShare + '% / 同档 ' + out.stats.sameTierWindowShare + '% / 歧义 ' + out.stats.looseWindowShare + '%');
  console.log('自检    : ' + KEY.map((k) => k + ' ' + got[KEY.indexOf(k)]).join(' / '));
  if (truth) {
    console.log('真值    : ' + KEY.map((k) => k + ' ' + truth[KEY.indexOf(k)]).join(' / '));
    console.log('差异    : ' + KEY.map((k, i) => (delta[i] >= 0 ? '+' : '') + delta[i]).join(' / ') + '  L1=' + l1);
  }
  if (evalOut) printEval(evalOut);
  console.log('结果    : ' + (ok ? '✔ 自检通过' : '✘ 自检失败') + '  -> ' + outFile);
  return { out, outFile, ok };
}

// ------------------------------------------------------------------ 真机评估: 逐点档位 / 面条头覆盖

const TIER_BY_NAME = { perfect: 0, great: 1, good: 2, ok: 3, meh: 4, miss: 5 };

/** 引擎逐点参照 (v2points_xf4.json): {points:{"<idx>":{hold_head:{j,off,t},...}}, objects:[{i,col,type,start,end}]} */
function buildEnginePoints(v2) {
  const pts = [];
  const objs = Array.isArray(v2.objects) ? v2.objects : [];
  for (const k of Object.keys(v2.points || {})) {
    const rec = v2.points[k];
    const obj = objs[Number(k)] || {};
    for (const part of ['hold_head', 'hold_tail', 'note_head']) {
      const q = rec[part];
      if (!q || q.j === undefined || q.j === null) continue;
      const partName = part === 'hold_head' ? 'head' : part === 'hold_tail' ? 'tail' : 'head';
      const type = part === 'note_head' ? 'note' : 'hold';
      pts.push({
        note: Number(k), part: partName, type, col: obj.col, tier: TIER_BY_NAME[q.j],
        err: q.off === undefined || q.off === null ? null : Number(q.off),
        instant: Number(q.t), time: Number(q.t) - (q.off ? Number(q.off) : 0),
        blind: partName === 'head' && type === 'hold',
      });
    }
  }
  pts.sort((a, b) => a.instant - b.instant || a.note - b.note);
  return pts;
}

/**
 * 按误差值分组后「组内按时刻排序」顺序配对。
 * 误差值重复时靠时刻顺序对齐 (真机 |st-at| 中位 12ms); 偏离 > TOL 的配对明显是错配,
 * 单独剔掉并计入 timeRejected —— 否则重复误差值会把配对噪声算进档位准确率。
 */
const PAIR_TOL_MS = 600;
function pairByErr(A, B) {
  const ga = new Map(), gb = new Map();
  for (const x of A) {
    if (x.err === null || x.err === undefined) continue;
    const k = Number(x.err);
    if (!ga.has(k)) ga.set(k, []);
    ga.get(k).push(x);
  }
  for (const x of B) {
    if (x.err === null || x.err === undefined) continue;
    const k = Number(x.err);
    if (!gb.has(k)) gb.set(k, []);
    gb.get(k).push(x);
  }
  const pairs = [];
  let dupKeys = 0, rejected = 0;
  const devs = [];
  for (const [k, arr] of ga) {
    const brr = gb.get(k) || [];
    if (arr.length > 1 || brr.length > 1) dupKeys += 1;
    const ta = (x) => (x.instant !== undefined ? x.instant : x.st);
    arr.sort((x, y) => ta(x) - ta(y));
    brr.sort((x, y) => ta(x) - ta(y));
    const n = Math.min(arr.length, brr.length);
    for (let i = 0; i < n; i++) {
      const d = Math.abs(ta(arr[i]) - ta(brr[i]));
      if (d > PAIR_TOL_MS) { rejected += 1; continue; }
      devs.push(d);
      pairs.push([arr[i], brr[i]]);
    }
  }
  devs.sort((a, b) => a - b);
  return {
    pairs, dupKeys, timeRejected: rejected,
    distMs: devs.length ? { p50: devs[Math.floor(devs.length * 0.5)], p90: devs[Math.floor(devs.length * 0.9)], max: devs[devs.length - 1] } : null,
  };
}

function evaluateReal(events, windows, truthPts, enginePts, ctx) {
  const winOf = new Map(windows.map((w) => [w.i, w]));
  const withTier = events.filter((e) => e.tier !== null && e.tier !== undefined);

  const accuracy = (A, B) => {
    const { pairs, dupKeys, timeRejected, distMs } = pairByErr(A, B);
    let ok = 0;
    const confusion = {};
    const byCls = { single: [0, 0], group: [0, 0], loose: [0, 0], prefix: [0, 0], none: [0, 0] };
    for (const [a, b] of pairs) {
      const cls = a.src && byCls[a.src] ? a.src : 'none';
      byCls[cls][1] += 1;
      if (a.tier === b.tier) { ok += 1; byCls[cls][0] += 1; }
      else {
        const k = KEY[b.tier] + '->' + KEY[a.tier];
        confusion[k] = (confusion[k] || 0) + 1;
      }
    }
    return {
      matched: pairs.length, total: B.length, tierOk: ok,
      accuracy: pct(ok, pairs.length),
      accuracyOverTotal: pct(ok, B.length),
      dupKeys, timeRejected, pairDistMs: distMs,
      byClass: Object.fromEntries(Object.entries(byCls).map(([k, v]) => [k, { n: v[1], acc: pct(v[0], v[1]) }])),
      confusion,
    };
  };

  // (1) 逐点档位准确率 (参照: 会话真值 / 引擎逐点)
  const vsTruth = accuracy(withTier, truthPts);
  const vsEngine = accuracy(withTier, enginePts);

  // (2) 结构确定 vs 歧义: 单判定窗 / 同档窗的档位**按构造就是 100% 正确**,
  //     所以它们的实测值（<100%）就是本评估方法的测量噪声下限。
  const clsN = { single: 0, group: 0, loose: 0 }, clsPts = { single: 0, group: 0, loose: 0 };
  for (const w of windows) clsN[w.cls] += 1;
  for (const e of withTier) if (e.src && clsPts[e.src] !== undefined && e.win >= 0) clsPts[e.src] += 1;
  const nWin = windows.length;
  const nPts = clsPts.single + clsPts.group + clsPts.loose;
  const structural = {
    windows: { single: clsN.single, group: clsN.group, loose: clsN.loose, sharePct: pct(clsN.single + clsN.group, nWin) },
    points: { single: clsPts.single, group: clsPts.group, loose: clsPts.loose, sharePct: pct(clsPts.single + clsPts.group, nPts) },
    measuredNoiseFloorPts: (100 - (vsTruth.byClass.group.acc === null ? 100 : vsTruth.byClass.group.acc)).toFixed(2),
    mixedEstimatePts: +((
      (clsPts.single + clsPts.group) / Math.max(1, nPts) * 100
      + clsPts.loose / Math.max(1, nPts) * (vsTruth.byClass.loose.acc || 0)
    )).toFixed(2),
    note: '结构确定 = 单判定窗 + 同档窗 (档位多重集只剩一档, 按构造 100%);'
      + ' 歧义窗内部分配靠 |err| 分类 + 单调配对, 其准确率才是 A 的真实增量',
  };

  // (3) 面条头档位覆盖率: 用真机窗口的计数差做「减法」
  const coverReal = headCoverage(windows, truthPts, ctx.W, 1.0);

  // (3) 仿真: 同一套算法在任意窗长下的能力 + 与真机的交叉验证
  const sim = {};
  const simWin = [5, 10, 20, 33, 50, 100];
  for (const ms of simWin) sim[ms + 'ms'] = simulate(truthPts, ms, ctx);
  const realMedian = windows.length ? dist(windows.map((w) => w.sizeMs).filter((x) => x !== null)) : null;
  const quant = realMedian ? Math.round(realMedian.p50) : null;

  return {
    note: 'accuracy = 重建 (tier) vs 参照逐点, 按误差值分组配对 (组内按时刻排序, 偏差 >600ms 剔除)',
    vsTruth, vsEngine, structural,
    engineRefCircular: engineCircularity(enginePts, ctx.W, ctx.TW),
    head: {
      baselineDirect: ctx.directHeadShare,
      realWindows: coverReal,
      headTotal: truthPts.filter((p) => p.blind).length,
    },
    sim,
    simAtRealMedian: quant ? { windowMs: quant, ...simulate(truthPts, quant, ctx) } : null,
    realMedianWindowMs: quant,
  };
}

/**
 * 引擎逐点参照的「自洽性」: v2points 的档位与它自己的 off 用同一套判定窗算出来的一致率。
 * 若接近 100%, 说明引擎档位本身就是用 |err| 推出来的 —— 拿它当参照评估 A 的档位是循环论证,
 * 只能当「与现状流水线的一致率」看。
 */
function engineCircularity(enginePts, W, TW) {
  let n = 0, agree = 0;
  for (const p of enginePts) {
    if (p.err === null || p.err === undefined) continue;
    n += 1;
    const w = p.part === 'tail' ? TW : W;
    const a = Math.abs(p.err);
    const guess = a <= w.perfect ? 0 : a <= w.great ? 1 : a <= w.good ? 2 : a <= w.ok ? 3 : a <= w.meh ? 4 : 5;
    if (guess === p.tier) agree += 1;
  }
  return { n, agree, share: pct(agree, n), circular: pct(agree, n) >= 98 };
}

/** 现状: 面条头里「直读真值」的占比 (headSrc === 'event'), 就是 35.1% 那个口径 */
function nowHeadShare(session) {
  let heads = 0, direct = 0;
  const src = {};
  for (const n of session.notes || []) {
    if (n.type !== 'hold') continue;
    heads += 1;
    const s = (n.game || {}).headSrc || '(none)';
    src[s] = (src[s] || 0) + 1;
    if (s === 'event') direct += 1;
  }
  return { heads, direct, share: pct(direct, heads), headSrc: src };
}

/**
 * 面条头档位覆盖率 (计数减法, 真机窗口):
 *   窗内计数差 - 窗内「非面条头」判定的已知档位 = 面条头这一窗的档位残差
 *   残差只剩 1 档 -> 这一窗所有面条头都定 (整组同档); 只剩 1 个头且残差 1 -> 单头锁定
 * tailVis < 1 时按比例随机「看不见」一部分非头判定, 模拟慢轮询下尾/rice 档位缺口。
 */
function headCoverage(windows, pts, W, tailVis, seed0, halfOpen) {
  // 把真值点按 instant 归到 st 区间
  const bounds = windows
    .filter((w) => w.st0 !== null && w.st1 !== null)
    .map((w) => ({ w, lo: w.st0, hi: w.st1 }))
    .sort((a, b) => a.lo - b.lo);
  const bucket = new Map();     // window.i -> {heads:[], others:[]}
  let unassigned = 0;
  for (const p of pts) {
    let hit = null;
    // 窗口约定: 快照 b 的计数差 = 判定时刻落在 (a.st, b.st] 的判定 (计数在 b 这一读之后才可见)。
    // 窗口按 st 有序 -> 二分找最后一个 lo < instant 的窗口, 再判 instant <= hi。
    let lo = 0, hi = bounds.length - 1, best = null;
    while (lo <= hi) {
      const mid = (lo + hi) >> 1;
      const left = halfOpen ? bounds[mid].lo <= p.instant : bounds[mid].lo < p.instant;
      if (left) { best = bounds[mid]; lo = mid + 1; } else hi = mid - 1;
    }
    if (best && (halfOpen ? p.instant < best.hi : p.instant <= best.hi)) hit = best.w;
    if (!hit) { unassigned += 1; continue; }
    if (!bucket.has(hit.i)) bucket.set(hit.i, { heads: [], others: [] });
    const b = bucket.get(hit.i);
    if (p.blind) b.heads.push(p); else b.others.push(p);
  }
  let seed = seed0 === undefined ? 12345 : seed0;
  const rnd = () => { seed = (seed * 1103515245 + 12345) & 0x7fffffff; return seed / 0x7fffffff; };
  const stat = { exact: 0, group: 0, amb: 0, none: 0, inconsistent: 0 };
  const perTier = {};
  for (const [wi, b] of bucket) {
    if (!b.heads.length) continue;
    const w = windows[wi];
    const rem = w.diff.slice();
    const vis = [];
    for (const p of b.others) if (tailVis >= 1 || rnd() < tailVis) vis.push(p);
    let bad = false;
    for (const p of vis) { rem[p.tier] -= 1; if (rem[p.tier] < 0) bad = true; }
    if (bad) { stat.inconsistent += b.heads.length; continue; }
    const s = rem.reduce((a, c) => a + c, 0);
    const nz = rem.filter((c) => c > 0).length;
    if (s === 0) { stat.none += b.heads.length; continue; }
    if (b.heads.length === 1 && s === 1) {
      stat.exact += 1;
      const t = rem.findIndex((c) => c > 0);
      perTier[t] = (perTier[t] || 0) + 1;
    } else if (nz === 1 && s === b.heads.length) {
      stat.group += b.heads.length;
      const t = rem.findIndex((c) => c > 0);
      perTier[t] = (perTier[t] || 0) + b.heads.length;
    } else stat.amb += b.heads.length;
  }
  const tot = stat.exact + stat.group + stat.amb + stat.none + stat.inconsistent;
  return {
    scope: '面条头 (LN head)', heads: tot, unassignedBySt: unassigned, tailVis,
    determined: stat.exact + stat.group,
    determinedShare: pct(stat.exact + stat.group, tot),
    singleHeadShare: pct(stat.exact, tot),
    sameTierGroupShare: pct(stat.group, tot),
    ambiguousShare: pct(stat.amb, tot),
    noResidualShare: pct(stat.none, tot),
    inconsistentShare: pct(stat.inconsistent, tot),
    detail: stat, byTier: perTier,
  };
}

/** 用真值点表仿真「完美计数序列」在给定窗长下的表现 (与采集端无关, 只验证算法上限) */
function simulate(pts, winMs, ctx) {
  const W = ctx.W, TW = ctx.TW;
  const evs = [];
  const wins = [];
  const sorted = pts.slice().sort((a, b) => a.instant - b.instant);
  // 每 winMs 一个窗 (按 winMs 网格切; 与 synth 的量化一致)
  let i = 0;
  while (i < sorted.length) {
    const lo = sorted[i].instant - (sorted[i].instant % winMs);
    const hi = lo + winMs;
    const inWin = [];
    while (i < sorted.length && sorted[i].instant < hi) { inWin.push(sorted[i++]); }
    const d = [0, 0, 0, 0, 0, 0];
    for (const p of inWin) d[p.tier] += 1;
    const slots = [];
    for (let t2 = 0; t2 < 6; t2++) for (let c = 0; c < d[t2]; c++) slots.push(t2);
    const sameTier = slots.every((x) => x === slots[0]);
    const wi = wins.length;
    wins.push({ i: wi, st0: lo, st1: hi, diff: d, k: slots.length, sameTier, cls: slots.length === 1 ? 'single' : sameTier ? 'group' : 'loose' });
    const cand = inWin.filter((p) => p.err !== null && p.err !== undefined).map((p) => ({ e: { err: p.err, st: p.instant }, c: classify(p.err, W, TW) }));
    const pr = pairWindow(slots, cand, { noErrorSlots: 0, exactClass: 0, paired: 0, chordExtra: 0 });
    for (let s = 0; s < slots.length; s++) {
      const a = pr.assign[s];
      evs.push({ tier: slots[s], err: a ? a.e.err : null, st: a ? a.st : hi, src: slots.length === 1 ? 'single' : sameTier ? 'group' : 'loose', win: wi });
    }
  }
  const withErr = evs.filter((e) => e.err !== null && e.err !== undefined);
  const { pairs } = pairByErr(withErr, pts);
  let ok = 0;
  const byCls = { single: [0, 0], group: [0, 0], loose: [0, 0] };
  for (const [a, b] of pairs) {
    const cls = byCls[a.src] ? a.src : 'loose';
    byCls[cls][1] += 1;
    if (a.tier === b.tier) { ok += 1; byCls[cls][0] += 1; }
  }
  const active = wins.length;
  const clsN = { single: 0, group: 0, loose: 0 };
  for (const w of wins) clsN[w.cls] += 1;
  const cover = headCoverage(wins.map((w) => ({ ...w, sizeMs: winMs })), pts, W, 1.0, 424242, true);
  const coverLow = headCoverage(wins.map((w) => ({ ...w, sizeMs: winMs })), pts, W, 0.52, 777, true);
  return {
    windowMs: winMs, windows: active,
    singleWindowShare: pct(clsN.single, active), sameTierWindowShare: pct(clsN.group, active), looseWindowShare: pct(clsN.loose, active),
    tierAccuracy: pct(ok, pairs.length),
    byClass: Object.fromEntries(Object.entries(byCls).map(([k, v]) => [k, pct(v[0], v[1])])),
    headDetermined: cover.determinedShare, headSingle: cover.singleHeadShare, headGroup: cover.sameTierGroupShare,
    headAmbiguous: cover.ambiguousShare,
    headDeterminedTail52: coverLow.determinedShare,
  };
}

function printEval(ev) {
  console.log('-- 逐点档位 (参照 = 会话真值) --');
  console.log('  配对 ' + ev.vsTruth.matched + '/' + ev.vsTruth.total + ' (时刻容差剔除 ' + ev.vsTruth.timeRejected + ')  档位正确 '
    + ev.vsTruth.tierOk + ' = ' + ev.vsTruth.accuracy + '% (按全部真值点 ' + ev.vsTruth.accuracyOverTotal + '%)');
  if (ev.vsTruth.pairDistMs) console.log('  配对时刻差: p50 ' + ev.vsTruth.pairDistMs.p50 + 'ms, p90 ' + ev.vsTruth.pairDistMs.p90 + 'ms');
  const bc = ev.vsTruth.byClass;
  console.log('  单判定窗 ' + (bc.single.acc === null ? '-' : bc.single.acc + '%(' + bc.single.n + ')')
    + ' / 同档窗 ' + (bc.group.acc === null ? '-' : bc.group.acc + '%(' + bc.group.n + ')')
    + ' / 歧义窗 ' + (bc.loose.acc === null ? '-' : bc.loose.acc + '%(' + bc.loose.n + ')'));
  const cf = Object.entries(ev.vsTruth.confusion).sort((a, b) => b[1] - a[1]).slice(0, 5);
  if (cf.length) console.log('  混淆: ' + cf.map(([k, v]) => k + ' ' + v).join(', '));
  console.log('-- 逐点档位 (参照 = 引擎 v2points) --');
  console.log('  配对 ' + ev.vsEngine.matched + '/' + ev.vsEngine.total + '  档位正确 ' + ev.vsEngine.tierOk + ' = ' + ev.vsEngine.accuracy + '%'
    + (ev.engineRefCircular.circular ? '   ⚠ 该参照的档位本身就是 |err| 推出来的 (自洽 ' + ev.engineRefCircular.share + '%), 属循环论证, 只看一致性' : ''));
  const st2 = ev.structural;
  console.log('-- 结构确定 vs 歧义 --');
  console.log('  窗口: 单判定 ' + st2.windows.single + ' + 同档 ' + st2.windows.group + ' = ' + st2.windows.sharePct + '%  (这些窗档位按构造 100% 正确)');
  console.log('  判定点: 结构确定 ' + st2.points.sharePct + '%  (实测上限噪声: 同档窗应为 100%, 实测 ' + ev.vsTruth.byClass.group.acc + '%)');
  console.log('  逐点准确率重估 (结构确定记 100% + 歧义窗实测): ' + st2.mixedEstimatePts + '%');
  console.log('-- 面条头档位覆盖 (现状直读 ' + ev.head.baselineDirect.share + '%) --');
  const hr = ev.head.realWindows;
  console.log('  真机窗口: 确定 ' + hr.determinedShare + '% (单头 ' + hr.singleHeadShare + '% + 同档组 ' + hr.sameTierGroupShare
    + '%)  歧义 ' + hr.ambiguousShare + '%  残差空 ' + hr.noResidualShare + '%  不一致 ' + hr.inconsistentShare + '%  [头 ' + hr.heads + ']');
  console.log('-- 仿真上限 (同一算法, 不同窗长) --');
  console.log('  窗长     单判定窗  同档窗   逐点档位   头确定(尾全可见/52%)');
  for (const k of Object.keys(ev.sim)) {
    const s = ev.sim[k];
    console.log('  ' + k.padEnd(8) + String(s.singleWindowShare).padStart(7) + '%' + String(s.sameTierWindowShare).padStart(8) + '%'
      + String(s.tierAccuracy).padStart(10) + '%' + String(s.headDetermined).padStart(12) + '%/' + s.headDeterminedTail52 + '%');
  }
  if (ev.simAtRealMedian) {
    const s = ev.simAtRealMedian;
    console.log('  交叉验证: 真机中位窗长 ' + s.windowMs + 'ms 的仿真 = 单判定窗 ' + s.singleWindowShare + '% / 同档窗 '
      + s.sameTierWindowShare + '% / 逐点档位 ' + s.tierAccuracy + '%');
  }
}

// ------------------------------------------------------------------ 真值点表 (合成/归属性分析共用)

/**
 * 从会话 JSON 取出「判定点真值」: 每个 V2 判定点 (rice / 面条头 / 面条尾) 的
 *   档位 (与游戏计数同口径的真值), 谱面时刻 (物件时间), 误差 (来自误差流绑定), 判定时刻。
 * 关键性质: 这张表按档位汇总 == 会话 summary.counts, 所以它等价于
 * 「采集到完美计数序列」时的真值源 (见 synth 打印的计数终值与 ✔)。
 */
function buildTruthPoints(session) {
  const bm = session.beatmap || {}, sc = session.score || {};
  const scoreV2 = sc.scoreV2 !== undefined ? !!sc.scoreV2 : true;
  const W = (sc.windows && sc.windows.perfect !== undefined) ? sc.windows : buildWindows(bm.od, Number(sc.rate || 1), scoreV2);
  const errBy = new Map();
  for (const e of session.events || []) {
    if (e.error === null || e.error === undefined) continue;
    errBy.set(e.note + '|' + e.part, Number(e.error));
  }
  const pts = [];
  for (const n of session.notes || []) {
    const g = n.game || {};
    if (n.type === 'hold') {
      if (g.headJudge) {
        const err = errBy.has(n.index + '|head') ? errBy.get(n.index + '|head') : null;
        pts.push({ note: n.index, part: 'head', col: n.column, type: n.type, time: n.startTime, tier: TIER[g.headJudge], err, instant: n.startTime + (err === null ? W.meh : err) });
      }
      if (g.judge) {
        const err = errBy.has(n.index + '|tail') ? errBy.get(n.index + '|tail') : null;
        pts.push({ note: n.index, part: 'tail', col: n.column, type: n.type, time: n.endTime, tier: TIER[g.judge], err, instant: n.endTime + (err === null ? W.meh : err) });
      }
    } else if (g.judge) {
      const err = errBy.has(n.index + '|head') ? errBy.get(n.index + '|head') : null;
      pts.push({ note: n.index, part: 'head', col: n.column, type: n.type, time: n.startTime, tier: TIER[g.judge], err, instant: n.startTime + (err === null ? W.meh : err) });
    }
  }
  pts.sort((a, b) => a.instant - b.instant || a.note - b.note);
  return { pts, W, scoreV2 };
}

// ------------------------------------------------------------------ 合成一份「完美计数序列」(离线模拟)

function synth(sessionFile, hz, tag) {
  const session = rd(sessionFile);
  const { pts } = buildTruthPoints(session);
  const period = 1000 / Math.max(1, Math.min(100, hz));
  const t0ms = 1_700_000_000_000;   // 合成墙钟起点 (单调即可)
  const lines = [];
  lines.push({ t: t0ms, ev: 'hdr', iso: new Date(t0ms).toISOString(), tool: 'counter_reconstruct.mjs --synth', v: 1, tosu: 'synthetic', hz, countOrder: KEY, note: '由会话真值合成的完美计数序列 (离线模拟, 无 osu!)' });
  lines.push({ t: t0ms, ev: 'st', state: 'Playing', playing: true, st: pts.length ? +pts[0].instant.toFixed(2) : 0, synthetic: true });
  const counts = [0, 0, 0, 0, 0, 0];
  let i = 0, ei = 0, nSnap = 0;
  const lastAt = pts.length ? pts[pts.length - 1].instant : 0;
  const start = pts.length ? Math.floor(pts[0].instant) : 0;
  for (let st = start; st <= lastAt + period * 2; st += period) {
    while (i < pts.length && pts[i].instant <= st) {
      const p = pts[i];
      counts[p.tier] += 1;
      if (p.err !== null) {
        lines.push({ t: Math.round(t0ms + p.instant), ev: 'e', err: p.err, arrival: Math.round(t0ms + p.instant), n: ei++, st: +p.instant.toFixed(2) });
      }
      i++;
    }
    lines.push({ t: Math.round(t0ms + st), ev: 'c', counts: counts.slice(), combo: null, score: null, st });
    nSnap += 1;
  }
  lines.push({ t: Math.round(t0ms + lastAt + period * 2), ev: 'end', reason: 'synth', counts: counts.slice(), snapshots: nSnap, errors: ei });
  const outFile = path.join(OUT_DIR, 'counterlog_synth_' + (tag || slug(sessionFile)) + '.jsonl');
  fs.writeFileSync(outFile, lines.map((l) => JSON.stringify(l)).join('\n') + '\n');
  const truth = (session.summary || {}).counts;
  console.log('== synth (完美计数序列, 离线模拟) ==');
  console.log('会话    : ' + sessionFile + '  判定点 ' + pts.length + ' 条 (' + KEY.map((k, idx) => k + ' ' + counts[idx]).join('/') + ')');
  console.log('真值    : summary.counts ' + JSON.stringify(truth) + '  ' + (JSON.stringify(truth) === JSON.stringify(counts) ? '✔ 一致' : '✘ 不一致'));
  console.log('采样    : ' + hz + 'Hz (' + fmt(period, 2) + 'ms), 误差 ' + ei + ' 条 (miss 等无输入判定没有误差)');
  console.log('产物    : ' + outFile + '  (' + lines.length + ' 行)');
  return outFile;
}

// ------------------------------------------------------------------ 可归属性 / 覆盖率上限模拟

function attrib(sessionFile, v2file, tag) {
  const session = rd(sessionFile);
  const v2 = rd(v2file);
  const { pts, W, scoreV2 } = buildTruthPoints(session);
  const bm = session.beatmap || {};
  const N = pts.length;

  // (0) 校验: 点表档位汇总必须 == summary.counts (否则真值源不可信)
  const ptCounts = [0, 0, 0, 0, 0, 0];
  for (const p of pts) ptCounts[p.tier] += 1;
  const truth = (session.summary || {}).counts || null;
  const ptCountsOk = !!(truth && JSON.stringify(ptCounts) === JSON.stringify(truth));

  // (1) 点时刻碰撞: 只靠「时刻」能唯一确定物件吗
  const groups = new Map();
  for (const p of pts) { if (!groups.has(p.time)) groups.set(p.time, []); groups.get(p.time).push(p); }
  const groupSizeHist = {};
  let uniqueT = 0, colDistinctGroups = 0, colAmbGroups = 0;
  for (const [, g] of groups) {
    groupSizeHist[g.length] = (groupSizeHist[g.length] || 0) + 1;
    if (g.length === 1) uniqueT += 1;
    const cols = new Set(g.map((p) => p.col));
    if (cols.size === g.length) colDistinctGroups += 1; else colAmbGroups += 1;
  }

  // (2) 「时刻 + 误差」可归属性: tau = 判定时刻 - 误差 ≈ 点的谱面时刻
  const tolTable = {};
  for (const tol of [0, 2, 5, 10]) {
    let uniq = 0, amb = 0, none = 0;
    for (const e of pts) {
      if (e.err === null) { none += 1; continue; }
      const tau = e.instant - e.err;
      let c = 0;
      for (const p of pts) { if (Math.abs(p.time - tau) <= tol) c += 1; if (p.time > tau + tol) break; }
      if (c === 1) uniq += 1; else if (c > 1) amb += 1; else none += 1;
    }
    tolTable[tol] = { unique: uniq, ambiguous: amb, none, uniqueShare: +(uniq / N * 100).toFixed(2) };
  }

  // (3) 计数减法能救回多少「面条头」档位真值
  //     头档位 = 窗内计数差 - 窗内已知(内存直读)的非头档位
  //       单头窗 + 非头可见 -> 直接锁定; 多头窗但余量只剩单一档 -> 整组同档; 否则歧义
  //     tailVis: 非头判定档位的可见率 (本会话 notesfast 10ms -> ~100%; 慢轮询实况 ~52%)
  const winTable = {};
  // 只看面条头 (LN head): 这才是「MAX 头 320->320 直读不到」的盲区;
  // rice 头的最终值靠 done 0->1 转换可见, 不在盲区里。
  for (const p of pts) p.blind = (p.part === 'head' && p.type === 'hold');
  const headPts = pts.filter((p) => p.blind);
  const byTierHead = [0, 0, 0, 0, 0, 0];
  for (const p of headPts) byTierHead[p.tier] += 1;
  let seed = 12345;
  const rnd = () => { seed = (seed * 1103515245 + 12345) & 0x7fffffff; return seed / 0x7fffffff; };
  for (const winMs of [10, 20, 33, 50, 100]) {
    for (const tailVis of [1.0, 0.52]) {
      const runs = tailVis === 1 ? 1 : 5;
      let exact = 0, group = 0, amb2 = 0, bad = 0;
      for (let r = 0; r < runs; r++) {
        let k = 0;
        while (k < pts.length) {
          const w0 = pts[k].instant;
          let kk = k; const d = [0, 0, 0, 0, 0, 0]; const inWin = [];
          while (kk < pts.length && pts[kk].instant < w0 + winMs) { d[pts[kk].tier] += 1; inWin.push(pts[kk]); kk++; }
          const hs = inWin.filter((p) => p.blind);
          if (hs.length) {
            const rem = d.slice();
            for (const p of inWin) { if (p.blind) continue; if (tailVis === 1 || rnd() < tailVis) rem[p.tier] -= 1; }
            if (rem.some((x) => x < 0)) bad += hs.length;
            else {
              const s = rem.reduce((a, b) => a + b, 0);
              if (hs.length === 1) { if (s === 1) exact += 1; else amb2 += 1; }
              else { const nz = rem.filter((x) => x > 0).length; if (nz === 1 && s === hs.length) group += hs.length; else amb2 += hs.length; }
            }
          }
          k = kk;
        }
      }
      const tot = headPts.length * runs;
      winTable[winMs + 'ms/tailVis' + tailVis] = {
        determinedShare: +(((exact + group) / tot) * 100).toFixed(2),
        singleHeadShare: +((exact / tot) * 100).toFixed(2),
        sameTierGroupShare: +((group / tot) * 100).toFixed(2),
        ambiguousShare: +((amb2 / tot) * 100).toFixed(2),
        inconsistentShare: +((bad / tot) * 100).toFixed(2),
      };
    }
  }

  // (4) 现状: 内存直读覆盖率 (只有 headSrc === 'event' 才是直读真值)
  const headSrc = {};
  let heads = 0, directHeads = 0;
  for (const n of session.notes || []) {
    if (n.type !== 'hold') continue;
    heads += 1;
    const s = (n.game || {}).headSrc || '(none)';
    headSrc[s] = (headSrc[s] || 0) + 1;
    if (s === 'event') directHeads += 1;
  }

  const out = {
    tag, mode: 'attrib', generatedAt: new Date().toISOString(),
    inputs: { session: sessionFile, v2points: v2file },
    meta: {
      beatmap: [bm.artist, bm.title, bm.version].filter(Boolean).join(' - '), od: bm.od, keys: bm.keys, scoreV2, windows: W,
      counts_truth: truth, pointCounts: ptCounts, pointCountsMatchTruth: ptCountsOk,
      counts_engine: v2.counts ? [v2.counts.perfect, v2.counts.great, v2.counts.good, v2.counts.ok, v2.counts.meh, v2.counts.miss] : null,
    },
    points: { total: N, heads, nonHeads: N - heads, uniqueTimePoints: uniqueT, uniqueTimeShare: +(uniqueT / N * 100).toFixed(2), groupSizeHist, colDistinctGroups, colAmbGroups },
    truthCoverageNow: { heads, directHeads, directHeadShare: +(directHeads / heads * 100).toFixed(2), headSrc },
    attribution: { note: 'tau = 判定时刻 - 误差 = 点的谱面时刻; ±tol 内候选数', tolTable },
    headByCountSubtraction: { scope: '面条头 (LN head)', headEvents: headPts.length, byTierHead, windows: winTable },
  };
  const outFile = path.join(OUT_DIR, 'counter_attrib_' + (tag || slug(sessionFile)) + '.json');
  fs.writeFileSync(outFile, JSON.stringify(out, null, 1));

  console.log('== attrib (方案A 可归属性 / 覆盖率上限) ==');
  console.log('判定点   : ' + N + ' (头 ' + headPts.length + ' / 非头 ' + (N - headPts.length) + ')  档位汇总 ' + JSON.stringify(ptCounts) + ' ' + (ptCountsOk ? '✔ == summary.counts' : '✘ != summary.counts'));
  console.log('时间唯一点: ' + uniqueT + '/' + N + ' = ' + out.points.uniqueTimeShare + '%   同刻分组 ' + JSON.stringify(groupSizeHist));
  console.log('同刻组    : 按列可区分 ' + colDistinctGroups + ' 组 / 列也撞 ' + colAmbGroups + ' 组');
  console.log('现状头真值: 直读 ' + directHeads + '/' + heads + ' = ' + out.truthCoverageNow.directHeadShare + '%   headSrc=' + JSON.stringify(headSrc));
  for (const tol of [0, 2, 5, 10]) {
    const q = tolTable[tol];
    console.log('  归属性 ±' + String(tol).padStart(2) + 'ms: 唯一 ' + String(q.uniqueShare).padStart(6) + '%  歧义 ' + fmt(q.ambiguous / N * 100) + '%  无候选 ' + fmt(q.none / N * 100) + '%');
  }
  console.log('计数减法救回面条头 (LN head, 共 ' + headPts.length + ') 档位 (窗长/非头可见率 -> 确定率):');
  for (const k of Object.keys(winTable)) {
    const w = winTable[k];
    console.log('  ' + k.padEnd(18) + ' -> 确定 ' + String(w.determinedShare).padStart(6) + '%  (单头窗 ' + w.singleHeadShare + '%, 同档组 ' + w.sameTierGroupShare + '%, 歧义 ' + w.ambiguousShare + '%)');
  }
  console.log('产物    : ' + outFile);
  return out;
}

// ------------------------------------------------------------------ main

const t = o.tag;
if (o.mode === 'synth') {
  const f = o.files[0];
  if (!f) { console.error('用法: node scripts/counter_reconstruct.mjs --synth <session.json> [--hz 30] [--tag x]'); process.exit(2); }
  synth(f, o.hz, t);
} else if (o.mode === 'attrib') {
  const [s, v] = o.files;
  if (!s || !v) { console.error('用法: node scripts/counter_reconstruct.mjs --attrib <session.json> <v2points.json> [--tag x]'); process.exit(2); }
  attrib(s, v, t);
} else {
  const [c, s, v2pos] = o.files;
  const v2 = o.v2 || v2pos || null;
  if (!c || !s) {
    console.error('用法:');
    console.error('  node scripts/counter_reconstruct.mjs <counterlog.jsonl> <session.json> [<v2points.json>] [--tag x]');
    console.error('  node scripts/counter_reconstruct.mjs --synth  <session.json> [--hz 30]');
    console.error('  node scripts/counter_reconstruct.mjs --attrib <session.json> <v2points.json>');
    process.exit(2);
  }
  const r = reconstruct(c, s, t, v2);
  if (!r.ok) process.exit(1);
}
