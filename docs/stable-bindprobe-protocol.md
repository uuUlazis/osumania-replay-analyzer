# bindprobe 使用协议 (stable 绑定层取证)

> 工具: osu-mania-live-reader/tools/bindprobe.exe (源码 bindprobe.cs, 只读内存, 不注入不改写)。
> 目的: 把 stable 内部的 press↔note 绑定变成可采集数据, 裁决 miss←hit 家族 (深夜 XIII ①) 与
> good←great 中断封顶 (②)。判读方法见 docs/stable-replay-internals.md §4。

## 采集步骤 (一次 play 完成)

1. osu!stable 启动后 (菜单即可), 运行:
   ```
   tools\bindprobe.exe osu! watchobj 10 300000 > output\bind_watchobj.jsonl
   ```
   watch 类命令会自动等待 play 开始 (最多 30 分钟), menu 中启动是安全的。
2. 在 osu! 里播放 Blue Zenith 0.9x 回放 (家族①锚点局; Azimuth Dan 亦可)。
3. 回放结束后 Ctrl+C 或等时长结束。jsonl 自动逐行 flush, 中途杀进程安全。

## 命令一览

| 命令 | 作用 | 输出 |
|---|---|---|
| cols | 列对象 (obj+0x94 去重) 头部 0x100 dump | 每列一行 bytes |
| side [n] | §9.3 旁路数组定位 (12 字节步长反查) + 伴随指针目标 0x40 | 每命中一行 |
| watchcols <ms> <时长> | 列对象高频 diff | {t, ev:"c", col, off, from, to} |
| watchobj <ms> <时长> [起始索引] [数量] | 未判定物件 0x100 高频 diff (range 模式, 默认 0..512; >2048 个时 ms 自动放宽 ≥20) | 同上 + hdr range 字段 |
| watchobjf <索引文件> <ms> [时长] | 指定 note 索引清单 (逗号/换行分隔) 的 range 模式 | 同上 + hdr indicesFile 字段 |
| scorewatch <ms> <时长> | score+0x24/0x34/0x44 列表增量 | {t, ev:"s", off, snap} |

## 判读要点

- t = bindprobe 相对毫秒 (TickCount), 与 .osr 帧钟的对齐用读取器同款 rice 事件中位偏移法
  (见 stable-memory-recon.md §9.5)。
- watchobj 关注:
  1) **+0x80 (判定) 与 +0x84 (done) 之外的跃迁** — 特别是「写入值 ≈ 某 press 的谱面时刻」的字段
     (绑定记录候选);
  2) **+0xE0/+0xE4** 的状态迁移序列 (0..4), 对齐 press/release 时刻 → LN 状态机真值;
  3) 跃迁顺序 vs .osr 帧 press 顺序 → stable 归属真值表 (谁吃了 press)。
- watchcols 关注: press 时刻列对象中「指针字段的切换」(指向被绑定 note) → notelock 锁定目标。
- 与读取器 jsonl 合并分析: 双方都有物件 index (i) 与 start, 直接对齐。

---

## 附加: gameplay 程序集内存导出 (与绑定采集同场执行)

背景: osu!gameplay.dll (31,854,304 B, 2021-06-23) 是**加密的真实游戏程序集** (全文件熵 ~7.98),
仅在进入 play 时由 osu!.exe 解密加载。绑定主路径的 IL 在里面 — 采集时顺带导出即可离线解析。

**在 play 进行中 (或刚结束后立即)**:

```
tools\memprobe.exe osu! scanbsjb       # 找解密后的元数据 (关注 metaSize 最大的条目)
tools\memprobe.exe osu! scanmz         # 找解密后的 PE 镜像 (第 3+ 个 MZ)
tools\memprobe.exe osu! dumpbin <addr> 40000000 build\stable-re\gameplay_decrypted.bin
```

- dump 起点 = scanmz 的第 3+ 个 MZ 地址 (或 scanbsjb 大条目回退到 MZ); 40MB 覆盖余量,
  离线按 PE 实际大小截断。若 byte[] 加载 (无 MZ), 用 BSJB 地址向上/向下探 4KB 边界。
- 导出后处理: build/stable-re 工具链解析 (lib.mjs 已参数化 OSU_BIN), 定位 mania
  判定/绑定类 → +0xA4 三态写入方法 → 判别式提案。
- 若 BSJB 扫描为 0: 解密负载未按托管 byte[] 驻留 — 改为对 osu! 进程全堆做高熵块导出
  (备用方案, 需 lead 批准)。

## 深夜 XXIV 附录: range/indices 模式 (残差精准打击)

- 用法: tools\bindprobe.exe osu! watchobj 10 300000 <起始索引> <数量> (例: 1394 512)
  或 tools\bindprobe.exe osu! watchobjf residual_idx.txt 10 300000
- hdr 行带 range/indicesFile 溯源; >2048 索引自动放宽 ms≥20 (轮询成本保护)。
- 回归验证: 同一回放播放后, 用 build/stepwise_bindverify.mjs 比对新采集与
  深夜 XXII 真值表 (note 0-511 段 0xA4 末值应逐 note 一致)。
- 首选采集批次 (bz 残差精准打击, 4 次播放): watchobj 10 300000 1394 512 /
  2418 512 / 4635 512 / 5686 512。
