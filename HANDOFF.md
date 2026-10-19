# HANDOFF — 判定语义收敛工程交接(2026-10-10 定稿; 2026-10-16 深夜 XXXV 更新)

> **权威语义文档: docs/acc-semantics.md (2026-10-17 定稿, V1+V2 acc 计算机制全量, 开发首选入口)**
> 探索中间件已归档: build/ 与 build/stable-re/ 的探针/记录 → archive/ (镜像原路径, 清单 archive/MANIFEST.md); 历史引用路径一律改读 archive/。
> 开场必读:本文件 + docs/v2-judgement-semantics.md **深夜 XXVIII–XXXIV**(上一会话增量: 判别式定稿/写盘路径/notelock 探针验证/cap 四重否证/lazer 语义固化) + docs/key-state-binding-external-refs.md(外部参照)。
> 锚点全量清册:build/anchor_scores_inventory.json(32 条, 含标定 acc/回放 acc/计算 acc/偏差 + 身份疑点; 校核 = 深夜 XXX)。

## 1. 项目状态(critical)

- ship 不变 = A+D+深断重握(185ms 判别式)+双写封顶;回滚点 build/hit_matcher.cpp.pre-walkF;env MRA_V1_* 可逐项回退
- 逐物件残差(当前):bz 95 / azimuth 114 / LN Dan Phase III 110(残差率 1.00%) / 雪月夜鐘 dACC 0.000 完美 / **V2 六真值图全部 ≤0.088 收敛**
- 锚点级残差(计算−标定, 现引擎 d389addb 2026-10-17 全量复验 = build/stable-re/v2anchor/v1_anchor_reverify.json): V1: bz +0.511 / azimuth +0.148 / 雪月夜鐘 0.000 / 10th Dan +0.189 / SCP +0.177 / LN Dan III +0.399 (10th/SCP/LN-Dan-III 较旧记录 −0.27/−0.34/−0.54 更优, 旧值系旧引擎 build); V2: 全部 ≤0.086 (trueblue +0.005 / 12th +0.001 / twinbloom −0.010 / negai +0.022 / 13th +0.061 / xfpsb +0.086) — 除灰按地板外规则侧无剩余未解释项
- 档案:docs/v2-judgement-semantics.md **深夜 III–XXXIV**;逆向:docs/stable-replay-internals.md;探针协议:docs/stable-bindprobe-protocol.md;外部参照:docs/key-state-binding-external-refs.md
- 绑定合规审计(深夜 XXXII):引擎 V1 绑定 ≉ XXVIII 判别式字面(7 偏离+5 有意偏离)但逐物件真值更优 3-13x;V2 分界线 walk 合规(探针双图背书);引擎不替换, 偏离已补档 = 「更优近似实现」

## 2. 对照口径与协议(critical)

- **用户给的参照一律是 stable 显示 acc**:V1 显示 = counts 公式(零例外);V2 显示 = accuracyV2。落盘 counts ≠ V2 显示 = 写盘路径把持有过尾 LN 尾按 300/彩档计(已 IL 实锤, 差 +0.41~0.62)
- **锚点入表必须过身份核验**(beatmapMd5 + playedTicks + counts N)——同名异图事故见深夜 X 与清册七疑点;reader.beatmap.beatmapId 是 scores.db 不具备的判别轴
- **证据层级**:读取器逐物件真值 > mania_number 文档 > counts 级标定;读取器 summary.acc = 用户「回放结束」acc(12/12 逐值一致)
- **标定 acc vs 回放 acc 差值 ≤±0.02 = stable 显示层内部微差**(对照容差, 与引擎残差无关, 勿混淆)
- .osr 解压体是文本协议 w|x|y, 60Hz, 解码双实现验证(210,047 帧 0 mismatch);V1 判定窗外部互证:MISS 188−3OD / MEH 151−3OD / OK 127−3OD(wiki, sourced from stable source)与解码常数 f11843/f3113/f3114 三源一致
- 逐物件对照口径:stable = notes[].game.judge(**head.eventTime 是启发式, 勿当 press 时刻**);ours = rice 取 head、hold 取最后 hold_tail 的 v1Judgement

## 3. 已证伪清单(禁止重试)

EAT 消费类;A2 最近松键结算;LN 体中段接住;全局 requeue;灰按立即全局封顶(Walk B;命中才封顶版已在 ship);判定窗查表假设;「stable 内部显示计票层」假设;顺序层绑定信号;短 LN jack 归属交换 = stable 内部绑定层(bindprobe 直读封案);gameplay.dll 运行时解密路线(XXVI 封案);**灰按绑定帧域判别式(四重否证: XXXI 模型审计 + 帧格 5 口径 + 常数/钩子 IL 真值 + 125 组实测 + 36414 组家族扫描——FP#4635/#4690/#5686 与 TP#344 同 off=−168 结构性不可分)**;**watchcols 列对象锁定信号(0x38 = 键/LN 状态位 7 值, 无锁定目标, v2 局不超集)**;**ManiaAction 历史位序(Special1=1/Key1=10 布局 ToLegacy/FromLegacy 互逆不成立, 禁用于复核; master = Key1=0..Key19)**

## 4. 运行时记忆

已全部写入(现 14 条, 含本会话 8 条增量)。开场只需对照 MEMORY.md 快照, 无待写入积压。

## 5. 状态与待办

### 🔒 挂起: 灰按绑定帧域不可判别族(bz 残差 ~90 不可观测 + 锁死族 + cap 5 误伤 0.023pp)
四重否证已定界: 差异 = stable 亚帧结算次序, 回放文件信息集外。**恢复条件(任一)**:
1. 拿到 osu-stable-reference 正文(HitObjectManagerMania.cs HandleInput; **私有仓**, 人工路径 5 条 + 拿到后即验的 3 条假设已预注册 → build/stable-re/stable_reference_hunt.json mechanismFirstPass)
2. 全图 0xA4 探针(watchobj 缓冲 512→全图改造; 一次播放全覆盖)
3. bit-exact 帧驱动+FSM 全链仿真(≈重写 stable mania 栈, 无收益比, 不建议)

### ✅ 已闭合(深夜 XXXVII): V2 锚点收敛裁定 — build/stable-re/v2anchor/VERDICT.md
- 6 局引擎残差 (harness 逐位复验): trueblue +0.005 / 12th +0.001 / twinbloom −0.010 ✅; negai +0.022 (灰板=8 头层亚帧翻转) / 13th +0.061 (灰板=24 hold-break 封顶尾) ⚠; xfpsb +0.088 (锚点自身显示 quirk −0.200, 对其计数公式 −0.112) ⚠
- V2 尾规则穷尽 (160 组合×4 局): lenience1.5 确证 / capgreat·固定maxraw·PMISS·NOREJUDGE 全证伪; 残差地板 = hold-break 标志族 (20/7/24/28) = 不可观测
- 仪器: bindsim_v2 修正 5 处 (V2MAXRAW 崩溃/S2 经验判据/counts 序/封顶方向); 「maxraw=41 共识」撤回 (误差抵消伪象→off); 批量记录更正 ΣL1=434→**1316** (旧值漏 14 张报错图), 35/35 解析, 4 图全等
- 引擎红线曾短暂破例 (用户令修 xfpsb): GetCappedResult 方向补丁实验**证伪回退** (六图反向恶化, 哈希 9a5a7c4c→c869→d389addb, 基线逐位恢复); xfpsb「显示 quirk」撤回 (屏显 96.66 = accV2(declared) 96.6623 精确, T3 算术错误); 引擎 +0.0858 系真实多记 (MAX+6/great+9/50+8/100−6/miss−17); harness = build/v1_harness.mjs; 12th 权威 ticks = 638178507594526492

### ✅ 已闭合(深夜 XXXVI): 剩余残差处理四目标 — 详见档案深夜 XXXVI
1. **LN 头尾合计规则**: 官方合成表**验证成立** — reader eventTime 数据不可用于规则级验证(档位边界全碎); bindsim 自身绑定时刻下 azdan 98.92%/azmarathon 98.48%/bz 98.22% 逐物件精确, 仅尾/取优/取劣/v1 全败; ×1.5 确证 press 门专属
2. **bindsim 常数**: HEAD_M/ARB/PRESS_M = miss/meh/ok 窗(OD 动态); f3112 = Max(HitPosition 派生项, **maxHold 运行最大值**)+200, W_BACK = +400; → 仪器 = build/stable-re/bindsim_v2.mjs
3. **DT 时基**: 裁定 **窗×速率(xrate)** — DT 38/194, HT 248; sedge(wiki 图谱时基口径)证伪; NC 速率 0.75→1.5 修正
4. **walkF 权威**: 裁定不必直调 — bindsim v2 自身 LN 闭环已达引擎级(98.2-98.9%); 引擎 walkF(现 hit_matcher V1/V2 分支)仍是引擎侧权威
- **35 tier-A 终局: ΣL1 33017→434 (76×), 4 图六向量全等(Shift/Madoka/Stage2/Stage3), worst=Azimuth Dan 156(MISS+78)**; 14 局系 ScoreV2/pairwise 计数(countsSum=notes+LN, 段位强制+自选), V2 尾机+释放尾 MAX raw 界 41(≈good/2, --v2maxraw auto) 已入; 残差重心=MISS 正差=灰按挂起族指纹
- ⛔ 已排除: #5 piano(−1.036) / #6 vibro(FUBUKI quirk 1.9pp) — 用户 2026-10-16 指令, 无限期搁置

## 6. 关键工具与地址

- bindprobe.exe / memprobe.exe(含 **fsmwatch 常数堆扫描器**, 备份 memprobe.exe.pre-fsmwatch-backup):osu-mania-live-reader/tools/(range/索引模式已支持; 双 osu! 实例必须 PID 直连; memprobe 参数只认十进制; bindprobe range hdr 非法 JSON 由解析器容错)
- 探针服务器随父进程退出被杀 → 所有 probe 操作单次脚本内完成
- **判读工具**:build/stable-re/notelock_v2.mjs(watchobj 0xA4 主线判定 + --osr 全溯源 + watchcols 伴随 + 垃圾闸门)/notelock_probe_semantics.json(0x38 位语义)/binding_compliance_audit.json(合规审计)/cap_discriminator_{audit,h1_test,constants_test}.json + fsm_hooks_constants_decode.json(判别式四重否证)/fsmwatch_tooling_report.json
- 语义档:build/stable-re/external_semantics.json + external/*.cs(lazer master@676f3b01 四件原文)
- 构建:scripts/build_backend.bat;读取器:node src/index.mjs --once(需 tosu 24050)
- 真值:test/sample*/ 12 份读取器 JSON + test/sample/new/ 会话(967fdec0×3 / a5af1629);锚点清册:build/anchor_scores_inventory.json