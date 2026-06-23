# xv6-spec 示例

`examples/xv6-spec` 是 VeriSpecOSLab 的参考项目。它用 `spec/` 描述一个 xv6 风格的 RISC-V 内核，再用 `vos` 完成检查、生成、构建、启动 QEMU 和公开验证。

这份 README 只讲这个示例怎么跑。通用命令和概念见 [用户手册](../../docs/user-manual.md)。

## 当前目录里有什么

这个示例包含三类内容：

- `spec/`：架构、模块、工具链、验证矩阵和报告契约。
- `.vos/`：项目配置、策略、toolchain manifest 和运行证据。
- `kernel/`、`user/`、`Makefile`、`build/`：当前 checkout 中可能已经存在的生成源码和构建产物。

如果你拿到的是干净的 spec-only 版本，可能没有 `kernel/`、`user/` 或 `Makefile`。这种情况下要先运行 `agent generate --apply`，再构建。

## 前置条件

先准备 Bun：

```bash
cd vos
bun install
bun run vos -- --help
```

如果要真正构建和运行 xv6，还需要 RISC-V 工具链、`make` 和 QEMU。常见命令包括：

- `riscv64-unknown-elf-objcopy`
- `riscv64-unknown-elf-objdump`
- `qemu-system-riscv64`

如果要从 spec 生成源码，还需要配置 `.vos/config.toml` 中声明的 LLM provider。当前示例默认读取 `DEEPSEEK_API_KEY`。

## 最短路径

下面的命令都从 `vos/` 目录运行，项目路径写作 `../examples/xv6-spec`。

先检查工具链和 spec：

```bash
bun run vos -- --project-root ../examples/xv6-spec toolchain lint
bun run vos -- --project-root ../examples/xv6-spec spec check-consistency
```

看构建计划：

```bash
bun run vos -- --project-root ../examples/xv6-spec build --dry-run
```

真正构建：

```bash
bun run vos -- --project-root ../examples/xv6-spec build
```

启动 QEMU：

```bash
bun run vos -- --project-root ../examples/xv6-spec run qemu --case boot-smoke
```

执行公开验证：

```bash
bun run vos -- --project-root ../examples/xv6-spec verify public
```

## 从 spec 生成源码

如果当前目录没有源码或构建入口，先看 Agent 能看到的上下文和计划：

```bash
bun run vos -- --project-root ../examples/xv6-spec agent context --scope public
bun run vos -- --project-root ../examples/xv6-spec agent plan --stage boot
```

生成并写入工作区：

```bash
bun run vos -- --project-root ../examples/xv6-spec agent generate --apply
```

只生成某个目标时，把目标写出来：

```bash
bun run vos -- --project-root ../examples/xv6-spec agent generate kernel/memory --apply
```

确认 provider、工具链和 QEMU 都可用后，也可以一条命令完成生成、构建和运行：

```bash
bun run vos -- --project-root ../examples/xv6-spec agent generate --apply --build --run
```

注意：`--build` 依赖 `--apply`，`--run` 依赖 `--build`。

## QEMU case 和测试 suite

查看可用运行配置：

```bash
bun run vos -- --project-root ../examples/xv6-spec run qemu --list-profiles
bun run vos -- --project-root ../examples/xv6-spec run qemu --list-cases
```

当前 `.vos/toolchain.json` 中常用 case：

- `boot-smoke`：启动到 shell。
- `usertests`：启动后输入 `usertests`，等待 `ALL TESTS PASSED`。

运行 usertests case：

```bash
bun run vos -- --project-root ../examples/xv6-spec run qemu --case usertests
```

运行一个公开测试 suite：

```bash
bun run vos -- --project-root ../examples/xv6-spec test --suite usertests_all_pass
```

先查看公开验证计划：

```bash
bun run vos -- --project-root ../examples/xv6-spec verify public --dry-run
```

## 目录说明

```text
examples/xv6-spec/
├── spec/
│   ├── architecture/      # 架构 seed、slice、composition、ADR
│   ├── modules/           # 模块和操作级约束
│   ├── toolchain/         # build、run、profile、debug 等规格
│   ├── verification/      # public matrix、evidence schema、report contract
│   ├── goals/             # 目标和验收
│   └── evolution/         # SpecPatch
├── .vos/
│   ├── config.toml        # Agent/provider 配置
│   ├── policy.yaml        # 本地命令和路径策略
│   ├── project.yaml       # 项目元数据和当前阶段
│   ├── toolchain.json     # 当前生效的运行时 manifest
│   └── runs/              # 每次命令留下的 evidence
├── kernel/                # 当前 checkout 中的内核源码
├── user/                  # 用户态程序
├── tests/public/          # 公开验证脚本
└── build/                 # 构建产物
```

## evidence 怎么看

每次执行类命令都会在 `.vos/runs/<run-id>/` 下留下证据。常看这几个位置：

- `manifest.json`：命令、状态、时间、产物和 evidence ref。
- `events.jsonl`：事件流水。
- `artifacts/`：构建日志、QEMU 日志、验证摘要、生成计划或行为测试结果。

例如 QEMU 失败时，先找本次 run 的 artifacts，再看是否出现 success regex 要求的输出。

## 常见问题

### `build` 找不到工具

确认 RISC-V 工具链和 QEMU 已安装，并且在 `PATH` 中。不同系统的包名不一样，课程环境或 DevBox 通常会预装。

### `agent generate` 调 provider 失败

检查 `.vos/config.toml` 和对应环境变量。当前示例默认使用 `DEEPSEEK_API_KEY`。

### `verify public` 没有跑到预期测试

先看 `.vos/toolchain.json` 里的 `test.suites` 和 `verify` 映射，再看 `spec/verification/public-matrix.yaml`。公开验证只会运行当前 manifest 能映射到的 suite。

### QEMU 启动了但 `run qemu` 失败

`vos run qemu` 判断的是日志里的成功信号。`boot-smoke` 需要看到 shell 启动输出，`usertests` 需要看到 `ALL TESTS PASSED`。
