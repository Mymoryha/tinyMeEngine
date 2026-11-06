# ADR-0006：显式三角形参数与 fence-safe 独立计时

- 日期：2026-11-06
- 状态：Accepted
- 关联：M2-002、[EXP-0001](../../experiments/EXP-0001-triangle/README.md)

## 背景与约束

已有 D3D12 三角形与事务 F5 重载，需要从两个应用共同传入参数并测量实际渲染范围。只支持当前 Windows x64 / C++20 / VS2022 / RTX 5080，不扩张成材质反射、通用 RHI 或 profiler 平台。正常帧不得为计时额外等待 GPU。

## 候选

1. 共享 upload buffer：代码少，但 CPU 可能覆盖 GPU 正在读取的参数；不能接受。
2. 每 frame context 独立 constant buffer：可安全扩展，但目前仅需 32 字节，引入 CBV/upload 生命周期没有收益。
3. root constants：命令录制时复制八个 DWORD，适合当前小契约；后续真实大参数需求出现再改为 frame-safe buffer。
4. CPU 包围 Present 或每帧 WaitIdle 测 GPU：把 VSYNC/等待混入算法范围，或改变管线行为；拒绝。

## 决定

### 参数

- 唯一 CPU 契约在 Core `TriangleParameters`：32 字节、16 字节对齐；RGBA tint 在偏移 0，clip-space XY 无量纲 scale 在 16，三个零预留 float 在 20。static assertions 检查大小、偏移、类型属性；HLSL 用 b0 / packoffset(c0、c1) 对应。
- scale 有限 [0.01,2]，RGBA 有限 [0,1]，预留必须为零。CLI 严格完整解析；Renderer 在提交副作用前再检查。当前不透明 PSO 不开启混合；tint alpha 改输出 alpha，不承诺透明合成。
- 每次 Render 把当前参数复制到命令列表；两个应用只通过共享 Runtime 配置传入。clear 不应用三角形参数，也不加载 DXC。
- M2-002使b0合法，当时未绑定资源拒绝回归改为b1；M2-003的 [ADR-0007](ADR-0007-indexed-triangle-and-camera.md) 将b1用于相机，当前拒绝回归因此使用b2。未知PSO/API故障分类继续遵守ADR-0005，不弱化为可恢复编译错误。

### 测量

- 仅显式 profile 创建两个 disjoint slots、四个 timestamp queries、32 字节持久映射 readback，slot 跟随既有 backbuffer frame context。
- CPU：allocator reset 前到 command-list close 后；不含 fence wait、提交、Present、诊断、重载。该范围包含启用计时时的 query 命令录制开销，不是整个 CPU frame。
- GPU：第一个 timestamp 在入 render-target barrier 后/clear 前；第二个在可选 triangle draw 后/出 barrier 与 ResolveQueryData 前。即 clear + 可选 triangle；不含 Present/VSYNC、barriers、resolve 或编译重载，不是整帧 GPU 时间。
- 在既有 frame fence 等待后 Poll；TakeCompletedTimings 只查 completed fence，不等待。只有对应 fence 完成后读该 slot；复用前必须领取旧值。退出沿已有 WaitIdle 收尾，之后释放 query/readback 再检查 live child。
- 未领取队列最多 256 条，溢出计入 dropped。频率查询失败、device-removed fence sentinel、倒序 timestamp 或不安全复用保持明确错误。

### 报告完整性

- CLI profile 默认 30 成功呈现帧预热 + 120 样本；预算各不超过 100000，样本至少 1。未指定 frames 自动有限执行；指定 frames 必须覆盖预算。
- 只统计目标成功呈现编号、初始尺寸/generation。遮挡提交编号零，不消费呈现预算；遮挡、F5请求、尺寸/DPI/最小化变化均使报告不完整。Window surface revision 保留同次消息泵里发生又恢复的变化。完整性采用整段会话的保守策略：显式frames长于采样预算时，采样后但退出前的上述变化或用户关闭仍使complete=false。
- 用户提前关闭、丢样本、验证 warning 或样本不足同样使 complete=false，CLI 返回 1；设备/API错误直接失败，不伪造报告成功。
- JSON schema 1 含设备/驱动、配置、验证开关、参数、范围、预算、状态原因、原始样本与独立统计。空统计 null；P95 nearest rank。clear 编译器字段为 null，parameters_applied=false。
- report 父目录必须存在；CREATE_NEW + 独占句柄拒绝覆盖。写入/短写/flush失败尝试按自身句柄删除新半成品；close失败在仍可能有效的自身句柄上标删除并限次重试关闭，原始错误与最终清理错误分别报告。不按路径删除，避免误删并发赢家的文件；删除或重复关闭失败不能声称已清理。不是原子发布或断电/进程崩溃恢复事务。未指定路径则 stdout 在诊断日志之后输出 JSON，不承诺 stdout 全部都是 JSON。

## 代价与限制

计时量很小，timestamp/验证层/驱动/调度影响不可忽略；验证层开启数据不与关闭数据混报。profile 不是 FPS、吞吐量或通用 benchmark。shader 路径和引擎版本不能独立标识修改后的源码，复现实验必须同时保留 Git 基准/工作区、shader 源码及输入。没有自动 GPU 时钟校准、功耗控制、性能回归阈值、PIX、UI 参数编辑、后台重载或 offscreen 路径。

## 验证与重审

验收数字、命令与已知未测项见 [STATE](../STATE.md)，参数像素与测量基线见 EXP-0001。自动覆盖无 GPU 参数/统计/报告/CLI、真实参数更新与重载、slot 领取/尾帧/resize、256 队列与四类会话中断；Debug Layer/GBV 清洁工作及释放检查。

第二个实验需要不同布局、更大参数、多个 pass/queue、UI 动态调参或持续后台采样时重审；按真实资源需要提取边界，不提前建立反射或通用 profiler。
