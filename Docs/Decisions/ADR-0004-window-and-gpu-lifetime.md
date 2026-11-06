# ADR-0004：窗口、GPU 帧与错误拆卸边界

- 日期：2026-11-06
- 状态：Accepted
- 关联任务：M1-001

## 背景与选项

M1 需要真实窗口和 D3D12 首帧，但不能破坏 M0 的纯逻辑路径，也不能因为首帧简单忽略 in-flight GPU 资源。可以把全部代码塞入 app host，或让 Runtime 自身强依赖 GPU；两者都扩大耦合。

## 决定

- 新增具体 Windows / D3D12 target，以及独立窗口 Runtime 组合 target。保留纯逻辑 `tinyme_runtime`，CPU tests 只链接 Core/逻辑 Runtime。
- 两应用使用 console 子系统，显式 `--windowed` 才创建窗口；无参数帮助和 `--headless` 语义不变。不需要 `WinMain` 或隐式 console 连接策略。
- 当前线程设为 Per-Monitor-V2，析构恢复原 DPI context；一个会话拥有一个窗口。跨屏 DPI 处理接受系统建议窗口矩形，错误留到消息泵外报告，异常不越过 OS 回调。
- HWND 借给 Renderer。`WM_CLOSE` / Escape 只记录意图；先 GPU Shutdown，再销毁窗口，不能在 WndProc 里释放 swapchain。
- 使用两个 flip-discard RGBA8 buffer、一个 direct queue、每 buffer allocator 与最后使用 fence。normal frame 只等待将复用的 allocator，resize/退出才等待整条队列安全点；Present 使用 VSYNC，不增加 tearing/fullscreen/HDR 适配。
- 每帧显式 `PRESENT → RENDER_TARGET → PRESENT` barrier；暂不建立 Render Graph 或 shader pass 接口。
- Debug 默认启用 Debug Layer，GBV 显式启用且不用于性能数据；优化配置可显式验证。调试层不可用即报错，不悄悄跳过验收。

## 失败与拆卸契约

Fence 等待总预算为 10 秒，每次 event 唤醒重新确认目标 fence；event 不是完成的事实源。必须检查 `UINT64_MAX` 的设备移除信号。

`stopped`（禁止提交）、`faulted`（提交/重建失败）和 `shutdown_complete`（安全拆卸完成）是不同状态。破坏性 resize 或提交失败后只允许 Shutdown；非法输入在破坏状态前拒绝，不改变正常状态。

Shutdown 确认 GPU 完成后释放子对象；设备已移除则释放并继续向上报告失败。存活设备等待超时不能当作成功拆卸：显式调用保持未完成状态，析构再尝试；若仍无法建立安全条件，使用明确 `std::terminate` fail-fast，拒绝释放仍被 GPU 使用的资源。此策略是罕见不可恢复故障处理，不承诺自动设备恢复，也不返回假成功。

调试层正常诊断全量检查 warning/error/corruption；完成子对象释放后检查 live objects。检查时 Device 自身与 summary 仍应存活，只排除这两项；残留 child 即使是 INFO 也计失败。诊断异常不阻止已经完成同步的资源释放。

## 验证与代价

默认 CTest 无 GPU；显式 `gpu-test` / 可选 GPU CTest 在交互桌面上运行。验证创建/销毁、实际尺寸变化、最小化/恢复、非法尺寸与颜色、关闭前 HWND 存活、退出后 HWND 消失、调试层/GBV 与 finite CLI 输出。

正常路径已在 RTX 5080 验证，完整证据在 STATE。未人为诱发 TDR、设备移除、GPU 超时、OOM 或真实跨屏 DPI 切换；这些范围不能根据正常测试宣布已实测。

代价：当前仅单窗口/单主线程，遇到致命 GPU 故障退出而非恢复。重审触发：M2/M3 需要后台编译、多窗口、离屏 GPU、稳定故障恢复或渲染编排变复杂。扩展前保留无 GPU 测试边界和资源所有权规则。
