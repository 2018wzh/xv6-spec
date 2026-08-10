# Lab 1 项目约束

本标签只包含 CTF 热身及其构建、测试和 QEMU 运行契约。不要提前加入后续实验的模块、接口、测试或术语。

- `spec/design.yaml`、两个 ModuleSpec 和手写 SpecPatch 是本阶段的设计依据。学生先讨论问题，再手写 Spec，依次运行 `vos spec lint` 和只读的 `vos agent review`，最后用普通 Git 命令提交。
- 新写的 Spec 在实现前可以没有 `vos.yaml` check 映射。`vos agent implement` 必须先验证结构化 target 提案，再由 VOS 原子更新 `vos.yaml`。
- 每个可观察 property 都要映射到确定性检查或明确的 oracle。静态源码扫描验证没有硬编码 flag；contract check 比较 Linux 与裸机读取器的长度、哈希和输出顺序。
- `lab/ctf-warmup` 负责读取器行为与生成测试；`toolchain` 负责 Makefile、构建产物、结构化命令和 QEMU 串口 oracle。跨模块绑定只通过已提交的 `lab1-ctf-toolchain` SpecPatch 生效。
- 所有测试使用生成的非秘密 fixture。实现、测试和日志不得写入课程 flag，也不得把本地完整路径写进仓库。
- `lab1/build` 和 `.vos` 都是可丢弃的本地产物，不进入 Git。Agent 的 detached worktree 便于回滚 Git 改动，但不是进程、网络、凭据或宿主文件的安全边界。
- 验收顺序为 `vos spec lint all`、`vos build`、`vos run qemu`、`vos verify`、`vos verify --hidden`、`vos report` 和 `vos submit`。任何失败都必须保留原始诊断，不得静默跳过。
