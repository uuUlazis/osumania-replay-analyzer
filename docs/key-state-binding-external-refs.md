# 按键状态绑定（Key State Binding）外部参照档案

> 用途：osu!mania **V1（stable）判定机制 / 按键状态绑定** 的公开文档、源码、库与工具索引。
> 生成：2026-10-16 21:15 (Asia/Shanghai)，检索方式 = web_search + GitHub Contents API (base64) + 直连抓取。
> 状态图例：**✅已抓取正文** / **🟡仅确认仓库元数据** / **⚪仅检索命中（链接未逐页抓取）**
> 环境限制（本机实测）：`raw.githubusercontent.com`、`github.com` HTML 直连超时；`web.archive.org`、`grep.app` 不可达；`api.github.com` ✅（匿名 60 次/时，Contents API 返回 base64）；`osu.ppy.sh` wiki ✅；Gitea 镜像 raw ✅。

---

## 0. 结论摘要

1. **“按键状态绑定”在官方文档里没有独立页面**。公开可读的完整链路只存在于 osu!lazer 源码（位掩码 → 列动作 → 物件输入）与 .osr 格式规范里。
2. **stable（V1）真正的绑定逻辑只在 `peppy/osu-stable-reference` 中**（`HitObjectManagerMania.cs`）。该仓库当前匿名访问 **404**（详见 §3.1），但 osu-wiki 与 ppy/osu PR 里保留了 commit 与行号线索。
3. **stable 的列内仲裁 FSM（未处理位 / Loop A 仲裁 / deadline 公式）在任何公开资料中都不存在** —— 只能靠本项目逆向结论 + 内存级真值采集对齐。
4. 可用于交叉验证的三份“锚”：`ManiaReplayFrame.FromLegacy`（列位序）、`ManiaAutoGenerator`（理想按键帧）、`ManiaHitWindows` Classic 分支（窗口数值，已与本项目 OD4=176 互证）。

---

## 1. A 层：规范 / 文档

| # | 资源 | 覆盖 | 关键内容 | 状态 |
|---|---|---|---|---|
| A1 | [.osr (file format) — osu! wiki](https://osu.ppy.sh/wiki/en/Client/File_formats/osr_%28file_format%29) | 帧格式、键位编码 | 帧 `w\|x\|y\|z`；`z` 位掩码 M1=1/M2=2/K1=4/K2=8/Smoke=16；**mania 列掩码存在 `x` 字段** | ✅ |
| A2 | [osu!mania judgement system — wiki EN](https://osu.ppy.sh/wiki/en/Gameplay/Judgement/osu%21mania) / [中文](https://osu.ppy.sh/wiki/zh/Gameplay/Judgement/osu%21mania) | 判定窗、LN 头按+尾放合成规则 | V1 窗：PERFECT 16 / GREAT 64−3OD / GOOD 97−3OD / OK 127−3OD / MEH 151−3OD / MISS 188−3OD；转谱另一套 16/34·47/67·77/97/121/158；DT·HT 不影响窗口时长；LN：头误差≤窗×1.2 且合计≤×2.4 → PERFECT（GREAT ×1.1/×2.2，GOOD·OK ×1/×2，其余 MEH，体段松键封顶 MEH） | ✅ |
| A3 | [commit 3795d6d "update mania judgement page with ground-truth information"](https://github.com/ppy/osu-wiki/commit/3795d6d161a18dde46cc2c1a30dd6835e71b1a0f) | 该页数据来源 | 提交说明 **"sourced from stable source"**（+36/−17） | 🟡 |
| A4 | [osu!mania — wiki](https://osu.ppy.sh/wiki/en/Game_mode/osu%21mania) | 模式层说明 | 列/键位/玩法梗概 | ⚪ |
| A5 | [Accuracy#osu!mania](https://osu.ppy.sh/wiki/en/Gameplay/Accuracy#osu!mania) | V1 准确率口径 | **V1 下彩 300 与 300 同权(300)**，V2 才 305 | ✅ |
| A6 | [ScoreV1/osu!mania](https://osu.ppy.sh/wiki/en/Gameplay/Score/ScoreV1/osu%21mania) | V1 分数构成 | Base+Bonus 各 50%；HitValue/HitBonusValue/HitBonus/HitPunishment 全表；Mod 乘数表 | ✅ |
| A7 | [Notelock — wiki](https://osu.ppy.sh/wiki/en/Gameplay/Judgement/Notelock) | 反例说明 | 只讲 osu! 标准模式物件锁；**mania 无对应文档** | ✅ |
| A8 | [ppy/osu Discussion #22413 准确率公式](https://github.com/ppy/osu/discussions/22413) | 公式争议 | 社区对 mania acc 公式的推导/纠错 | ⚪ |

---

## 2. B 层：osu!lazer 源码（键状态 → 动作 → 判定，完整链路）

| # | 文件 / PR | 作用 | 状态 |
|---|---|---|---|
| B1 | [ManiaReplayFrame.cs](https://github.com/ppy/osu/blob/master/osu.Game.Rulesets.Mania/Replays/ManiaReplayFrame.cs) | `FromLegacy`：`activeColumns = (int)legacyFrame.MouseX`，逐位 → `ManiaAction.Key1..N`；`ToLegacy` 反向。**列位序权威** | ✅ |
| B2 | [ManiaFramedReplayInputHandler.cs](https://github.com/ppy/osu/blob/master/osu.Game.Rulesets.Mania/Replays/ManiaFramedReplayInputHandler.cs) | 当前帧 `Actions` → `ReplayState<ManiaAction>.PressedActions` | ✅ |
| B3 | [commit b97cab4f（special keys）](https://github.com/ppy/osu/commit/b97cab4f29166a03a46ae9e415a60432b79d34fa) | 特殊键交错的修正：必须用 playfield 各列的 actions | 🟡 |
| B4 | [FramedReplayInputHandler.cs](https://github.com/ppy/osu/blob/master/osu.Game/Rulesets/Replays/FramedReplayInputHandler.cs) | 帧推进 `SetFrameFromTime`；`inImportantSection`（按住且距下帧 ≤ 60帧时间×1.2 时禁止中间时刻）；帧序强制排序 | ✅ |
| B5 | `osu.Game/Input/Handlers/ReplayInputHandler.cs` | 回放输入注入输入栈的基类（namespace `osu.Game.Input.Handlers`） | ✅(间接) |
| B6 | [LegacyReplayFrame.cs](https://github.com/ppy/osu/blob/master/osu.Game/Replays/Legacy/LegacyReplayFrame.cs) / [ReplayFrame.cs](https://github.com/ppy/osu/blob/master/osu.Game/Rulesets/Replays/ReplayFrame.cs) | `ReplayButtonState` 位标志与帧模型 | ✅ |
| B7 | [ManiaInputManager.cs（历史，镜像原文）](https://git.sim1222.com/sim1222/osukey/raw/commit/0579780bb8c38e25675755a4bc5cf045c2e1e7aa/osu.Game.Rulesets.Mania/ManiaInputManager.cs) | `ManiaAction`：`Special1=1, Special2=2, Key1=10 … Key20`；`RulesetInputManager<ManiaAction>` + `SimultaneousBindingMode.Unique` | ✅ |
| B8 | [PR #826 mania-input-base](https://github.com/ppy/osu/pull/826) / [PR #827 mania-note-input](https://github.com/ppy/osu/pull/827) / [commit 79cdba5](https://github.com/ppy/osu/commit/79cdba58c144120335b1eee556697bc558bddbdc) | 2017 年 mania 列/输入/判定骨架 | ⚪ |
| B9 | [DrawableHoldNote.cs](https://github.com/ppy/osu/blob/master/osu.Game.Rulesets.Mania/Objects/Drawables/DrawableHoldNote.cs) + Head/Tail/Body 子类；`DrawableManiaHitObject.cs` | `OnKeyDown/OnKeyUp`、尾释放判定 | ✅ / ✅(部分) |
| B10 | [ManiaHitWindows.cs](https://github.com/ppy/osu/blob/master/osu.Game.Rulesets.Mania/Scoring/ManiaHitWindows.cs) | `ClassicModActive && !ScoreV2Active` 分支 = V1 窗口：`floor(x*mult)+0.5`；great `34+3(10−OD)`、good `67+…`、ok `97+…`、meh `121+…`、miss `158+…`；转谱分支 16/34·47/67·77/97/121/158 | ✅ |
| B11 | [ManiaScoreProcessor.cs](https://github.com/ppy/osu/blob/master/osu.Game.Rulesets.Mania/Scoring/ManiaScoreProcessor.cs) / `ManiaHealthProcessor.cs` / [ManiaLegacyScoreSimulator.cs](https://github.com/ppy/osu/blob/master/osu.Game.Rulesets.Mania/Difficulty/ManiaLegacyScoreSimulator.cs) | V1 计分模拟、Mod 乘数（NoFail/Easy/HT ×0.5，键数修正 0.9 / 0.9−0.04Δ） | ✅ |
| B12 | `osu.Game.Rulesets.Mania/Replays/ManiaAutoGenerator.cs` | 由谱面生成“理想按下/松开帧” → **可作为绑定算法的黄金基准** | ✅(路径) |
| B13 | [PR #33982 mania hit window quirks](https://github.com/ppy/osu/pull/33982)（2025-07-07 合入） | 原文：lazer 旧窗**只在 stable ScoreV2 生效时成立**；非 V2 时 stable 有两套（原谱/转谱） | ✅ |
| B14 | [PR #33882 flooring & half-ms](https://github.com/ppy/osu/pull/33882)、[PR #26452 Classic 用 stable 窗](https://github.com/ppy/osu/pull/26452)、[issue #11311](https://github.com/ppy/osu/issues/11311) | 窗口取整与公式推导 | 🟡 |
| B15 | [PR #34867 secondary keys](https://github.com/ppy/osu/pull/34867) / [Discussion #26350](https://github.com/ppy/osu/discussions/26350) | 副键/多键 → 列映射 | ✅(正文片段) |

**取源技巧（本机 raw 超时时）**：`api.github.com/repos/ppy/osu/contents/<path>`（base64）或 Gitea 镜像 raw
`https://git.sim1222.com/sim1222/osukey/raw/commit/<sha>/<path>`、`https://git.redxen.eu/RepoMirrors/osu/raw/commit/<sha>/<path>`（实测 200）。

---

## 3. C 层：stable（V1）源码与运行时

### 3.1 原始出处
- **`peppy/osu-stable-reference`**（stable 反编译源码）——按键绑定总入口 `osu!/GameplayElements/HitObjectManagerMania.cs`，LN 在 `osu!/GameplayElements/HitObjects/Mania/HitCircleManiaLong.cs`。
  - 现状：`api.github.com/repos/peppy/osu-stable-reference` 与 `ppy/osu-stable-reference` 均 **404**（2026-10-16 复核），仓库搜索 0 命中 → 疑似转私有/移除。
  - 仍可引用的一致性线索（来自 osu-wiki 与 PR）：
    - `HitObjectManagerMania.cs#L147-150`（mania 的 HR/EZ 特殊处理，PR #33982 引用 commit `996648fb…`）
    - `HitObjectManagerMania.cs#L151-160`（速率模组不改窗口）
    - `HitObjectManagerMania.cs#L208-226`（转谱窗口）
    - `HitCircleManiaLong.cs#L235-296`（LN 头尾合成判定）
    - wiki 页引用 commit `1531237b…`
- 社区旁证：[论坛 “Borrow the Source Code”](https://osu.ppy.sh/community/forums/topics/1609854)、[知乎「如何看待 OSU! 源码泄露？」](https://www.zhihu.com/question/46809986)、[论坛 No Lazer Extension 讨论串](https://osu.ppy.sh/community/forums/topics/2112014?n=90)。

#### 3.1.1 镜像 / 存档检索记录（2026-10-16 21:20–21:35，逐渠道实测）

| 渠道 | 方法 | 结果 |
|---|---|---|
| GitHub API 直查 | `GET /repos/peppy/osu-stable-reference`、`/repos/ppy/osu-stable-reference`（各 2 次，均在未限流时） | **404**（非 301 → 不是改名） |
| GitHub 仓库搜索 | `q=osu-stable-reference`、`q=stable reference osu`、`HitObjectManagerMania in:readme`、`HitCircleManiaLong in:readme` | **total_count = 0** |
| GitHub issue/PR 搜索 | `q="osu-stable-reference"` → 96 条（分词命中，非精确）；`q="1531237b63392e82c003c712faa028406073aa8f"` → 17 条（均为 ppy/osu、ppy/osu-wiki 的引用型 PR） | 无镜像线索 |
| Software Heritage | `origin/search/osu-stable-reference`、`origin/search/osu-stable-reference.git`、origin 直查 | 搜索 0 命中；直查 **NotFoundExc**（SWH 从未收录） |
| Common Crawl | `index.commoncrawl.org/CC-MAIN-2025-30`、`CC-MAIN-2024-38` 前缀查询 | **No Captures found**（其余 15 个 collection 返回 503，未取到） |
| GitCode（CSDN 镜像） | 站点页 + `api/v5/search/repositories` | **404 / 空** |
| Gitee | `api/v5/search/repositories?q=osu-stable-reference` | **空** |
| Gitea.com | `api/v1/repos/search?q=osu-stable-reference` | **空** |
| ecosyste.ms（仓库元数据档案） | `repos.ecosyste.ms/api/v1/hosts/GitHub/repositories/peppy%2Fosu-stable-reference` | **Repository not found** |
| Bitbucket | 全局仓库搜索 | 无结果 / 端点不可用 |

**结论：截至 2026-10-16，未发现任何公开镜像或代码存档。** 结合「404 而非 301」「SWH 从未收录」「Common Crawl 无捕获」，最可能是**仓库已转私有或被删除、且从未被公共归档抓取**——这两种情形从外部无法区分。

#### 3.1.2 本沙盒网络不可达、需人工在浏览器复核的渠道

| 渠道 | 待复核 URL | 说明 |
|---|---|---|
| Wayback CDX | `https://web.archive.org/cdx/search/cdx?url=github.com/peppy/osu-stable-reference*&output=text&fl=original,timestamp,statuscode` | 最可能存有 blob 页快照（旧版 GitHub blob 页 HTML 内嵌文件正文） |
| Wayback 直取 | `https://web.archive.org/web/2024/https://github.com/peppy/osu-stable-reference`，以及 `/blob/1531237b63392e82c003c712faa028406073aa8f/osu!/GameplayElements/HitObjectManagerMania.cs` | 若命中即得 stable 源码正文 |
| archive.today | `https://archive.ph/newest/https://github.com/peppy/osu-stable-reference` | |
| Memento 聚合 | `https://memgator.cs.odu.edu/timemap/link/<url>`（本机 404，需换正确路径） | |
| Arquivo.pt | `https://arquivo.pt/wayback/cdx?url=github.com/peppy/osu-stable-reference&output=json` | |
| 代码搜索（关键） | 登录态 GitHub Code Search：`HitCircleManiaLong` / `HitObjectManagerMania` / `SliderEndLenience` | 找“改名 fork / 片段拷贝”的最有效途径 |
| grep.app / Sourcegraph | 浏览器直开 | 本机被 Vercel 校验 / 防火墙拦截 |
| GH Archive / BigQuery | `data.gharchive.org` | 仅能确认存在时间线，无正文 |

#### 3.1.3 建议取回路径（按成功率排序）

1. 浏览器执行 3.1.2 第一条 Wayback CDX 查询；命中 blob 快照即直接下载 `HitObjectManagerMania.cs` / `HitCircleManiaLong.cs`。
2. 用个人 GitHub 账号做 Code Search（唯一免费“按文件内容”找残留副本的途径）。
3. 在 ppy/osu 或 osu!dev 渠道询问该仓库是否已转私有、能否授予访问（PR #33982 作者 bdach 与 peppy 显然有权限）。
4. 退路：§3.2 的 stable 运行时真值通道 + §1 A2 的 osu-wiki 地真值表已覆盖窗口与 LN 规则，缺口仅剩列内仲裁 FSM。

### 3.2 替代：直接读 stable 运行时（等同拿到绑定/判定结果）
| 资源 | 语言 | 说明 |
|---|---|---|
| [l3lackShark/gosumemory](https://github.com/l3lackShark/gosumemory) | Go | ★730，跨平台内存读取；mania 判定/误差输出 |
| [tosuapp/tosu](https://github.com/tosuapp/tosu) | TS | ★573，stable+lazer 内存读取与 pp 计数 |
| [UnnamedOrange/osu-memory](https://github.com/UnnamedOrange/osu-memory) | C++ | 内存读取器 |
| [vys534/MinigameJudge](https://github.com/vys534/MinigameJudge) | — | 基于 gosumemory 的 mania 判定计数 |

---

## 4. D 层：回放解析库（键状态提取）

| 语言 | 资源 | 状态 |
|---|---|---|
| Python | [kszlim/osu-replay-parser](https://github.com/kszlim/osu-replay-parser)（PyPI `osrparse`）、[circleguard](https://github.com/circleguard/circleguard) | 🟡 / 🟡 |
| C# | [mrflashstudio/OsuParsers](https://github.com/mrflashstudio/OsuParsers)、[NuGet OsuParsers](https://packages.nuget.org/packages/OsuParsers/1.5.2) | ⚪ |
| JS/TS | [kionell/osu-parsers](https://github.com/kionell/osu-parsers)、[@voxary/osr-parse](https://www.npmjs.com/package/@voxary/osr-parse)、[osureplayparser](https://www.npmjs.com/package/osureplayparser) | ⚪ |
| Rust | [rosu-replay 0.2.0](https://docs.rs/rosu-replay/0.2.0)、[osu-db::replay::ManiaButtonSet](https://docs.rs/osu-db/0.1.0/osu_db/replay/struct.ManiaButtonSet.html)、[osu-replay-parser types.rs](https://docs.rs/osu-replay-parser/latest/src/osu_replay_parser/types.rs.html) | ⚪ |
| Go | [wieku/rplpa](https://pkg.go.dev/github.com/wieku/rplpa@v1.0.0)、[memsdm05/replay](https://pkg.go.dev/github.com/memsdm05/replay)、[hndada/gosu format/osr](https://pkg.go.dev/github.com/hndada/gosu/format/osr) | 🟡 / ⚪ / ⚪ |
| Java/Kotlin | [opsu-dance ReplayFrame.java](http://git.osufx.com/Sunpy/opsu-dance/src/commit/88bdf256aadfcd18acc7f775e91ceaf00482562a/src/itdelatrisu/opsu/replay/ReplayFrame.java?display=source) | ⚪ |

---

## 5. E 层：已实现“键状态绑定 + mania 判定复算”的工具（交叉验证用）

| 资源 | 语言/星 | 价值 | 状态 |
|---|---|---|---|
| [uyitroa/osr2mp4-core](https://github.com/uyitroa/osr2mp4-core) + [osr2mp4-app](https://github.com/uyitroa/osr2mp4-app) | C/Python ★120/★372 | 逐帧重放按键 + 判定渲染 | 🟡 |
| [Mania-Replay-Master](https://github.com/Mania-Visualization-Project/Mania-Replay-Master) | Kotlin ★77 | mania 专用，offset+judgement 渲染 | 🟡 |
| [bhonris/osu-mania-replay-analyzer](https://github.com/bhonris/osu-mania-replay-analyzer) / [redcray/osuMania_replay_analyzer](https://github.com/redcray/osuMania_replay_analyzer) | — | mania 回放分析 | 🟡 |
| [MaxOhn/rosu-pp](https://github.com/MaxOhn/rosu-pp)（`src/mania/`） + [HitWindows 文档](https://docs.rs/rosu-pp/latest/rosu_pp/model/beatmap/struct.HitWindows.html) | Rust ★139 | 判定窗/pp 参考实现 | 🟡 |
| [kionell/osu-pp-calculator](https://github.com/kionell/osu-pp-calculator) | TS | pp 计算 | ⚪ |
| [mania-judge](https://www.npmjs.com/package/mania-judge) | JS | 纯判定窗 | ⚪ |
| [hndada/gosu](https://github.com/hndada/gosu) | Go ★47 | VSRG 克隆（含 osr/判定） | 🟡 |
| [bot-hibiki osu-mania-web-simu](https://bot-hibiki.github.io/osu-mania-web-simu/main.html)、[tussle1/Keyfall](https://github.com/tussle1/Keyfall)、[4accccc/Cpp_Mania_Player](https://github.com/4accccc/Cpp_Mania_Player)、[telecomadm1145/cmania](https://github.com/telecomadm1145/cmania) | — | 简易 mania 实现 | ⚪ |
| [ManiaRTRender 论坛帖](https://osu.ppy.sh/community/forums/topics/1074833) | — | 实时判定显示 | ⚪ |

---

## 6. F 层：其他开源节奏游戏（输入→判定实现参考）

| 资源 | 说明 | 状态 |
|---|---|---|
| [Quaver/Quaver](https://github.com/Quaver/Quaver) ★802 | C# 竞技向 mania 引擎，输入/回放自成体系 | 🟡 |
| [Cytoid/Cytoid](https://github.com/Cytoid/Cytoid) ★1127 | Unity C#，触屏 mania 类 | 🟡 |
| [Etterna](https://github.com/etternagame/etterna)：[Player Input and Scoring (DeepWiki)](https://deepwiki.com/stepmania/stepmania/2.3-player-input-and-scoring)、[judge.rs](https://docs.rs/etterna/0.1.0/src/etterna/judge.rs.html)、[metaHandInfo](https://docs.etterna.dev/doxygen/html/structmetaHandInfo.html) | 列判定/输入处理（不同判定体系，仅借结构） | ⚪ |
| [thsvkd/autosu_mania](https://github.com/thsvkd/autosu_mania) | 硬件宏式自动打，反向印证按键时序约束 | ⚪ |

---

## 7. G 层：社区讨论（按键注册 / 丢键 / 歧义）

| 资源 | 议题 | 状态 |
|---|---|---|
| [论坛 “Randomly Missing Notes”](https://osu.ppy.sh/community/forums/topics/1091649?start=7521398) | mania 丢键现象 | ⚪(页面 JS 渲染，未取到正文) |
| [ppy/osu Discussion #12984](https://github.com/ppy/osu/discussions/12984) | mania 窗口重叠 → 绑定歧义（mania 版 notelock 议题） | ⚪ |
| [论坛 “The offset processor”](https://osu.ppy.sh/community/forums/topics/1160164) | offset/判定计算 | ⚪ |
| [bilibili《Mania 尾部判定样式的制作分析记录》](https://www.bilibili.com/opus/558074486939244819) | 中文，LN 尾判定窗实测 | ⚪ |
| [DeepWiki ppy/osu](https://deepwiki.com/ppy/osu/1-overview) / [Hit Objects & Playfield](https://deepwiki.com/ppy/osu/3.3-hit-objects-and-drawable-system) / [nyaruku/osu Rulesets](https://deepwiki.com/nyaruku/osu/4.1-rulesets) | 源码结构检索（AI 生成，仅作导航） | ⚪ |

---

## 8. H 层：与本项目的对接点

| 外部结论 | 项目内对应 | 用途 |
|---|---|---|
| ManiaHitWindows Classic：miss = `158+3(10−OD)` → OD4 = **176** | 逆向得到的 OD4 miss 窗 176 | 外部互证，确认 V1 窗口取值 |
| `ManiaReplayFrame.FromLegacy`：列 = MouseX 位序 Key1..N | 项目读取器列映射 | 校验列位序是否与官方一致 |
| `ManiaAutoGenerator` 生成的按键帧 | 绑定算法输出 | 作为“理想绑定”基准做差分 |
| wiki LN：头≤窗×1.2 且 合计≤×2.4 → PERFECT（退档 ×1.1/×2.2、×1/×2） | LN 档位窗 1.5x/1.2x 宽限待精化 | 官方阈值表，直接作为精化目标 |
| wiki：DT/HT 不改窗口**时长** | 逆向结论“窗口 ×速率(DT×1.5/HT×0.75)” | 时基口径不同（真实时间 vs 谱面时间），对齐时必须写明 |
| stable 列内 FSM（loc4 / Loop A / deadline） | 项目逆向主体 | **外部无资料**，仅能靠自身结论 + 0xA4 级内存真值 |

---

## 9. 后续可执行项

1. 用 `ManiaAutoGenerator` 生成理想按键帧，与绑定模拟器做逐帧差分（探针：列位序、按住区间端点）。
2. 拉取 `ManiaReplayFrame.cs` / `ManiaInputManager.cs` 原文（镜像 raw 或 API），把 `ManiaAction` 位序固化进 `build/stable-re/`。
3. 尝试登录态或网页存档获取 `osu-stable-reference`，优先 `HitObjectManagerMania.cs`（`HandleInput` 链路）与 `HitCircleManiaLong.cs`。
4. 用 gosumemory / tosu 采集 stable 运行时的 mania judgement 与按键状态，作为 §8 最后一行的外部真值通道。
5. 本档案若新增抓取，请同步在表格中更新“状态”列并记录 commit sha 与抓取时间。

---

## 10. 检索痕迹（可复现）

- 检索词覆盖：`osu!mania V1 判定机制 按键绑定`、`ScoreV1 judgement hit windows legacy stable`、`osu!lazer LegacyScoreSimulator mania`、`replay key press binding notelock`、`HitObjectManagerMania.cs`、`ManiaButtonSet`、`osr replay key bitmask mania`、`mania key state binding github`、`osu!mania 判定 逆向 源码` 等 24 条。
- 正文抓取来源：GitHub Contents API（ppy/osu、ppy/osu-wiki）、`osu.ppy.sh/wiki`、Gitea 镜像 raw。
- 已知不可达：`raw.githubusercontent.com`（超时）、`github.com` HTML（超时）、`web.archive.org`、`grep.app`（Vercel 校验）。
