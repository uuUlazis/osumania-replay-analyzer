# osu!stable 回放判定内部机制逆向 (绑定层) — stable-re-replay 工作文档

> 任务: 逆向 stable 的 mania 判定/绑定代码路径 (press→note 绑定顺序 / LN 状态机 / notelock / 显示计票层),
> 解释三个未解家族: ① 短 LN jack 链归属交换 ② good←great 中断封顶判别式 ③ V2 display 计票层。
> 约束: C:\osu!\ 严格只读; 不改引擎判定; 结论标注置信度 **[实测]** / **[推断]** / **[猜测]**。
> 工具链: build/stable-re/*.mjs (自写 PE/CLR 元数据解析 + IL 反汇编, 零依赖)。
> 状态: 阶段性落盘 (第 1 轮, 2026-10-10 凌晨)。

---

## 1. 二进制全景 [实测]

| 项 | 值 |
|---|---|
| 文件 | C:\osu!\osu!.exe, 4,541,216 字节, PE32 (x86), **.NET 程序集** (CLR v4.0.30319) |
| 元数据 | #~ 表流 + #Strings (632,984 B) + #Blob (151,024 B) + #US (708 B) + #GUID |
| 表规模 | TypeDef 2907 / MethodDef 21398 / Field 15621 / Param 23024 / MemberRef 6703 / TypeSpec 1326 / ImplMap 468 (P/Invoke) |
| 混淆 | **ConfuserEx 系**: 类型/方法/字段名全部改名为 `#=z...` / `#=q...` (base64 样); #US 字符串常量全部加密 (运行时解密); <Module>::.cctor 仅 11 字节 → 安装两个 AppDomain 解析器钩子 (ResourceResolve m16382 / AssemblyResolve m10699, 后者为**内嵌程序集按需加载器**: 名字→Base64→查内嵌存储→Assembly.Load / 写盘 LoadFrom) |
| 关键结论 | **IL 与元数据结构完好** — 方法体可自由反汇编; 不可恢复原名 (混淆为单向改名, 非可逆加密), 因此静态分析靠**结构锚点**而非类名 |

### 1.1 自研工具链 (build/stable-re/, 已可复用)

- `lib.mjs` — PE/节表/CLR 头解析, rva2off, hexdump
- `meta.mjs` — 元数据表定位 (含 coded-index 位宽修正: **编码值 = 行数<<tagBits 参与位宽判定**, 例: MethodDef 21398 行使 MemberRefParent/HasConstant/HasCustomAttribute/HasDeclSecurity 全部升为 4 字节 — 这是第一轮表对齐失败的根因), TypeDef/MethodDef/Field/TypeRef/MemberRef 读取
- `il.mjs` — IL 反汇编器 (操作码全量, token→可读名), `methodBody()` tiny/fat 头
- 经验: Constant 表 = 10 字节行 [type(1) pad(1) parent(4) blob(4)], 位于 0x33e8b0; TypeDef 行 = 18 字节 (fieldList/methodList 为 **2 字节**, 因为 Field/MethodDef < 65536)
- 用法示例: `node build/stable-re/xxx.mjs` (脚本自包含, 只读 osu!.exe)

### 1.2 已验证为死路/低产的锚点 (避免重复)

| 锚点 | 结果 |
|---|---|
| 判定枚举常量 {0,50,100,200,300,320} | **不存在** — Constant 表 3390 个 I4 常量里无此组合 → 判定分值是 IL 字面量/查表, 非枚举 [实测] |
| `ldc.i4 320` 扫描 | 仅 10 方法, 全是 UI 布局坐标/输入 flag 掩码 (m13449 是输入枚举器: 320=0x140 为键值掩码) [实测] |
| `ldc.i4 24.9f` (V2 max 窗公式) | 0 命中 → stable 的窗公式用其它线性形式/双精度 [实测]; 待试 64−3od 等整数窗常量 |
| `ldc.i4 512` | 77 命中, 无判别力 [实测] |
| 保留明文名 | #Strings 里残留本地化键字段名 (如 `Options_Graphics_ResolutionWidescreen`) — ConfuserEx 漏网, 可作类锚点 [实测] |

---

## 2. 【家族③ 破案】显示 acc = 实时计票公式 (IL 级实锤)

### 2.1 方法 m6626 (t888) — accuracy getter [实测, IL 全文见 §2.3]

```
static float GetAccuracy(Score s):                 # m6626, 静态方法, arg0 = Score 对象 (t2241)
    N = s.get_JudgementCount() (m16540)
    if (N == 0) return 1.0f
    if (s.mods & 0x20000000) != 0:                 # 0x20000000 = ScoreV2 mod 位 (f10592, op_Implicit)
        return (50*c50 + 100*c100 + 200*cKatu + 300*c300 + 305*cGeki) / (float)(N * 305)
    else:                                          # V1
        return (50*c50 + 100*c100 + 200*cKatu + 300*(c300 + cGeki)) / (float)(N * 300)
```

字段映射 (t2241 = Score 类, 字段区 f10585..10640):

| 字段 | 语义 | 证据 |
|---|---|---|
| f10587 | count50 | m6626 中 `×50` [实测] |
| f10585 | count100 | `×100` |
| f10589 | countKatu(200) | `×200` |
| f10586 | count300 | `×300` |
| f10588 | countGeki(MAX) | `×305` (V2) / 并入 300 (V1) |
| f10590 | UInt16 计数字段之一 | m9566 (结算 UI) 对其 `box UInt16 → Format` [实测] — 与内存侧 score+0x88..0x92 的 16 位计数互相印证 |
| f10592 | mods | `& 0x20000000` ScoreV2 位 |
| f10597 | score (int32) | 结算 UI 直接 Format |

### 2.2 推论 (对三个未解家族)

1. **显示 acc 就是「实时计票公式」**: V1 = 300 权重 / V2 = 305 权重, 读的是 Score 对象的实时计数短整型 —
   即读取器早已在读的 score+0x88..0x92 同一组字段。[实测 → 推论]
2. **家族③ 重新定性**: 深夜 XV 的「display 与落盘之间存在 stable 内部显示计票层」假设**不成立** —
   显示层没有自己的计票; display = counts 公式 (实时计票), 而落盘 (.osr/scores.db) counts 是**另一条
   序列化路径**写出的另一份计数。因此「落盘 ≠ 显示」的差异**只能来自写盘路径** (或实时计数在
   写盘之后仍被更新)。这与深夜 XVI 的经验规则 (display = 真实判定计票, save−display = +0.41..+0.62 同向) 完全相容。[推断, 建立在 §2.1 实测上]
3. **决定性实验 (读取器一轮可做, 建议 lead 排期)**: 对一张 V2 LN 图, 在 play 结束瞬间 (items 释放前)
   读 `score+0x88..0x92` 实时计数, 与该局 .osr counts 逐类对比:
   - 若 live ≠ saved → 落盘路径独立成文, 家族③收敛为「写盘 demote/重计票规则」逆向;
   - 若 live = saved → 用户显示值另有来源 (可能性低, 与 §2.1 矛盾)。
   同时轮询实时计数的时间线 (notesfast 思路), 看 V2 局 live counts 何时被写入 —
   若「持有过尾的 LN 尾」在实时计数里是 300/geki 而落盘写的是别的值, 差异点直接定位到写盘函数。[方案]

### 2.3 m6626 原始 IL (关键段, 供复核)

```
IL_98714: ldarg.0; callvirt m16540 (get_N); brtrue.s; ldc.r4 1.0; ret      # N==0 → 1.0
IL_9872a: ldfld f10592 (mods); op_Implicit; ldc.i4 536870912; and; cgt     # mods & ScoreV2
IL_98744 (V2 分支):
  f10587*50 + f10585*100 + f10589*200 + f10586*300 + f10588*305
  → conv.r4 / (m16540() * 305) / ret
IL_9878e (V1 分支):
  f10587*50 + f10585*100 + f10589*200 + (f10586 + f10588)*300
  → conv.r4 / (m16540() * 300) / ret
```

---

### 2.4 补充发现 (第 1 轮后半)

- **t2241 的计数短整型在整个程序集中零直接写入** (stfld/ldsflda 扫描 21398 方法全零) — 说明
  t2241 更可能是「结果/存档副本」对象 (显示与序列化读它), **实时计数写入发生在另一个类**
  (内存侧 gameplay+0x38 指向的 live score 对象, 类未定)。→ §2.2 的决定性实验权重更高:
  live counts (读取器) vs saved counts (.osr) vs display (t2241 公式) 三方对比, 一次实验定位
  「写盘重计票」发生在哪条链上。[实测 (零写入) → 推断]
- **m16554/m16555 = Score→字符串格式化器** (18 字段 Format + MD5 计算链):
  字段序 = [count300+geki? / c50 / cGeki / cKatu / c300? / cMiss? / 选手名 / hash...] +
  mods + 日期 (ToUniversalTime) — 与 scores.db/.osr 头部字段序高度吻合,
  可作为 **scores.db 格式审计的 IL 依据** (团队 declared 审计的旁证材料)。[实测 (IL) → 语义推断]
- m16543/m16558/m16559/m16536 = 同族格式化/属性方法 (读全部计数), 未逐一深挖。

## 3. 静态分析路线 (判定/绑定方法定位) — 进行中

### 3.1 已确认的结构锚点

- **Score 类 = t2241** (字段 f10585..10640): 计数/mods/score/acc getter (m16538/16540) [实测]
- **结算 UI 类**: m9566 (t1276) — 引用 Score 计数与 acc, 320 为布局坐标 [实测]
- **.osr score 帧处理候选 = m4510** (t573, 5130 字节, 50×7/100×8/300×13 字面量 + 回放帧字段) —
  疑为回放 score 帧 (携带 counts) 的读写路径, 待确认 [推断]
- 混淆器漏网明文: 本地化键字段名 (见 §1.2)

### 3.2 下一步锚点 (按性价比排序)

1. **hitErrors 列表写入点**: score+0x38 = List<int> hitErrors (内存实测)。IL 中对该 List 的
   `Add` 调用点 = 每次判定的记录位置, 其所在方法 = 判定主路径; 再顺调用图向上找 press 处理。[方案]
2. **判定窗常量整型形式**: V1 窗 64−3od/97−3od/... 为整数线性式, 扫 `ldc.i4.s 3` × `mul` 邻接
   `ldarg.0 od 字段` 的方法; 或扫 188/167/151/127 常量组合。[方案]
3. **内嵌程序集提取**: AssemblyResolve 钩子 (m10699→m10702/10703) 表明 exe 内嵌了按需加载的程序集 —
   需确认其中是否含 XNA/依赖 shim (主游戏逻辑应在主 exe, 21398 方法量级吻合) [推断]。
   提取后用同一套解析器读其元数据。
4. **字符串解密 — ✅ 已打通 (第 1 轮末完成)**: 见 §5。

---

## 4. 动态观测方案 (绑定结构) — 设计

目标: 把「press↔note 绑定顺序」变成可采集数据, 直接裁决家族①/②。

### 4.1 观测对象 (全部来自已有内存侦察 stable-memory-recon.md §2/§9)

| 对象 | 已知结构 | 绑定相关猜想 |
|---|---|---|
| HitObject (rice/LN 两类, stride≈1532) | +0x10 start, +0x14 end, +0x9C col, +0xA0 isLN, +0x80 judge, +0x84 done, +0xE0/+0xE4 状态 0..4 | **+0xE0/+0xE4 值域 0..4 且渐进翻转 — 疑为判定/绑定状态机字段** (未深究过) [猜测→待测] |
| 列对象 (7 个, obj+0x94 指向) | 未测绘 | notelock 锁定目标/队列头的宿主 [猜测] |
| 12 字节旁路数组 (§9.3) | [objPtr, ptr, ptr] × N, 与物件表同序 | 第 2/3 指针指向 obj+0x2xx 堆 — 可能是 drawable 或「判定↔物件绑定」记录 [猜测] |
| Score+0x24 List | 元素数≈判定数, 浮点对 (如 1123.0, 1.0) | **疑为 (时刻, 判定值) 显示计票流水** — 与 §2.2 实验③联动 [猜测] |
| Score+0x34/0x44 List | 容量 8192, 指针/浮点混合 | 未定性 |

### 4.2 探针设计 (memprobe 扩展, 只读)

1. **colwatch <intervalMs>**: 解链后枚举 7 个列对象 (从任一 obj 的 +0x94), 高频 diff 其前 0x100 字节;
   jack 段 (连续 2-3 根短 LN 同列) 播放时逐 tick 记录 — 找「当前锁定 note 指针」字段
   (值应在 press 时刻切换到被绑定 note)。判据: 该字段值 ∈ 物件表指针集合, 且切换时刻与 .osr 帧对齐。[方案]
2. **objdiff <intervalMs> <windowMs>**: 只对「当前时间 ±2s 窗口内」的物件做 0x00..0x100 全字段 diff
   (数据量可控), 捕捉 +0x80/+0x84/+0xE0/+0xE4 之外的**新跃迁字段** — 特别是「被绑定的 press 时刻」
   类字段 (值≈press 时间戳)。[方案]
3. **scorewatch**: Score+0x24/0x34/0x44 三个 List 逐 tick 增量 (§2.2 实验③用)。[方案]
4. **旁路数组定性**: play 中读 §9.3 数组的第 2/3 指针目标区 0x40 字节, 与 obj+0x2xx 比对; 若含判定值/
   press 时间戳即为绑定记录。[方案]

### 4.3 判读矩阵 (家族①)

对 bz col6 jack 族 (#293..#317) 回放:
- 若 colwatch 捕捉到「press 到来时列锁定对象仍是上一根未超时 LN」→ stable 有 notelock 排队,
  我们引擎的 flow 规则与它差异点即锁定切换判据;
- 若 objdiff 发现 press 时刻某物件新字段写入 = press 时间戳 → 直接得到 stable 的归属真值表,
  与我们的 findTarget 输出逐案 diff → 判别式可写。

---

## 5. 【突破】字符串解密已打通 (运行时反射路线) [实测]

### 5.1 结构 (IL 逆向)

- 混淆字符串访问形态: `ldc.i4 <id>; call m16032` — 全程序集 **9519 个调用点 / 6340 个唯一 id**。
- m16032 = 查缓存字典 (f10334) → 未命中转 m16033 (初始化器):
  由常量异或拼出资源名 → `GetExecutingAssembly().GetManifestResourceStream(name)` →
  包成自定义流对象 (m16038) → 按需解密条目 (m16045 = 取字符串, m16040 = 计数)。
- **manifest 资源 (反射枚举实测)**: `f99455430df64b4387099de160f2eb9a` (16 B = AES 密钥),
  `4e422e1ecc2d4ca278fc67948614a11f` (223 KB 主常量堆), `e26219084b984214e5b0e82add2e00d5`
  (44 KB, 名字即 #US 中那条明文)。
- **反篡改**: m16033 用 StackStackTrace GetFrame + DeclaringType 与 `RuntimeMethodHandle` 比对 —
  **直接反射调用 m16032 一律返回占位符 "X0X"** [实测] (防"从外部进程调用解密器")。

### 5.2 绕过 (已验证可复现)

**不直接调解密器, 而是调用 osu! 自己的静态字符串 getter** (它们内部调 m16032, 调用栈合法):
1. 用 .NET Framework 4.8 的 powershell.exe (非 pwsh): `[Assembly]::Load(osu!.exe bytes)` — 加载成功;
2. 扫 IL 找「static + 无参 + 返回 string + 方法体 ≤120B」的包装 getter (91 个, build/stable-re/getters.mjs);
3. 逐个 `Invoke` → 返回值 = 真实解密字符串。
- 实测产出: **100 KB 解密文本 / 1166 个本地化键** (build/stable-re/decrypted_getters.txt)。
- mania 相关锚点已在其中: `ConfigMania_OsuManiaSpeedSet / ConfigMania_BPMscale`,
  `Options_OsuManiaLayout / Options_ManiaSpecial_*`, `Options_UsePerBeatmapManiaSpeed`,
  AI 谱面检查警告族 (`AIComposeMania_HoldNoteTooShort` 等 — 编辑器 mania 规则文案)。
- 风险控制: 只调无参 static getter (≤120B), 未触碰实例/带参方法 — 无文件写入/网络行为;
  加载的程序集进程即退, 不影响运行中的 osu!。

### 5.3 下一步 (第 2 轮排序)

1. **扩大收获**: ① 实例 getter (对已知单例/配置对象); ② 1 参 static 方法 (传安全值); 
   ③ 反汇编每个 getter 引用的 id 集合 → 直接建立「方法 ↔ 字符串」映射表 (比 invoke 更全面);
2. 用 `ConfigMania_*` 键定位 mania 配置类 → 顺类型邻域找 mania playfield/判定类;
3. 解密字符串 (skin 元素名/键位配置名) 作为 IL 锚点, 完成 press 绑定/notelock 主路径定位;
4. 备选 (如需完整常量表): 纯 Node 复刻 m16033/m16038/m16045 流水线 (AES key 资源已拿到,
   需移植其自定义流解密 — 约 300-500 行, 仅在「锚点不足」时做)。

---

## 6. 置信度汇总 (本节随进度更新)

| 结论 | 置信度 |
|---|---|
| osu!.exe 为 ConfuserEx 系混淆 .NET 程序集, IL 完好可反汇编 | 实测 |
| 显示 acc = 实时计票公式 (V1 300 / V2 305 权重, mods&0x20000000 分支) | 实测 (IL) |
| 显示层无独立计票, 落盘≠显示的差异在写盘路径 | 推断 (基于上条 + 深夜 XVI 五图规则) |
| t2241=Score 类, f10585..f10592 语义 | 实测 (IL 交叉) |
| +0xE0/+0xE4 = 绑定状态机; 列对象持有 notelock 目标; Score+0x24 = 显示计票流水 | 猜测 (待 §4 探针) |
| 字符串解密可复现 (反射 + 包装 getter 绕过反篡改) | 实测 |
| 解密串含 mania 锚点 (ConfigMania_*/Options_*Mania*) | 实测 (1166 键) |

---

## 7. 第 2 轮进展 (bindprobe 落地 + 字符串表边界确认)

### 7.1 观测探针已落地可跑 [实测]

- `osu-mania-live-reader/tools/bindprobe.cs/.exe` (csc 编译通过, 只读内存, 与 notesfast 同款 JSONL 风格):
  命令 = cols / side / watchcols / watchobj / scorewatch; **watch 类自动等待 play 开始**
  (menu 中 JIT 代码不存在 → aob_not_found 是预期行为, 已实测), 最长等 30 分钟。
- 使用协议: docs/stable-bindprobe-protocol.md (采集步骤 + 判读要点)。
- 本轮实测边界: osu! 处于菜单且本会话未玩过 mania → JIT 判定代码不存在, 采集需等待下一次 play。

### 7.2 字符串表边界 [实测]

- m16032 (字符串入口) 的 9444 个调用点**全部是常量 id**, 无变量 id 包装 → 无法借一个方法解全表。
- 解密器 provider (t2190, 内嵌 Stream 包装类) 为**流式按条目解密** (anti-dump 设计):
  m16041=ReadInt32 / m16043=ReadExactly / m16044=Close; 完整明文从不整体存在。
- m16033 初始化在合法调用栈下成功 (getter invoke 实证), 但 stream 用完即关 (f10343=null)。
- 直接调用 m16032 受 StackFrame 反篡改拦截 → 占位 "X0X"。
- **结论: 全表解密需完整复刻 m16033 解密状态机 (~几百行移植) 或在真实 play 中批量触发;
  性价比低于动态探针 → 搁置, 备用方案已记录。**
- manifest 资源结构确认: `f99455430df64b4387099de160f2eb9a` (16B AES key) /
  `4e422e1ecc2d4ca278fc67948614a11f` (223KB 常量堆密文) / `e26219084b984214e5b0e82add2e00d5` (44KB)。
- FieldRVA 静态数据扫描: 仅见 AES S-box 等**资源解密表**与 UI 常量, **无判定窗口/分值表**
  (→ 判定窗为运行时计算, 与 mania_number 的线性公式一致)。

### 7.3 .osr 解码正确性独立验证 + MRM 参考算法对比 (用户指令, 全 PASS)

- **LZMA/帧解码独立交叉验证 = PASS**: 引擎源码外壳 (MSVC 原样编译 osr_reader + replay_decoder +
  replay_frames + LzmaDec, build/stable-re/verify_osr.exe) vs 独立 Python (lzma.FORMAT_ALONE +
  独立帧解析): azimuth 2 局 + bz + TrueBlue 共 4 份, 解压**逐字节一致** (SHA-256 全同),
  **210,047 帧五元组 0 mismatch**。勘误: stable .osr 解压体是文本协议 ("w\|x\|y" 逗号连接),
  非二进制 uleb; 首记录 = RNG seed 帧; 尾部 "0\|0\|0" 剥离; 各图开头有负增量引前段
  (引擎如实计数), azimuth 末段一次 −31ms 回跳为流本体特性。
- **Mania-Replay-Master 参考算法锚点试算** (忠实移植, build/stable-re/mrm_port.py):
  azimuth 94.615 / bz 90.763 / TrueBlue 98.820 — **MRM ≈ 我们 ship (Δ ≤ 0.13)** 且与 stable
  同向偏差 (+0.25/+0.81), 证实 naive 参考实现本身不匹配 stable (历次行走方向正确);
  其 V2 acc 口径独立复现「V2 显示 = 真实判定计票」 (落在 display 98.79 而非 saved 99.20)。
  详见深夜 XX 追补 2。

### 7.4 下一步 (第 3 轮排序)

1. **等一次 play → 跑 watchobj + watchcols 采集** (协议见 docs/stable-bindprobe-protocol.md) →
   家族①/② 的归属真值表 (lead/用户配合: 播放 Blue Zenith 0.9x 或 Azimuth Dan 回放)。
2. 采集分析: 绑定记录字段识别 → notelock 判别式 → 与引擎 findTarget 逐案 diff。
3. (备用) IL 静态: 以 watchobj 证实字段语义后, 反查引用这些字段的全部方法 → 绑定主路径 IL 定位
   (届时字段 rid 已知, 不再需要 TypeSpec)。

---

## 8. "osu!gameplay.dll" — 【已更正】文件存在 (31,854,304 B, mtime 2021-06-23), 为加密的真实游戏程序集 [实测]

> **更正记录**: 本节初稿曾结论「该文件不存在/单体构建」— **错误**。原因: 文件带隐藏属性或
> PowerShell 通配匹配缺陷导致两次 Get-ChildItem 搜索漏报 (文件名含 "!"), 深夜 XIX/XX 的
> 「单体构建」结论**撤销**。用户指出后已直接核验。教训: 关键存在性判断必须用 -Force + 直接
> Test-Path 复核, 不能只依赖通配搜索。

### 8.1 定性 [实测]

- PE32 托管 (CLR v4), 单一 .text 节 31,834,664 B; **全文件熵 7.72-7.98 = 整体加密**。
- 可见元数据只有 704 B 的表流 (TypeDef=2: <Module> + osu_gameplay.ResourcesStore;
  MethodDef=4; ManifestResource=1) — 一个 **ResourcesStore 桩**, "Copyright 2013",
  版本 1.0.0.0, 强类型资源访问器模板产物。
- **31.8MB 加密负载 = 真实游戏程序集** (推测置信度高: 体积与 osu! 游戏代码量级吻合;
  资源桩无业务逻辑; osu!.exe 在运行时解密并加载它 — 具体解密器位置未定位, 可能在
  osu!.exe 内或 DLL 的原生入口)。
- 当前运行中的 osu! 进程 (pid 43900, 00:51 起, WS 仅 9MB = 空壳状态) **未加载** 解密后的
  gameplay 程序集: 全内存 MZ 扫描仅 2 镜像 (osu!.exe + 系统原生 DLL), BSJB 扫描 0 命中,
  全地址空间仅 19 regions / 6MB 可读 — 证实 gameplay.dll 在进入 play 时才解密加载。

### 8.2 对逆向路线的影响 (重大)

- **家族①② (press 绑定/notelock/LN 状态机) 的 IL 不在 osu!.exe 里** — exe 中的 2907 类型
  是壳 + 遗留/辅助代码 (如 m6626 accuracy、计分格式化器)。绑定主路径必须在**解密后的
  gameplay 程序集**上逆向。
- **运行时导出方案已就绪** (memprobe scanmz / scanbsjb / dumpbin 已实现):
  进入 play 后, 解密负载驻留进程内存 → 全内存扫描 BSJB/MZ → dumpbin 落盘 → 用现成工具链解析。
  若 2013 年代的壳意味着 payload 未混淆, 类型名可能直接可读 (待验证)。
- **采集协议统一**: 下次 play 时并行三路 — ①bindprobe watchobj/watchcols (绑定真值);
  ②memprobe scanbsjb + dumpbin (解密程序集); ③读取器 (判定真值)。一次 play 全拿下。

### 8.3 此前的验证结论 (保留)


> 结论先行: 本机 osu! build 20260924.1 为**单体构建** — gameplay 代码就在 osu!.exe 内
> (即本轮静态分析一直针对的模块), 不存在独立的 osu!gameplay.dll。

| 验证 | 结果 |
|---|---|
| ① 磁盘全量文件名搜索 | C:\ 全盘 depth-3 搜 "osu!gameplay*" = 0 命中; C:\osu!\ 递归 *.dll = **0 个 dll** (整个安装无任何 DLL, 只有 osu!.exe + sb.exe) |
| ② 运行进程镜像扫描 (memprobe scanmz, 全提交内存) | 仅 2 个 MZ 镜像: 0x8D0000 = osu!.exe 本体 (~4.3MB), 0x77880000 = 一个原生系统 DLL (节 .data/.00cfg/.mrdata, 疑为 CLR shim) — **无 osu!gameplay.dll** |
| ③ 内嵌程序集存储解密 | osu!.exe 的 AssemblyResolve 内嵌存储 (manifest 串解密实锤) 只含 **1 个程序集**: 全名 ZX_6CF3E95F19724FB3213A664DC47D6443 / 文件名 zx_6cf3e95f19724fb3213a664dc47d6443.dll / 资源 4e422e1ecc2d4ca278fc67948614a11f (223,232 B, 'a' 标志 = m10700 XOR 流解密: 固定 8 字节键 [94,44,D0,34,F1,5D,C3,DC] + Base64 二次键 + 32 周期生成器)。**解密后解析: 它是一个纯 .resources 卫星程序集** (28 个 .resources 资源 + osu.Properties.Resources.resources, 无业务代码) — 不是 gameplay |
| ④ 本地数据目录 | %LOCALAPPDATA%\osu! / %APPDATA%\osu! 均不存在; Downloads 只有 .osz 谱面包 |

**方法论沉淀**: ①内嵌程序集提取配方已打通 (reflection 加载 osu!.exe → AssemblyResolve 枚举 →
m10703/decrypt → 落盘), 对未来任何 osu! 构建可直接复用; ② exe 的 AssemblyRef 表 18 行名字解不出来
(名字堆被混淆), 但内嵌清单证明外部依赖只有框架程序集。

**对绑定机制逆向的意义**: gameplay 主路径只能在 osu!.exe 内 — 三条线 (m4510 写盘 / hitErrors 追踪 /
0xA4 三态锚点) 继续以 osu!.exe 为对象; TypeSpec 表定位仍是 IL 泛型解析的卡点 (逐表行宽账目尚差
~5.7KB, 已排除 TypeSpec 缺失的可能)。
