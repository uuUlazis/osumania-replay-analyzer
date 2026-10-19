# osu!mania 回放判定与 acc 计算 · 权威文档 (V1 + V2)

> 版本: 2026-10-17 机制定稿态 · 适用: backend/src/mania/hit_matcher.cpp 与观测仪器 build/stable-re/bindsim_v2.mjs
> **范围声明: 本文档只载计算机制, 不含任何锚点成绩/残差数值/局次信息。** 验证数据与锚点清册一律见 §8 所列数据源。
> 探索史/证伪过程: docs/v2-judgement-semantics.md (深夜 III–XXXVII) 与 build/stable-re/v2anchor/VERDICT.md。

## 0. 总则

1. **参照口径**: 用户参照 = stable 显示 acc。V1 显示 = counts 公式; V2 (ScoreV2) 显示 = accuracyV2。
2. **时间域**: .osr 回放帧时基 = 真实时间; 判定比较在谱面时基进行, **判定窗 × 速率**(DT/NC ×1.5, HT ×0.75)。NC 速率 = 1.5。
3. **残差地板 (定性)**: stable 内部亚帧结算次序与 hold-break 内部标志在回放信息集外, 绑定/尾归属存在不可复现残差 (逐物件 0.09–1.3% 量级)。规则侧已无可修项; 恢复条件 = osu-stable-reference 正文或 0xA4 全图探针。

## 1. 判定窗

| 档 | 公式 |
|---|---|
| perfect (MAX/彩) | 16 |
| great (300) | 64 − 3·OD |
| good (200) | 97 − 3·OD |
| ok (100) | 127 − 3·OD |
| meh (50) | 151 − 3·OD |
| miss | 188 − 3·OD |

- **⚠ 会话内存窗优先**: 端侧存在逐局校准, 实测窗组可偏离公式且随局而变 (perfect 16/17/18 均有出现)。读取器会话的 score.windows / tailWindows = 该局真值, **原样采纳, 勿由 OD 反推**。
- **V2 尾窗 = 基窗 × 1.5** (lazer RELEASE_WINDOW_LENIENCE 同源; 会话 tailWindows 实证恰为基窗×1.5)。
- 判定窗 × 速率 (§0.2)。

## 2. 计数序与 acc 公式

- **ScoreV2 局判据 (经验)**: 六计数和 == notes 数 + LN 数 (pairwise) → V2; 或显示 acc == accuracyV2 公式。会话 scoreV2 flag 不可靠; 段位课程常被强制 ScoreV2; mods 位不可靠。
- **accuracyV2** = (geki×305 + n300×300 + katu×200 + n100×100 + n50×50) / (N×305) × 100
- **accuracyV1 (counts 公式)** = ((n300+geki)×300 + katu×200 + n100×100 + n50×50) / (N×300) × 100
- N = 六计数和。⚠ 计数**顺序不一**: 会话 summary.counts 序 = [MAX/geki, 300, katu/200, 100, 50, miss]; .osr 六计数 i16 序 = [300, 100, 50, 彩, 200, miss]。用前必核。

## 3. 绑定 (按键 → 物件归属), V1/V2 共用

- 每帧次序: ① sweep 过期头 → ② press 扫描 → ③ release 结算。
- **滑窗**: 可判物件 = start ∈ [G − W_BACK, G + 300]; W_BACK = Max(滚动项(HitPosition 派生, 皮肤相关), maxHold=全图最长 hold 时长) + 400。IL: f3112 = Max(项, f11848) + 200, 切片器再减 200。
- **press 窗**: rice [start − miss窗, start + ok窗); LN [start − miss窗, end + ok窗×1.5)。
- **Loop A 槽仲裁** (同列多物件, replay 必经): 逆序扫窗; deadline = Min(cand.start + ARB, slot.start − 1) + Max(0, slot.start − cand.start − 2·ARB), ARB = meh窗; G ≤ deadline 夺槽; 已判物件终结该列。
- **press 绑定条件**: 在窗 ∧ 未判 ∧ 按键正沿 ∧ 该列本帧未处理 ∧ (replay) Loop A 仲裁胜者。
- **LN**: 头绑定开启 hold; 尾 = 独立物件按自身窗判定 (stable 无 key-up 直接判定语义)。

## 4. V1 判定 (classic, 单元制: 头+尾合一)

- rice: judge(|press − start|, 窗)。头可判界 = start ± meh窗。
- LN 单元:
  1. 头未绑 (swept) → Miss。
  2. 早释: release < end − meh窗 → Miss (深断可入重握等待)。
  3. startDiff = |press − start|; press < start − meh窗 (远早按) → startDiff = duration − 1 (合并视作从头握住)。
  4. endDiff = min(|release − end|, meh窗) (heldthru 钳制)。
  5. **合计档位表**: 依次判 MAX ×1.2 / great ×1.1 / good ×1.0 / ok ×1.0 — 条件 startDiff ≤ W档×r ∧ startDiff + endDiff ≤ 2×W档×r; 全不满足 → 50。
  6. 持有过尾 → 升格 (内部值 300→彩, 150 类→200)。
- V1 判定窗 = 基窗 (不吃 ×1.5 尾宽限)。

## 5. V2 判定 (ScoreV2, 头尾分离制)

- **头**: 独立判定 |press − start| vs 窗; 扫出界 = miss窗。press-miss 消费默认关。
- **尾** (release 相对 end):
  - 局末仍持有 (flush) → Perfect (断过 → Meh)。
  - 迟释 err > ok窗 × 1.5 → Miss。
  - 早释超 meh窗 → **断尾留守**: 重按续 hold, 再判结果封顶 Meh。
  - 其余释放 → judge(|err| / 1.5, 基窗)。
- ⚠ **释放尾 MAX 上界 = 会话 perfect窗 × 1.5** (逐局派生; 固定值均已证伪)。

## 6. 引擎实现映射 (backend/src/mania/hit_matcher.cpp)

- V1 合计表 L500-559; V2 尾支 L416-469; 断尾 DEFER L445-454; resume L720-726/1075-1126; sweep/lateBound L604-631; 旗标 L155-246。
- **env 旗标 (默认)**: MRA_V1_NOREQUEUE=ON / MRA_V1_NOCAP=OFF / MRA_V1_SWIFT_SETTLE=OFF / MRA_V1_HELDTHRU_EXT=ON / MRA_V1_NEARREL=OFF / MRA_V1_NO_REGRIP=OFF(即 ON) / MRA_V1_BREAK_BOUND=185 / MRA_V1_GREYCAP=ON / MRA_V1_NO_REGRIPCAP=ON / MRA_V1_EAT_DEAD=OFF / MRA_V2_TAIL_NOREJUDGE=OFF / MRA_V2_TAIL_LATEBOUND_MS=0(→ok×1.5) / MRA_V2_LATEBOUND=miss窗 / MRA_V2_PMISS=OFF / MRA_V2_RESUME_LATE=ON / MRA_V2_TAIL_MAX_RAW=−1(关) / MRA_V2_TAIL_CAP_GREAT=OFF / MRA_V2_BOUND_GOOD=OFF。
- harness: build/v1_harness.mjs (withServer/measureMap)。exe --headless → POST /api/scores/load-replay {playedTicks, ticks} → sid = lr.data.sessionId → 轮询 GET /api/analysis/{sid}/summary → GET /api/analysis/{sid}/hits?version=v1|v2、/api/beatmaps/{sid}/objects。

## 7. 验证方法 (数据与成绩不入本文)

- 锚点级: harness 对锚点局跑引擎, accuracy.scoreV1/scoreV2 对照锚点显示 acc; 数据源 = anchor_scores_inventory.json。
- 逐物件级: bindsim_v2 --truth 对照会话 game 层; pairwise 真值 = LN头(game.headJudge) + LN尾(game.judge) + rice(game.judge)。
- 批量级: bindsim_batch2.mjs (35 tier-A, counts 六向量)。
- ⚠ 三条仪器纪律: reader head/tail.eventTime 不能用于规则级验证; 会话窗原样采纳; ScoreV2 用经验判据。

## 8. 仪器与数据源

- 仪器: build/stable-re/bindsim_v2.mjs (模拟器+真值对照; --s2/--lnmodel/--tmode xrate/--v2maxraw off) · bindsim_batch2.mjs (批量) · notelock_v2.mjs (--osr LZMA 解码) · v2anchor/osr2frames.mjs (.osr→frames CSV) · strictil.mjs + meta.mjs (IL 严格解码) · build/v1_harness.mjs (引擎 API)。
- 数据: build/stable-re/bindtruth_table.json · bindkeys_index.json + bindkeys_*.json · external_semantics.json + external/*.cs (lazer 参照) · build/anchor_scores_inventory.json (锚点清册与成绩) · build/stepwise_*(引擎基线记录) · build/v2anchor/ (复验记录)。
- .osr 寻址: C:/osu!/Replays/ 为主, 部分在 C:/osu!/Data/r/<md5>-<ticks>.osr; 以 (beatmapMd5, playedTicks) 双核。

## 9. 证伪清单 (禁止重试)

EAT 消费类 · A2 最近松键结算 · LN 体中段接住 · 全局 requeue · 灰按立即全局封顶 · 判定窗查表假设 · 顺序层绑定信号 · 灰按帧域判别式 (四重否证) · watchcols 列锁信号 · ManiaAction 历史位序 (Special1=1/Key1=10) · sedge 时基 (边沿×速率) · 释放尾 MAX 固定界 (真值上界 = 会话 perfect×1.5, 逐局派生) · 尾无 100 档假设 · MRA_V2_TAIL_CAP_GREAT · MRA_V2_TAIL_NOREJUDGE · MRA_V2_PMISS · 引擎 GetCappedResult 方向补丁 (与补丁语义反向恶化) · 会话 scoreV2 flag 直接采信 · 屏显≠计数公式的"显示 quirk"假设。

## 10. 通用身份纪律

- 锚点/局次入结论必须 (beatmapMd5 + playedTicks + counts N) 三对齐; 缺任一 → 存疑隔离, 严禁硬配。
- 同名异图以 md5/beatmapId 判别, 图名与日期不构成身份。
- .osr 可能在 Replays/ 之外的 Data/r/ 下; 头部 md5+ticks 双核后才可采用。
