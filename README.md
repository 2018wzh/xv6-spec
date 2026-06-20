# xv6-spec: 从零生成、构建并运行 xv6

`examples/xv6-spec` 是一个“只有规格、没有内核源码”的示例工程。它的目标不是在仓库里直接附带一份手写的 xv6，而是把 `spec/` 作为真相源，通过 `vos` 的 skeleton projection + module generation 流程，从零生成整个 xv6-riscv MVP，然后再构建、启动和验证。

当前规格的最终阶段是 `full-syscall`。对这个阶段执行一次完整生成，会按依赖顺序覆盖：

- `kernel/headers`
- `kernel/boot`
- `kernel/memory`
- `kernel/trap`
- `kernel/process`
- `user/programs`
- `kernel/syscall`
- `kernel/bio`
- `kernel/log`
- `kernel/fs`
- `kernel/file`
- `kernel/exec`
- `kernel/pipe`
- `kernel/uart`
- `kernel/plic`
- `kernel/console`
- `kernel/printk`
- `kernel/virtio`
- `kernel/sysfile`
- `kernel/sysproc`
- `user/headers`
- `user/stubs`
- `user/lib`
- `user/tests`

也就是说，`vos agent generate --apply` 是“从零生成整个当前 xv6 MVP”的入口。在这个示例里，省略 target 会默认回落到当前 stage，也就是 `full-syscall`。

## 工程现状

仓库里的 `examples/xv6-spec` 初始只包含：

- `spec/`: 架构、模块、工具链、验证规格
- `.vos/config.toml`: agent/provider 配置
- `.vos/runs/`: 运行证据与生成记录

初始状态下没有这些待生成文件：

- `include/*.h`
- `kernel/*.c`
- `kernel/*.S`
- `kernel/link.ld`
- `user/init.c`
- `user/user.ld`

因此，不能把这个示例当成“已有源码，直接 `vos build`”的项目看待。正确顺序是：

1. 校验 spec
2. 生成 skeleton 与模块实现
3. 构建
4. 运行 QEMU
5. 执行公开验证

## 前置依赖

### 1. Bun / TypeScript VOS CLI

目标实现使用 Bun / TypeScript workspace。需要能在仓库根目录执行：

```powershell
cd vos
bun install
bun run typecheck
```

当 `vos-cli` package 落地后，推荐把 `vos` 安装到 PATH，或从仓库根目录使用目标 wrapper：

```powershell
bun run vos -- --help
```

### 2. 构建工具链

按 `spec/toolchain/profile.yaml` 与运行时实现，至少需要：

- `gcc`
- `ld`
- `ar`
- `make`
- `riscv64-unknown-elf-objcopy`
- `riscv64-unknown-elf-objdump`
- `qemu-system-riscv64`

运行下面的命令可以先检查工具链规格是否被正确解析：

```powershell
vos --project-root examples/xv6-spec toolchain lint
```

### 3. Agent / LLM 提供方

从零生成源码需要可用的 LLM provider。当前项目默认配置在 `.vos/config.toml`：

```toml
spec_root = "spec"

[agent]
provider = "deepseek"
model = "deepseek-v4-pro"
base_url = "https://api.deepseek.com/v1"
timeout_secs = 120

[agent.auth]
env = "DEEPSEEK_API_KEY"
```

因此至少要保证：

```powershell
$env:DEEPSEEK_API_KEY="..."
```

如果你改用别的 provider，需要同步修改 `.vos/config.toml`。

## 推荐调用方式

下面所有命令都假设你在仓库根目录 `VeriSpecOSLab` 下执行。

统一格式如下：

```powershell
vos --project-root examples/xv6-spec <subcommand>
```

如果尚未把 `vos` 安装到 PATH，目标 TypeScript workspace 应提供等价调用：

```powershell
bun run vos -- --project-root examples/xv6-spec <subcommand>
```

如果你已经在 `examples/xv6-spec` 目录中，也可以直接运行：

```powershell
vos <subcommand>
```

## 第 1 步：先确认 spec 是自洽的

建议至少先跑这几条：

```powershell
vos --project-root examples/xv6-spec spec check-consistency spec
vos --project-root examples/xv6-spec arch compose spec/architecture/seed.yaml
vos --project-root examples/xv6-spec agent plan --stage syscall
```

这几条分别会确认：

- `spec/` 全量规格可被加载并通过一致性检查
- 当前架构阶段是 `syscall`
- 生成波次是 `kernel/headers -> kernel/boot -> kernel/memory -> kernel/trap -> kernel/process -> user/programs -> kernel/syscall`

如果只想看 agent 视角下允许生成哪些文件，可以再执行：

```powershell
vos --project-root examples/xv6-spec agent context --stage syscall
```

## 第 2 步：从零生成整个 xv6 MVP

这是最关键的一步：

```powershell
vos --project-root examples/xv6-spec agent generate --apply
```

这条命令会做几件事：

1. 规范化并检查 `spec/`
2. 先做 skeleton projection，创建最小可编辑源码骨架
3. 按依赖波次为各模块生成 `editable_region`
4. 先应用 skeleton/base 批次，再按 generation wave 逐波把生成结果写入工作区
5. 在 `.vos/runs/<run-id>/` 下记录 manifest、计划、验证报告与重试记录

生成完成后，工作区里应当出现至少这些文件族：

- `include/*.h`
- `kernel/boot.c`
- `kernel/memory.c`
- `kernel/process.c`
- `kernel/trap.c`
- `kernel/syscall.c`
- `kernel/entry.S`
- `kernel/kernelvec.S`
- `kernel/trampoline.S`
- `kernel/swtch.S`
- `kernel/link.ld`
- `user/init.c`
- `user/user.ld`

如果你只想生成单个模块，也可以显式传模块名，比如：

```powershell
vos --project-root examples/xv6-spec agent generate kernel/memory --apply
```

如果你想显式指定当前 stage，也仍然可以写成 `agent generate syscall --apply`。但默认推荐写法是省略 target，让命令按当前 stage 生成整个系统。

## 第 3 步：生成后先做一次 dry-run 构建

本地构建系统由 `vos agent generate --apply` 在工作区里直接生成，并把当前生效 manifest 写到 `.vos/toolchain.json`。如果 agent 已经生成了 `Makefile`、`CMakeLists.txt` 或 `xtask/Cargo.toml` 等允许的构建入口，但还没有写出 `.vos/toolchain.json`，`vos toolchain lint`、`vos build`、`vos run qemu` 和 `vos verify` 会根据 `spec/toolchain/build.yaml` 与 `spec/toolchain/run.yaml` 物化一份默认 manifest，并在 `generator` 字段标记来源为 `vos-cli/default-toolchain-manifest`。

`vos build --dry-run` 负责读取当前 manifest 并展示将执行的入口目标：

```powershell
vos --project-root examples/xv6-spec build --dry-run
```

这一步不会编译源码，但会：

- 解析 `spec/toolchain/toolchain.yaml`
- 读取 `.vos/toolchain.json`
- 校验项目根中已生成的构建系统文件是否存在
- 校验这些文件是否属于 `spec/toolchain/build.yaml` 中 `build.allowed_output_path` 声明的允许列表
- 告诉你最终会按什么目标顺序执行

当前示例在 `spec/toolchain/build.yaml` 中声明了：

```yaml
build:
  allowed_output_path:
    - Makefile
    - CMakeLists.txt
    - xtask/src/tasks.rs
    - xtask/Cargo.toml
```

这表示 agent 可以在这些路径里选择一种或多种本地构建系统文件来落盘，但不能写到列表之外的位置。

## 第 4 步：真正构建内核

```powershell
vos --project-root examples/xv6-spec build
```

当前工具链规格要求最终产物至少包括：

- `build/kernel.elf`
- `build/kernel.bin`
- `build/kernel.asm`

构建日志和阶段日志会写到：

- `.vos/runs/<run-id>/build.log`
- `.vos/runs/<run-id>/phase-*.log`

## 第 5 步：启动 QEMU

```powershell
vos --project-root examples/xv6-spec run qemu
```

`spec/toolchain/run.yaml` 当前绑定的是：

- emulator: `qemu-system-riscv64`
- machine: `virt`
- success signal: `XV6_BOOT_OK`

运行成功的判定方式不是“QEMU 进程还活着”，而是日志里检测到 `XV6_BOOT_OK`。

QEMU 输出会保存到：

- `.vos/runs/<run-id>/qemu.log`

## 第 6 步：执行公开验证

公开验证会串起构建和运行：

```powershell
vos --project-root examples/xv6-spec verify public
```

公开验证要求来自 `spec/verification/public-matrix.yaml`，核心覆盖：

- boot banner
- page allocator
- kernel page table
- trap vector
- timer interrupt
- fork / exit / wait
- syscall dispatch
- `sys_write`
- `sys_fork`
- `sys_sbrk`

## 一条命令直接“生成 + 构建 + 运行”

如果你确认 provider、交叉工具链和 QEMU 都已经就绪，可以直接：

```powershell
vos --project-root examples/xv6-spec agent generate --apply --build --run
```

这个命令等价于：

1. 从零生成整个当前阶段 xv6
2. 执行 `vos build`
3. 执行 `vos run qemu`

注意：

- `--build` 依赖 `--apply`
- `--run` 依赖 `--build`

这是 CLI 目标实现中的硬约束。

## 目录说明

```text
examples/xv6-spec/
├── spec/                  # 唯一真相源
│   ├── architecture/      # 架构 seed、slice、composition、ADR
│   ├── modules/           # 聚合模块、叶子模块、操作契约、并发约束、测试绑定
│   ├── toolchain/         # build/link/run/profile/debug 规格
│   ├── verification/      # public matrix、evidence schema、report contract
│   ├── goals/             # 目标与验收
│   └── evolution/         # spec patch
├── .vos/
│   ├── config.toml        # agent/provider 配置
│   ├── cache/             # 规格归一化缓存
│   └── runs/              # 每次生成/构建/运行/验证的证据
├── include/               # 生成后出现
├── kernel/                # 生成后出现
├── user/                  # 生成后出现
└── build/                 # 构建后出现
```

## 当前实现边界

这份 README 描述全 TypeScript `vos` CLI 的目标链路。当前仓库已经具备 `apps/vos-agent` 与 `apps/vos-web`，但 `packages/vos-cli`、`packages/vos-runtime`、`packages/vos-agent-core` 等 package 仍需按设计文档落地。

下面这些命令不应纳入“从零生成并跑起来”的首个主流程：

- `vos trace syscall`
- `vos report generate`
- `vos submit pack`
- `vos verify full`
- `vos verify fuzz`
- `vos verify invariant`

## 最短复现路径

如果你只想从空工程一路跑到可启动的 xv6，按下面顺序执行即可：

```powershell
vos --project-root examples/xv6-spec toolchain lint
vos --project-root examples/xv6-spec spec check-consistency spec
vos --project-root examples/xv6-spec agent generate --apply
vos --project-root examples/xv6-spec build
vos --project-root examples/xv6-spec run qemu
vos --project-root examples/xv6-spec verify public
```

如果你已经具备稳定的 provider 与交叉工具链环境，也可以直接：

```powershell
vos --project-root examples/xv6-spec agent generate --apply --build --run
```

## 可选真实复现检查

默认自动化测试应使用 fake/headless runner 与 dry-run 路径，不依赖真实 LLM、RISC-V 工具链或 QEMU。若本机已经配置好 `DEEPSEEK_API_KEY`、交叉工具链与 `qemu-system-riscv64`，可以手动执行真实链路：

```powershell
cd vos
bun install
bun run typecheck
bun test
vos --project-root ../examples/xv6-spec agent generate --apply --build --run
vos --project-root ../examples/xv6-spec verify public
```
