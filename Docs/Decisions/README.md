# 架构决策索引

编号不复用；变更历史保留；具体流程见 [知识管理](../KNOWLEDGE.md)。

| ID | 状态 | 决策 | 重审触发 |
| --- | --- | --- | --- |
| [ADR-0001](ADR-0001-platform-and-toolchain.md) | Accepted | Windows x64 / C++20 / VS2022 v143 / D3D12 单后端 | 当前工具或 API 阻塞真实实验 |
| [ADR-0002](ADR-0002-boundaries-and-render-evolution.md) | Accepted | Editor/Runtime 分离；显式 pass 渐进演化，不预制通用 RHI | 第二个管线出现实际重复或资源依赖难维护 |
| [ADR-0003](ADR-0003-knowledge-and-handoff.md) | Accepted | 仓库知识单一来源，任务/决策/历史分工 | 同步维护成本超过实际收益或引入外部任务平台 |
| [ADR-0004](ADR-0004-window-and-gpu-lifetime.md) | Accepted | 独立窗口 Runtime、逐帧 fence、延后关闭与故障拆卸 | 多窗口、离屏 GPU 或设备恢复需求 |
| [ADR-0005](ADR-0005-dxc-and-transactional-shader-reload.md) | Accepted | 固定 DXC、主文件快照、事务 PSO 与人工 F5 | 第二个资源契约、编译停顿或多文件一致性需求 |

新增文档使用 `ADR-XXXX-short-topic.md`，包含：日期、状态、关联任务、背景、选项、决定、代价、验证、重审触发。替代决定必须双向链接，不覆盖旧结论。
