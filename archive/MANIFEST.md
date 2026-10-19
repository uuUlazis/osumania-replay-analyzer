# archive/ 归档清单 (2026-10-17, 深夜XXXVII 收官)

判定机制探索过程的中间文件 (探针输出/IL 分析/批量中间产物/旧引擎基线/视频对照等),
由 build/ 与 build/stable-re/ 移入, **保留原相对路径** (archive/build/... = 原 build/...)。

- 归档文件数: 687
- 失败: 0

## 留在原地 (活性仪器与权威数据, 勿归档)

- docs/ (acc-semantics.md 权威文档 + 4 份档案/协议), backend/, src/ (读取器), scripts/, test/, release/, build/cmake/
- build/: v1_harness.mjs, testset.json, allrows.json, mapinfo.json, anchor_scores_inventory.json, anchor_acc_residual_table.json, v2hits_*(12), stepwise_display_*(12), stepwise_anchor_*, stepwise_remount_*, stepwise_trueblue_new/azdan_ours/base_summary/base_azimuth/base_bz/xueyueyedong
- build/stable-re/: bindsim_v2.mjs, bindsim_batch2.mjs(+fixed.json), bindtruth_table.json, bindkeys_*(44+index), external/ + external_semantics.json, mrm/, stable-reference/, v2anchor/, strictil/meta/il/lib, notelock_v2.mjs, verify_osr.*, engine_*.frames.csv/.raw (已验证帧表 fixture)

## 说明

- 历史引用路径 (HANDOFF/档案中的 build/stable-re/xxx.json) 一律改读 archive/build/stable-re/xxx.json。
- stable_reference_hunt.json / optable_audit.json / m18905_symbols.json 等逆向中间件在 archive 内, 结论已收录于 docs/v2-judgement-semantics.md。
## 追加 (同日): 工作区根目录散件

- archive/root/xxx = 原工作区根目录 xxx: 20 个 .obj (accuracy/analysis/hit_matcher/judgement/LzmaDec/md5/osr_reader/osu_parser/replay_decoder/replay_frames/test-parse/test_mania_core/verify_osr/wv_probe0-7, 手工编译散件, 与 build/cmake 正式产物无关) + debug.log。
- 根目录其余项保留: CMakeLists.txt / ManiaReplayAnalyzer.exe / agent.md / README.md / LICENSE / HANDOFF.md / .gitignore / 各源码与数据目录。