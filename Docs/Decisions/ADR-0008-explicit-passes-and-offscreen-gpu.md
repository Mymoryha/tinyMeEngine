# ADR-0008：显式颜色pass与独立无窗口GPU

- 日期：2026-11-06
- 状态：Accepted
- 关联：M3-001/002、[ADR-0002](ADR-0002-boundaries-and-render-evolution.md)、[ADR-0004](ADR-0004-window-and-gpu-lifetime.md)、[ADR-0006](ADR-0006-experiment-parameters-and-timing.md)

## 背景与范围

整个M3要求两种真实pass组合、一次算法替换、Editor调参复用Runtime、无窗口GPU复现及架构复盘。M2仅direct场景输出且只有窗口GPU，不能把RTT当无窗口执行，也不能把纯逻辑headless偷偷升级GPU模式。

## 决定与候选

- 三执行模式为headless逻辑、windowed GPU、offscreen GPU。offscreen不创建Window/HWND/swapchain、不Present、有限1帧默认；profile无显式frames沿预热+样本预算。两应用共享Application和Runtime配置/算法，不复制UI算法。
- 保留direct scene→output；新增scene→RGBA8_UNORM纹理→全屏copy/grayscale→output。用现有ClearRenderer具体设备/命令/双slot/fence共用输出target分支，不复制离屏renderer、RHI或Render Graph。
- 每slot独立scene/output纹理、固定RTV与SRV索引；scene的PSR→RT→PSR，output的COMMON/PRESENT→RT→COMMON/PRESENT手工明确转换。resize/选择pass时显式idle；正常固定输入Render仅既有slot复用等待，没有新增全队列flush或像素readback。首次真实GPU检测RT纹理无optimized clear产生ID820，修复为传入实际clear值；clear颜色变更是显式冷资源边界，idle后重建匹配优化值的scene/offscreen纹理，不屏蔽性能warning。
- 两算法共用独立post根签名：PS t0 table与PS b0单DWORD选择，不污染scene的b0八DWORD/b1十六DWORD。HLSL整像素Load不需sampler；copy不重采样，grayscale用0.2126/0.7152/0.0722权重保留alpha，在当前UNORM数值域运算，不宣称线性光照或sRGB正确色彩管理。
- scene和post PSO各自事务替换、独立generation。F5请求使采样环境不完整；候选语法/明确根签名拒绝保留该pass旧PSO，未知API/设备错误仍fatal退出。不是多文件原子事务或后台编译。
- scene GPU耗时沿M2 clear+indexed draw，post GPU只包围全屏draw；同slot四queries，两slots8queries/64byte timestamp readback，以同提交fence和frame_number关联。presentation只是真实窗口成功呈现编号，offscreen始终0。没有post时报告post统计null；不能把两段之和冒称包含barrier/resolve的整帧时间。
- 分辨率1..2048，明确限制峰值资源/捕获预算。PNG仅offscreen最后成功帧显式capture边界：先等最后output fence、CopyTextureRegion到专属readback、restore、再等capture fence；去row pitch padding，RGBA8 top-down。capture命令不计GPUframe/present/样本，禁止正常Render同步像素readback。
- 像素readback由Renderer持有直到GPU完成，Signal/等待失败不局部提前释放。WIC在内存编码PNG，再由现有CREATE_NEW独占安全写保存，不新增图片库或覆盖文件。report/capture前置检查父目录、既存和路径冲突，不承诺两文件事务/崩溃原子发布。
- 默认verify明确排除GPU标签，不依赖缓存option初值；GPU批处理虽无窗口仍只能显式GPU验收执行。

## 代价与限制

仍固定单三角形、无深度/资产/场景/UI；双slot资源维护手工，初次初始化/resize/F5/capture允许可解释等待。offscreen双slot复用仍可能等待slot fence，这不是像素readback。两不同实验为direct索引相机和RTT灰度后处理，非凭空声明支持任意pass图。

## 验证与重审

2026-11-06整个M3工程验收完成：direct/copy同输入图像、灰度独立期望、两入口与无窗口复现、非256对齐宽度capture、最后帧/resize/算法切换、根签名与shader-linkage候选拒绝/恢复、PNG原字节不覆盖、scene/post样本与资源释放、双配置Editor OFF/GPU/GBV全通过。真实检查数/日志/未测项在STATE；架构保留/调整复盘见 [ADR-0009](ADR-0009-m3-architecture-review.md)。

出现第三种真实pipeline、跨queue或动态依赖且手工barrier错误率/重复证据明确，再评估Graph/资源边界；当前不推翻单后端、不添加通用RHI。
