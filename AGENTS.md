# xv6-spec 项目约束

这是一个完整源码参考项目，同时是 VeriSpecOSLab 学生 v2 主链的可运行样例。

- 规格只允许 `spec/design.yaml`、`spec/modules/**/*.yaml`、`spec/interfaces/**/*.yaml`、可选 `spec/goals/**/*.yaml` 和手写 `spec/patches/**/*.yaml`。
- 不要恢复 OperationSpec、独立 ConcurrencySpec、架构切片、Verification Matrix、旧 ToolchainSpec 或 `.vos/project.yaml`。
- ModuleSpec 的 `owns` 必须是仓库相对路径；实现 Agent 不得修改目标模块和已提交 SpecPatch 影响模块之外的文件。
- `vos.yaml` 是结构化 argv 执行投影，不是 shell 字符串。QEMU 必须使用非图形串口输出。
- `.vos/`、构建产物、`.env` 和本机绝对路径不得进入 Git。
- `spec check`、`verify`、`report` 和 `submit` 必须保留确定性和可追溯证据；错误不得被静默吞掉。
- Agent 的 detached worktree 只提供 Git 回滚能力，不是安全沙箱。Agent 可以继承宿主网络、凭据和当前用户权限。
- 修改规格、Makefile、公开验证脚本或文档后，先运行 `vos --project-root examples/xv6-spec spec check`，再按可用环境运行增量构建和验证。
- 课程入口是 orphan-root Lab 1–10 历史；每个课程标签必须通过完整 tree 泄露审计。
- Lab 9/10 目前只能标记 candidate。没有 VisionFive 2 四核实体 `usertests` 和人工复核证据时，不得改称 complete。
