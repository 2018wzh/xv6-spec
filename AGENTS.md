# xv6 课程工作区约束

学生先围绕设计问题使用 `vos agent ask` 讨论，再手写 Spec，依次运行 `vos spec lint` 和只读的 `vos agent review`，根据建议修改后用普通 Git 命令提交。`vos agent implement` 只能以已提交的 Spec 为依据生成实现与测试，不能替学生生成或修改 Spec。

- 新写的 Spec 在实现前可以没有 `vos.yaml` check 映射。Agent 必须通过结构化提交工具返回 target 提案，VOS 验证 `program`、`args`、`cwd`、`env`、`timeout`、`verifies` 和路径后再原子更新 `vos.yaml`，Agent 不直接编辑 manifest。
- 每个可观察 property 都要通过稳定、模块前缀化的 check ID 绑定 public、contract、固定种子 fuzz 或有界 trace/oracle 证据。失败必须保留原始诊断，不能静默跳过。
- SpecPatch 新声明的 check ID 必须在实现前写入对应 ModuleSpec 的 property 文本；Patch、ModuleSpec 和结构化 target 提案使用同一个稳定 ID，不能只在 Patch 中临时命名。
- Lab 7 的跨模块 check 按被观察的状态迁移确定所有者：进程树与 fork/exit/wait 使用 `kernel_process_*`，pipe 的 FIFO、对端关闭和由 pipe 驱动的回收使用 `kernel_pipe_*`。`verifies` 可以同时引用多个 Spec，但 target ID 保留所有者模块前缀。
- Lab 1 的 CTF 测试只使用生成的非秘密 fixture；实现、测试和日志不得写入课程 flag，也不得把本机绝对路径写进仓库。`lab/ctf-warmup` 负责读取器行为，`toolchain` 负责构建、结构化命令和 QEMU 串口 oracle。
- `kernel/boot` 负责机器态入口、PMP 切换、监督态入口和串口启动标记；它对 `toolchain`、`vos.yaml` 和共享启动测试框架的影响由已提交的 `lab2-bootstrap-toolchain` SpecPatch 约束。
- `tests/public/verify.sh` 与 `tests/public/lab2-boot.sh` 属于 `toolchain` 的共享公共测试框架；`tests/generated/kernel/boot` 是 Agent 为 `kernel/boot` 生成并跟踪的具体测试，不是可丢弃的构建产物。
- 跨 InterfaceSpec 边界的 ModuleSpec 操作应与接口操作名一致，或明确说明聚合映射。描述符槽位存储属于 `kernel/process`，槽位引用的对象属于 `kernel/file`；裸块、inode、日志和磁盘耗尽仍由对应存储模块负责，即使错误最终通过文件系统调用暴露。
- 只要 `rely` 明确列出对应的接口操作和下层责任，ModuleSpec 可以用不直接暴露为 ABI 调用的内部聚合操作描述容量等横切语义。
- `.vos`、`lab1/build` 和其他构建目录都是可丢弃的本地产物，不进入 Git。Agent 的 detached linked worktree 只提供 Git 改动回滚，不是进程、网络、凭据或宿主文件系统的安全边界；宿主命令继承当前用户和网络权限。
- 累计验收顺序为 `vos spec lint all`、`vos build`、`vos run qemu`、`vos verify`、`vos verify --hidden`、`vos report` 和 `vos submit`。只有 clean HEAD 证据可以提交。
