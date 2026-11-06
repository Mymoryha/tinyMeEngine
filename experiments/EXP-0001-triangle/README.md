# EXP-0001：可诊断的 HLSL 三角形与失败恢复

日期：2026-11-06；状态：最小实验完成，非性能实验。关联任务 M2-001，取舍见 [ADR-0005](../../Docs/Decisions/ADR-0005-dxc-and-transactional-shader-reload.md)。

## 问题与假设

同一Windows/D3D12基础能否由Editor与Runtime复用，编译HLSL三角形，并在修改错误时保留上一有效画面，恢复正确代码后安全替换？

通过条件：两入口GPU有限帧成功；F5错误不换generation/画面；修正后generation增1、画面变化；资源验证无意外warning/error/live child。不是验证整套Render Graph，也不测GPU耗时。

## 实现与输入

- 源码：[Triangle.hlsl](../../shaders/Triangle.hlsl)，VSMain/PSMain、vs_6_0/ps_6_0、HLSL2021/O3/-Ges/-WX。
- SV_VertexID产生三个顶点，无vertex buffer、相机、资产、随机数或纹理；位置与顶点色在HLSL明确声明。
- clear → triangle → present，RGBA8 UNORM、SDR、1280×720、VSYNC；clear色0.025/0.055/0.09/1。
- Windows11 x64 / Ryzen9950X / RTX5080 / NVIDIA577.00；CMake3.31.10、VS2022 v143、SDK26100；DXC1.8.2505.32。Debug/RelWithDebInfo均验证，Debug Layer/GBV开关分别记录。
- 实测时为未提交工作区；发布锚点为包含本实验记录的0.3.0首次工程基线提交，具体hash从Git历史取得，不引用自身hash。完整当前验证位置在STATE。

## 复现

从仓库根目录；首次安装见README，后续不联网：

```powershell
powershell -NoProfile -File tools/dev.ps1 verify -Configuration Debug
.\out\build\windows-vs2022\bin\Debug\tinyme_editor.exe --windowed --shader shaders/Triangle.hlsl
```

修改源文件，按F5重编译；语法错误时看console诊断，画面保持，修正后再F5。Escape退出。默认不传shader时读取程序旁部署副本；开发时建议显式指定仓库源，避免修改错文件。清屏对照用 `--windowed --experiment clear --frames 8`；有限三角形用 `--windowed --frames 8 --gpu-validation`。

自动回归：`tools/dev.ps1 gpu-test`，或独立GPU CTest。前者包含默认CPU/DXC检查、GBV开关两套GPU生命周期/重载以及两应用严格CLI；默认verify不初始化GPU。

## 真实桌面观察与正确性

使用隔离的中文路径临时shader（只位于out，本轮结束清理），启动Debug Editor+GBV。仅截取引擎客户区并自动发送F5/Escape，不宣称人手点击：

| 步骤 | generation | 中心RGB | 结论 |
| --- | --- | --- | --- |
| 默认插值色三角形 | 1 | 146,93,92 | 真实三角形可见 |
| 写入语法错误并F5 | 1 | 146,93,92 | 编译失败保留旧画面 |
| PS改为float4(0.9,0.1,0.6,1)，F5 | 2 | 229,25,153 | 成功替换为新颜色 |

像素比较容差每通道2；采样在三角形内部，不依赖边缘光栅化完全一致。输出：`out/m2-triangle.png`、`out/m2-reload-rejected.png`、`out/m2-reload-success.png`；日志`out/m2-interactive-reload.log`。这些小过程产物仍不进Git，表内数值与命令是长期摘要，不以截图可用性替代复现。

GPU自动测试另验证正确编译但引用未绑定b0的shader：候选PSO被root契约拒绝，报告预期ID690，旧管线继续，修正后恢复；这类已隔离候选诊断不是被隐藏的正常GPU错误。未测任意复杂shader/resource契约。

## 性能与限制

不报告FPS/耗时/优化收益；presented_frames与simulated_seconds都不是GPU计时。编译和重载同步，GPU idle发生在人工重载边界；可能短暂停顿。没有CPU/GPU计时、缓存、依赖监听、多文件include原子快照、shader调试或完整参数系统。

本实验支持“最小可修改shader链路和错误恢复”的假设，不证明未来全引擎可扩展性。下一切片建议加入明确的参数数据契约与独立GPU计时，再逐步扩展网格/相机；第二个真实pass用例出现后才评估抽象。
