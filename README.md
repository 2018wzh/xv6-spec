# xv6-spec 参考项目

`examples/xv6-spec` 是完整源码参考项目，现已迁移到学生 v2 主链。它保留 xv6 风格 RISC-V 内核、用户程序和公开验证脚本，但规格层只使用五类文件：

- `spec/design.yaml`：系统目标、GNU C、RISC-V、QEMU virt 和硬件移植约束。
- `spec/modules/<module>.yaml`：31 个 ModuleSpec；每个文件合并操作、性质、错误、并发和测试绑定。
- `spec/interfaces/<interface>.yaml`：syscall、IPC、driver 和 ABI 边界。
- `spec/goals/<goal>.yaml`：核心功能目标与确定性验收口径。
- `spec/patches/<patch>.yaml`：两次架构演进及影响模块；VOS 会从 changes 推导回归范围。

`vos.yaml` 是唯一执行投影，保存结构化 argv、超时、产物、QEMU 串口 Runner、公开检查和 Spec ID 覆盖。旧的 Architecture Seed/Timeline/Slice、独立 OperationSpec、ConcurrencySpec、Verification Matrix、ToolchainSpec 和 `.vos/project.yaml` 已从项目中删除。`.vos/` 只保存本地运行证据并被 Git 忽略。

## 主链

从仓库根目录运行：

```sh
vos --project-root examples/xv6-spec spec check
vos --project-root examples/xv6-spec doctor
vos --project-root examples/xv6-spec build
vos --project-root examples/xv6-spec run qemu
vos --project-root examples/xv6-spec verify
vos --project-root examples/xv6-spec report
vos --project-root examples/xv6-spec submit
```

修改某个模块时，先保证 clean HEAD 和已提交 ModuleSpec：

```sh
vos --project-root examples/xv6-spec agent spec kernel/memory
vos --project-root examples/xv6-spec agent spec kernel/memory --confirm
vos --project-root examples/xv6-spec agent implement kernel/memory
```

`agent spec` 只有在确认后才提交规格。`agent implement` 在 detached linked worktree 中构建、执行对应公开检查和契约检查，成功后只把 `owns` 范围内的变更原子提交回原工作树。worktree 只是 Git 变更回滚机制，不隔离进程、网络、凭据或宿主文件；Agent 命令继承当前用户权限。

## 构建和真实 QEMU 验收

真实构建需要 `make`、Bash、Perl、RISC-V GNU 工具链和 `qemu-system-riscv64`。Makefile 的 QEMU 配置使用 `-nographic` 和 `mon:stdio`，公开脚本通过串口日志判断启动、shell、文件系统、IPC、驱动和 `usertests` 结果：

```sh
make all
vos --project-root examples/xv6-spec run qemu
vos --project-root examples/xv6-spec verify
```

`verify` 是确定性门禁，不调用模型，不运行 fuzz、trace 或 hidden tests。硬件 Runner 的结果若被配置，提交状态仍是 `pending_human_review`，不能代替人工验收。

## KB 与隐私

`vos.yaml` 锁定 `spec` 相对目录的内容 hash；`agent kb` 只把锁定源同步到 `.vos/kb-sources`。本地 `.env` 只供当前机器使用，不能提交。审计、对话、工具调用、diff 和运行结果写入 gitignored 的 `.vos/audit`；提交归档会遮蔽凭据并替换绝对路径。

参考源码对学生可读，这是策略约束而不是保密边界。
