# 判定计算 · 已知问题与残差登记 (Issue Tracker)

> 纪律: 本文档遵循「机制与数据分离」——只登记问题、机制与修复方向, **不内嵌锚点局名与成绩数值**; 残差数据一律走指针。
> 权威机制: docs/acc-semantics.md · 残差定性: 其 §0.3 · 探索档案: docs/v2-judgement-semantics.md (深夜 III–XXXVII) · 复验记录: build/stable-re/v2anchor/ · 锚点清册: build/anchor_scores_inventory.json
> 状态标记: [待修复] 已确认存在且未修 · [地板] 回放信息集外, 规则侧无可修项 (恢复条件见条目) · [功能缺口] 支持缺失, 非机制错误 · [验证缺口] 机制已实现, 缺数据级验证

---

## I-1 [已修复-置换层] RD (Random) mod 列置换

- 登记: 2026-10-18 (用户实测确认; 前期伏笔 = docs/v2-judgement-semantics.md:317「ScoreV2+RD → 引擎缺 Random 列置换支持」、:328) · **置换层修复: 2026-10-19 (已验证)**
- 现象 (修复前): RD 局 (mods 位 1<<21) 判定整体错位——输入轨道未按 RD 随机映射后的列判定 (某 7K ScoreV2+RD 局 ACC 42.41% vs stable 97.26)。
- 根因与修复: backend 对 RD 零处理 (`kModRandom` 仅定义于 backend/src/replay/osr_reader.h:32, 全库无消费)。取证结论 (osu!.exe 为 .NET 程序集, IL 反汇编 m18881/m18890 + 原始按键终审): stable RD = .NET System.Random(seed) **逐列全域重抽直至未用** (`Next(0,K)=(int)(Sample()*K)` double 截断非取模; K 偶数>4 有首列特例未覆盖, 7K 不触发); 决定性验证 = 真实回放 MRA_TRACE 命中权威置换两串 ([1,5,4,2,0,3,6] / [6,4,5,3,1,0,2]) + 两局 scoreV2 97.32/97.61 vs declared 97.2561/97.5142 (dV2 +0.067/+0.098), 非 RD 回归无损。lazer 未复现 stable RD (LegacyScoreDecoder 只留 Todo), 本实现为独立成果。前端 mods 缩写 RN→RD 已同步。
- [残余→地板族] 置换正确后暴露 11-13 个 LN 尾 stable 记 good、引擎记 great 的归因差 (perfect/ok 与 declared 逐位相等, 算术吻合), 已证伪 1.5× 尾窗边界解释——属 I-2/I-3 尾归属地板族新信号, 留待判定语义 walk。
- 数据指针: build/tmp_accupdate/rd2_*.{mjs,py} (IL 反汇编/穷举/终审)、rd3_*.{mjs,log} (修正验证/尾归因诊断); osu!.exe IL m18881/m18890/m16542/m16567; docs/v2-judgement-semantics.md:317-318。
- 关联: I-5。

## I-2 [地板] 灰按绑定亚帧不可判别族

- 现象: 部分局的 MISS↔hit 归属差异无法用回放文件信息集内的任何帧域规则复现。
- 机制: stable 内部亚帧结算次序位于回放信息集外; 帧域判别式已被四重否证 (模型审计 / 帧格五口径 / 常数-钩子 IL 真值 / 125 组实测 + 36414 组家族扫描——FP 与 TP 结构性不可分)。
- 恢复条件 (任一): osu-stable-reference 正文 (HitObjectManagerMania.cs HandleInput, 私有仓) / 全图 0xA4 探针 (watchobj 缓冲全图改造) / bit-exact 帧驱动+FSM 全链仿真 (无收益比, 不建议)。
- 数据指针: docs/v2-judgement-semantics.md 深夜 XXXI; HANDOFF §5; build/stable-re/cap_discriminator_{audit,h1_test,constants_test}.json、fsm_hooks_constants_decode.json; build/stable-re/v2anchor/VERDICT.md。

## I-3 [地板] hold-break 内部标志族 (V2 尾残差地板)

- 现象: V2 局断尾后 stable 内部 hold-break 标志的可见性差异构成残差地板; 引擎尾规则矩阵已穷尽 (160 组合×4 局), lenience 1.5 确证, capgreat / 固定 maxraw / PMISS / NOREJUDGE 全证伪。
- 机制: 断尾标志族 (各局 20/7/24/28 量级) 在回放信息集外不可观测。
- 数据指针: build/stable-re/v2anchor/VERDICT.md、engine_flagmatrix.json、rulesweep.json。

## I-4 [地板] stable 亚帧结算次序 (逐物件残差地板)

- 现象: 绑定/尾归属存在不可复现残差, 逐物件 0.09–1.3% 量级; 规则侧无可修项。
- 机制: acc-semantics.md §0.3 已定性。
- 恢复条件: 同 I-2 (信息集外)。
- 数据指针: acc-semantics.md §0.3; build/v2anchor/ 各局复验记录; build/anchor_scores_inventory.json (逐锚点残差)。

## I-5 [验证缺口] 变速 (DT/NC/HT) 判定: 机制已实现, 缺数据级验证

- 现状: backend 已实现「判定窗 × 速率」(MatchOptions.rate; DT/NC ×1.5, HT ×0.75; 只放窗不碰帧/边沿, 与仪器 bindsim_v2 --tmode xrate 同口径), 并有 rateWindows 单元测试; 引擎内 OD 反推用未缩放基窗, V2 perfect 窗 floor 后同乘速率。
- 缺口: 锚点集与真值集全部为 1.0x 局——**没有任何变速局的锚点级/逐物件级验证**; RD 之外的这一功能缺口仅剩验证面。
- 修复方向: 收集变速真值局 (读取器会话 + declared counts), 按 §7 纪律走锚点级 + 逐物件验证; 重点关注 V2 perfect 窗「floor 后 ×rate」与「×rate 后 floor」两种口径的判别。
- 数据指针: backend/src/mania/hit_matcher.cpp (rate 缩放段); backend/tests/test_mania_core.cpp (testRateWindows); docs/v2-judgement-semantics.md:328。

---

## 维护纪律

1. 新增 issue 只登记「现象/机制/修复方向/数据指针」, 不写锚点局名与成绩数值;
2. 问题关闭时在条目标注关闭方式 (修复 commit / 证伪归档 / 合并至其他条目) 与日期, 不删除历史条目;
3. 本文档与 docs/acc-semantics.md 的分工: 那边只载已定稿机制, 这里只载未闭合问题; 两边互不混写。
