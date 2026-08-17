# VisionFive 2 实板验收

## 固定启动链

课程固定 StarFive BSP `VF2_v3.8.2`。BootROM 加载 SPL，SPL/OpenSBI 进入 U-Boot，
U-Boot 从 SD 卡 FAT 分区读取 `xv6.itb` 并执行 `bootm`。FIT 内核以 S-mode 进入
`0x40200000`，`a0` 是启动 hart ID，`a1` 是 FIT 中经过哈希校验的 DTB。

DTB 是唯一硬件地址来源。内核要求 VisionFive 2 compatible、DRAM、reserved-memory、
四个可用 U74、UART0、PLIC 和 SDIO1 节点，并探测 SBI TIME、IPI、RFENCE、HSM、SRST。
缺失或不支持即停止启动。启动 hart 完成解析后，通过 SBI HSM 按 DT hart 顺序释放其余
三个 U74；固件提前释放 secondary hart 会被视为错误。

## 存储契约

QEMU 后端是 virtio-mmio；VisionFive 2 后端是 `starfive,jh7110-sdio` 的 DesignWare
控制器 PIO 状态机。所有 reset、command、data 与 busy 等待都有上限，控制器或介质
错误不会回退到其他设备。

SD 镜像使用主、备 GPT。第一个分区是 FAT16 `xv6boot`，包含哈希 FIT；第二个分区使用
Linux filesystem-data type GUID 且名称必须为 `xv6fs`。内核验证 protective MBR、GPT
header CRC、entry-array CRC、GUID、名称、唯一性和范围后才获得分区起点，不包含固定
LBA。

SD 控制器实现依据公开 Synopsys DesignWare 寄存器协议独立编写，没有复制 Linux 或
StarFive GPL 驱动。`third_party/libfdt` 来自 DTC `v1.7.2` 的 BSD-2-Clause 选择，来源
revision 与许可保存在 vendored 目录及 `hardware/visionfive2/sources.lock`。

## 实板门禁

准备 VisionFive 2 v1.3B、3.3V USB-UART 和可写 SD 卡，安装固定 `pyserial==3.5`，设置
`VOS_VF2_BOARD_ALIAS` 与 `VOS_VF2_SERIAL_PORT` 后运行 hardware runner。验收必须同时
满足：

1. U-Boot 从 SD FAT 分区读取并校验 FIT；
2. `XV6_BOOT_OK` 出现，三个 secondary 的启动消息齐全；
3. shell 执行完整 `usertests` 并输出 `ALL TESTS PASSED`；
4. 脱敏日志 hash 与 evidence JSON 由人工复核；
5. evidence 绑定当前 clean HEAD、spec hash 和 config hash。

仓库已保留 2026-08-16 的 VisionFive 2 四核实板记录：SPI U-Boot 启动后，经 TFTP
加载内核与 DTB，以 SD 卡作为 xv6 文件系统，并在四个 U74 hart 上完成完整
`usertests`，最终输出 `ALL TESTS PASSED`。原始串口分段、关键行摘要与采集说明位于
`hardware/visionfive2/evidence/`。自动记录仍不能代替教师复核；课程提交只有在 Portal
中完成证据上传和人工审批后，才从待复核状态闭合为 complete。
