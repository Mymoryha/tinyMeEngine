# EXP-0002：显式RTT后处理与独立无窗口GPU复现

- 日期：2026-11-06
- 状态：整个M3工程验收与两入口六组优化实采已完成；最终检查数/日志和发布状态见 [STATE](../../Docs/STATE.md)
- 基准：M2已发布1fad4a6；本轮0.6.0源码尚未提交。与 [EXP-0001](../EXP-0001-triangle/README.md) 组成两套不同pass实验。
- 决策：[ADR-0008](../../Docs/Decisions/ADR-0008-explicit-passes-and-offscreen-gpu.md)

## 输入、输出和边界

固定索引三角形，Camera eye=(0.25,0.125,-3)、target=(0.25,0.125,0)、up=(0,1,0)、FOV75、near0.1/far100，scale0.75、tint=(1,0.8,0.6,0.5)、clear=(0.025,0.055,0.09,1)。客户端/离屏尺寸641×359故意不满足D3D12 copy row pitch对齐，捕获去padding后RGBA8 top-down。

- `none`：scene clear+indexed triangle直接输出最终RGBA8目标。
- `copy`：scene写独立RGBA8纹理、RT→SRV barrier、全屏Load原像素、输出目标。每slot独立scene/output，最终完整覆盖。
- `grayscale`：同一post边界和输入，只替换算法；灰度为dot(rgb,0.2126/0.7152/0.0722)，保留alpha。在UNORM数值域处理，不宣称sRGB/线性光照管理。
- 窗口模式使用swapchain/VSYNC；offscreen无HWND/swapchain/Present，默认1帧，profile有限预算。headless仍纯逻辑，不是GPU测试。
- normalRender没有pixel readback；PNG是最后成功帧显式capture copy/等待/encode边界，不计frame_number或样本。首次RT纹理ID820通过optimized clear值修复，clear值变化在冷边界重建，不隐藏warning。
- PNG由内置WIC内存编码再独占CREATE_NEW保存；report/capture已有文件不覆盖，父目录必须存在，无两输出事务/崩溃原子发布承诺。

## 同输入复现命令

从仓库根目录执行；输出必须新路径，所有参数在Editor/Runtime完全一致，程序部署后可从任意工作目录运行。

```powershell
powershell -NoProfile -File tools/dev.ps1 verify -Configuration RelWithDebInfo
.\out\build\windows-vs2022\bin\RelWithDebInfo\tinyme_runtime.exe --offscreen --postprocess copy --width 641 --height 359 --camera-eye 0.25 0.125 -3 --camera-target 0.25 0.125 0 --fov 75 --scale 0.75 --tint 1 0.8 0.6 0.5 --profile --warmup 30 --samples 120 --capture out/m3-new-copy.png --report out/m3-new-copy.json
.\out\build\windows-vs2022\bin\RelWithDebInfo\tinyme_runtime.exe --offscreen --postprocess grayscale --width 641 --height 359 --camera-eye 0.25 0.125 -3 --camera-target 0.25 0.125 0 --fov 75 --scale 0.75 --tint 1 0.8 0.6 0.5 --profile --warmup 30 --samples 120 --capture out/m3-new-gray.png --report out/m3-new-gray.json
```

direct对照把postprocess改为none，另用新输出名。Editor改可执行文件名，窗口编辑使用`--windowed --postprocess grayscale --post-shader shaders/PostProcess.hlsl`并按F5；场景与post独立generation，任意F5请求使整次测量不完整。候选shader错误保留该pass上个PSO，不保证两shader多文件事务。

## 真实图像证据

2026-11-06，Windows x64/VS2022，RTX5080、驱动32.0.15.7700（577.00），固定DXC1.8.2505.32，HLSL2021 O3 Ges WX。实际两入口顺序运行，未下载新依赖，关闭Debug Layer/GBV，无VSYNC。PNG仅本地忽略产物，不提交二进制；结果可由源码/命令重建。

- Runtime direct与copy PNG全字节SHA256一致；Editor direct/copy和Runtime一致。
- 两入口grayscale PNG全字节SHA256一致、不同于彩色对照。
- JSON保存完整相机/矩阵/几何、post模式/source/options/两个generation、pass_order、各120 raw样本、capture.saved=true；实际frame_number31..150，presentation始终0。颜色图与灰度图已视觉检查，无倒置/遗漏末帧。
- PNG SHA256彩色`2647f0cd1bcd3af848a089de98f63fa09730bec0fd8212ec82ed82154eafbe08`，灰度`bfa7580c99bcedbe9f23487f08d72483e2bf5e5cffc3077512b21f980aeb666e`；这些是非秘密产物校验摘要，不替代像素算法测试。

## 分段GPU与CPU实采

各30预热/120样本，complete=true、timestamp频率1000000000Hz。CPU为scene+post命令录制，不含相机数学/fence/submit/Present/capture/PNG。Scene GPU仅clear+indexed draw；Post GPU仅全屏draw，barrier/resolve在二者范围之外；没有“全帧GPU”字段。

| 入口 / 模式 | CPU mean / median / P95 ms | Scene mean / median / P95 ms | Post mean / median / P95 ms |
| --- | --- | --- | --- |
| Runtime direct | 0.0102166667 / 0.00525 / 0.0322 | 0.00413493333 / 0.004448 / 0.004576 | null |
| Runtime copy | 0.0131116667 / 0.0063 / 0.0344 | 0.00401706667 / 0.004384 / 0.004544 | 0.00241386667 / 0.002368 / 0.00256 |
| Runtime grayscale | 0.0152783333 / 0.0084 / 0.0391 | 0.0041512 / 0.004416 / 0.004544 | 0.0024464 / 0.002368 / 0.002784 |
| Editor direct | 0.00698416667 / 0.0044 / 0.0282 | 0.00397386667 / 0.0044 / 0.004576 | null |
| Editor copy | 0.0178358333 / 0.0097 / 0.0433 | 0.0041816 / 0.004416 / 0.004544 | 0.00244186667 / 0.002368 / 0.00288 |
| Editor grayscale | 0.0138091667 / 0.00775 / 0.0333 | 0.00421333333 / 0.004448 / 0.004544 | 0.00241813333 / 0.002368 / 0.002592 |

产物`out/m3-measure-{runtime,editor}-{none,copy,grayscale}.{json,png,log}`。P95为nearest-rank。没有多轮随机顺序、时钟固定或背景负载控制；数据验证独立测量和额外pass存在，不证明优化收益、CPU/GPU同步整帧时间或两应用优劣。不能对比M2窗口VSYNC实采宣布回归/提升。

## 验收与未测项

完整六组合、专项全图/生命周期/F5测试、独立PNG解码及架构复盘均已通过；真实检查数/日志只在STATE。copy全图误差0、grayscale按独立量化参考max_error=1，奇数行距/alpha/方向/32帧尾样本/反色shader替换与恢复正常，候选根签名ID690/linkageID666明确隔离。架构保留/调整见 [ADR-0009](../../Docs/Decisions/ADR-0009-m3-architecture-review.md)。GPU OOM/Signal/等待超时/物理设备移除未实际注入；report与PNG I/O契约、Win32路径限制和潜在部分发布见ADR-0008/STATE。
