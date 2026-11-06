# 重要节点记录

倒序追加，只记值得长期保留的节点。当前任务状态见 [STATE](STATE.md)，决定原文见 [ADR 索引](Decisions/README.md)。不以本文件复制全部代码改动或会话记录。

## 2026-11-06 — 整个M3验收：幼年期工程出口完成

- M3-001/002/003全部实现验收，不在RTT完成后停止。0.6.0基于已发布M2 1fad4a6；本轮已完成两种pass组合、copy/grayscale及实际post shader反色替换/恢复、Editor/Runtime共享调参、真正无窗口GPU、PNG/JSON复现与两套实验。
- [ADR-0008](Decisions/ADR-0008-explicit-passes-and-offscreen-gpu.md)落实每slot scene/output、显式barrier与capture fence；[ADR-0009](Decisions/ADR-0009-m3-architecture-review.md)完成保留/调整复盘。仍无通用RHI/Graph/场景/资产/UI，不复制renderer，normalRender无pixel readback。
- 首次实际RT清屏ID820用optimized clear与clear变更冷资源边界修复而非隐藏；post shader-linkage候选分类补齐；旧profile用户错误文案恢复，测试门槛不削弱。M2桌面RGB采集瞬态失败保留日志，容差2未改，最终GPU/GBV全部通过。
- 双配置默认128/128、Editor OFF67/67、GPU opt-in138/138；M2+post中断GPU1740、M3全图460718、CLI752、PNG独立WIC45、报告91、相机151、DXC14检查。大量460718是逐像素断言，不冒称这么多独立功能。copy fullimage max0、grayscale max1；32帧尾样本与capture不增frame均验证。
- 六组两入口direct/copy/grayscale优化120样本实采完整，同输入direct/copy PNG全字节相同，两灰度也相同；数据在EXP-0002，不用无VSYNC/小数值证明优化优势。headless逻辑、窗口present、offscreen提交frame三语义严格区分。
- 默认verify/test与CTest presets显式排除GPU，实际缓存ON验证仍只128无GPU项后恢复OFF。正常工作和安全释放意外warning/error/live child为0；根签名ID690与linkageID666为预期候选拒绝，明确打印。
- GPU新上传/capture OOM/Signal/等待/设备移除未故障注入，PNG与report不是两文件原子事务，已列未测与部分发布限制。最后正常Git提交/推送按所有者新授权进行；本轮止于M3，下一最小深度切片只登记Ready不继续编码。

## 2026-11-06 — M3开工：两种pass组合与无窗口GPU

- M2基准1fad4a6已正常提交并推送到既有origin/main。整个M3按三切片推进，本轮不得在M3-001结束时停止；STATE是唯一当前任务源。
- [ADR-0008](Decisions/ADR-0008-explicit-passes-and-offscreen-gpu.md)确定direct对照、scene颜色纹理+copy/grayscale、GPU-only target分支与显式最后帧PNGcapture；不复制renderer或预建Graph/RHI/UI。资源与scene/post时间范围分别解释，offscreen不冒称present。
- 默认verify/test明确排除GPU标签，修复缓存曾设GPU tests ON时“默认无GPU”不可靠的契约漏洞；GPU批处理也须显式验收。
- 当前正在实现，尚未验证或完成整个M3。新建实验/架构复盘须包含真正同输入对照与资源/计时证据，不以计划替代结果。

## 2026-11-06 — 新授权与M2基准提交

- 所有者允许agent随时向既有GitHub远端正常提交/推送已验证成果，并明确本轮完成整个M3再停下；授权写入协作规范/Agent入口，不改写历史或提交工具数据。
- 保存已通过六组合与GPU/GBV的M2完整链路0.5.0作为M3开发前基准；M3尚未实现，不在该基准声明完成。后续出口包含两种pass组合、真实copy→grayscale算法替换、两入口共享调参、独立无窗口GPU复现、两套实验与架构复盘ADR。

## 2026-11-06 — M2-003验收：索引几何与透视相机，M2完成

- 0.5.0未提交工作区，基于834c297；没有自动提交或推送。两入口共享固定索引三角形、POSITION/COLOR24字节VB、R16 IB、左手透视相机、CLI eye/target/FOV与JSON复现数据。模型identity、无通用网格加载或深度附件；边界见ADR-0007。
- 首次实际GPU验收产生buffer初态ID1328警告：当前驱动忽略CreateCommittedResource的COPY_DEST。改以COMMON创建，上传前后均显式barrier，不屏蔽warning；再跑六组合全部通过。staging由Renderer持有到upload fence完成，F5/resize复用VB/IB，记录最后draw fence，Shutdown在live child检查前释放。
- 相机row-major行向量View*Projection/b1十六DWORD与旧b0八DWORD每次完整录制复制；CPU双精度内部数学并验证有限/退化输入。审查补近共线up阈值两侧回归，没有转置或共享mapped CB覆盖。
- 默认RGB146/93/92保持；非对称平移、斜视方向、FOV/距离、near/far裁剪、resize比例、24帧连续交替相机和F5保持均实际GPU验证。上传/非法相机不计呈现，46相机样本尾帧唯一；旧18样本与256队列/4 dropped回归保留。输入NORMAL不匹配ID65和未绑定b2 ID690候选拒绝均隔离明示，不把它们冒称正常GPU错误为零。
- Debug/RelWithDebInfo默认CTest各102/102，Editor OFF各53/53，GPU opt-in各108/108；相机151检查、报告75检查、GPU每配置GBV开/关各1724检查，正常工作/释放意外warning/error/live child均0。具体命令与日志只在STATE，画面/优化实采数值在EXP-0001。
- 两入口优化关闭验证层顺序实采30预热/120样本，complete=true；CPU/GPU独立，不据旧/new微小数值宣布优化收益。M2工程出口已满足；M3/幼年期整体尚未完成，下一切片为离屏颜色目标和可替换全屏后处理，不预建Render Graph。

## 2026-11-06 — M2-003开工：索引资源与相机契约

- 按用户继续开发请求开始下一纵向切片，不提交/推送，不扩张场景、资产导入或Editor UI。关键资源与数学取舍见 [ADR-0007](Decisions/ADR-0007-indexed-triangle-and-camera.md)。
- 静态DEFAULT VB/IB仅首次有效候选时上传；staging由Renderer持有直到上传fence完成，正常帧不增WaitIdle。矩阵为独立b1十六DWORD，b0原八DWORD不变，避免共享CB覆盖in-flight数据。
- 明确左手row-major行向量View*Projection、D3D深度[0,1]，CPU/HLSL不做隐式转置。默认16:9保持旧画面；resize按实际aspect保持几何比例，scale仍是投影后clip.xy缩放。
- 当前尚在实现，未运行本切片完整验收，不以该开工记录宣称已支持。最终结果、验证与遗留项在STATE维护。

## 2026-11-06 — M2-002审查收尾：报告I/O失败与不覆盖

- 最后审查指出close失败发生在既有删除判断之后，可能残留自身报告且cleanup error误为0；补齐按仍可能有效的独占句柄标删除、限次重试关闭、原始错误与清理错误分别诊断。不按路径删除，保留CREATE_NEW与并发赢家安全。
- 仅在Runtime内部报告头增加单次调用的四个Win32函数注入点，不增加全局生产开关或通用文件系统。真实文件句柄注入Write/实际短写/Flush/Close/删除失败/连续Close失败；可恢复路径自动无残留且可重试，清理失败明确诊断并由测试释放残留fixture。八轮双线程竞争只有一个成功且赢家原字节不变。
- 报告检查16→70，两配置均直接执行通过；默认CTest各89/89、Editor OFF各46/46、GPU opt-in各95/95重新通过。日志与复现命令在STATE；clang-format19.1.5、/W4/WX、IDE诊断与diff检查纳入收尾。
- ADR-0006明确整段会话的保守完整性政策：采样完成后、会话退出前的环境变化或用户关闭仍判不完整。正式路径独占写入不是原子发布；故障注入不冒充实际磁盘满、断电或崩溃实测。
- 没有修改GPU计时路径、提交、推送或清理未识别文件；M2-003仍为下一切片。

## 2026-11-06 — M2-002：共享实验参数与独立计时

- 版本0.4.0，基于已发布834c297的未提交工作区；本轮未自动提交/推送。Core建立32字节/16对齐的tint/scale契约，HLSL b0/packoffset对应，每帧八DWORD root constants复制，不用共享upload覆盖in-flight数据。取舍见 [ADR-0006](Decisions/ADR-0006-experiment-parameters-and-timing.md)。
- 两应用共享严格CLI参数与profile预算。CPU只测命令录制，GPU只测clear/可选draw；两个slot四queries/32字节readback，完成fence后非阻塞领取，退出安全点收尾。队列最多256条，丢样本显式记录。
- JSON包含真实adapter/driver/frequency、参数、构建/验证开关、预热/样本、原始时间、mean/median/nearest-rank P95及完整性原因；clear不虚称加载DXC/应用参数。新报告不覆盖已有文件，写失败尝试按句柄清理自身半成品。
- 真实中断回归发现一次PumpMessages可处理最小化又恢复，最终状态比较会漏报变化；新增Window surface revision保留瞬时尺寸/最小化/DPI变化，关闭前先检查。F5/窗口变化/遮挡/提前关闭/丢样本/验证warning均使报告不完整，而非性能成功。
- 优化STL路径大小暴露过对齐参数的C4324：将WindowedRunOptions参数放到结构开头，不禁用/WX。此前LNK1168实际为已打开Editor占用，正常关闭后重建。flip-model窗口GDI DC可给旧重定向像素，测试改采真实桌面坐标并在捕获边界固定等待/DwmFlush，未放宽颜色断言；桌面捕获不计入性能样本。
- 完整双配置/Editor OFF/GPU opt-in验收及命令证据见STATE；Core165检查，报告16检查，无GPU DXC11检查，GPU每模式1512检查。参数像素原146/93/92→红146/0/0，缩放外露clear6/14/23；重载后参数保留、尾帧与有界队列、四类窗口中断均通过。
- 优化、验证层关闭下实采两入口30帧预热/120样本，明确不是FPS、吞吐或优化收益；数值与限制见 [EXP-0001](../experiments/EXP-0001-triangle/README.md)。下一切片是最小网格/相机，不扩张Editor UI/Render Graph。

## 2026-11-06 — 0.3.0首次工程基线发布

用户授权将M0/M1/M2-001当前内容提交并正常推送到已有origin/main。发布前远端分支/标签查询为空；源码、规范、ADR和实验记录纳入基线，.tools/out/生成工程及截图不纳入Git。构建与GPU验证沿用本日M2-001完整验收结果，本次只更新发布交接文本，不改变实现。实际提交ID/推送结果以Git记录为准，不强制覆盖、不跳过hooks、不更改Git配置。

## 2026-11-06 — M2-001：第一条 HLSL 三角形实验链

- 版本0.3.0；固定官方DXC NuGet1.8.2505.32，显式安装到.tools、CMake离线核验摘要、部署compiler/validator及许可。取舍见 [ADR-0005](Decisions/ADR-0005-dxc-and-transactional-shader-reload.md)。
- 默认窗口实验为triangle，保留clear回归；两入口共享VSMain/PSMain编译与清屏→三角形→呈现。主源有4MiB预算，两阶段同快照，include即时读取限制明确记录。
- 人工F5重编译：编译失败保留旧PSO；已隔离且正向确认的root/linkage候选拒绝保留旧版本，未知GPU/API故障仍faulted。最终诊断先于PSO/generation交换，正常帧不加全GPU同步。
- wmain/UTF-8参数/C++20路径解决中文shader路径；构建单shader资产target避免只改HLSL不重链接时部署副本过期。
- Debug/优化默认CTest各60，runtime-only各31，opt-in各64通过；Core68检查、无GPU DXC11检查、每模式GPU782检查。M1生命周期仍独立验证，三角形编译错误、PSO拒绝和恢复均实测。
- 真实桌面中文临时shader路径F5验证：原画面中心146/93/92，语法错误后不变，修正后229/25/153；generation为1→1→2，GBV零意外诊断、Escape正常退出。实验与复现记录见 [EXP-0001](../experiments/EXP-0001-triangle/README.md)。
- 不声称完成整个M2：暂无网格/相机、参数资源契约、GPU/CPU计时、Editor UI、Render Graph或异步编译。下一切片是参数契约与可观测性，实际队列见STATE。

## 2026-11-06 — M1：Win32 与 D3D12 首帧

- 新增实际 Windows 窗口、具体 D3D12 Renderer 和独立窗口 Runtime；Editor / Runtime 共享同一实现，Core/纯逻辑测试仍不依赖 GPU。版本 0.2.0。
- 单 RTX 5080、双 flip-discard RGBA8 buffer、逐 buffer allocator/fence、显式状态 barrier；最小化/遮挡等待消息，resize/关闭才等待完整 GPU 安全点。`WM_CLOSE` / Escape 延后 HWND 销毁。
- `--windowed` 默认交互运行，可用 `--frames` 精确限成功呈现帧；`--headless` 仍无 GPU。Debug 默认验证，GBV 显式开启；使用 console 子系统保留 CLI 捕获。
- 首次 D3D12 能力查询确认 feature level 12.2、查询范围 shader model 6.8、RT1.1 / VRS2 / mesh tier1；只是硬件能力，不表示对应 feature 已实现。完整环境事实见 ENVIRONMENT。
- 关键取舍见 [ADR-0004](Decisions/ADR-0004-window-and-gpu-lifetime.md)：所有权、延后关闭、故障状态与安全拆卸策略。

### 验收与边界

最终 Debug/RelWithDebInfo 默认 CTest 各47/47，Runtime-only 各24/24，opt-in CPU+GPU CTest 各51/51。每种配置下 Debug Layer/GBV 两模式真实生命周期各633项检查，包含3轮创建销毁、24次真实resize、24次最小化恢复，诊断及live child为0。两应用精确8帧退出；另用真实桌面窗口消息验证WM_CLOSE/Escape，截图中心RGB 6,14,23符合清屏色，PMv2 DPI实测144。

审查修正 fence event 不能代表目标 fence 已完成、等待失败不能把 stopped 当作拆卸完成、破坏性resize/提交失败必须faulted等异常路径；设备移除允许报错拆卸，存活设备不可确认GPU完成则拒绝不安全释放并fail-fast。未人为触发TDR/超时/OOM，正常路径通过不能替代故障实测。

严格CMake子进程测试发现首次ShowWindow可能遵循父进程STARTUPINFO导致最小化/零尺寸；窗口显式恢复并读取实际客户区后再初始化GPU。另一次优化CLI捕获缺少末尾摘要，独立重复未复现，现已显式刷新摘要；最终严格套件通过，未将未确认根因写成确定修复。

当前没有shader绘制、场景、Editor UI或Render Graph；下一步M2-001为固定DXC与首个可重编译HLSL三角形实验。验证命令、日志位置与未测范围只在STATE维护。本轮无Git提交、推送或配置更改。

## 2026-11-05 — M0：工程与知识基座

### 背景与结果

仓库原先只有初始化的 Git 元数据，没有代码和构建体系。建立 Windows C++ 渲染研究项目的最小工程基座，而不是立即堆完整引擎。明确幼年期/发展期/铺展期的目标、退出条件和暂不做的内容。

- 形成 README 导航、章程、架构、环境、协作规范、任务交接、决策记录与实验模板；每类知识仅一个权威来源。
- 实测开发机：9950X、约 96 GB 内存、RTX 5080 约 16 GB 显存、Windows 11；完整数据见 `ENVIRONMENT.md`。
- 采用 Windows x64 / C++20 / VS2022 v143 / D3D12 单后端路线，见 [ADR-0001](Decisions/ADR-0001-platform-and-toolchain.md)。D3D12 是后续实现路线，本轮没有 GPU 初始化。
- Editor/Runtime 是两个独立 CLI 入口，共享 app host 和纯逻辑 Session；无窗口逻辑模式明确区别于未来离屏 GPU 模式，见 [ADR-0002](Decisions/ADR-0002-boundaries-and-render-evolution.md)。
- 用仓库知识与交接替代会话记忆依赖，见 [ADR-0003](Decisions/ADR-0003-knowledge-and-handoff.md)。
- 首次没有可用 CMake，安装固定 3.31.10 到忽略的 `.tools/python/`，不修改系统环境。构建过程不联网。

### 验证摘要

最终版本经实际验证：VS2022 / MSVC 19.44.35228.0 / SDK 10.0.26100.0；默认 Debug 与 RelWithDebInfo 各 31/31 个 CTest 通过；关闭 Editor 的独立目录两种配置各 16/16 通过。核心测试含 41 个显式检查，优化构建同样执行。CLI 验证精确退出码、stdout/stderr、帮助/版本、有效执行及非法/重复/越界参数；clang-format 19.1.5 检查通过。

### 审查修正与经验

发现 CMake CMP0149 的 NEW 行为不再用 `CMAKE_SYSTEM_VERSION` 固定 VS 的 Windows SDK；如果只设置这个变量，新装 SDK 后会发生选择漂移。已改用 preset 的 `x64,version=10.0.26100.0`，配置时核对 `CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION`，并用 fresh cache 重验全部四种构建组合。依据为 CMake 3.31.10 内置 `--help-policy CMP0149` 与 `--help-variable CMAKE_GENERATOR_PLATFORM`。

### 边界与后续

当前仅完成工程基座，没有窗口、GPU 渲染、Editor UI、Render Graph、场景或物理；不将逻辑 tick 称为已渲染帧。下一步 M1-001 是 Win32 + D3D12 可诊断首帧及 resize/退出生命周期，具体验收见 STATE。未创建 Git 提交、未推送、未更改原有的 1 个远端配置；远端内容与连通性未验证，首次本地提交由所有者决定。
