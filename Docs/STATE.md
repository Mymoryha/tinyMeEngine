# 当前状态与接手板

更新时间：2026-11-06。唯一当前进度源；重要历史见 [DEVLOG](DEVLOG.md)。

## 1. 当前定位

- 幼年期：M0、M1与M2已完成工程验收；M2-001/002/003组成首条可复现索引几何/相机/shader/参数/计时链。M3及整个幼年期尚未完成。
- 版本0.5.0，基于834c297（已发布0.3.0）的已验证未提交工作区；本轮没有提交或推送。实时差异以Git为准，.codebuddy工具数据保留，不当作本轮源码交付物。
- 2026-11-06所有者新授权随时向既有GitHub origin正常提交/推送已验证成果，并要求完成整个M3再停下；不复用旧授权、不改写历史、不选择新许可证。当前先保存已验收M2基准，随后完成M3两种pass组合/算法替换/无窗口GPU复现与架构复盘。
- 已实现：Win32/D3D12窗口、clear/索引triangle、静态DEFAULT VB/IB与安全上传、左手透视相机/resize宽高比、固定DXC、F5事务重编译、共享相机/tint/scale输入、独立CPU/GPU计时与含相机/几何的JSON、纯逻辑headless、诊断与安全退出。
- 未实现：通用网格/资产加载、模型变换/深度附件、场景、Editor UI、离屏GPU、Render Graph、物理、后台编译或通用材质/Profiler。

## 2. 紧邻队列

| ID | 状态 | 目标 / 范围 | 验收 / 依赖 |
| --- | --- | --- | --- |
| M0 | Done | 工程与知识基座 | 历史见DEVLOG |
| M1-001 | Done | Windows/D3D12首帧 | 回归持续通过 |
| M2-001 | Done | 固定DXC + 可重编译HLSL三角形 | 本节与EXP-0001 |
| M2-002 | Done | 实验参数数据契约与CPU/GPU独立计时 | 本节第4部分、ADR-0006与EXP-0001 |
| M2-003 | Done | 最小索引网格与相机变换 | 第5部分、ADR-0007与EXP-0001；六组合验收通过 |
| M3-001 | Ready | 离屏颜色目标与可替换全屏后处理 | 两种pass组合、明确资源barrier/计时、同输入算法替换与相机/F5回归；开工时细化 |

当前无活动编码任务。M2-003已完成第5部分验收；没有提交/推送，没有开始M3编码。下一切片M3-001在第6部分细化范围；不是立即建立Render Graph/场景/资产平台。任务与交接规则见 [知识管理](KNOWLEDGE.md)。

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

## 4. M2-002验收与复现

2026-11-06，834c297上的0.4.0未提交工作区；环境沿用第3部分与ENVIRONMENT，未升级工具/驱动/DXC。契约见 [ADR-0006](Decisions/ADR-0006-experiment-parameters-and-timing.md)，像素和实际采样值见 [EXP-0001](../experiments/EXP-0001-triangle/README.md)。

| 范围 | Debug | RelWithDebInfo |
| --- | --- | --- |
| 默认无GPU CTest，Editor+Runtime | 89/89 | 89/89 |
| Editor OFF，独立目录 | 46/46 | 46/46 |
| GPU tests ON，独立CPU+GPU CTest | 95/95 | 95/95 |

- Core165显式检查：参数边界/预留、严格解析、模式/重复/预算、统计median/nearest-rank P95与非法样本；纯逻辑Session回归。无GPU DXC仍11检查。报告70检查：独立统计、JSON转义/标题文本/UTF-8路径、空统计null、完整性原因、不覆盖和窗口创建前失败；真实句柄注入Write/实际短写/Flush/Close/删除失败/连续Close失败，验证原始与清理错误、限次关闭、可恢复路径自动无残留且同路径可重试；八轮双线程输出竞争验证唯一赢家和原字节不变。清理本身失败的fixture由测试显式收尾，不冒充生产自动清理成功。
- GPU每配置/GBV开关模式各1512检查：M1/M2-001回归、真实tint/scale/重载像素、非法参数无副作用、关闭计时无query资源、18样本slot领取/重载/resize/尾帧、256队列上限与准确4个dropped；四类真实会话关闭/F5/最小化恢复/resize均不误报完整。
- 像素默认146/93/92→红146/0/0，scale0.5后原三角形内部点变clear6/14/23，重载保持参数。桌面采集要求测试窗口未遮挡；只在测试捕获边界固定等待100ms/DwmFlush，不把捕获当GPU fence或正常帧计时。
- 两入口profile CLI实际解析JSON：2预热+3样本正确编号3/4/5；中文新输出路径、同路径拒绝且原字节不变；clear零预热/1样本在shutdown领取尾帧、stdout JSON成功、未加载shader/参数标记准确。
- 正常GPU工作/释放意外warning/error/live child均为0；M2-001预期ID690候选拒绝仍隔离并明示，不冒充从未发生任何诊断。
- 优化、验证层关闭两入口均实采30预热+120样本，complete=true；CPU录制与GPU pass分别统计，timestamp频率实测1000000000Hz。不是FPS、吞吐或算法优化收益。
- 修复真实验收问题：已打开Editor导致LNK1168，正常关闭而非强杀；优化STL路径大小导致C4324，参数放到配置结构开头而非降警告；最小化又恢复在同次消息泵漏报，surface revision保留变化。头文件/依赖、clang-format19.1.5、IDE诊断、UTF-8/LF、文档链接及git diff hygiene纳入交付检查。

根目录复现（输出文件必须尚不存在）：

```powershell
powershell -NoProfile -File tools/dev.ps1 verify -Configuration Debug
powershell -NoProfile -File tools/dev.ps1 verify -Configuration RelWithDebInfo
powershell -NoProfile -File tools/dev.ps1 gpu-test -Configuration Debug
powershell -NoProfile -File tools/dev.ps1 gpu-test -Configuration RelWithDebInfo
.\out\build\windows-vs2022\bin\RelWithDebInfo\tinyme_runtime.exe --windowed --shader shaders/Triangle.hlsl --scale 0.75 --tint 1 0.8 0.6 1 --profile --warmup 30 --samples 120 --report out/triangle-profile-new.json
```

Editor使用同样选项；clear支持profile但拒绝scale/tint；headless不接受任何GPU/参数/计时选项。`--profile`未指定frames默认有限预算；不完整返回1，非法CLI返回2。`--report`省略时stdout先有诊断日志再有JSON，需要纯JSON使用报告文件。

独立配置/CTest命令沿用第3部分：runtime-only关闭Editor/GPU tests；gpu-validation启用GPU tests，两者均重跑Debug与RelWithDebInfo。初次最终日志：`out/m2-002-verify-<Configuration>.log`、`out/m2-002-final-runtime-only-<Configuration>.log`、`out/m2-002-final-gpu-validation-<Configuration>.log`；开发入口GPU日志`out/m2-002-gpu-<Configuration>.log`。报告审查修复后六组合再次通过，最新日志为`out/m2-002-report-review-verify-<Configuration>.log`、`out/m2-002-report-review-runtime-only-<Configuration>.log`、`out/m2-002-report-review-gpu-validation-<Configuration>.log`；默认目录两配置另直接执行`tinyme_profile_tests.exe`，均输出`checks=70 failures=0 no_gpu=1`。两份实测JSON/日志为`out/m2-002-profile-{runtime,editor}-RelWithDebInfo.{json,log}`，数值长期摘要在EXP-0001；产物不纳入Git。

## 5. M2-003验收与复现

2026-11-06，0.5.0未提交工作区，基准834c297；环境/工具/驱动沿用前文，未下载或升级依赖。资源/矩阵契约见 [ADR-0007](Decisions/ADR-0007-indexed-triangle-and-camera.md)，画面与独立实采值见 [EXP-0001](../experiments/EXP-0001-triangle/README.md)。

| 范围 | Debug | RelWithDebInfo |
| --- | --- | --- |
| 默认无GPU CTest，Editor+Runtime | 102/102 | 102/102 |
| Editor OFF，独立目录 | 53/53 | 53/53 |
| GPU tests ON，独立CPU+GPU CTest | 108/108 | 108/108 |

- 相机151显式检查：64字节/16对齐矩阵、左手view/投影与row-major行向量、非对称平移/aspect与90度旋转/up方向、near/far深度、FOV边界、坐标/有限范围、近平行up阈值两侧、非法aspect和矩阵溢出、严格CLI与模式隔离。Core原165/DXC原11检查保持；报告75检查保留六类I/O失败和并发不覆盖，并增加几何/矩阵/clear-null/初始化前非法相机。
- GPU每配置/GBV开关模式各1724检查：原M1/M2回归加默认索引RGB146/93/92、实际非顺序索引/24字节布局、非对称平移与斜视相机、24帧交替root矩阵、FOV/距离、near/far裁剪、resize比例与F5矩阵保持；上传fence完成/staging释放、后续无重复上传、last-use fence、46相机样本尾帧唯一和非法相机无提交。旧18样本及256队列/4 dropped不削减。
- 初次GPU失败为驱动对COPY_DEST创建初态报告ID1328；改COMMON创建并显式COMMON→COPY_DEST→VB/IB，修复后六组合重新通过，无警告屏蔽。候选NORMAL输入缺失ID65和真正未绑定b2 ID690仅在隔离范围明示消费；正常GPU工作/释放意外warning/error/live child均0。
- 两入口profile CLI实际解析非默认eye=(0.25,0.125,-3)、target=(0.25,0.125,0)、FOV75、几何4/3/24、R16、upload/last-use fence、16浮点矩阵；参数/预热/中文路径/不覆盖与clear1帧尾样本均通过。clear camera/geometry为null，headless不触达GPU。
- 优化、Debug Layer/GBV关闭，两入口顺序运行30预热/120样本，complete=true、timestamp频率1000000000Hz；报告包含相机/矩阵。CPU录制不含相机数学/上传，GPU为clear+indexed draw；数值只验证链路，不声明算法优化收益。
- /W4/WX、clang-format19.1.5、IDE诊断、UTF-8/LF、文档链接与diff检查纳入最终交付；所有初次失败已修复重新跑，没有放宽像素或诊断门槛。源码/知识在工作区，未提交/推送。

根目录复现（report必须新路径，父目录已存在）：

```powershell
powershell -NoProfile -File tools/dev.ps1 verify -Configuration Debug
powershell -NoProfile -File tools/dev.ps1 verify -Configuration RelWithDebInfo
powershell -NoProfile -File tools/dev.ps1 gpu-test -Configuration Debug
powershell -NoProfile -File tools/dev.ps1 gpu-test -Configuration RelWithDebInfo
.\out\build\windows-vs2022\bin\RelWithDebInfo\tinyme_runtime.exe --windowed --shader shaders/Triangle.hlsl --camera-eye 0.25 0.125 -3 --camera-target 0.25 0.125 0 --fov 75 --scale 0.75 --tint 1 0.8 0.6 1 --profile --warmup 30 --samples 120 --report out/indexed-camera-profile-new.json
```

Editor选项完全相同；无相机选项为默认eye0/0/-2、target0/0/0、up0/1/0、垂直FOV90、near0.1/far100。坐标有限[-10000,10000]、FOV[10,120]；目标/视线/up退化拒绝。resize更新实际aspect保持几何比例，scale保持投影后clip.xy缩放。自定义shader须适配POSITION/COLOR及b1矩阵；不兼容输入拒绝候选保留旧画面。

独立配置/CTest命令沿用第3部分。最新六组合日志：`out/m2-003-final-verify-<Configuration>.log`、`out/m2-003-final-runtime-only-<Configuration>.log`、`out/m2-003-final-gpu-validation-<Configuration>.log`。开发入口gpu-test也实际复跑两配置并通过，日志`out/m2-003-gpu-entry-<Configuration>.log`；首次GPU修复后的单独执行日志`out/m2-003-gpu-Debug.log`。两份优化实采JSON/日志`out/m2-003-profile-{runtime,editor}-RelWithDebInfo.{json,log}`，长期数值在EXP-0001。产物忽略，不作为知识唯一源。

## 6. 下一切片：M3-001

目标：离屏颜色目标与可替换全屏后处理，首次验证真正不同pass组合和同输入算法替换。

- 保留当前直接三角形路径作为对照；新增渲染到颜色纹理再全屏输出的显式顺序，解释每个资源、barrier和计时范围。
- 以两种简单全屏处理实现实际可替换算法，而非只搭空接口；继续让Editor/Runtime复用Runtime路径，不加入通用RHI、Scene、资产平台或Render Graph。
- 渲染到离屏纹理不等于无窗口GPU；无窗口GPU需独立执行模式与捕获/有限预算契约，不复用纯逻辑headless。具体离屏执行与导出安排在该切片开工时决定。
- 通过同输入画面/数值对照、参数/相机/F5/clear/headless回归与GPU生命周期验收；现在未实现。没有阻塞，不复用先前发布授权。

## 7. 已知限制与未测项

- root签名为b0八DWORD参数+b1十六DWORD矩阵，只支持固定VS/PS索引三角形；无通用材质/反射/参数UI、资产加载、深度buffer或模型变换。tint alpha写输出但当前不透明PSO不启用合成。
- 首次上传Signal/WaitForFence失败由既有faulted/设备移除/Shutdown边界处理，staging不在提交后的局部栈展开中提前释放；未对新上传路径实际注入OOM/Signal/等待超时，因此不能用正常生命周期测试冒充故障覆盖。
- 同步F5允许GPU idle/编译停顿；无后台重载/监听/缓存、完整include原子快照。Debug CPU下shader仍O3，无shader调试符号/PIX。
- GPU计时只含clear+可选draw，CPU只含命令录制及计时命令开销，均非整帧；VSYNC仍开启。三角形耗时很小、验证/驱动/调度影响明显，没有性能回归阈值、受控时钟或引擎整体benchmark。
- JSON记录版本/路径而非完整shader依赖摘要；复现须保留对应Git基准、工作区shader和输入，不能仅靠路径独立还原代码。已有输出不覆盖，父目录不自动创建；失败按自身独占句柄尝试清理并明确清理错误，关闭失败限次重试。六类可检测I/O失败已有真实句柄故障注入，但未实际填满磁盘、触发底层flush/close故障、进程崩溃或断电；直接写正式路径不是原子发布，不保证上述崩溃场景无半成品。
- 尚未人为触发TDR/设备移除/超时/OOM/timestamp倒序；正常GPU通过不替代故障实测。跨屏DPI与纯DXGI遮挡路径未单独实测，最小化/resize/F5/关闭已测；其他M1限制见ADR-0004。
- 单窗口/主线程、SDR/VSYNC，无自动设备恢复、无集显/WARP降级。PIX未验证；固定DXC安装脚本尚未在第二空目录重装，现有包/部署摘要核验通过。
- 既往一次优化CLI捕获缺尾部摘要根因未确认；最终严格回归均通过，复现时保留证据，不放宽断言。
- 项目尚未指定自身LICENSE或托管CI；DXC分发许可须保留，不能替代引擎授权。
