# 架构与契约

## 1. 已实现结构（M3已验收）

依赖箭头从使用者指向被使用者；本节只描述真实代码。

```text
apps/editor ──┐                      ┌── tinyme_core（CLI / 参数 / 统计）
             ├── tinyme_app_host ────┼── tinyme_runtime（纯逻辑 Session）
apps/runtime ┘                      └── tinyme_windowed_runtime
                                           ├── tinyme_core
                                           ├── tinyme_runtime
                                           └── tinyme_d3d12 ──→ tinyme_windows
                                                   └── tinyme_core

Core tests ── tinyme_core + tinyme_runtime
Report tests ── tinyme_windowed_runtime + tinyme_d3d12（不创建 GPU）
GPU tests ── tinyme_d3d12 + tinyme_windows + tinyme_windowed_runtime
```

- 两个应用保留 console 子系统，CLI 的 stdout/stderr 与退出码可被脚本捕获；不是仅能通过 GUI 启动的程序。
- `Session` 仍只拥有逻辑 tick/模拟时间，不依赖窗口、GPU、Editor。`WindowedSession` 是单独的组合实现，不把 GPU 依赖带入 Core 测试 target；报告测试链接窗口组合但只执行纯序列化、文件与窗口创建前失败路径。
- `tinyme_windows` 拥有一个 Win32 HWND；`tinyme_d3d12` 是具体清屏/三角形实现，通过 PImpl 隐藏 COM 细节，不是通用 RHI。
- `RunWindowedSession` 位于 Runtime 层，依次构造 Window → Renderer → Session。显式 Renderer Shutdown 后才允许 Window 析构。
- 无参数显示帮助；headless保持无窗口/无GPU、默认1逻辑tick；windowed运行GPU窗口、默认持续到关闭/Escape；offscreen独立无窗口GPU、默认有限1帧。三个显式模式互斥。
- `--frames 1..1000000`必须配模式；分别计逻辑tick、成功present、GPU提交frame，不把capture计帧。窗口未指定帧数且无profile才无限，显式0拒绝。
- `--debug-layer`、`--gpu-validation`仅用于两GPU模式。Debug默认启用Debug Layer；优化构建可显式开启，GBV隐含Debug Layer；缺组件报错，不静默禁用。
- 退出码：`0` 成功，`1` 初始化/运行失败，`2` 参数错误。帮助/版本独占使用，所有非法输入先于窗口初始化拒绝。
- `TINYME_BUILD_EDITOR=OFF` 不影响 Runtime；默认 CTest 不打开窗口或初始化 GPU。`TINYME_ENABLE_GPU_TESTS=ON` 显式登记交互 GPU 测试。
- `--experiment triangle|clear` 选择窗口实验，默认 triangle；`--shader PATH` 指定三角形源码。默认从程序旁 `shaders/Triangle.hlsl` 读取，自定义路径相对工作目录。headless 不接受这些 GPU 选项。
- `ClearRenderer` 保留清屏生命周期并可选执行三角形，明确 clear → draw → present；shader内部编译器返回自有字节码。F5在Runtime线程人工重编译，候选PSO通过GPU同步/诊断后原子提交，失败保留上一个generation。
- 两程序使用wmain和UTF-8内部参数解析，中文路径通过C++20 char8_t恢复Windows路径；stdout/stderr契约不变。
- 默认测试新增shader.compiler：实际DXC但无GPU。Core测试仍只链接Core/逻辑Runtime；shader测试链接渲染模块但不创建device/窗口。
- 主HLSL文件4MiB预算、两阶段共享快照；include即时读取，不承诺完整依赖图快照。已有固定索引几何与透视相机，尚无通用网格/资产加载、深度附件、离屏GPU、场景、Editor UI或Render Graph。

生命周期取舍见 [ADR-0004](Decisions/ADR-0004-window-and-gpu-lifetime.md)，编译/重载取舍见 [ADR-0005](Decisions/ADR-0005-dxc-and-transactional-shader-reload.md)。

## 2. 目标结构（未实现）

```text
Editor UI ──→ Application / Runtime ←── Runtime App / CLI Driver
                         │
            Scene / Experiment State
                         │
              Render Pipeline（显式 pass 组合）
                         │
                 D3D12 实现与 GPU 资源
                         │
              Windows Window / Surface

Core ← 被必要的上层模块复用；不反向依赖任何模块
```

目标不是所有能力都塞进 Runtime。Runtime 负责组合与生命周期，平台负责 OS，渲染负责资源与命令提交，实验负责具体算法。新目录/target 随首次可验收实现建立，不提前造空接口。

## 3. Editor、Runtime、CLI 三个维度

- **Editor**：编辑与诊断的外壳；调用 Runtime 的配置/操作接口，不能把唯一算法实现写在 UI 回调中。
- **Runtime**：脱离编辑器运行实验或 demo；不包含 Editor 头文件、依赖或资源。
- **CLI**：一种控制入口，不是另一套引擎；可控制 Editor/Runtime 程序。窗口与 GPU 是否初始化由执行模式明确决定。

目标执行模式：窗口 + GPU；无窗口 + GPU（离屏实验，未来）；无窗口 + 无 GPU（M0 的逻辑测试）。三者不能只靠一个模糊的“headless”开关混为一谈；引入离屏模式时定义独立参数契约并更新测试。

## 4. 渲染可编辑性：渐进实现

首后端 D3D12，不创建通用多后端 RHI。用具体 D3D12 类型实现真实路径，但避免向 Scene / Editor 泄漏 COM 指针。算法可以使用 D3D12 特有能力，无需降级成最低公分母接口。

演进顺序：

1. 明确的设备 / swapchain / frame 生命周期与资源销毁；清屏可诊断。
2. 显式有序 pass：每个 pass 的输入、输出、资源格式与参数能被解释；先手工 barrier。
3. 让实验选择 pass 组合，参数不硬编码在 Editor；HLSL 与配置可单独迭代。
4. 出现两个真实管线、生命周期或 barrier 维护负担后，再评估 Render Graph。引入时必须能查看 pass 与资源依赖、执行顺序和 barrier，不能成为调试黑箱。

“可编辑”初期指能修改代码/shader/参数并快速验证；不代表立即提供节点式图编辑器、运行时任意脚本或稳定插件 ABI。

## 5. GPU 生命周期约束（M1 已落地）

- App 拥有会话；会话组合窗口/渲染服务；渲染服务拥有设备和 frame context。销毁按依赖逆序。
- 初期主线程处理窗口和渲染提交；不预制 job system。确有 CPU 瓶颈后再拆线程。
- 每个资源明确 CPU 所有者、GPU 最后使用的 fence 与可释放时刻；不能把 CPU `delete` 当作 GPU 已完成。
- 每帧 context fence 防止覆盖 in-flight 数据；resize/退出可以显式等待安全点。日常渲染不得随手每帧 flush 全 GPU。
- 不在正常帧路径同步 GPU readback；实验导出如需等待，必须在明确的批处理/捕获边界，记录时间不计入正常帧统计。
- 处理非零尺寸、最小化、恢复、窗口关闭；设备移除先明确报错并安全退出，暂不自动恢复设备。
- 初始化时枚举并记录实际 adapter、驱动、feature level 和所需能力。以 RTX 5080 为指定目标，不靠 DXGI 默认顺序选到集显，也不悄悄使用 WARP。
- RT / mesh shader / VRS / shader model 等能力须运行时查询，不能只看显卡型号断言可用。

## 6. 扩展点与明确延后

未来物理可以成为更新阶段的独立模块，通过场景数据和资源契约交互；现在不加入空 `IPhysicsWorld`。资产系统、序列化、ECS、异步编译、后台导入、热重载基础设施均由实验需求驱动。

重构触发条件：第二个实验被迫复制基础代码、跨层依赖增加、资源所有权无法说明、修改 pass 需要触碰不相关入口。触发后记录 ADR 和迁移/验证办法，先减耦合，不直接升级为大框架。

## 7. 实验参数与测量（M2-002 已实现）

- `TriangleParameters` 的唯一 CPU 布局在 Core；HLSL b0 两个 register 对应 32 字节/16 对齐，八个 root constants 在每次 Render 录制时复制。默认保持原图；scale 是 clip-space XY 无量纲缩放，tint 是 RGBA 乘数。Renderer 提交前验证，clear 不使用此参数。当前仍是不透明混合，不支持透明合成。
- CLI 与 Runtime 共享参数/profile 配置。`--profile` 创建 D3D12 内部 `GpuTimer`，不构建通用 Profiler；关闭时无 query/readback 资源。CPU 仅命令录制，GPU 仅 clear + optional draw，两个值都不是整帧时间。
- 两 frame slots 的 query/readback 区间各不重叠，提交绑定最后 fence、尺寸、generation、成功呈现编号。正常领取只查 completed fence，既有 frame context 安全复用前读取旧值；不新增 WaitIdle 或同步回读。退出沿既有安全点收尾，释放 GPU 计时资源后验证 live child。
- Runtime 筛选预热/目标样本并检查环境；Window surface revision 保留尺寸/最小化/DPI 的瞬时变化。F5请求、遮挡、窗口变化、提前关闭、验证 warning 或队列丢样本都不能冒称完整测量。clear 可独立测量，但编译器字段为 null、参数未应用。
- `ProfileReport` 是窗口 Runtime 内部模块：UTF-8 JSON、设备/驱动/构建与验证开关、原始样本、独立统计、完整性原因；不覆盖输出文件。未指定 report 路径时 stdout 先有日志，再输出 JSON；需要机器解析的完整文件使用 `--report`。

数据布局、准确起止范围、失败文件行为和重审条件见 [ADR-0006](Decisions/ADR-0006-experiment-parameters-and-timing.md)，实际验证及未测项见 STATE。

## 8. 索引几何与相机（M2-003 已实现）

- 渲染目标仍只有clear或固定triangle，不增加Scene、资产系统或通用Mesh API。Renderer拥有POSITION/COLOR各float3的24字节VB和R16_UINT IB，四顶点/三索引；额外未引用离屏顶点使错误非索引draw不能混过原画面验收。
- 首次有效候选PSO后，在已有WaitIdle边界创建DEFAULT资源和一次UPLOAD staging，以COMMON初态创建buffer，录制COMMON→COPY_DEST、copy与COPY_DEST→VB/IB状态转换，Signal后等待upload fence完成才释放staging。上传不Present、不增加sample/generation；staging为Renderer成员，Signal/等待失败时交由安全Shutdown处理，不能在局部栈展开时提前释放。F5与resize复用网格；最后索引draw的fence明确记录。
- b0仍32字节八DWORD，b1新增VS-only十六DWORD相机矩阵，两个契约每次录制完整复制；根签名共24DWORD，不需要共享mapped CB。HLSL显式row_major，行向量mul(position,WorldToClip)，Core构造左手View*Projection与[0,1]深度，不隐式转置。
- CameraParameters是无OS/GPU依赖的数据：eye/target/up、vertical FOV、near/far。Renderer在GPU副作用及CPU录制计时前验证/构造矩阵，实际客户区aspect随resize重算。默认eye0/0/-2、target0/0/0、up0/1/0、FOV90、near0.1/far100；16:9保留历史默认三角形。scale依然作用于投影后clip.xy，tint仍不启用alpha混合。
- Runtime和两入口共享--camera-eye/--camera-target/--fov；headless与clear拒绝显式相机选项。报告保留b0 parameter_bytes=32，另列实际几何和fence、相机输入、64字节row-major矩阵；clear为null。初始化上传与CPU相机数学不计入原CPU/GPU采样范围。
- 输入签名不兼容仅消费明确的Debug Layer候选拒绝消息，未知API/设备错误仍faulted；真正未绑定资源测试使用b2。新增资源沿已有WaitIdle/设备移除规则释放，live child在释放后检查。

取舍与重审触发见 [ADR-0007](Decisions/ADR-0007-indexed-triangle-and-camera.md)。M2-003双配置、Editor OFF、GPU/GBV及像素/矩阵验收已完成，证据见STATE与EXP-0001；这里只支持固定索引三角形与最小相机，不等于通用网格/场景系统。

## 9. 显式pass组合与独立GPU批处理（M3已实现）

- 具体模式为direct、RTT-copy、RTT-grayscale，场景与后处理各自PSO/root signature/generation。算法通过同一纹理输入的PS实现与单DWORD选择实际替换，不预建Graph/RHI；两应用组合/调参/重载仍依赖同一Runtime。
- 每slot拥有最终output和可选scene纹理；窗口output来自swapchain，offscreen来自DEFAULT RGBA8纹理。固定RTV[0..1]输出/[2..3]scene，SRV[0..1]scene。slot fence完成后复用；scene PSR→RT→PSR，output COMMON/PRESENT→RT→COMMON/PRESENT。resize或切换资源组合显式WaitIdle后维护descriptor。
- RT纹理创建带实际optimized clear，首次验证ID820已修复；API改变clear值属于冷边界，先idle再重建匹配颜色，不屏蔽warning。不变输入正常Render只保留M2 slot复用等待，不加全队列idle或像素readback。
- post根签名PS t0 table和PS b0单DWORD，整像素Load不加sampler。copy原值、grayscale Rec709数值权重且alpha保持；当前UNORM域，不宣称linear/sRGB色彩管理。旧scene b0八DWORD/b1十六DWORD不变，两个shader使用固定DXC与各自完整主源码快照。
- F5针对已启用scene/post各自事务候选，语法/明确根签名或shader-linkage拒绝保留该pass旧PSO，未知/设备错误仍fatal。没有跨shader多文件事务，不在后台编译；请求使保守完整性报告失效。
- GPU timer扩展为每slot四query（scene前后、post前后），8query/64字节readback。原gpu_pass仍scene clear/draw，新增gpu_post仅全屏draw，barrier/resolve不计二者；无post时null，不假装两段之和等于整帧。frame_number是真实GPU提交，presentation只标窗口成功呈现；offscreen始终0。
- Runtime区分headless纯逻辑、windowedGPU、offscreenGPU。offscreen不创建Window/ HWND/swapchain，不Present，默认有限1帧，profile预算保持；分辨率1..2048。它复用Renderer device/slot/draw分支，不复制设备/PSO实现。
- CaptureRgba只允许已渲染的offscreen输出，显式等最后fence、CopyTextureRegion/restore、capture fence完成，再Map逐行剥离pitch。capture readback是Renderer成员，提交失败也不局部提前释放；命令不计frame/present/timestamp样本。正常Render与TakeCompletedTimings无像素readback。
- WIC只在CPU编码边界运行：内存stream PNG，top-down packed RGBA8，再复用独占CREATE_NEW写；CPU无GPU测试独立验证PNG。report/capture父目录/存在/路径别名前置GPU检查，不承诺两文件事务/崩溃原子发布。
- 默认verify/test显式排除GPU标签，即使缓存GPUtestsON仍不执行GPU；GPU opt-in串行执行窗口与无窗口批处理，实际出口由STATE维护。

边界取舍见 [ADR-0008](Decisions/ADR-0008-explicit-passes-and-offscreen-gpu.md)，实际两套对照和六组实采见EXP-0001/ [EXP-0002](../experiments/EXP-0002-postprocess/README.md)。完整M3六组合、GPU/GBV、算法替换/恢复与PNG验收通过，证据在STATE；保留/调整架构复盘见 [ADR-0009](Decisions/ADR-0009-m3-architecture-review.md)。
