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
   - **V1 稳定**：整根长条共享一个综合判定（头按或尾松不及格则整条降级），描边显示该综合判定色；
   - **V2 ScoreV2**：长条头部与尾部分别判定——头部保持自身描边色，尾部（松开时刻）单独判定并以色带标注在长条尾端，两者可以不同。
6. 「音乐(可选)」选择 mp3/ogg/wav/flac/m4a 音频后，预览播放时会同步播放该音乐
   （进度与时间轴保持同步，跳转/暂停即时跟随；未选择则静默预览）。「音量」滑杆实时调节音乐音量。

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

错误码：`INVALID_JSON` `MISSING_PATH` `REPLAY_FILE_NOT_FOUND` `REPLAY_PARSE_FAILED` `REPLAY_DECODE_FAILED`
`REPLAY_FRAMES_INVALID` `BEATMAP_FILE_NOT_FOUND` `BEATMAP_NOT_MANIA` `BEATMAP_PARSE_FAILED` `SESSION_NOT_FOUND`
`RESOURCE_NOT_FOUND` `SKIN_NOT_LOADED` `INTERNAL_ERROR`；对话框取消返回 `data.code=DIALOG_CANCELLED`。

## 判定与统计口径

- 判定窗口（OD）：MAX ≤16ms；300 ≤64−3·OD；200 ≤97−3·OD；100 ≤127−3·OD；50 ≤151−3·OD；MISS ≤188−3·OD
- 偏移 `offsetMs = 实际按键时刻 − 物件时刻`（负值=提前）；长按尾部松开晚于尾部时刻记为 MAX（偏移 0）
- 准确率：V1 权重 MAX=300，V2 MAX=305（另提供 320 口径）；分段 ACC 用同一模型重算，不按全局比例折算
- 判定匹配采用按列状态机（非最近邻）：和弦逐列独立判定；长带头/尾分别判定；无输入时也会推进 MISS 扫描
- 一致性报告：谱面 MD5、模式、判定计数（OSR 声明 vs 本地重建，mania 计数含长按尾）、回放正常结束
- 注意：osu!stable 对长按尾判定存在未公开的放宽系数（源码不可得），因此稳定端回放的判定计数与本地重建可能存在小幅差异（实测约 ±1%~3%，如 MAX300/300 之间少量偏移）。此时 `judgementCountsMatch` 会如实报 false，分析本身仍然正确。

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
- 音频播放未接入（预览使用单调时钟）；scores.db 为 phase 2 计划
- 皮肤不完整时使用内置默认渲染回退
- 列灯光/打击特效严格按皮肤资源渲染:皮肤将灯光贴图置空(如 1×1 透明图)时预览同样无灯光,与游戏内表现一致
