# 当前状态与接手板

更新时间：2026-11-06。唯一当前进度源；重要历史见 [DEVLOG](DEVLOG.md)。

## 1. 当前定位

- 幼年期：M0、M1及M2-001首个shader实验已完成工程验收；整个M2尚未结束。
- 版本0.3.0；本快照作为首次工程基线提交，包含M0/M1/M2-001。提交ID与实时工作区/远端状态以Git记录为准，不在提交内容里引用自身尚未生成的hash。
- 2026-11-06用户授权提交并正常推送到原有origin/main；发布前远端分支/标签查询为空。保留远端配置，不强推，不纳入.tools/out等产物。
- 已实现：Win32/D3D12窗口、清屏/三角形、固定DXC、HLSL人工F5重编译、事务PSO替换、有限帧CLI、纯逻辑headless、诊断与安全退出。
- 未实现：网格/相机、场景、参数资源契约、CPU/GPU计时、Editor UI、离屏GPU、Render Graph、物理、后台编译。

## 2. 紧邻队列

| ID | 状态 | 目标 / 范围 | 验收 / 依赖 |
| --- | --- | --- | --- |
| M0 | Done | 工程与知识基座 | 历史见DEVLOG |
| M1-001 | Done | Windows/D3D12首帧 | 回归持续通过 |
| M2-001 | Done | 固定DXC + 可重编译HLSL三角形 | 本节与EXP-0001 |
| M2-002 | Ready | 实验参数数据契约与CPU/GPU独立计时 | 下一切片；不同时实现场景/完整Editor |

当前无活动编码任务。任务与交接规则见 [知识管理](KNOWLEDGE.md)。

## 3. M2-001验收

2026-11-06、未提交工作区；VS2022 / MSVC19.44.35228.0 / CMake3.31.10 / SDK26100 / RTX5080驱动577.00。DXC官方NuGet1.8.2505.32，实测b106a961d。固定源、摘要与许可部署见 [环境基线](ENVIRONMENT.md) 和 [ADR-0005](Decisions/ADR-0005-dxc-and-transactional-shader-reload.md)。

| 范围 | Debug | RelWithDebInfo |
| --- | --- | --- |
| 默认无GPU CTest，Editor+Runtime | 60/60 | 60/60 |
| Editor OFF，独立目录 | 31/31 | 31/31 |
| GPU tests ON，独立CPU+GPU CTest | 64/64 | 64/64 |

- Core：68项显式检查；shader.compiler：11项实际DXC检查、不创建GPU/窗口，包括include、缺文件/entry、空/超限源和错误恢复。默认verify无GPU，headless不加载编译器。
- GPU：每种配置与GBV开/关模式各782项检查，包含M1原633项生命周期；三角形语法失败、缺文件、PSO root契约拒绝、修正恢复和generation均实际测试。
- 预期候选root契约拒绝报告ID690，只在已同步隔离范围消费并输出为rejected_candidate；不能拿这类已解释错误声称全部诊断从未出现。正常GPU工作/释放的意外warning/error/live child均为0。
- 两应用严格CLI测试：默认triangle和显式clear均准确呈现8帧、GBV、安全退出；首次缺shader返回1并诊断。优化不开Debug Layer另实际成功呈现8帧，未开启时零统计不代表执行了验证。
- 真实桌面中文临时shader路径F5实验：初始generation1，中心RGB146/93/92；语法错误后generation/像素不变；修正PS改色后generation2、中心229/25/153。Escape正常退出，GBV正常工作与live child检查为0。详见 [EXP-0001](../experiments/EXP-0001-triangle/README.md)。这是自动窗口消息/客户区捕获，不伪称所有者手工操作。
- 编译/W4/WX、clang-format19.1.5、IDE诊断和文档/文本规范纳入最后交付检查；所有初期失败测试已修正并重新执行，没有放宽门槛。

### 体验与复现

仓库根目录，固定DXC已经安装，不重复运行安装脚本：

```powershell
powershell -NoProfile -File tools/dev.ps1 verify -Configuration Debug
# 默认程序旁部署副本，跨工作目录可用。
.\out\build\windows-vs2022\bin\Debug\tinyme_editor.exe --windowed
# 开发时明确指向仓库源，修改后F5；失败留旧画面，修正后F5；Escape退出。
.\out\build\windows-vs2022\bin\Debug\tinyme_editor.exe --windowed --shader shaders/Triangle.hlsl
# M1回归与无GPU逻辑路径仍存在。
.\out\build\windows-vs2022\bin\Debug\tinyme_runtime.exe --windowed --experiment clear --frames 8
.\out\build\windows-vs2022\bin\Debug\tinyme_runtime.exe --headless --frames 3
powershell -NoProfile -File tools/dev.ps1 gpu-test -Configuration Debug
```

verify与gpu-test均已用RelWithDebInfo重复；gpu-test需要交互桌面/RTX5080/Graphics Tools。独立GPU CTest：

```powershell
$cmake = '.\.tools\python\cmake\data\bin\cmake.exe'
$ctest = '.\.tools\python\cmake\data\bin\ctest.exe'
& $cmake --preset windows-vs2022 -B out/build/gpu-validation -DTINYME_ENABLE_GPU_TESTS=ON
& $cmake --build out/build/gpu-validation --config Debug --parallel
& $ctest --test-dir out/build/gpu-validation -C Debug --output-on-failure --no-tests=error
```

Runtime-only使用独立`out/build/runtime-only`，配置`-DTINYME_BUILD_EDITOR=OFF -DTINYME_ENABLE_GPU_TESTS=OFF`。独立两目录也均已执行Debug/RelWithDebInfo。手工执行确认每条退出码再继续，dev.ps1会自动传播失败。

最终六组合复核日志：`out/m2-verify-<Configuration>.log`、`out/m2-final-runtime-only-<Configuration>.log`、`out/m2-final-gpu-validation-<Configuration>.log`。GPU/桌面过程日志：`out/m2-gpu-<Configuration>.log`、`out/m2-interactive-reload.log`。截图在out，临时shader/test fixture已清理；产物不进Git，长期摘要是本节/实验记录。

## 4. 下一切片：M2-002

目标：让实验输入与测量有明确契约，不继续硬编码在App/Editor，建立可理解的参数更新与计时。

- 定义最小CPU/HLSL参数布局（对齐、单位、更新时刻），从CLI/Runtime共享传入；不做通用反射/完整材质系统。
- 建立frame-safe参数buffer或root constant，验证修改与重载不覆盖in-flight数据。
- CPU计时与GPU timestamp分别记录；GPU结果异步、延迟读取，不在正常帧同步readback，不将VSYNC/编译卡顿误报为算法耗时。
- 用EXP记录参数/测量范围/预热与样本，保留headless/clear和shader恢复回归。随后再拆网格/相机切片。

验收：参数输入可复现，两入口复用，布局有检查、GPU验证通过；测量明确来源和验证层开关，不承诺本轮不存在的性能数据。

## 5. 已知限制

- 同步F5编译/候选创建允许GPU idle，可能短暂停顿；无文件监听、后台编译或shader缓存。主源两阶段共享快照，但include各阶段即时读取，尚无完整依赖图原子快照。
- 只有空root签名/固定VS与PS三角形契约；资源绑定不匹配的PSO可恢复分类需要Debug Layer正向诊断证据。不开验证层或无证据的未知API失败会抛出并faulted，不冒充普通编译失败。
- Debug CPU配置下shader仍O3，无shader调试符号/PIX；首次DXC安装本轮使用官方包直接下载解压，配套安装脚本完成审查但未在第二个空目录重新安装。现有文件与CMake摘要校验均通过。
- shader默认读输出副本，每次构建单asset target同步；人工开发建议--shader指向仓库源，避免改错副本。
- 尚未人为触发TDR/设备移除/超时/OOM；故障路径经审查，不以正常GPU通过替代故障实测。跨屏DPI等M1限制继续存在，见ADR-0004。
- 单窗口/主线程、SDR/VSYNC，无自动设备恢复、无集显/WARP降级。没有GPU性能基准，也未验证PIX。
- 既往一次优化CLI捕获缺尾部摘要根因未确认；已经显式flush，最终严格测试通过，若复现仍需保留原日志而非放宽断言。
- 项目未指定自身LICENSE、未接入托管CI；DXC输出license目录必须保留用于分发，不能以第三方许可代替引擎授权。
