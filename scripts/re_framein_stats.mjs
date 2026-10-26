// re_framein_stats.mjs — A13 (ManiaColumn WasHeld / 每帧一次输入) 取证脚本
// 逐锚点统计回放帧流里「同时间戳连续记录」「亚帧短按」的分布。
// 用法: node scripts/re_framein_stats.mjs [tag]
// 输出: build/tmp_accupdate/re_framein_<tag>.json
import fs from 'fs';
import { withServer, measureMap } from '../build/v1_harness.mjs';

const tag = process.argv[2] || 'run';
const inv = JSON.parse(fs.readFileSync('build/anchor_scores_inventory.json', 'utf8'));
const A = (id) => inv.anchors.find(x => x.anchorId === id);
const ONLY = process.argv[3] ? process.argv[3].split(',') : null;
const IDS = ['xfpsb_5th_beyond', 'lndan13_yoru_underjoy_0313', 'twinbloom', 'negai_1001',
  'lndan12_yuugure_2304', 'trueblue_new',
  'bz_09x', 'azimuth_c8b6075b', 'lndan_10th_v1', 'artificial_human_scp_v1',
  'azimuth_lndan3_21a9c6b6', 'xueyueyedong_49b4bd1d', 'fubuki_vibro'];

function popcount(x) { let n = 0; while (x) { x &= x - 1; n++; } return n; }

const OUTP = 'build/tmp_accupdate/re_framein_' + tag + '.json';
let out = {};
try { out = JSON.parse(fs.readFileSync(OUTP, 'utf8')); } catch {}
await withServer(28971, async (base) => {
  for (const id of (ONLY || IDS)) {
    const ticks = String(A(id).identity.playedTicks);
    const r = await measureMap(base, ticks);
    if (r.error) { out[id] = { error: r.error }; console.log('ERR', id, r.error); continue; }
    const fr = await (await fetch(base + '/api/replays/' + r.sid + '/frames?limit=500000')).json();
    if (!fr.ok) { out[id] = { error: 'frames' }; continue; }
    const F = fr.data.frames;  // {timeMs, keys, isSeed}
    const st = {
      frames: F.length, pressEdges: 0, releaseEdges: 0,
      sameTsGroups: 0, sameTsFrames: 0,      // 组内出现「同列多次转移」的组数 / 同 ts 帧数
      groupsWithRepeatCol: 0,                // 涉及的同列重复转移总数
      sameTsDownUpSameCol: 0,                // 单条记录里同一列既按下又松开
      tapDur: {},                            // 按下->松开时长分布 (按列)
      taps: 0,
      tapsLe4: 0, tapsLe8: 0, tapsLe16: 0,
    };
    // 逐帧推边沿, 同时把「同一时间戳的连续记录」归组: 组内每列的状态转移次数
    // > 1 表示该列在组内先按后松(或先松后按) —— 真实采样器一帧只看到组末状态,
    // 这些边沿对判定不可见(collapse 会丢掉它们)。
    const groupTrans = new Map();  // col -> 组内转移次数
    const groupCols = new Set();
    let prevKeys = null, prevT = null, groupEdges = 0, groupInvisible = 0;
    const downAt = new Map();  // col -> ts of press
    for (let i = 0; i < F.length; ++i) {
      const f = F[i];
      if (f.isSeed) continue;
      const base = prevKeys == null ? 0 : prevKeys;
      const dn = f.keys & ~base, up = ~f.keys & base;
      const newGroup = prevT !== null && f.timeMs !== prevT;
      if (newGroup || prevT === null) {
        // 结算上一组
        if (prevT !== null) {
          let g = 0;
          for (const [, n] of groupTrans) if (n > 1) g++;
          if (g) { st.sameTsGroups++; st.groupsWithRepeatCol += g; }
        }
        groupTrans.clear();
        groupEdges = 0; groupInvisible = 0;
      }
      for (let c = 0; c < 16; ++c) {
        const d = ((dn >> c) & 1) + ((up >> c) & 1);
        if (!d) continue;
        if (d > 1) st.sameTsDownUpSameCol++;
        groupTrans.set(c, (groupTrans.get(c) || 0) + 1);
        if ((dn >> c) & 1) { st.pressEdges++; downAt.set(c, f.timeMs); }
        if ((up >> c) & 1) {
          st.releaseEdges++;
          const t0 = downAt.get(c);
          if (t0 != null) {
            const dd = f.timeMs - t0;
            st.taps++;
            st.tapDur[dd] = (st.tapDur[dd] || 0) + 1;
            if (dd <= 4) st.tapsLe4++;
            if (dd <= 8) st.tapsLe8++;
            if (dd <= 16) st.tapsLe16++;
            downAt.delete(c);
          }
        }
      }
      if (prevT !== null && f.timeMs === prevT) st.sameTsFrames++;
      prevT = f.timeMs;
      prevKeys = f.keys;
    }
    if (prevT !== null) {
      let g = 0;
      for (const [, n] of groupTrans) if (n > 1) g++;
      if (g) { st.sameTsGroups++; st.groupsWithRepeatCol += g; }
    }
    // 压缩 tapDur 到前 12 个最短
    st.tapDur = Object.fromEntries(Object.entries(st.tapDur).map(([k, v]) => [k, v])
      .sort((a, b) => +a[0] - +b[0]).slice(0, 12));
    out[id] = st;
    console.log(id.padEnd(30), 'frames', String(st.frames).padStart(6),
      'sameTsGroups', String(st.sameTsGroups).padStart(4),
      'repeatCol', String(st.groupsWithRepeatCol).padStart(4),
      'netZero', String(st.sameTsDownUpSameCol).padStart(3),
      'taps<=4/8/16', st.tapsLe4 + '/' + st.tapsLe8 + '/' + st.tapsLe16);
  }
  fs.writeFileSync(OUTP, JSON.stringify(out, null, 1));
});
console.log('wrote', OUTP);
