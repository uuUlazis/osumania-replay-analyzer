# osu!mania Replay Analyzer

本地 osu!mania 回放分析工具：解析 `.osr` 回放与对应 `.osu` 谱面，重建每一颗音符的判定与按键偏移，
给出准确率、偏移统计、一致性核查与可视化时间轴。全部数据在本地处理，不访问网络。

## 交付物

- 单一可执行程序：`build/cmake/bin/ManiaReplayAnalyzer.exe`（前端资源已内嵌，无需安装）
- 双击运行：优先以 WebView2 内嵌窗口打开；失败时自动回退到系统默认浏览器
  （窗口关闭后服务随进程退出；浏览器模式下关闭标签页后请退出托盘进程/结束进程即可）

## 使用方法

1. 点击「回放 .osr」选择回放文件，点击「谱面 .osu」选择对应谱面（皮肤文件夹、音乐文件均可选）。
2. 点击「加载分析」。左侧为准确率/诊断/选段面板，右侧为时间轴、下落式预览与命中表。
3. 时间轴：单击跳转；长按 250ms 后拖动可选段（选段后在左下角查看选段 ACC 与偏移）。
4. 回放预览「判定显示」开关：开启后叠加显示每次输入的位置与其判定——
   每列虚线中心线为参考；判定色横条标记实际按下/松开时刻（偏离物件即提前/压后，颜色与判定徽章一致：300g 蓝、300 黄、200 绿、100 橙、50 玫红、MISS 红）；命中的物件以对应判定色描边（描边严格限制在本列宽度内，不会越界到相邻轨道）。
5. 「判定规则」下拉选择预览叠加使用的判定版本：
   - **V1 稳定**：整根长条共享一个综合判定——头按或尾松超出 miss 窗口则整条判 Miss，50~miss 窗口之间的灰区按压/松开不直接判死、由综合公式定档（下限 50），描边显示该综合判定色；
   - **V2 ScoreV2**：长条头部与尾部分别判定——头部保持自身描边色，尾部（松开时刻）单独判定并以色带标注在长条尾端，两者可以不同。
6. 「音乐(可选)」选择 mp3/ogg/wav/flac/m4a 音频后，预览播放时会同步播放该音乐。
   音频经 WebAudio decodeAudioData 整曲解码为 PCM 后播放：跳转即精确的采样偏移，
   不经过浏览器的 mp3 字节映射（VBR mp3 按平均码率估算落点可偏差数秒）；
   音频轨是唯一时钟源，预览每帧对齐解码时钟（偏差超过阈值做一次精确跳转），
   点击跳转与拖动释放都一步到位；拖动过程中音频停靠，松手后从滑块位置继续。
   DT/NC 谱面的音频以 1.5×、HT 以 0.75× 播放（预览时间轴保持谱面时间，与回放帧一致）。
   未选择音乐则静默预览。「音量」滑杆实时调节音乐音量。

### 命令行参数

| 参数 | 说明 |
| --- | --- |
| `--headless` | 不创建窗口，仅启动本地 API 服务 |
| `--port N` | 指定端口（默认随机分配，仅绑定 127.0.0.1） |

### WebView2 故障诊断

- 内嵌窗口启动失败（回退浏览器）时，exe 旁会生成 `mra_webview.log`，记录失败阶段与 HRESULT，
  例如 `webview init failed (controller): hr=0x8000FFFF`。
- 若日志显示 controller 阶段 0x8000FFFF：多为 WebView2 运行时浏览器进程启动即崩溃
  （事件查看器可见 msedgewebview2.exe 在 msedge_elf.dll 中 0x80000003 异常）。
  可尝试设置 `MRA_WEBVIEW2_NOSANDBOX=1` 后启动；仍失败则使用自动回退的浏览器模式，分析功能不受影响。

程序会在 exe 旁写入 `mra_webview.state`，记录上一次成功的 WebView2 初始化方式；下次启动直接优先使用该方式，跳过必然失败的尝试以加快启动。删除该文件可恢复默认尝试顺序。
- 程序会按顺序自动尝试多个 WebView2 用户数据目录：默认 `%LOCALAPPDATA%\ManiaReplayAnalyzer\WebView2` →
  exe 旁的 `webview-profile` 文件夹，两者各尝试一次沙箱模式与 `--no-sandbox` 模式；
  任一尝试成功即在窗口内显示前端。可用 `MRA_WEBVIEW2_DATA` 环境变量指定自定义用户数据目录（优先级最高）。
- 环境变量 `MRA_WEBVIEW2_NOSANDBOX=1`：禁用 WebView2 浏览器沙箱。仅当浏览器进程沙箱引导失败
（控制器创建返回 0x8000FFFF）时使用；正常机器请保持默认沙箱模式。

## 构建（Windows）

依赖：Visual Studio（MSVC v14.5x + Windows SDK）、CMake+Ninja（VS 自带）、Node.js + pnpm。

```bat
scripts\build.bat   :: 前端 pnpm build → CMake 配置 → 编译 ManiaReplayAnalyzer.exe
scripts\test.bat    :: 构建并运行 mra_tests 单元测试
```

脚本内的工具链路径写在文件头部（vcvars64.bat / cmake.exe / ninja.exe / pnpm.cmd），按需修改。

## 本地 HTTP API（127.0.0.1，仅本机）

统一响应封装：`{ok:true,data:{...}}` / `{ok:false,error:{code,message,details}}`。

| 端点 | 说明 |
| --- | --- |
| `GET /api/health`、`GET /api/app/info` | 健康检查 / 应用信息 |
| `POST /api/dialog/open-file`、`POST /api/dialog/open-folder` | 原生文件/文件夹对话框 |
| `POST /api/replays/load` | 载入回放+谱面（解析、匹配、分析一次完成），返回 sessionId |
| `GET /api/replays/{sid}/metadata` `GET /api/replays/{sid}/frames?from&to&limit` | 回放元数据 / 按时间分页帧 |
| `GET /api/beatmaps/{sid}/metadata` `GET /api/beatmaps/{sid}/objects?from&to` | 谱面元数据 / 物件 |
| `GET /api/analysis/{sid}/summary` `GET /api/analysis/{sid}/hits?from&to&limit` `GET /api/analysis/{sid}/holds` `POST /api/analysis/{sid}/segment` | 总分析 / 命中明细 / 长按头尾判定 / 选段分析（[start,end) 半开区间） |
| `POST /api/skins/load`、`GET /api/skins/{sid}/resource/{element}` | 皮肤载入 / 皮肤图片资源 |
| `POST /api/scores/load`、`GET /api/scores?mode&limit` | 载入本地 scores.db（自动推导旁侧 Data\r）/ 按模式查询成绩 |
| `POST /api/scores/datar` `POST /api/scores/songs` | 手动指定 Data\r 回放目录 / Songs 谱面目录（均只读） |
| `POST /api/scores/browse` `POST /api/scores/load-replay` | 成绩浏览（懒建 Data\r + Songs 索引）/ 按 playedTicks 一键载入回放+谱面+音乐 |

错误码：`INVALID_JSON` `MISSING_PATH` `REPLAY_FILE_NOT_FOUND` `REPLAY_PARSE_FAILED` `REPLAY_DECODE_FAILED`
`REPLAY_FRAMES_INVALID` `BEATMAP_FILE_NOT_FOUND` `BEATMAP_NOT_MANIA` `BEATMAP_PARSE_FAILED` `SESSION_NOT_FOUND`
`RESOURCE_NOT_FOUND` `SKIN_NOT_LOADED` `SCORES_NOT_LOADED` `PARSE_ERROR` `INTERNAL_ERROR`；对话框取消返回 `data.code=DIALOG_CANCELLED`。

## scores.db 本地成绩库（只读）

在文件载入面板选择 `scores.db`（通常位于 osu! 安装目录）。程序**只读解析**该文件，绝不写入或修改：

- 载入后自动在 scores.db 同目录检索 `Data\r` 回放目录；找不到或想换目录时点右侧 `Data\r 目录` 手动选择
- 选择 Songs 目录后即可打开 **回放列表**（应用内窗口，类似资源管理器）：
  - 首次打开会扫描 Data\r 顶层的 .osr（建立 `谱面MD5-FILETIME ticks` 文件名索引与内嵌 replay MD5 索引）与 Songs 全部 .osu（计算谱面 MD5 + Title/Artist/Creator/Version/AudioFilename），数万文件约需半分钟；之后每次打开都会自动检测 scores.db / Data\r / Songs 的磁盘变化并**增量刷新**（osu! 正在写入新成绩也没关系），未变化的文件只做 stat 检查，刷新通常一两秒内完成
  - "有回放"判定与游戏内一致：按 osu! 的配对方式用 `<谱面MD5>-<FILETIME ticks>` 文件名匹配（±1ms 容差），replayMd5 匹配作回退；因此列表口径与实际可载入完全一致
  - 列表显示日期、谱面名、曲师、谱师、玩家、分数、ACC、Miss、Mods、是否有回放；支持搜索与按 日期/谱面名/曲师/谱师/玩家 升降序排序
  - 单选一条"有回放"的成绩 → `确认载入分析`：按成绩的 playedTicks（.NET ticks，以字符串传输避免超出 JS 安全整数）从 Data\r 取 .osr、从 Songs 取谱面与音乐并完成全部分析
- ACC 按 mania 权重本地重算：`(300·MAX + 200·200 + 100·100 + 50·50) / (判定总数·300)`
- Mods 显示键型（1K-9K）在前的完整列表

## 判定与统计口径

- 判定窗口（OD）：MAX ≤16ms；300 ≤64−3·OD；200 ≤97−3·OD；100 ≤127−3·OD；50 ≤151−3·OD；MISS ≤188−3·OD
- 偏移 `offsetMs = 实际按键时刻 − 物件时刻`（负值=提前）；长按尾部松开晚于尾部时刻记为 MAX（偏移 0）
- 准确率：V1 权重 MAX=300，V2 MAX=305（另提供 320 口径）；分段 ACC 用同一模型重算，不按全局比例折算
- 判定匹配采用按列 FIFO 状态机（非最近邻）：和弦逐列独立判定；长带头/尾分别判定；无输入时也会推进 MISS 扫描
- 按键接受窗不对称（对齐 stable 档位窗语义）：早按仅在 100 窗内命中；晚按直到 50 窗仍可命中（物件在 miss 扫描前保持可打）。50~miss 窗口之间的灰区空按既不计判定也不消费物件——物件留给下一次按键，滚链里的灰按不会引发连锁错位
- 长条共享一个综合判定（V1）：头按或尾松超出 miss 窗判整条 Miss，灰区存留由综合公式定档（下限 50）
- 一致性报告：谱面 MD5、模式、判定计数（OSR 声明 vs 本地重建，mania 计数含长按尾）、回放正常结束
- 注意：判定重建与 `.osr` 声明计数已高度一致（8 张实测谱面 ACC 偏差 ≤1.0 个百分点，多数 ≤0.3）；剩余小幅差异集中在长条密集段（stable 对长按尾判定存在未公开的放宽细节）。此时 `judgementCountsMatch` 会如实报 false，分析本身仍然正确。

## 架构

```
backend/src/
  io/        BinaryReader, MD5
  replay/    .osr 解析、LZMA 解压、帧解码
  beatmap/   .osu 解析（列映射、长带、时间点）
  mania/     判定窗口、按列匹配器、准确率、统计分析
  skin/      皮肤加载（mania-note{col}[H|B] 命名，含回退）
  session/   会话存储（大数组留在 C++ 内存，前端只取窗口）
  http/      httplib 本地 API（httplib.h / nlohmann json 内嵌于 third_party）
  app/       原生对话框、内嵌资源、入口
  webview/   WebView2 壳（失败回退默认浏览器）
frontend/src/ React + Vite：时间轴（长按选段）、Canvas 预览、分析面板
backend/tests/ 单元测试 + gen_fixture.py 生成 fixtures/{replays,beatmaps}
```

## 文件签名与 SmartScreen

本机编译的 exe 不做代码签名，与 mania skin editor 完全一致（双方均未签名、均无 Mark-of-the-Web；本 exe 内嵌标准 VERSION_INFO 资源）。发布产物位于 `release\ManiaReplayAnalyzer.exe`（build.bat 第 4 步自动生成，并同步一份到项目根目录）。

**单一可执行程序**：前端界面与分析逻辑全部内嵌于 exe（RCDATA 资源），运行时不在 exe 旁边生成任何文件——WebView2 用户数据目录、日志、启动状态文件统一写入 %LOCALAPPDATA%\ManiaReplayAnalyzer\（%TEMP% 为备选；仅当系统可写目录全部不可用时才作为最后手段回退到 exe 旁）。release\ 目录在运行后始终只包含 exe 本身。

**SmartScreen"Windows 已保护你的电脑"的真实原因（已定位并修复）**：本项目目录树曾被早期沙箱实验写入 **Low Mandatory Integrity 标签**，构建/复制进来的 exe 会继承该标签；SmartScreen 对"未签名 + 零云信誉 + Low 完整性"的 exe 一律弹窗拦截——与 Mark-of-the-Web 无关（所以属性里没有"解除锁定"勾选框）。mania skin editor 的目录树没有该标签，所以从不弹窗。修复：给 exe 设置显式 Medium 完整性标签覆盖继承，build.bat 部署步骤已内置：

    icacls ManiaReplayAnalyzer.exe /setintegritylevel M

若弹窗仍出现（例如把 exe 复制到带网络标记的目录、或从浏览器/聊天工具下载），点"更多信息 → 仍要运行"一次即可；彻底消除需受信任的代码签名证书（Azure Trusted Signing 或 OV/EV）。

## 已知限制（phase 1）

- Mod：仅识别 Mirror/Coop 并提示；KeyX/ScoreV2 等不参与分析（在诊断中标注）
- 音频播放未接入（预览使用单调时钟）
- 皮肤不完整时使用内置默认渲染回退
- 列灯光/打击特效严格按皮肤资源渲染:皮肤将灯光贴图置空(如 1×1 透明图)时预览同样无灯光,与游戏内表现一致
