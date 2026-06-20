# TODO: 剩余缺口

> 当前阶段只覆盖 boot、memory、trap、process、syscall 五个核心阶段。
> 每个模块都有 `module.yaml`，每个生成目标都是一个 `editable_region` 操作。
> 省略 `target` 时，默认按当前 stage 生成整个当前系统；显式传模块名时只生成该模块及其依赖闭包。

## 生成器注意事项

| 操作类型 | 示例 | editable_region 指向 | 说明 |
|---------|------|---------------------|------|
| C 函数 | kernel/memory.kalloc | kernel/memory.c | 标准代码生成 |
| 汇编函数 | kernel/process.swtch | kernel/swtch.S | 需生成 RISC-V 汇编 |
| 头文件 | kernel/headers.types | include/types.h | 需生成 C 头文件 |
| 链接脚本 | kernel/headers.link_ld | kernel/link.ld | 需生成 GNU ld 脚本 |
| 用户程序 | user/programs.init | user/init.c | 需生成 freestanding 用户 C 代码 |
| 用户链接脚本 | user/programs.user_ld | user/user.ld | 需生成用户态 ld 脚本 |

生成器需要根据 `editable_region.file` 的扩展名选择目标语言/格式。
`guarantee.declarations` 或 `guarantee.linker_sections` 字段提供结构化生成指导。

## 轻量未覆盖项

- `include/defs.h` 的函数声明应与各模块 ops 同步（当前手动维护在 `kernel/headers.defs` 中）
- `user/programs` 仍只包含 init 和 user.ld
- 未来的文件系统、pipe、设备驱动和完整 syscall 扩展将在下一阶段引入

## 已全部由 spec 覆盖

以下所有项均通过对应的 OperationContract 覆盖，无需手写代码：
- ✅ 早期 5 个阶段的核心内核操作（kernel/boot、kernel/memory、kernel/trap、kernel/process、kernel/syscall）
- ✅ 8 个头文件 + 1 个链接脚本（kernel/headers 模块）
- ✅ 1 个用户程序 + 1 个用户链接脚本（user/programs 模块）
- ✅ 3 个汇编文件（entry.S, kernelvec.S, trampoline.S, swtch.S）
