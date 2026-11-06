# ADR-0005：固定 DXC 与事务式 shader 迭代

- 日期：2026-11-06
- 状态：Accepted
- 关联任务：M2-001；实验 EXP-0001

## 背景与选项

首个 shader 实验需编译 HLSL、显示可修改结果、失败不中断有效画面。可使用系统任意 DXC、离线编译流程或固定运行时 DXC。选择固定运行时 DXC，保留人工触发路径，不引入后台编译/文件监听/插件。

## 决定

采用微软官方 NuGet `Microsoft.Direct3D.DXC` 1.8.2505.32（官方发布 v1.8.2505.1 对应包）；实测 `dxcompiler.dll: 1.8 - 1.8.2505.32 (b106a961d)`。锁文件、URL和摘要在 `tools/dxc-lock.json`。摘要是本次官方来源下载后计算的项目锁，不冒称发布方独立签名。通过显式 `tools/install-dxc.ps1` 获取，CMake 配置只检查本地文件、版本锁变化触发重配，不联网。

动态加载程序旁 x64 compiler DLL，仅允许 DLL 自身目录/System32 搜索，不依赖 PATH；DXC COM 对象全部释放后才卸载 module，字节码拷贝为 C++ 自有数据。shader 默认位置也相对程序，不依赖工作目录。公开发布需携带 compiler/validator 的许可证和通知，CMake已复制到输出 licenses/dxc；这不替代引擎自身 LICENSE 决策。

编译 VSMain/vs_6_0 和 PSMain/ps_6_0，HLSL 2021、严格模式、warning-as-error、O3。Debug CPU 编译不代表 shader 使用 O0；当前未启用 shader 调试符号或 PIX 集成。主文件有4MiB读取预算，两阶段共享同一主源快照；include仍由DXC在各阶段即时读取，暂不提供整个依赖图原子快照。

## 渲染与重载

保留 M1 具体 `ClearRenderer` 类型，并增加可选三角形 PSO；旧类型名仍表达默认清屏生命周期，不进行无必要的大范围重命名。无 shader 管线时只清屏；存在管线时显式 clear → draw triangle → present。三角形由 SV_VertexID 生成，不引入网格资产/场景框架。

- `--windowed` 默认 triangle；`--experiment clear` 保持 M1 路径；`--shader PATH` 仅 triangle。
- 窗口 F5 只登记请求，Runtime 主循环编译；重复按键不持续触发。初次编译失败返回1，后续编译失败保留旧管线并输出诊断。
- 完整编译候选之后先等待旧 GPU 工作，检查原有诊断，再建立候选 root/PSO；最后通过诊断才提交对象和增长generation。不能在失败返回前已经替换旧版本。
- 编译失败为普通可恢复结果。候选 PSO 中明确 root/linkage 契约拒绝单独报告 `rejected_candidate`，不混进正常帧 GPU error 统计；只有这个同步隔离范围可消费白名单ID。其他warning/error/corruption仍faulted并抛出，不能全局屏蔽调试层。没有调试层或没有白名单正向证据的PSO创建失败同样抛出，不能将OOM等未知API失败伪装为普通shader拒绝；开发资源契约时应开debug层。
- 重载时允许GPU idle等待，正常帧不因此添加全队列同步；这是初期可靠的人工迭代路径，不宣称无卡顿异步热重载。

Windows CLI改用wmain，参数转换为UTF-8供纯逻辑解析，路径通过C++20 char8_t恢复为UTF-16；console子系统和退出码不变。中文shader路径已经桌面实测。

## 验证与代价

默认无GPU CTest包括实际DXC编译、include定位、错误恢复与输入预算。显式GPU测试保留M1生命周期，并增加shader编译失败、缺文件、PSO契约拒绝、恢复和generation检查。真实桌面F5实验对比失败前后像素与成功改色结果，详见EXP-0001/STATE。

代价：同步编译可能短暂停顿；include不是原子快照；只有固定的三角形契约，尚无任意材质/参数资源绑定。未知device故障仍沿ADR-0004退出。重审触发：第二个shader实验需要不同资源契约、明显编译停顿、多文件编辑一致性、GPU计时或shader调试；先给出实际痛点，不直接升级完整Render Graph。

## 来源

- 官方发布：https://github.com/microsoft/DirectXShaderCompiler/releases/tag/v1.8.2505.1
- 官方包：https://www.nuget.org/packages/Microsoft.Direct3D.DXC/1.8.2505.32
- 获取日期：2026-11-06。已检查包内 nuspec/发布说明/许可；平台为 x64，未做其他架构适配。
