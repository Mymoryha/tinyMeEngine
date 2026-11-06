# ADR-0007：静态索引三角形与显式相机矩阵

- 日期：2026-11-06
- 状态：Accepted
- 关联：M2-003、[ADR-0006](ADR-0006-experiment-parameters-and-timing.md)

## 背景与范围

M2-002仍在VS中用SV_VertexID产生clip-space三角形。下一切片需要可解释的顶点/索引资源与相机变换，不建立通用网格/资产/场景系统，不改变Windows/D3D12与正常帧不额外等待GPU的约束。

## 候选与决定

- 常驻UPLOAD顶点简单，但静态几何不需要CPU可写；选DEFAULT VB/IB和一次UPLOAD staging，首次成功候选PSO后在已有GPU空闲边界录制COMMON→COPY_DEST、copy、COPY_DEST→VB/IB的显式barrier、Signal并等待上传fence。当前驱动忽略buffer创建时COPY_DEST并报告ID1328，因此使用实际COMMON初态，不屏蔽警告。后续重载和resize不重建网格。
- staging由Renderer持有，执行提交后若Signal/等待失败不得在局部栈展开时提前释放；只有完成fence后释放。退出沿已有WaitIdle/设备移除安全策略释放。记录upload fence与最后draw fence，不增加正常帧WaitIdle。
- 顶点为POSITION float3与COLOR float3（24字节，偏移0/12），索引R16_UINT。固定资源包含四个顶点、三个索引；一个离屏未引用顶点用于让错误的非索引绘制或顺序索引无法通过默认画面验证。有效三角形位置/颜色保持M2-002在16:9下的默认像素基线。
- b0继续32字节、8DWORD参数；新增VS可见b1、64字节、16DWORD相机矩阵。两者每次录制全量复制，总root成本24DWORD；不引入CBV/共享映射缓冲。真正未绑定资源拒绝回归移至b2。
- Core持有有限值、退化校验和相机数学；CameraParameters为eye/target/up、垂直FOV、near/far。左手、行向量、row-major、View*Projection、D3D深度[0,1]；HLSL显式row_major，mul(float4(position,1),WorldToClip)，不转置。模型当前为identity，没有场景变换系统。
- 默认eye=(0,0,-2)、target=(0,0,0)、up=(0,1,0)、垂直FOV90度、near0.1/far100。有效顶点使用世界坐标；resize重新计算实际客户区aspect，保持几何比例，不再随窗口横向拉伸。旧scale保持投影后clip.xy缩放，不偷改为模型缩放。
- 两应用通过同一Runtime和CLI提供--camera-eye/--camera-target/--fov；headless与clear拒绝显式相机选项。CPU计时仍从allocator reset到list close，不含CPU相机数学；GPU仍clear+draw，不含初始化上传。报告增量记录几何、相机、矩阵约定及独立64字节，保留b0 parameter_bytes=32。

## 代价与限制

仅一个固定索引三角形；没有资产加载、动态网格更新、网格池、模型变换UI、深度附件或遮挡正确性承诺。首次上传允许明确等待，但不是稳态性能样本。自定义shader须适配POSITION/COLOR输入及需要时使用b1；输入契约导致的候选拒绝仅根据明确Debug Layer消息分类，不吞掉未知API/GPU错误。

## 验证与重审

2026-11-06验收完成：纯CPU矩阵/边界/CLI检查、真实索引画面/非对称及斜视相机/连续更新/裁剪、resize/F5及既有计时/clear/headless回归；Debug和优化、Editor OFF、GPU/GBV验证与live child通过。具体检查数/命令/未测故障只在STATE维护，画面与独立实采在EXP-0001。不据正常测试声称已注入上传OOM/Signal/等待失败。

第二个真实网格、动态上传、模型变换、深度测试或参数增长出现时重新评估资源/变换边界；不为了未来需求提前建立RHI、ECS、资产导入或统一渲染资源框架。
