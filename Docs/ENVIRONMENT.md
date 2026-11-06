# 开发环境与硬件基线

采集日期：2026-11-05。只记录工程相关信息，不记录用户名、机器名、序列号或账号。该基线服务当前开发机，不代表跨设备兼容性承诺。

## 1. 实测硬件

| 项 | 结果 | 工程含义 |
| --- | --- | --- |
| OS | Windows 11 专业版 x64，10.0.26100 | 唯一 OS 基线 |
| CPU | AMD Ryzen 9 9950X，16 核 / 32 线程 | 初期不需要任务系统；构建可并行 |
| 内存 | 系统报告约 93.6 GiB（约 96 GB 配置） | 足够渲染研究；不能以大内存代替资源生命周期管理 |
| 主 GPU | NVIDIA GeForce RTX 5080，驱动报告 16303 MiB 显存 | 约 16 GB VRAM；唯一计划中的渲染验收 GPU |
| NVIDIA 驱动 | `nvidia-smi` 577.00；WMI 32.0.15.7700 | 这是实际安装版本，不宣称最新 |
| 其他显示适配器 | AMD Radeon(TM) Graphics | 初始化时必须显式选 RTX 5080，防止用错 adapter |
| 当前显示模式 | WMI 报告 3840 × 2160 | 不推断显示器数量/HDR/刷新率；窗口必须处理 DPI |
| 工作盘 | E:，NTFS，约 3815.4 GiB，总空闲约 3613.9 GiB | 初始快照，空闲会变化；大捕获不进 Git |

初始来源：`Get-CimInstance`、`Get-Volume`、`nvidia-smi`。2026-11-06 M1 实际 D3D12 查询：RTX 5080，driver `32.0.15.7700`，最高 feature level `12_2`，查询范围内 shader model `6_8`，RT tier `1.1`，VRS tier `2`，mesh shader tier `1`。这是能力查询，不代表这些渲染特性已有实现；shader model 查询最高枚举为 6.8，不声称探测了更新版本。

DXGI `DedicatedVideoMemory` 为 15889 MiB，与 NVIDIA 总显存报告口径不同，不是实时显存预算/使用量。M1 窗口实测 DPI 为 144（150%），客户区 1280×720 像素。系统调试层 `D3D12SDKLayers.dll` 版本 `10.0.26100.9549` 已可用并实际运行 GBV；DXC 已于 M2 固定并实测，PIX 尚未核实。

## 2. 工具链选择

| 工具 | 已安装/固定基线 | 说明 |
| --- | --- | --- |
| Visual Studio | Community 2022 17.14.35 | 使用已安装的 C++ 组件 |
| MSVC | v143，工具目录 14.44.35207；实测编译器 19.44.35228.0 | CMake 固定 v143；不绑定机器绝对路径 |
| Windows SDK | 10.0.26100.0 | preset 使用 `x64,version=10.0.26100.0` 显式选择；配置时核对实际 SDK |
| CMake | 3.31.10 | 已安装到忽略的 `.tools/python/`；按下述命令可复现 |
| DXC | 官方 NuGet 1.8.2505.32；实测 b106a961d | `.tools/dxc/1.8.2505.32/`；版本/URL/SHA256 在 `tools/dxc-lock.json` |
| clang-format | 19.1.5（VS2022 附带） | `.clang-format` 基于 LLVM；不是编译前提 |
| Git | 2.55.0.windows.1 | 已安装 |
| Python | 本机已有 3.14 系列 | 仅可选 CMake wheel 安装使用，不是引擎运行依赖 |

本机同时有 VS2026，当前不使用，以减少工具链漂移。`tools/dev.ps1` 使用 `vswhere` 明确查找 VS2022 C++ 组件；CMake 的 generator 为 `Visual Studio 17 2022`。

## 3. 安装与故障定位

从普通 PowerShell 在仓库根目录执行：

```powershell
python -m pip install --only-binary=:all: --disable-pip-version-check --no-warn-script-location --target .tools/python -r tools/requirements-build.txt
# 尚无固定 DXC 时安装；已存在不覆盖。
powershell -NoProfile -File tools/install-dxc.ps1
powershell -NoProfile -File tools/dev.ps1 doctor
powershell -NoProfile -File tools/dev.ps1 verify -Configuration Debug
```

DXC安装来源为微软官方NuGet，固定包/文件摘要由本次下载后计算并记录在锁文件；不是发布方独立签名。`tools/install-dxc.ps1` 明确联网并拒绝覆盖已有目录，缓存archive校验失败时不解压。CMake只校验header/compiler/validator并部署程序旁DLL，不搜索系统DXC；许可证/通知复制到 `bin/<Configuration>/licenses/dxc/`，公开分发前需保留这些文件。安装包含 `LICENSE-MS.txt`、`LICENSE-LLVM.txt`、`LICENCE-MIT.txt`，已核对 `distributable_files.txt`，不替代项目自身许可证选择。详见 ADR-0005。

本地 CMake 实体路径为 `.tools/python/cmake/data/bin/cmake.exe`。脚本优先使用该固定工具，其次 PATH 的 CMake，再尝试 VS2022 bundled CMake；版本不等于基线会报错并要求显式解决，不会自动联网安装。`ctest.exe` 必须与所选 CMake 在同一目录。

- 缺少 MSVC：通过 Visual Studio Installer 增加 VS2022 的“使用 C++ 的桌面开发”。不自动安装大型组件。
- 缺少 SDK：安装 `10.0.26100.0`；若要升级基线，修改 preset 和本文件并重新验证两种配置。
- 执行策略阻止脚本：先查看组织/个人策略，由所有者决定可接受的方式；不在脚本内自动修改 ExecutionPolicy。
- 工具安装仅为首次准备；构建不使用 `FetchContent` 或自动更新依赖。
- 切换 SDK/generator 时另建构建目录，或由所有者确认清理对应 `out/build/` 子目录；不要删除整个仓库、`.git` 或 `.codebuddy`。

## 4. 配置与排障入口

- `Debug`：CPU 调试和契约检查。M1 引入后默认开启 D3D12 Debug Layer。
- `RelWithDebInfo`：带符号优化构建；将来性能测量用此配置并记录 GPU 验证开关。
- `out/build/windows-vs2022/`：生成项目；`bin/<Configuration>/`：可执行；`Testing/Temporary/LastTest.log`：最近 CTest 记录（会覆盖，不能作为唯一长期证据）。
- `tools/dev.ps1` 支持 `doctor / configure / build / test / verify / gpu-test`。`verify` / `test` 先构建再跑默认 CPU CTest；`gpu-test` 在此基础上执行两种 GPU 生命周期验证和两入口严格窗口测试，需要交互桌面，不能在无桌面 runner 上声称通过。
- 可用 `-DTINYME_ENABLE_GPU_TESTS=ON` 在独立目录登记 GPU CTest；默认 OFF，避免普通验证弹窗。
- 窗口为 1280×720 客户像素、VSYNC、SDR RGBA8；不是性能基准。`--windowed --frames N` 可限定成功呈现帧数；最小化/遮挡不消耗该预算。

## 5. 环境变更规则

驱动、SDK、编译器、DXC 或依赖升级单独提交/记录，先跑原有用例再做 feature 开发。驱动升级前后性能数据分别标记；不得把新驱动收益归因于算法。新能力引入时补充实际查询结果和必要条件，而不是扩展成全硬件适配矩阵。
