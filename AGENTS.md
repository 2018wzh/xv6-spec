# Lab 2 项目约束

本标签在完整保留 Lab 1 CTF 热身的基础上，加入 Lab 2 的 `kernel/boot` 启动链。不要提前加入 Lab 3 及后续实验的模块、接口、测试或术语。

- `spec/design.yaml`、三个 ModuleSpec 和两份手写 SpecPatch 是本阶段的设计依据。学生先讨论问题，再手写 Spec，依次运行 `vos spec lint` 和只读的 `vos agent review`，最后用普通 Git 命令提交。
- 新写的 Spec 在实现前可以没有 `vos.yaml` check 映射。`vos agent implement` 必须先验证结构化 target 提案，再由 VOS 原子更新 `vos.yaml`。
- 每个可观察 property 都要映射到确定性检查或明确的 oracle。静态源码扫描验证没有硬编码 flag；contract check 比较 Linux 与裸机读取器的长度、哈希和输出顺序。
- `lab/ctf-warmup` 负责读取器行为与生成测试；`toolchain` 负责 Makefile、构建产物、结构化命令和 QEMU 串口 oracle。跨模块绑定只通过已提交的 `lab1-ctf-toolchain` SpecPatch 生效。
- `kernel/boot` 负责机器态入口、PMP 切换、监督态入口和串口启动标记；它对 `toolchain`、`vos.yaml` 和共享启动测试框架的影响由已提交的 `lab2-bootstrap-toolchain` SpecPatch 约束。
- `tests/public/verify.sh` 与 `tests/public/lab2-boot.sh` 属于 `toolchain` 的共享公共测试框架；`tests/generated/kernel/boot` 是 Agent 为 `kernel/boot` 生成并跟踪的具体测试，不是可丢弃的构建产物。
- 所有测试使用生成的非秘密 fixture。实现、测试和日志不得写入课程 flag，也不得把本地完整路径写进仓库。
- `lab1/build` 和 `.vos` 都是可丢弃的本地产物，不进入 Git。Agent 的 detached worktree 便于回滚 Git 改动，但不是进程、网络、凭据或宿主文件的安全边界。
- 验收顺序为 `vos spec lint all`、`vos build`、`vos run qemu`、`vos verify`、`vos verify --hidden`、`vos report` 和 `vos submit`。任何失败都必须保留原始诊断，不得静默跳过。
