# 重要节点记录

倒序追加，只记值得长期保留的节点。当前任务状态见 [STATE](STATE.md)，决定原文见 [ADR 索引](Decisions/README.md)。不以本文件复制全部代码改动或会话记录。

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
