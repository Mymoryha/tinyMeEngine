# 当前状态与接手板

更新时间：2026-11-06。唯一当前进度源；重要历史见 [DEVLOG](DEVLOG.md)。

## 1. 当前定位

- 幼年期M0–M3工程出口全部验收：至少两种pass组合、真实算法替换、Editor/Runtime共享调参、独立无窗口GPU复现、两套实验与架构保留/调整ADR齐备。成长期未开工，仍不是完整通用引擎。
- 版本0.6.0，M2基准1fad4a6与整个M3实现ecec75d已正常提交推送至既有origin/main；Git确认HEAD与origin/main无差异。此后仅本次发布交接文档更新，最后文档提交以Git log为准；.codebuddy工具数据保留但不提交。
- 2026-11-06所有者授权随时向既有GitHub origin正常提交/推送已验证成果，并要求完成整个M3再停下；没有改写历史、变更远端或选择许可证。
- 已实现：M2窗口/索引相机/共享参数/DXC/F5链、direct/RTT-copy/grayscale、两入口共享scene/post独立计时、独立offscreen有限GPU与最后帧PNG、含两generation和frame_number的JSON、pure headless及safe shutdown。
- 未实现：通用网格/资产加载、模型变换/深度附件、场景、Editor UI、Render Graph、物理、后台编译或通用材质/Profiler。PNG捕获不是持续正常帧readback，无跨平台/多GPU承诺。

## 2. 紧邻队列

| ID | 状态 | 目标 / 范围 | 验收 / 依赖 |
| --- | --- | --- | --- |
| M0 | Done | 工程与知识基座 | 历史见DEVLOG |
| M1-001 | Done | Windows/D3D12首帧 | 回归持续通过 |
| M2-001 | Done | 固定DXC + 可重编译HLSL三角形 | 本节与EXP-0001 |
| M2-002 | Done | 实验参数数据契约与CPU/GPU独立计时 | 本节第4部分、ADR-0006与EXP-0001 |
| M2-003 | Done | 最小索引网格与相机变换 | 第5部分、ADR-0007与EXP-0001；六组合验收通过 |
| M3-001 | Done | direct对照、颜色纹理与copy/grayscale全屏pass | 第6部分、ADR-0008、同输入图像与实际post替换 |
| M3-002 | Done | 独立无窗口GPU与PNG捕获 | 两入口无HWND/swapchain/Present；有限帧与PNG/JSON复现 |
| M3-003 | Done | 架构复盘与完整阶段验收 | 两套实验、ADR-0009、六组合GPU/GBV均通过；ecec75d已推送 |
| G1-001 | Ready | 成长期首切片：最小深度附件与遮挡对照 | 仅固定几何深度正确性/生命周期；开工细化，尚未实现 |

当前无活动编码任务；整个M3编码、验收与Git正常发布完成，按要求停止本轮，不启动成长期。无场景、资产、Graph、UI或新框架。任务与交接规则见 [知识管理](KNOWLEDGE.md)。

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

## 6. 整个M3验收与复现

2026-11-06，0.6.0，基于已推送1fad4a6/M2；Windows x64/VS2022 v143、固定DXC1.8.2505.32、RTX5080驱动577.00，依赖/平台未升级。整个M3由M3-001/002/003完成，非只完成RTT。契约见 [ADR-0008](Decisions/ADR-0008-explicit-passes-and-offscreen-gpu.md)，保留/调整复盘见 [ADR-0009](Decisions/ADR-0009-m3-architecture-review.md)，两套实验为 [EXP-0001](../experiments/EXP-0001-triangle/README.md) / [EXP-0002](../experiments/EXP-0002-postprocess/README.md)。

| 范围 | Debug | RelWithDebInfo |
| --- | --- | --- |
| 默认无GPU CTest，Editor+Runtime | 128/128 | 128/128 |
| Editor OFF独立目录 | 67/67 | 67/67 |
| GPU opt-in独立目录，CPU+10显式GPU项 | 138/138 | 138/138 |

- Core原165与Camera151检查保留；新三模式/CLI/pipeline752检查、PNG独立WIC解码45检查、报告91检查、DXC14检查。报告保留六类I/O故障注入与并发不覆盖，并新增offscreen无present、独立post统计、capture状态、窗口/离屏前置拒绝和Win32尾随点/空格/ADS。
- M2/M3窗口GPU每配置GBV开/关各1740检查：原窗口/相机/静态上传/参数/F5/计时/18样本及256队列/4dropped保留，加post启用下F5/最小化/resize/关闭四类会话中断。
- 专项M3 GPU每配置GBV开/关各460718检查，其中大部分是全图每像素灰度一致性；全图copy max_error=0，灰度独立量化参考max_error=1。640×360与641×359、非对称相机/scale/tint、RGBA alpha/top-down、17×13、32提交帧尾样本、捕获不增frame/present、场景/post独立generation、模式切换、resize、冷clear重建、网格上传fence保持、安全释放全部通过。
- actual post shader反色替换再恢复，图像改变/恢复，scene generation不变；缺文件/语法/未绑定t1(ID690)/VS-PS TEXCOORD链接(ID666)只拒绝候选并明确打印，旧PSO继续。正常GPU/释放意外warning/error/live child均0，不把预期候选错误冒称不存在。
- 两入口真实CLI跑windowed copy/grayscale、offscreen同模式+641×359+2预热/3样本PNG、clear单帧尾样本/无scene元数据、defaultoffscreen1帧、shader失败无PNG、既存/缺父目录/路径别名GPU前拒绝。headless仍纯逻辑。程序从非仓库工作目录读取已部署两个shader。
- 同输入两入口共六组优化无验证层实采，30预热/120样本，timestamp频率1000000000Hz、complete=true、frame_number31..150、presentation0、VSYNCoff。Runtime/Editor的direct/copy PNG完整字节相同，两灰度PNG也相同；图像/独立CPU/scene/post数值长期记录在EXP-0002，不据微小差异宣布性能收益。
- 默认verify/test与CTest preset显式排除GPU标签；已实际将默认目录缓存GPU tests ON后执行verify，仅128无GPU项，preset列举128，随后恢复OFF，日志`out/m3-stale-gpu-cache-no-gpu.log`。
- 实际初次问题：RT纹理无optimized clear ID820，已提供真实值并支持clear变更冷边界重建，不屏蔽warning；post linkage拒绝分类补齐；两应用旧profile错误文案new output path恢复，不修改旧断言。一次M2桌面RGB采集瞬态失败保留证据，原容差2重跑与最终两配置GPU全通过，未扩大容差。
- /W4/WX、clang-format19.1.5、IDE诊断、严格UTF-8/LF、相对文档链接及Git diff检查纳入最后发布。GPUTest只在显式命令执行，capture是显式copy/wait，不混入正常Render/样本。

根目录复现，输出为新文件、父目录存在：

```powershell
powershell -NoProfile -File tools/dev.ps1 verify -Configuration Debug
powershell -NoProfile -File tools/dev.ps1 verify -Configuration RelWithDebInfo
powershell -NoProfile -File tools/dev.ps1 gpu-test -Configuration Debug
powershell -NoProfile -File tools/dev.ps1 gpu-test -Configuration RelWithDebInfo
.\out\build\windows-vs2022\bin\RelWithDebInfo\tinyme_runtime.exe --offscreen --postprocess grayscale --width 641 --height 359 --camera-eye 0.25 0.125 -3 --camera-target 0.25 0.125 0 --fov 75 --scale 0.75 --tint 1 0.8 0.6 0.5 --profile --warmup 30 --samples 120 --capture out/m3-new-gray.png --report out/m3-new-gray.json
.\out\build\windows-vs2022\bin\Debug\tinyme_editor.exe --windowed --postprocess grayscale --post-shader shaders/PostProcess.hlsl
```

copy/none对照仅换postprocess，另用新输出名；Editor和Runtime参数完全共用。两GPU模式尺寸1..2048，offscreen默认1帧、窗口未设frames/profile才无限；headless/ticks、windowed/presentations、offscreen/frame_number语义分开。F5重载启用的scene/post各自候选；任何请求使保守会话采样不完整，不承诺多shader事务。clear+post有效且scene compiler/geometry/camera为null。

最终六组合日志：`out/m3-final-verify-<Configuration>.log`、`out/m3-final-runtime-only-<Configuration>.log`、`out/m3-final-gpu-validation-fixed-<Configuration>.log`；首次失配日志`out/m3-final-gpu-validation-Debug.log`保留。开发gpu-test完整入口另存`out/m3-gpu-entry-<Configuration>.log`。实采`out/m3-measure-{runtime,editor}-{none,copy,grayscale}.{json,png,log}`，产物忽略，不作为知识唯一源。

## 7. 下一阶段与止点

整个M3结束后按所有者要求停止本轮编码。下一Ready切片G1-001仅建议最小深度附件/遮挡对照：在固定几何中验证深度状态、尺寸资源、安全释放、同输入direct/RTT/offscreen回归；开工时再细化，不先搭Scene/ECS/资产/UI/Graph。成长期框架化或扩大平台仍属需所有者决策的范围。

## 8. 已知限制与未测项

- scene root签名b0八DWORD参数+b1十六DWORD矩阵；post独立PS t0 table+PS b0单DWORD。仅固定三角形与明确copy/grayscale契约，无通用材质/反射/UI、资产加载、深度buffer或模型变换。tint alpha写输出但当前不透明PSO不启用合成。
- 上传/capture的Signal/WaitForFence失败沿已有faulted/设备移除/Shutdown边界处理，staging/readback由Renderer持有，不提交后局部提前释放；新GPU路径未实际注入OOM/Signal/等待超时，正常生命周期测试不替代故障实测。
- 同步F5与resize/模式组合/clear值变更冷边界允许GPU idle；无后台重载/监听/缓存、完整include/多shader原子事务。Debug CPU下HLSL仍O3，无shader调试符号/PIX。
- CPU录制、GPU scene clear/draw、GPU post fullscreen draw独立；都不含整帧/fence/Present/capture。窗口VSYNCon、offscreen无Present/VSYNCoff。小三角形调度/时钟影响明显，没有性能回归阈值或引擎整体benchmark。
- PNG仅最后成功offscreen帧显式capture边界；WIC内存编码和CREATE_NEW不覆盖，不承诺PNG/JSON两文件事务。PNG写成功而JSON后续失败可保留完整PNG，诊断失败不声称完整发布；不保证断电/进程崩溃无半成品。UNORM域灰度不是sRGB/linear色彩管理。
- JSON记录版本/路径而非完整shader依赖摘要；复现须保留对应Git基准、工作区shader和输入，不能仅靠路径独立还原代码。已有输出不覆盖，父目录不自动创建；失败按自身独占句柄尝试清理并明确清理错误，关闭失败限次重试。六类可检测I/O失败已有真实句柄故障注入，但未实际填满磁盘、触发底层flush/close故障、进程崩溃或断电；直接写正式路径不是原子发布，不保证上述崩溃场景无半成品。
- 尚未人为触发TDR/设备移除/超时/OOM/timestamp倒序；正常GPU通过不替代故障实测。跨屏DPI与纯DXGI遮挡路径未单独实测，最小化/resize/F5/关闭已测；其他M1限制见ADR-0004。
- 单窗口/主线程、SDR/VSYNC，无自动设备恢复、无集显/WARP降级。PIX未验证；固定DXC安装脚本尚未在第二空目录重装，现有包/部署摘要核验通过。
- 既往一次优化CLI捕获缺尾部摘要根因未确认；最终严格回归均通过，复现时保留证据，不放宽断言。
- 项目尚未指定自身LICENSE或托管CI；DXC分发许可须保留，不能替代引擎授权。
