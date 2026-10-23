# 判定计算报告 — osu!mania 判定/ACC 计算器（versions/0.1.2）

> 生成日期：2026-10-24。本报告阐述 0.1.2 版本内置判定/ACC 计算器的完整计算语义。

---

## 1. 概述

计算器实现 osu!mania（stable）的判定与 ACC 计算语义：

- **数值语义保真**：保留原始计算的精度路径（float/double 混用位置、整数截断点、
  运算顺序）与行为怪癖；不做算术化简、不引入 epsilon。
- 输入 = 已解码回放帧流（`ReplayFrame{timeMs, rawKeys, keyDownMask, keyUpMask}`），
  mania 按键掩码位于帧的 X 字段（bit c = 列 c）。
- 输出 = 逐物件判定事件、六档 counts（MAX/300/200/100/50/MISS）、acc（V1/V2）、
  等级、combo、总分。

## 2. 计算管线总览

```
回放帧流 ──► 按键写入（列状态 + 边沿生成）
        ──► 活动物件切片（BinarySearch 窗口）
        ──► 判定扫描（绑定/仲裁 → 判定提交）
        ──► 回放结束结算（未判物件按到期时刻补 tick）
        ──► 事件投影 (HitEvent/HoldJudgement) ──► 计分簇
        ──► counts (uint16×6) ──► acc (V1/V2) / 等级 / combo / 总分
```

每帧一 tick，帧内顺序固定：**输入写入 → 活动物件切片 → 判定扫描**（无条件执行）。
判定时间 `now = frame.Time`（逐帧钉回、可回跳、不单调）；DT/HT **从不缩放帧时间**，
只经判定窗因子进入判定。

## 3. 判定窗（四条互斥路径 + mod 因子）

| 路径 | 条件 | 公式 |
|---|---|---|
| A | ScoreV2 | 分段插值，锚点 22.4/64/97/127/151（±5/±8/±12/±15/±18 ms 刻度）→ MAX/300/200/100/50；NaN 走第一支；**提前输入上限不过 mod** |
| B | 原生 mania 谱 | 全程 **float**：`d = clamp(10f − OD, 0, 10)`（OD 先转 float），300=34f+3f·d、200=67f+3f·d、100=97f+3f·d、50=121f+3f·d、MAX=16、提前输入上限=158f+3f·d |
| C/D | 转换谱 | `Round(OD)`（ToEven）> 4 → 16/34/67/97/121，否则 16/47/77/97/121；提前输入上限=158 |

**mod 因子（窗口调整，四步全 double，最后一步才转 int32 并向零截断）**：
`HR ⇒ w/=1.4`（否则 `EZ ⇒ w*=1.4`）→ `DT/NC ⇒ w*=1.5`（否则 `HT ⇒ w*=0.75`）。
仅这四个 mod 改判定窗；窗口字段分属子类/基类两处（MAX/200 与 300/100/50）。

## 4. 绑定与仲裁（每帧一次线性扫描）

1. **活动物件切片**：BinarySearch 的键包装对象 `StartTime = EndTime = now−Approach−200`
   与 `now+300`，而比较函数按 **EndTime** 做三路比较（列表本身按 StartTime 升序）；
   未命中取 `~i`（第一个严格大于键的位置，不加 −1）；含 LN 时（EndTime ≠ StartTime）
   只能逐比较复刻该二分；切片为闭区间 ⇒ **多含 1 个 StartTime>now+300 的物件**。
2. **consumed[col]**：每列每 pass 只消费 1 个物件（一次按键边沿至多绑一个）。
3. **到期判定先于边沿判定**：`now ≥ End + W100`（V2 的 LN `W100×1.5`）→ miss；
   V2 下到期先补判未判的 LN 头（未按下 ⇒ 头 MISS 票）。
4. **怪癖照抄**：`IsJudged && StartTime > now` 的物件把该列标记为已消耗，
   **封锁同列本帧全部更晚物件**；ScoreV2 有"每列选一个"预选
   （`Min(obj+W50, cur−1) + Max(0, cur−obj−2·W50)`，从列表末尾向前）。
5. **按键边沿单 tick 生存期**：`WasHeld = IsHeld` 无条件先搬再比较；
   漏处理任何一个 tick 即永久丢边沿（引擎逐帧写入 + 对无帧 tick 显式重同步）。

## 5. note / LN 状态机

- **rice**：按下 → `|PressTime − StartTime|`（int）依次 `<=`
  WindowMax/300/200/100/50 → MAX/300/200/100/50；到期未按 → miss。
- **LN 头**（V2）：按下 → 窗口链同上；**未按下的头 → MISS 票**
  （档位默认值为 MISS，未按下的分支不清除它）；V1 恒无头判定（LN 单元计票）。
- **LN 尾**：松手时判定。V2 = `|Release − End| / 1.5` 窗链，破 LN
  （`WasBroken || !HeadJudged`）封顶 50；V1 = effStart/effEnd 合成，
  double 系数 1.2/2.4、1.1/2.2 与 int×2 混算，破 LN 封顶 200。
  提前松手 → 返回 −1048576 且**不写 IsJudged（可重复触发）**；
  三个返回 0 的守卫同样不写 IsJudged。
- **LN tick**：按住每 100ms 一档，档位值 7（进 combo/分数，不进六计数）。
- **Perfect mod**：只改判定函数的局部返回值（`r < 134217728` 才降为 −65536），
  不改物件记录的档位。

## 6. 计分 / ACC / 等级

- **counts 全部 uint16**（自增后截断，65535→0 回绕）；mania 总数为 6 项
  （MAX/300/200/100/50/MISS）；MaxCombo 为 int32、CurrentCombo 为 uint16。
- **V1 acc** = `(50·C50 + 100·C100 + 200·C200 + (C300+CMax)·300) / (total·300)`
- **V2 acc** = `(50·C50 + 100·C100 + 200·C200 + 300·C300 + 305·CMax) / (total·305)`
  - 两者均 **int32 算完 → 转 float → 单精度除法**（全 300 的 V2 局 acc = 300/305 ≈
    0.9836，表观差异非 bug）；零计数 ⇒ 1.0f。
- **等级**：`!Passed → F`；`acc == 1.0f`（精确单精度相等）→ XH/X；
  `(double)acc > 0.95` → SH/S；>0.9 → A；>0.8 → B；>0.7 → C；else D。
  HD/FL/FadeIn 只影响 X 与 S 两档；NaN → D。
- **V1 分数**：`mult = 500000 / ScoringObjectCount × mod倍率(mods 去掉 0x100458 位)`；
  每判定双重截断（`intScore := (int)doubleAcc` 再 `+=` 增量；结束时
  `(int)Round`（ToEven）覆盖；999999 → 1000000 特判）；`CountMiss` 被连续清零两次。
- **V2 分数**：`Round(200000·奖池/快照池 + 800000·acc^(2f+2f·acc)·(total/快照条数)) × 倍率`，
  指数为单精度运算、Round 为 ToEven；mania 奖池恒 0 → **0/0 = NaN → 转 int32 =
  −2147483648**（无除零守卫，照原始行为）。
- **combo**：每次判定按档推进/断连（7 与 −1048576 不加六计数，7 仍 combo+1）；
  `MaxCombo = Max(MaxCombo, 计数器+1)`。
- **acc 从不落盘**（scores.db 行与 .osr 头均无 acc 字段），显示值全部现算。

## 7. 计算怪癖清单（行为的一部分）

未来时间的已判定物件封锁该列 · 活动物件切片闭区间多含 1 个物件 · LN 提前松手
−1048576 可重复触发 · LN tick 100ms 一档且只前进一格 · counts uint16 回绕 ·
V1 分数双重截断 + 999999→1000000 · V2 奖池 NaN→INT_MIN 无守卫 · CountMiss 双清零 ·
按键位值 = `(int)Pow(2.0, Index)`（Index=31 → 0x80000000）· 计算中被丢弃的
`Math.Abs`/float 除法调用 · Perfect mod 只改局部返回值 · 全 300 的 V2 局
acc = 300/305。

## 8. 实现声明（原始行为不可恢复处的确定性选择）

| 项 | 处理 |
|---|---|
| 宿主时钟/帧序 | 一帧 = 一 tick，`now = frame.Time`；无音频时钟插值 |
| 回放结束结算 | 未判物件按到期时刻升序补 tick（V2 LN ×1.5） |
| 空帧快进（墙钟 >16ms） | 不建模（离线无墙钟，永不丢掩码） |
| ApproachTime 滚动预判 | 参数 `scrollPreemptMs`（默认 1000ms）近似，只影响切片下界 |
| SpecialStyle | .osr 不记录 → 引擎参数 `specialKeyRight`（默认 false；NoMod 下与物件侧位移相消） |
| `Math.Abs(int.MinValue)` / NaN·溢出转 int32 | 按 x64 `cvttsd2si` 确定行为（运行时未指定处的实现端约定） |
| BCL `.NET Random`（RD 列映射） | 按 .NET 经典算法实现 |
