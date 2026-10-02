# osu!mania Replay Analyzer — Agent Development Guide

> 本文件用于指导代码代理/开发代理实现 `osu!mania Replay Analyzer`。
> 目标不是营销网站，而是一款 Windows 本地桌面分析工具：React + TypeScript + Vite 静态 SPA，C++ 本地 HTTP 服务负责所有文件系统访问与 osu! 数据解析，Windows WebView2 承载前端，无 WebView2 时回退到系统默认浏览器。

## 0. 核心目标

实现一个专注于 osu!mania `.osr` 回放分析与回放预览的本地工具，满足以下核心能力：

1. 直接导入 `.osr`，再选择对应 `.osu` 谱面；音乐文件可选。
2. 第二阶段增加 `scores.db` 只读导入，并允许用户绑定 `Songs` 目录后从数据库中的回放/成绩定位实际谱面。
3. 能可靠解析 `.osr` 元数据、压缩 ReplayData、输入帧/按键状态。
4. 能将 `.osu` 中的 osu!mania hit objects 与回放输入按列、时间、Hold 状态进行匹配，得到可复核的全局与局部判定结果。
5. 全局至少提供：ACC、判定计数、逐击偏移、平均偏移、偏移分布等。
6. 回放预览必须能根据谱面、回放、可选音乐和所选皮肤渲染 osu!mania 游戏区域；支持进度拖拽。
7. 在同一条进度轴上提供“点击跳转 / 长按框选局部区间”的交互；松开长按选择后，预览时间点停留在松开位置。
8. 局部区间可即时计算 local ACC、local offset、判定计数、偏移分布等。
9. 所有对 osu! 用户数据的操作均为只读：不得修改、覆盖、移动、删除、重打包 `.osr`、`.osu`、音乐、皮肤、`Songs`、`scores.db` 等源文件。
10. 分析过程尽量内存化；不得为了“导入”而复制/改写用户的 osu! 文件。

---

# 1. 产品边界与只读原则

## 1.1 输入源

### 第一阶段：直接回放导入

用户可以：

- 通过原生文件选择器选择 `.osr`；
- 从资源管理器将 `.osr` 拖入 SPA；
- 直接从资源管理器选择/拖入 `.osu`；
- 可选选择音频文件（例如 `.mp3`、`.ogg`、`.wav` 等）；
- 选择皮肤目录。

直接导入 `.osr` 时，用户必须额外提供对应 `.osu`；音乐文件可以缺失，此时回放预览仍应能够无音频工作。

### 第二阶段：`scores.db` + `Songs`

用户先绑定 `scores.db` 和 `Songs` 目录：

1. C++ 只读解析 `scores.db`。
2. 前端按 beatmap / score 列出本地成绩。
3. 用户选择一个 mania score。
4. 根据 score 中的 beatmap MD5、score metadata、必要时 replay MD5 等信息在绑定的 `Songs` 目录中定位 `.osu`。
5. 找到谱面后从该谱面所在目录解析音频与皮肤相关资源。
6. 回放数据的获取方式应由实际数据库/本地 replays 文件布局决定；不要假设 `scores.db` 内直接存储完整 `.osr` 字节流。

> `scores.db` 是 legacy osu!stable 的本地成绩库；官方 ppy Wiki 将其定义为保存本地成绩的数据库，并记录每个 beatmap MD5 下的 score 元数据。[来源：ppy/osu Legacy database file structure](https://github.com/ppy/osu/wiki/Legacy-database-file-structure)

## 1.2 严格只读

后端可以执行：

- 打开文件；
- 读取文件 metadata；
- 顺序读取/随机读取；
- 计算 MD5；
- LZMA 解压到内存；
- 解析 `.osu` / `.osr` / `scores.db`；
- 为分析建立内存索引；
- 在 HTTP 响应中返回分析数据。

后端不得执行：

- 覆盖原文件；
- 修改 `scores.db`；
- 修改 `.osu`；
- 修改 `.osr`；
- 修改皮肤；
- 修改 `Songs` 内任何文件；
- 将用户源文件复制到应用目录作为“导入副本”；
- 将分析结果写回源数据；
- 将 replay 解压结果落盘为持久化中间文件。

允许建立应用自身的短生命周期进程状态、HTTP 会话状态和内存缓存。若未来需要本地持久化设置，必须与 osu! 用户数据严格分离，并单独设计为应用设置，而不是修改 osu! 文件。

---

# 2. 技术栈与平台

## 2.1 Frontend

- React
- TypeScript
- Vite
- Static SPA
- 建议使用严格 TypeScript 配置（`strict: true`）
- 前端只负责 UI 状态、交互、图形渲染和 API 调用
- 前端禁止直接调用 `fetch(file://...)`、Node fs、浏览器 File System Access API 来绕过 C++ 后端的文件访问边界

### 前端设计原则

UI 应像桌面工具，而不是网站：

- 左侧输入/资源面板；
- 中间核心回放视图；
- 下方时间轴与分析区间选择；
- 右侧结果统计/详情；
- 顶部使用简单菜单栏或工具栏；
- 信息密度高但有层级；
- 避免 Hero、营销文案、巨大 CTA、渐变宣传区块。

推荐默认布局：

```text
┌───────────────────────────────────────────────────────────────┐
│ Menu / Toolbar                                                │
├──────────────┬───────────────────────────────┬────────────────┤
│ Input/Files  │ Mania Replay Preview          │ Analysis       │
│              │                               │                │
│ .osr         │       Game field              │ Global ACC     │
│ .osu         │                               │ Offset         │
│ Audio        │                               │ Judgements     │
│ Skin         │                               │                │
├──────────────┴───────────────────────────────┴────────────────┤
│ Timeline / Selection / Play controls                           │
├───────────────────────────────────────────────────────────────┤
│ Local Analysis / Timing Distribution / Hit Table               │
└───────────────────────────────────────────────────────────────┘
```

## 2.2 Backend

- C++20
- Windows-first
- 负责全部文件系统访问
- 负责 `.osr` / `.osu` / `scores.db` 解析
- 负责 MD5
- 负责 replay LZMA 解压
- 负责命中匹配与统计计算
- 提供本机 HTTP API

后端不要把 osu! 解析逻辑分散到前端。前端应得到稳定、明确、版本化的 JSON DTO。

## 2.3 Desktop shell

- Windows WebView2 优先。
- 前端构建产物为静态资源，可被嵌入 exe 或由本机静态服务器提供。
- 无 WebView2 时启动系统默认浏览器访问 localhost HTTP 地址。
- WebView2 与浏览器模式必须访问同一 HTTP API，不得维护两套业务实现。
- WebView2 初始化失败不得导致应用完全无法使用。

现有参考仓库 `uuUlazis/osumania-skin-editor` 的 README 明确采用 React + TypeScript + Vite、C++20、WebView2，并且该项目已经实现“前端资源嵌入单个可执行文件”和 mania 预览相关基础能力；CMake 中也展示了 WebView2 可选加载和前端资源嵌入的组织方式。实现本项目时可将其作为工程组织参考，但不要假定其现有代码可以直接满足 replay analyzer 的数据模型。[来源：osumania-skin-editor README](https://github.com/uuUlazis/osumania-skin-editor)；[CMakeLists.txt](https://github.com/uuUlazis/osumania-skin-editor/blob/main/CMakeLists.txt)

---

# 3. 数据源规范与可信度层级

实现时按以下优先级处理资料：

1. osu! 官方 Wiki / ppy 官方仓库代码与文档；
2. ppy `osu!` / legacy database file structure 等官方工程资料；
3. 用户给出的 CSDN `.osr` 解析文章；
4. 其他第三方实现只作为测试/兼容参考。

CSDN 文章不能覆盖当前格式的全部细节。其内容中 `.osr` 的主体结构与官方一致，但文章所展示的 mod 表属于较旧时期的字段集合；当前官方 `.osr` Wiki 已包含更新的 mod 值，例如 ScoreV2、Mirror，以及更完整的 replay 文件说明。[来源：CSDN .osr 解析文章](https://blog.csdn.net/aa13058219642/article/details/51372819)；[官方 .osr 格式](https://osu.ppy.sh/wiki/zh/Client/File_formats/osr_%28file_format%29)

官方文件格式 Wiki 规定 `.osr` 是 osu! replay 文件，`.osu` 是 beatmap difficulty 文件。[来源：osu! 文件格式](https://osu.ppy.sh/wiki/zh/Client/File_formats)

参考资料：

- https://osu.ppy.sh/wiki/zh/Game_mode/osu!mania
- https://osu.ppy.sh/wiki/zh/Gameplay/Replay
- https://osu.ppy.sh/wiki/zh/Gameplay/Accuracy
- https://osu.ppy.sh/wiki/zh/Gameplay/Judgement/osu!mania
- https://osu.ppy.sh/wiki/zh/Client/File_formats
- https://osu.ppy.sh/wiki/zh/Client/File_formats/osr_(file_format)
- https://osu.ppy.sh/wiki/zh/Client/File_formats/osu_(file_format)
- https://github.com/ppy/osu/wiki/Legacy-database-file-structure
- https://blog.csdn.net/aa13058219642/article/details/51372819
- https://github.com/uuUlazis/osumania-skin-editor
- 如本机存在：`workspace\mania skin editor\version\0.1.3`

---

# 4. `.osr` Replay 文件解析规范

## 4.1 基础类型

官方 `.osr` 格式定义使用：

- `Byte`：1 字节
- `Short`：2 字节 little-endian
- `Integer`：4 字节 little-endian
- `Long`：8 字节 little-endian
- `ULEB128`：变长整数
- `String`：`0x00` 表示空；`0x0b` 后跟 ULEB128 字节长度与 UTF-8 内容

务必封装为独立 `BinaryReader` / `ReplayReader`，不要散落 `istream.read()` + 手工 offset 计算。

## 4.2 `.osr` 顶层字段

按照官方格式解析：

```text
Byte      game mode
Integer   game version
String    beatmap MD5
String    player name
String    replay MD5
Short     count300
Short     count100
Short     count50
Short     countGeki / MAX 300
Short     countKatu / 200
Short     countMiss
Integer   score
Short     max combo
Byte      perfect/full combo flag
Integer   mods
String    life bar graph
Long      timestamp
Integer   compressed replay data length
Byte[]    compressed replay data
Long      online score ID
Double    extra mod information, only when Target Practice applies
```

官方 Wiki 还说明：`game mode = 3` 表示 osu!mania；replay data 为 LZMA 流；解压后的每个 frame 由 `w|x|y|z` 组成，其中 `w` 是相对于上一动作的毫秒间隔，`x/y` 是 replay 坐标，`z` 是按键/鼠标按键位掩码。20130319 或更新版本的 replay 还会在流末尾加入 `-12345|0|0|seed` 形式的 RNG seed 帧。[来源：官方 .osr 格式](https://osu.ppy.sh/wiki/zh/Client/File_formats/osr_%28file_format%29)

### 解析器要求

- 首先检查文件长度是否足够；
- 对所有可变长字段检查边界；
- 拒绝整数溢出；
- 压缩数据长度不得超过剩余文件大小；
- 解压后检查帧格式，不允许因坏数据产生无限循环；
- RNG seed frame 必须能识别并与普通输入 frame 区分；
- 对未知/未来字段采取 forward-compatible 策略：如果官方格式在尾部增加字段，解析器应至少报出 `extra bytes / unsupported extension`，而不是静默将数据错位。

## 4.3 Replay frame 时间重建

ReplayData 使用 delta time：

```text
absoluteTime[0] = deltaTime[0]
absoluteTime[i] = absoluteTime[i - 1] + deltaTime[i]
```

建立规范化内部结构：

```cpp
struct ReplayFrame {
    int64_t timeMs;          // 重建后的绝对回放时间
    float x;                 // 0..512，mania 通常不用于列判断
    float y;                 // 0..384，mania 通常不用于列判断
    uint32_t rawKeys;        // z
    uint32_t keyDownMask;    // 当前帧相对上一帧的按下事件
    uint32_t keyUpMask;      // 当前帧相对上一帧的抬起事件
    bool isSeedFrame;
    uint32_t seed;
};
```

`keyDownMask = rawKeys & ~previousRawKeys`，`keyUpMask = ~rawKeys & previousRawKeys`。

注意：不要把每个 replay frame 直接当成一次“击打”。frame 表示状态，实际 hit event 必须从按键状态变化与谱面物件时间进行匹配。

## 4.4 Mania 按键位

legacy `.osr` 文档给出 `z` 的按键/鼠标 bit mask。工程内部必须建立一个明确的 `ReplayKeyMask` 抽象，不要在业务层直接写 magic number。

至少保留兼容映射：

```cpp
enum class ReplayActionBit : uint32_t {
    M1 = 1,
    M2 = 2,
    K1 = 4,
    K2 = 8,
    Smoke = 16,
};
```

同时注意不同版本/客户端对 replay action 的语义可能发生变化；所有“稳定版 replay 解释”应集中在一个 compatibility layer 中。不要从“某个键位动画”反推 z bit。

---

# 5. `.osu` 谱面解析规范

## 5.1 必须支持的章节

第一阶段至少解析：

- `[General]`
- `[Metadata]`
- `[Difficulty]`
- `[TimingPoints]`
- `[Events]` 中与背景/视频/Break 相关的内容
- `[HitObjects]`

`.osu` 文件是按章节组织的文本格式，当前 stable legacy 格式常见版本为 v14；lazer 使用新的高版本格式，但本项目的第一目标是 legacy `.osr` + legacy `.osu` 组合。对非目标版本应在 UI 明确显示兼容性状态。[来源：官方 .osu 格式](https://osu.ppy.sh/wiki/zh/Client/File_formats/osu_%28file_format%29)

## 5.2 Beatmap MD5

用户直接选择 `.osr` + `.osu` 后，必须计算 `.osu` 文件内容 MD5，并与 `.osr` 中记录的 beatmap MD5 对比。

规则：

- 完全匹配：`ExactMatch`，允许进入标准分析。
- 不匹配：仍可允许用户查看原始 replay 元数据，但不得把它标记为“已验证谱面”；分析页显示“谱面 MD5 不匹配，命中判定结果可能不可靠”。
- 不要用文件名、Title、Artist、Version 代替 MD5。

历史资料也确认 beatmap checksum 是基于 `.osu` 内容计算的 MD5；官方稳定客户端要求本地谱面 checksum 与 replay/成绩对应。

## 5.3 Mania HitObjects

官方 `.osu` Wiki 规定 HitObjects 通用语法：

```text
x,y,time,type,hitSound,objectParams,hitSample
```

在 osu!mania：

- `y` 不用于决定列；
- `x` 决定列；
- 普通圆圈等价于普通 note；
- slider / spinner 不用于 mania；
- bit 7 表示 Hold note；
- Hold note 语法：`x,y,startTime,type,hitSound,endTime,hitSample`。

列号算法为：

```text
column = floor(x * keyCount / 512)
column = clamp(column, 0, keyCount - 1)
```

需要同时参考官方文档中对于 `x` 与 key count 的定义，并使用与 ppy 当前 legacy encoder 一致的边界语义。[来源：官方 .osu HitObjects 文档](https://osu.ppy.sh/wiki/zh/Client/File_formats/osu_%28file_format%29)；[ppy LegacyBeatmapEncoder](https://github.com/ppy/osu/blob/master/osu.Game/Beatmaps/Formats/LegacyBeatmapEncoder.cs)

建立内部对象：

```cpp
enum class ManiaObjectType {
    Note,
    Hold
};

struct ManiaHitObject {
    ManiaObjectType type;
    int column;
    int64_t startTimeMs;
    std::optional<int64_t> endTimeMs;
    int sourceIndex;
};
```

不要把原始 x/y/type/hitSound 丢掉；保留 raw fields 供调试和兼容。

---

# 6. osu!mania 判定与 ACC 计算

## 6.1 判定体系

官方 osu!mania 判定 Wiki 给出的当前 timing windows 为：

| 判定 | 分值 | ACC 权重 | 最大绝对误差 |
|---|---:|---:|---:|
| PERFECT | 320 | 100% | 16 ms |
| GREAT | 300 | 100% | `64 - 3 × OD` |
| GOOD | 200 | 66.67% | `97 - 3 × OD` |
| OK | 100 | 33.33% | `127 - 3 × OD` |
| MEH | 50 | 16.67% | `151 - 3 × OD` |
| MISS | 0 | 0% | `188 - 3 × OD` |

判定 Wiki 说明“误差 ≤ 最大允许误差”时判入对应区间，所谓 error 是以判定时间为中心的有符号偏移。[来源：官方 osu!mania 判定系统](https://osu.ppy.sh/wiki/zh/Gameplay/Judgement/osu%21mania)

但是：本项目必须区分“当前 lazer/新 ruleset 语义”和“legacy osu!stable replay 成绩语义”。`.osr` 顶层字段中的 `countGeki` 在 mania 代表 MAX 300；官方 Accuracy Wiki 对 legacy/ScoreV1 与 ScoreV2 的说明是：

- ScoreV1：MAX 与普通 300 都按 300 权重计算；
- ScoreV2：MAX 权重为 305。

因此不要在 legacy replay 上简单套用“PERFECT=320”的新评分权重。[来源：官方准确率](https://osu.ppy.sh/wiki/zh/Gameplay/Accuracy)；[官方 .osr 格式](https://osu.ppy.sh/wiki/zh/Client/File_formats/osr_%28file_format%29)

## 6.2 ACC 的统一抽象

实现 `AccuracyModel`：

```cpp
enum class AccuracyProfile {
    LegacyScoreV1,
    LegacyScoreV2,
    ModernRuleset
};
```

第一阶段主目标：准确复现 legacy `.osr` 成绩中的稳定版统计。

建议 legacy 公式：

```text
ScoreV1 accuracy numerator =
    countGeki * 300 +
    count300  * 300 +
    countKatu  * 200 +
    count100   * 100 +
    count50     * 50

accuracy = numerator / (totalJudgedObjects * 300)
```

ScoreV2：

```text
ScoreV2 accuracy numerator =
    countGeki * 305 +
    count300  * 300 +
    countKatu  * 200 +
    count100   * 100 +
    count50     * 50

accuracy = numerator / (totalJudgedObjects * 300)
```

其中 `totalJudgedObjects = sum(all five hit result counts + misses)`。

> 这些公式应通过真实 `.osr` fixture 与官方结果页数据做回归验证；若发现特定 client version / mod profile 有不同语义，不要覆盖旧公式，而应新增 compatibility profile。

## 6.3 Offset 定义

统一定义：

```text
offsetMs = actualPressTimeMs - objectStartTimeMs
```

因此：

- `offset < 0`：提前（Early / Fast）
- `offset > 0`：延迟（Late / Slow）
- `offset = 0`：理论完美同步

必须同时存：

```cpp
struct HitEvent {
    int objectIndex;
    int column;
    int64_t objectTimeMs;
    std::optional<int64_t> actualTimeMs;
    std::optional<double> offsetMs;
    HitResult judgement;
    HitPhase phase; // NoteHead / HoldHead / HoldTail
    bool inferredFromReplay;
    bool verifiedAgainstScoreCounts;
};
```

### 统计至少包含

全局：

- ACC
- judgement count：MAX/300/200/100/50/MISS
- mean offset
- mean absolute offset
- median offset
- standard deviation
- early count / late count
- early average / late average
- min / max offset
- offset histogram
- per-column statistics

局部：同样字段，但只对选定区间进行统计。

建议再提供：

- P95 absolute offset
- 10ms / 20ms / 30ms / 40ms 等 offset bucket 分布
- per-column median/mean

不要在产品 UI 中把“平均 offset”误称为“平均击打时间”。

---

# 7. Replay ↔ HitObject 匹配算法

这是本项目最重要的核心模块，必须与前端渲染彻底解耦。

## 7.1 不允许简单 nearest-neighbour

错误实现示例：

```text
对每个 Note 找离它最近的 keyDown
```

不能直接这样做，因为 mania 存在：

- 同时多列 chord；
- 相邻时间的多个 note；
- Hold head/tail；
- 持键状态跨越多个 hit object；
- 多个列同时按下；
- note lock / 已被前一个对象占用的输入。

必须按“列 + 时间 + 持键状态 + object 生命周期”处理。

## 7.2 推荐状态机

每列维护：

```text
ColumnState
 ├─ currentlyHeld
 ├─ activeHoldObject
 ├─ pendingNotes
 └─ lastAcceptedPressTime
```

全局维护：

```text
ReplayAnalysisCursor
 ├─ replayFrameIndex
 ├─ objectIndex
 ├─ column states
 └─ judgement events
```

算法按时间线推进：

```text
for each replay frame in chronological order:
    apply key state transitions
    produce keyDown / keyUp events
    feed events to affected columns
    resolve notes whose timing window has opened/closed
    resolve hold heads and tails
```

同时还要在没有输入的情况下推进 miss judgement：不能因为 replay 没有 frame 就漏掉一个 Note 的 MISS。

## 7.3 同时按键（chord）

同一时间附近多个不同列的 Note 应分别匹配到对应列的 keyDown。

不要把 chord 作为单个“群组对象”，但可以在统计层建立 `ChordGroup` 方便展示：

```cpp
struct ChordGroup {
    int64_t centerTimeMs;
    std::vector<int> objectIndices;
};
```

## 7.4 Hold

Hold note 至少拆成：

- head judgement
- hold state
- tail judgement

第一阶段分析 UI 至少显示 head/tail 各自 offset 与 judgement。

内部模型：

```cpp
struct HoldJudgement {
    HitEvent head;
    HitEvent tail;
    bool releasedEarly;
    bool heldThroughTail;
};
```

注意：不能简单以“keyDown 距离 endTime 最近”判 tail；必须依据该列在 endTime 前后的 key state transitions 以及游戏规则窗口判断。

## 7.5 Replay 时间推进

Replay frame 是离散的，但实际命中时间也是从离散状态变化推导的。第一阶段应优先采用 replay frame 时间戳作为实际 input event 时间。

不要进行未经验证的“帧间插值”。如未来要拟合物理按键时间，必须做成可选、明确命名的模型，而不是悄悄改变默认 offset。

osu! 当前 GitHub 代码中存在有关 frame/input 时序影响 replay judgement 的讨论，因此 timing pipeline 必须明确区分：

- replay frame time；
- input state change time；
- object judgement time；
- rendered time。

不能让动画时钟直接决定分析结果。[参考：ppy/osu replay/input timing issues](https://github.com/ppy/osu/issues/34009)；[mania replay issue](https://github.com/ppy/osu/issues/1626)

---

# 8. Replay 校验策略

分析页需要同时展示两套数据：

## 8.1 OSR 声明值

直接读取 `.osr` 顶层字段：

- player
- game version
- beatmap MD5
- replay MD5
- score
- max combo
- perfect/full combo flag
- mods
- score judgement counts
- timestamp

## 8.2 本地重建值

从 `.osu` + replay data 重建：

- object count
- hit event count
- judgement distribution
- max combo
- ACC
- offset

## 8.3 一致性报告

定义：

```cpp
struct ReplayConsistencyReport {
    bool beatmapMd5Match;
    bool modeIsMania;
    bool objectCountPlausible;
    bool judgementCountsMatch;
    bool maxComboMatch;
    bool accuracyMatch;
    bool replayEndedNormally;
    std::vector<Diagnostic> diagnostics;
};
```

任何不一致都应显式显示。

例如：

```text
Beatmap MD5     ✓ exact match
Mode            ✓ osu!mania
Judgements      ✓ reconstructed counts match OSR
Max combo       ✓ match
Accuracy        ✓ 99.421%
Replay stream   ✓ decoded
```

或者：

```text
Beatmap MD5     ! mismatch
Judgements      ! not trusted
Max combo       — unavailable
Accuracy        — partial analysis only
```

---

# 9. Replay 预览器

## 9.1 目标

根据：

- `.osu`
- `.osr`
- 可选音乐
- 所选皮肤

渲染 osu!mania replay preview。

预览器必须使用与分析模块相同的：

- hit object 时间轴；
- key column 映射；
- Hold start/end；
- replay frame 时间线。

严禁预览器再自行解析 `.osr`。

## 9.2 渲染层

推荐：

```text
ReplayTimeline
      │
      ├─ ObjectRenderer
      ├─ KeyRenderer
      ├─ HoldRenderer
      ├─ JudgementRenderer
      ├─ StageRenderer
      ├─ HUDRenderer
      └─ OptionalAudioClock
```

### 浏览器优先实现建议

第一阶段允许使用 Canvas 2D；如果后续需要大量图片、粒子、动画、多个长键同时存在，可升级为 WebGL。

不要因为用了 Canvas 就把 osu! skin 逻辑写死在 React component 内。

## 9.3 时间轴

统一定义：

```text
previewTimeMs
```

所有元素都由这个时间计算位置：

```text
screenY = judgeLineY + scrollDirection * f(objectTimeMs - previewTimeMs)
```

其中 `f()` 按 mania scroll speed / beatmap timing / skin 配置得到。

第一阶段可以优先支持一个稳定、可复现的视觉滚动模型；但时间轴与分析时间必须始终使用毫秒，不使用 frame index 作为主时间单位。

## 9.4 皮肤

皮肤必须设计为数据驱动。

参考 osu!mania skin 官方文档：

- `[Mania]` 可以按 `Keys` 提供独立配置；
- `ColumnStart`
- `ColumnSpacing`
- `ColumnWidth`
- `ColumnLineWidth`
- `BarlineHeight`
- 各种 `mania-key*`
- note / hold body / tail
- stage / judgement 等元素

官方 skin 文档说明 `[Mania]` 的水平位置参数以 480px 高度为基准；key count 可以分别配置；不同 key count 的设置应使用独立 `[Mania]` 段。[来源：官方 skin.ini](https://osu.ppy.sh/wiki/zh/Skinning/skin.ini)

官方 osu!mania skin 页面还列举了 `mania-key*`、note、hold tail、stage 等资源的文件名与缩放行为。[来源：官方 osu!mania 皮肤自定义](https://osu.ppy.sh/wiki/zh/Skinning/osu%21mania)

参考仓库 `osumania-skin-editor` 当前 README 还明确说明其预览面板已经以 osu! 480 高度坐标系渲染轨道背景、音符、面条、判定线、HUD，并处理 `ColumnStart` / `ColumnWidth` / `BarlineHeight` / `HitPosition` 等 mania 参数，因此本项目可复用其架构思路。[来源：osumania-skin-editor README](https://github.com/uuUlazis/osumania-skin-editor)

## 9.5 Skin loader

定义：

```cpp
struct ManiaSkinProfile {
    std::string name;
    double version;
    int keys;
    std::vector<double> columnStart;
    std::vector<double> columnWidth;
    std::vector<double> columnSpacing;
    std::vector<double> columnLineWidth;
    double hitPosition;
    bool upsideDown;
    // more parameters
};
```

前端拿到统一 JSON：

```json
{
  "name": "SkinName",
  "keys": 7,
  "version": 2.7,
  "mania": {
    "columnStart": 136,
    "columnWidth": [30,30,30,30,30,30,30],
    "columnSpacing": [0,0,0,0,0,0],
    "hitPosition": 402,
    "upsideDown": false
  },
  "resources": {
    "key1": "...",
    "key1D": "...",
    "note": "...",
    "noteH": "...",
    "stageBottom": "..."
  }
}
```

资源 URL 不应暴露任意文件系统路径。推荐后端提供带 session/token 的静态资源接口：

```text
GET /api/resources/{sessionId}/{resourceId}
```

resourceId 在后端解析时映射到已验证的只读文件路径，防止 `../` 穿越。

## 9.6 无皮肤资源的 fallback

必须有 default renderer：

- 纯 CSS/Canvas 轨道；
- 默认 note；
- 默认 hold；
- 默认 key down/up；
- 默认 judgement line。

这样即使皮肤不完整，也能分析回放。

---

# 10. 音频

音乐文件为可选项。

### 有音乐

以音频 playback clock 作为 preview clock，但必须支持暂停、跳转和 seek。

### 无音乐

以 `requestAnimationFrame` + monotonic clock 作为 preview clock。

### 音频时钟和分析时间

音频只是“播放时钟来源”，不改变 replay/beatmap 原始毫秒数据。

音频 decoder 出错时：

- 预览仍可运行；
- UI 提示“无音频模式”；
- 分析不受影响。

---

# 11. 进度条与局部选区交互

这是需求中的关键交互，必须区别“点击跳转”和“长按选择”。

## 11.1 基础行为

### 单击

```text
pointerdown
pointerup
且未进入 long-press selection
→ previewTime = clickedTime
→ 清除当前选区（默认）
```

### 长按

```text
pointerdown = selectionStartTime
进入 long-press 状态
移动过程中显示预选区
pointerup = selectionEndTime
→ range = normalize(start, end)
→ previewTime = pointerupTime
→ 保留 [start, end] 为局部选区
```

必须使用 pointer capture，因此鼠标移出 timeline 后仍可正常收到 `pointerup`。

## 11.2 长按判定阈值

长按阈值不要写死在业务逻辑中，定义为可调参数，例如：

```ts
const LONG_PRESS_MS = 250;
const DRAG_THRESHOLD_PX = 4;
```

这里的阈值是 UI 手势参数，不属于 osu! gameplay judgement。

推荐行为：

- `pointerdown` 开始计时；
- 超过 `LONG_PRESS_MS` 或移动超过 `DRAG_THRESHOLD_PX` 时进入 selection mode；
- 在 selection mode 中实时更新 `[start,end]`；
- `pointerup` 后固定区间。

## 11.3 边界

若选择区间反向拖动：

```text
raw = [release, press]
normalized = [min(raw), max(raw)]
```

若 start == end：

- 视为 jump，不建立有效局部区间。

不得自行添加“最小选区必须 100ms”之类的业务限制，除非产品需求另行定义。

---

# 12. 局部分析语义

## 12.1 区间定义

统一采用半开区间：

```text
[startMs, endMs)
```

这样避免边界 hit object 同时计入两个相邻 segment。

## 12.2 哪些对象计入局部分析

推荐按 object judgement time 判断：

- Note：`object.startTime`
- Hold head：`head judgement time`
- Hold tail：`tail judgement time`

局部分析默认按“击打事件”统计，而不是按 Hold 的持续长度统计。

UI 可额外显示：

```text
Objects in range
Notes
Hold heads
Hold tails
```

## 12.3 局部 ACC

调用同一个 `AccuracyModel`，只过滤区间内的 `HitEvent`。

不得用：

```text
global ACC * segment ratio
```

必须重新计算实际 hit result 的权重。

## 12.4 局部偏移

计算：

- mean offset
- median offset
- mean absolute offset
- SD
- P95
- early/late count
- per-column distribution

局部区间越短，样本数越少；UI 必须显示 `N`。

例如：

```text
Local ACC  98.73%
Hits       57
Mean       +3.4 ms
Median     +1.0 ms
SD         19.8 ms
Early      21
Late       34
```

---

# 13. API 设计

API 只对 localhost 开放。

推荐默认：

```text
127.0.0.1:<random-or-allocated-port>
```

不要默认监听 `0.0.0.0`。

## 13.1 统一响应

成功：

```json
{
  "ok": true,
  "data": {}
}
```

失败：

```json
{
  "ok": false,
  "error": {
    "code": "REPLAY_DECODE_FAILED",
    "message": "Unable to decode replay stream",
    "details": {}
  }
}
```

## 13.2 Session

每次导入创建内存 session：

```text
sessionId
 ├─ replay
 ├─ beatmap
 ├─ optional audio
 ├─ skin
 ├─ decoded frames
 ├─ hit objects
 ├─ analysis
 └─ consistency report
```

Session 默认只存在进程内，退出进程全部释放。

## 13.3 建议 endpoints

### App

```http
GET /api/health
GET /api/app/info
```

### Native file picker

```http
POST /api/dialog/open-file
POST /api/dialog/open-folder
```

返回：

```json
{
  "path": "C:\\...\\play.osr"
}
```

### Replay

```http
POST /api/replays/load
GET  /api/replays/{sessionId}/metadata
GET  /api/replays/{sessionId}/frames
```

`frames` 默认分页/按时间范围读取，不要一次把超大型 replay 全量 JSON 化发送到前端。

### Beatmap

```http
POST /api/beatmaps/load
GET  /api/beatmaps/{sessionId}/metadata
GET  /api/beatmaps/{sessionId}/objects?from=...&to=...
```

### Analysis

```http
GET  /api/analysis/{sessionId}/summary
GET  /api/analysis/{sessionId}/hits?from=...&to=...
POST /api/analysis/{sessionId}/segment
```

`POST /segment` 不是文件写操作，只是根据现有内存分析数据计算指定时间区间的 DTO；可以改为 GET/query 参数，具体取决于 API 风格。

### Skin

```http
POST /api/skins/load
GET  /api/skins/{sessionId}/profile
GET  /api/skins/{sessionId}/resource/{resourceId}
```

第二阶段：

```http
POST /api/scores-db/load
GET  /api/scores-db/{sessionId}/maps
GET  /api/scores-db/{sessionId}/scores?mapHash=...
GET  /api/scores-db/{sessionId}/scores/{scoreId}
POST /api/scores-db/{sessionId}/resolve-song
```

## 13.4 HTTP API 安全

虽然只监听 localhost，也必须：

- 禁止任意路径访问；
- 所有文件读取通过路径 canonicalization + allowlist；
- 资源接口不得支持 `..`；
- sessionId 必须不可预测；
- 对超长路径、超大 body 做限制；
- 不要把任意 Windows path 直接拼接进 URL。

---

# 14. 推荐 C++ 模块结构

```text
backend/
├─ src/
│  ├─ app/
│  │  ├─ main.cpp
│  │  ├─ launcher.cpp
│  │  └─ app_context.cpp
│  ├─ http/
│  │  ├─ api_server.cpp
│  │  ├─ routes_replay.cpp
│  │  ├─ routes_analysis.cpp
│  │  ├─ routes_skin.cpp
│  │  └─ routes_scores_db.cpp
│  ├─ io/
│  │  ├─ binary_reader.cpp
│  │  ├─ safe_path.cpp
│  │  └─ md5.cpp
│  ├─ replay/
│  │  ├─ osr_reader.cpp
│  │  ├─ replay_decoder.cpp
│  │  ├─ replay_frames.cpp
│  │  └─ replay_compat.cpp
│  ├─ beatmap/
│  │  ├─ osu_parser.cpp
│  │  ├─ mania_objects.cpp
│  │  ├─ timing_points.cpp
│  │  └─ beatmap_hash.cpp
│  ├─ mania/
│  │  ├─ key_mapping.cpp
│  │  ├─ judgement.cpp
│  │  ├─ hit_matcher.cpp
│  │  ├─ accuracy.cpp
│  │  └─ analysis.cpp
│  ├─ skin/
│  │  ├─ skin_scanner.cpp
│  │  ├─ skin_ini.cpp
│  │  ├─ mania_skin.cpp
│  │  └─ skin_resources.cpp
│  ├─ scores_db/
│  │  ├─ scores_db_reader.cpp
│  │  └─ scores_index.cpp
│  └─ session/
│     ├─ session.cpp
│     └─ session_store.cpp
└─ tests/
   ├─ osr_tests.cpp
   ├─ osu_tests.cpp
   ├─ mania_judgement_tests.cpp
   ├─ replay_match_tests.cpp
   ├─ accuracy_tests.cpp
   ├─ skin_tests.cpp
   └─ scores_db_tests.cpp
```

前端：

```text
frontend/src/
├─ app/
│  ├─ App.tsx
│  ├─ routes.tsx
│  └─ api.ts
├─ components/
│  ├─ Layout/
│  ├─ FileInput/
│  ├─ ReplayPreview/
│  ├─ Timeline/
│  ├─ AnalysisSummary/
│  ├─ OffsetChart/
│  ├─ HitTable/
│  ├─ SkinSelector/
│  └─ Diagnostics/
├─ features/
│  ├─ replay/
│  ├─ beatmap/
│  ├─ analysis/
│  ├─ timeline/
│  ├─ skin/
│  └─ scoresDb/
├─ models/
│  ├─ replay.ts
│  ├─ beatmap.ts
│  ├─ analysis.ts
│  └─ skin.ts
├─ render/
│  ├─ maniaRenderer.ts
│  ├─ noteRenderer.ts
│  ├─ holdRenderer.ts
│  └─ hudRenderer.ts
└─ styles/
   └─ desktop-tool.css
```

---

# 15. 状态管理

推荐状态分层：

```text
AppState
 ├─ sourceFiles
 ├─ session
 ├─ preview
 │   ├─ timeMs
 │   ├─ playing
 │   ├─ playbackRate
 │   └─ selection
 ├─ skin
 └─ analysis
```

不要把整个 replay frame 数组塞进 React state。

推荐：

- 元数据进入 React state；
- 大型数组保存在模块级 store / Zustand 等专用 store / immutable model；
- renderer 订阅时间指针；
- 时间推进不触发整个右侧分析面板重渲染。

---

# 16. 大数据性能

目标：对普通 osu!mania replay 应有明显“工具感”的即时响应。

规则：

- replay frame 保存在 C++ 内存中；
- 分析结果预计算并按时间索引；
- `GET hits?from&to` 支持时间范围；
- offset histogram 不要每次重新遍历全表；
- local segment 查询优先使用 sorted events + binary search；
- preview renderer 只绘制可视窗口附近的 objects；
- 不要每一帧重新 parse `.osu`；
- 不要每一次拖动进度条都从文件重新读取 `.osr`。

建立：

```cpp
std::vector<HitEvent> eventsByTime;
std::vector<size_t> objectOffsetsByTime;
```

局部查询：

```text
lower_bound(startMs)
upper_bound(endMs)
```

---

# 17. scores.db 第二阶段规范

官方 legacy database structure 给出了 `scores.db` 的基础布局：

```text
Int       version
Int       number of beatmaps
Beatmap*  ...
```

每个 beatmap：

```text
String beatmap MD5
Int    number of scores
Score* scores
```

每个 score 至少包含：

```text
Byte    gameplay mode
Int     score/replay version
String  beatmap MD5
String  player name
String  replay MD5
Short   300
Short   100
Short   50
Short   Geki / mania MAX
Short   Katu / mania 200
Short   misses
Int     replay score
Short   max combo
Bool    perfect combo
Int     mods
String  empty legacy field
Long    replay timestamp
Int     0xffffffff
Long    online score ID
Double  extra mod information if Target Practice
```

这些字段由官方 ppy legacy database 文档记录；特别注意 mania 中 `count100` 表示 100、`count50` 表示 50、`countGeki` 表示 MAX、`countKatu` 表示 200。[来源：ppy/osu Legacy database file structure](https://github.com/ppy/osu/wiki/Legacy-database-file-structure)

## 17.1 `scores.db` 只读策略

- 从文件加载到只读内存模型；
- 用户操作只是排序、过滤、选择；
- 不实现保存；
- 不实现删除 score；
- 不实现修改 score；
- 不实现“修复数据库”。

## 17.2 Songs 检索

优先匹配：

1. scores.db beatmap MD5 == `.osu` 内容 MD5；
2. beatmap entry 中记录的 `.osu` 文件名；
3. beatmap 所在 Songs 子目录；
4. 文件存在且 Mode=3；
5. 必要时检查 metadata / BeatmapID / BeatmapSetID 作为辅助诊断。

不要因为 metadata 相似就视为 exact match。

---

# 18. Mod 与兼容性

`.osr` 中 `mods` 是 bit flags。当前官方 `.osr` Wiki 包含：

- NoFail
- Easy
- Hidden
- HardRock
- SuddenDeath
- DoubleTime
- Relax
- HalfTime
- Nightcore
- Flashlight
- Autoplay
- Perfect
- Key1..Key9
- Coop
- ScoreV2
- Mirror
- 等

其中 mania 专属 key mods / Coop 等会影响有效 key count 或 stage 结构。

第一阶段不要试图支持所有 mod 的 gameplay 重演。必须明确支持范围。

推荐第一阶段：

- 无 mod；
- KeyX；
- ScoreV2；
- Mirror（至少能识别并提示）；
- Coop（至少能识别并提示）；
- 其他 mod：识别并显示，但若分析模型尚未实现，标记为 `analysis compatibility limited`。

### 重要

不要把“replay header 里有某个 mod”简单忽略。若 mod 会改变：

- key count；
- column mapping；
- timing window；
- beatmap object transformation；

就必须进入 `ReplayCompatibilityProfile`。

---

# 19. 测试策略

## 19.1 Parser golden tests

准备 fixture：

```text
fixtures/
├─ replays/
│  ├─ mania_4k_fc.osr
│  ├─ mania_7k_mixed.osr
│  ├─ mania_hold.osr
│  ├─ mania_chord.osr
│  └─ malformed.osr
├─ beatmaps/
│  ├─ mania_4k.osu
│  ├─ mania_7k.osu
│  └─ mania_hold.osu
└─ skins/
   ├─ minimal/
   └─ reference/
```

绝对不要把真实用户文件提交进仓库。

## 19.2 `.osr` parser tests

验证：

- mode == 3；
- version；
- beatmap MD5；
- player；
- replay MD5；
- judgement counts；
- score；
- max combo；
- mods；
- timestamp；
- compressed size；
- LZMA decode；
- frame count；
- delta → absolute time；
- seed frame；
- malformed/truncated data rejection。

## 19.3 `.osu` parser tests

验证：

- Mania mode detection；
- key count；
- OD；
- timing points；
- note column；
- hold start/end；
- malformed object line handling；
- MD5 exact match。

## 19.4 判定测试

至少覆盖：

```text
exact 0 ms
±1 ms
±16 ms
±window boundary
window + 1 ms
OD changes
```

分别验证 Early / Late。

## 19.5 Replay matching tests

至少覆盖：

1. 单 note 正中；
2. 单 note early；
3. 单 note late；
4. miss；
5. chord；
6. 两个相邻 chord；
7. hold 正常；
8. hold early release；
9. hold tail timing；
10. 同列连续 notes；
11. 长时间无输入；
12. replay frame 多次只改变位置不改变 keys；
13. keyDown / keyUp 在同一时间的边界；
14. seed frame 不被误判为 input；
15. replay frame 时间不连续。

## 19.6 ACC tests

使用手算 oracle 验证：

```text
all MAX
all 300
all 200
all 100
all 50
all MISS
mixed counts
```

同时验证 ScoreV1 / ScoreV2 profile。

## 19.7 Local analysis tests

验证：

```text
[start,end)
事件刚好落在 start
事件刚好落在 end
区间反向拖动
start == end
空区间
只包含一个 hit
只包含 Hold tail
```

---

# 20. 诊断与可观察性

开发模式提供 diagnostics：

```text
OSR
  format parsed: yes
  mode: mania
  version: 2026...
  replay frames: 18432
  compressed bytes: 38124
  decompressed bytes: 124018

Beatmap
  mode: mania
  keys: 7
  objects: 13872
  md5: ...
  replay match: exact

Analysis
  matched hits: 13872
  unmatched input: 3
  unmatched objects: 0
  reconstructed accuracy: 98.7312%
  score accuracy: 98.7312%
```

诊断信息应方便开发者发现 parser bug，但普通用户看到的 UI 必须保持整洁。

---

# 21. 错误处理

推荐 error codes：

```text
FILE_NOT_FOUND
FILE_ACCESS_DENIED
INVALID_ARGUMENT
UNSUPPORTED_FILE_TYPE
INVALID_OSR_HEADER
INVALID_OSR_STRING
INVALID_OSR_LENGTH
REPLAY_LZMA_DECODE_FAILED
REPLAY_DATA_MALFORMED
INVALID_REPLAY_MODE
BEATMAP_PARSE_FAILED
BEATMAP_NOT_MANIA
BEATMAP_MD5_MISMATCH
BEATMAP_OBJECT_PARSE_FAILED
SKIN_NOT_FOUND
SKIN_INI_PARSE_FAILED
SKIN_RESOURCE_NOT_FOUND
SCORES_DB_PARSE_FAILED
SCORES_DB_UNSUPPORTED_VERSION
SONGS_RESOLUTION_FAILED
ANALYSIS_NOT_AVAILABLE
```

UI 文案不要只显示“Failed”。必须给出：

- 发生了什么；
- 哪个文件；
- 是否还能部分使用；
- 建议的下一动作。

例如：

```text
谱面校验未通过
Replay 中的 beatmap MD5 与所选 .osu 文件内容不一致。
你仍可以查看 replay 原始信息，但命中/ACC 分析不作为可靠结果。
```

---

# 22. 开发阶段

## Phase 1 — Replay Analyzer MVP

### P0

1. C++ HTTP server + session。
2. `.osr` parser。
3. LZMA replay decoder。
4. `.osu` mania parser。
5. beatmap MD5。
6. replay frame model。
7. mania hit object model。
8. replay ↔ object matcher。
9. global ACC。
10. global offset statistics。
11. consistency report。
12. React desktop UI。
13. Native file picker。
14. timeline jump。
15. local range selection。
16. local ACC/offset。

### P1

17. Skin loader。
18. Canvas replay renderer。
19. Hold rendering。
20. judgement rendering。
21. optional audio。
22. resource serving。

### P2

23. per-column analytics。
24. offset histogram。
25. hit event table。
26. keyboard overlay。
27. replay speed control。
28. polished diagnostics。

## Phase 2 — scores.db

1. `scores.db` parser。
2. score list UI。
3. mania-only filtering。
4. Songs root binding。
5. MD5-based beatmap lookup。
6. replay path resolution。
7. click score → load analyzer session。
8. exact/ambiguous resolution diagnostics。

---

# 23. 实现顺序建议

代码代理开始编码时严格按依赖顺序推进，不要一开始同时写全部 UI。

```text
1. BinaryReader
2. MD5
3. OSR header parser
4. LZMA decoder
5. ReplayFrame reconstruction
6. OSU parser
7. Mania object conversion
8. Hit matcher
9. Accuracy model
10. Analysis index
11. HTTP API
12. React source selection UI
13. Timeline
14. Preview renderer
15. Skin
16. Audio
17. Local analysis UI
18. Integration tests
19. WebView2 shell
20. Browser fallback
```

每一步都应保持可测试。

不要先写一个“假的 preview”，再倒逼后端数据结构；后端规范化数据模型应先冻结。

---

# 24. 第一版完成定义（Definition of Done）

Phase 1 只有在以下条件同时满足时才算完成：

### 输入

- 能打开真实 `.osr`；
- 能打开真实 `.osu`；
- 能检查 MD5；
- 音频可有可无；
- 至少一个皮肤能加载，缺皮肤资源有 fallback。

### 分析

- mode 正确识别为 osu!mania；
- replay frame 正确解压；
- note / hold 正确解析；
- chord 正确匹配；
- hold head/tail 正确处理；
- 全局 judgement count 可重建；
- 全局 ACC 与 OSR 成绩匹配到允许误差；
- offset 正负方向明确；
- 局部区间计算为重新聚合而不是比例估算；
- 选择区间后 previewTime 等于松开位置。

### 预览

- note 随时间移动；
- hold 正确绘制；
- key down/up 有可见反馈；
- skin 切换不影响分析结果；
- seek 后能恢复正确场景；
- 暂停/播放/拖动不破坏分析数据；
- 无音乐仍可正常运行。

### 只读

- 测试期间没有写入用户 `.osr`；
- 没有写入 `.osu`；
- 没有写入 Songs；
- 没有写入 skins；
- 没有修改 `scores.db`；
- 没有生成持久化“导入副本”。

---

# 25. 不要做的事情

1. 不要从前端直接读取任意本地路径。
2. 不要把 `.osr` 当 ZIP。
3. 不要把 LZMA 解压后的每个 frame 当成一次 hit。
4. 不要用 nearest-neighbour 单独完成 mania note matching。
5. 不要用 replay 文件名判断 beatmap。
6. 不要用 Artist/Title/Difficulty 替代 MD5。
7. 不要在不说明 compatibility profile 的情况下把现代 mania `PERFECT=320` 权重套到 legacy score ACC。
8. 不要把 Hold 当普通 Note。
9. 不要在 React render 函数中每帧遍历整个 replay。
10. 不要为了显示皮肤资源而暴露任意 filesystem path。
11. 不要实现“修复/写回 scores.db”。
12. 不要把局部 ACC 用全局 ACC 按长度/对象数简单缩放。
13. 不要把 preview clock 与 analysis time 混为一个可变值。
14. 不要让皮肤 renderer 决定 gameplay semantics。
15. 不要在 MD5 mismatch 时静默给用户一个“看起来准确”的分析结果。

---

# 26. 关键工程原则

## Single Source of Truth

后端 parser 是所有 replay/beatmap 数据的唯一事实来源；前端只能消费 DTO。

## Deterministic Analysis

同一组 `.osr` + `.osu` + compatibility profile，任何时候都应产生相同分析结果。

## Rendering ≠ Analysis

渲染可以每帧运行；分析不应依赖浏览器帧率。

## Raw ≠ Derived

OSR header 中的成绩是 raw metadata；从 replay+beatmap 重建的 judgement 是 derived data；两者必须分别保存并支持对照。

## Read-only by construction

API 设计层面就不提供源文件修改接口，而不是“提供接口再提醒不要调用”。

## Compatibility explicit

遇到版本差异时新增 profile / adapter，而不是修改一个全局算法使旧 replay 和新 replay 互相污染。

## Test with real fixtures

解析器和 hit matcher 的正确性不能靠人工观察 UI 判断，必须通过真实 replay fixture 和自动化 golden tests 证明。

---

# 27. 参考实现说明

`uuUlazis/osumania-skin-editor` 的当前公开仓库采用：

- React + TypeScript + Vite；
- C++20 backend；
- WebView2 shell；
- CMake；
- mania skin parser / scanner；
- 480 高度坐标系的 mania preview。

其 README 和 CMake 可作为项目目录、WebView2 optional linkage、frontend embedding、skin parser 组织方式的参考。[来源：osumania-skin-editor README](https://github.com/uuUlazis/osumania-skin-editor)；[CMakeLists.txt](https://github.com/uuUlazis/osumania-skin-editor/blob/main/CMakeLists.txt)

本项目应优先复用“思路和可迁移的工程模式”，而不是直接依赖该仓库的内部实现。若本机存在：

```text
workspace\mania skin editor\version\0.1.3
```

开发代理应在开始实现 skin renderer 前检查该目录的实际源码，并优先评估：

- skin.ini parser；
- mania skin schema；
- column layout 计算；
- 资源 fallback；
- WebView2 shell；
- frontend embedding。

若本机路径不存在，则以公开 GitHub 仓库为参考，不阻塞 Phase 1 replay parser/analysis 开发。

---

# 28. 最终执行要求

当代码代理收到本项目任务时：

1. 先读取本 `agent.md`。
2. 再检查现有仓库结构，避免覆盖已有实现。
3. 先建立 parser / model / test 层，再开发 UI。
4. 任何不确定的 `.osr` / `.osu` / scores.db 语义优先查官方 ppy Wiki/代码。
5. 对 legacy stable 与 modern/lazer 差异做 compatibility layer。
6. 所有分析结果都可追溯到具体 replay frame 与 hit object。
7. 所有用户源文件均只读。
8. 优先交付一个可以对真实 `.osr` 做“解析 → 验证 → 全局分析 → 时间轴 → 局部分析”的纵向切片，再扩展 skin/audio/数据库。

本文件应被视为项目的开发约束，而不是普通需求说明；若实现与本文冲突，应先指出冲突，再选择不会破坏 replay 数据正确性的方案。
