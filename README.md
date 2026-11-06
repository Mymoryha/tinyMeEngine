# TinyMeEngine

面向个人学习与渲染研究的 Windows C++ 小型引擎。目标不是复刻商业引擎，而是让一个渲染想法能够被清晰实现、观察、验证和复现，并让未来的人或 agent 能够继续工作。

> **幼年期 M0–M3 工程出口已验收**：可重编译索引三角形与相机、共享调参、direct / RTT-copy / RTT-grayscale、独立 scene/post GPU 计时、无窗口 GPU 与 PNG/JSON 复现。真实进度、验证记录和下一步只在 [Docs/STATE.md](Docs/STATE.md) 维护。Editor / Runtime 共享实验路径；尚无 Editor UI、场景、通用网格/资产系统、深度缓冲或 Render Graph，不等于商业引擎完成。

## 接手入口

1. 阅读 [项目章程](Docs/PROJECT.md)：为什么做、做什么、不做什么、阶段验收。
2. 阅读 [当前状态](Docs/STATE.md)：已完成、未完成、下一件事、已知限制。
3. 阅读 [架构](Docs/ARCHITECTURE.md) 和 [协作规范](CONTRIBUTING.md)：模块边界与交付规则。
4. Agent 额外遵循 [AGENTS.md](AGENTS.md)。重要决定参见 [ADR 索引](Docs/Decisions/README.md)。

## 五分钟开始

支持环境：Windows 11 x64、Visual Studio 2022 的“使用 C++ 的桌面开发”、MSVC v143、Windows SDK 10.0.26100.0。使用 C++20、CMake 3.31.10、固定 DXC 1.8.2505.32。完整基线见 [开发环境](Docs/ENVIRONMENT.md)。

在项目根目录的 PowerShell 中：

```powershell
# 若机器尚无 CMake，用已有 Python 安装到仓库忽略目录；只需首次执行。
python -m pip install --only-binary=:all: --disable-pip-version-check --no-warn-script-location --target .tools/python -r tools/requirements-build.txt

# 若本地尚无固定 DXC，显式安装；已安装时不要重复执行。
powershell -NoProfile -File tools/install-dxc.ps1

# 只读诊断；不安装软件、不改系统环境变量。
powershell -NoProfile -File tools/dev.ps1 doctor

# 配置、编译、CTest；失败返回非零退出码。
powershell -NoProfile -File tools/dev.ps1 verify -Configuration Debug
powershell -NoProfile -File tools/dev.ps1 verify -Configuration RelWithDebInfo

# 两个独立程序；都是明确的无窗口逻辑冒烟，不是离屏 GPU 渲染。
.\out\build\windows-vs2022\bin\Debug\tinyme_runtime.exe --headless --frames 3
.\out\build\windows-vs2022\bin\Debug\tinyme_editor.exe --headless --frames 3

# 打开真实 D3D12 窗口；关闭按钮或 Escape 退出。两个程序复用同一渲染实现。
.\out\build\windows-vs2022\bin\Debug\tinyme_editor.exe --windowed
# 有限 GPU 呈现帧数，适合命令行冒烟；不等于 headless 逻辑 tick。
.\out\build\windows-vs2022\bin\Debug\tinyme_runtime.exe --windowed --frames 120
# 编辑仓库 shader 后按 F5；编译失败保留上一画面，修正后再 F5。
.\out\build\windows-vs2022\bin\Debug\tinyme_editor.exe --windowed --shader shaders/Triangle.hlsl
# M1 清屏回归，不编译 shader。
.\out\build\windows-vs2022\bin\Debug\tinyme_runtime.exe --windowed --experiment clear --frames 8
# 两入口共享参数/相机；输出路径必须是新文件，父目录必须存在。
.\out\build\windows-vs2022\bin\Debug\tinyme_runtime.exe --windowed --camera-eye 0.25 0.125 -3 --camera-target 0.25 0.125 0 --fov 75 --scale 0.75 --tint 1 0.8 0.6 1 --profile --warmup 30 --samples 120 --report out/triangle-profile.json

# 独立无窗口GPU复现；不创建HWND或Present，输出必须为新文件。
.\out\build\windows-vs2022\bin\RelWithDebInfo\tinyme_runtime.exe --offscreen --postprocess grayscale --width 641 --height 359 --profile --warmup 30 --samples 120 --capture out/gray-new.png --report out/gray-new.json
# Editor调参/重载走同一Runtime；F5重编译启用的scene和post shader。
.\out\build\windows-vs2022\bin\Debug\tinyme_editor.exe --windowed --postprocess grayscale --post-shader shaders/PostProcess.hlsl

# 显式GPU/GBV验证，会打开/缩放/最小化测试窗口并执行无窗口捕获。
powershell -NoProfile -File tools/dev.ps1 gpu-test -Configuration Debug
```

安装命令需要联网；后续构建和测试不下载依赖。无参数或 `--help` 显示帮助；`--version` 显示版本；无效参数返回 `2`。执行模式必须明确指定，`--headless` / `--windowed` / `--offscreen` 三者互斥；`--frames` 为 `1..1000000`，分别计数逻辑tick / 成功present / GPU提交帧。窗口未设帧数时持续运行，headless默认1 tick，offscreen默认有限1帧且不创建HWND/swapchain。

三角形支持 `--scale 0.01..2`、`--tint R G B A`（各 `0..1`，均须有限）。`--profile` 默认有限运行 30 帧预热 + 120 样本，可由 `--warmup` / `--samples` 修改；显式帧数须覆盖预算。`--report` 不覆盖已有文件；省略时在 stdout 诊断日志之后输出 JSON。CPU 命令录制与 GPU clear/draw 独立统计，均不是整帧/FPS；重载、窗口变化、遮挡、提前关闭或丢样本使报告不完整并返回 `1`。契约与限制见 [ADR-0006](Docs/Decisions/ADR-0006-experiment-parameters-and-timing.md)。

相机支持 `--camera-eye X Y Z`、`--camera-target X Y Z`（有限且各坐标在 `[-10000,10000]`）、`--fov 10..120`；默认左手透视，eye `(0,0,-2)`、target `(0,0,0)`、up `(0,1,0)`、FOV90、near0.1/far100。退化视向量拒绝；resize保持几何比例，scale仍作用于投影后clip.xy。当前仅固定索引三角形，无通用网格加载或深度缓冲；相机/资源契约见 [ADR-0007](Docs/Decisions/ADR-0007-indexed-triangle-and-camera.md)。

`--postprocess none|copy|grayscale`选择direct或颜色纹理+全屏处理；`--post-shader PATH`只配已启用后处理，`--width/--height 1..2048`用于两GPU模式。`--capture PATH.png`只用于offscreen最后成功帧；PNG/JSON父目录须存在、路径不能冲突、不能覆盖旧文件。捕获是显式同步copy/等待/编码，不放入正常Render或计时样本。scene与post独立GPU范围、offscreen frame_number与真实presentation区别见 [ADR-0008](Docs/Decisions/ADR-0008-explicit-passes-and-offscreen-gpu.md)。

Debug GPU模式默认启用 D3D12 Debug Layer。优化构建使用 `--debug-layer` 或 `--gpu-validation` 显式验证；GBV 隐含 Debug Layer。所需 Graphics Tools 缺失时明确报错。默认 `verify` 不初始化 GPU；完整验收方法和限制见 `Docs/STATE.md`。

## 仓库地图

| 路径 | 职责 |
| --- | --- |
| `engine/core/` | 不依赖 OS / GPU / Editor 的基础代码 |
| `engine/runtime/` | 纯逻辑 Session 与独立的窗口会话组合 |
| `engine/platform/windows/` | Win32 窗口、DPI、消息和关闭意图 |
| `engine/render/d3d12/` | 清屏/三角形、固定 DXC、PSO 重载与 GPU 生命周期 |
| `shaders/`、`experiments/` | HLSL 源码与可复现实验记录 |
| `apps/common/` | 两个进程共享的 CLI 驱动，不是引擎公共 API |
| `apps/runtime/`、`apps/editor/` | 独立可执行入口 |
| `tests/` | 默认无 GPU 的契约/CLI 测试，以及显式 GPU 生命周期/窗口测试 |
| `tools/` | PowerShell 开发入口与构建工具版本 |
| `Docs/` | 人与 agent 共用的项目知识源 |
| `.codewiki/`（可选，尚未生成） | 可重建的仓库导览；不是规范与状态的权威来源 |
| `out/`、`.tools/` | 忽略的构建产物、本地工具；不可作为知识源 |

尚未实现的场景、通用网格/资产、离屏GPU等能力按可验收切片添加，不提前堆放空接口。首个实验见 [EXP-0001](experiments/EXP-0001-triangle/README.md)。

## 知识导航

- [项目章程与三阶段路线](Docs/PROJECT.md)
- [当前架构与演进边界](Docs/ARCHITECTURE.md)
- [环境与硬件实测基线](Docs/ENVIRONMENT.md)
- [开发、质量、提交与协作](CONTRIBUTING.md)
- [知识管理：存什么、存哪里、什么时候更新](Docs/KNOWLEDGE.md)
- [当前任务、验收与交接](Docs/STATE.md)
- [重要节点历史](Docs/DEVLOG.md)
- [架构决策记录](Docs/Decisions/README.md)
- [渲染实验记录模板](Docs/Templates/EXPERIMENT.md)

尚未指定开源许可证；公开发布或引入第三方资产前由项目所有者明确授权策略。
