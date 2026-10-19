# stable mania V2 面条/按键判定语义 (从 test1006/62/63 控制谱 + 文档反推, 已三谱验证)

## ⭐ 稳定侧权威对照案例(用户实测,回归基准以本表为准)

> 以下为用户在 osu!stable 内实测的准确率(成绩界面标注 + 回放播放结束值),
> 是 stable 侧权威参考。**回归 declared 一律以本表为准;但每行必须先过谱面身份
> 核验 (beatmapMd5 + playedTicks + counts N) —— azimuth 案即同名异图混谱
> (深夜 X),核验前不得入基线。**

| 谱面 | 模式 | 成绩标注 | 回放结束 | 我们 ours | 真实 dACC |
|---|---|---|---|---|---|
| azimuth [LN] (testset = Extra Level (LN) [Azimuth Dan (Marathon)], md5 c8b6075b, 18406 notes) | v1 | 94.37 (counts 公式, 与 .osr/scores.db 自洽) | — | 94.67746 (fix2) / 94.56971 (A2) | **−0.307 / −0.199** | 回放实测 94.37/94.37 ✓ = counts;读取器真值已导出 | −0.31 (A2) |
| Blue Zenith 0.9x | v1 | 89.95 | 89.95 | 90.61210 (fix2) | **−0.66** |
| Negai | v2 | 96.22 | 96.22 | ≈96.24 | ≈−0.02 |
| 4K LNDan 12th | v2 | 98.15 | 98.17 | 98.35607 | **−0.21** |
| 7k LN Dan Phase 10th [10th Dan] | v1 | 97.13 | 97.13 | 97.40244 (ticks 638800566152052311) | **−0.272** (display = counts 97.1321 ✓ 精确) |
| Artificial Human [SCP] | v1 | 92.17 | 92.19 | 92.50794 (ticks 638764382782124936) | **−0.338** (display = counts 92.1720 ✓ 精确; 标注/回放差 0.02 = stable 内部微差) |
| xfpsb LN 5th dan [5th Dan - Beyond] | v2 | 96.66 | 96.66 | 96.74817 = accV2 (ticks 639182527588160235) | **−0.088** (saved counts 公式 97.2652, quirk −0.60 同向) |
| 4k ln13th-yoru [13th Dan - Yoru] (eZmmR 变体) | v2 | 97.21 | 97.21 | ⚠局次未定; 候选局 2026-03-13 (ticks 639090060691327285) ours accV2 97.46729 → +0.257 | **+0.257 ⚠候选局** |
| Twin Bloom | v2 | 94.57 | 94.58 | 94.56464 | **+0.005** ✓收敛 |
| True Blue (新局 20:40:51, 用户已确认) | v2 | 98.79 | 98.79 | 98.12535 (counts) | 显示 quirk: LN尾geki落盘demote为katu, V2(320/200)显形; 逐物件2106/2106精确 → 判定收敛, 差异纯显示层 |

注:
- azimuth 案 = **同名异图混谱反例 (深夜 X 破案)**: 用户 95.05/95.06 实测属于
  「7K Dan Course - **LN Dan Phase III** [Azimuth Dan]」(md5 21a9c6b6, 10980 notes,
  scores.db 该局 counts 公式 95.0471 ≈ 95.05), **非 testset 图** (Extra Level (LN)
  [Azimuth Dan (Marathon)], md5 c8b6075b, 18406 notes)。testset azimuth 的
  counts 公式 94.3705 与 .osr/scores.db 完全自洽, **declared = counts 公式**,
  无「counts vs 显示」分叉。dACC: fix2 −0.307 / A2 −0.199 (A2 改善 +0.108)。
  深夜 IX 的 declared 修正与 katu→geki quirk 理论已撤销 (原 95.05 行移出
- **显示 acc 规则确立 (2026-10-09)**: 用户参照一律为 stable 显示 acc。V1 显示 =
  counts 公式 (azimuth 94.37/94.37 ✓); V2 显示 = counts + LN尾geki还原(320) —
  True Blue 新局 display 98.79 vs counts 98.125 实锤 (判定 2106/2106 精确, 差异纯
  显示层)。analyzer 增加 displayAccuracy 输出 (V2: LN尾perfect计为geki)。
- azimuth 读取器逐物件真值已导出 (2026-10-09, output/latest.json, md5 c8b6075b ✓,
  18406 notes ✓)。
- 新增锚点待回填 ours: 7k LN Dan Phase 10th v1 97.13/97.13; Artificial Human [SCP]
  v1 92.17/92.19; xfpsb LN 5th dan v2 96.66/96.66; 4k ln13th-yoru v2 97.21/97.21。
  testset 基准, 仅作异图参考)。
- Blue Zenith 案 = 正例: counts 公式 89.95415 与 stable 显示 89.95 一致。
- **教训**: 一切用户锚点必须先过 beatmapMd5 + playedTicks + counts N 身份核验
  再进基线;「Azimuth Dan」同名难度跨 dan course 包是现成反例。
- 4K LNDan 案: 成绩与回放相差 0.02(stable 内部即有微小差), 对照时允许 ±0.02 容差。
- 7k LN Dan Phase 10th / Artificial Human [SCP] / xfpsb LN 5th dan / 4k ln13th-yoru:
  尚未入 testset, 待抓取 ticks 后纳入回归并回填 ours 列。


来源: https://page.27345861.xyz/docs/mania_number/ 专题:ScoreV2 (作者解包过 osu!stable)
+ test1006/test10062/test10063 三张控制谱的 stable 标注逐单位验证。

## 窗口 (od7 例, V2)
彩300(max) = 24.9−1.1×od (od≥5; od≤5 为 22.4−0.6×od), 线性插值, 半毫秒 floor;
其余判定窗与 V1 相同: [300=64−3od, 200=97−3od, 100=127−3od, 50=151−3od, miss=188−3od]。
ACC 权重: max=305, 300=300, 200=200, 100=100, 50=50, miss=0, 归一 305。

## 头 (LN 头与 rice 同规则)
- 按键绑定: 按键作用于列内下一个未判定物件; 若误差早于 −meh → 按键完全无效
  (物件留队, 稍后可被更近的按命中)。
- 头判定: |err| ≤ 各判定窗 (原始窗)。
- 头可判状态到 头+ok 为止, 超时未按 → 扫为 miss。

## 尾
- 尾可判区 = [尾−meh, 尾+1.5×ok]。
- 区内抬起 → 用抬起时刻判尾, 判定窗 = 1.5× (即 |err|/1.5 对照原始窗)。
- 早于 尾−meh 的抬起 = 体中间断开 (弹 miss 字, 不计入 miss 数), 面条保持可判;
  之后的按键 = 接回 (重按本身不产生判定); 接回后的最终抬起 → 尾 = meh 平判。
- 迟于 尾+1.5×ok 仍未结束 → 尾 miss。
- 全程未松 (按住过尾) → 尾 = 300g (MAX)。

## 绑定分界线 (V2 notelock, 密集段关键)
列内未判定物件按时间排序; 对其中 start ≤ 当前时刻的最后一个 note k:
gap = start(k+1) − start(k):
- gap < meh          → 分界 = start(k+1) − 1
- gap ∈ [meh, 2×meh] → 分界 = start(k) + meh
- gap > 2×meh        → 分界 = start(k+1) − meh
分界前的按 → 归 k; 分界后的按 → 归 k+1。多个已开始未判定的取最后一个。

## 已修
- analysis.cpp: holds 计数去重 (每 object 只取最后归属)。
- hit_matcher.cpp: 体断边界 miss×1.5 → meh; 接回无条件生效; 接回后尾 meh 平判;
  V2 绑定分界线 (边界 walk); MRA_TRACE 跟踪。
- 待修: 头绑定窗口生命周期 (pending 进入/移除时机导致 test10063 头仍判 200)。


## 实现状态 (2026-10-06 00:30)
- 已落地并验证: 三张控制谱 comp = 标注精确 (test1006/62/63 ✓✓✓)。
- 全量回归 (25 图, mod感知公式): V2 侧 test 谱完美, 但边界 walk 使 Negai +0.110
  (原+0.003)/OMOIDE +0.042/ARCADIA +0.051/TwinBloom −0.259(原−0.210) 恶化;
  Chocolate +0.001(改善)。V1 侧 ende/promise 仍 0, ice −0.011, azimuth +0.085,
  newspapers +0.036, penguin +0.177, bz +1.002, piano +1.036 (不变 ✓)。
  vibro: Believe 0, VibroLovers +4.342 (不变), Swan 未测出 (服务器中途 404)。
- 待查: ①边界 walk 的方向/数值与文档语义的细微差 (恶化图 vs 改善图的结构差异);
  ②Sunflower 之后服务器 404 (进程疑似崩溃, 需查);
  ③V1 vibro 差距未动 (V1 路径本轮未改 — V1 需要同等的 defer/最终归属语义移植)。
- 回归基线 (改前): V2: OMOIDE −0.020/ARCADIA −0.004/Negai +0.015/Chocolate +0.039/
  TwinBloom −0.210/TrueBlue −0.059/4KLNDan −0.039, 其余 0;
  V1: azimuth +0.085/bz +1.002/penguin +0.177/ice −0.011/newspapers +0.036/
  ende 0/promise 0/piano +1.036; vibro: Swan +4.198/VibroLovers +4.342/FUBUKI +4.171。


## 关键实证补充 (test10063 逐 press 分析)
- press 1052 (−148, 超出 meh 早窗) → stable 完全忽略 (不消费); LN0 头在 头+ok=1306 被扫为 miss ✓ (stable miss×1)。
- release 1090 (尾−147, 超出 尾−meh) → 体断; press 1131 被**体断 LN 的接回**消耗 (notelock 分界线 1274 归属 LN0); release 1169 (尾−68) → 判 200 → **体断封顶 meh** ✓ (stable meh×1 = LN0 尾)。
- press 1212 (−63) → LN1 头 200 ✓; press/释放 1244/1291/1319/1376 → 200×4 ✓; release 1408 (−54/1.5=36) → 300 ✓; press 1451/1488 → 200/300 ✓。
- **结论: LN 头的早判窗 = meh(130)** (press −148 忽略、−110/−69/−63 命中), 头未判超 头+ok 扫 miss。rice 早窗待定 (endemize V1 证据 = miss)。
- **实现教训**: 边界 walk 的"准入界"(miss, 用于归属判定) 与 "提交界"(meh, 用于实际出判定) 必须分离 — 准入但不提交的按 = 完全忽略。当前 build 两处都是 miss → test10063 回归失败 [0,0,0,1,2,4] (LN1 头被 −144 误判 miss 级联)。

## 下次会话执行清单
1. hit_matcher.cpp V2 press 路径重写为显式状态机 (规格见上):
   - 绑定: notelock 分界线归属 (分界线前的按归 k; k 已完成 → 丢弃)。
   - 头提交界: |err| ≤ meh 早侧 / err ≤ ok 晚侧 → judgeAbs(err, w2); 早侧 (meh, miss] → 完全忽略。
   - 尾: 可判区 [尾−meh, 尾+1.5×ok] → judgeAbs(err/1.5, w2), 体断/头未判封顶 meh; 区外早松 → defer; 区外晚 → miss; 全程未松 → 300g。
2. 回归门禁: 三测试谱精确 + 25 图不劣化 (基线见 build/regression_v9.json 与 docs 本文件)。
3. 然后 V1 移植 (bz/piano/vibro)。


## 实现死锁记录 (2026-10-06 00:45)
test10063 LN0 的绑定存在表面矛盾:
- press 1052 (−148) → stable LN0 头 = MISS (判了)。
- press 1131 (−69 vs LN0 头) → stable LN0 头 ≠ 200 (是 miss/meh 系)。
- press 1212 (−63 vs LN1 头) → stable LN1 头 = 200。
任何固定早窗都同时解释不了 1052 命中 miss 而 1131 不命中。唯一自洽解释:
**notelock 分界线归属 (1131/1212 都 < 分界线1274 → 归 LN0) + LN0 头被 1052 判 miss
+ 1131 作为"接回"被消费 (不重判头)** = E6 状态 (接回在 walk 前 + walk早窗=miss +
最终校验=mehW(Hold)) — 该状态三谱全过 ✓✓✓。但同状态在 20 图回归中
Negai/OMOIDE/ARCADIA/TwinBloom 恶化 — 说明分界线数值/适用在真实密度下仍有出入。

## 当前构建状态
- 三谱: test1006 ✓ test10062 ✓ test10063 ✗ [0,0,0,1,2,4] (E7 空按接回 + 双 miss 界 = 级联)。
- 20 图: V1 零回归 ✓; V2: OMOIDE −0.109/Negai −0.156/Chocolate −0.089/TrueBlue −0.180
  (接回恢复后的全面恶化)。
- 结论: E7(接回移到空按路径)是错误方向; 应恢复 E6 状态, 然后对 Negai/TwinBloom
  做逐 press 对比调试 (MRA_TRACE 已具备), 或引入 trace 驱动的绑定审计工具。

## 恢复 E6 的确切步骤 (下次会话)
1. hit_matcher.cpp press 路径: 把 "t >= boundary: flows past" 分支改为
   resume-consume (即 E6 语义: brokenHold 存在时, t < boundary → 接回;
   t >= boundary → 也接回?? — 不, E6 = 无条件接回 (boundary 计算仅用于 trace)。
   确切 E6 代码: brokenHold ≥ 0 → 无条件 resume-consume (无 boundary 检查),
   walk 早窗 = mehW(Hold)/miss(rice), 最终校验 = mehW(Hold)/miss(rice)。
2. 重建 → 三谱回归 → 记录 Negai/TwinBloom/OMOIDE/ARCADIA 数值。
3. 若 Negai −0.061/TwinBloom −0.259 仍在: 用 MRA_TRACE 对 Negai 做逐 press
   归属审计, 找出分界线规则的正确形式。


## 会话收尾状态 (2026-10-06 13:30)
- ✅ walk future 分支 continue 修复已落地 (press 超出 LN 头早窗时继续走而非 break —
  press 42842 正确命中 LN267 而非被 LN262 挡掉)。
- ✅ 三测试谱 3/3 精确; V1 8 图零回归; V2: Chocolate +0.001/Sunflower 0/Pallet 0 ✓。
- ✅ Negai 16 个 swept 头 = 合法 miss (stable 同)。
- ⏳ Negai 已判 miss 超出 +11 (我们 40 vs stable 29): 数据已存
  build/negai_events.json + build/negai_trace_v2.txt (V2 pass, PRESS 正则:
  /PRESS t=(-?\d+) col=(\d+) oi(\d+) off (-?[\d.]+) j(\d+)/ — 注意 "off " 带空格无等号)。
- ⏳ 当前全量 (build/regression_v11.json): V1 sumAbs 6.689 (bz +1.002/piano +1.036 为主);
  V2: OMOIDE +0.042/ARCADIA +0.051/Negai +0.110/TwinBloom −0.259/其余 ≤0.08。
- 当前构建语义 = E4(无条件接回) + walk(早界 miss/future-continue, LN头) + 尾边界 meh +
  resolveDeferredTail flat + 计数去重 + MRA_TRACE。
- 下一会话: ①Negai +11 已判 miss 逐单位定位 (对比 stable 归属); ②TwinBloom/OMOIDE/
  ARCADIA/Negai 的绑定精调; ③V1 移植 (bz/piano/vibro 大头)。


# 2026-10-07: 头晚区 (ok→miss) 语义修正 + test10064 三回放全对

## test10064 收官 (od7.7? 实为 od7 控制谱, 头早窗 meh 复确认)
- 三回放 3/3 精确: 首打 300g+300 (按住过尾 4ms → MAX); 纯漏键双 miss; 末打 300+50
  (头 −31 → great, 尾 0 → meh)。头早窗界 ∈ [63,110] 与 meh(130) 一致。
- 结论: V2 头/尾/体断/接回/notelock/305-ACC 经 test1006/62/63/64 共 8 判定单位全精确。

## Twin Bloom 密集段审计 (MRA_TRACE 逐 press 归属, build/tb_*.json)
- 工具: MRA_TRACE=1 → stderr [TR] PRESS/RELEASE; /hits?version=v2&limit=200000 全量
  (默认截 5000); /beatmaps/{sid}/objects 全量物表; /summary countMode=lazer (头尾各计,
  与 stable 头总数一致)。trace 正则注意行尾 \r ($ 不匹配, 先 strip)。
- 发现: 引擎把头可判区晚界设为 +ok, 头在 头+ok 被 sweep 杀死后, (ok, miss] 晚 press
  **fall-through 落到下一根 note 并判出高判定** — 29 例 (kOff 104–159), 权重 Σ7650,
  与 TwinBloom dACC −0.259 (=7525) 几乎吻合; 另 2 例 [−meh,+ok] 内被边界公式跳过。
- 修正①: lateBoundOf V2 ok→miss (sweep 界 = 头+miss)。头在 (ok, meh] 内的晚 press
  经边界公式绑定 k → judgeAbs → 50; (meh, miss] 的 press 仍按分界线 flow-past。
- 回归 (variant B, 默认构建): 控制谱 3/3 精确 ✓; TwinBloom −0.259→**−0.175**;
  Negai +0.110→+0.073; OMOIDE +0.042→+0.026; 4KLNDan −0.077→−0.051;
  Chocolate +0.034→**+0.001**; TrueBlue −0.134→−0.092; Σ|dACC| 0.462→0.407。全图不劣化。

## 旋钮 (hit_matcher.cpp)
- MRA_V2_LATEBOUND (默认 = miss 窗): 头 sweep/提交晚界。
- MRA_V2_PMISS (默认 0): (meh, miss] 晚 press 一律 press-miss 消费 k 并吃掉 press。
  **已证伪**: 无差别消费在密集列中级联 (下一按晚一拍 → 连环消费),
  TwinBloom +0.186 过冲, OMOIDE +0.065 恶化; 仅 4KLNDan −0.024 略好。保留代码供实验。

## 残余 TwinBloom −0.175 的定位
- variant B 下残余 ≈ 21 例 (meh, miss] fall-through 的 tgt 高判定 (Σ≈5800≈0.199%)。
- 两种候选 stable 语义待判别:
  ①flow (当前实现): press 落 k+1 判高判定, k 扫 miss (→ 残余需另找来源);
  ②eat: press 被 (已死/将死) k 吃掉, 不下传, k+1 等下一次按 (→ 当前 21 例多判)。
  test10063 曾实测 LN0 头在 头+ok 弹 miss 字 — 若 stable 内部同秒杀死头,
  则 (meh,miss] press 应被吃 → 倾向②, 但需要弹字实验定案。

## test10065 判别谱设计 (od7, 4K; 窗 meh130/miss167/ok106)
- A (dead-zone flow?): col0 k@1000, k+1@1200 (gap 200∈[meh,2meh]); 仅按 1140 (+140)。
  flow → k=miss + k+1=200(−60); eat/consume → 双 miss。
- B (ok–meh 带): col1 k@3000, k+1@3200; 仅按 3110 (+110)。
  绑 k → 50 + miss; 仍 flow → miss + 100(−90)。
- C (带内 sanity): col2 k@5000, k+1@5200; 仅按 5100 (+100≤ok) → 100 + miss (两假设一致)。
- D (gap>2meh): col3 k@7000, k+1@7300; 仅按 7160 (+160; 分界 7170 前绑 k) → 双 miss (sanity)。
- E (hold 变体): col0 hold 9000→9500, k+1@9700; 按 9160 (+160), 松 9300。
  头 press-miss 后尾的处理: 双 miss vs 头miss+尾50。
- 用例间 ≥2500ms; 请 stable 打一次并读弹字, 回放给引擎跑 /hits 对比。
- 当前最优基线已存 build/regression_current.json (旧); 新默认构建数值见上「回归」。

## 2026-10-07 下午: test10071/72 实测 → LN 尾 press 时提前判定修正 ✅
- 用户手打 test10071(rice stream gap75 od10)×2 + test10072(LN stream gap75 od7, 体37ms)×1。
- test10071 两份引擎逐单位精确 ✓ (含 (ok,meh] 带 press +106 → flow 判 300, stable 同 —
  (ok,meh] 带 eat 被否定; 死区 (meh,miss] 手打无法覆盖, 留待后续)。
- **test10072 抓到真 bug**: press 落在尾之后时, 引擎在 press 时刻以 release:=press 判尾
  (按住过尾 → MAX), stable 实际等**真实 key-up** 用 (release−尾)/1.5 判。
  逐单位验证 (press/release 639/675, 742/792, 838/892, 938/996):
  尾应得 300/200/300/300, 旧引擎给 MAX/300/MAX → stable [geki1,300×5,200×2,miss×2],
  修正后 [1,5,2,0,0,2] 精确 ✓✓✓。
- 修复: press handler 删除 "if (t >= endTime) resolveTail(t, t)" 提前判尾分支,
  hold 保持 activeHold 到真实 key-up (release 路径 judgeAbs(err/1.5);
  全程未松 → 结尾扫 held-through MAX)。
- 修正后回归: **test10071×2 + test10072 全精确** ✓; 控制谱 3/3 ✓;
  **TwinBloom −0.259 → +0.008 (达标 ≤0.02)**; 4KLNDan −0.077 → −0.016 (达标);
  TrueBlue −0.092 → −0.063; Σ|dACC| 0.407 → 0.344。
- ⚠️ Negai +0.073 → **+0.149** (比原始 +0.110 还差): 尾改真实 key-up 后 Negai 变过严,
  下一步用同套工具 (trace + frames + declared counts) 做 Negai 逐 press 审计。
- 已达标 V2 图: TwinBloom +0.008 / 4KLNDan −0.016 / Chocolate +0.001 / Sunflower 0 /
  Pallet 0 / Airengokuka −0.006 / OMOIDE +0.026 (≤0.02 边缘外 +0.006)。
  未达标: Negai +0.149 / ARCADIA +0.051 / TrueBlue −0.063 / Realize +0.024。

## 2026-10-07 晚: Negai 审计 → 体断接回 press 抢占语义 ✅ (Σ|dACC| 0.344 → ~0.16)
- stable 回放精确 96.22 (declared 反推 96.2216 ✓)。engine 96.073。
- 计数差结构: engine [geki+31→+38, 300−40→−36, miss+10→−5]。
- **发现① (resume 抢占)**: 12 个「头 ±1~12ms 处有按却 miss」—— press 前刚抬键 →
  brokenHold → 顶层接回无条件消费吃掉了 press。stable 用这按打中了新头。
- **RESUME_LATE 语义 (已设为默认, MRA_V2_RESUME_LATE=0 可关)**:
  press 先走 walk;打中真命中(≥50) → steal(消费新 note, brokenHold 尾平判 meh 结算);
  会提交成 MISS 且目标在 future → 回落接回 (test10063 1131: LN1 存活);
  会提交成 MISS 且目标在晚侧死区 → press-miss 打死该 note + 平判结算 (Negai 锚定)。
- 三态夹逼: resume-first +0.149 / 无条件 steal +0.010 (控制谱崩) / 条件 steal −0.054。
- **发现② 否证**: 尾 MAX raw 窗界假说 (MRA_V2_TAIL_MAX_RAW 23/24/25/26 扫描) —
  只对 Negai 单调改善、其他图全恶化, 38 个 geki 差值另有来源, 旋钮保留默认关。
- **V1 回归警告**: 删提前判尾时动了共享代码, V1 azimuth/penguin/bz/Swan 全漂 —
  已修复: 提前判尾分支保留给 V1 (reference behaviour), 仅 V2 等真实 key-up。
- 当前全表 (控制谱 3/3, V1 全回基线): TwinBloom +0.008 ✓ / 4KLNDan +0.021 ✓(压线) /
  ARCADIA −0.004 ✓ / OMOIDE −0.017 ✓ / Chocolate +0.001 ✓ / TrueBlue −0.021 ✓ /
  Sunflower/Pallet/AreYouReady 0 ✓ / Realize +0.024 / **Negai −0.054 (残余)**。
- Negai 残余 −0.054 = geki+38/300−36 (fallback 2 例 RFBK 无关, 尾 MAX 窗界无关),
  需下一轮: 对 38 个 geki 差值逐个定位 (head 侧边际? resume 交互? steal 值域?)。

## 2026-10-07 深夜: 体时间(body-time)接回规则 + 尾 MAX raw 界否证 (Σ 0.083)
- 用户签字: 4KLNDan +0.021 = 回放自身偏移, 销案。
- **尾 MAX raw 界否证**: MRA_V2_TAIL_MAX_RAW=23(晚侧) 使 Negai geki 精确 (3360 ✓,
  raw 24-26 = 38 个全中) 但打破 Sunflower/Pallet 逐位精确 (+0.009) — 它们在
  scaled 规则下本已全对 → 纯窗界规则不成立, 旋钮保留默认关。
- **体时间(body-time)接回规则 (已设为默认, 取代 err 方向版)**:
  brokenHold 存在时, press vs 断 LN 的尾:
  * press < 尾 (体还活着) → 接回消费 (test10063 1131 ✓)
  * press ≥ 尾 (LN 已结束, 接回不可能) → press 自由: 真命中 → 抢占+断尾平判;
    死区 → press-miss 打死 (RFREE trace)
  Negai STEAL 明细: 5 例中 4 例 press 在断尾之后 (+187/+177/+167/+68), 1 例 −55。
- 结果: Negai −0.054→−0.031, 4KLNDan +0.021→−0.016, Σ|dACC| 0.156→**0.083**;
  控制谱 3/3 ✓; test10071×2 + test10072 仍逐位精确 ✓; V1 全回基线 ✓。
- Negai 现存 [geki+38, 300−37, meh+1, miss−2] — 与 resume 机制无关 (body-time 已落地),
  尾 raw 窗界被否证 → 下轮: 在 LN 密集图上定位 38 个 geki 差值的共同结构
  (候选: stable 尾 MAX 的其他前置条件 / head-tail 联动 / 计帧差)。

## 2026-10-07 深夜 II: V2 max 窗 = 整数 floor ✅ (Negai 弹字定案)
- 用户读弹字 (Negai 回放): raw +25 尾 = **MAX** ×3 (40202/127765×2),
  raw +26 尾 = **300** ×3 (141140×2/214137) → 尾 MAX scaled 界 ∈ [16.67, 17.33)
  → **整数 floor 17**, 不是半毫秒 17.5。
- 修复: w2.perfect = floor(p2) (整数) — od6.5: 17.75→17。head 侧整数帧下无感,
  尾侧 |raw| > 25.5 的 geki → 300 (Negai 12 晚 + 24 早 = 36 个转换)。
- Negai → **−0.021** ✓ (残余: geki+2/300−1/meh+1/miss−2, 5 个物件级边缘)。
  Chocolate +0.001→+0.011 (int floor 影响, 仍 ≤0.02 ✓)。
- TrueBlue −0.063 揭穿: od6 的 int floor = 半毫秒 (18=18) → 差异与窗界无关;
  实际只有 ~5 个物件 (我们 [geki×3, 50×1] vs stable [miss×3, 200×1]),
  knob-23 时的 −0.021 是 87 个尾降级的错误掩盖。同 Negai 一类物件级边缘。
- **当前 V2 全表**: TwinBloom +0.008 / Negai −0.021 / 4KLNDan −0.016 / TrueBlue −0.063 /
  OMOIDE −0.017 / ARCADIA −0.004 / Chocolate +0.011 / Sunflower 0 / Pallet 0 /
  Airengokuka −0.006 / AreYouReady 0 / Realize +0.024; 控制谱+test1007 全精确; V1 基线。
- 下轮: TrueBlue 5 物件 + Negai ±2 残差 = 同族物件级边缘 (resume-steal/死区交界),
  用 STEAL/RFREE trace 逐个钉; Realize +0.024 独立查。

## 2026-10-07 深夜 III: TrueBlue 4 对晚按 + good 窗分界否证
- TrueBlue (od6, 7K, 2106 obj, "Repentance (MWC Edit)"): 差异 = 我们 [geki×3, 50×1]
  vs stable [miss×3, 200×1]。RFREE=0, DEFER=6 → 与接回/体时间规则无关。
- col1 55542 谜底: hold 923 的头在 55378 被 −160 press-miss 打死 (j5), 55494 松键 DEFER,
  55542 press = 体时间接回 (正确消费, 非异常)。
- **4 对晚按候选** (press 落 A +64/+86/+111/+120, gap 全 183 ∈ [meh133, 266] →
  现行分界 A+133 → 归 A; B 在 press 早侧 −63~−118 全在命中窗):
  ①col4 34227/34410 (press 34313): 我们 A=100, B=miss
  ②col6 39257/39440 (press 39368): 我们 A=50, B=miss
  ③col3 42093/42275 (press 42157): 我们 A=200, B=miss
  ④col6 110233/110416 (press 110353): 我们 A=50, B=miss
  若 stable 把其中 3 个按判给 B → stable miss +3 ✓ 完全对上。
- **good 窗分界否证**: MRA_V2_BOUND_GOOD=1 (gap∈[meh,2meh] 分界 = start+good79)
  → TrueBlue −0.134/Chocolate +0.085/Airengokuka +0.089 全恶化, 默认关。
  4 对的真实归属待用户弹字: 每对 press 打中的是前根还是后根?
- 另: geki+3 差值与 4 对无关 (对内无 geki), 仍待定位。

## 2026-10-08: 逐物件内存真值接入 (osu-mania-live-reader) ✅ 里程碑
- 用户自研读取器 (workspace/osu-mania-live-reader): 直读 osu!stable 内存逐 note
  最终判定 + 10ms 头判定重建, 计数与 tosu 完全对账。Negai/TrueBlue 两图接入
  逐物件真值对比 (联接键 = column:startTime:endTime; reader 类型标记是 rice/hold)。
- **TrueBlue 新局** (9-20 20:41 回放, 读取器看牌 15:49): **2106/2106 逐位精确** ★★
  (整数 max 窗、尾等真实抬键、体时间接回、头尾分离计数全部二次验证)。
  同图旧局 (20:37, testset) 仍 −0.063 = 5 物件 — 该局专属按键时机所致。
- **Negai**: rice 703 零差; LN 头差 17 + 尾差 5 (尾差全是头差连带)。
  ACC −0.021 ✓ (残余 geki+2/300−1/meh+1/miss−2)。

## 2026-10-08 深夜 II: 全库回放扫描 (100 份近期样本) + 404 根因
- scores.db 共 24687 成绩 / 16575 有回放文件; 分层抽样 (每 33 取 1 = 274) +
  近期 100 份两轮扫描 (每 20 份重启引擎防会话劣化)。
- 近期 100 份结果: 逐位 24 | 家族带 0.02~2 = 41 (53%!) | 结构异常 >2 = 2。
  另一分层样本 98 份分析: >0.02 = 50 (51%) — 问题广泛存在, 非孤例。
- **结构异常 2 份 = mod 支持缺口, 非判定语义**:
  My Man [Lv.62] +4.317 = **DT (mods=64, 1.5x) 回放未变速**;
  Propose [unfulfilled love] +54.846 = **ScoreV2+Random (mods=538968064, 随机列)**
  → 引擎缺 rate 缩放与 Random 列置换支持 (未来功能项)。
- 家族带构成: 无 mod 18 份 Σ5.43 (纯绑定两义) + ScoreV2 6 份 (含 Negai −0.021,
  读取器已验证其中大部分头差为重建假读) + 17 份 mods 未记录 (首轮扫描)。
- **死区 flow 语义大规模确认**: TrueBlue 新局含 82 次 press 落前根 (meh,miss]
  死区的按键, 我们 flow-to-next 且 2106 物件逐位精确 → eat 假说彻底死。
- **load-replay 404 根因** (非会话 flake): 那些成绩的 .osr 文件在 Data\r
  **不存在** (fail 退出未保存/被清理) — 数据现实。曾实现"同谱面最近 ticks
  兜底"但会静默加载同谱面其他局次的错误回放 (Flower Flag 同谱面有 16 份
  .osr, ±60s 必撞) → **已撤销** (正确性优先, 缺文件必须 404)。
- 结论: 家族带差异 (两义按归属) 是当前唯一可收敛的判定语义缺口,
  收敛路径 = test10074 隔离谱或更多读取器真值; DT/Random/变速支持另列功能。
- **读取器已知局限** (解读差异时必须知道): ①LN 头判定是 10ms 采样重建
  (自述 0.24% ≈ 6 根/图), 规则②③④在"同列 LN 链 + press 落前根死区"场景
  有系统性误重建倾向 — LN260 案例取证: 重建标记 eventTime 42388 处无原始按键
  (原始帧 col2 只有 42505/43014), headJudge=MISS 不可尽信;
  ②按住过尾 (尾=MAX) 的 LN 头必被重建为 MAX (规则④), 该类头读数不可作证据。
- 我们与 stable 的可确认残余: Negai [geki+2, meh+1, miss−2] (~2-5 判定,
  LN 密集两义按), TrueBlue 旧局 5 物件。均 ≤0.021, 达 ≤0.02 目标量级。
- 数据: build/negai_vs_stable_diffs.json / build/tbold_vs_stable_diffs.json /
  build/negai_deadzone_cases.json / negai5_* / tb_old2_* / tb_new_*。
- 读取器: C:\Users\19677\Desktop\workspace\osu-mania-live-reader\
  (output\<日期>_<时刻>_<md5前8>.json 每局一份; latest.json 同步;
  docs/stable-memory-recon.md = 内存逆向完整记录)。

## 2026-10-08 深夜 III: V1 死区 eat 假说否证 (Blue Zenith 逐物件真值)
- Blue Zenith [0.9x OD5] = testset bz (V1, mods=0, baseline +1.002)。
  读取器看牌 (declared 与 .osr 头一致, 无 desync) → V1 pass 逐物件对比:
  **427 差异, 我们偏高 214 / stable 偏高 213 / 零同级 — 完美对称的归属交换指纹**。
- **MRA_V1_EAT_DEAD 旋钮 (默认关) 两轮实验均否证**:
  ① blanket(所有 tooLate 死 note 吃 press): Blue Zenith 427→4257, V1 全图 +11~+62;
  ② 真死区限 (meh, miss]: 427→3566, V1 全图 +4~+46 —
  **V1 的死区 press 实际是 flow(数千例与 stable 一致), eat 在任何变体下都崩**。
- 结论: V1 与 stable 的差异不是死区 eat, 而是**同列多按/换指场景的绑定归属**
  (427 个对称交换 = press 归属互换), 参考移植的 findTarget/tier 公式与
  stable 实际行为在这些两义点存在系统差异。突破路径 = 读取器对更多 V1 回放
  出真值 + 对 427 清单做逐案归因 (数据已存 build/bz_v1_diffs.json)。
- 旋钮保留: MRA_V1_EAT_DEAD (默认关, 已证伪); 404 根因见上节 (.osr 缺失, 数据现实)。

## 2026-10-08 深夜 IV: V1 归属交换破案 — LN 头晚死界 + 断尾不可复挂 (fix1+fix2 已实现)
- **口径重建**: 旧 427 清单 = stable 端 reader notes[].game.judge (内存终判真值),
  我们端 rice=note_head.judgement / hold=最后一个 hold_tail.v1Judgement。
  修复重建脚本 type 字段 bug (objects 里 rice 记为 "note") 后 **427/427 逐条复现**。
  旧 bz_v1_events.json 本就是干净基线局 (breakdown 与重跑逐位一致)。
- **col6 jack 段现场还原** (#293..#317, 55-84ms 短 LN, 166ms 间距):
  stable 侧 #293 从未被按 (超时 MISS), press@21307 **流过** 给 #297 头 (−9→MAX)。
  我们侧 press@21307 被 #293 消费 (头 miss@+157), 级联全灭。
  两个锚点: LN 头 **+117 → meh 命中**, **+157 → 已死流走** ⇒
  **stable V1 LN 头晚窗 = 50 窗 (start+meh), 不是参考实现的 end+ok**。
- **fix1** (hit_matcher.cpp V1 分支): LN 头 tooLate = e > w.meh (原 t > end+ok);
  history-overlap 块在新规则下不可达, 移除。bz 427→183 差异。
- **col0 #479 现场还原**: press@27145 (−116) 头 meh 命中 → release@27206 (−138)
  尾断 MISS → press@27310 (+49) 在我们引擎里**复挂**续判尾 → final good;
  stable 显示 MISS ⇒ **stable V1 断尾 LN 首次松键即判死, 不可复挂**。
- **fix2**: ① MRA_V1_NOREQUEUE 默认翻转为 ON (=0 恢复旧 requeue);
  ② LN 头早灰 (−miss,−meh] press 不判不杀 (bz 1224 ignore : 0 kill,
  全部被后续 press 救活; rice 早灰保持 endemize 消费规则不变)。
- **净结果** (declared−ours 口径, |dACC| 越小越好):
  ende 0→0 / promise 0→0 / ice −0.011→−0.017 / newspapers +0.036→+0.060 /
  azimuth +0.085→**−0.307(恶化)** / penguin +0.177→−0.143 / piano +1.036→+1.071 /
  **bz +1.002→−0.658** / **FUBUKI +4.171→+2.946** / MachineGun −0.285→−0.285。
  Σ|dACC| 6.805→5.487 (**净 −1.318**); bz 逐物件差异 427→143。
  Swan/VibroLovers/Believe 未测完 (Swan ours=89.98756, Vibro ours=86.57315,
  Believe ours=93.26599, declared 待补)。V2 组不受影响 (改动全在 V1 分支)。
- **azimuth 恶化根因** = fix1 误杀长 LN 的体中段接住: 旧 end+ok 规则允许
  press 落在 LN body 内时绑定接住; fix1 一律流走。→ 引出 v3 模型 (未实现)。
- **v3 模型规格** (下一轮实现, 证据齐备):
  ① press ≥ LN end → 头已死, MISS + 流走 (bz col6 ✓);
  ② press 落在 LN body 内 (start+meh, end) → 头判 MISS 且**接住开始持有**
     (azimuth 长 LN catch; 参考 #344: 头 MISS 后仍可持有出尾 200);
  ③ press ∈ (−miss,−meh) 早灰 → 头判 MISS 记账, LN 保持 pending,
     press 流走, 后续 press ∈ [−meh,+meh] 仅开持不再判头 (#344);
  ④ 尾 = 松键 err 对**普通头窗** (无 1.5x 宽容) — #103: −72 → good(200) ✓;
     held-through (release ≥ end) 现有 Perfect 分支待复核 (#6900 家族
     end=+312 stable=ok 无法用任何 err 单调规则解释, 需读取器误差流)。
- **未解矛盾 (需读取器误差流知识)**:
  ① #333/#344 最小对: 头 err −5/+2, 尾松键 −78/−79, stable 分别判
     great/good — 任何 (startDiff,endDiff) 单调公式都无法同时满足;
     #333 的读取器尾重建 = +46@22670 (指向**第二次松键** r22665),
     怀疑 stable 尾判在「离尾端最近的松键」而非首次 — 与 #414 矛盾;
  ② 读取器重建 eventTime 与 .osr 帧钟差 ~26-32ms (#333 头 −36@22446 vs
     帧 P22478), 误差流 eventTime 语义需确认 (stable 内部钟 or 事件时刻);
  ③ −173 精确边界: #3685 (LN, −173, 杀) vs −171/−168 (不杀) — 帧量化
     ±5ms 内, 无法用帧时间分辨, 需 stable 真实 press 时刻;
  ④ 晚松 LN (#6900 end=+312→ok / #6933 end=+1311→good / #1233 end=+161→good)
     无 err 规则可解释, 疑似与头档/持有状态相关的独立结算路径。
- 残差构成 (bz 143): miss→hit 62 (含 −173 边界家族) / 一级差 57
  (meh→ok 20 + ok→good 21 + good→great 13, 尾公式带) / 其余散点。
- 数据: build/bz_v1_events_clean.json (干净基线事件) / bz_v1_events_fix1.json /
  bz_v1_events_fix2.json / bz_v1_diffs_fix2.json (143 残差) /
  bz_frames_clean.json (按键流) / regression_fix2_full.json (全表)。
- 代码状态: fix1+fix2 已构建进 release\ (MRA_V1_NOREQUEUE 默认 ON,
  MRA_V1_EAT_DEAD 仍默认关); 回滚点 = git HEAD (fix1+fix2 diff 见
  hit_matcher.cpp V1 分支三处注释块)。

## 2026-10-08 深夜 V: mania_number 文档对照 — LN 规则全面对齐, 三个悬案当场告破
- **参考文档**: 《osu!mania中的数字》 <https://page.27345861.xyz/docs/mania_number/>
  (作者解包 stable; 已并入 agent.md §27.0, 判定开发必读)。此前只用其 ScoreV2
  绑定边界章 (V2 已收敛); acc.html 的 LN 章本次首次对照 V1。
- **当场解开的悬案**:
  ① 晚松 LN 家族 (#6900 end=+312→ok / #6933 +1311→good / #1233 / #6928 / #6964 /
     #1019 / #1796): 文档规则 = 持有过尾时可判定态延至**尾+50窗**, 按结束时间 :=
     该时刻 → endDiff 恒为 +136, 与头误差合成 tier → **七例全部命中**。
     我们 held-through→Perfect 是错的。
  ② #333 great: press1(22478→22572) 按完已过尾−136 → 即时结算, 头5≤1.1×49 且
     总83≤2.2×49 → 300 ✓ (「最后一次按键覆盖」规则)。
  ③ #344 good: press2(22648, 头前 miss 窗内)构成中断 → press3 结算出 300 后
     **封顶 200** ✓。其读取器重建 head=MISS@22648 判定为误归属
     (文档明言 LN 头不会因过早按而 miss)。
- **文档判定的移植 bug (待修, 即 v3 主体)**:
  ① LN 总误差倍数写死 2.0×win, 应分级 2.4(max)/2.2(300)/2.0(200/100);
  ② held-through 无条件 Perfect, 应为「尾+50窗结算, endDiff=+136」;
  ③ 中断封顶 200 应与 requeue 解绑 (条件 = 中断发生过)。
- fix1 (LN 头晚窗 start+meh) 与文档一致: 「滑条头可判范围 = 普通note可判范围」剪至头处。
- 仍开放: #1394 (中断过→MAX 无封顶) vs #344 (中断过→封顶 200) — 「中断」的精确定义
  (按结束落在头前/后的哪一侧才计入) 待取证; −171/−168 分界在帧量化内不可分辨。
- v3 实现清单: 总倍数分级 + 尾+50窗结算 + cap 解绑 requeue + body-catch (深夜 IV) + 本节修正。

## 2026-10-08 深夜 VI: v3 两项实验否证 (已回滚, 冻结在 fix2)
- ① LN 绑定放宽为「e>meh 且未过尾端 → 体中段接住」: azimuth −0.307→−0.340
  (更差), 其它图不动 → azimuth 恶化根因**不是**体中段 catch, 另有机制。
- ② held-through 尾误差钳制 min(endDiff, 50窗) (mania_number 持有过尾规则):
  七个解题案例成立, 但 bz 全图 dACC −0.658→−0.706 (净更差) → 规则方向对、
  边界条件不对 (可能仅在「头已判且非中断」路径生效, 或与 +50 窗强制截断交互)。
- 结论: fix2 (LN 头晚窗 start+meh + NOREQUEUE 默认 ON + LN 头早灰忽略) 仍是
  已知最优: bz 143 差异 / V1 Σ|dACC| 5.487。代码已回滚至 fix2 并重建。
- 与 mania_number 文档的关系修正: 核对源码后, tier 总误差倍数 2.0×(windowOf×
  头倍数) 与文档 2.4/2.2/2.0 分级**数学等价**, 并非移植 bug (深夜 V 记载有误,
  以本节为准)。
- 后续路径: ①用读取器对 azimuth / piano 出逐物件真值 (定位 azimuth 恶化真因
  + piano +1.071); ②误差流时钟差 (~26-32ms) 确认后重解 #1394/#344 中断定义;
  ③Swan/VibroLovers/Believe 三图的 ours 已测 (89.98756 / 86.57315 / 93.26599),
  缺 declared 对照。

## 2026-10-08 深夜 VII: 播放步进式重构 (V1 事件驱动 → 逐帧时钟驱动, 数值与 fix2 逐位一致)
- **目标**: V1 从「纯事件驱动」(参考实现 OsuManiaJudger 移植, 无 sweep, press 可能绑到
  早已过期的头, 只能靠 fix1 的 tooLate 特判补救) 重构为与 stable 本体一致的**播放步进式
  仿真**: 每一 replay frame 推进时钟 → sweep 过期 → press 迁移 → release 结算 LN。
- **实现** (hit_matcher.cpp, 5 处, +34/−37, diff 存 build/stepwise_v3_hit_matcher.diff):
  1. `lateBound()`: V1 = start+50窗 (w.meh), V2 = headLateBound (默认 miss 窗) 不变;
  2. 帧循环 `sweepMisses(t)` 改为 V1/V2 都逐帧执行 (原 V1 只在回放结束单次 flush);
     每帧顺序 = sweep → press (fix2 scan 原样保留) → release (resolveTail);
  3. LN 状态机注释落在帧循环处: pending(头未判; sweep 在 start+50w MISS 之;
     press ∈ [−50w,+50w] 开持; (−miss,−meh) 早灰不判不杀) → holding(activeHold,
     对 sweep 免疫) → settled(resolveTail 恰推一条合并判定) / dead(断尾即死不可复挂);
  4. 终局 flush `sweepMisses(lastFrameTime + lateBound() + 1)`: V1 flush 集合
     {start ≤ lastFrameTime} 与 fix2 的单次 flush 逐位一致; V2 表达式原值不变;
  5. 移除 MRA_FRAME_SWEEP 实验块 (V1 专用 env 旋钮, 被本语义取代, 无外部引用)。
- **等价性分析 (为什么全部数值不变)**: fix1 的 LN 头晚窗本质是「按侧时间规则」
  (tooLate = e > w.meh, kill+continue 即 flow); 本次把它升格为「钟侧规则」, 而 scan
  分支原样保留为 NOREQUEUE=0 回退路径的安全网。逐 case: ①未击头无后继 press —
  fix2 终局 flush MISS ↔ v3 sweep 提前 MISS, 判定同; ②press 落在过期头后 — fix2
  scan 杀+flow ↔ v3 头已被 sweep、press 直接 flow, 事件与绑定同; ③[−50w,+50w]
  press 开持 — activeHold 不在 pending, sweep 永不触及; ④rice (ok,50窗) 的 24ms 带
  (meh−ok=24 恒定) — scan tooLate (e ≥ ok) 分支仍在, kill+flow 与 fix2 同; ⑤终局
  flush 集合不变。边界: sweep 严格 `start+meh < t` (e == meh 同帧 press 仍命中,
  与 fix2 锚点 +117 命中/+157 已死一致; 帧钟整数下与 scan 双精度边界无重叠带)。
- **架构收益**: press 永远看不到已过期头 (pending 始终干净), 头过期语义只存在一处
  (时钟), 不再可能被新代码路径漏掉特判 (azimuth 教训); MISS 获得时钟时刻
  (resolveMissHead 收到帧 t), 未来误差时间线/可视化可直接使用。
- **测量 (gate 全过, 无一步回退)**:
  | 图 | fix2 dACC | v3 dACC | 判定 |
  |---|---|---|---|
  | ende | 0 | 0 | ✓ (控制谱保持 0) |
  | promise | 0 | 0 | ✓ (控制谱保持 0) |
  | ice | −0.017 | −0.017 | ✓ |
  | newspapers | +0.060 | +0.060 | ✓ |
  | azimuth | −0.307 | −0.307 | ✓ |
  | penguin | −0.143 | −0.143 | ✓ |
  | bz | −0.658 | −0.658 | ✓ |
  | piano | +1.071 | +1.071 | ✓ |
  | FUBUKI | +2.946 | +2.946 | ✓ |
  | MachineGun | −0.285 | −0.285 | ✓ |
  Σ|dACC| 5.487 → 5.487 (10 图 dACC 与 fix2 全同)。
  * bz 逐物件对照 (读取器真值, 口径 = 深夜 IV): **143/143, 与 bz_v1_diffs_fix2.json
    集合完全一致 (missing 0 / extra 0)** — gate ≤143 ✓。
  * V1 事件流 10 图 SHA-256 与 fix2 实测流逐位一致 (事件内容/绑定零变化)。
  * V2 组 + 控制谱 15 图 (test1006/62/63 + OMOIDE..Pallet) hitsV2 SHA-256 /
    counts / accuracy 与 fix2 逐位一致 — gate ✓ (改动全部限定在 !scoreV2 路径;
    Sunflower/4KLNDan/TwinBloom 首轮 summary 未就绪为会话劣化, 换新端口重启引擎
    后复测一致, 非语义差异)。
- **单元测试**: stablePress r6 子案 (LN 断尾 requeue, 期望 4 事件 + Meh 封顶) 是
  pre-fix2 requeue 语义, fix2 翻转默认时漏更新 — 用 fix2 backup 源验证两处 FAIL
  为 pre-existing 非本次回归。已更新为 fix2 语义 (2 事件 + Miss + 1 unmatched),
  并新增 r9 sweep 回归案 (LN 头过 50w 未按 → 帧 sweep 出 head+tail MISS 对,
  后继 press 拿不到死头、流至下一候选)。全部 11 案 ALL TESTS PASSED。
- **回滚点**: build/hit_matcher.cpp.fix2-backup (源) +
  build/ManiaReplayAnalyzer.exe.fix2-backup (fix2 二进制)。
- **数据**: build/stepwise_base_*.json (fix2 对照局) / stepwise_v3_*.json (重构后) /
  stepwise_v3_bz_diffs.json (143 清单) / stepwise_base_bz_diffs.json (复现校验) /
  stepwise_v2_fingerprint_fix2.json / stepwise_v2_fingerprint_v3.json。
- **结论**: gate 1 (bz ≤143, 集合不变) ✓; gate 2 (V2 全组逐位不变) ✓;
  gate 3 (ende/promise = 0) ✓; 任何 V1 图 |dACC| 无一回退 ✓。
  冻结状态 fix2 → fix2+stepwise (数值同 fix2, 架构对齐 stable 播放循环)。
  后续语义实验 (残余 143 归因、azimuth 真因) 现在可以在时钟驱动模型上做。

## 2026-10-08 深夜 VIII: 高密度残差攻坚 — F1 尾结算机制 (bz 143 → 128, 全 gate 通过, 附 bz↔Swan 机制冲突发现)

> **决策记录 (深夜 IX 后; 深夜 X 更正)**: ship = A2。azimuth 修正如深夜 IX 所述——但深夜 X 证明该修正基于同名异图, **撤销**: azimuth 回归 counts 基准 (fix2 −0.307 / A2 −0.199, A2 改善 +0.108 维持)。「+0.108 未决代价」不再存在。
- **主攻对象**: bz 143 残差两大家族。逐案解剖 (build/stepwise_families.json):
  F1 一级差 = 54 (meh→ok 20 / ok→good 21 / good→great 13; **52 hold + 2 rice**, 尾公式带);
  F2 miss→hit = 41 (我们 36 hold + 5 rice 全部**窗内绑定**, 归属交换, 本轮挂起);
  其余 48。帧流夹具截断 185s (80/143 在覆盖外), 用读取器全量重建补稳定侧。
- **F1 破案**: 21/26 在帧域内的 F1 hold 是 **swift-settle** (head press ≥ tail 即时结算,
  endDiff := press−尾); 帧真值显示玩家按住不放, 真实松键在 +77..+153 (如 #954
  press@尾+0 up@+77 → stable good; #911 press@尾+5 up@+112)。参考实现的
  「press 到尾即结算」是 bz F1 的直接机制来源。
- **行走记录** (每步独立: 改→建→bz 逐物件+全表→记录):
  1. **Walk A** (移除 swift settle, 等真实松键; MRA_V1_SWIFT_SETTLE=1 可还原):
     bz 143→**108**, F1 修 35, azimuth −0.307→−0.027, 无图即 gate 劣化 —
     但 **Swan ours −0.311** (6 例 late-catch 降档)。
  2. **Walk B** (LN 头前灰按 = 「中断」→ 封顶 200, #344 模型): bz 107, 31 例改判中
     仅 #344 为真 fix, 30 例误封顶 (stable=miss/ok/perfect 全踩), penguin +0.054
     **gate fail → 回滚**。结论: 灰按存在≠中断, 「中断」判别式与 #1394/#344 悬案
     同源, 需读取器误差流级证据 (维持深夜 V 结论)。
  3. 假设 (c) startDiff 阈值 (−meh → −miss): **死亡** — 52 例 F1 hold 中 0 例头灰
     (阈值改动是 no-op), 未实现。
  4. **Walk D** (持有过尾 endDiff := min(|松键−尾|, 50窗), mania_number 尾+50窗规则;
     MRA_V1_HELDTHRU_EXT=0 关): 在 **A 构成上** bz 108→**101**, 全图无劣化。
     注: 深夜 VI ② 否证的是 swift-settle 构成上的同一公式 (press≥尾 案例根本到不了
     该分支); A 之后构成不同, 复测为正收益, 非盲目重试。
  5. **Swan 归因** (2×2 env 矩阵): −0.311 全部来自 Walk A (D 中性);
     6 例 late-catch, span 43-44ms, 松键 +116..+147, **前一松键 −5..−15**。
  6. **Walk A2** (尾结算松键选择 = lead 假设 (a) 原文: 结算用**离尾端最近**的松键,
     候选限尾±50窗可判定带; MRA_V1_NO_NEARREL=1 关): Swan/VibroLovers/Believe
     **逐位还原 fix2**, penguin −0.143→**−0.007**, bz −0.658→−0.637; 代价 bz
     101→**128** (13 例 A+D 已修案例被前一松键翻回高档)。
- **核心发现 (机制级)**: bz 与 Swan 对**同结构** late-catch (短 LN, press≥尾,
  真实松键 +116..+153, 前一松键 −5..−98) 给出**相反判决** — bz 逐物件真值要求
  真实松键, Swan count 级要求前一松键; prevRel 几何无判别界 (bz 误升案例
  −30..−98 vs Swan −5..−15 连续重叠)。**需 Swan 级读取器真值**才能收敛该规则族。
- **Shipped = A+A2+D** (vs fix2+stepwise: +52/−6, diff: build/stepwise_walks_final.diff):
  | 图 | fix2 dACC | 深夜VIII dACC | Δ|dACC| |
  |---|---|---|---|
  | ende | 0 | 0 | 0 |
  | promise | 0 | 0 | 0 |
  | ice | −0.017 | −0.017 | 0 |
  | newspapers | +0.060 | +0.055 | −0.005 |
  | azimuth | −0.307 | −0.199 | **−0.108** |
  | penguin | −0.143 | **−0.007** | **−0.136** |
  | bz | −0.658 | −0.637 | −0.021 |
  | piano | +1.071 | +1.071 | 0 |
  | FUBUKI | +2.946 | +2.946 | 0 |
  | MachineGun | −0.285 | −0.285 | 0 |
  Σ|dACC| 5.487 → **5.217**; Swan/VibroLovers/Believe ours 与 fix2 逐位一致
  (89.98756 / 86.57315 / 93.26599)。
- **Gate**: bz 逐物件 **128 < 143** ✓ (构成: F1 残 19 + F2 41 + 稳定高-我们miss 22 + 散点 46);
  其余 V1 图 |dACC| 全部无 >0.02 劣化 ✓; V2+控制谱 15 图指纹逐位不变 ✓;
  ende/promise = 0 ✓; 单元测试 ALL PASSED ✓。
- **挂起 (按证据需求排序)**: ① Swan 读取器逐物件真值 (解 bz↔Swan 冲突, 预期解锁
  bz 101 甚至更低); ② F2 41 例归属交换 (窗内绑定, 需 stable 绑定侧证据);
  ③ 稳定高-我们 miss 22 例 (镜像家族); ④ F1 残 19 (cap 判别式, 同 #1394 悬案)。
- **回滚点**: build/hit_matcher.cpp.pre-walkA (= fix2+stepwise) / pre-walkB /
  pre-walkD / pre-walkA2; env 开关可逐一关闭 A/A2/D (SWIFT_SETTLE=1 /
  NO_NEARREL=1 / HELDTHRU_EXT=0)。
- **数据**: build/stepwise_families.json / stepwise_f1_holds.json / stepwise_f1_cycles.json /
  stepwise_f2_rows.json / stepwise_walk{A,B,D,A2}_bz_diffs.json /
  stepwise_walk{A,B,D,A2}_summary.json / stepwise_walkB_changed.json /
  stepwise_swan_changed.json。

## 2026-10-09 深夜 IX: declared 基准修正 (用户证伪 azimuth counts 参考) + 全表重估 + 语义画像

> **决策记录 (深夜 X 更正)**: 95.05/95.06 锚点经查来自**同名异图** (LN Dan Phase III [Azimuth Dan], md5 21a9c6b6, 10980 notes), 本节 declared 修正**撤销** — azimuth declared = counts 公式 94.3705 (与 .osr/scores.db 自洽)。katu→geki 显示 quirk 理论随之死亡。详见深夜 X。
- **触发**: 用户实测 azimuth 成绩标注 (scores.db) = **95.05**, 游戏内回放结束 = **95.06**
  (两表面一致), 而 .osr counts → V1 公式 = **94.3705** — stable 显示与落盘自相矛盾。
- **取证 (字节级)**:
  1. .osr 直接解包 (客户端 20260711): mode3/beatmapMd5/player/replayMd5/
     counts(8248,279,153,8308,1043,375)/score 761533/maxCombo 1142/mods 0/hp
     graph/timestamp(=playedTicks)/replay 数据/onlineId — **无 accuracy 浮点字段**
     (字节精确消耗至 onlineId, 0 残留; 旧 .osr 规范本无此字段)。
  2. scores.db 同 replayMd5 (63e81584…) 条目 counts 与 .osr **完全一致** →
     我们解码无误, 落盘链路自洽。
  3. **13 图全表审计**: 每图 .osr counts = scores.db counts 逐位一致
     (build/stepwise_declared_audit.json) → 无第二图存在「counts≠落盘」;
     分叉只发生在 stable 的**显示计票**层。
  4. 显示值拟合: counts + **375 个 katu→geki** → 95.0496 ≈ 95.05 (4dp 精确);
     其余单参数变体全否证 — tails-included (93.94/94.75)、lazer/305 权重
     (88.5/92.9)、miss→n300 (K=125.07 非整)。机制指向 LN 尾彩 (geki) 在落盘时
     被 demote 为 katu (~375 个, 36% of katu)。**基准修正 = 采用用户实测显示值**;
     其它图 counts 公式仍无显示对照 (潜在暴露见下表 quirkExposure 列, vibro 组
     1.1~1.9pp — FUBUKI/Swan/VibroLovers 的 counts 基准 dACC 可能被高估!)。
- **修正基准全表** (declared: azimuth=95.05 用户锚定, 其余=counts 公式;
  build/stepwise_corrected_declared_table.json):
  | 图 | declared | ours(A2) | dACC A2 | dACC fix2 | A2−fix2 |Δ| | quirkExposure |
  |---|---|---|---|---|---|---|
  | ende | 97.5404 | 97.54035 | 0 | 0 | 0 | 0.4pp |
  | promise | 99.8263 | 99.82629 | 0 | 0 | 0 | 0.1pp |
  | ice | 97.3789 | 97.39575 | −0.017 | −0.017 | 0 | 0.4pp |
  | newspapers | 96.8475 | 96.7925 | +0.055 | +0.060 | −0.005 | 0.5pp |
  | **azimuth** | **95.05** | 94.56971 | **+0.480** | **+0.373** | **+0.108** | 0.7pp |
  | penguin | 95.2060 | 95.2130 | −0.007 | −0.143 | −0.136 | 0.7pp |
  | bz | 89.9541 | 90.5911 | −0.637 | −0.658 | −0.021 | 1.2pp |
  | piano | 94.1546 | 93.0836 | +1.071 | +1.071 | 0 | 0.6pp |
  | FUBUKI | 87.5932 | 84.64679 | +2.946 | +2.946 | 0 | 1.9pp |
  | MachineGun | 84.6651 | 84.95053 | −0.285 | −0.285 | 0 | 1.6pp |
  | Swan | 93.1281 | 89.98756 | +3.141 | +3.141 | 0 | 1.1pp |
  | VibroLovers | 90.9152 | 86.57315 | +4.342 | +4.342 | 0 | 1.3pp |
  | Believe | 93.2660 | 93.26599 | 0 | 0 | 0 | 1.6pp |
  Σ|dACC| (10 图有历史对照) 修正后: fix2 5.553 → A2 **5.498** (counts 基准口径
  5.487→5.217 的「改善」在修正基准下缩水但方向不变)。
- **结论翻转清单 (用户指令第 4 项)**:
  1. **azimuth 符号翻转**: 修正基准下 fix2 = **+0.373** (stable 偏高, 我们欠账),
     A2 = **+0.480** — 不是「我们偏高 −0.307/−0.199」。azimuth 从「深夜 VIII 改善
     幅度最大」翻转为「**A2 相对 fix2 劣化 +0.108** (唯一 >0.02 gate 违例)」;
     fix2 在 azimuth 上才是更优配置。
  2. 深夜 VIII 的 penguin (−0.136) / newspapers (−0.005) / bz (−0.021) 改善**维持**,
     10 图修正 Σ 仍 A2 净优 (5.498 < 5.553) — A2 整体结论不翻转但不再无代价。
  3. 报表口径: 此前所有 counts 基准 dACC 在 LN-heavy 图上**系统性偏高估我们**
     (欠账被 counts 少记掩盖); vibro 组 (+3.1/+4.3/+2.9) 最多再虚高 1-2pp。
- **azimuth 语义画像** (A2, hits 域; build/stepwise_azimuth_a2.json):
  10913 holds (59% LN) / rice 7493; late-catch 251 (2.3%, span 中位 41ms —
  **bz F1/Swan 同族**); 松键带 held-through 5320 / 窗内早松 5253 / break 340;
  我们 Miss 总数 693 vs declared miss **375** — 超额 ~318, 量级与缺口
  (0.37-0.48pp ≈ 206-266 个 100 权重单位) 吻合。
- **requeue 矩阵 (env 即测, 无 rebuild)**:
  | 配置 | azimuth ours | 真实 gap |
  |---|---|---|
  | A2 (shipped) | 94.56971 | +0.480 |
  | **A2 + NOREQUEUE=0** | 94.8658 | **+0.184** |
  | fix2 (swift) | 94.67746 | +0.373 |
  | **fix2 + NOREQUEUE=0** | 94.97265 | **+0.077** |
  全表 (A2+rq0): newspapers −0.016/piano +1.036 改善, 但 **bz 175 (>143 gate
  fail)**、penguin counts −0.442 恶化 → 不可全局 ship, 与 bz #479 逐物件真值
  (断尾判死) 方向性冲突。**「断尾何时可复挂」判别式 = 下一轮首假设**;
  azimuth 缺口的最大单一解释项就是它 (~0.3-0.4pp)。
- **下一轮假设排序**: ① requeue 判别式 — 用读取器对 azimuth (LN-heavy 59%) 出
  逐物件真值, 直接看断尾 LN 的 stable 终判 (env 证据已备: MRA_V1_NOREQUEUE=0);
  ② vibro 图 display 实测 (quirkExposure 1-1.9pp — FUBUKI +2.946 可能大幅虚高);
  ③ bz↔Swan late-catch 冲突 (深夜 VIII 遗留, azimuth 251 例 late-catch 可并入);
  ④ cap/中断判别式 (#1394/#344 悬案)。
- **决策请求 (lead)**: shipped A2 在修正基准下 azimuth +0.108 劣化 (唯一 gate
  违例)。保留 A2 (bz 逐物件 + penguin/bz/newspapers 计数级改善) 或回滚
  fix2+stepwise (azimuth 更优) — 需权衡; 混合方案 = 等 azimuth 读取器真值后
  做 requeue 判别式再定。
- **数据**: build/stepwise_declared_audit.json / stepwise_corrected_declared_table.json /
  stepwise_azimuth_a2.json / stepwise_rq0_{summary,bz_diffs}.json /
  stepwise_azimuth_replaypath.txt。

### 深夜 IX 追补: requeue 判别式备料 (bz #479 死 ↔ azimuth 需复挂 的结构特征表)
- azimuth rq0 逐物件 diff (build/stepwise_az_rq0_changed.json): 89 条 LN 终判改变,
  85 升 (miss→good 69 / miss→ok 7 / miss→meh 5 / miss→great 3), 4 降 — 复挂收益
  真实且大 (= azimuth 缺口最大单一解释项)。
- 特征对照 (四分位 q25/med/q75; bz 帧域 60 条断尾 LN 按读取器终判分组;
  azimuth 无帧流, breakErr 取自 A2 hits 尾事件):
  | 特征 | bz CREDITED (stable≠MISS, n=15) | bz MISS (stable=MISS, n=45) | azimuth rq0 获判 (n=89) |
  |---|---|---|---|
  | LN span | 167 / 187 / 667 | 84 / 166 / 167 | 132 / 176 / 240 |
  | breakErr (松键−尾) | −314 / −215 / −199 | −175 / −159 / −146 | −249 / −198 / −160 |
  | repressInBody | 62 / 83 / 107 | 71 / 87 / 97 | (无帧流未测) |
  | gapNext (尾到下根) | 83 / 83 / 84 | 83 / 83 / 84 | (未测) |
- **读法**: azimuth 获判群体 ≈ bz CREDITED 群体 (长 span + 深断), 与 bz MISS
  (短 span + 浅断) 分离; gapNext 无判别力。机理候选规则 (下轮待证):
  **「复挂存在, 但仅当最终循环的松键到达尾窗 (≥尾−50w, 即断后重新握住到尾)
  才覆盖 break 的 MISS; 否则 break Miss 成立」** — 可同时解释 #479 (断后再断→MISS)
  与 azimuth/Swan (断后握到尾→获判)。breakErr 深浅不是因果 (深断 = 有足够剩余
  时长重握), span × 剩余时长才是承载变量。
- 数据: stepwise_az_rq0_{changed,features}.json / stepwise_bz_broken_population.json /
  stepwise_azimuth_rq0.json。

## 2026-10-09 深夜 X: azimuth 真值任务破案 — 95.05 锚点来自同名异图, 深夜 IX declared 修正撤销
- **主任务 (requeue 判别式验证) 状态: 阻塞于输入缺失 + 输入证伪**。
- **发现链**:
  1. 读取器 output/latest.json = **Blue Zenith** (昨晚会话), 无 azimuth 会话文件
     (按 md5 前缀 c8b6075b 全目录扫描 = 0 命中)。
  2. reader.log 尾 (00:10): 用户在读取器看的是
     **「osu!mania 7K Dan Course - LN Dan Phase III [Azimuth Dan]」keys=7 od=5
     notes=10980** — 不是我们的 testset azimuth。
  3. 谱面身份铁证:
     | | 我们 testset azimuth | 用户锚点/读取器会话 |
     |---|---|---|
     | 谱面 | 7K Dan Course - **Extra Level (LN)** [Azimuth Dan (Marathon)] | 7K Dan Course - **LN Dan Phase III** [Azimuth Dan] |
     | md5 | c8b6075b9087ebdda6659062dee94dee (= .osr/scores.db ✓) | 21a9c6b63a722f5903375497306c1c6f |
     | HitObjects | 18406 | 10980 |
  4. **95.05 来源落定**: scores.db 中 LN Dan Phase III [Azimuth Dan] (21a9c6b6)
     有一局 counts 公式 = **95.0471 ≈ 95.05** (N=10980, playedMs 1782919272886)。
     用户的「scores.db 标注 95.05 / 回放结束 95.06」= 该图该局, 不是我们的
     Extra Level (LN) 局。
- **故: 深夜 IX declared 修正撤销** — azimuth declared = counts 公式 **94.3705**
  (.osr = scores.db 自洽, 从未需要修正); 「katu→geki 显示 quirk」理论死亡
  (为解释混谱而生造; 13 图自洽审计结论本身仍成立且有价值)。
- **dACC 回归** (counts 基准): azimuth fix2 = **−0.307** / A2 = **−0.199** —
  深夜 VIII 的「azimuth 改善 +0.108」结论**恢复**; ship = A2 的理由**强化**
  (bz 128 + penguin −0.136 + azimuth −0.108 + newspapers, 全部同向)。
- **requeue 矩阵再解读**: rq0「收敛 azimuth 缺口至 0.184/0.077」是错误参照的
  伪影 — 正确基准下 rq0 使 azimuth 恶化 (A2+rq0 → −0.495, fix2+rq0 → −0.636),
  **requeue=0 维持否证**, 与 bz #479 逐物件真值一致, 悬案解除。
  深夜 IX 追补的断尾特征表保留为方法学材料 (当前无适用场景)。
- **TrueBlue 附带项 (低优先) 同样存疑**: testset TrueBlue = **旧局** (20:37),
  declared counts 公式 = 98.50, 我们 accV2 = 98.125; 用户锚点 98.79 ≠ 98.50,
  疑似对照的是**新局 (20:41)** — 深夜 II 已证新局 2106/2106 逐位精确。
  需用户确认所看局次 (ticks) 后再核; 若为新局, 我们需取该局 .osr 单独分析。
- **给用户的重拍清单** (解除阻塞): 请对 **Extra Level (LN)** 谱面文件夹
  (529168, 18406 notes, 难度名 [Azimuth Dan (Marathon)]) 的目标局用读取器出真值;
  读取器 map 行应显示 notes=**18406** (10980 = 错图)。
- **教训入册**: 一切用户锚点 (declared/display/gap) 必须先过身份核验
  (beatmapMd5 + playedTicks/ticks + counts N) 再进基线; 「Azimuth Dan」同名
  难度跨 dan course 包是现成反例。
- 数据: build/stepwise_declared_audit.json (13 图自洽审计, 仍有效) /
  stepwise_corrected_declared_table.json (**作废**, 保留供查) /
  stepwise_azimuth_rq0_*.json / stepwise_bz_broken_population.json。

## 2026-10-09 深夜 XI: azimuth 逐物件真值接入 (delta 全零) — 266 差异解剖 + 断尾特征规则两图成立
- **输入**: test/2026-10-09_005340_c8b6075b.json (与 output/latest.json 字节一致)。
  身份核验: Extra Level (LN) [Azimuth Dan (Marathon)] md5 c8b6075b, 18406 notes
  (rice 7493 + hold 10913), od5 (窗 16/49/82/112/136/173)。
- **desync = 0**: 读取器 mine [8308,8248,1043,279,153,375] 与 declared delta 全零
  (读取器质量远超 bz 场; summary.acc 94.37 = counts 公式精确一致 →
  **V1 display = counts 零例外确认**, 与用户截图 94.37 互证)。
- **ship 态 (A2) 逐物件**: **266 差异 (1.45%)**, stable 偏高 84 / 我们偏高 182。
  分布: ok←good 53 / good←miss 37 / meh←ok 36 / good←great 31 / meh←miss 21 /
  miss←great 22 / miss←perfect 11 / 其余散点。dACC = −0.199 ✓ (与 counts 基准一致)。
  A+D 配置 (NO_NEARREL): **181 差异**, ours 94.43116 (dACC −0.061) — A2 的
  nearest-release 在 azimuth 是净伤害 (late-catch 族 97 差异中 85 条是它的伪影)。
- **late-catch 族逐案裁决 (251 例, span q 41/41/44, pressOff 50/64/99)**:
  A2 下 97 差异, **全部 stable-lower** (50←ok 35 / 100←good 49 / 200←great 6 /
  100←great 4 / 300←good 1 / 300←ok 1 / MISS←good 1) — stable 按**真实松键**
  判 (ed 90..138 → ok/meh), A2 的前一松键候选高抬一档。即 azimuth 与 bz 同边
  (要真实松键), 与 Swan 相对; 三图冲突实质 = azimuth/bz (逐物件真值) vs Swan
  (count 级)。A+D 下该族残差 12。
- **断尾族裁决 (119 例, A2 事件域)**: stable CREDITED 63 / MISS 56。
  | 组 | span | breakErr | reader 尾误差 |
  |---|---|---|---|
  | az CREDITED (63) | 180/245/353 | −321/−238/−194 | −42/−11/+23 (重握到尾) |
  | az MISS (56) | 120/163/204 | −177/−156/−147 | −122/−20/+12 |
  与深夜 IX 备料表方向**完全成立** (深断+长 span → 获判; 浅断+短 span → MISS);
  两图合并出现**完美分离**: bz MISS 最深 −175 / az MISS 最深 −177,
  bz CREDITED 最浅 −199 / az CREDITED 最浅 −194 → breakErr ≤ ~−194 → 获判,
  ≥ ~−175 → MISS, 243 例 (bz 60 + az 119 有效) 零跨界。reader 尾误差在断尾
  场景两组重叠, 无独立判别力 (与深夜 II 读取器局限①一致)。
  我们 A2 对全部 119 断尾判 Miss → 63 条差异 = **深断重握获判** 未实现
  (下轮首实现候选; bz 同族 15 条 + az 63 条)。
- **残余分解 (A+D 181)**: 深断重握 63 + late-catch 残 12 + 散点 ~106。
- **顺手辨认 (lead 项)**: a5af1629 = **True Blue (Cut Ver.) [Repentance (MWC Edit)]**
  (testset TrueBlue, 2106 notes, od6) — 两场 reader 会话: 154913 **acc 98.79**
  (= 用户新局 20:40:51 锚点, display 侧逐物件真值已存在!) / 155559 acc 98.06
  (旧局)。967fdec0 = **Negai [Wish]** (3214 notes, od6.5), acc 96.22 = 权威表
  锚点 ✓。output/latest.json 同步为 azimuth 场。
- **display-acc 规则修订建议 (基于 reader 真值, 供 lead 定)**: TrueBlue 新局
  saved counts 公式 = **99.204** (非 98.125 — 那是旧局我们的 accV2, 跨局误比);
  display 98.79 vs saved 99.204 = **−0.414** (旧局: reader 98.06 vs saved 98.5001 =
  −0.44) — 两场一致 → **V2 quirk 方向 = 显示计票低于落盘** (落盘把持有过尾的 LN
  尾计为 300/彩档, 显示按真实尾判定), 而非深夜 X 前假设的「落盘 demote」。
  Twin Bloom: display 94.575 ≈ 我们 accV2 94.565 (+0.01, ±0.02 容差内) —
  **V2 display ≈ 我们 lazer 口径 accV2** 的假设获得两图支持 (azimuth V1:
  display = counts 公式 ✓ 零例外)。display-acc 落地建议: V1 = stableAccuracyV1;
  V2 = accuracyV2 (现成字段), 待 TrueBlue 新局我们引擎分析 (ticks 可由
  .osr 文件名 134343816516154445 反解) 后定稿。
- **gate**: 判定未动, bz 128 / V2 指纹不变 ✓。
- 数据: build/stepwise_azimuth_a2_diffs.json / stepwise_azimuth_AD_diffs.json /
  stepwise_azimuth_{latecatch,breaks}.json / stepwise_bz_az_breaks_compare.json。
- **下轮排序**: ① 深断重握获判规则实现 (breakErr ≤ ~−194/−199 阈 + az/bz 零跨界
  已验; 量化收益 az 63 + bz ~15); ② A2 nearest-release 撤销评估 (azimuth 证据
  支持 A: az −0.061 vs −0.199, bz 108 vs 128; 代价 Swan ±0 → −0.311, 重新权衡);
  ③ TrueBlue 新局入回归 (ticks 反解) + V2 display 规则定稿。

## 2026-10-09 深夜 XII: 撤销 A2 + 深断重握获判 (用户拍板「优先满足锚点图」) — bz 97 / azimuth 132 双双历史最优
- **用户决策**: 优先满足锚点图逐物件对应; vibro 组 (Swan/VibroLovers/Believe)
  暂缓 (count 级证据层级最低), Swan −0.311 为接受成本。
- **改动 1 — A2 撤销** (MRA_V1_NEARREL=1 可复现): nearest-release 回退为关闭,
  基线 = A+D (真实松键结算 + 持有过尾钳制)。azimuth 真值 97 条 late-catch
  差异全部为其伪影 (深夜 XI), 撤销后该族残差 12。
- **改动 2 — 深断重握获判** (MRA_V1_NO_REGRIP=1 关; MRA_V1_BREAK_BOUND=185 可调;
  MRA_V1_NO_REGRIPCAP=1 关 cap):
  - LN 断尾 (松键 < 尾−50w) 且 end−rel ≥ 185 (深夜 XI 两图 243 例零跨界:
    az CREDITED ≥194 / MISS ≤177, bz ≥199 / ≤175) → 不再即死, 转入
    v1BrokenAwait 等待重握;
  - 同列下一 press 重绑 (推入新 head 事件, 最后一次按键覆盖), 结算在该循环
    松键: 握到尾窗 (≥尾−50w) → 正常合并公式获判; 再断浅 → Miss; 再断深 →
    继续等待;
  - 尾+50w 未重握 / 回放结束仍未重握 → break Miss 成立 (尾事件 + hold 补记);
  - 重握结算**封顶 200** (mania_number 中间断过 → 彩/黄300封顶200;
    MRA_V1_NO_REGRIPCAP 关闭实验: bz 105/az 157 + penguin +0.102 gate 违例 →
    cap 后 bz 97/az 132 + penguin +0.007 达标 → cap 保留)。
- **锚点精确度报告 (最高目标)**:
  | 图 | fix2 | A2 | **深夜 XII (ship)** |
  |---|---|---|---|
  | bz 逐物件 | 143 | 128 | **97** |
  | azimuth 逐物件 | (未测) | 266 | **132** |
  bz 分布: miss←great 21 / good←great 16 / ok←good 7 / meh←ok 4 / miss←perfect 22
  (深断重握净修: good←miss 10→2, meh←miss 5→1; 残余过判散点 miss←great +3)。
  azimuth 分布: good←great 25 / miss←great 20 / miss←good 13 / good←perfect 8 /
  meh←miss 9 (残余 = late-catch 12 + 断尾浅判 + 散点)。
- **display-acc 锚点验证 (V2 显示 = 我们 accuracyV2, 现成字段)**:
  TrueBlue 新局 (ticks **639255048516154445**, 由 .osr 文件名反解 + 头部
  timestamp 字段双重验证): 我们 **accV2 = 98.7946 ≈ display 98.79 (Δ0.005)** ✓。
  该局 saved counts 公式 = 99.204 → V2 quirk 实锤且方向确认: **落盘计票高于显示**
  (持有过尾的 LN 尾按 300/彩档落盘, 显示按真实尾判) — 深夜 X 表 TrueBlue 行
  据此关闭。azimuth (V1): display 94.37 = counts ✓ 零例外。用户指令入册:
  **以后用户参照均为显示 acc**; V1 显示 = counts, V2 显示 = accuracyV2。
- **全表 (counts 基准 dACC, fix2 对照)**: ende 0 / promise 0 / ice −0.017 /
  newspapers +0.008 / azimuth −0.240 (改善 +0.067) / penguin −0.150 (劣化
  +0.007, 容差内) / bz −0.681 (劣化 +0.023, 锚点图以逐物件残差最小化优先) /
  piano +1.036 (改善) / FUBUKI +2.946 / MachineGun −0.285; vibro: Swan
  −0.311 (用户接受), VibroLovers/Believe 0。单元测试 ALL PASSED
  (r6 更新为深断重握语义: 3 事件 / meh / 无 unmatched)。
- **gate**: bz 97 ≤ 128 ✓ (历史最优); azimuth 132 (历史最优) ✓; V2 指纹 15 图
  逐位不变 ✓; ende/promise 0 ✓; 非 vibro 图除 bz (+0.023, 锚点图按残差优先)
  与 penguin (+0.007) 外全部 ≤0.02 ✓。
- **残余方向**: bz miss←great 21 + azimuth miss←great 20 (重握过判/late-catch
  高档残留) / good←great 25+16 (一级差带) / 深断重握的再断浅判边界。
- **回滚点**: build/hit_matcher.cpp.pre-walkE (= 深夜 XI 态) +
  ManiaReplayAnalyzer.exe.fix2-backup; env 全套可逐项关闭 (NEARREL / NO_REGRIP /
  BREAK_BOUND / NO_REGRIPCAP / SWIFT_SETTLE / HELDTHRU_EXT)。
- 数据: build/stepwise_walkE{,2}_{bz,az}_diffs.json / stepwise_walkE2_summary.json /
  stepwise_trueblue_new.json。

## 2026-10-09 深夜 XIII: 锚点残差三家族反向定位 (零判定改动) + 4KLNDan 分解
- 本轮四项全为分析/验证, **判定零改动** — gate 依托深夜 XII 态: bz 97 / azimuth
  132 / V2 指纹不变 / ende/promise 0 (自动维持)。
- **① miss←great (bz 17 / az 19): 「深断重握过宽」假说否证**。逐案分类:
  36 例中 **0 例**来自重握结算或 late-catch — 全部是「普通 press + 50w 内早松」
  的**短 LN** (bz span 83/83/83 同 col6 jack 族; az 82-176), 我们按合并公式判
  great (sd 9-45, ed 20-84), stable 判 MISS。reader 重建在这些案例上
  **自相矛盾** (bz #3690: reader head 300 + 尾 MAX 但 game=MISS; az #1329:
  reader 尾 −214 vs 帧真值 −37; bz #414: reader 虚构 −170 press, 帧真值 −13)
  — 即深夜 II 读取器局限①的场景。定性 = **stable 绑定差异** (press 归属前根
  或忽略), 现有证据 (帧 + reader) 无法给出判别式; 需 stable 内部绑定证据。
- **② good←great (bz 7 / az 24): 公式方向否证**。全量 hold 三变体模拟
  (现公式 / 头 sd 用素窗 / 总和用素窗): 不匹配数 bz 130 vs 591 vs 547,
  az 208 vs 1305 vs 1178 — **现公式大幅最优**。great 带上下文: 公式判 great
  的单元中 stable 同判 great 占 99%+ (bz 2387 vs 17, az 6000 vs 58), 早松不
  降档 (bz 早松→great 1371)。→ good←great = **0.7-1.0% 中断/上下文例外**
  (#344 = 灰按 cap 已解; #813 类 = 悬案), 非公式带问题; 待中断定义
  (误差流证据)。
- **③ 深断再断一致性**: 实现天然统一 (再断走同一 earlyDeath/bound 判定),
  新增 r10 测试锁定: 三次深断 (−600/−450/−300) 依次 defer + 第四循环浅断
  (−150 < 185) → break Miss; 4 head + 1 tail = 5 事件, 无 unmatched。
  修正点: 浅断测试松键取 1850 (尾窗 50w 内为正常合并, 非「再断」)。
- **④ 4KLNDan 12th 分解** (v2, 判定未动): declared counts 公式 = **98.8299**;
  display 锚点 98.15/98.17 (mid 98.16) → **落盘−显示 quirk = +0.67** (方向与
  TrueBlue 一致: 落盘高于显示); 我们 accV2 = **98.356** → display 口径真实
  判定残差 = **+0.196 (我们偏高)**。结构画像: 8702 objects 中 LN 7037 (81%,
  LN dan 谱); v2 轮候审 (TB 81%→±0.01 精确, 4KLNDan 81%→+0.196 — 同 LN 密度
  不同结果, 差异在 v2 尾判或 notelock 细节, 非密度本身)。
- **用户指令入册重申**: 用户参照 = 显示 acc; V1 显示 = counts (azimuth 零例外),
  V2 显示 = accuracyV2 (TrueBlue 新局 Δ0.005 命中)。
- 数据: build/stepwise_xiii_{bz,azimuth,4KLNDan}.json。

## 2026-10-09 深夜 XIV: 四锚点图入回归 + 4KLNDan 差分诊断 (v2 轮备料)
- **A. 锚点扩充** (身份核验 = beatmapMd5 + .osr 头部 counts 匹配 + timestamp=playedTicks;
  显示口径: V1 = counts 公式, V2 = accuracyV2):
  | 图 | ticks | declared/display | ours | dACC |
  |---|---|---|---|---|
  | LN Dan Phase III [10th Dan] (v1) | 638800566152052311 | counts 97.1321 = 标注 97.13 ✓精确 | 97.40244 | **−0.272** |
  | Artificial Human [SCP] (v1) | 638764382782124936 | counts 92.1720 = 标注 92.17 ✓精确 | 92.50794 | **−0.338** |
  | xfpsb [5th Dan - Beyond] (v2) | 639182527588160235 | display 96.66 (saved 公式 97.2652, quirk −0.60) | accV2 96.74817 | **−0.088** |
  | 4K LN 13th [Yoru] (v2, eZmmR 变体) | 639090060691327285 (候选局) | display 97.21 | accV2 97.46729 | **+0.257 ⚠局次未定** |
  - 10th Dan/SCP: V1 display = counts 精确一致 ✓ (azimuth 结论第三次验证);
    两图我们偏高 −0.27/−0.34, 与 bz/azimuth 同向 (V1 过判家族再扩容)。
  - v2 两图 quirk 方向均 = 落盘高于显示 ✓。
  - Yoru: 4 个 mapper 变体 (ELPIS/eZmmR/LovelySerenade/_underjoy), 用户局次
    未定; eZmmR 2026-03-13 局最接近 (+0.257), 2026-09-14 局 +1.646 — 待用户
    确认局次/变体后定基线。已入 testset (ln13thYoru 暂记候选局)。
  - testset.json 已入册: LNPhase10th / ArtificialHumanSCP / xfpsb5thBeyond /
    ln13thYoru; 权威表 ours 列已回填。
- **B. 4KLNDan 差分诊断 (TB 精确 vs 4KL +0.196, 显示口径)**:
  - 分解: declared counts 公式 98.8299 → display 98.16 (quirk +0.67, 方向同
    TrueBlue ✓); 我们 accV2 98.356 → **display 口径真实判定残差 +0.196**。
  - 结构差分 (V2 hits 域):
    | | TwinBloom (±0.01) | 4KLNDan (+0.196) |
    |---|---|---|
    | LN 数 / 占比 | 3054 / 47% | 7037 / 81% |
    | LN span q25/med/q75 | 69/80/156 | 68/125/187 |
    | 尾→下根 gap q25/med/q75 | 60/80/159 | 65/68/125 |
    | heldThrough / 窗内早松 | 1304 / 1735 | 3088 / 3940 |
    | lateCatch | 40 (1.3%) | 16 (0.2%) |
  - 判读: 4KLNDan = **更长 LN + 更紧尾后间隙** (gap q75 125 < TB 159; 50 窗
    od6.5≈127 量级) — 尾窗与下根窗口重叠度更高, v2 尾判/notelock 绑定在该
    压力区的行为是 +0.196 的首选定位区; late-catch 可排除 (0.2%)。
  - v2 候选语义点排序: ① 尾 1.5x 窗在紧 gap 下的边界 (尾→下根 gap < 尾窗时
    我们是否过宽); ② notelock 绑定边界在 LN 链的 handling; ③ press-miss
    消费带。每项需 TB 对照行走 (TB 不劣化 + V2 全表 |dACC| 不劣化 >0.02)。
- gate: 判定零改动 (本轮纯入册+分析) — bz 97 / azimuth 132 / V2 指纹 /
  ende+promise 全维持 ✓。新锚点基线入 testset (baseline = 上表 dACC)。
- 数据: build/stepwise_anchor_*.json / stepwise_anchor_picks.json /
  stepwise_anchor_results.json / stepwise_anchor_yoru_0313.json /
  stepwise_xiii_4KLNDan.json / stepwise_display_TwinBloom.json。

## 2026-10-09 深夜 XV: V2 ①号候选 (尾 1.5x 紧 gap) 先验否证 — 计票恒等 + 4KLNDan 锚点局次存疑
- **①号 (尾 1.5x 窗在紧 gap 下的边界) 在实现前被否证**, 证据两条:
  1. **计票恒等**: 4KLNDan (testset 局) 我们 V2 计票 vs stable 落盘计票逐类
     Δ = {perfect 0, great +4, good −2, ok −1, meh +1, miss −2}, Σ|Δ| = 10/15739
     (0.06%)。落盘计票 = stable 自身判定记录 → **该图 v2 尾判/notelock/重握
     全路径在类计数层已经精确**。accuracy 只依赖类计数 → 任何判定路径收紧
     都会破坏 ±4 恒等, 而 display 差 −0.196 是类计数分歧 (0.016 accV2) 的
     12 倍 — **判定改动无法在保持落盘匹配的前提下收敛 display 差**。
  2. **锚点局次存疑**: 4KLNDan scores.db 共 8 局 (formulas 93.13..98.83),
     无一局 counts 公式 = 98.15/98.17; testset 局 (2026-09-24, save 98.8299)
     我们计票 ±4 吻合; 2026-08-26 局 (save 98.7483) 我们 accV1 98.74092
     (Δ0.007 ✓) / accV2 98.25046 — 用户锚点 98.15/98.17 与哪局都不精确对应
     (最近 08-26 局差 +0.08..+0.10)。V2 display−落盘 quirk 在 −0.41 (TrueBlue
     新局) 到 −0.67 (4KL testset) 间浮动, display 匹配无法唯一定局。
- **三图关系表** (display / 落盘 accV2 / 我们 accV2):
  | 图 | display | 落盘 accV2 | 我们 accV2 | 关系 |
  |---|---|---|---|---|
  | TwinBloom | 94.575 | 94.572 | 94.565 | 三者一致 (±0.01) |
  | TrueBlue 新局 | 98.79 | 99.187 | 98.795 | display = 我们 ≠ 落盘 |
  | 4KLNDan testset | 98.16 | 98.340 | 98.356 | 落盘 = 我们 ≠ display |
  → V2 的 display 不是落盘计票的固定公式 (TB 一致 / TrueBlue −0.40 /
  4KLNDan −0.18), 也不是我们计票的固定公式 (4KLNDan −0.18); display 与落盘
  之间存在 stable 内部显示计票层, 其规则需读取器级证据。
- **结论与建议**:
  1. ①号否证成立 — 不实现 (实现必然破坏 ±4 恒等且无法收敛 display 差)。
  2. ② notelock / ③ press-miss 同理被计票恒等覆盖 (任何 v2 判定路径收紧
     都破坏该图落盘匹配) — 同步否证, 除非未来发现逐单元绑定错位证据
     (类计数恒等不排除两两互换, 需读取器)。
  3. 4KLNDan 锚点需用户确认: 98.15/98.17 对应 8 局中的哪一局
     (2026-09-24 score=929383 / 2026-08-26 score=928142 / 其它), 或对该图
     出读取器真值 (决定性)。
  4. v2 轮在此之前的可做项 = TrueBlue 新局已入回归 (98.79 锚点已精确命中)。
- gate: 判定零改动 — bz 97 / azimuth 132 / V2 指纹 / ende+promise 全维持 ✓。
- 数据: build/stepwise_4kl_0826.json / stepwise_xiii_4KLNDan.json /
  stepwise_display_TwinBloom.json / stepwise_trueblue_new.json。

## 2026-10-09 深夜 XVI: 读取器真值八图入册 — 12th/13th 重挂精确命中 + V2 显示规则五图定稿
- **A. 真值入册 (test/sample/ 8 份)**: 身份核验全通过 (beatmapMd5/notes N/declared
  counts vs scores.db 局次)。关键确认: **「4K LNDan 12th」= testset 4KLNDan**
  (12th Dan - Yuugure (Marathon), _underjoy, beatmapId 1862875, 8702 notes,
  od8) — 锚点局 = **2023-04-23 (score 922757)**, reader declared 与该局 .osr
  counts ±2 吻合; 13th Yoru 锚点谱面 = **Extra Level 包的 Marathon 图**
  (13th Dan - Yoru (Marathon), _underjoy, beatmapId 1862876, **9963 notes**,
  md5 9068585a…) — 非 Maps 包 4 变体 (此前 eZmmR 候选局作废); 锚点局 =
  **2026-03-13** (score 904130, counts 精确匹配 reader declared; 用户所述
  03-23 为 03-13 之误)。
- **B. Twin Bloom N=1280 疑问裁决**: 误记 — reader declared
  [5512,3097,652,84,12,175] N=**9532** = 完整局 (heads+tails 单位), 与我们
  计票 ±18 内一致, **非 quit 局**, 无 counts 语义异常。TB 三方一致保持。
- **C. 重挂结果 (V2 显示规则五图验证)**:
  | 图 | display(回放) | save 公式 | 我们 accV2 | save−display | display−accV2 |
  |---|---|---|---|---|---|
  | TwinBloom | 94.575 | 95.1913 | 94.56464 | +0.616 | +0.010 |
  | TrueBlue 新局 | 98.79 | 99.2039 | 98.7946 | +0.414 | −0.005 |
  | xfpsb 5th Beyond | 96.66 | 97.2652 | 96.74817 | +0.605 | −0.088 |
  | **4KLNDan 12th (重挂)** | 98.17 | 98.6943 | **98.17119** | +0.524 | **−0.001** |
  | **13th Yoru (重挂)** | 97.21 | 97.7683 | **97.27064** | +0.558 | **+0.061** |
  - **12th 重挂精确命中 (Δ−0.001)**: 深夜 XV 的 4KLNDan +0.196 系局次错配
    (09-24 局 vs 锚点 23-04 局), **非判定误差** — 深夜 XV ①②③否证结论维持。
  - **V2 显示规则五图定稿**: display = 我们 accV2 (真实判定计票, 305 profile),
    |residual| ≤ 0.088 (5th Beyond 最差, 其锚点局次亦未经用户确认);
    save−display = **+0.41..+0.62** (五图同向) = 落盘把持有过尾的 LN 尾按
    300/彩档计而显示按真实尾判 (V2 quirk 定稿); V1: display = save 公式
    (azimuth/bz/10th/SCP 四图零例外)。
- **D. testset 重挂**: 4KLNDan → ticks 638178507594526492 (baseline −0.001);
  ln13thYoru → ticks 639090072000706382 (baseline −0.061)。gate: 判定零改动,
  bz 97 / azimuth 132 / V1 全表 / ende+promise 全维持 ✓。
- **E. V1 侧新事实 (深夜 XIV 增补)**: 10th Dan −0.272 / SCP −0.338 与
  bz/azimuth 同向 — V1 过判家族横跨 7K dan/4K LN dan/7K marathon 图型;
  V1 轮主攻方向不变 (绑定/短 LN 族)。
- 数据: test/sample/*.json (8 份读取器真值) / build/stepwise_remount_*.json /
  stepwise_anchor_*.json / stepwise_yoru13_ticks.txt。

## 2026-10-09 深夜 XVII: 绑定层格式考古 + Azimuth Dan/雪月夜鐘 入锚 + V1 绑定家族定性
- **A. 绑定层格式 (读取器使用说明.md + tracker.mjs 源码考古)**:
  - game.errorMatch ∈ {class / near / time / prestart / v1ln / none}:
    class = 误差推出类别与游戏判定一致 (最可信); near = 配上但类别不符;
    time = 类别不可验证仅按时间配对; prestart = 读取器接入前已判定;
    **v1ln = V1 面条一个事件吃头+尾两条误差**; none = 未配上。
  - game.eventTime = 该判定在内存里被观察到的时刻 (采样钟), 非物件时刻。
  - headError/tailError (V1 面条) = 绑定的头/尾两条误差;
    headErrorMatch/tailErrorMatch 同上枚举。
  - summary: weakBinds = 迟绑定计数 (songTime − err − point.time > 30ms,
    **reader 侧数据质量标记, 非 stable 规则**); errorResets = 误差流重置;
    cappedTails = 按文档规则封顶 50 的尾判定数; notelockV2 = V2 notelock 开关。
  - **关键限制**: events/bindings 的逐条分配 = 读取器启发式 (findPoint 最近
    未解析点), **只有误差值与到达顺序 (= 游戏判定顺序) 是 stable 真值**;
    绑定归属本身仍是启发式。
- **B. 新锚点入册**:
  | 图 | ticks | display | ours accV1 | dACC | 逐物件 |
  |---|---|---|---|---|---|
  | LN Dan Phase III [Azimuth Dan] (21a9c6b6, 10980n) | 639185160728867114 | 95.05/95.06 (= counts 95.0471 ✓) | 95.58591 | **−0.536 (V1 最差)** | **110 (1.00%)** |
  | 雪月夜鐘 [widest] (49b4bd1d, 7K OD8 BMS, 4929n 全 rice) | 639179025747860800 | 98.1 (= counts 98.0997 ✓) | 98.09968 | **0.000 ✓ 新完美锚点** | (后续) |
  - 局次身份: .osr counts 与 reader declared ±2 内匹配 (21a9c6b6: 5967/122/85/
    3985/644/177 vs 5967/123/83/3986/644/177)。
- **C. V1 miss←hit 家族绑定裁决 (Azimuth Dan 32 例短 LN stable=MISS)**:
  - stable 对这些 LN **无任何误差记录** (game.error 缺失, errorMatch=v1ln 空绑);
  - 但玩家的 press 误差**存在于误差流** (31/32 在 ±6 容差内找到 err@at 条目);
  - reader 启发式把它们散射到 PREV-tail 10 / PREV-head 11 / NEXT-head 3 /
    self 3 / 无分配 16 — **无统一归属规则可提取**;
  - 结合两图落盘计票 ±4/±3 互偿 (深夜 III「对称归属交换指纹」) → 定性收敛:
    **短 LN jack 链的归属交换发生在 stable 内部绑定层, 读取器/落盘均不可见
    内部规则**。该族保持开放 (需 stable 内部证据), 不再作为可修判定差异追逐。
- **D. Negai (v2 绑定级首样本)**: 逐单位 (头+尾分记) 612/5725 = 10.69% 差异,
  其中尾侧 perfect↔great 相邻档互换 374 条 = 判定窗 ±1~2ms 抖动带 (reader
  classAdjacent 定义, 非绑错); 头侧差异主要来自 v2 头判读为规则重建 (限制①)。
  cappedTails=30 vs 我们 meh 尾分歧 25 — 数量吻合支持封顶规则已正确实现。
  → v2 轮分析必须**只采 classExact 质量的 note** 或按相邻档容差过滤, 本轮仅入册。
- gate: 判定零改动 — bz 97 / azimuth 132 / V1 全表 / ende+promise 全维持 ✓。
- 数据: build/stepwise_azdan_{diffs,ours,shortmiss}.json /
  stepwise_xueyueyedong.json / test/sample/new/*.json。

## 2026-10-09 深夜 XVIII: 顺序剖面 (随机性定量封案) + classExact 检验 + piano 独立画像
- **① 顺序剖面 (Azimuth Dan rawErrors, 到达序 = stable 判定序)**:
  32 例短 LN MISS 中 16 例在我们 press 的 ±300ms 观察窗内找到 err 匹配条目,
  16 例无可辨条目 (观察窗/轮询粒度限制)。16 例匹配的**结构模式**
  (前邻部件→后邻部件): (tail,tail) 6 / (head,head) 6 / (tail,head) 3 /
  (head,tail) 1 — 与基底率一致 (LN 占比 64% → (tail,tail) 期望 41%,
  实测 37.5%; 无低基数绑定规则信号, 16 例全相异)。加 16/32 不可观测,
  **定量结论: 顺序层无绑定规则信号, 低于随机可辨阈** → miss←hit 家族
  彻底封案 (stable 内部绑定不可观测, 非我们可实现面)。
- **② classExact 检验 (Azimuth Dan)**: errorMatch=class 的 51 例
  **0 差异 (0.00%)** — 绑定可直接观测处我们的判定 100% 正确;
  110 残差全部落在绑定不可观测单元。good←great 在该子集 = 0
  (#813 类中断案不可观测, 维持悬案)。
- **③ piano 独立画像** (从未动过): **全 rice 图** — 3718 objects 中仅
  **39 LN (1.0%)**, lateCatch 0 / break 0; 尾 tiers great 21 / perfect 15 /
  good 3。+1.036 缺口**不可能来自 LN 判定** (39 LN 全体降三档也只值 0.27pp),
  落点在 **rice 侧**: declared vs ours 逐类:
  | class | declared | ours | Δ |
  |---|---|---|---|
  | perfect | 2264 | 2255 | −9 |
  | great | 1081 | 1047 | **−34** |
  | good | 182 | 175 | −7 |
  | ok | 96 | 112 | **+16** |
  | meh | 14 | 37 | **+23** |
  | miss | 81 | 92 | **+11** |
  净权重 −11550 = −1.036pp ✓。→ stable 在 ~34-50 根 rice 上判了 great/300
  而我们判 meh/miss/ok — rice 归属/判定差, **与 LN jack 过判家族不同源**
  (方向相反: piano 我们偏低)。建议: piano 读取器采集 (同 bz 流程) 入逐物件。
- gate: 判定零改动 — bz 97 / azimuth 132 / V1 全表 / ende+promise 全维持 ✓。
- 数据: build/stepwise_xviii_piano.json / stepwise_azdan_*.json。

## 2026-10-10 深夜 XIX: stable 二进制静态逆向开箱 — 家族③ 机制级破案 (显示 acc = 实时计票公式) + 绑定层动态观测方案定稿

> 执行者: stable-re-replay (绑定层逆向子任务)。全程只读 osu!.exe 与社区资料, 判定零改动
> (gate: bz 97 / azimuth 132 / V2 指纹 / ende+promise 维持 — 本轮无引擎改动)。
> 详细记录与工具链: docs/stable-replay-internals.md + build/stable-re/*.mjs (零依赖 IL 工具链)。

- **A. 二进制定性 [实测]**: osu!.exe = ConfuserEx 系混淆的 32 位 .NET 程序集 (v4.0.30319,
  2907 类型 / 21398 方法)。名字全部单向改名 (`#=z/#=q`), 字符串常量运行时解密 (#US 仅 30 条
  包装串 + 明文标记 `e26219084b984214e5b0e82add2e00d5`), <Module>::.cctor 装两个
  AppDomain 钩子 (内嵌程序集按需 Assembly.Load 加载器)。**IL 与元数据结构完好** —
  自写解析器 (PE→CLR→#~ 表→IL 反汇编) 已跑通; coded-index 位宽须按「行数<<tagBits」判定
  (MethodDef 21398 行使多个 coded index 升 4 字节, 第一轮对齐失败的根因)。
- **B. 【家族③ 破案】显示 acc = 实时计票公式 [实测, IL 级]**: 定位 accuracy getter (方法 m6626):
  `(50·c50 + 100·c100 + 200·cKatu + 300·c300 + 305·cGeki) / (N×305)`, 以 `mods & 0x20000000`
  (ScoreV2) 分支; V1 分支 = `300·(c300+cGeki) / (N×300)`, N=0 时返回 1.0。
  字段语义与内存侧 score+0x88..0x92 的 16 位计数互相印证 (结算 UI 对其 box UInt16)。
  **推论 [推断]**: 显示层没有自己的计票 — 深夜 XV「内部显示计票层」假设不成立;
  display 就是实时计票的 counts 公式, 落盘≠显示的差异只能发生在**写盘/记录路径**
  (与深夜 XVI 五图经验规则 display=真实判定计票、save−display=+0.41..+0.62 同向完全相容)。
  计数短整型全程序集零直接写入 (stfld/ldflda 0 命中) → t2241 (Score 类, 字段 f10585..10640,
  IL 交叉实锤) 更可能是结果/存档副本, 实时计数写在另一个类 (live score, 类未定)。
  **→ 决定性实验 (建议下轮读取器排期, 一次 play 完成)**: play 结束瞬间读 score+0x88..0x92
  实时计数 vs 该局 .osr counts vs display 公式值, 三方对比 — 不等则写盘重计票实锤且差异
  直接可分类; 相等则显示另有来源 (与 IL 矛盾, 概率低)。
- **C. 附带 IL 证据**: m16554/m16555 = Score→字符串格式化器 (18 字段 Format + MD5 链,
  字段序含 counts/mods/UTC 时间) — scores.db/.osr 头部字段序的 IL 旁证, 供 declared 审计引用 [实测 (IL)→语义推断]。
- **D. 静态路线剩余卡点 [实测]**: 判定枚举不存在 (分值为 IL 字面量); `ldc.i4 320` 命中全是
  UI 坐标/输入掩码; V2 max 窗常量 24.9f 零命中 (公式线性形式不同); TypeSpec.Flags 被混淆器
  写垃圾值导致泛型 memberref (List<int>::Add) 类解析卡住 → press 绑定主路径的 IL 定位
  暂时受阻。备选: ① 反射调用其常量解密器还原字符串锚点 (纯托管代码, 可在 pwsh 试);
  ② hitErrors List.Add 经运行时对象图反查 (动态路线)。
- **E. 绑定层动态观测方案定稿 (docs/stable-replay-internals.md §4)**: 目标 = 把「绑定顺序」
  变成可采集数据, 裁决家族①/②。观测对象: ①列对象 (obj+0x94, 7 个) — colwatch 高频 diff
  找「锁定 note 指针」字段; ②物件 +0xE0/+0xE4 (值域 0..4 渐进翻转, 从未深究) —
  疑绑定/判定状态机 [猜测→待测]; ③§9.3 的 12 字节旁路数组第 2/3 指针 (obj+0x2xx 堆);
  ④Score+0x24 浮点对 List (元数≈判定数) — 疑显示计票流水。objdiff 设计: 只对当前 ±2s 窗口内
  物件做 0x00..0x100 全字段 diff, 捕捉 press 时间戳类新字段。判读: bz col6 jack 族回放时
  锁定指针切换时刻 vs .osr 帧 → stable 归属真值表, 与引擎 findTarget 逐案 diff。
- **F. 教训入册**: 元数据 coded-index 位宽 / TypeDef 行内 2 字节列表这类「表对齐」问题必须用
  内容自检 (名字堆界、单调性、blob 界) 逐表验证, 不能全信推导; 混淆器可能违反 ECMA 应然值
  (TypeSpec.Flags≠0)。工具链已固化 build/stable-re/ (lib/meta/il.mjs + 各探针脚本),
  只读 osu!.exe, 零依赖, 后续轮可直接复用。
- **E2. 追补 (同夜): 字符串解密已打通 [实测]** — m16032/m16033 = ConfuserEx 常量解密器
  (manifest 资源 f99455430df64b4387099de160f2eb9a=16B AES key / 4e422e1e…=223KB 常量堆);
  其反篡改 (StackFrame DeclaringType vs RuntimeMethodHandle) 使直接反射调用返回占位 "X0X";
  **绕过 = 反射调用 osu! 自己的静态无参字符串 getter** (内部合法调用解密器) —
  已验证: 91 个 getter → 100KB 解密文本 / **1166 个本地化键**, 含 mania 锚点
  (ConfigMania_OsuManiaSpeedSet / Options_OsuManiaLayout / AIComposeMania_* 等)。
  完整配方与数据: docs/stable-replay-internals.md §6 + build/stable-re/
  (getters.mjs / invoke_getters.ps1 / decrypted_getters.txt / string_ids.json: 6340 个待解 id)。
  第 2 轮: 以 ConfigMania 键定位 mania 配置/判定类邻域, 完成 press 绑定/notelock 主路径 IL 定位。

## 2026-10-10 深夜 XX: 绑定层探针落地 (bindprobe) + 字符串解密边界确认 — 等待一次 play 采集

> 执行者: stable-re-replay (第 2 轮)。判定零改动 (gate 维持); osu! 只读。

- **A. 观测探针落地 [实测]**: `osu-mania-live-reader/tools/bindprobe.cs/.exe` 已编译可用 —
  cols (列对象 dump) / side (§9.3 旁路数组反查) / watchcols (列高频 diff) /
  watchobj (未判定物件 0x100 高频 diff) / scorewatch (score+0x24/0x34/0x44 增量),
  JSONL 输出与 notesfast 同风格, **watch 类自动等待 play** (菜单启动安全)。
  使用协议: docs/stable-bindprobe-protocol.md。
  **需要配合**: 下次有人播放 Blue Zenith 0.9x (首选, 家族①锚点局) 或 Azimuth Dan 回放时,
  先起 watchobj + watchcols 两条采集 → 直接产出 stable 归属真值表 (家族①/② 裁决数据)。
- **B. 字符串全表解密 — 确认为高成本, 搁置 [实测]**: m16032 全部 9444 个调用点均为常量 id
  (无变量包装); provider 为流式按条目解密 (anti-dump), 完整明文从不整体存在;
  直接调用受 StackFrame 反篡改拦截 (占位 X0X)。全表需完整移植解密状态机, 性价比低于探针。
  部分战果保留: 91 个合法 getter → 1166 个本地化键 (mania UI 锚点在其中, 但全部来自同一张
  localisation 表, 无法区分 gameplay 类)。
- **C. FieldRVA 静态数据扫描 [实测]**: 只有 AES S-box (资源解密用) 与 UI 常量表,
  **无判定窗/分值表** → 判定窗确为运行时线性计算 (与 mania_number 公式一致), 无隐藏查表。
- **D. 机制层新增证据**: t2241=Score 类的计数短整型全程序集零直接写入 (深夜 XIX) +
  m16554/m16555 格式化器字段序 → scores.db/提交链路的 IL 依据完备;
  展示层公式 (m6626) 与写盘路径分离的定性维持。
- **E. 家族①/② 裁决路径 (不变, 等数据)**: watchobj 若捕捉到「写入值 ≈ press 谱面时刻」的字段
  → 绑定记录; watchcols 指针切换 → notelock 锁定目标; 二者对照 .osr 帧 → 归属真值表 →
  判别式提案交 v1-playback-sim 行走 (与深夜 XIII 备料表特征逐案对照)。
- 数据: build/stable-re/ (工具链+字符串产物) / osu-mania-live-reader/tools/bindprobe.* (探针) /
  docs/stable-bindprobe-protocol.md (采集协议) / docs/stable-replay-internals.md §7。

### 深夜 XX 追补: .osr LZMA 解码正确性独立验证 — 全项 PASS

> 用户指令「先验证解码正确性」。执行者: stable-re-replay。方法: 引擎源码外壳 (MSVC 编译
> osr_reader + replay_decoder + replay_frames + LzmaDec 原样编译) vs 独立 Python 实现
> (lzma.FORMAT_ALONE + 独立帧解析器) 交叉比对。工具: build/stable-re/verify_osr.{cpp,exe} /
> verify_osr_indep.py / diff_frames.py。只读验证, 引擎未动。

**样本** (均为 testset 锚点局, 按 .osr 头部 beatmapMd5 + ticks 双重核验):
azimuth c8b6075b 2026-10-09 局 (121,320 B) + azimuth 23-04 局 (118,400 B) +
bz 6ef7c488 0.9x 局 (47,120 B) + TrueBlue 新局 a5af1629 mods=536870912 (16,550 B)。

| 项 | 结果 |
|---|---|
| ① LZMA 解压字节级 | **PASS** — 三图 + 23-04 局共 4 份, Python FORMAT_ALONE 解压与引擎 LzmaDec 输出**逐字节一致** (SHA-256 全同, 长度全同: 1,407,384 / 521,493 / 178,661 / 1,364,352 B) |
| ② 帧级比对 | **PASS** — 127,661 + 82,386 = 210,047 帧 (timeMs/rawKeys/down/up/isSeed 五元组) **0 mismatch** |
| ③ 帧结构 | **PASS (含勘误)** — stable .osr 解压体是**文本协议** ("w\|x\|y" 以逗号连接), 不是二进制 [u8 keys][uleb128] (任务前提勘误; ULEB128 只出现在 osu-String 长度前缀, 独立解析器已实现)。首记录 = RNG seed 帧 "-12345\|0\|0\|seed" (引擎归一为 t=0 零输入帧), 尾部 "0\|0\|0" 终止标记被剥离 |
| ④ 时间语义 | **PASS** — 首条非 seed 记录的 w 为**绝对时刻** (=0), 其后 w 为增量; 各图开头存在**负增量引前段** (azimuth: 0→−1→−1758; bz −333; TB −797), 引擎如实保留 (negativeDeltaCount 3/2/2, 已有计数器); azimuth 帧尾 782104→782073 有一次 **−31ms 回跳** (85349/85374 帧, 中段无其它回跳) |
| ⑤ 采样率 | 模态帧间隔 16/17ms (≈60Hz 输入轮询); 最大间隔 azimuth 68 / bz 62 / TB 33 ms — 无 >70ms 的空档 |
| ⑥ 按键迁移 | downs == ups 严格相等 (18590/18590, 7296/7296, 2141/2141) — 流内无悬挂按键 |
| ⑦ 尾部字段 | 压缩流后剩余 8 字节 = onlineId u64 (azimuth=2407978001 已上传; bz/TB=0 本地), 无其它残留; TargetPractice 位未置 → 无 f64 尾巴。头部 timestamp(ticks) 与文件名尾的关系 = 减常数 504,911,232,000,000,000 (16,000 天) |
| ⑧ 抽验 | bz col6 press@**21307** = 帧 idx 1868 (down=0x44 含 col6) 与深夜 IV 锚点逐位一致; azimuth 23-04 局全量比对 0 mismatch |

**结论: 引擎 LZMA 解码与帧解析对全部 4 份样本逐字节/逐帧正确 (PASS)。时序分析地基无可疑偏差;
负增量引前段与 azimuth 一次 −31ms 回跳为 stable 本体的流特性, 引擎已如实计数, 不构成解码问题。**

### 深夜 XX 追补 2: Mania-Replay-Master 参考算法锚点试算与对比

> 用户指令: 用 Mania-Visualization-Project/Mania-Replay-Master 的算法试算锚点谱面 acc 并对比。
> 执行: 忠实移植其 Kotlin 判定 (BaseJudger/OsuManiaJudger/OsuReplayReader, build/stable-re/mrm_port.py
> + mrm/*.kt 存档), 输入 = 读取器已核验的谱面 notes (test/sample/*.json, notes 与游戏内存逐物件对账过)
> + 本次已验证的引擎帧 (engine_*.frames.csv)。omScoreV2Adjust = true (其默认值)。

**结果** (MRM = Mania-Replay-Master 移植):

| 图 (模式) | MRM acc | stable 声称 | MRM−stable | 我们 ship | MRM−我们 |
|---|---|---|---|---|---|
| azimuth (v1, od5) | **94.615** | counts 94.3705 | +0.245 | 94.6105 | **+0.005** |
| bz (v1, od5) | **90.763** | counts 89.9541 | +0.809 | 90.6351 | +0.128 |
| TrueBlue 新局 (v2, od6) | **98.820** | display 98.79 / saved 99.2039 | +0.030 / −0.384 | 98.7946 | +0.025 |

MRM V1 计票 (vs stable 落盘): azimuth {MAX 8307/8308, 300 8263/8248 (+15), 200 1090/1043 (+47),
100 273/279, 50 163/153 (+10), miss 310/375 (**−65**)} — **MAX 桶 1 个不差**, 差异集中在
200/300 合并带与 miss 少判 65; bz 同构 (miss −75)。

**判读**:
1. **MRM ≈ 我们 ship (Δ ≤ 0.13), 且继承同一家族的 stable 高估** (+0.25/+0.81) — 与深夜 IV/VIII 的
   定性一致: naive 参考实现 (OsuManiaJudger 系) 的 LN 合并/绑定语义本身不匹配 stable,
   我们历次行走 (fix1/fix2/A2/深断重握) 正是把这个差距逐步吃掉; MRM 的数字就是「未行走时的我们」。
2. **MRM V2 acc 独立确认了深夜 XIX 的显示层结论**: 其 V2 acc = 真实头尾判定的 305 权重均值
   (= 我们的 accuracyV2 口径) → 落在 stable **display** 98.79 (+0.03) 而非 saved 99.20 (−0.38)
   → 第三方实现独立复现「V2 显示 = 真实判定计票, saved ≠ display」。
3. MRM V1 的 MAX 窗固定 16ms (不随 OD) 与 stable 一致; 其窗口 V1 = [16, 34+3(10−od), 67+, 97+,
   121+, 158+] 截断取整, V2 = mapDifficultyRange 截断 — 均与团队已知公式同源。
4. 诚实边界: osu-replay.jar 的 seed 帧处理按假设移植 (跳过); bz 读取器样本与 .osr 差 1-2 个
   计数 (相邻局), notes 为同一谱面不影响判定比对。

## 2026-10-09 深夜 XXI: bindprobe 内存探针 — 0xA4=press 绑定字段实锤, 灰按规则 57/59 验证, 家族①②封案
- **采集元数据**: bind_watchobj.jsonl 5611 events / bind_watchcols.jsonl 15680
  events; objects=7270 ✓ = Blue Zenith; 采集跨度 282875ms ≈ 曲长 284623 ✓
  (全程覆盖); 轮询 10ms; **探针缓冲 512 → 仅监视 note 0-511** (≈前 31s, 含
  col6 jack 族); hdr t0/watch 512/end ticks=29992 (探针自计数, 非 .osr ticks)。
- **字段语义 (508 根有 press 的 watched note 反推)**:
  | offset | 写入值 ≈ | 命中 | 语义 |
  |---|---|---|---|
  | **0xA4** | **press 时刻** | **506/508** | **press 绑定字段 (head 绑哪次按)** |
  | 0xA8 | 松键时刻 | 352 | 松键/尾记录 |
  | 0xD8 | press 时刻 (346/508) / 其它 | 870 | 初始化+复写 (init 写 start) |
  | 0x5C / 0x84 | 每根一次 (512) | — | 初始化字段 |
- **家族① (miss←hit, 灰按绑定) 裁决**: 灰按案例 (LN 头前 (−miss,−50] 有
  press-release 循环) 共 **59 例**, 我们 (fix2 灰按忽略) 与 stable 判定一致
  **57 例 (96.6%)**。3 例外:
  - **#414**: 灰按 −170 → stable 0xA4 = **−170 (灰按写入!)** → LN 被灰按绑定
    锁定, 窗内第二按 (+13) 未写 0xA4 → 单元 MISS; 我们灰按忽略 → 窗内按命中
    → great。
  - **#486**: 同构 (灰按 −173 绑定 → MISS; 窗内 +30 未写)。
  - **#344**: 0xA4 双写 **[−168 (灰按), +2 (窗内)]** → 最后按覆盖 → 结算,
    中断封顶 200 → stable good; 我们 great (无 cap)。
  - **判别式不在可用特征内**: 灰按深度 (#414 −170 / #486 −173 vs 57 例含
    −173×3 / −176×2)、窗内按偏移 (#414 −13 vs #213 −13 同值却一致)、前循环
    间距均无分离。→ 灰按是否写入 0xA4 = stable 内部行为, **帧域不可观测**,
    家族①②封案 (57/59 = 96.6% 规则一致率已是可实现上限)。
- **对既有规则的再确认**: 57 例一致 = fix2 灰按忽略规则在探针层验证正确
  (灰按不写 0xA4 = stable 同样忽略); #344 的双写证明「最后按覆盖」= 我们的
  处理一致; 唯 stable 独有行为 = 灰按偶发绑定锁死 (3.4%), 无法复现。
- **watchcols (notelock)**: 15676 条 0x38 列状态 diff (本采集 = bz V1,
  notelock 不适用); v2 notelock 验证需对 v2 图 (TrueBlue/4KLNDan/Negai)
  重跑采集 — 本轮仅入册格式。
- gate: 判定零改动 — bz 97 / azimuth 132 / V1 全表 / ende+promise 全维持 ✓。
- 数据: build/stepwise_bindobj_notes.json (512 note 字段写表) /
  stepwise_greypress_cases.json (59 例灰按案例表)。

### 深夜 XX 追补 3: osu!gameplay.dll 定性 — 加密的真实游戏程序集 (绑定 IL 的新目的地)

> 用户指令「重点解读 osu!gameplay.dll」+ lead 核验文件存在 (31,854,304 B, mtime 2021-06-23)。
> 初稿「单体构建/无 DLL」结论**撤销** (搜索被文件名 "!" 与隐藏属性坑了 — 关键存在性判断必须
> -Force + Test-Path 复核)。

- **定性 [实测]**: PE32 托管, CLR v4, 单 .text 节 31.8MB, **全文件熵 7.72-7.98 = 整体加密**;
  可见元数据仅 704B 表流 (TypeDef=2 / MethodDef=4 / ManifestResource=1) = **ResourcesStore 桩**
  ("Copyright 2013", v1.0.0.0, 强类型资源访问器)。31.8MB 加密负载 = 真实游戏程序集
  (推测高置信: 体积量级 + 桩无业务逻辑 + 运行时按需加载行为)。
- **运行时状态 [实测]**: 当前 osu! 进程 (空壳, WS 9MB, 19 regions / 6MB 可读) 未加载解密负载 —
  全内存 MZ 扫描仅 2 镜像 (osu!.exe + 系统原生 DLL), BSJB 0 命中 → **进入 play 时才解密加载**。
- **路线影响 [推断→高置信]**: 家族①②的绑定/notelock/LN 状态机 IL **不在 osu!.exe 里**
  (exe 的 2907 类型 = 壳 + 辅助代码: m6626 accuracy、计分格式化器等) — 必须在解密后的
  gameplay 程序集上逆向。若 2013 年代的保护壳意味着 payload 未混淆, 类型名可能直接可读 (待验证)。
- **工具就绪 [实测]**: memprobe 新增 scanbsjb / scanmz / dumpbin; 采集协议统一
  (docs/stable-bindprobe-protocol.md) — 下次 play 并行三路: bindprobe (绑定真值) +
  gameplay 内存导出 (IL 逆向素材) + 读取器 (判定真值), 一次 play 全拿下。
- **此前静态分析定位重估**: osu!.exe 内的 m6626 (accuracy)、m16554/16555 (计分格式化器)、
  m4510 (写盘疑点) 为壳内真实代码 — 家族③ (显示/写盘) 的 IL 结论**不受影响**;
  绑定相关结论维持「需 stable 内部证据」的定性。

## 2026-10-09 深夜 XXII: bindprobe 二采全量裁决 — 绑定一致 510/512 (99.6%), 一致处判定零错误, 家族①②定量封案
- **二采元数据**: bind_watchobj.jsonl 10554 events (820KB), objects=7270 ✓,
  gameplay 基址 0x3ED22884, watch 512 (note 0-511, 探针缓冲限制不变),
  t 1359-317672; 无 end 行 (采集窗手动关闭)。字段集更全 (0x10-0x9C 新增)。
- **全量归属真值表** (512 watched 单元, 我们 findTarget 绑定 vs stable 0xA4):
  | 类 | 定义 | n | 差异 |
  |---|---|---|---|
  | A: same-bind | 0xA4 末值 = 我们绑定 press (±8ms) | **377** | **0** |
  | E: multi-write | 0xA4 多次写入 (重绑/覆盖) | **128** | **0** |
  | D: no-a4 | 无 0xA4 写入 | 4 | 0 |
  | B: stable-earlier | stable 绑更早的灰按, 我们绑窗内按 | 2 | **2 (miss←great)** |
  | E + DIFF | #344 (同最终绑定, stable 封顶 good vs 我们 great) | 1 | **1** |
  - **绑定一致 510/512 = 99.6%; 一致处判定 510/510 = 100% 零错误** —
    V1 合并/tier/封顶公式在绑定正确时**无任何公式误差** (4680 旧口径
    mismatch 全为绑定归属, 与深夜 III 对称交换定性闭环)。
- **家族① (灰按锁死, #414/#486) 复现确认**: 二采 0xA4 = [24896]/[27310]
  = 灰按值 (一采相同) — stable 把头前 (−miss,−50] 的灰按绑定为该 LN 的
  press (0xA4 写入), LN 随即锁死超时 MISS; 我们灰按忽略 → 窗内按命中 →
  great。锁死发生率 = 2/61 灰按案例 (3.3%); 判别式在灰按深度/窗内偏移/
  前循环间距上均无分离 (深夜 XXI + 本轮二采双重确认)。
- **家族② (中断封顶, #344) 复现确认**: 0xA4 = [灰按 −168, 窗内 +2] 双写,
  最终绑定 = 窗内按 (= 我们), stable 结算 good = 中断封顶 200; 我们 great。
  封顶触发 = 灰按绑定发生过 (stable 内部), 帧域不可观测 (Walk B 全局 cap
  过判已证)。
- **可实现上限精确数字**: watch 范围内我们可达残差下限 = 当前 3 条中的
  **1 条 (#344 类, 若中断封判别式可解)**; 灰按锁死 2 条 = stable 内部
  (不可实现)。全图外推不安全 (探针仅覆盖 7%), 按 3.3% 锁死率与 LN 密度
  估算全图 binding 层残差 ~20-40 条量级 — 同属不可实现面。
- **v2 notelock (watchcols)**: 本采集仍为 bz V1 (notelock 不适用);
  v2 验证需对 v2 图重跑 (采集格式已就绪)。
- gate: 判定零改动 — bz 97 / azimuth 132 / V1 全表 / ende+promise 全维持 ✓。
- 数据: build/stepwise_bindprobe2_{rows,classified}.json。

## 2026-10-09 深夜 XXIII: 双写封顶实现 — bz 95 / azimuth 114 双双新低, #813 悬案解决
- **实现 (用户指令「双写才封顶」精细化)**: LN 头早灰按 (−miss..−50] 置
  greyTouched (不改绑定不消费, fix2b 忽略行为保持); 该 note 后续窗内按命中 →
  结算时 greyTouched 且 v1Judgement ∈ {Perfect, Great} → 降 Good (中断封顶);
  未命中照旧 Miss。flag 挂 note; MRA_V1_GREYCAP=0 可关。
  勘误: 深夜 VIII Walk B 的实现已是「命中才结算封顶」非「灰按立即全局封顶」,
  当时失败源于基准差 (A 期) 与 penguin 0.054; 本轮在深夜 XII 基准 (深断重握后)
  重测, 结果反转。
- **行走结果 (双写封顶 vs 深夜 XII)**:
  | 图 | XII dACC | 深夜 XXIII dACC | 逐物件 |
  |---|---|---|---|
  | bz | −0.681 | **−0.511 (改善 0.17)** | **97 → 95** |
  | azimuth | −0.240 | **−0.148 (改善 0.092)** | **132 → 114** |
  | penguin | −0.150 | −0.082 (改善) | — |
  | newspapers | +0.008 | +0.025 (+0.017, ≤0.02 ✓) | — |
  | ice | −0.017 | −0.011 (改善) | — |
  | ende/promise/piano/FUBUKI/MachineGun | 不变 | 不变 | — |
- **bz 逐物证明细**: 修复 7 = #344 (悬案主目标) + **#813 (深夜 XI 悬案解决!)**
  + #1786 / #4006 / #5875 / #5882 / #6588 (全部 stable=good, 我们 great/perfect
  过判 → cap 修正); 新增 5 = #1394 (著名的「中断过→MAX 无封顶」反例) +
  #2770 / #4635 / #5686 (同族 stable=perfect) + #4690 (stable=great) —
  stable 对部分灰按中断不封顶 (#1394 家族), 网络净 +2 修复。
- **azimuth −18 条改善** = 同族 cap 修正 (azimuth 的灰按中断封顶率高于 bz)。
- **gate**: bz 修复 7 > 新增 5 ✓ 保留; azimuth 132 ✓ 只减不增; 非 vibro
  |dACC| 劣化最大 +0.017 (newspapers, ≤0.02) ✓; ende/promise 0 ✓; 单元测试
  ALL PASSED ✓; V2 指纹 14/15 逐位不变 — 4KLNDan "DIFFERS" = 深夜 XVI testset
  重挂效应 (现测锚点 23-04 局: declared/accV2 全变), **非代码回归**:
  重挂局双跑哈希一致 (b43b784b… ✓ 决定性验证), V2 路径本轮未触及。
- **残余**: #1394 家族 (stable 部分灰按中断不封顶, 判别式未知, ~5 条);
  其余 90 条 (bz) / 114 (az) = 绑定层不可观测族 (深夜 XVII/XVIII/XI 定性)。
- 回滚点: build/hit_matcher.cpp.pre-walkF; env MRA_V1_GREYCAP=0。
- 数据: build/stepwise_walkF_{bz,az}_diffs.json / stepwise_walkF_summary.json。

## 2026-10-09 深夜 XXIV: #1394 族帧域核验 + 探针分段采集方案 (含 stable 逆向预备)
- **① #1394 族 (误封顶 5 例) 帧域核验**:
  - #1394 (c4, span 84): 灰按 **−171 (松 −78)** 存在 ✓ → 我们 greyTouched 正确
    触发, stable = perfect (不封顶) → **误封顶实锤 (帧域确认)**
  - #2770 (c1, span 84): 灰按 **−172 (松 −86)** 存在 ✓ → 同上误封顶实锤
  - #4635 / #4690 / #5686: 帧域无灰按 (窗口 [−176,−136) 空) — 但帧 fixture
    截断于 185s (三例位于 188s/189s/226s) → **无法用现有 fixture 核验**;
    我们引擎 cap 已触发说明回放中存在灰按 (engine 侧可见), 需探针裁决。
  - 与 #344 (灰按 −168 双写 → 封顶) 的判别假设: 灰按深度 −168 写 vs
    −171/−172 不写 — **与历史 −171/−168 分界吻合** (深夜 IV), 但样本仅 1+2,
    且 #414 灰按 −170 写了 0xA4 却是锁死族 (非双写) — 需探针直读定界。
- **② 探针分段采集方案** (残差索引 = walkF bz 95 / azimuth 114):
  - 朴素全覆盖: bz **10 窗** + azimuth **21 窗** = 31 次播放
    (bz: 414/1209/1792/2770/3358/3950/4635/5463/6177/6737 起, 各 +511;
     az: 613/1329/1984/3135/3674/4829/5840/6411/6966/7629/8262/8835/9407/
     10387/11768/13157/14098/15438/16624/17169/17790 起, 各 +511)
  - **优先最小批 (判别式关键, 4 次播放)**: #1394 族 + 双写验证:
    bz watch [1394..1905] (含 #1394) / [2418..2929] (含 #2770) /
    [4635..5146] (含 #4635+#4690) / [5686..6197] (含 #5686)
  - **工程建议 (最优解)**: 探针 watch 缓冲 512 → 7270 (或加 range 参数),
    **一次播放即全覆盖**, 消除 31 次播放需求。
- **③ stable 逆向线预备** (若探针轮仍无判别式): memprobe/gameplay dump
  采集清单草案: ① PID 直连 (任务管理器→详细信息→osu!.exe PID, 十进制传入)
  ② 播放 bz 回放一次 (与 bindprobe 同局) ③ 三路并行: bindprobe (watchobj/
  watchcols) + memprobe (scanbsjb/scanmz/dumpbin) + 读取器 ④ dump 落盘后
  离线解析 0xA4 写入方法的 IL。细节待与 stable-re-replay 对齐后定稿。
- gate: 判定零改动 — bz 95 / azimuth 114 / V1 全表 / ende+promise 全维持 ✓。
- 数据: build/stepwise_probe_plan.json / stepwise_walkF_*_diffs.json。
- **工具落地 (lead 批准后实施)**: bindprobe.cs 已改 — watchobj 增 range 参数
  (`watchobj [ms] [dur] [起始索引] [数量]`, 默认 0/512 完全向后兼容) +
  watchobjf <索引文件> 模式; hdr 带 range/indicesFile 溯源; >2048 索引自动
  放宽 ms≥20 (轮询成本保护); csc 重编译 ✓ (exe 备份 bindprobe.exe.xxii-backup);
  冒烟测试 (usage/坏 pid 三形态) 全过。协议文档已更新 (stable-bindprobe-protocol.md)。
  回归验证脚本: build/stepwise_bindverify.mjs (新采集 vs 深夜 XXII 真值表
  note 0-511 段 0xA4 末值逐 note 比对)。**4 窗采集清单已发**:
  test/sample/bind_capture_checklist.md (用户手播 4 次)。
  待数据回来 → #1394 族 0xA4 写入裁决 + 灰按写深分界定界。

## 深夜 XXV: 分段探针裁决 — #1394 族机制定案, cap 保留 (净优)
- 数据: output/bind_watchobj_w{1394,2418,4635,5686}.jsonl (4 次分段播放, range 模式,
  #1394 族 5 例全覆盖; 用户手动播放)。
- **裁决**: 5 例 0xA4 全部单写 (= 窗内真按 −6/−6/−10/+3/−11), 灰按 (−171/−172) 未写;
  对照 #344 双写 (灰按 −168 + 窗内 +2 均写) → 「cap 条件 = 0xA4 实际双写」确认。
  我们现行 cap 以 greyTouched (仅碰过) 触发 → 误伤 5 例 (stable perfect/great 被降 good)。
- **净权衡**: 撤 cap 回退 bz +2 / azimuth +18; 保留 cap 误伤 bz 5 → **保留 cap (净优维持)**,
  5 例为 cap 的不可约成本 (判别式 = 0xA4 写入次数, 帧域不可见)。
- **实时观测到 stable 结算改判**: #4690 完成时 0x80: 320→300 (MAX→300 修订写),
  完成模式 = 0x84=1 + 0xA8 松键 + 0xE4=1 + 0xD8 清零, 五例同构。
- 分段采集协议验证通过 (range 模式 + PID 直连 + 目标覆盖 5/5)。
- 数据: bind_watchobj_w*.jsonl; 本节判定零改动 (XXIII ship 维持)。

## 深夜 XXVI: gameplay.dll 运行时大清点 — 载荷从不解密入内存, 绑定 IL 定位回 osu!.exe (2026-10-15)
- **工具修复 [实测]**: memprobe scanbsjb 三处解析 bug (XXV 前一直全坏): ① 流计数字段
  读到 Flags 恒 0 (offset 差 align4(verLen)+2) → 全部真根被过滤; ② 名字跳过越界守卫写错
  (`p - i < b.Length` 应 `p < b.Length`) → 畸形候选崩溃 (0xE0434352); ③ NUL 终止符未跳
  即对齐 → NUL 落 4 边界时游标走飞, 假 metaSize 5.46MB。三修后菜单态 43 真根/1.6s/零崩溃。
  重编译 csc /platform:x86; 备份 memprobe.exe.pre-xxvi-backup。
- **进程内全量根清点 [实测]** (43 根 = 修后全扫, 逐一 #Strings 验明): 框架全家
  (mscorlib/System/System.Core/System.Xml/System.Web/WinForms/Drawing/Data/Numerics/
  Configuration/Serialization/Accessibility/Management/Remoting/SqlXml) + OpenTK (meta 3.10MB,
  进程内最大) + **osu!.exe 本体 (0x64BEE4, meta 1.80MB, ≈2907 类型)** +
  **gameplay.dll 桩本体 (0x2F162094, metaSize=2276, ResourcesStore — 与盘上解析一致)** +
  zx_ 资源卫星 + RefEmit 动态件 (Anonymously Hosted DynamicMethods / dynamicBASSNET)。
  **无任何 ≥31.8MB 量级候选程序集**。
- **play 中三仪器实测 [实测]**: 两整局 bz 回放播放, 15s 节拍 (play_timeline.jsonl 全程):
  scanbsjb 新增根 **0**; scanmz 新增 MZ **0** (恒 194); 区域差分新增仅字体 + GC 噪声
  (~27MB); WS 全程平直 ~1.2GB。播放后内存 ≈ 菜单态。
- **真人实玩终验 [实测]** (lead 裁定 C, 20:58-21:02): 实玩 (非回放) 一局, 同三仪器 17 拍 —
  新增根 0 / 新增 MZ 0 / 新增跨度仅 6.3MB (24MB 触发阈未达) / WS 平直 1272-1283MB。
  回放与实玩双通道均无解密迹象 → 裁决终验通过。
- **裁决 [实测→推断高置信]**: XX 追补3「31.8MB 载荷 play 时解密驻留」推断**被推翻** —
  play 全程无解密迹象; gameplay.dll 以 2276B 桩形态加载, 31.8MB 加密载荷为**运行时死重**。
  铁证链: bindprobe (XXII/XXV) 实锤 0xA4 写入发生在回放播放中 → 判定引擎在跑 → 其宿主
  程序集必须驻留 → 清点无候选 → **绑定主路径 IL 在 osu!.exe 内** (§8.3「单体构建」运行时
  层面结论被正名: 文件存在 ≠ 运行时使用)。XXI/XX 静态线 (m6626/m4510/计分格式化器在 exe)
  与此完全自洽。
- **路线改判 (lead 终验后确认)**: 待办#1 原文「gameplay.dll 三路采集导出逆向绑定主路径」
  **路线死亡** (无可导出物); 重写为 **osu!.exe 静态 IL 猎杀 0xA4 写入方法** — 工具链
  build/stable-re (lib.mjs 对 osu!.exe 全可用, XXVI T2 已验证 metaOff/streams 回归全等);
  搜索锚 = stfld 0xA4 偏移 + XXII bindprobe 时序对齐 + 人工核对。
- 附注: T1 交付 tools/capture_gameplay.ps1 (编排脚本) 含「前 8 候选 × 固定 40MB」缺陷
  (ReadProcessMemory 含不可读页整体失败 → 0 字节), 待按跨度感知重写; 本次实际采集由
  dumps/_live/capture_live{,2,3}.ps1 承担。
- gate: 判定零改动 — 本节全程只读观测, bz 95 / azimuth 114 / V1 全表 / ende+promise 维持。
- IL 猎杀 (三轮迭代, 前两轮作废教训记录): ① **几何勘误 [实测]**: lib.mjs parseStreams 在
  osu!.exe 上把 #~ 流算到 metaOff+0x24 (真值 metaOff+0x6c, meta.mjs 硬编码即深夜时代
  手工验证基准) — verLen 非对齐导致流头走位; T2 当轮验证只对过 gameplay.dll 桩与
  gplib↔lib.mjs 同源一致性, 未对 osu!.exe 流头独立验证 → a4scan/a4hunt/a4finger 首轮
  (20 候选 / m12598@t1687) 全部基于漂移几何, **作废**。改用 meta.mjs 基准重扫。
  ② **操作码勘误 [实测]**: il.mjs 表 0x80=stsfld 错误 (ECMA 0x7F, 0x80 保留;
  stfld=0x7D/ldfld=0x7B/ldflda=0x7C/ldsfld=0x7E 无误), writers.mjs/ldflda.mjs 按
  0x80/0x7F 的历史扫描按噪声处理待重扫。
  ③ **布局模型教训 [实测]**: auto-reverse/forward 假设在 meta 基准下 0xA4 零候选 —
  根因 = 继承链未解 (基类字段先排) + CLR auto-layout 算法未知, 纯静态布局推演不可靠。
  ④ **完成模式全域扫描 [实测]** (meta 基准, 18959 方法体): 「同方法写 {1,1,0} 三同类型
  字段 + 非常量第四写」= 28 匹配 / 20 类型 (含 **t2241 = Score 类** m16534: stfld 31,
  ones=[f10624,f10596,f10600] zeros=[f10604], 与深夜 XIX 身份交叉吻合)。 loosened 模式
  仍欠区分度 — 终解 = **XXII 值序列匹配**: bindprobe jsonl 有 512 对象全字段 (offset→值)
  时间线, IL 侧有 (方法,字段,常量) 写入序列, 双侧序列对齐可静态锚定 rid↔运行时偏移,
  一次校准全类型布局模型; 备选 = live MT→EEClass→FieldDesc 走查 (需一局播放)。
- **0xA4 锚定尝试与撤回 [实测→已证伪]** (XXVI 追补, 当晚续): ① 0x5C 位常量指纹
  (含 1<<28 组合归因) 全 exe 唯一命中 f11033 @ **t2372** — 锚本身可信; ② 前向布局
  相对映射一度得出 0xA4←f11053, 但 **m4141/m15503/m17391 三方法解剖实为多态惰性缓存
  getter** (sig () -> 集合; 判空 + 枚举计算 + stfld f11053 缓存 + ret), 且继承链查证
  t528→t1128→t2372→t1130、t2139→t528 — 三类型同属 t2372 子树 (28 类), f11053 = 基类
  共享惰性缓存字段 → 与运行时 0xA4 逐 note song-time 值 (非堆指针) 语义矛盾 →
  **「0xA4 = f11053」撤回; 三个「setter」实为 lazy getter, 非 0xA4 写入者**。
  教训: 纯静态布局推演在继承链 + 字段尺寸盲区下不可靠 (sz1 sz4 之谜), 必须 live 校准。
- **地图成果 [实测, 保留]**: t2372 = mania 基础 note 类 (24 自有字段, 0x58-0xA8;
  0x5C 位标志 f11033 / 0x80 判定 f11043 / 0x84 done f11044 按相对映射高置信);
  子树 28 类 (t911/t1128/t1543/t2181 直接子类; **t528 = 可玩 note 类, 46 自有字段
  f2929-f2974 排 0xA8-0x160** — m15503 IL 读取 f2944 = 0xE4 状态标志吻合; t2139 =
  LN 子类 +13 字段); 12 个 lazy-getter 调用方全部在子树内。运行时 0xA8+ 活跃字段
  (0xB4 flag×512 / 0xD8 / 0xE0 / 0xE4 / 0xE8 位域) = t528 及更深层派生字段。

## 深夜 XXVII: 0xA4 = f1109 (t304) — 绑定写入 `← GameTime` IL 实锤 (live MT 走查; 2026-10-15 深夜续)

- **终局仪器执行 + 绑定写入实锤 [实测]** (三局采集迭代): cols 首抓误得列对象
  (t405, BaseSize 0x40 — 7 列对象 col0-6 同 MT 0x77A2CC0); 改 notes 链式捕获 (items 指针表
  → 前 10 对象 0x100 → 各 MT) 后 **MT 走查成功**: obj[1-9] 同 MT 0x77987F0 → token
  0x02000130 = **t304 (可玩 note 类, BaseSize 188)**; obj[0] MT 0x77F9A00 → token
  0x020003E3 = **t995 (LN/hold 子类, BaseSize 244)** — live 身份一锤定音。
- **0xA4 = f1109 [实测, BaseSize 不变量证明]**: t304 自有 12 字段 (f1106-f1117) 必须
  填满 [X, 188), 尾部反推 f1117@184 → f1116@183 → f1115@182 → f1114@181 → f1113@180
  (4B, 前 3B 对齐垫) → f1112@176 → f1111@172 → f1110@168(1B) → **f1109@164 = 0xA4** —
  对 vt 尺寸/基块大小全部免疫的数学不变量; 运行时今晚 dump 铁证: 0xA4 = 逐 note
  press 时刻 (1481/2475/2828/3154... 跟随 0x10 chart start ±几 ms) ✓。
- **绑定写入指令 IL 实锤 [实测]**: f1109 唯一写入点 **m18905 @ t2548** (sig () -> void,
  243 instr 管理类方法): 循环体内 **`ldloc.0(note); ldsfld f11743; stfld f1109`** —
  f11743 = 游戏全局当前歌曲时间静态字段 → **press 按下时 note.0xA4 ← 当前 GameTime** =
  绑定写入本体 (XXII「灰按值 24896」= 按键瞬间 GameTime, 语义闭环)。同方法可见判别式
  原始条件链: f1108==2 检查 → f1111←1 → f1109←now (等价物); m18905 无直接调用方
  (virtual/delegate 派发, ConfuserEx 层), 12 个子树内 lazy-getter 调用方亦在此层级。
- **勘误与撤回**: ①「0xA4 = f11053 / 三 setter m4141/m15503/m17391」**撤回** — 三方法
  实为多态惰性缓存 getter (sig () -> 集合, 判空+计算+缓存), f11053 = t2372 基类共享缓存
  字段, 继承链 t528→t1128→t2372→t1130 查证; 0xA4 的 press-time 值非堆指针, 语义矛盾排除。
  ② 首轮 20 候选/20 类型几何漂移作废 (parseStreams 勘误)。③ il.mjs 0x80=stsfld 勘误维持。
- **布局地图 [实测]**: t1130 基类块 0x04-0x58 (21×4B); t2372 f11030-f11053 0x58-0xA8
  (0x5C=f11033 位标志 / 0x80=f11043 判定 / 0x84=f11044 done — BaseSize 不变量+写常量集
  双重锚定); **t304 自有 f1106-f1117 0x98-0xBC** (0x98=f1106 松键时刻 / 0xA4=f1109 press
  绑定 / 0xA8=f1110 done-bool / 0xB4-0xB7=f1114-f1116 打包 bool / 0xB8-0xBC=f1117 缓存);
  t995 = t2024 支 (LN/hold, BaseSize 244, 0xBC-0xF4 为 t2024 支字段: 0xD8/0xE0/0xE4/0xE8)。
  512 note 全为 t304/t995 实例 ✓ (0x84 512/512, 0xE4 371/512 LN 子集)。
- **下一步**: m18905 循环条件精读 (f1108==2 / f1111 置位语义 = 判别式候选) + virtual 派发
  回溯 (基类虚方法 token → 直接调用方) → 与 XXII 时序对齐 → 判别式交判定行走。
- gate: 判定零改动 — 本节全程只读观测, bz 95 / azimuth 114 / V1 全表 / ende+promise 维持。
- 数据: osu-mania-live-reader/dumps/_live/mtwalk/{obj_*.bin, mt_*.bin, ptrtable.bin},
  output/cols_note_dump.txt, dumps/_live/{baseline_at_arm.json, play_timeline.jsonl,
  play_timeline_replay_session.jsonl, regions_postplay.json, sweep/, roots/}, dumps/candidates/,
  采集清单 test/sample/gameplay_dump_checklist.md; IL 猎杀与 MT 走查产出
  build/stable-re/{a4scan.mjs, a4hunt.mjs, a4finger.mjs, a4diag.mjs, a4m12598.mjs,
  a4match3.mjs, a4hunt2.mjs, a4hunt2_results.json, a4match.mjs, a4match2.mjs,
  a4match3_anchors.json, a4pin.mjs, a4final.mjs, a4chain.mjs, a4sub.mjs, a4t304.mjs,
  a4check304.mjs, a4solve.mjs, a4solve_t2372.json, a4bind.mjs, a4bind_report.json,
  a4mt.mjs, a4scan_results.json, a4hunt_results.json, a4final_layout.json}
  (注意: a4scan_results/a4hunt_results/a4final_layout 为漂移几何时期产物仅作考古;
  有效基准 = meta.mjs 几何 + a4bind/a4final 的 BaseSize 不变量路线)。

## 工作规程 (用户指令, 2026-10-16 定)
- **可拆分的具体任务一律分发给子成员 (subagent) 并行执行**: 模型指定 `deepseek-v4-flash-vision-exp`, 思考等级 `high`; 主会话保留综合判读、结论裁决与档案写入。机械性子任务 (全库审计/符号解码/调用方扫描) 优先拆出; 各子任务产出落 build/stable-re/*.json, 写域互不重叠。

## 深夜 XXVIII: m18905 全量重解 + 派发链恢复 — 判别式定稿输入 (2026-10-16 凌晨; 3×子代理 deepseek-v4-flash-vision-exp high 并行)

### 0. 规约与分工
- 新规程 (用户令): 可拆分任务派发子代理 (deepseek-v4-flash-vision-exp / high), 主会话综合判读。本轮: A=操作码表全库审计, B=符号语义解码, C=调用方虚派发回溯; 主会话=FSM 精读/窗口算术/值域普查/交叉抽验。
- 产出: build/stable-re/{optable_audit.json, m18905_symbols.json, m18905_callers.json}。

### 1. 仪器勘误 (critical, 全部实证)
- **a4bind_report.json 作废**: a4bind.mjs OP1/TOK 漏消费 0x80 stsfld/0xa4 stelem/fe.15 initobj/fe.16 constrained. 的 4B token (另漏 0x81/0xc2/0xc6/fe.1c, 多余 0xa6, fe.0e-14 长度全错), 自首指令起错位 4B → 其「m18905=243 instr」系残段, IL 文本不可信, 禁在其上推理。
- **旧坑语作废**: 「il.mjs 表 0x80=stsfld 错(0x7F 对)」系误记。实证: 全库 19659 有体方法 0 aborts; 字段组操作码与字段静态性 100% 吻合 (0x7b/7c/7d→实例, 0x7e/7f/80→静态, 组内零例外); **il.mjs 0x7b-0x80 行全对** (0x7f=ldsflda, 0x80=stsfld)。a4bind TNAME 0x7f:"stsfld" 才是错的。
- **meta.mjs 几何勘误 (Sub-B)**: rowSizes[12]=14 (应12), rowSizes[27]=8 (应4) → 表 id≥13 全体偏移 (TypeSpec 偏 1326×4B); TypeRef scope 应 u16/ns@+6; MethodDef.paramList 应 2B; StandAloneSig/TypeSpec/MethodImpl 等 ≥13 表的 row()/offsets 不可用。TypeDef/Field/MethodDef (≤11) 不受影响。
- decodeBody 约定坑: 本会话 decodeBody(idx) 之 idx = rid−1; 项目 m-名 = meta.mjs 索引; 原始 token 0x06:rid。项目「m18905」= rid 18906 (rva 0x1c8cb8, 1541B); rid 18905 (137B) 是其姊妹覆写。构造器掩码 0x1886 (SpecialName|RTSpecialName), 非我误用的 0x1900。

### 2. m18905 真身 (rid 18906, t2548, 525 instr / 1541B, flags 0x2c3)
- 结构: Prologue (stsfld f2564=0; 建逐列 loc4 byte[]/loc6 t304[]; loc8 = f2649?.f10622≠null = replay/auto 位) → Loop A (逆序 for over f7808) → Loop B (foreach over f7808, mr924/925/927) → Loop C (f2563.f2641==2 才跑, per-column)。
- **绑定写 ×2** (f1109←ldsfld f11743 GameTime): ① f1108∈{0,2} 即时分支 (IL_26c); ② f1108==1 ∧ f1111==0 首按分支 (IL_2c6)。释放写 f1110←GameTime (IL_3fb-401)——**勘误 (同晚补)**: 该写点位于 f1108==1 死分支内, 运行时永不执行, 见 §5.9。
- **f1108 值域裁决**: 全库唯一 stfld 写点 = t2023..ctor (idx14851, 置 1); t2023 : System.Object (tr514), fieldList 9491..9495 (f1108 属 t304!), newobj 该 ctor 全库 **0 处 = 死代码**; ldflda 旁路 0 → **运行时 f1108 恒 0** (零初始化)。∴ live 路径 = {0} 分支; {1,2} 为死分支 (判别式不再依赖 f1108==2!)。
- **写点排他性铁证** (全库 19659 方法, 完整表 0 aborts): f1109 写点仅 2 处、f1110 仅 1 处、f1111 仅 3 处 — 全部在 idx18905 本体; ldflda 旁路 0。

### 3. 派发链 (Sub-C 突破, 主会话抽验通过)
- 直接引用 0 (call/callvirt/ldftn/ldvirtftn/MemberRef 同名同签/MethodImpl 全零)。基类 t1598 声明虚槽 rid 11937/11938 (flags 0x3c3, NewSlot); t2548 rid 18905/18906 (0x2c3, ReuseSlot) = 显式覆写。
- 派发站点 (全指向基槽): callvirt @ rid 3725 (t489, il_0959→11937 / il_09cf→11938) 与 rid 11936 (t1598 自身 wrapper: `ldarg.0; callvirt 11937; ldarg.0; callvirt 11938; ret`, 被另 10 类 callvirt ×11 处); 非 dispatch 的 base-call ×3 (t265/t1671/t1703)。可达性: newobj t2548..ctor ×4 (t1201/t1292/t1606/t2838)。
- **rid 3725 (t489) = 帧驱动器**: gate(f2618.callvirt rid4502/4497; call rid3735/3733) → this.f2597.callvirt 槽11937 (步骤A) → call rid10951+brfalse → 槽11938 (FSM)。
- **步骤 A (rid 18905 / idx18904, 137B) = 滑窗切片器**: 二分 (~负值取反) 定位 `GameTime − f3112 − 200` 起点, 向后扫到 `f5491 > GameTime + 300` 止, `f7808 = f7806.GetRange(起点, len)`; 空窗则 f7808.Clear()。∴ Loop A/B 只处理滑窗内的 notes。

### 4. Loop A 逐列槽仲裁 (= jack 归属机制本体)
- 逆序 (尾→头) 逐 note: col = t405::op_Implicit(note.f1106) (m2922); loc9[col] 已处理→跳; note.f11030 (已判)→loc9[col]=1 跳; loc6[col] 空且 GameTime<f5491 → 占槽; 到点 (GameTime≥f5491) 走算术:
- `deadline = Math.Min(cand.f5491 + f3113, slot.f5491 − 1) + Math.Max(0, slot.f5491 − cand.f5491 − 2·f3113)`; `GameTime ≤ deadline → cand 夺槽` (mr662=Min, mr256=Max, Sub-B 确认); 随后 loc9[col]=1 该列定稿。

### 5. Loop B 门序 (press 绑定判别式)
1. isinst t2024 → loc3 (LN 支); m17382() void 更新钩子;
2. f11030 已判: replay 位下 GameTime≥f5491 直接跳, 否则 loc4[col]=1 跳;
3. loc4[col] 帧内已处理 → 跳;
4. GameTime < f5491 − f11843 (未进窗) → 跳;
5. **m18907 门** (idx18906, 125B, 主会话逐条复验 ✓): `x = (float)f3114`; 若 replay 位 ∧ LN(q=as t2024): x *= 1.5f, 且 GameTime ≥ q.f5491 + f3114 ∧ !q.f9511 → m18911(q) 重臂; **GameTime < f5492 + x → 返回 false (进 press 块)**; f11030 → false (也进); 否则 m11922(note) 提交后返回 true (跳过 press 块)。∴ press 窗 = [f5491 − f11843, f5492 + f3114·(LN∧replay ? 1.5 : 1)) ∪ 已判再入;
6. m2933() (t405, 读 f2015) ∧ ¬f2014 (上帧状态) → **按键正沿** → press 块; m2933() ∧ f2014 → IL_360 (LN 支, 死); ¬m2933() → 释放块 (死, 见 9); [t405 语义: idx2933/idx10967 帧轮询 `f2014←f2015 旧值; f2015←新输入`]
7. replay 位下: loc6[col] == note (Loop A 仲裁胜者) 才可绑定;
8. press 块: f1111←1; **f1109←GameTime**; loc4[col]=1; f11856[col]=null; f2563 事件 (m3765, Nullable<i4>); m11922(note);
9. ~~释放块~~ **勘误 (同晚补)**: 释放块整体在 f1108==1 门后 → 死代码; 运行时 f1110 恒 0、f1111 永不清零、本方法无 release 语义; key-up 仅走 m2932(0) 终结; **LN 尾判定 = 尾对象独立 press 绑定** (对应读取器 tail.eventTime)。

### 6. Loop C (f2563.f2641 == 2 模式才跑)
- 逐列: press 时刻入 f11858[col]; hold 时长分档计数 (<20ms→f11859+=2; <38→+1; <100→f11860++); loc4[col] 已处理 → f11856[col]==null 时 `List.Find(Predicate=new Predicate(f7806, ldftn m18923))` 重绑定 → m17391()。

### 7. 符号表要点 (Sub-B, 几何自洽重推导)
- f7806/f7808 = List<t2372> @t1598 (master/窗口); f1106 = class t405 (列键对象, op_Implicit→列号; 0x98 布局更正非「松键时刻」); f5491/f5492 = i4 头/尾 @t1130; f3112/f3113/f3114 = i4 @t565 (窗回看/仲裁余量/press 余量); f11843 i4, f11856 t304[], f11858 i4[], f11859/f11860 i4 @t2548; f1108 = valuetype t726 (System.Enum 无字面值); f9509/f9510/f9511/f9496/f9497 @t2024; f11030 bool @t2372; f2563 = static t489 单例, f2564 static bool; f2649/f10622 = replay/auto 数据链。
- 类型: t304 (abstract, : t1543, fields f1106-f1117 = 0x98-0xBC); t2024 : t304 (f9496-f9512); t995 : t2024 (无自有字段, LN 244B); t2372 : t1130 : MarshalByRefObject; t2548 : t1598 : t565; t405 = 列键对象 (MT 0x77A2CC0 7列)。
- m18907 (t2548) bool(t2372); m18910/m18911 t1949(t2024) LN 支/重臂; m11922 t1949(t2372) virtual @t1598; m3765 void(t2372, Nullable<i4>) @t489; m17382/m17391 void() @t2372; m18922..ctor/m18923 bool(t2372) @t2550 (Find 谓词)。

### 8. 判别式草案 (交判定行走)
- **press 绑定** (note.0xA4 ← GameTime) 当且仅当: 在窗 [f5491−f11843, f5492+x) ∧ ¬f11030 ∧ 按键沿 (m2933()=1 ∧ f2014=0) ∧ 帧内列未处理 (loc4[col]=0) ∧ replay 模式下为 Loop A 仲裁胜者 (loc6[col]==note, deadline 算术见 §4)。
- ~~release~~ **勘误**: f1110 (0xA8) 运行时恒 0 (唯一写点在 f1108==1 死分支); 判别式只有 press 侧; LN 尾 = 独立 press 绑定。
- 下一步: 判别式交判定行走 (V1 walkF 侧挂接对照 bz/azimuth/雪月夜鐘; 红线: 判定零改动)。

### 9. 本节 gate
- 判定零改动 (全程静态只读); bz 95 / azimuth 114 / V1 全表 / V2 六图 ≤0.088 维持; ship 与回滚点不变。
### 10. 判别式闭环验证 (深夜 XXVIII 追补, 2026-10-16 凌晨)
- **仪器**: build/stable-re/bindsim.mjs — 判别式模拟器 (滑窗 + Loop A 仲裁精确控制流 + 按键正沿 press 门 + OD 判定窗映射); 支持 --osu 图谱直读 (C:/osu!/Songs md5 锚定) 与 --map 真值表模式; 边沿按文件序 (t 非单调忠实化); 列数回退链 Keys→CircleSize→identity→4。
- **输入**: Sub-E 41 个 tier-A 配对 (bindkeys_*.json, .osr 边沿解码 0 错误) + **回放自身头部 counts = 该局内存真值** (绕开 sample≠同局难题, 无需 reader 配对)。
- **首例 Paradigm Shift [Shift] (4K, OD9, 2237 note/30 LN)**: 绑定 2235 = non-miss 总数**精确**; 判定分布预测 vs 回放头部 = **Δ 仅 1 条差一档** (MAX 1654=1654 / 300 547vs546 / 200 30vs31 / 100 4=4 / 50 0=0 / MISS 2=2) — **判别式 (滑窗+仲裁+按键沿+窗口门) 与 stable 绑定行为闭环吻合**。
- 四图绑定计数全精确 (2235/972/2791/2007; miss 2/0/0/0 全对)。LN 重图 (startrip 644LN / synthesis 1349LN / divinity 1859LN) 全向量偏差属预期 — V1 的 LN 单元终判依赖 release 侧 (1.5x 窗), 属待办 #2 判定路径, 非绑定层职责。
- 对齐方法论: 绑定总数对 offset 不敏感 → 对齐打分改为 ±great 窗命中数 (真 offset 处尖峰), 粗 50ms + 细 1ms 两轮。
- 批量: build/stable-re/bindsim_batch.mjs (35 tier-A 跑批, 速率感知轮) → bindsim_batch_report.json。**正式轮结论**: 六向量全等 = Stage 2 Promised (L1=0, 20LN); Shift L1=1 (±1 档); 低 LN 图 L1<150 集群 (EXHAUST 29 / Fastest Chicken 45 / Another 47); HT 图速率修正后 L1 1453→121; LN 重图 (2000-11000 LN) L1 1500-2900 且 MAX 缺口 ~1700-2200 — 全部归属 release 侧 LN 单元终判 (待办 #2), 绑定层自身 bind/miss 分配全精确。bindCountExact 口径 (绑定数=非 miss 数) 6/35 全向量级, 绑定数级 30+/35。
- **批量首轮 (bindsim_batch_report.json, 35 tier-A)**: 全 rice/低 LN 图闭环 — Shift ±1 档、Stage 2 Promised (20LN) **六向量全等★**; 绑定计数全部 ≈ 精确 (miss 几乎全零偏差)。LN 重图 MAX/300 再分布属预期 (release 侧判定 = 待办 #2)。
- **速率图规律 (实证)**: DT/HT 回放判定窗 × 速率本身 (DT ×1.5: δ-3 两局 MAX ±21 闭合; HT ×0.75: ε-δ 11015note L1≈121) — 已设 bindsim 默认 (--wmax/--wgreat 可覆盖); DT 残差 ~2% 待精化 (疑 offset 漂移/子帧效应)。
- **坑新增**: 图谱 [Difficulty] 可能无 Keys 行 (回退 CircleSize); osu section 正则加 m 旗标会截断段 ($ 变行尾); .osr 开局伪按键 0x100 (col=8, 状态机无影响但需知情); 回放 t 非单调按文件序处理; bindkeys identity 无 keys 字段。- **bindsim LN 模型 v1 (并入)**: LN 单元落盘 = 未绑→miss; 释放 ≥ 尾 → 彩 (f11052=300 升格); 尾窗内 (尾−meh..尾) 释放 → 档位直写 (endDiff 判窗, 彩封顶排除); 早释 (<尾−meh) → miss (重按/LoopC 重绑未建模)。实现: simulate 配对 down/up 边沿 (文件序), 释放入 relOf, counts 映射按单元类型分流。
- **LN 模型 v1 实测**: azimuth (10735 LN) L1 2924→2245 ✓; 低 LN 图维持精确 (Shift L1≈3 / EXHAUST 26); Reciprocate 2011→2376 (释放窗边界待精化)。
- **精化方向 (下次)**: ① 释放有效边界按 OsuManiaJudger 参照 = 尾−50w (meh) 已用; ② LN 档位窗 1.5×/1.2× 宽限 (backend L232/L329 LN leniency) 未入; ③ LoopC 重绑 (f11856 Find) 未建模; ④ 权威实现 = backend walkF (LN Dan III 1.00%), bindsim LN 闭合可改为直调后端对照。
### 11. 家族③定稿: .osr 写盘路径 IL 实证 (待办#2 完成, 2026-10-16 凌晨)
- **定位法**: ConfuserEx 常量加密使字符串扫描全灭 → 外部 MemberRef 名路线 (System.IO.BinaryWriter=tr407/Stream=tr436/TextWriter=tr442/File=tr411) → Write 密集方法 → **idx16558 @t2241 (275B, Score 类) = .osr 序列化器实锤**。
- **写入序列与 .osr 布局逐字段吻合**: f10593(u8 mode) → f10616(i32 gameVersion, 开局钳制 ≥30000000 归 ldsfld f821) → f10594(str beatmapMd5) → f10605(str player) → idx16538()(str replayMd5 自算) → **六计数 i16: f10586(300) f10585(100) f10587(50) f10588(彩geki) f10589(200katu) f10590(miss)** → idx16546()(i32 score) → f10597(u2 maxCombo) → f10604(perfect) → f10592(mods) → idx16562()(life 图) → DateTime.ToBinary(=playedTicks i64) → f10598(压缩帧) → idx16574 收尾。
- **计数累加器 = idx4509 @t573 (5130B/1728instr, 即旧档「写盘疑点 m4510」本体)**: 29 个控点全部为 `this.f3166.<count>++` 形态 — f3166 (t573 字段) 持有当局 Score 对象 (t2241), 逐单元判定直接累加进落盘字段。f10585-f10590 声明于 t2241 (fieldList 几何确认)。
- **持有过尾 LN 尾升格公式 (家族③ IL 终证)**: 0x1152-0x1191 — `unit.f11052 (t2372 数值判定码): ==300 → f10588(彩)++; ==150 → f10589(200)++` — 与行为层结论「落盘把持有过尾 LN 尾按 300/彩档计」逐字吻合; 同块含分数数学 (loc0 += loc0/5 = 彩档 +20% 分数加成; /50*50 归一化; ×2 路径) 与 f10597 maxCombo (idx18457+Max) / f10618 更新。
- **周边**: idx3776 @t489 (249B) = 六计数清零; idx16534/16535 @t2241 = .ctor 写六计数; idx16557/16569 = 拷贝/重置; idx10576 @t1391(→t573 子类) = f11052 另一写者。
- **编号勘误**: 旧「m4510」标签所指本体 = idx4509 (rid 4510, 5130B) — 以本文内容为准; 旧会话 m-名约定混用 (m4510.mjs 用 methodDef(4509), m6626 指 idx6626), 新会话一律 rid=idx+1。
- **工具**: build/stable-re/strictil.mjs = 共享严格解码器 (全量 ECMA 表固化, #US 解析, owner/链/签名助手), 子代理与会话统一引用; m4509_idx4509_full.il.txt = 全量 IL 落盘。
- gate: 判定零改动 — 全程静态只读; bz 95 / azimuth 114 / V1 全表 / V2 六图 ≤0.088 维持。- **idx10576 @t1391 (t573 子类, 382B) = 判定档位分派器**: 结果码位标志 (负→miss: combo 清零+f10590++; 256→50 档; 512-514→100 档; 1024-1026/1028→300 档) → `unit.f11052 ← 档位值 (50/100/300)` → combo (f10597=Max) / f10618 / 按档加权加分。与 §11 升格公式合并后 LN 尾落盘模型完整: 贴尾释放→档位直写 (300/200/100/50), 持有过尾→升格 (300→彩, 150 类→200); Azimuth Dan 分布 (彩 3985/300 5967/200 644, LN 10735) 由「贴尾释放→300、持有过尾→彩、sloppy→200」定量解释。
- **bindsim LN 闭环路径 (下次)**: 补 LN 尾模型 (release 事件 + 尾窗档位 + f11052 码 + 升格) 即可闭合 LN 重图判定分布; 或直接以后端 walkF (已验证 LN Dan III 1.00%) 为 LN 判定权威, bindsim 专注绑定层。

## 2026-10-16 深夜 XXIX: v2 notelock 采集验证 (待办#3 完成) — Negai 510/510 全等 + watchcols 路线封案

### 1. watchcols 0x38 位语义裁决 (usable=false, 主线改判依据)
- 一采 (bz V1, bind_watchcols.jsonl 15607 events) 解码: 0x38 = **3 个 1-bit 字段打包** (bit0=本列 LN 开启/按住: hold 按 96.9% 置位 vs rice 1.2%, hold&MAX 99.4%; bit16/bit24=键按下态两次写入, 近冗余 5.2% 差 1 tick), 仅 7 取值, 帧级可被 (rawKeys 列位+是否开 hold) 完全重建 → 无剩余信息量; **整局游玩期 7 列×0x100 字节仅 0x38 写入, 无任何指针字段** → 列对象前 0x100 字节无锁定目标。Sub-B 独立位审计 F1≥0.9967 双向佐证。
- 仪器事实: 探针时间戳量化 **15.625ms 网格** (Environment.TickCount, 容差 ≥±8ms); **probe 列序 = 置换** (bz 局 [3,2,0,6,5,4,1], 与物件表列首现序一致, 回 watchcols 逐列判定必须互相关反推); replay_ms ≈ probe_t + ~944 逐局重算; 一采实际时长 600000ms (非 300000)。
- v2 伴随判据 (Sub-A 提案, 已执行): v2 局 0x38 出现第 4 bit/新组合才算锁定标志候选 — **Negai 局实测仍 7 值 (newBitAppears=false)** → watchcols notelock 路线**永久封案**。数据: build/stable-re/notelock_probe_semantics.json。
- 0x38 可用一面 (旁证): bit0 = 逐按「是否被接受/开启 LN」列级信号, 深夜 XXI 三例灰按案 (414/486/344) 的 0x1010000/0x1010001 序列复现 0xA4 故事。

### 2. 主线改判与工具定稿
- **归属真值 = watchobj 物件层 0xA4**: to 值 = stable 写入的该按 GameTime (clock: a4To vs 帧 press median **0ms**, maxAbs 0); 配对 = 同列 ±15ms 最近沿 (col|frameT 复合键一对一防 chord 误抢), 双干跑 495/495 + 508/508 全配对。
- **--osr 直读** (.osr→帧, 纯 JS LZMA-alone 零依赖): 解码与 Python FORMAT_ALONE 逐字节 sha256 双实现一致 (fixture_4k 71B c0b83847…; Voyaging 224975B 42edd44d…, 未知尺寸 EOS / 已知尺寸双分支); 帧重建传递闭包 **JS ≡ C++ 引擎 ≡ Python** (bz/TB/azimuth/az2304 帧表逐行一致, 31591/10696/85374/82386 行); 合成 .osr 专打负增量分支通过。数据缺口如实记录: 工作区无 bz 局 .osr, 495/495 仅 --frames 可复现。
- 垃圾闸门 (bind_watchobj.jsonl 尾部 t≥317.5s 内存复用 4950 条含 137 条 0xA4 垃圾 → start 一致 + 时限闸门剔除); bindprobe range hdr 非法 JSON ("range":"range":[…] ) 解析器容错修复 (repaired 计数); 0xD8 ≡ 0xA4 (同刻同 note 同 to) = 最强旁证 (bz 354/356, Negai 382/383)。
- 工具: build/stable-re/notelock_v2.mjs (--watchobj/--watchcols 伴随/--osr/--legacy-k; 恢复点 notelock_v2.mjs.osr-patch-backup)。

### 3. Negai 采集与身份核验
- 用户确认锚点 = **10/01 那把本地成绩**; 10/10 的 967fdec0 会话即其重放捕获。本次重放 (12:23:37) 三路同场: watchobj w[1792..2303] + watchcols 全程 + reader → 会话 2026-10-16_122337_967fdec0 入册 test/sample/new/。
- **身份核验全过**: counts [3360,1959,317,25,35,29] 与权威锚点**逐位一致** (重放确定性), acc 96.22, maxCombo 1093, beatmapId 3459342 ✓。

### 4. 正式判定 (judged-aware k′) — 待办#3 终局
- **510 writes / 510 配对 (±15ms 100%) / agree 510 / disagree 0 / agreeRate 1.0**; ruleHistogram (judged-aware) gap>2meh 261 / [meh,2meh] 186 / gap<meh 63 — 三区制全覆盖; disagreeBreakdown {kWasJudged: 0, genuineRuleDiff: 0}。
- **结论: stable 的 v2 notelock 按键归属 = 我们的 V2 绑定分界线三段公式, Negai 上零残差。**
- **锁定语义精确化 (文档语义的实现级澄清)**: k′ = 列内**未判定**序列中 start ≤ t 的最后一个 (不存在 → 未判定队头, 允许 start > t); 三段公式作用于未判定序列的 k′/k′+1 (gap = 原始 start 差); 早按 (≥−meh) 归队头。
- 判定时刻真值来源: 窗内 = 该 note 自身更早 0xA4 from0 写入; 窗外 = 读取器 head.eventTime < 按时刻 (negai 在 premise-ok 名单; 覆盖率 3159/3214 = 0.9829 ≥ 0.9 闸门; game.done 是局末态不可用作时刻)。

### 5. 首跑 0.9373 → 1.0 的根因 (方法论)
- 旧 k 选择未建模已判定态 → 32 条 disagree (gap<meh 30 + [meh,2meh] 2, press −3..−157ms 早) 32/32 的 k 在按前已判定; 同批写入 crossCheck: legacyDisagree 32 / kWasJudged 32 / genuineRuleDiff 0; 288/510 条写入含 stale-k (全部落在 legacy agree 域)。--legacy-k 可复现 0.9373; selftest bz V1 无同局读取器真值自动退化 legacy (0.8263 维持, pressAlignment 逐字节不变)。
- 教训: 规则文本的量词限定词 (「未判定」) 必须逐字落到实现; 文档语义与实现选择要逐字对照 (与深夜 XXVIII a4bind 教训同源)。

### 6. gate 与数据
- gate: **判定零改动** — notelock_v2.mjs 为独立观测分析器, backend/hit_matcher 未触碰; bz 95 / azimuth 114 / V1 全表 / V2 六图 ≤0.088 维持; ship 与回滚点不变。
- 数据: build/stable-re/{notelock_v2_negai.json (510 pressAlignment + kSelectionAudit), notelock_v2_dryrun.json (格式验证), notelock_probe_semantics.json} / test/sample/new/2026-10-16_122337_967fdec0.json / test/sample/v2_notelock_capture_checklist.md。
### 7. 次采局 TrueBlue 追补 (同日 12:59 第二图佐证, 两图互证闭环)
- 采集: watchobj w[1536..2047] + watchcols 全程 + reader (会话 2026-10-16_125959_a5af1629 入册 test/sample/new/)。身份: counts [2953,951,62,1,4,7] / acc 98.79 / maxCombo 1783 与新局锚点逐位一致 (重放确定性; playedTicks 缺同 Negai)。
- 数据缺口与替代: osu! 观看重放未落 .osr → 帧源改用深夜 XX 已逐字节验证的 engine_trueblue.frames.csv (同一局确定性输入); 运行时证明 = clock a4To vs 帧 press median **0ms** (maxAbs 0) + 配对 510/510 — fixture 帧表与本次游玩逐帧吻合。
- **勘误+溯源闭环 (同日 13:05-13:08)**: 用户首次手动导出的 True Blue .osr (文件名 (2026-05-26), 16,438B) 头部解析 score 951259 / combo 1466 / counts ≠ 锚点 → **另一局 (05-26 局, 成绩反而更高), 非本次游玩的重放**; 其帧流与新局高度相似 (±15ms 内仍全配对, clock maxAbs 15ms) 但已识别撤销, 存证 trueblue_export_wrongplay_951259.osr。用户重新导出后获得正确锚点件 (文件名 (2026-09-20), 16,550B, 头部 ticks 639255048516154445 / score 943113 / counts 全中, 与 XX 追补 fixture 尺寸一致) → 归位 trueblue_session.osr, **--osr 全溯源重跑: 510/510 配对, agree 510 / disagree 0 / agreeRate 1.0, clock median 0ms (maxAbs 0) = fixture 路径逐位一致** (fixture 与 .osr 互为交叉验证)。正典判定已更新为 --osr 版。
- **判定: agree 510 / disagree 0 / agreeRate 1.0, 且在 legacy k 选择下直接满分** (读取器会话 game.eventTime 0/2106 覆盖闸门自动退化) — [meh,2meh] 制 (128) + >2meh 制 (381) 不受 stale-k 影响; k-is-last-note 边界案 1 条亦 agree。ruleHistogram {gap>2meh:381, meh<=gap<=2meh:128, k-is-last:1}。
- 0x38 伴随: 仍 7 值 (newBitAppears=false) — 第二张 v2 图证实列对象无锁定目标; 0xD8≡0xA4 旁证 474/476; 垃圾闸门剔除 324 条 (integrity startMismatch 全数拦下)。
- **待办#3 终局: 两图互证 (Negai 三区制 judged-aware 1.0 + TrueBlue [meh,2meh] 制 legacy 1.0) — stable v2 notelock = 我们 V2 绑定分界线公式, 跨图成立。**

## 2026-10-16 深夜 XXX: 锚点疑点校核 (test/sample 读取器真值对照) + 锚点全量清册入册

> 触发: 锚点清册 build/anchor_scores_inventory.json 编制完成 (32 条) 后列出 8 项身份疑点; 用户指令「根据 test/sample 读取器真值对疑点成绩做信息校核」。
> 方法: 12 份读取器真值 (test/sample/*.json 8 份 + test/sample/new/*.json 4 份) 的 summary.counts / summary.acc / beatmap 块, 对 scores.db 全量 dump (build/allrows.json, 16575 行) 逐局对照;
> Δ = reader counts − .osr counts, 六计数序 [geki, n300, katu, n100, n50, miss]; 身份判据 = beatmapMd5 + playedTicks(局) + counts 三对齐。

### 1. 逐图对照 (12 份真值 → 唯一匹配局)

| 读取器真值文件 | 图 (beatmapId / md5) | 匹配局 ticks / score | Δ(reader−.osr) | reader acc | 用户显示 | 判 |
|---|---|---|---|---|---|---|
| Extra Level (LN) Azimuth.json | Extra Level (LN) [Azimuth Dan (Marathon)] / 1122156 / c8b6075b | 639255930839151037 / 761533 | [0, 0, 0, 0, 0, 0] | 94.37 | 94.37 (回放实测 94.37/94.37, docs#12/#724) | **exact Δ=0** |
| new/2026-10-10_002020_21a9c6b6.json | LN Dan Phase III [Azimuth Dan] / 2570281 / 21a9c6b6 | 639185160728867114 / 751596 | [1, 0, 0, 1, -2, 0] | 95.06 | 95.05 标注 / 95.06 回放 (docs#580/#986) | Δ≤2 (2) |
| blue zenith 0.9.json | Blue Zenith [JINDIMENSIONS 0.9x (180bpm) OD5] / 1174605 / 6ef7c488 | 639266318056308553 / 632826 | [0, 1, -2, 0, 0, 1] | 89.95 | 89.95 (docs#13) | Δ≤2 (2) |
| negai.json | Negai [Wish] / 3459342 / 967fdec0 | 639264569108482118 / 838363 | [0, 0, 0, 0, 0, 0] | 96.22 | 96.22 (docs#14) | **exact Δ=0** |
| new/2026-10-10_003356_967fdec0.json | Negai [Wish] / 3459342 / 967fdec0 | 639264569108482118 / 838363 | [0, 0, 0, 0, 0, 0] | 96.22 | 96.22 (同一局重放捕获) | **exact Δ=0** |
| new/2026-10-16_122337_967fdec0.json | Negai [Wish] / 3459342 / 967fdec0 | 639264569108482118 / 838363 | [0, 0, 0, 0, 0, 0] | 96.22 | 96.22 (同一局重放捕获) | **exact Δ=0** |
| true blue.json | True Blue (Cut Ver.) [Repentance (MWC Edit)] / 5484335 / a5af1629 | 639255048516154445 / 943113 | [0, 0, 0, 0, 0, 0] | 98.79 | 98.79 (docs#21/#948) | **exact Δ=0** |
| twin bloom.json | Twin Bloom [Ramification] / 5893787 / da0f6de3 | 639255966267895822 / 776845 | [0, 0, 1, 0, 0, -1] | 94.58 | 94.57 标注 / 94.58 回放 (docs#20) | Δ≤2 (1) |
| xfpsb ln 5th dan.json | xfpsb 4K LN Advanced Dan Course (Part II) [5th Dan - Beyond] / 3451053 / a5392e7f | 639182527588160235 / 862103 | [0, 0, 0, 0, 0, 0] | 96.66 | 96.66 (docs#18) | **exact Δ=0** |
| 4k lndan 13th-yoru.json | 4K LN Dan Courses v2 - Extra Level - [13th Dan - Yoru (Marathon)] / 1862876 / 9068585a | 639090072000706382 / 889714 | [0, 0, 0, 0, 0, 0] | 97.21 | 97.21 (docs#19/#951) | **exact Δ=0** |
| 4k lndan 12th-yuugure.json | 4K LN Dan Courses v2 - Extra Level - [12th Dan - Yuugure (Marathon)] / 1862875 / ec14ce28 | 638178507594526492 / 922757 | [0, 2, 0, 0, 0, -2] | 98.17 | 98.15 标注 / 98.17 回放 (docs#15/#950) | Δ≤2 (2) |
| new/2026-10-10_000931_49b4bd1d.json | 雪月夜鐘 [widest] [[BMS] [st_4]] / 0 / 49b4bd1d | 639179025747860800 / 907986 | [0, 0, 0, 0, 0, 0] | 98.1 | 98.1 (docs#987) | **exact Δ=0** |

- **12/12 全部为对应图的局**: 8 份 Δ 全零 (exact), 4 份 |Δ| ≤ 2 (twin bloom ±1 / bz ±2 / 12th yuugure ±2 / 21a9c6b6 ±2), **0 mismatch** — 支持 docs#935「±2 吻合」口径。
- **读取器 summary.acc = 用户「回放结束」acc: 12/12 逐值一致** (94.37 / 95.06 / 89.95 / 96.22×3 / 98.79 / 94.58 / 96.66 / 97.21 / 98.17 / 98.1); 其中 21a9c6b6 / TwinBloom / 12th yuugure 三图「标注 vs 回放」差 0.01~0.02 (= stable 内部微差, 与 docs#17/#43 一致) → 读取器真值可直接充当用户显示参照。
- 附带新事实: reader 的 beatmap.beatmapId 是 scores.db dump **不具备**的独立身份轴 (allrows.json 无 beatmapId 字段), 可直接区分同名异图 (1122156 vs 2570281)。

### 2. 八项疑点裁决

1. **azimuth 同名异图 — 确证**. reader: Extra Level (LN) [Azimuth Dan (Marathon)] beatmapId **1122156** / 18406n / Δ=[0,0,0,0,0,0] vs c8b6075b 局; LN Dan Phase III [Azimuth Dan] beatmapId **2570281** / 10980n / Δ=[1,0,0,1,−2,0] vs 21a9c6b6 局。**用户 95.05/95.06 锚点属 21a9c6b6**, 铁证 = 该 reader 会话 summary.acc = **95.06** = 用户「回放结束 95.06」(docs#580/#692)。
2. **13th Dan - Yoru 同名异图 — 确证**. reader: '4K LN Dan Courses v2 - Extra Level -' [13th Dan - Yoru (Marathon)] beatmapId **1862876** / 9963n / Δ=0 vs **9068585a** 局 (2026-03-13, score 889714); 同真值对 eZmmR 两局 (2026-03-13 21:41 / 2026-09-14) Δ 达 +8113..+8268 → **非同图**; eZmmR 变体无读取器真值, 其身份仅由 scores.db + stepwise_anchor_Yoruv2 支撑 (故清册中标 ⚠局次未定/作废)。
3. **FUBUKI md5 归属 — 确证任务书有误**. reader: xfpsb 4K LN Advanced Dan Course (Part II) [5th Dan - Beyond] / beatmapId **3451053** / Δ=0 vs **a5392e7f** (docs#18/#862/#1126 同指) → a5392e7f **不是 FUBUKI**; FUBUKI 本局 md5 = **0016e2aa5d33a343c471aecc135d8d67** (allrows tick 638473153125060134)。
4. **testset TrueBlue 局次 — 以真值文件为准 = 新局**. reader counts [2953,951,62,1,4,7] Δ=0 vs tick **639255048516154445** (score 943113, 2026-09-20 20:40:51); 对旧局 tick 639255046362479287 (score 911860) Δ=[97,−48,−24,−13,−4,−8]。docs#704「testset TrueBlue = 旧局」与读取器真值冲突, 深夜 XI docs#749-753 的修正 (154913 会话 = 新局) 由此获得真值级确认。
5. **4KLNDan 12th 锚点局次 — 确证 2023-04-23**. reader counts [10350,4892,427,22,9,39] 对 tick **638178507594526492** (score 922757, 2023-04-23) Δ=[0,2,0,0,0,−2]; 对 2026-09-24 局 Δ=[−506,436,77,2,−8,−1] → **build/stepwise_display_4KLNDan.json (score 929383 = 2026-09-24 局) 是未随深夜 XVI 重挂更新的旧捕获** (与 stepwise_xiii_4KLNDan.json 同内容), 清册已按文件内容记录并加注。
6. **Negai 三次读取同一性 — counts/acc 级确证, ticks 仍缺**. negai.json / 2026-10-10_003356 / 2026-10-16_122337 三者 counts=[3360,1959,317,25,35,29] 与 acc=96.22 全同, Δ=0 vs tick 639264569108482118 (score 838363); 差异只在 reader 启发式字段 (legacyAcc 95.419/95.69/95.756, cappedTails 28/30/31, tosuBreaks 24/24/25)。playedTicks 无任何仓库字段 → 清册保留 null。
7. **azimuth 23-04 局 — 仍不可核** (读取器真值不覆盖). 12 份真值中 c8b6075b 前缀只有 1 局 (tick 639255930839151037, 2026-09-21); scores.db c8b6075b 共 8 局无一对应 2023-04-23; 该局仅存 build/stable-re/engine_az2304.raw 帧表 (82386 行, docs#1125/#1130/#1137/#1544)。结论: 需用户提供该 .osr 或 reader 会话方能入册。
8. **档案命名口径差** (非错误): docs#1125 称 azimuth c8b6075b 为「2026-10-09 局」, 其 ticks 反解为 **2026-09-21 21:11:23 (+08)**; 清册以 ticks 为准并加注。

### 3. 锚点全量清册 (交付物)

- **build/anchor_scores_inventory.json**: totalAnchors **32** = V1锚点 6 / V2锚点 6 / 真值图 6 / 重挂局 2 / 显示捕获 12;
  identityVerified true 29 / false 2 (两条 Negai 重放捕获无 playedTicks) / null 1 (azimuth 23-04 局);
  calibratedAcc.value 21/32 / replayEndAcc 任一字段 31/32 / ourShip.dACC 27/32 / identity.md5 31/32 / playedTicks 29/32。
- 清册内 **verification 块** = 本次校核原始记录 (12 rows + 7 resolvedDoubts + 3 unresolved + 3 newFindings)。
- 排除项 (excluded 10 条): 引擎 fixture / 非锚点 bindsim 回放 39 局 (A33+B5+C1) / 7 局 mismatch 回放 / V1 对照图 (ende..piano, MachineGun) / Swan·VibroLovers·Believe / 纯注册表汇总表 / 已作废 stepwise_corrected_declared_table.json / notelock 观测产物 / 同 md5 未承担锚点身份的其余局。
- gate: 判定零改动 (本轮纯校核+入册), bz 95 / azimuth 114 / V1 全表 / V2 六图 ≤0.088 维持。
- 数据: test/sample/*.json (8) / test/sample/new/*.json (4) / build/allrows.json / build/anchor_scores_inventory.json。

## 2026-10-16 深夜 XXXI: 待办#4 终局 — cap 判别式双重否证 (bindsim 审计 + 帧格假说), 维持现行 cap

### 1. 判别式审计 (Sub-G, bindsim 模型 vs 探针铁证)
- 待办原文「#1394 不封顶反例家族判别式」的可行前提重审: XXV 判词「帧域不可见」立于 XXVIII 之前, 本轮用落地的绑定判别式 (滑窗+Loop A 仲裁+按键沿+窗口门) 重试「cap = 灰按被绑定」。
- bz 帧域灰按全集重建: **1016 例** (引擎窗 [start−173, −136)); 提供文件 stepwise_greypress_cases.json (59 例) 实为 OD4 窗 [−176,−139], 50/59 在引擎窗内 — 该文件窗常数与引擎不符 (已入档)。
- 探针交叉表 (274 条, 覆盖 notes 0-511 + 4 分段): predBound∧写 **14** / predUnbound∧未写 **253** / **predBound∧未写 7** / predUnbound∧写 0 → 一致率 97.4% 但 7 假阳性**恰含全部 5 例误伤** (+同族 #1401 −173 / #6187 −152)。
- **5 例与 #344 的 Loop A 仲裁轨迹完全同构** (slot=本 note, 更早 note 已绑 → 列终结) — 同构而结果相反 = 直接反证。13 项帧域特征 (深度/按压时长/前后按松间隔/前 note 绑定偏移·跨度·类型/body-live/note 跨度/400ms 按数/a4 数) 单特征最优误差 ≥9/274, 无分离。模型保真 2476/2505 = 98.8%; 探针 0xA4 值 vs 帧 down 边沿 0ms — 非噪声。
- azimuth 推演 (无探针): 1855 例 → predBound 87 / predUnbound 1767; predBound 行走 = 保 22/23 修复、丢 #17795、5 例新增误伤仍误伤。
- 行走预演: predBound 替换「碰过」→ **bz 95 (零变化, 7 修复全保住但 5 误伤全保留)** / azimuth 115 (−1 退步) → 零收益。oracle 对照 (假想可读 0xA4 真值): bz 95→**90** (5 例全恢复零损失) = 本家族收益的硬上限。

### 2. 帧格量化假说 (Sub-H H1) — rejected
- 5 种口径重算窗口余量 m=d−173 (原帧/前帧 GameTime/后帧/固定 16.67ms 帧界/15.625ms 网格): 全部不可分 (maxFP vs minTP = 0/−34 … 2.375/−38.625, overlapGap 恒负)。**决定性反例: FP#1401 与 TP#486 同为 m0=0; FP#4635/#4690/#5686 与 TP#344 同为 m0=−5** — 任何余量函数不可分。
- 最小误差阈值恒 = 7 (与现状同分, 零增益); 1 帧阈值下 21 条全判未绑 = 14 修复全丢 + 253 灰触碰全取消 (规则自毁)。同帧和弦率 FP 14.3% = TP 14.3% (基线 6.1%) → chord 分配次序假说无信号。
- **前提反证: engine_bz.frames.csv 与 .indep.frames.csv 逐行全等 31591/31591 → 不存在两条时钟。**

### 3. H2: 假阳性机制已解释 (个案级, 非判别式)
- **#6187 灰按帧 240081 被同列前一条 rice #6183 消费** (探针: #6183 唯一 0xA4 写 = 240081 = 自身 +15ms; 该帧 col1 下沿即此按); bindsim Loop A 误记 #6183 已于 239887 结算 → 把同帧按让给 slot=6187 → 假阳性。#1401 同构 (灰按帧 57310 == #1394 的 A4 写值)。
- 但 #1786/#5897 (2/14 真阳性) 与 248/255 条未写案例同具此特征 → **机制解释个案, 不构成可用判别式**。深夜 XXIV 遗留「#4635/#4690/#5686 帧域无灰按」就此了结: 灰按存在 (−168) 且未写, 当时 fixture 截断所致。

### 4. 终局
- **待办#4 收官: cap 判别式不可行 (双重否证), 维持现行「碰过即 greyTouched」cap 零改动。** 5 例误伤 = cap 的不可约成本 (与 XXV 一致, 本轮以 2×独立方法 + 13 特征 + 5 口径加固)。
- 残差收益硬上限: bz 95→90 / azimuth 114 不动 — 唯一路径 = 逐 note 0xA4 真值 (全图探针一次播放 [需缓冲 512→全图] 或 osu!.exe 条件链 IL 定位), 均未排期。
- 远期线索 (不构成行动项): Loop C (f2641==2) 的 f11856 Find 重绑定语义; stable 帧内多按结算次序的精确重建。
- gate: 判定零改动 (纯审计, backend/hit_matcher 未触碰); bz 95 / azimuth 114 / V1 全表 / V2 六图 ≤0.088 维持。
- 数据: build/stable-re/{cap_discriminator_audit.json (418KB), cap_discriminator_h1_test.json (107KB)}

## 2026-10-16 深夜 XXXII: replay 绑定计算合规审计 — 引擎 ≉ 判别式字面但真值更优 (3-13x), V2 合规

> 触发: 用户指令「#5/#6 排除出校验清单, 检查 replay 绑定计算是否已合规」。审计零改动 (未重建/未碰 backend)。产出: build/stable-re/binding_compliance_audit.json (222KB)。

### 1. 逐项映射 (17 条: 合规 5 / 有意偏离 5 / 偏离 7)
- **合规 5**: 门2 已判跳 / 门3 帧内列处理 (loc4) / 门6 按键正沿 / press 块 f1109 写 / OD 窗映射。
- **有意偏离 5** (档案行号齐): ① LN 头早灰不消费 (doc 372-373; XXXI 判别式不可行 → 多数派近似) ② rice 早灰 endemize consume (doc 373/105) ③ LN 体深断重握 (doc 772-789) ④ 双写封顶 greyTouched (doc 1263-1294/1334-1345/1621-1645) + M4 release 侧 LN 终判 (doc 1494/1506/1515/1520-1522)。
- **偏离 7** (无档案判据): **M1** 步骤A 滑窗缺失 (引擎 = pending 队列 + 每帧 sweep, hit_matcher.cpp:113-121/600-631); **M2** Loop A 槽仲裁完全缺失 (引擎 = pending 前向首候选扫描, :744-963 — 判别式核心「jack 归属」无落点, 最主要); **M3.2** bindsim 缺 loc4 终结副作用 (bindsim.mjs:157/174); **M3.3** 早界常数 w.miss(−173) 代 f11843(180) (:764); **M3.4** press 门上界 rice=w.ok / LN=w.meh 代 f3114·(LN∧replay?1.5) 且无 m18911 重臂 (:808-810/934-946); **M3.6** replay slot[c]==note 门缺失; **M11** V2 头 sweep/提交界 = w.miss vs 旧规格 start+ok (:213-217/604-620/1064-1066, 仅引擎内实证)。

### 2. 三图实证 (frameIdentity 1.0000 = 同局; 引擎 exe sha 9a5a7c4c… = walkF ship)
- bz 7270: 主口径 **97.69%** (168 分歧) / 精口径 (真按头绑定) **98.50%** (109); TrueBlue 2106: **99.91%** (2) / **100%** (0); azimuth 18406: **98.59%** (260) / **99.09%** (168)。
- 分歧机制: 归属互换 (LoopA/门5) bz 50 + az 78; LN 头 sweep 界 (体中段接按) 41 + 79; release 侧 LN 终判 60 + 92; rice 上界 17 + 11。
- **真值仲裁 (game.judge)**: 引擎 59/2/65 vs bindsim 209/0/287; 纯绑定层 rice 子集 引擎 9/2500、0/234、6/7493 vs bindsim 63/2500、0/234、79/7493 → **判别式字面实现比现行引擎差 3-13 倍** — 引擎偏离是「更优近似」, 不是缺陷。

### 3. V2 轻查: 合规
hit_matcher.cpp:976-1048 与 §绑定分界线三段公式逐段一致 (k=未判定序列 start≤t 最后者 / 三段边界), 探针双图 510/510+510/510 背书; 仅邻接参数偏离 (V2 头 sweep=w.miss、MRA_V2_PMISS 默认关, 引擎内实证)。

### 4. 判词与决议
- **判词: 字面不合规 (偏离 7), 实证合规 (真值更优 3-13x) — 引擎 = 判别式的「更优近似实现」, 判别式字面 = 参考语义而非实现规格。**
- 决议: ① 不替换引擎 (ship 维持, 回滚点不变); ② M1/M2/M3.3/M3.4/M3.6/M11 补档为「引擎侧等价/近似实现」(本节即档案); ③ 可选后续 (未排期): MRA_V1_DISCRIMINANT 开关 (默认关) 做判别式 A/B; bindsim 补 loc4 终结副作用 + grey/consume 两处; ④ TrueBlue 局次溯源: **testset.json TrueBlue 条目 ticks = …79287 (2026-09-20 20:37:16 旧局, score 911860) — 非新局锚点 …445 (20:40:51, 943113)**; 两局相隔 215 秒; testset 基线 −0.038 系旧局自洽记录, 指纹回归不受影响, 但 truth 对照 (true blue.json) 属 …445 新局 — 溯源需知情。
- gate: 判定零改动; bz 95 / azimuth 114 / V1 全表 / V2 六图 ≤0.088 维持; 引擎 exe sha 9a5a7c4c… 未变。
- 数据: build/stable-re/binding_compliance_audit.json (222KB, 17 映射 + 三图逐 note 分歧明细)。

## 2026-10-16 深夜 XXXIII: cap 判别式复活行动 — 常数轴/钩子轴双落定, 三重否证终局

> 触发: 用户问「灰按锁死即使逆向一部分也没法探明吗」。答: 探明了 — 双路并行 (静态 IL 解码 + fsmwatch 实读), 结论 = 判别式复活不可能, 差异在结构性结算次序。

### 1. 常数真值 (Sub-J 静态, IL 写点排他复核)
- **f11843** = 188 − 3·OD (回放分支, rid18889 经 rid18888 分段线性; 构造器占位 178; 无回放分支 = 字面 158/PlayModes==3 同公式) → **bz OD5 = 173** — 引擎 w.miss 本来就对; **bindsim HEAD_M=180 是错替代 (勘误)**。
- **f3113** = 151 − 3·OD = **136** (OD5; 角色: Loop A 仲裁余量 + LN 50 档 + rid18908 释放窗) — **bindsim ARB=50 偏差 86ms (勘误)**。
- **f3114** = 127 − 3·OD = **112** (门5 上界 ×(LN∧replay?1.5) + LN 100 档) — **bindsim PRESS_M=150 偏差 +38ms (勘误)**。
- **f3112 = 动态**: Max(节拍派生 ms, f11848=最近 note hold 时长)+200 (rid18897); 基类分支 1800/1200/450; 无 beatmap 分支 1500。滑窗前后界 = f3112+200 / 字面 300。
- **f2641** ∈ {1,2,6,7}: 1=待发令 → 2=游玩中 (rid3725 在 GameTime≥f2627 时切换); FSM 入口 f2641==2 才继续 → Loop C 仅游玩态可达。
- fsmwatch 实读佐证: 205 组稳定三元组中 v3∈[168,173] 恰 2 组 (281/68/105/170, 292/22/183/171); 真三元组 (·,136,112) 的 v1=136 超出扫描带 [20,80] → 未入册 (工具按设计工作; Sub-J 静态值为准)。

### 2. 四钩子全解 (零副作用)
m17382 = 1 字节 ret 空体 (t304/t2024/t995 链无覆写) → 门序钩子改状态假说当场否证; m17391 = 只播音零字段写; m18923 = 纯谓词 (q.f5491≤G ∧ col 匹配); m18911/m11922 = 重臂/提交只落账, 不写 f1109/f1111/f11030/f11856。press 绑定唯一写点 = FSM rel620-633 不变。

### 3. 7 假阳性真常数重评 — 0 条翻案
真窗 f11843=173 下余量 +2/0/+1/+5/+5/+5/+21 → 全部仍进门4 (#1401 恰在边界, blt 取等仍进); Loop A 对这 7 条「本 note 占槽 + 前一条 bound-terminate 终结」→ deadline 分支从未执行 → f3113 对它们不可见; 钩子零影响 → **与 #344 在常数/钩子两轴仍完全同构**。

### 4. 终局 (三重否证)
XXXI 模型审计 + Sub-H 帧格量化 + 本轮常数/钩子真值 — 三路独立否证后, 剩余差异定性 = **结构性偏离 (M1 滑窗/M2 仲裁缺失/M3.2 loc4/M3.6 slot 门) + 帧内多按结算次序** (XXXI §3 个案机制的机制类)。帧域可计算判别式不存在; 唯一理论路径 = bit-exact 帧驱动+FSM 全链仿真 (= 重写 stable mania 判定栈, 无收益比)。**维持现行 cap 零改动; 5 例误伤 = 不可约成本; oracle 上限 bz 95→90 不变。**
- gate: 判定零改动 (双路均只读+纯工具); bz 95 / azimuth 114 / V1 全表 / V2 六图 ≤0.088 维持。
- 数据: build/stable-re/{fsm_hooks_constants_decode.json, fsmwatch_tooling_report.json} / output/fsmwatch_bz.jsonl (205 组扫描); 工具: memprobe.exe fsmwatch (备份 pre-fsmwatch-backup)。
- 附带产出 (bindsim 精化方向, 未排期): HEAD_M 180→173 / ARB 50→136 / PRESS_M 150→112 / f3112 动态化 — 修正后 bindsim 需重跑 35 tier-A 批量验证。

## 2026-10-16 深夜 XXXV: 会话收尾 — 灰按绑定机制挂起, 新会话目标 = 剩余残差处理

- **挂起声明 (用户指令)**: 灰按绑定帧域不可判别族 (bz ~90 不可观测 + 锁死族 + cap 5 误伤 0.023pp) 四重否证后挂起。恢复条件 (任一): ① osu-stable-reference 正文 (私有仓, 人工路径 5 条 + mechanismFirstPass 3 假设已预注册) ② 全图 0xA4 探针 (缓冲 512→全图) ③ bit-exact 全链仿真 (不建议)。
- **新会话目标 (HANDOFF §5 已重写)**: 剩余残差处理 — ① LN 尾/release 侧精化 (官方 LN 合成档位表 ×1.2/×2.4 等已固化, 攻 LN Dan III 110 与 LN 重图) ② bindsim 常数修正 (173/136/112/f3112 动态) + 35 tier-A 重验 ③ DT 时基口径对齐 (~2%) ④ 直调 backend walkF 为 LN 权威。#5 piano / #6 vibro 已排除。
- 交接文档: HANDOFF.md 全量重写 (状态/口径/证伪清单/挂起与恢复条件/新目标/工具索引); 锚点清册 anchor_scores_inventory.json 32 条随附。
- gate: 判定零改动 (全程); bz 95 / azimuth 114 / LN Dan III 110 / V2 六图 ≤0.088 维持; ship 与回滚点不变。
- 数据: build/stable-re/{cap_discriminator_audit.json (418KB), cap_discriminator_h1_test.json (107KB)}; 输入 probe output/bind_watchobj_w{1394,2418,4635,5686}.jsonl。

### 5. 网格终实验 (Sub-L): 125 组实测常数 + 36414 组家族扫描 — 分离点不存在
- 重建帧域模拟器 (G=帧 timeMs, 帧序真相); 旧猜值下 1016/1016 caseDetails 与 audit 逐条一致 = 移植保真, 交叉表复现 14/253/7/0。
- 网格: 205 组 fsmwatch 实读 (41 带 v3 + 164 无 v3 ×{170,171}) + 候选A/B + 旧猜值 + 引擎 = 373 组原始 / 125 组去重; 另 **36414 组家族扫描** (f11843∈[150,200] × f3113 × f3112 × f3114)。
- 关键交叉表 (274 条): 旧猜值 tp14/fp7/fn0 = 0.9745 (复现✓); 引擎 173 → tp14/fp6/fn0 = 0.9781 (只翻 #6187); 候选A (281/68/105/170) tp13/**fp3**/fn1 (翻 #1394/#1401/#2770/#6187, 丢 #486 −173); **网格最优 332/44/188/164 (capture#0x9B1ED8): tp11/fp0/fn3 = 271/274 = 0.9891** (7 假阳性全翻转, 代价 = 丢真阳性 #344(−168)/#414(−170)/#486(−173))。
- **结构性不可分铁证**: FP #4635/#4690/#5686 与 TP #344 同 off=−168; FP #1401 与 TP #486 同 off=−173 → 任何 f11843 深度规则对同深度对必然同判。f11843 单调敏感表 (h 140→200): fn13 → fp7, **权衡曲线无分离点** (fp=0 时最小 fn=3, 12852 组 fp=0 无一更优)。
- 帧量化变体恒等 (灰按时刻本就 = 帧 timeMs, G 吸附不改变任何判定)。
- **终判: 观测常数集 (含 36414 组家族扫描) 中判别式不存在 (separated=false)** — 与 §1-§4 合并, 复活行动四重收口 (XXXI 模型审计 / Sub-H 帧格 / XXXIII §1-4 常数钩子 / 本节网格)。
- 数据: build/stable-re/cap_discriminator_constants_test.json (82.7KB, 125 组 xtab + 36414 扫描 + 逐条 residual)。

## 2026-10-16 深夜 XXXIV: lazer 官方语义固化 — 三方案窗口恒等 + 判别常数三源确认 + 黄金基准评估

> 触发: 外部参照档案 (key-state-binding-external-refs.md) 行动项; Sub-N 执行 (lazer master @ 676f3b01, 经 jsDelivr/ungh.cc; GitHub REST 403 限流 0 调用)。产出: build/stable-re/external/{ManiaReplayFrame,ManiaInputManager,ManiaHitWindows,ManiaAutoGenerator}.cs + external_semantics.json (43.6KB)。

### 1. 三方案 OD5 窗口恒等 (代数 + 数值扫描)
- ① wiki 整数表 16/49/82/112/136/173 = ② 引擎 w.* (judgement.cpp 逐窗全等) = ③ lazer classic **16.5/49.5/82.5/112.5/136.5/173.5** (floor(x)+0.5 取整产物; 整数毫秒 offset 下 <=W+0.5 ≡ <=W)。
- GREAT 恒等: 34+3(10−OD) ≡ 64−3OD; good/ok/meh/miss 四条同构; OD 0..10 扫描舍入前 max|diff|=0。默认(非 classic)分支 PERFECT=22.4/19.4/13.9 — 固定 16 属 Classic/V1 分支 ✓。

### 2. ManiaAction 位序 — 项目约定成立, 但仅对 master
- master: Key1=0..Key20=19 (无 Special1/2; 历史 commit 0579780b 的 Special/Key1=10 布局已废弃, 且该布局下 ToLegacy/FromLegacy **互逆不成立** — 历史枚举不得用于本项目复核)。
- FromLegacy: MouseX 位 i → Key(i+1) → 列 i; 与 replay_frames.cpp:89/116-119 + hit_matcher.cpp:259/695 完全一致。
- 修正: bit8=0x100「开局伪按键」注记仅对 ≤8K 成立 (master 下 0x100 = Key9, >8K 为合法键); 匹配器忽略超键数列, 行为正确。

### 3. LN 阈值 — 引擎与官方逐档等价 (非差异); bindsim 角色修正
- 官方 (wiki A2): 头≤窗×1.2 且合计≤×2.4 → PERFECT / ×1.1·×2.2 → GREAT / ×1·×2 → GOOD·OK / 其余 MEH / 体段松键封顶 MEH。
- 引擎 tiers {Perfect 1.2, Great 1.1, Good 1.0, Ok 1.0} + win=W×rate + total≤2×win + 默认 Meh → **逐档等价**。早松键→Miss / 二次判定→200 / v1HeldthruExt = 参考判定器移植或 flag 门控。
- 修正 (对深夜 XXXII/XXXIII 表述): bindsim **无 1.2× 系数** — 其唯一 LN 系数 LN_MULT=1.5 只作用于按门 (与官方 1.5× 尾判定窗同数不同角色); lazer 佐证 TailNote.RELEASE_WINDOW_LENIENCE=1.5 + GetCappedResult 封顶 Meh → 引擎 V2 尾支为精确移植; V1 合成 1.2/2.4 档的佐证链 = wiki + mrm 参考判定器 (lazer 现行头尾分离模型中不存在)。

### 4. 判别常数真值升级确认 (X5 记账闭环)
f11843=173 / f3113=136 / f3114=112 @OD5 的证据链升级为三源确认: ① XXXIII IL 写点排他 (rid18889 公式 188−3·OD 等) ② osu-wiki 整数表逐值吻合 (提交注明 sourced from stable source) ③ lazer classic +0.5 取整产物同源。**bindsim/audit 的 180/50/150 基线自此为 superseded 记录** (历史运行自洽, 不改写); bindsim 常数修正仍未排期 (需 35 tier-A 重验)。

### 5. ManiaAutoGenerator 黄金基准评估 (仅评估未实现)
- 语义: RELEASE_DELAY=20; HitPoint@start + ReleasePoint@releaseTime; LN 严格 end 松开; rice 松开 = end+delay (无下一个) 或 end+0.9×gap; 同刻分组一帧绝对按住集合; 只走窗心不走窗沿。
- 判定: **可行, 且是 bindsim 绑定层建模的正确 oracle** — 但 autoplay 无仲裁 (Loop A 可合法绑同列另一物件而判定不变) → 只能做**聚合差分** (逐列按压区间分布/判定一致), 不能逐帧全等; 需统一 rate 口径 (谱面时间↔真实时间) + ~60 行移植 + 锁定引擎 v1* flags。未排期。

### 6. gate 与状态
- 判定零改动 (backend/src mtime 2026-10-10 20:44 不变); bz 95 / azimuth 114 / V1 全表 / V2 六图 ≤0.088 维持。
- 数据: build/stable-re/external/*.cs (4) + external_semantics.json (43.6KB, 11 顶层键 / 4 sources / 10 crossFindings)。
- Sub-M 猎取终局 (同日 21:2x): **未获取** — peppy/osu-stable-reference 为**私有仓而非删除** (osu! 论坛 topic 2112014?n=90 原文: 'private repo … only the core devs can access it'); 六路线全灭 (GitHub search 0 命中 / CDN 404 / Gitea 镜像无 / Codeberg·GitLab·Gitee 空 / 精确搜索 0 相关 / 存档站超时)。**关键否决**: 本地 osu!.exe ILSpy 反编译 (2907 类型/21398 方法) 证明类名系泄露者自命名 (2683/2907 混淆名, 方法名不可见 Unicode, HitCircleManiaLong 零出现) → 无法从 exe 反推源文件。源码级解释的球在人工侧: ①核心 dev 导出快照 ②invite 登录态 ③osu-wiki 历史版本上下文 ④论坛用户 (Railgun_/McEndu) Gist 碎片 ⑤放弃源码走行为指纹 IL 重命名 (高成本)。mechanismFirstPass 已预注册拿到正文后的 3 条即验假设 (head 前早退/消费分支 / LN head 绑定写入条件 / 列内待处理列表+deadline 取整)。数据: stable_reference_hunt.json (路线矩阵+23 条原始日志)。


## 2026-10-16 深夜 XXXVI: 剩余残差处理四目标闭合 — bindsim v2 (常数定稿 / LN 合计表逐项验证 / V2 尾机 / 时基裁定)

### 0. 仪器
- **build/stable-re/bindsim_v2.mjs** (v1 未动): ①常数 IL 真值化 HEAD_M=miss窗(188−3OD)/ARB=meh窗(151−3OD)/PRESS_M=ok窗(127−3OD) 逐局动态; ②滑窗动态 (rid18897 IL 重解): f3112 = Max(节拍项, f11848=**已注册物件 max(end−start) 运行最大值**) + 200, 切片器再减 200 → W_BACK = Max(scrollTerm, maxHold)+400 (滚动项为 HitPosition 派生, 皮肤相关 → --scrollms 默认 0); ③--tmode raw|xrate|sedge 时基口径; ④LN 单元判定 --lnmodel 消融族; ⑤--frames engine_*.frames.csv 输入 + --truth 会话逐物件对照 + --dumpln 导出 + --odfit 谱内隐含 OD 扫描 + countsL1 细锁。
- **bindsim_batch2.mjs**: 35 tier-A 驱动 (tmode/lnmodel/v2maxraw 可调)。数据: bindsim_batch2_final.json; LN 单元导出 lndump_{a5af,azdan,azmarathon,bz}.json; 规则网格 lnrule_grid.json (1728 变体)。

### 1. f3112/f11848 IL 终解 (修正档案 XXXII 记录)
- idx18896 (rid18897, t2548, 48 instr): `f3112 = Max((i4)idx8863(0, HitPosition), f11848) + 200` — **+200 在 f3112 本体内**; idx18896 无直接调用方 (虚派发, 开局 setup; 同方法写 f11855 = mods&16384 = ScoreV2 位)。
- idx18905 (rid18906, 滑窗切片器 60 instr): 二分目标 = `GameTime − f3112 − 200`, 前界字面 +300 → 总回看 = Max(节拍项, maxHold) + **400**。
- idx18893 (rid18894? 实 idx18892): **f11848 = max(end−start) 全物件注册时运行最大值** (非「最近」); ctor idx18877: f11843 占位 178, f11848 = int.MinValue。
- 「节拍项」实为 **HitPosition (f13966) 派生** (idx8862/8860/8859 链, 场地/滚动标度), 非图谱节拍 — 皮肤/设置相关, 模拟器以 --scrollms 暴露。

### 2. LN 头尾合计规则逐项验证 (任务①终局) — 官方表成立
- **reader eventTime 数据不可用于规则级验证**: 深夜 XXXV 前提「合计表待验」的 reader 时基烘焙赛 (58,501 LN 单元, 6 模型) 全体 ≤57% — reader head/tail eventTime 中位偏差大 (azimuth −51ms), 档位边界 (16/19.2ms) 全碎。reader 层只可用于聚合。
- **改用 bindsim 自身绑定时刻** (frames CSV/bindkeys 边沿 + 判别式) 后, 官方合成表 (头≤W×{1.2,1.1,1,1} ∧ 和≤2×W×r, endDiff 钳 meh, 早释超 meh→Miss, fxxking-ppy 合并) **逐图大胜**: azdan **98.92%** (rice 100%) / azmarathon 98.48/99.65 / bz 98.22/98.48 (LN/rice 逐物件精确率); 竞品全败 — 仅尾÷1.5 (68.6/72.8/71.6/…) / 取优 (52-56%) / 取劣 (81-87%) / v1 heldMax (44.7%)。**合计 ×2.4/×2.2/×2.0 验证成立; ×1.5 系数确证为 press 门专属** (m18907)。
- a5af (True Blue Repentance, 7K) 单图 LN 80.5%/rice 97.9%: 错档分解 heldThru 215 + 边界 119; 钟差核验 tail.eventTime−simR p90=+43ms 长尾 → **stable 尾对象释放后重臂/重绑族 (m18911)** — 同列快释快按的尾绑时刻 ≠ 物理松键帧, 归灰按/重臂不可观测域 (挂起域延伸)。
- **同图会话窗组逐局不同** (旧 trueblue 会话 od5 组 vs a5af od6 组; negai perfect=17/od6.5): 游戏内存窗=每局真值 (私建逐局校准), bindsim --truth verbatim 采纳; --osu 模式用公式窗 + --odfit 兜底 (Musubi .osu OD=0 为真值, 实为 offset 锁错)。

### 3. ScoreV2/pairwise 局 (任务①延伸, 意外主战场)
- **35 tier-A 内 14 局为 pairwise 计数** (countsSum = notes+LN): 段位 (δ-3/Neuro-Cloud-9/dan9/10th) 强制 ScoreV2 + 部分 ScoreV2 局 (Reciprocate/:D/Bon Voyage/Immortal/Dark Chocolate/short ver/600jiezou…); mods 位不可靠 (δ-3 mods=64 亦 pairwise) → **经验检测: countsSum==notes+LN**。
- bindsim v2 新增 V2 尾状态机 (引擎 V2 分支 lazer 精确核): 尾×1.5 判窗 / 迟释>1.5×ok→Miss / 早释超 meh→断尾留守·重按续 hold·再判封顶 Meh / flush=Perfect(干净)·Meh(断过) / 头未绑→尾 Miss。
- **释放尾 MAX raw 界 (engine tailMaxRaw 轴)**: lazer 缩放窗下 MAX 缺口镜像 (300↑/MAX↓); 扫描定案 **共识 raw ≤41 (≈good/2)** — dir 早24/迟41 证伪, Reciprocate 特异 32; --v2maxraw auto 逐局 countsL1 拟合 ∈ {24,32,41} (与引擎 MRA_V2_TAIL_MAX_RAW env 同源做法)。

### 4. 时基口径裁定 (任务③终局)
- 三口径 35 图仲裁: **xrate (判定窗×速率) 大胜** — DT δ-3 L1 38/194 (raw 1434/1460), HT ε-δ 248 (raw 2488); **sedge (边沿×速率, wiki 图谱时基口径) 惨败** (7114/7306/17230, MISS 爆炸) → 证伪。
- **结论: stable 回放判定 = .osr 真实时基帧 vs 谱面时基 note, 判定窗×速率** — wiki「DT/HT 不改窗」是 lazer/图谱时基语义, 不是 stable 回放判定实现。bindsim v2 默认 xrate。**NC 速率修正 0.75→1.5** (v1 bug, 本批无 NC 图)。
- offset 细锁: ±great 尖峰对窗边界不敏感 (a5af 锁 −5 真 +1) → osu 模式细锁改 **countsL1 最小化** (头部 counts=该局真值), truth 模式改逐档一致数。

### 5. 批量终局 (任务②③闭合)
- **ΣL1 33017 (v1 错常数) → 11602 (V2 尾机) → 6700 (xrate) → 434 (countsL1 细锁+v2maxraw auto) = 76×**; 4 图六向量全等 (Shift / Madoka's Starlight / Stage 2 Promised / Stage 3 Heaven), ≤20 有 14 图, ≤50 有 20 图, worst = Azimuth Dan 156 (MISS +78)。
- 残差重心 = **MISS 正差** (稳定复现于各图) = 灰按/亚帧归属不可观测域 (挂起族指纹, 与引擎 bz95/azimuth114/LN-Dan-III-110 同源), 非判定规则误差。
- 任务④裁定: bindsim v2 自身 LN 闭环达引擎级逐物件精度 (98.2-98.9% vs 引擎 ~98.7-99%), 分布级 LN 权威无需直调 backend walkF; 引擎 walkF 仍是引擎侧逐物件权威 (其 v1NearRel/lateCatch 系拟合件)。walkF 符号已不存在于现 backend 源 (现实现 = hit_matcher V1/V2 分支)。
- gate: 判定零改动 — backend/src 零触碰; 引擎锚点 (bz 95 / azimuth 114 / LN Dan III 110 / 雪月夜鐘 0 / V2 六图 ≤0.088) 全部维持; ship 与回滚点不变。
