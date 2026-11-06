# 架构与契约

## 1. 已实现结构（M2-001）

依赖箭头从使用者指向被使用者；本节只描述真实代码。

```text
apps/editor ──┐                      ┌── tinyme_core（CLI）
             ├── tinyme_app_host ────┼── tinyme_runtime（纯逻辑 Session）
apps/runtime ┘                      └── tinyme_windowed_runtime
                                           ├── tinyme_runtime
                                           └── tinyme_d3d12 ──→ tinyme_windows

CPU tests ── tinyme_core + tinyme_runtime
GPU tests ── tinyme_d3d12 + tinyme_windows
```

- 两个应用保留 console 子系统，CLI 的 stdout/stderr 与退出码可被脚本捕获；不是仅能通过 GUI 启动的程序。
- `Session` 仍只拥有逻辑 tick/模拟时间，不依赖窗口、GPU、Editor。`WindowedSession` 是单独的组合实现，不把 GPU 依赖带入 CPU 测试 target。
- `tinyme_windows` 拥有一个 Win32 HWND；`tinyme_d3d12` 是具体清屏/三角形实现，通过 PImpl 隐藏 COM 细节，不是通用 RHI。
- `RunWindowedSession` 位于 Runtime 层，依次构造 Window → Renderer → Session。显式 Renderer Shutdown 后才允许 Window 析构。
- 无参数显示帮助；`--headless` 保持无窗口/无 GPU，默认 1 个逻辑 tick；`--windowed` 运行 GPU 窗口，默认持续到关闭/Escape。模式互斥。
- `--frames 1..1000000` 必须配模式；headless 计数逻辑 tick，windowed 计数成功呈现。未显式指定窗口帧数才无限执行，显式 0 拒绝。
- `--debug-layer`、`--gpu-validation` 仅用于窗口。Debug 默认启用 Debug Layer；优化构建可显式开启；GBV 隐含 Debug Layer。缺少调试组件时报错而非静默禁用。
- 退出码：`0` 成功，`1` 初始化/运行失败，`2` 参数错误。帮助/版本独占使用，所有非法输入先于窗口初始化拒绝。
- `TINYME_BUILD_EDITOR=OFF` 不影响 Runtime；默认 CTest 不打开窗口或初始化 GPU。`TINYME_ENABLE_GPU_TESTS=ON` 显式登记交互 GPU 测试。
- `--experiment triangle|clear` 选择窗口实验，默认 triangle；`--shader PATH` 指定三角形源码。默认从程序旁 `shaders/Triangle.hlsl` 读取，自定义路径相对工作目录。headless 不接受这些 GPU 选项。
- `ClearRenderer` 保留清屏生命周期并可选执行三角形，明确 clear → draw → present；shader内部编译器返回自有字节码。F5在Runtime线程人工重编译，候选PSO通过GPU同步/诊断后原子提交，失败保留上一个generation。
- 两程序使用wmain和UTF-8内部参数解析，中文路径通过C++20 char8_t恢复Windows路径；stdout/stderr契约不变。
- 默认测试新增shader.compiler：实际DXC但无GPU。Core测试仍只链接Core/逻辑Runtime；shader测试链接渲染模块但不创建device/窗口。
- 主HLSL文件4MiB预算、两阶段共享快照；include即时读取，不承诺完整依赖图快照。尚无离屏GPU、场景、Editor UI、网格/相机或Render Graph。

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
