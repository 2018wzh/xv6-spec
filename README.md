# xv6-spec 课程参考项目

本仓库按 Lab 1–10 保存一条可执行、可审计的课程历史。每个 `course/labN-*`
注释标签都是一个累计课程切片：树中只允许出现该 Lab 已经讲授的设计、规格、源码、
公开检查和文档。课程入口是新 `main`；重建前的源码只通过 archive 标签恢复，不在
课程主线的祖先链中。

规格采用学生 v2 五类文件：

- `spec/design.yaml`：随课程逐步演进的系统设计；
- `spec/modules/**/*.yaml`：模块责任、所有权与不变量；
- `spec/interfaces/**/*.yaml`：跨模块 driver、ABI 与 syscall 边界；
- `spec/goals/*.yaml`：可由确定性 oracle 判定的行为目标；
- `spec/patches/*.yaml`：当期跨模块设计变化及回归范围。

`vos.yaml` 是唯一结构化执行投影，不是 shell 脚本。`.vos/` 仅保存本地证据并被 Git
忽略。

## 课程边界

| Lab | 累计主题 | 发布边界 |
| --- | --- | --- |
| 1 | 空项目、身份、RISC-V/GNU C 与当期启动目标 | complete |
| 2 | QEMU 启动、串口 banner | complete |
| 3 | 页分配、对齐、耗尽、Sv39 内核映射 | complete |
| 4 | trap、timer、PLIC、UART | complete |
| 5 | 进程、trap-frame、syscall | complete |
| 6 | virtio、buffer cache、log、inode、exec | complete |
| 7 | resource ABI、fd、pipe、shell | complete |
| 8 | 上游 xv6 可观察行为兼容与完整 `usertests` | complete |
| 9 | VisionFive 2、DTB/SBI/HSM、SD/GPT、FIT 与硬件 runner | candidate |
| 10 | 累计投影、发布审计、report/submit | candidate |

Lab 9–10 的 candidate 只表示代码、静态契约、镜像检查和完整 QEMU 回归通过。当前没有
VisionFive 2、USB-UART 与可写 SD 卡，因而没有实体板四核 `usertests` 证据，也没有
人工复核结论。QEMU、模拟串口或编译成功都不能替代这项门禁。

## 常用命令

```sh
vos spec check
vos doctor
vos build
vos run qemu
vos verify
vos report
vos submit
python3 tools/course_history_audit.py
```

真实 QEMU 验收需要 Bash、Perl、GNU make、RISC-V GNU 工具链和
`qemu-system-riscv64`。`usertests_all_pass` 最长允许 900 秒，以覆盖低速文件系统上的
完整套件；成功 oracle 仍只有串口中的 `ALL TESTS PASSED`。

## VisionFive 2 candidate

硬件构建固定引用 StarFive `VF2_v3.8.2` DTB 和 vendored `libfdt v1.7.2`。构建会先
校验来源哈希，再生成包含 S-mode 内核与 DTB 的 SHA-256 FIT，最后生成带 FAT 启动分区
和 GPT `xv6fs` 分区的 SD 镜像：

```sh
make vf2-image
make vf2-image-check
python3 -m pip install --requirement requirements-hardware.txt
VOS_VF2_BOARD_ALIAS=visionfive2-v1.3b \
VOS_VF2_SERIAL_PORT=/dev/ttyUSB0 vos run hardware
```

硬件 runner 缺少任一环境配置会立即失败。它驱动 U-Boot 加载 `xv6.itb`，检查四个
U74 hart 和完整 `usertests`，只保存脱敏串口日志及哈希；即使自动检查全部通过，结果
也固定为 `pending_human_review`。详见 `docs/visionfive2.md`。

## 隐私与可追溯性

提交前保持 clean HEAD。`verify`、authoritative hardware evidence 和 `submit` 都受
clean-tree/current-HEAD 门禁保护。报告只应记录 commit、spec/config hash、检查状态、
计数和脱敏诊断；不得提交 `.vos/`、串口名、绝对路径、凭据或原始私有载荷。
