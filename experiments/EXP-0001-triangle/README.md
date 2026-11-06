# EXP-0001：可诊断的 HLSL 三角形与失败恢复

日期：2026-11-06；状态：M2-001 shader、M2-002参数/计时和M2-003索引几何/相机均已验收，M2完成；不是引擎整体benchmark。前半部保留历史实验，当前相机与几何验证见末节。取舍见 [ADR-0005](../../Docs/Decisions/ADR-0005-dxc-and-transactional-shader-reload.md) / [ADR-0006](../../Docs/Decisions/ADR-0006-experiment-parameters-and-timing.md) / [ADR-0007](../../Docs/Decisions/ADR-0007-indexed-triangle-and-camera.md)。

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

M2-001历史GPU测试使用未绑定b0的shader；M2-002使b0成为参数契约，当前回归改测未绑定b1：候选PSO被root契约拒绝，报告预期ID690，旧管线继续，修正后恢复。这类已隔离候选诊断不是被隐藏的正常GPU错误。未测任意复杂shader/resource契约。

## M2-001历史性能与限制

M2-001当时不报告FPS/耗时/优化收益；presented_frames与simulated_seconds都不是GPU计时。编译和重载同步，GPU idle发生在人工重载边界；可能短暂停顿。当时没有CPU/GPU计时或参数资源契约；缓存、依赖监听、多文件include原子快照、shader调试仍未实现。

原实验支持“最小可修改shader链路和错误恢复”的假设，不证明未来全引擎可扩展性。当前扩展参数与计时如下；第二个真实pass用例出现后才评估抽象。

## M2-002：参数与独立计时

### 输入与契约

2026-11-06，0.4.0未提交工作区，基准为已推送834c297。无资产/纹理/随机数/相机，仍是同一三角形；不可只用引擎版本号还原未提交源码。CPU/HLSL布局、单位、更新时刻与生命周期见ADR-0006。

- 参数默认scale1、tint1/1/1/1；32字节/16对齐、b0八DWORD，每次Render复制，避免共享upload覆盖in-flight。
- 正确性参照：640×360客户区，中心(320,180)默认RGB146/93/92；红色tint后146/0/0；scale0.5后原内部点(390,180)变clear6/14/23；重载保持非默认参数。Debug/优化、GBV开关均实际通过。各通道容差2，不使用边缘逐像素完全一致。
- 连续交替红/绿参数更新通过真实入口验证；测试采桌面合成坐标，固定100ms稳定等待与DwmFlush只发生在捕获边界，不计入被测正常帧。窗口GDI DC在flip-model下可能保留旧重定向内容，不能作为可靠的新帧参照。测试需要窗口未被遮挡。
- `tint.a`修改shader输出alpha，但当前PSO不混合，不代表透明合成。

### 可复现采样

根目录先构建优化配置，输出路径须是新文件：

```powershell
powershell -NoProfile -File tools/dev.ps1 verify -Configuration RelWithDebInfo
.\out\build\windows-vs2022\bin\RelWithDebInfo\tinyme_runtime.exe --windowed --shader shaders/Triangle.hlsl --scale 0.75 --tint 1 0.8 0.6 1 --profile --warmup 30 --samples 120 --report out/profile-new-runtime.json
.\out\build\windows-vs2022\bin\RelWithDebInfo\tinyme_editor.exe --windowed --shader shaders/Triangle.hlsl --scale 0.75 --tint 1 0.8 0.6 1 --profile --warmup 30 --samples 120 --report out/profile-new-editor.json
```

此次实采环境：1280×720、RGBA8 UNORM/SDR、VSYNC on、RTX5080/driver32.0.15.7700（NVIDIA577.00）、VS2022/CMake/SDK/DXC同上；CPU RelWithDebInfo，shader HLSL2021/O3/Ges/WX；Debug Layer=false、GBV=false。各30成功呈现帧预热/120有效样本，complete=true、无环境变化/关闭/丢样本；timestamp频率1000000000Hz。两应用顺序运行，不是受控并发对比。

| 入口 / 范围 | mean ms | median ms | nearest-rank P95 ms |
| --- | --- | --- | --- |
| Runtime CPU录制 | 0.110979167 | 0.1118 | 0.1447 |
| Runtime GPU clear+triangle | 0.00476 | 0.004896 | 0.004992 |
| Editor CPU录制 | 0.11567 | 0.11455 | 0.1462 |
| Editor GPU clear+triangle | 0.0051472 | 0.00496 | 0.006368 |

实际输出`out/m2-002-profile-{runtime,editor}-RelWithDebInfo.json`及同名.log，JSON含原始样本/min/max/配置/参数/范围，产物忽略；表内数值为长期摘要。这里只验证独立计时链路，不据应用间微小差异声称算法或架构性能差异。

### 测量口径与失败

- CPU：allocator reset至command-list close，含计时命令录制开销；排除fence wait/提交/Present/诊断/F5，不是整个CPU帧。
- GPU：clear前与可选triangle draw后timestamp；排除barriers/query resolve/Present/VSYNC/F5，不是整帧GPU时间。时间量级很小，query、驱动调度和验证层开销不可忽略；未控制GPU时钟、功耗或系统背景负载，不做性能优化结论。
- fence完成后才能读slot，TakeCompletedTimings不等待；退出沿已有WaitIdle领取尾帧。自动测试18样本generation/size与顺序准确，有界256队列主动溢出4条并明示，未偷偷丢数据。
- CLI自动测试两入口2预热/3样本、clear0预热/1尾样本、中文报告路径、不覆盖，以及无GPU非法参数路径。JSON空统计是null，clear不虚称加载DXC/应用三角形参数。
- 真实关闭/F5/最小化恢复/resize都得到不完整报告，安全退出；消息批次快速恢复由surface revision保留变化。纯DXGI遮挡未单独制造实测，实现明确标记不完整。CLI不完整返回1，报告同时保留原因；未知GPU/API错误直接失败。
- M2-002历史时默认verify无GPU，未有网格/相机，本切片当时未宣称整个M2结束。当前索引相机已完成如下；仍无通用Profiler/UI参数编辑/offscreen、多queue/pass校准、shader依赖摘要或PIX。

## M2-003：索引几何与透视相机

### 当前输入与资源

2026-11-06，0.5.0未提交工作区，基准834c297；固定DXC/工具/RTX5080/驱动沿用前文。不引用引擎版本作为未提交源码的唯一标识。前文无VB/相机描述均为M2-001/002历史，不是当前实现。

- 具体静态几何为POSITION3/COLOR3、24字节VB、R16_UINT索引3/0/2，四个顶点中一个离屏未引用，其余为默认三角形。世界位置x±2.31111111、y±1.3、z0；默认16:9投影保持历史clip.xy±0.65和顶点色。错误非索引draw或顺序索引不能混过RGB参照。
- DEFAULT buffer实际以COMMON创建，初始化时显式转换COPY_DEST、copy、再转换VB/IB。首次上传fence完成后释放Renderer持有的staging，F5/resize复用资源；上传不增加呈现、样本或generation。初次COPY_DEST创建产生驱动ID1328警告，修复而非隐藏。
- b0仍原32字节tint/scale；b1是独立64字节row-major矩阵，VS-only root constants。每次录制复制，模型identity，CPU左手View*Projection与HLSL行向量mul对应、不转置。
- 默认eye=(0,0,-2)、target=(0,0,0)、up=(0,1,0)、垂直FOV90、near0.1/far100；resize按客户区aspect保持几何比例。scale仍作用于投影后clip.xy而非模型空间。没有深度buffer或资产导入/场景。

### 实际GPU正确性

Debug/优化、Debug Layer与GBV开关各模式通过。客户区640×360，内部点比较，各通道容差2；桌面采集仍固定100ms/DwmFlush只在捕获边界，需窗口不遮挡。

| 输入 / 操作 | 参照 | 结果 |
| --- | --- | --- |
| 默认索引三角形 | 中心(320,180) RGB146/93/92 | 与历史默认相同 |
| 红tint/scale0.5 | 中心红146/0/0，(390,180)为clear6/14/23 | 原参数/F5回归保持 |
| eye=(1.5,0.3,-2)、target=(1.5,0.3,0) | 原中心露出clear，投影点(185,207)有插值RGB | 非对称矩阵平移正确 |
| 24帧交替默认/平移相机，随后F5 | 最后平移画面保持 | 无in-flight旧矩阵覆盖；无重复网格上传 |
| eye=(2,0,-2)、target默认 | (320,139)偏红 | 斜视方向和透视插值正确 |
| eye z=-6；FOV60 | 原内部点露clear；窄FOV扩大几何 | 透视距离/FOV生效 |
| far=1或near=3 | 中心clear | D3D [0,1]裁剪正确 |
| resize到800×360 | (480,180)内部，(530,180)外部clear | 像素宽度保持，不横向拉伸 |

相机资源测试46成功呈现样本编号唯一、尾样本保持；非法相机无提交，upload/staging/last-use fence与安全释放实测。旧18样本generation/尺寸、256队列/4 dropped、四类窗口中断、clear/headless全部保留。NORMAL输入缺失预期ID65、未绑定b2预期ID690候选拒绝保留旧PSO；这些隔离候选错误不冒称“从未有诊断”。正常GPU与释放意外warning/error/live child为0。

### 当前复现与实采

根目录，输出为新文件；Editor使用相同选项：

```powershell
powershell -NoProfile -File tools/dev.ps1 verify -Configuration RelWithDebInfo
.\out\build\windows-vs2022\bin\RelWithDebInfo\tinyme_runtime.exe --windowed --shader shaders/Triangle.hlsl --camera-eye 0.25 0.125 -3 --camera-target 0.25 0.125 0 --fov 75 --scale 0.75 --tint 1 0.8 0.6 1 --profile --warmup 30 --samples 120 --report out/indexed-camera-new-runtime.json
```

实际两入口顺序运行1280×720、RGBA8 UNORM/VSYNC、优化CPU与O3 HLSL、Debug Layer=false/GBV=false；上述相机/参数、near0.1/far100。各30预热/120样本，complete=true、无环境变化/关闭/丢样本，timestamp频率1000000000Hz。JSON含实际顶点/索引/stride/fence、相机输入与16浮点矩阵，b0 parameter_bytes=32与camera matrix_bytes=64分别记录；clear geometry/camera为null。

| 入口 / 范围 | mean ms | median ms | nearest-rank P95 ms |
| --- | --- | --- | --- |
| Runtime CPU录制 | 0.130734167 | 0.123 | 0.2276 |
| Runtime GPU clear+indexed triangle | 0.00464213333 | 0.004704 | 0.004896 |
| Editor CPU录制 | 0.130849167 | 0.12055 | 0.2359 |
| Editor GPU clear+indexed triangle | 0.00467093333 | 0.004768 | 0.004928 |

输出`out/m2-003-profile-{runtime,editor}-RelWithDebInfo.{json,log}`，产物忽略，长期证据是表格/命令/源码与STATE。CPU范围排除相机数学/初始化上传，GPU范围排除初始化上传/barrier/resolve/Present；均非整帧。输入已不同于历史M2-002，时钟/背景负载未控制，禁止据两表微小差异宣布性能优化或应用间优劣。

M2工程验收完成；M3和幼年期整体未完成。下一切片为离屏颜色目标与可替换全屏后处理，仍不预建Scene/Render Graph；渲染到纹理不等于无窗口GPU，独立离屏执行模式后续明确。完整六组合与检查数只在STATE维护。
