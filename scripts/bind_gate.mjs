// bind_gate.mjs — 通用 0xA4 归属闸门: 探针绑定真值 vs 引擎首个头事件
// 用法: node scripts/bind_gate.mjs <anchorId> <probe.jsonl> [probe2.jsonl ...] [--tag name]
// 口径: 0xA4 写入值 = stable 记录的该物件绑定时刻 (press GameTime);
//      agree = 引擎首个头事件时刻命中该物件任一 A4 写入;
//      rebind = 命中**同列其它物件**的写入; x = 完全不命中。
import fs from 'fs';
import path from 'path';
import { withServer, measureMap } from '../build/v1_harness.mjs';

const args = process.argv.slice(2);
let tag = 'bindgate';
const ti = args.indexOf('--tag');
if (ti >= 0) { tag = args[ti + 1]; args.splice(ti, 2); }
const [ANCHOR, ...FILES] = args;
if (!ANCHOR || !FILES.length) { console.error('用法: node scripts/bind_gate.mjs <anchorId> <probe.jsonl...> [--tag name]'); process.exit(2); }

const inv = JSON.parse(fs.readFileSync('build/anchor_scores_inventory.json', 'utf8'));
const a = inv.anchors.find(x => x.anchorId === ANCHOR);
if (!a) { console.error('anchor not found'); process.exit(2); }
const ticks = String(a.identity.playedTicks);

// --- 解析探针: 按物件收集 0xA4 写入值 ---
const writes = new Map();   // index -> [to,...]
let lines = 0, a4 = 0;
const hdrs = [];
const covered = new Set();  // 探针 hdr 声明的覆盖索引
for (const f of FILES) {
  if (!fs.existsSync(f)) { console.log('skip missing', f); continue; }
  for (const raw of fs.readFileSync(f, 'utf8').split('\n')) {
    const s = raw.trim(); if (!s) continue;
    let j; try { j = JSON.parse(s); } catch {
      // hdr 行可能是 "range":"range":[a,b] 这种非严格 JSON, 用正则兜底
      if (s.indexOf('"hdr"') >= 0 && s.indexOf('watchobj') >= 0) {
        const m = /\[(\d+),(\d+)\]/.exec(s);
        if (m) { const st = Number(m[1]), cnt = Number(m[2]); for (let k = st; k < st + cnt; k++) covered.add(k); }
      }
      continue;
    }
    if (j.hdr !== undefined || j.ev === undefined) {
      hdrs.push(j);
      // hdr 形如 "range":"range":[1394,512] 或 indicesFile 清单
      const m = /\[(\d+),(\d+)\]/.exec(String(j.range || ''));
      if (j.cmd === 'watchobj' && m) {
        const st = Number(m[1]), cnt = Number(m[2]);
        for (let k = st; k < st + cnt; k++) covered.add(k);
      }
      continue;
    }
    lines++;
    const off = String(j.off || '');
    if (off !== '0xA4') continue;
    a4++;
    const i = j.i;
    if (!writes.has(i)) writes.set(i, []);
    writes.get(i).push(j.to);
  }
}
console.log('probe 文件', FILES.length, '| 事件行', lines, '| 0xA4 写入', a4, '| 有写入物件', writes.size, '| hdr 覆盖索引', covered.size);

await withServer(28980, async (base) => {
  const r = await measureMap(base, ticks);
  if (r.error) { console.log('ERR', r.error); return; }
  const objOf = new Map(r.objects.map(o => [o.index, o]));
  const headOf = new Map();
  for (const h of r.hits || []) {
    if (h.phase === 'hold_tail') continue;
    if (!headOf.has(h.objectIndex)) headOf.set(h.objectIndex, h);
  }
  // 同列映射 (用于 rebind 判定)
  const colObjs = new Map();
  for (const o of r.objects) {
    if (!colObjs.has(o.column)) colObjs.set(o.column, []);
    colObjs.get(o.column).push(o);
  }
  const watch = [...writes.keys()].filter(i => objOf.has(i));
  // 覆盖但无 A4 写入的物件: stable 未绑定 —— 引擎若给了头事件就是分歧
  const coveredNoA4 = [...covered].filter(i => objOf.has(i) && !writes.has(i));
  let agree = 0, rebind = 0, miss = 0, ghostBind = 0;
  const bad = [];
  for (const i of watch) {
    const o = objOf.get(i), h = headOf.get(i);
    const t = h && h.actualTimeMs != null ? h.actualTimeMs : null;
    const ws = writes.get(i);
    if (t != null && ws.includes(t)) { agree++; continue; }
    // rebind: t 命中同列其它物件的写入
    let rb = false;
    if (t != null) {
      for (const o2 of (colObjs.get(o.column) || [])) {
        if (o2.index === i) continue;
        const w2 = writes.get(o2.index);
        if (w2 && w2.includes(t)) { rb = true; break; }
      }
    }
    if (rb) rebind++; else miss++;
    bad.push({ i, col: o.column, type: o.type, start: o.startTimeMs, end: o.endTimeMs,
      engineHead: t, engineJudge: h ? h.judgement : 'miss',
      engineOff: t != null ? +(t - o.startTimeMs) : null, a4: ws, kind: rb ? 'rebind' : 'x' });
  }
  const rate = watch.length ? +(agree / watch.length * 100).toFixed(2) : 0;
  // 覆盖但无 A4: 引擎仍绑定的物件 (stable 未绑定)
  for (const i of coveredNoA4) {
    const h = headOf.get(i);
    if (!h || h.actualTimeMs == null) continue;
    const o = objOf.get(i);
    ghostBind++;
    bad.push({ i, col: o.column, type: o.type, start: o.startTimeMs, end: o.endTimeMs,
      engineHead: h.actualTimeMs, engineJudge: h.judgement,
      engineOff: +(h.actualTimeMs - o.startTimeMs), a4: [], kind: 'engine-only' });
  }
  console.log(`TOTAL watched=${watch.length} agree=${agree} rebind=${rebind} x=${miss} rate=${rate}%`);
  console.log(`      覆盖无A4物件=${coveredNoA4.length} 其中引擎仍绑定=${ghostBind} (engine-only)`);
  const byCol = {};
  for (const b of bad) byCol[b.col] = (byCol[b.col] || 0) + 1;
  console.log('  x/rebind 分布 (列) ', JSON.stringify(byCol));
  for (const b of bad.slice(0, 25))
    console.log(`   ${b.kind.padEnd(6)} i=${b.i} col=${b.col} ${b.type} start=${b.start} engineHead=${b.engineHead} (${b.engineOff}) a4=[${b.a4.join(',')}]`);
  if (bad.length > 25) console.log(`   ... 共 ${bad.length}`);
  fs.writeFileSync('build/tmp_accupdate/bind_gate_' + tag + '.json', JSON.stringify({
    anchor: ANCHOR, tag, files: FILES, at: new Date().toISOString(),
    watched: watch.length, agree, rebind, x: miss, rate,
    coveredNoA4: coveredNoA4.length, engineOnly: ghostBind,
    mismatches: bad,
  }, null, 1));
  console.log('wrote build/tmp_accupdate/bind_gate_' + tag + '.json');
});
