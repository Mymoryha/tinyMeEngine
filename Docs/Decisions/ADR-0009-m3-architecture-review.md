# ADR-0009：M3架构复盘——保留具体边界，调整执行与验证契约

- 日期：2026-11-06
- 状态：Accepted
- 关联：整个M3、[ADR-0002](ADR-0002-boundaries-and-render-evolution.md)、[ADR-0008](ADR-0008-explicit-passes-and-offscreen-gpu.md)、[EXP-0001](../../experiments/EXP-0001-triangle/README.md)、[EXP-0002](../../experiments/EXP-0002-postprocess/README.md)

## 实际证据

M2 direct索引相机实验和M3 RTT-copy/grayscale实验共享相机/几何/shader/Runtime。两入口以相同输入执行窗口和独立offscreen GPU，direct/copy图像全字节一致，灰度按独立UNORM期望全图误差<=1。post shader实际替换为反色，再恢复原shader，scene generation与静态网格upload fence不变；缺文件/语法/未绑定t1与VS-PS语义链接仅拒绝候选。

双配置默认CTest各128/128、Editor OFF各67/67、GPU opt-in各138/138；GPU GBV两模式均实际运行，具体断言/日志只在STATE。正常frame没有像素readback，capture单独copy/wait/encode，32帧GPU测试不因捕获虚增样本；PNG CPU解码验证颜色/alpha/方向。两入口六组优化120样本实采完成，不据此声明性能优势。

## 保留

1. Windows x64 / C++20 / v143与唯一D3D12后端；Core无OS/GPU，Editor依赖Runtime而不是反向。纯逻辑headless仍不初始化window/GPU。
2. 原scene b0/b1根常量、静态DEFAULT几何上传、双slot fence资源所有权；按实际替换需求新增post独立契约，而不是通用材质/资源API。
3. Runtime组合共享逻辑，Application只解释模式/输入/退出；窗口与offscreen输出分支复用具体renderer，未复制D3D12设备/PSO实现。
4. shader候选事务替换与明确错误分类；不吞未知API/设备/OOM故障，不为“恢复所有错误”绕过Debug Layer。
5. 独立CPU/GPU测量与可解释采样范围，知识文件而不是agent记忆交接，不把成功GPU测试扩大成任意硬件/故障保障。

## 调整与依据

- **独立GPU执行模式**：RTT是pass目标，不是无窗口运行。新增offscreen必须有限帧、真实presentation=0；把它塞进headless会打破无GPU逻辑边界，故明确第三模式。
- **目标/descriptor所有权**：同一renderer增加每slot scene与最终纹理分支，资源转换直接写出；当前只有两组合，Graph的依赖声明/编译/生命周期引入成本高于已证明需求。
- **算法边界**：post PSO/root signature/t0输入与scene分开，实现copy→grayscale和实际shader反色替换，不需要抽象IAlgorithm/插件。下次真实第三组合/跨queue出现时再提取边界。
- **分段计时**：原gpu_pass严格保留scene范围，新增gpu_post与frame_number，不能混入post/barrier而让历史指标语义漂移。捕获必须显式边界且不增加样本。
- **默认无GPU守卫**：缓存GPU tests ON会使仅靠option默认值的verify失效；开发入口和CTest presets显式排除GPU标签。已实际设置缓存ON验证128无GPU测试，并恢复OFF。
- **纹理清屏契约**：首次真实GPU产生ID820，不屏蔽warning，传optimized clear并在clear值变化冷边界重建；固定输入正常Render无新增idle。
- **输出安全**：既存/父目录/相对别名/大小写/Win32尾随点空格/ADS GPU前校验，WIC内存编码复用CREATE_NEW。两输出不做事务：PNG成功后JSON失败可能保留完整PNG，显式诊断，不承诺崩溃无残留。
- **结构债务**：ClearRenderer具体实现已变长，但重复字段helper只在实际资源转换/target创建处提取，不趁阶段结束全文件重写；两输出分支再扩张时优先独立具体pass方法/资源成员，再按证据决定更大接口。

## 暂不做与下一阶段

没有场景/ECS、资产加载、深度缓冲、UI、通用Profiler、Graph、RHI、跨平台、后台编译、多文件shader事务。M3出口满足只代表幼年期首条可迭代实验链成熟，不等于所有长期目标完成；下一阶段如何进入成长期由所有者与STATE新切片决定，不自动加入大框架。

尚未实际注入新的GPU OOM/Signal/等待超时/设备移除/断电；已有文件I/O故障注入与正常资源释放不能代替这些证据。单驱动/hardware的像素/计时对照不是跨GPU兼容性承诺，不因人工桌面捕获瞬态改宽RGB容差。

## 重审触发

第三条真实pass路径、跨queue依赖、动态资产/target数量增长、可测重复维护成本，或算法替换需要通用资源描述时重审本决策。不得把预计扩展当作当下已支持；不能提前推翻既有单后端/平台ADR。
